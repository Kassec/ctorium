#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

// Forward declarations to allow method signatures without circular includes.
namespace ctr { template <class> class Bean; }
namespace ctr { class BeanContext; }

#include "../ContributedDescriptor.hpp"
#include "../Descriptor.hpp"
#include "../DescriptorId.hpp"
#include "../DiscoverContribution.hpp"
#include "../Identity.hpp"
#include "../Lifetime.hpp"
#include "../NameId.hpp"
#include "../TypeId.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/Options.hpp"
#include "DefaultsTable.hpp"
#include "DescriptorTable.hpp"
#include "ListenerStore.hpp"
#include "NameInterning.hpp"
#include "PrototypeStore.hpp"
#include "ResolutionContext.hpp"
#include "SessionStore.hpp"
#include "SingletonStore.hpp"
#include "ThreadLocalStore.hpp"
#include "TypeIndex.hpp"
#include "TypeInterning.hpp"

namespace ctr::detail {

/**
 * @brief Core shared state for a root context: all components owned and coordinated here.
 *
 * `Registry` is the single internal unit that `BeanContext` wraps.  In inline mode
 * (header-only), `BeanContext` holds a `Registry` by value; in PImpl mode, `BeanContext`
 * holds a `Registry*` (owned by the global shared runtime).  The accessor `core()` on
 * `BeanContext` returns `Registry&` in both modes, keeping all template methods
 * (`resolve<T>`, `discover<>`, `typeIdFor<T>`) identical regardless of linkage mode.
 *
 * ### Component ownership
 * `Registry` is the exclusive owner of:
 *  - `DescriptorTable`  — immutable runtime descriptors after `start()`.
 *  - `TypeInterning`    — qualified type name → TypeId.
 *  - `NameInterning`    — named qualifier string → NameId.
 *  - `TypeIndex`        — (TypeId, NameId) → sorted candidate DescriptorIds.
 *  - `DefaultsTable`    — per-TypeId default NameId (thread-safe reads).
 *  - `SingletonStore`   — singleton instances (root lifetime).
 *  - `PrototypeStore`   — prototype instances with refcount slots.
 *  - `ListenerStore`    — listener registrations and lifecycle dispatch.
 *
 * Session beans are owned by `ScopedContext` (each scope has its own `SessionStore`).
 * ThreadLocal beans are deferred to `ThreadLocalStore` (pending ADR).
 *
 * ### start() merge algorithm  (specs-internal §7)
 * 1. For each `ContributedDescriptor` in all submitted spans:
 *    a. Look up `identity` in a local `Identity → DescriptorId` map.
 *    b. If new: intern names and types, append to `DescriptorTable`, update maps.
 *    c. If duplicate: verify key fields match (guards against bugs, not collisions).
 * 2. Resolve each `factoryMethodIdentity` to a `factoryMethodDescriptor`.
 * 3. Sort all candidate vectors by descending priority.
 * 4. Freeze TypeId space.  Resize `DefaultsTable`.
 * 5. Mark context as started.
 *
 * ### Thread safety  (hot path)
 * `resolve<T>()` for singleton/prototype is lock-free after `start()`: it reads
 * `TypeIndex::candidatesFor()` (read-only), checks `SingletonStore::find()`
 * (read-only), and calls `materialize()` under `writeLock_` only when the
 * singleton is already materialized: it reads `SingletonStore::find()` (read-only map).
 * When not yet materialized, `resolve<T>()` uses a two-phase pattern: claim the
 * DescriptorId under `writeLock_`, construct outside the lock, then store under
 * the lock and notify waiters via `cv_`.
 */
class Registry {
public:
    // -------------------------------------------------------------------------
    // Component accessors (used by ScopedContext and specialised engine code)
    // -------------------------------------------------------------------------

    [[nodiscard]] DescriptorTable& descriptorTable()  noexcept { return descriptors_; }
    [[nodiscard]] TypeInterning&   typeInterning()    noexcept { return typeInterning_; }
    [[nodiscard]] NameInterning&   nameInterning()    noexcept { return nameInterning_; }
    [[nodiscard]] TypeIndex&       typeIndex()        noexcept { return typeIndex_; }
    [[nodiscard]] DefaultsTable&   defaultsTable()    noexcept { return defaults_; }
    [[nodiscard]] SingletonStore&  singletonStore()   noexcept { return singletons_; }
    [[nodiscard]] PrototypeStore&  prototypeStore()   noexcept { return prototypes_; }
    [[nodiscard]] ListenerStore&   listenerStore()    noexcept { return listeners_; }

    // -------------------------------------------------------------------------
    // Discovery contribution (pre-start)
    // -------------------------------------------------------------------------

    /**
     * @brief Registers a discovery contribution span for merge at `start()`.
     *
     * May be called multiple times before `start()`.  Each call appends the span
     * to a pending list; spans are processed in submission order during `start()`.
     * Calling after `start()` raises `ContextStateError`.
     *
     * @param contribution Span of `ContributedDescriptor` values with static lifetime.
     * @param options      Discovery options for this contribution.
     * @throws ctr::ContextStateError if already started.
     */
    void submitContribution(DiscoverContribution contribution, DiscoverOptions options) {
        std::lock_guard lock(writeLock_);
        if (started_) {
            throw ctr::ContextStateError(
                "discover<>() called after start() — contributions must be "
                "submitted before the context is started.");
        }
        pending_.push_back({contribution, options});
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Merges all contributions, interns identifiers, validates, sorts, and starts.
     *
     * Idempotent: calling `start()` on an already-started registry is a no-op.
     *
     * On exception (from a user constructor or hook during eager singleton
     * materialization), the registry remains in its pre-`start()` state and
     * `start()` may be called again.
     *
     * @param ctx The owning `BeanContext`; registered as a self-injectable singleton.
     * @throws ctr::ConfigurationError on conflicting contributions.
     * @throws Any exception from eager user constructors / hooks.
     */
    void start(BeanContext* ctx) {
        std::lock_guard lock(writeLock_);
        if (started_.load(std::memory_order_relaxed)) return;

        // --- Phase 1: merge contributions ---
        // Local identity → DescriptorId map, used only during start().
        std::unordered_map<Identity, DescriptorId> identityMap;
        std::vector<std::pair<DescriptorId, Identity>> factoryLinks;
        {
            std::size_t total = 0;
            for (const auto& [span, opts] : pending_) total += span.size();
            identityMap.reserve(total);
            factoryLinks.reserve(total);
        }

        for (const auto& [span, opts] : pending_) {
            for (const ContributedDescriptor& cd : span) {
                auto [it, inserted] = identityMap.emplace(cd.identity, DescriptorId{0});
                if (inserted) {
                    // New descriptor: intern names and types, build runtime Descriptor.
                    // Capture interned ids before std::move(d) invalidates the descriptor.
                    Descriptor d;
                    const TypeId exposedType = typeInterning_.internByName(
                        cd.exposedTypeName, cd.exposedTypeInfo);
                    d.exposedType  = exposedType;
                    d.concreteType = typeInterning_.internByName(
                        cd.concreteTypeName, cd.concreteTypeInfo);
                    const NameId name = nameInterning_.intern(cd.beanName);
                    d.name         = name;
                    d.priority     = cd.priority;
                    d.lifetime     = cd.lifetime;
                    d.origin       = cd.origin;
                    d.construct         = cd.construct;
                    d.destroy           = cd.destroy;
                    d.postConstruct     = cd.postConstruct;
                    d.preDestroy        = cd.preDestroy;
                    d.size              = cd.size;
                    d.align             = cd.align;
                    d.allocAndConstruct = cd.allocAndConstruct;
                    d.dealloc           = cd.dealloc;
                    d.factoryMethodDescriptor = kInvalidDescriptorId; // resolved in Phase 2

                    const DescriptorId id = descriptors_.append(std::move(d));
                    it->second = id;

                    if (cd.factoryMethodIdentity != kNoFactoryMethod) {
                        factoryLinks.push_back({id, cd.factoryMethodIdentity});
                    }

                    // Register in the TypeIndex using locals captured before the move.
                    typeIndex_.insertCandidate(exposedType, name, id);
                }
                // Duplicate: no error — duplicate contributions are expected and correct.
            }
        }

        // --- Phase 2: resolve factory method identities → DescriptorIds ---
        for (const auto& [beanId, factoryMethodIdentity] : factoryLinks) {
            const auto fit = identityMap.find(factoryMethodIdentity);
            if (fit == identityMap.end()) {
                throw ctr::ConfigurationError(
                    std::string("Registry::start(): factory method identity not found "
                                "for bean '") + std::string(typeInterning_.nameOf(descriptors_.at(beanId).concreteType)) + "'.");
            }
            // Write factoryMethodDescriptor into the already-appended Descriptor.
            // Safe: descriptors_ is append-only during start(); the reference is stable.
            const_cast<Descriptor&>(descriptors_.at(beanId))
                .factoryMethodDescriptor = fit->second;
        }

        // --- Phase 3: sort candidate vectors by descending priority ---
        typeIndex_.sortAllCandidates(
            [this](DescriptorId id) { return descriptors_.at(id).priority; });

        // --- Phase 3.5: graph validation ---
        // Condition 4 (specs-internal §7 step 6): two producer methods on the same
        // factory returning the same type and named key → ConfigurationError.
        // factoryIds is declared outside the outer loop to reuse its capacity.
        std::vector<DescriptorId> factoryIds;
        for (TypeId typeId{0};
                static_cast<std::size_t>(typeId) < typeInterning_.size();
                ++typeId) {
            const NameTable* table = typeIndex_.tableFor(typeId);
            if (!table) continue;
            for (const auto& [nameId, candidates] : table->entries) {
                factoryIds.clear();
                for (DescriptorId id : candidates) {
                    const Descriptor& d = descriptors_.at(id);
                    if (d.origin == Origin::FactoryProduct
                            && d.factoryMethodDescriptor != kInvalidDescriptorId) {
                        factoryIds.push_back(d.factoryMethodDescriptor);
                    }
                }
                std::sort(factoryIds.begin(), factoryIds.end());
                if (std::adjacent_find(factoryIds.begin(), factoryIds.end())
                        != factoryIds.end()) {
                    throw ctr::ConfigurationError(
                        "Registry::start(): two producer methods on the same factory "
                        "return the same type and named key: type='"
                        + std::string(typeInterning_.nameOf(typeId))
                        + "' name='"
                        + std::string(nameInterning_.nameOf(nameId))
                        + "'.");
                }
            }
        }

        // --- Phase 4: register BeanContext bean (resizes singletons_), freeze TypeId space ---
        // BeanContext must be interned before freeze(); defaults_ and started_ publish after.
        // singletons_ is resized once inside registerContextBean, after its descriptor is appended.
        registerContextBean(ctx);
        typeInterning_.freeze();
        defaults_.resize(typeInterning_.size());

        // Release store: ensures all structures populated during start() are visible
        // to threads that subsequently read started_ with memory_order_acquire.
        started_.store(true, std::memory_order_release);
        pending_.clear(); // Release contribution spans (no longer needed).
    }

    /**
     * @brief Permanently stops the registry and destroys all owned bean instances.
     *
     * Destroys singletons in reverse construction order via `executeDestructionLifecycle`,
     * then prototypes in reverse slot order.  Listeners are cleared last so that
     * `onPreDestroy`/`onDestroyed` callbacks fire during the sweep.
     * After this call the registry must not be used; `BeanContext::stop()` handles
     * the lifecycle contract at the public API level.
     */
    void stop() noexcept {
        std::lock_guard lock(writeLock_);

        // Mark stopped first so that Bean<T> destructors triggered by d.destroy()
        // calls below see startedRelaxed()==false and skip releaseIfPrototype(),
        // preventing double-destroy when a bean holds a Bean<T> member to another
        // prototype that the sweep has already (or will) destroy.
        started_.store(false, std::memory_order_relaxed);

        // Singletons: reverse insertion order (≈ reverse construction order).
        const auto& order = singletons_.insertionOrder();
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            void* mem = singletons_.find(*it);
            if (mem != nullptr) executeDestructionLifecycle(*it, mem);
        }
        singletons_.releaseAll();

        // Prototypes: reverse slot index (≈ reverse allocation order).
        const std::size_t count = prototypes_.slotCount();
        for (std::size_t s = count; s > 0; --s) {
            void* mem = prototypes_.memoryAt(static_cast<SlotId>(s - 1));
            if (mem != nullptr) {
                executeDestructionLifecycle(
                    prototypes_.descriptorIdAt(static_cast<SlotId>(s - 1)), mem);
            }
        }
        prototypes_.releaseAll();

        listeners_.clear();
    }

    /** @brief True after a successful `start()`, false before or after `stop()`. */
    [[nodiscard]] bool started() const noexcept {
        return started_.load(std::memory_order_acquire);
    }

    // -------------------------------------------------------------------------
    // Named defaults
    // -------------------------------------------------------------------------

    /**
     * @brief Interns a name under the write lock and returns a stable `NameId`.
     *
     * Called by `BeanContext::defaultNamed<T>("x")` after `start()`.
     * The write lock is held only during the intern operation; no other lock is
     * acquired, so this is safe from user code concurrent with `resolve<T>()`.
     *
     * Empty string → `kUnnamed` (fast path, no lock acquired).
     *
     * @param name Name to intern; empty string maps to `kUnnamed`.
     * @return Stable `NameId` for the name.
     */
    [[nodiscard]] NameId internNameSafe(std::string_view name) {
        if (name.empty()) return kUnnamed;
        std::lock_guard lock(writeLock_);
        return nameInterning_.intern(name);
    }

    /**
     * @brief Sets the runtime default `NameId` for the given `TypeId`.
     *
     * Thread-safe.  Called by `BeanContext::defaultNamed<T>("x")`.
     * Sets `hasAnyDefault_` true on the first non-kUnnamed default; never reverts to
     * false (safe over-approximation: a cleared default leaves the flag set).
     *
     * @pre `typeId` must be a valid interned TypeId (i.e., `start()` completed).
     * @param typeId TypeId of the type.
     * @param nameId Default NameId to use; `kUnnamed` to clear.
     */
    void setDefault(TypeId typeId, NameId nameId) noexcept {
        defaults_.setDefault(typeId, nameId);
        if (nameId != kUnnamed)
            hasAnyDefault_.store(true, std::memory_order_relaxed);
    }

    /** @brief Returns the current default `NameId` for the given `TypeId`. */
    [[nodiscard]] NameId getDefault(TypeId typeId) const noexcept {
        return defaults_.getDefault(typeId);
    }

    /** @brief True if at least one non-kUnnamed default has been configured. */
    [[nodiscard]] bool hasAnyDefault() const noexcept {
        return hasAnyDefault_.load(std::memory_order_relaxed);
    }

    // -------------------------------------------------------------------------
    // Type lookup (used by typeIdFor<T>)
    // -------------------------------------------------------------------------

    /**
     * @brief Looks up the `TypeId` for a runtime type by its `std::type_index`.
     *
     * Returns `kInvalidTypeId` when the type was never registered.  Called by
     * `typeIdFor<T>()` on the first resolution of T; the result is then cached
     * in a per-type static atomic inside the thunk.
     *
     * Lock-free after `start()`.
     */
    [[nodiscard]] TypeId lookupTypeId(std::type_index index) const noexcept {
        return typeInterning_.lookupByTypeIndex(index);
    }

    /** @brief Unique identifier for this Registry instance. Never zero. */
    [[nodiscard]] std::uint32_t registryId() const noexcept { return registryId_; }

    // -------------------------------------------------------------------------
    // typeIdFor<T>  (thunk-facing, hot-path cache)
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the dense `TypeId` for T, populating a per-type static cache.
     *
     * The cache stores `(registryId << 32) | TypeId` in a single 64-bit atomic so
     * that lookups are correct across multiple independent Registry instances.
     * On a hit (high 32 bits match this registry's unique ID), only one relaxed
     * atomic load is paid.  On a miss, performs `lookupTypeId` (lock-free) and
     * updates the cache.
     *
     * Returns `kInvalidTypeId` when T was never registered in this registry.
     * The caller (`resolve<T>()`) raises `ResolutionError` in that case.
     *
     * @tparam T The requested bean type.
     * @return Dense TypeId, or `kInvalidTypeId` if unknown.
     */
    template <typename T>
    [[nodiscard]] TypeId typeIdFor() noexcept {
        static std::atomic<std::uint64_t> cache{0};
        const std::uint64_t e = cache.load(std::memory_order_relaxed);
        if ((e >> 32) == static_cast<std::uint64_t>(registryId_)) {
            return static_cast<TypeId>(e & 0xFFFF'FFFFu);
        }
        const TypeId id = lookupTypeId(std::type_index(typeid(T)));
        if (id != kInvalidTypeId) {
            const std::uint64_t packed =
                (static_cast<std::uint64_t>(registryId_) << 32)
                | static_cast<std::uint64_t>(id);
            cache.store(packed, std::memory_order_relaxed);
        }
        return id;
    }

    // -------------------------------------------------------------------------
    // resolve<T>  (hot path — singleton and prototype lifetimes)
    // -------------------------------------------------------------------------

    /**
     * @brief Resolves one bean compatible with T and the given named qualifier.
     *
     * Implements the resolution algorithm of specs-internal §9 for singleton and
     * prototype lifetimes.  Session and threadLocal lifetimes are stubbed pending
     * their respective implementations.
     *
     * Steps:
     *  1. State check: raises `ContextStateError` if not started.
     *  2. TypeId lookup via `typeIdFor<T>()` cache.
     *  3. Candidate lookup in `TypeIndex`.
     *  4. Priority arbitration: head of sorted vector; ambiguity check at index 1.
     *  5. Materialization per lifetime.
     *
     * @tparam T  Requested bean interface type.
     * @param nameId  `NameId` of the named qualifier; `kUnnamed` for unnamed resolution.
     * @param ctx     Active `ResolutionContext` (carries the active scope if any).
     * @return Tracked `Bean<T>` handle.
     * @throws ctr::ContextStateError  if not started.
     * @throws ctr::ResolutionError    if no candidate, or priority ambiguity.
     * @throws ctr::ConfigurationError on unsupported lifetime (session/threadLocal stub).
     */
    template <typename T>
    [[nodiscard]] auto resolve(NameId nameId, ResolutionContext& ctx) -> ctr::Bean<T>;
    // Defined in BeanInlineImpl.hpp (included at the bottom of Ctorium.hpp) to
    // avoid a circular dependency between Registry.hpp and Bean.hpp.

    /**
     * @brief Returns all candidates for (TypeId of T, nameId), materializing each.
     *
     * Unlike `resolve<T>()`, no priority arbitration is performed: every registered
     * candidate is materialized and returned.  Returns an empty vector when there are
     * no candidates (no exception).
     *
     * @tparam T Requested bean interface type.
     * @param nameId NameId of the qualifier; `kUnnamed` for unnamed resolution.
     * @param ctx    Active `ResolutionContext`.
     * @return Vector of `Bean<T>` handles, one per candidate.
     * @throws ctr::ContextStateError if not started.
     */
    template <typename T>
    [[nodiscard]] auto resolveAll(NameId nameId, ResolutionContext& ctx)
        -> std::vector<ctr::Bean<T>>;
    // Defined in BeanInlineImpl.hpp.

    // -------------------------------------------------------------------------
    // Runtime binding
    // -------------------------------------------------------------------------

    /**
     * @brief Binds an externally constructed singleton to the registry.
     *
     * Follows the specs-api §11.1 contract:
     *  - Before `start()`: registers a pending descriptor; the instance enters the
     *    lifecycle during `start()`.
     *  - After `start()` for a known TypeId: allowed if not yet instantiated.
     *  - After `start()` for an unknown TypeId: raises `ConfigurationError`.
     *  - After `start()` for an already-instantiated TypeId: raises `ConfigurationError`.
     *
     * `postConstruct` is never called (the object is already constructed).
     * `preDestroy` is called at root shutdown if the type carries that hook.
     * All listener phases fire in the standard order for bound objects.
     *
     * @tparam T Bound type.
     * @param object  Ownership of the singleton instance.  Transferred to the registry.
     * @param nameId  Named qualifier for the binding; `kUnnamed` for unnamed.
     * @param priority Candidate priority; default 0.
     * @throws ctr::ConfigurationError on post-`start()` unknown type or double-bind.
     */
    template <typename T>
    void bindSingleton(std::unique_ptr<T> object, NameId nameId, int32_t priority);
    // TODO: implement after Bean<T> and the full materialization chain are in place.

private:
    /**
     * @brief Materializes a single bean descriptor and returns a tracked handle.
     *
     * Contains the lifetime switch extracted from `resolve<T>`: singleton two-phase
     * lock, prototype CycleGuard + slot activation, and lifecycle dispatch.
     * Used by both `resolve<T>` (single candidate) and `resolveAll<T>` (all candidates).
     *
     * @tparam T Requested bean interface type.
     * @param descId  Descriptor to materialize.
     * @param ctx     Active `ResolutionContext`.
     * @return Tracked `Bean<T>` handle.
     * @throws ctr::ConfigurationError for unimplemented lifetimes (session, threadLocal).
     */
    template <typename T>
    [[nodiscard]] auto materializeOne(DescriptorId descId, ResolutionContext& ctx)
        -> ctr::Bean<T>;
    // Defined in BeanInlineImpl.hpp.
    // -------------------------------------------------------------------------
    // Dependency cycle detection  (specs-internal §13)
    // -------------------------------------------------------------------------

    /**
     * @brief Accessor to the thread-local stack tracking DescriptorIds under materialization.
     *
     * Before materializing a bean, the engine pushes its DescriptorId and scans
     * the stack for a duplicate.  A duplicate indicates a dependency cycle and
     * raises `ResolutionError`.  The entry is popped when materialization completes.
     *
     * Cost is paid only during materialization, not on resolved-singleton lookups.
     */
    static std::vector<DescriptorId>& materializationStack() noexcept;

    struct CycleGuard {
        std::vector<DescriptorId>& stack;
        explicit CycleGuard(std::vector<DescriptorId>& s, DescriptorId id)
            : stack(s) {
            for (DescriptorId existing : stack) {
                if (existing == id) {
                    throw ctr::ResolutionError(
                        "Registry: dependency cycle detected during bean materialization.");
                }
            }
            stack.push_back(id);
        }
        ~CycleGuard() { stack.pop_back(); }
    };

    std::atomic<bool> started_{false};
    /// True once any non-kUnnamed default has been configured via setDefault().
    /// Never reverts to false (safe over-approximation).  Used by resolve<T>() to
    /// skip the DefaultsTable lookup on the common no-defaults path.
    std::atomic<bool> hasAnyDefault_{false};

    /** @brief Reads started_ with relaxed ordering. Reserved for Bean<T> handle helpers. */
    [[nodiscard]] bool startedRelaxed() const noexcept {
        return started_.load(std::memory_order_relaxed);
    }

    /// Unique per-instance ID, used by the per-type static cache in typeIdFor<T>().
    /// Never zero (nextRegistryId_ starts at 1).
    const std::uint32_t registryId_ =
        nextRegistryId_.fetch_add(1, std::memory_order_relaxed);
    inline static std::atomic<std::uint32_t> nextRegistryId_{1};

    template <class> friend class ctr::Bean;

    /**
     * @brief Executes the full destruction lifecycle for one bean instance.
     *
     * Sequence: onPreDestroy → preDestroy hook → C++ destructor → onDestroyed →
     * `::operator delete` (skipped when `d.size == 0` — externally owned memory).
     * Defined in BeanInlineImpl.hpp on the model of `resolve<T>()`.
     */
    void executeDestructionLifecycle(DescriptorId descId, void* mem) noexcept;

    /**
     * @brief Registers the owning `BeanContext` as a self-injectable singleton.
     *
     * Called during `start()` before `typeInterning_.freeze()`.  Appends a synthetic
     * `Descriptor` with `size == 0` (externally owned memory) and stores `ctx` in
     * the `SingletonStore`.  Dispatches `onInitialized` and `onCreated`.
     * Defined in BeanInlineImpl.hpp.
     */
    void registerContextBean(BeanContext* ctx);

    // -------------------------------------------------------------------------
    // Components
    // -------------------------------------------------------------------------

    DescriptorId    beanContextDescId_ = kInvalidDescriptorId;

    DescriptorTable descriptors_;
    TypeInterning   typeInterning_;
    NameInterning   nameInterning_;
    TypeIndex       typeIndex_;
    DefaultsTable   defaults_;
    SingletonStore  singletons_;
    PrototypeStore  prototypes_;
    ListenerStore   listeners_;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    mutable std::mutex writeLock_;
    std::condition_variable cv_; ///< Notified when a singleton finishes materializing.

    /// DescriptorIds currently being constructed (thunk executing without the lock).
    /// Guarded by writeLock_.  Small vector: contains at most one entry per thread.
    /// Swap-and-pop-back O(1) removal; linear scan is fast for N ≤ thread count.
    std::vector<DescriptorId> materializing_;

    struct PendingContribution {
        DiscoverContribution contribution;
        DiscoverOptions      options;
    };
    std::vector<PendingContribution> pending_;
};

inline std::vector<DescriptorId>& Registry::materializationStack() noexcept {
    thread_local std::vector<DescriptorId> stack;
    return stack;
}

} // namespace ctr::detail
