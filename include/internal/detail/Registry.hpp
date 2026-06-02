#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

// Forward declarations to allow method signatures without circular includes.

#include "../../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE { template <class> class Bean; }
namespace CTORIUM_NAMESPACE { class BeanContext; }
namespace CTORIUM_NAMESPACE { class ScopedContext; }

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

namespace CTORIUM_NAMESPACE::detail {


inline constexpr std::uint32_t kNoMaterializationThreadToken = 0;

inline constexpr std::size_t kScopeChunkSize = 256;
inline constexpr std::size_t kScopeTopCapacity = 256;
using ScopeChunk = std::array<
    std::atomic<CTORIUM_NAMESPACE::ScopedContext*>,
    kScopeChunkSize>;

inline constexpr std::size_t kTypeIdCacheChunkSize = 256;
inline constexpr std::size_t kTypeIdCacheTopCapacity = 256;
using TypeIdCacheChunk = std::array<std::atomic<TypeId>, kTypeIdCacheChunkSize>;

/** @brief Key used to serialize singleton and session materialization. */
struct MaterializationKey {
    DescriptorId descId = kInvalidDescriptorId;
    CTORIUM_NAMESPACE::ScopedContext* scope = nullptr;
};

[[nodiscard]] inline bool operator==(
        MaterializationKey lhs,
        MaterializationKey rhs) noexcept {
    return lhs.descId == rhs.descId && lhs.scope == rhs.scope;
}

/** @brief In-flight materialization entry, owned by one thread token. */
struct MaterializingEntry {
    MaterializationKey key;
    std::uint32_t threadToken = kNoMaterializationThreadToken;
};

/** @brief Outgoing wait edge for one materializing thread token. */
struct MaterializationWait {
    MaterializationKey key;
    bool active = false;
};

inline constexpr std::size_t kMaterializationWaitChunkSize = 256;
using MaterializationWaitChunk =
    std::array<MaterializationWait, kMaterializationWaitChunkSize>;

/** @brief Type-erased handle form selected by non-template materialization. */
enum class MaterializedHandleForm {
    Direct,
    Proxy,
    ThreadLocal
};

inline constexpr SlotId kMaterializedProxySlot = kInvalidSlotId - 1;
inline constexpr SlotId kMaterializedThreadLocalSlot = kInvalidSlotId - 2;

/** @brief Type-erased result used by `materializeOne<T>` to build a typed handle. */
struct MaterializedBeanHandle {
    std::uintptr_t value = 0;
    std::uint64_t bits =
        (static_cast<std::uint64_t>(kInvalidDescriptorId) << 32)
        | static_cast<std::uint64_t>(kInvalidSlotId);

    [[nodiscard]] static MaterializedBeanHandle direct(
            void* instance,
            SlotId slot,
            DescriptorId descId) noexcept {
        return {
            reinterpret_cast<std::uintptr_t>(instance),
            pack(slot, descId)
        };
    }

    [[nodiscard]] static MaterializedBeanHandle proxy(
            NameId scopeNameId,
            DescriptorId descId) noexcept {
        return {
            static_cast<std::uintptr_t>(scopeNameId),
            pack(kMaterializedProxySlot, descId)
        };
    }

    [[nodiscard]] static MaterializedBeanHandle threadLocal(
            DescriptorId descId) noexcept {
        return {
            0,
            pack(kMaterializedThreadLocalSlot, descId)
        };
    }

    [[nodiscard]] MaterializedHandleForm form() const noexcept {
        const SlotId currentSlot = slot();
        if (currentSlot == kMaterializedProxySlot)
            return MaterializedHandleForm::Proxy;
        if (currentSlot == kMaterializedThreadLocalSlot)
            return MaterializedHandleForm::ThreadLocal;
        return MaterializedHandleForm::Direct;
    }

    [[nodiscard]] void* instance() const noexcept {
        return reinterpret_cast<void*>(value);
    }

    [[nodiscard]] SlotId slot() const noexcept {
        return static_cast<SlotId>(bits & 0xFFFF'FFFFu);
    }

    [[nodiscard]] DescriptorId descId() const noexcept {
        return static_cast<DescriptorId>(bits >> 32);
    }

    [[nodiscard]] NameId scopeNameId() const noexcept {
        return static_cast<NameId>(value);
    }

private:
    [[nodiscard]] static constexpr std::uint64_t pack(
            SlotId slot,
            DescriptorId descId) noexcept {
        return (static_cast<std::uint64_t>(descId) << 32)
            | static_cast<std::uint64_t>(slot);
    }
};

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
 * ThreadLocal beans live in `ThreadLocalStore`: one instance per owning thread,
 * key, and registry.
 *
 * ### start() merge algorithm
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
class Registry : public std::enable_shared_from_this<Registry> {
public:
    Registry() : prototypes_(std::make_unique<PrototypeStore>(this)) {}

    // -------------------------------------------------------------------------
    // Component accessors (used by ScopedContext and specialised engine code)
    // -------------------------------------------------------------------------

    [[nodiscard]] DescriptorTable& descriptorTable()  noexcept { return descriptors_; }
    [[nodiscard]] TypeInterning&   typeInterning()    noexcept { return typeInterning_; }
    [[nodiscard]] NameInterning&   nameInterning()    noexcept { return nameInterning_; }
    [[nodiscard]] TypeIndex&       typeIndex()        noexcept { return typeIndex_; }
    [[nodiscard]] DefaultsTable&   defaultsTable()    noexcept { return defaults_; }
    [[nodiscard]] SingletonStore&  singletonStore()   noexcept { return singletons_; }
    [[nodiscard]] PrototypeStore&  prototypeStore()   noexcept { return *prototypes_; }
    [[nodiscard]] PrototypeStore*  prototypeStoreBlock() noexcept { return prototypes_.get(); }
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
            throw CTORIUM_NAMESPACE::ContextStateError(
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
        std::unique_lock<std::mutex> lock(writeLock_);
        if (started_.load(std::memory_order_relaxed)) return;

        if (beanContextDescId_ != kInvalidDescriptorId) {
            root_ = ctx;
            singletons_.resize(descriptors_.size());
            singletons_.store(beanContextDescId_, static_cast<void*>(ctx));
            started_.store(true, std::memory_order_release);
            const TypeId beanCtxTypeId = descriptors_.coldAt(beanContextDescId_).exposedType;
            lock.unlock();

            CTORIUM_NAMESPACE::AnyBean beanCtxBean;
            beanCtxBean.object_         = static_cast<void*>(ctx);
            beanCtxBean.bits_.f1.slot   = static_cast<std::uint32_t>(kInvalidSlotId);
            beanCtxBean.bits_.f1.descId = beanContextDescId_;
            beanCtxBean.registry_       = this;
            listeners_.dispatch(
                ListenerStore::phaseInitialized(),
                beanCtxTypeId,
                &beanCtxBean,
                ListenerStore::kNoScope);
            listeners_.dispatch(
                ListenerStore::phaseCreated(),
                beanCtxTypeId,
                &beanCtxBean,
                ListenerStore::kNoScope);
            return;
        }

        // --- Phase 1: merge contributions ---
        // Local identity → DescriptorId map, used only during start().
        std::unordered_map<Identity, DescriptorId> identityMap;
        std::vector<std::pair<DescriptorId, Identity>> factoryLinks;
        std::vector<std::pair<DescriptorId, Identity>> aliasLinks; // exposed alias → primary
        {
            std::size_t total = 0;
            for (const auto& [span, opts] : pending_) total += span.size();
            identityMap.reserve(total);
            factoryLinks.reserve(total);
            aliasLinks.reserve(total);
            descriptors_.reserve(total + pendingRuntimeSingletons_.size() + 1);
            typeInterning_.reserve(total);
            nameInterning_.reserve(total);
        }

        for (const auto& [span, opts] : pending_) {
            for (const ContributedDescriptor& cd : span) {
                auto [it, inserted] = identityMap.emplace(cd.identity, DescriptorId{0});
                if (inserted) {
                    // New descriptor: intern names and types, build runtime Descriptor.
                    // Capture interned ids before std::move(d) invalidates the descriptor.
                    Descriptor d;
                    DescriptorCold cold;
                    const TypeId exposedType = typeInterning_.internByName(
                        cd.exposedTypeName, cd.exposedTypeInfo);
                    cold.exposedType  = exposedType;
                    cold.concreteType = typeInterning_.internByName(
                        cd.concreteTypeName, cd.concreteTypeInfo);
                    const NameId name = nameInterning_.intern(cd.beanName);
                    cold.name      = name;
                    d.priority     = cd.priority;
                    d.lifetime     = cd.lifetime;
                    cold.origin         = cd.origin;
                    cold.construct         = cd.construct;
                    cold.destroy           = cd.destroy;
                    cold.postConstruct     = cd.postConstruct;
                    cold.preDestroy        = cd.preDestroy;
                    cold.size              = cd.size;
                    cold.align             = cd.align;
                    cold.lazy              = cd.lazy;
                    cold.allocAndConstruct = cd.allocAndConstruct;
                    cold.dealloc           = cd.dealloc;
                    cold.factoryMethodDescriptor = kInvalidDescriptorId; // resolved in Phase 2
                    cold.factoryMethodName       = cd.factoryMethodName;
                    d.adjustToExposed  = cd.adjustToExposed;
                    cold.adjustToConcrete = cd.adjustToConcrete;
                    // primaryDescriptor: set to self for primaries; resolved in Phase 2 for aliases.
                    d.primaryDescriptor = kInvalidDescriptorId;
                    // Bean-metadata fields.
                    cold.observedTypeGetter = cd.exposedTypeInfo;
                    cold.exactTypeGetter    = cd.concreteTypeInfo;
                    cold.nameStr            = cd.beanName;
                    cold.reflectiveData     = cd.reflectiveData;

                    const DescriptorId id = descriptors_.append(std::move(d), std::move(cold));
                    it->second = id;

                    if (!cd.isExposedAlias) {
                        // Primary: link to self.
                        descriptors_.atMutable(id).primaryDescriptor = id;
                    } else if (cd.aliasOfPrimaryIdentity != kNoFactoryMethod) {
                        aliasLinks.push_back({id, cd.aliasOfPrimaryIdentity});
                    }

                    if (cd.factoryMethodIdentity != kNoFactoryMethod) {
                        factoryLinks.push_back({id, cd.factoryMethodIdentity});
                    }

                    // Register in the TypeIndex using locals captured before the move.
                    typeIndex_.insertCandidate(exposedType, name, id);
                } else {
                    // Duplicate: verify key fields match (guards against hash collision / bugs).
                    const Descriptor& existing = descriptors_.at(it->second);
                    const DescriptorCold& existingCold = descriptors_.coldAt(it->second);
                    if (std::string_view{typeInterning_.nameOf(existingCold.exposedType)}
                                != std::string_view{cd.exposedTypeName}
                            || nameInterning_.nameOf(existingCold.name)
                                != std::string_view{cd.beanName}
                            || existing.lifetime != cd.lifetime) {
                        throw CTORIUM_NAMESPACE::ConfigurationError(
                            std::string("Registry::start(): Identity collision between '"
                                )
                            + cd.exposedTypeName + "' and existing '"
                            + typeInterning_.nameOf(existingCold.exposedType)
                            + "' — divergent exposedTypeName, beanName, or lifetime "
                              "for the same Identity value.");
                    }
                }
            }
        }

        // --- Phase 1.5: process pending runtime singleton bindings ---
        // Processed before sortAllCandidates() so they participate in priority sort.
        struct BoundAtStart { DescriptorId descId; void* instance; TypeId exposedTypeId; };
        std::vector<BoundAtStart> runtimeBound;
        runtimeBound.reserve(pendingRuntimeSingletons_.size());
        for (auto& pb : pendingRuntimeSingletons_) {
            const TypeId typeId = typeInterning_.internByName(pb.typeName, pb.typeInfoGetter);
            Descriptor d;
            DescriptorCold cold;
            cold.exposedType         = typeId;
            cold.concreteType        = typeId;
            cold.name                = pb.nameId;
            d.priority               = pb.priority;
            d.lifetime               = Lifetime::Singleton;
            cold.origin              = Origin::RuntimeBinding;
            cold.construct           = [](void*, void*) noexcept {};
            cold.destroy             = pb.destroy;
            cold.postConstruct       = nullptr;
            cold.preDestroy          = pb.preDestroy;
            cold.size                = pb.size;
            cold.align               = pb.align;
            cold.allocAndConstruct   = nullptr;
            cold.dealloc             = pb.dealloc;
            cold.factoryMethodDescriptor = kInvalidDescriptorId;
            cold.factoryMethodName       = nullptr;
            cold.observedTypeGetter  = pb.typeInfoGetter;
            cold.exactTypeGetter     = pb.typeInfoGetter;
            cold.nameStr             = pb.typeName;
            const DescriptorId descId = descriptors_.append(std::move(d), std::move(cold));
            typeIndex_.insertCandidate(typeId, pb.nameId, descId);
            runtimeBound.push_back({descId, pb.instance, typeId});
        }
        pendingRuntimeSingletons_.clear();

        // --- Phase 2: resolve factory method identities → DescriptorIds ---
        for (const auto& [beanId, factoryMethodIdentity] : factoryLinks) {
            const auto fit = identityMap.find(factoryMethodIdentity);
            if (fit == identityMap.end()) {
                throw CTORIUM_NAMESPACE::ConfigurationError(
                    std::string("Registry::start(): factory method identity not found "
                                "for bean '")
                    + std::string(typeInterning_.nameOf(descriptors_.coldAt(beanId).concreteType))
                    + "'.");
            }
            // Write factoryMethodDescriptor into the already-appended Descriptor.
            // Safe: descriptors_ is append-only during start(); the reference is stable.
            descriptors_.coldAtMutable(beanId).factoryMethodDescriptor = fit->second;
        }

        // --- Phase 2.5: resolve exposed alias → primary DescriptorId ---
        for (const auto& [aliasId, primaryIdentity] : aliasLinks) {
            const auto fit = identityMap.find(primaryIdentity);
            if (fit == identityMap.end()) {
                throw CTORIUM_NAMESPACE::ConfigurationError(
                    std::string("Registry::start(): primary descriptor not found for "
                                "exposed alias '")
                    + typeInterning_.nameOf(descriptors_.coldAt(aliasId).exposedType)
                    + "'.");
            }
            descriptors_.atMutable(aliasId).primaryDescriptor = fit->second;
        }

        // --- Phase 3: sort candidate vectors by descending priority ---
        typeIndex_.sortAllCandidates(
            [this](DescriptorId id) { return descriptors_.at(id).priority; });

        // --- Phase 3.5: graph validation ---
        // Condition 4 (start() graph-validation step): two producer methods on the same
        // factory returning the same type and named key → ConfigurationError.
        // factoryIds is declared outside the outer loop to reuse its capacity.
        std::vector<DescriptorId> factoryIds;
        for (TypeId typeId{0};
                static_cast<std::size_t>(typeId) < typeInterning_.size();
                ++typeId) {
            const NameTable* table = typeIndex_.tableFor(typeId);
            if (!table) continue;
            for (const auto& [nameId, entry] : table->entries) {
                factoryIds.clear();
                for (DescriptorId id : entry.candidates) {
                    const DescriptorCold& cold = descriptors_.coldAt(id);
                    if (cold.origin == Origin::FactoryProduct
                            && cold.factoryMethodDescriptor != kInvalidDescriptorId) {
                        factoryIds.push_back(cold.factoryMethodDescriptor);
                    }
                }
                std::sort(factoryIds.begin(), factoryIds.end());
                if (std::adjacent_find(factoryIds.begin(), factoryIds.end())
                        != factoryIds.end()) {
                    throw CTORIUM_NAMESPACE::ConfigurationError(
                        "Registry::start(): two producer methods on the same factory "
                        "return the same type and named key: type='"
                        + std::string(typeInterning_.nameOf(typeId))
                        + "' name='"
                        + std::string(nameInterning_.nameOf(nameId))
                        + "'.");
                }
            }
        }

        // --- Phase 3.75: session/scoped injection graph validation ---
        // (a) [[=ctr::scoped{...}]] on non-session target → ConfigurationError.
        // (b) session dep injected into non-session consumer without [[=ctr::scoped]] → ConfigurationError.
        for (const auto& [span, opts] : pending_) {
            for (const ContributedDescriptor& cd : span) {
                if (!cd.params || cd.paramCount == 0) continue;
                for (std::size_t pi = 0; pi < cd.paramCount; ++pi) {
                    const ContributedParamDescriptor& p = cd.params[pi];
                    const TypeId injTypeId =
                        typeInterning_.lookupByTypeIndex(std::type_index(p.injectedTypeInfo()));
                    if (injTypeId == kInvalidTypeId) continue;
                    const std::vector<DescriptorId>* injCandidates =
                        typeIndex_.candidatesFor(injTypeId, kUnnamed);
                    if (!injCandidates || injCandidates->empty()) continue;
                    const Lifetime injLt = descriptors_.at((*injCandidates)[0]).lifetime;
                    // (a): [[=ctr::scoped]] on a non-session dependency target
                    if (p.hasScopedAnnotation && injLt != Lifetime::Session) {
                        throw CTORIUM_NAMESPACE::ConfigurationError(
                            std::string("start(): [[=ctr::scoped]] targets non-session bean '")
                            + (p.injectedTypeName ? p.injectedTypeName : "?") + "'.");
                    }
                    // (b): session dep injected into a non-session consumer without [[=ctr::scoped]]
                    if (!p.hasScopedAnnotation
                            && injLt == Lifetime::Session
                            && cd.lifetime != Lifetime::Session) {
                        throw CTORIUM_NAMESPACE::ConfigurationError(
                            std::string("start(): session bean '")
                            + (p.injectedTypeName ? p.injectedTypeName : "?")
                            + "' injected into a non-session consumer without [[=ctr::scoped]].");
                    }
                }
            }
        }

        // --- Phase 4: register BeanContext bean (resizes singletons_), freeze TypeId space ---
        // BeanContext must be interned before freeze(); defaults_ and started_ publish after.
        // singletons_ is resized once inside registerContextBean, after its descriptor is appended.
        // The resize covers all descriptors including Phase 1.5 runtime bindings.
        // registerContextBean does NOT dispatch lifecycle events; dispatch happens below,
        // after the lock is released (A5: prevent deadlock from listeners calling writeLock_).
        registerContextBean(ctx);
        sessionSlotCount_ = 0;
        for (std::size_t i = 0; i < descriptors_.size(); ++i) {
            Descriptor &d = descriptors_.atMutable(static_cast<DescriptorId>(i));
            if (d.lifetime == Lifetime::Session) {
                d.sessionSlot = static_cast<SessionSlot>(sessionSlotCount_++);
            } else {
                d.sessionSlot = kInvalidSessionSlot;
            }
        }
        const TypeId beanCtxTypeId = descriptors_.coldAt(beanContextDescId_).exposedType;

        // Store pre-start bound instances.
        // Lifecycle dispatch (onInitialized/onCreated) is deferred to
        // dispatchBoundSingletonLifecycle(), called by BeanContext::start() after
        // flushing deferred listeners so that pre-start listeners observe the events.
        for (const auto& rb : runtimeBound) {
            singletons_.store(rb.descId, rb.instance);
            startLifecyclePending_.push_back({rb.descId, rb.instance, rb.exposedTypeId});
        }

        typeInterning_.freeze();
        defaults_.resize(typeInterning_.size());

        // Release store: ensures all structures populated during start() are visible
        // to threads that subsequently read started_ with memory_order_acquire.
        started_.store(true, std::memory_order_release);

        // A5: release writeLock_ before dispatching BeanContext lifecycle events so
        // that listener callbacks can call resolve() or other registry operations
        // without deadlocking on writeLock_.
        lock.unlock();

        CTORIUM_NAMESPACE::AnyBean beanCtxBean;
        beanCtxBean.object_         = static_cast<void*>(ctx);
        beanCtxBean.bits_.f1.slot   = static_cast<std::uint32_t>(kInvalidSlotId);
        beanCtxBean.bits_.f1.descId = beanContextDescId_;
        beanCtxBean.registry_       = this;
        listeners_.dispatch(
            ListenerStore::phaseInitialized(),
            beanCtxTypeId,
            &beanCtxBean,
            ListenerStore::kNoScope);
        listeners_.dispatch(
            ListenerStore::phaseCreated(),
            beanCtxTypeId,
            &beanCtxBean,
            ListenerStore::kNoScope);
    }

    /**
     * @brief Permanently stops the registry and destroys all owned bean instances.
     *
     * Destroys singletons, prototypes, and thread-local instances of still-alive
     * threads in order.  Listeners are cleared last so lifecycle
     * callbacks fire throughout the sweep.
     * After this call the registry must not be used; `BeanContext::stop()` handles
     * the lifecycle contract at the public API level.
     */
    void stop() noexcept {
        if (!stopRegistryOwnedBeans()) return;

        // Thread-local instances on still-alive threads.
        // Collect+erase under tlMutex_ (brief critical section); destroy outside it.
        std::vector<std::pair<DescriptorId, void*>> tlToDestroy;
        {
            std::lock_guard tlLock(tlMutex_);
            for (TLData* threadData : tlThreadStores_) {
                threadData->collectFor(registryId(), tlToDestroy);
            }
            tlThreadStores_.clear();
        }
        for (auto it = tlToDestroy.rbegin(); it != tlToDestroy.rend(); ++it) {
            executeDestructionLifecycle(it->first, it->second, ListenerStore::kNoScope);
        }

        listeners_.clear();
    }

    /**
     * @brief Destroys all thread-local instances owned by the calling thread for
     * this registry.  Called by the per-thread TLCleanup sentinel on thread exit.
     *
     * Executes the full destruction lifecycle on the exiting thread.
     * Collect+erase under `tlMutex_`; destroy outside to avoid holding the lock
     * during user destructors.
     */
    void cleanupCurrentThread() noexcept;
    // Defined in RegistryRuntimeImpl.hpp.

    /**
     * @brief Materializes (or retrieves) the thread-local instance for `descId`
     * on the calling thread.  No lock needed — the TL store is thread-private.
     *
     * @param descId Descriptor of the threadLocal bean.
     * @param ctx    Active ResolutionContext.
     * @return Live instance pointer (never null on success).
     */
    void* materializeThreadLocalInstance(DescriptorId descId, ResolutionContext& ctx);
    // Defined in RegistryRuntimeImpl.hpp.

    /** @brief True after a successful `start()`, false before or after `stop()`. */
    [[nodiscard]] bool started() const noexcept {
        return started_.load(std::memory_order_acquire);
    }

    /** @brief Number of dense session slots assigned in the descriptor table. */
    [[nodiscard]] std::size_t sessionSlotCount() const noexcept {
        return sessionSlotCount_;
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
        // A6: NameInterning::intern() acquires its own shared_mutex (write).
        // Do NOT acquire writeLock_ here — the lock order is always
        // writeLock_ (outer) → NI::mutex_ (inner) when start() calls intern(),
        // so taking NI::mutex_ without writeLock_ avoids any inversion.
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
     * `typeIdFor<T>()` on the first resolution of T in this registry; the result
     * is then cached in this registry's per-type slot table.
     *
     * Lock-free after `start()`.
     */
    [[nodiscard]] TypeId lookupTypeId(std::type_index index) const noexcept {
        return typeInterning_.lookupByTypeIndex(index);
    }

    /** @brief Unique identifier for this Registry instance. Never zero. */
    [[nodiscard]] std::uint32_t registryId() const noexcept { return registryId_; }

    /**
     * @brief Static factory shim: constructs a deferred Form 2 `Bean<U>` handle.
     *
     * Delegates to `Bean<U>::makeDeferred` (private, accessible here because
     * `Registry` is a declared friend of `Bean<T>`).  Called from `injectParam`
     * for `[[=ctr::scoped{...}]]` parameters.
     *
     * @tparam U       Session bean type.
     * @param scopeNameId     NameId of the target scope.
     * @param descId          DescriptorId of the target session bean.
     * @param reg             Root Registry pointer.
     */
    template<typename U>
    [[nodiscard]] static CTORIUM_NAMESPACE::Bean<U> makeDeferredHandle(
            NameId scopeNameId, DescriptorId descId, Registry* reg) noexcept {
        return CTORIUM_NAMESPACE::Bean<U>::makeDeferred(scopeNameId, descId, reg);
    }

    // -------------------------------------------------------------------------
    // Scope table  (NameId → ScopedContext*)
    // -------------------------------------------------------------------------

    /**
     * @brief Registers a scope under its interned NameId.
     *
     * Called by `BeanContext::resolveScope`.  Publishes the two-level scope
     * table with release stores so Form 2 handles can find scopes lock-free.
     *
     * @throws ctr::ConfigurationError if `id` exceeds the fixed top-level
     *         scope table capacity.
     */
    void registerScope(NameId id, CTORIUM_NAMESPACE::ScopedContext* scope) {
        std::lock_guard lock(scopesMutex_);
        const auto idx = static_cast<std::size_t>(id);
        const std::size_t chunkIndex = idx / kScopeChunkSize;
        if (chunkIndex >= kScopeTopCapacity) {
            throw CTORIUM_NAMESPACE::ConfigurationError(
                "Registry::registerScope: scope NameId exceeds segmented "
                "scope table capacity.");
        }

        ScopeChunk* chunk = scopeChunks_[chunkIndex].load(std::memory_order_acquire);
        if (chunk == nullptr) {
            auto owned = std::make_unique<ScopeChunk>();
            chunk = owned.get();
            ownedScopeChunks_.push_back(std::move(owned));
            scopeChunks_[chunkIndex].store(chunk, std::memory_order_release);
        }

        auto& slot = (*chunk)[idx % kScopeChunkSize];
        slot.store(scope, std::memory_order_release);
    }

    /**
     * @brief Looks up a scope by its NameId.  Returns `nullptr` if not found.
     * Used by the Form 2 proxy path in `Bean<T>::operator->`.
     */
    [[nodiscard]] CTORIUM_NAMESPACE::ScopedContext* findScope(NameId id) const noexcept {
        const auto idx = static_cast<std::size_t>(id);
        const std::size_t chunkIndex = idx / kScopeChunkSize;
        if (chunkIndex >= kScopeTopCapacity)
            return nullptr;

        const ScopeChunk* chunk =
            scopeChunks_[chunkIndex].load(std::memory_order_acquire);
        if (chunk == nullptr)
            return nullptr;
        return (*chunk)[idx % kScopeChunkSize].load(std::memory_order_acquire);
    }

    /**
     * @brief Interns a scope name (same interning table as named keys), with a
     * per-call-site 64-bit packed cache for multi-registry correctness.
     *
     * `resolveScope("x")` and `internScopeNameCached("x", cache)` both use
     * `nameInterning_.intern()` and therefore produce the same NameId.
     *
     * @param name  Scope name string; null or empty → `kUnnamed`.
     * @param cache Per-call-site static atomic: `(registryId << 32) | NameId`.
     * @return Interned NameId.
     */
    [[nodiscard]] NameId internScopeNameCached(const char* name,
                                               std::atomic<std::uint64_t>& cache) {
        if (!name || *name == '\0') return kUnnamed;
        const std::uint64_t e = cache.load(std::memory_order_relaxed);
        if ((e >> 32) == static_cast<std::uint64_t>(registryId_)) {
            return static_cast<NameId>(e & 0xFFFF'FFFFu);
        }
        const NameId nid = internNameSafe(std::string_view{name});
        cache.store(
            (static_cast<std::uint64_t>(registryId_) << 32)
            | static_cast<std::uint64_t>(nid),
            std::memory_order_relaxed);
        return nid;
    }

    // -------------------------------------------------------------------------
    // typeIdFor<T>  (thunk-facing, hot-path cache)
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the dense `TypeId` for T, populating this registry's cache.
     *
     * Each T receives one process-wide slot, stored in a constant-initialized
     * function-local atomic.  The slot indexes this Registry's chunked `TypeId`
     * cache, so independent roots do not evict each other's cached value.
     * On a cache miss, performs `lookupTypeId` and records the result locally.
     *
     * Returns `kInvalidTypeId` when T was never registered in this registry.
     * The caller (`resolve<T>()`) raises `ResolutionError` in that case.
     *
     * @tparam T The requested bean type.
     * @return Dense TypeId, or `kInvalidTypeId` if unknown.
     */
    template <typename T>
    [[nodiscard]] TypeId typeIdFor() noexcept {
        static std::atomic<std::uint32_t> slotCache{kInvalidTypeId};
        std::uint32_t slot = slotCache.load(std::memory_order_relaxed);
        if (slot == kInvalidTypeId) {
            slot = claimTypeIdCacheSlot(slotCache);
            if (slot == kInvalidTypeId) {
                return lookupTypeId(std::type_index(typeid(T)));
            }
        }

        const TypeId cached = cachedTypeIdForSlot(slot);
        if (cached != kInvalidTypeId) {
            return cached;
        }
        return cacheTypeIdForSlot(slot, std::type_index(typeid(T)));
    }

    // -------------------------------------------------------------------------
    // resolve<T>  (hot path — singleton and prototype lifetimes)
    // -------------------------------------------------------------------------

    /**
     * @brief Resolves one bean compatible with T and the given named qualifier.
     *
     * Implements the hot-path resolution algorithm for singleton,
     * prototype, session, and threadLocal lifetimes.  Session and threadLocal
     * materialization is delegated to `materializeSessionInstance()` and
     * `materializeThreadLocalInstance()`.
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
     * @throws ctr::ContextStateError  if not started, or if a session bean is
     *                                  resolved without a started scope.
     * @throws ctr::ResolutionError    if no candidate, priority ambiguity, or
     *                                  dependency cycle.
     * @throws ctr::ConfigurationError if an internal descriptor lifetime is unhandled.
     */
    template <typename T>
    [[nodiscard]] auto resolve(NameId nameId, ResolutionContext& ctx) -> CTORIUM_NAMESPACE::Bean<T>;
    // Defined in RegistryRuntimeImpl.hpp (included at the bottom of Ctorium.hpp) to
    // avoid a circular dependency between Registry.hpp and Bean.hpp.

    /**
     * @brief Finds or two-phase-materializes a session instance.
     *
     * Shared between `materializeOne` (session case) and `proxyResolve_()` (Form 2
     * operator->).  Uses the registry `writeLock_` / `cv_` and the scope's
     * `SessionStore`.  Returns the live instance pointer.
     *
     * @param descId Descriptor of the session bean.
     * @param scope  Non-null owning scope.
     * @param ctx    Active `ResolutionContext` (scope must be set).
     * @return Live instance pointer (never null on success).
     * @throws ctr::ResolutionError on dependency cycle.
     */
    void* materializeSessionInstance(DescriptorId descId,
                                     CTORIUM_NAMESPACE::ScopedContext* scope,
                                     ResolutionContext& ctx);
    // Defined in RegistryRuntimeImpl.hpp.

    // -------------------------------------------------------------------------
    // Runtime binding
    // -------------------------------------------------------------------------

    /**
     * @brief Binds an externally constructed singleton to the registry.
     *
     * Pre-`start()`: enqueues a pending descriptor; the instance enters the lifecycle
     * during `start()`.  Post-`start()`: an unknown TypeId is interned and adopted;
     * the exact `(TypeId, NameId)` pair must not be instantiated and the type must
     * not be concurrently materializing.
     *
     * @tparam T Bound type.
     * @param object   Ownership of the instance.  Transferred to the registry.
     * @param nameId   Named qualifier; `kUnnamed` for unnamed.
     * @param priority Candidate priority.
     * @return `Bean<T>` handle to the bound instance (empty handle when pre-`start()`).
     * @throws ctr::ConfigurationError on forbidden or invalid post-`start()` bind.
     */
    template <typename T>
    [[nodiscard]] CTORIUM_NAMESPACE::Bean<T> bindSingleton(std::unique_ptr<T> object,
                                             NameId nameId, int32_t priority);
    // Defined in BindReflectiveImpl.hpp.

    /**
     * @brief Fires onInitialized/onCreated for pre-start bound singletons.
     *
     * Called by BeanContext::start() AFTER flushDeferredListeners_() so that
     * listeners registered before start() observe the events.
     */
    void dispatchBoundSingletonLifecycle();

    /**
     * @brief Materializes all eager singletons (`lazy == false`) after `start()`.
     *
     * Iterates the descriptor table for `Lifetime::Singleton`, `origin != RuntimeBinding`,
     * `lazy == false`, sorted by descending priority, and constructs each via the standard
     * two-phase lock pattern.  Exceptions from user constructors propagate to the caller
     * (user-exception pass-through policy).
     *
     * Called by `BeanContext::start()` after `dispatchBoundSingletonLifecycle()`, outside
     * the registry's internal write lock (`started_` is already published before this call).
     */
    void materializeEagerSingletons();
    // Defined in RegistryRuntimeImpl.hpp.

    /**
     * @brief Releases pre-start contributions after the full public start succeeds.
     *
     * Called only after eager singleton materialization has completed, so a failed
     * eager start can keep the discovery spans available for a retry.
     */
    void completeStart() noexcept {
        pending_.clear();
    }

    /**
     * @brief Rolls back a failed public start after `started_` may have been published.
     *
     * Destroys registry-owned singleton/prototype instances already materialized by
     * the failed start and marks the registry stopped. Discovery contributions are
     * left intact so the prepared descriptor state can be started again.
     */
    void rollbackFailedStart() noexcept;
    // Defined in RegistryRuntimeImpl.hpp.

    /** @brief Destroys registry-owned beans and pending runtime singleton bindings. */
    ~Registry() noexcept;
    // Defined in RegistryRuntimeImpl.hpp.

private:
    /**
     * @brief Stops registry-owned lifetimes and destroys singleton/prototype beans.
     *
     * Idempotent: returns false when the registry was already stopped. Marks
     * `started_` false before user destruction callbacks so Bean<T> members skip
     * prototype release on the way out. Does not touch thread-local stores.
     *
     * @return true when this call transitioned the registry from started to stopped.
     */
    bool stopRegistryOwnedBeans() noexcept;

    [[gnu::cold]] void releasePrototypeLast(SlotId slot, PrototypeStore* store) noexcept;

    /**
     * @brief Materializes a single bean descriptor and returns type-erased handle data.
     *
     * Contains the lifetime switch shared by `materializeOne<T>` and eager singleton
     * construction: singleton two-phase lock, prototype CycleGuard + slot activation,
     * session proxy preparation, thread-local preparation, alias handling, and lifecycle
     * dispatch.  It does not depend on the requested C++ type.
     *
     * @param descId Descriptor to materialize.
     * @param ctx    Active `ResolutionContext`.
     * @return Type-erased handle data for the thin typed wrapper.
     * @throws ctr::ContextStateError if a session bean is resolved without a started scope.
     * @throws ctr::ResolutionError on dependency cycle.
     * @throws ctr::ConfigurationError if an internal descriptor lifetime is unhandled.
     */
    [[nodiscard]] MaterializedBeanHandle materializeOneImpl(
        DescriptorId descId,
        ResolutionContext& ctx);
    // Defined in RegistryRuntimeImpl.hpp.

    /**
     * @brief Builds the typed `Bean<T>` handle from `materializeOneImpl()`.
     *
     * @tparam T Requested bean interface type.
     * @param descId  Descriptor to materialize.
     * @param ctx     Active `ResolutionContext`.
     * @return Tracked `Bean<T>` handle.
     * @throws ctr::ContextStateError if a session bean is resolved without a started scope.
     * @throws ctr::ResolutionError on dependency cycle.
     * @throws ctr::ConfigurationError if an internal descriptor lifetime is unhandled.
     */
    template <typename T>
    [[nodiscard]] auto materializeOne(DescriptorId descId, ResolutionContext& ctx)
        -> CTORIUM_NAMESPACE::Bean<T>;
    // Defined in RegistryRuntimeImpl.hpp.

    /**
     * @brief Claims a singleton/session materialization key or waits for its owner.
     *
     * Shared Phase 1 for singleton and session materialization.  The caller must
     * perform the lock-free leading `find()` before entering this helper and must
     * construct/store outside this helper when it returns true.
     *
     * @tparam FindExisting Callable returning the already materialized instance.
     * @param descId Descriptor being materialized.
     * @param scope Owning scope for sessions; nullptr for singletons.
     * @param findExisting Store lookup used while `writeLock_` is held.
     * @param intraThreadCycleMessage Message for the existing stack-based cycle.
     * @param crossThreadCycleMessage Message for the wait-graph cycle.
     * @return true when the caller claimed the key; false when another thread
     *         completed materialization while this thread waited.
     * @throws ctr::ResolutionError on intra-thread or cross-thread dependency cycle.
     */
    template <typename FindExisting>
    [[nodiscard]] bool claimMaterializationOrWait(
        DescriptorId descId,
        CTORIUM_NAMESPACE::ScopedContext* scope,
        FindExisting&& findExisting,
        const char* intraThreadCycleMessage,
        const char* crossThreadCycleMessage);

    /** @brief Returns the lazily assigned non-zero token for the calling thread. */
    [[nodiscard]] static std::uint32_t materializationThreadToken() noexcept;

    /** @brief Clears this registry's wait-graph slot for a recycled thread token. */
    void clearMaterializationWaitSlot(std::uint32_t threadToken) noexcept;

    /** @brief Returns a thread-exit materialization token to the global freelist. */
    static void recycleMaterializationThreadToken(std::uint32_t threadToken) noexcept;

    /** @brief True when a wait-graph slot has been published for `threadToken`. */
    [[nodiscard]] bool hasMaterializationWaitSlot(
        std::uint32_t threadToken) const noexcept;

    /** @brief Returns the published wait-graph slot for `threadToken`. */
    [[nodiscard]] MaterializationWait& materializationWaitSlot(
        std::uint32_t threadToken) noexcept;

    /** @brief Returns the published wait-graph slot for `threadToken`. */
    [[nodiscard]] const MaterializationWait& materializationWaitSlot(
        std::uint32_t threadToken) const noexcept;

    /**
     * @brief Ensures the wait-edge table has a slot for `threadToken`.
     *
     * Any allocation happens before `writeLock_` is acquired.  Publication of
     * new wait-slot chunks happens under `writeLock_` without copying entries.
     */
    void ensureMaterializationWaitSlot(std::uint32_t threadToken);

    /**
     * @brief Traverses the current wait graph after adding current -> owner.
     *
     * `writeLock_` must be held.  Traversal is bounded by the number of in-flight
     * materializations because each thread has at most one outgoing wait edge.
     */
    [[nodiscard]] bool materializationWaitCycleDetected(
        std::uint32_t currentThreadToken,
        std::uint32_t ownerThreadToken) const noexcept;

    /** @brief Lazily claims the process-wide cache slot for one T. */
    [[nodiscard]] static std::uint32_t claimTypeIdCacheSlot(
            std::atomic<std::uint32_t>& slotCache) noexcept {
        std::uint32_t current = slotCache.load(std::memory_order_relaxed);
        if (current != kInvalidTypeId) {
            return current;
        }

        std::uint32_t next = nextTypeIdCacheSlot_.load(std::memory_order_relaxed);
        for (;;) {
            if (next == kInvalidTypeId) {
                return kInvalidTypeId;
            }
            if (nextTypeIdCacheSlot_.compare_exchange_weak(
                    next,
                    next + 1u,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;
            }
        }

        current = kInvalidTypeId;
        if (slotCache.compare_exchange_strong(
                current,
                next,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return next;
        }
        return current;
    }

    /** @brief Reads a cached TypeId for a process-wide slot in this registry. */
    [[nodiscard]] TypeId cachedTypeIdForSlot(std::uint32_t slot) const noexcept {
        const std::size_t chunkIndex = slot / kTypeIdCacheChunkSize;
        if (chunkIndex >= kTypeIdCacheTopCapacity) {
            return kInvalidTypeId;
        }

        const TypeIdCacheChunk* const chunk =
            typeIdCacheChunks_[chunkIndex].load(std::memory_order_acquire);
        if (!chunk) {
            return kInvalidTypeId;
        }
        return (*chunk)[slot % kTypeIdCacheChunkSize].load(std::memory_order_relaxed);
    }

    /** @brief Creates an empty TypeId cache chunk without throwing. */
    [[nodiscard]] static std::unique_ptr<TypeIdCacheChunk> makeTypeIdCacheChunk() noexcept {
        TypeIdCacheChunk* const raw = new (std::nothrow) TypeIdCacheChunk{};
        if (!raw) {
            return {};
        }
        for (std::atomic<TypeId>& cached : *raw) {
            cached.store(kInvalidTypeId, std::memory_order_relaxed);
        }
        return std::unique_ptr<TypeIdCacheChunk>{raw};
    }

    /** @brief Looks up and records a TypeId in this registry's cache miss path. */
    [[nodiscard]] TypeId cacheTypeIdForSlot(
            std::uint32_t slot,
            std::type_index index) noexcept {
        const std::size_t chunkIndex = slot / kTypeIdCacheChunkSize;
        if (chunkIndex >= kTypeIdCacheTopCapacity) {
            return lookupTypeId(index);
        }

        const std::size_t offset = slot % kTypeIdCacheChunkSize;
        std::lock_guard<std::mutex> lock(typeIdCacheMutex_);

        TypeIdCacheChunk* chunk =
            typeIdCacheChunks_[chunkIndex].load(std::memory_order_relaxed);
        if (chunk) {
            const TypeId cached = (*chunk)[offset].load(std::memory_order_relaxed);
            if (cached != kInvalidTypeId) {
                return cached;
            }
        }

        const TypeId id = lookupTypeId(index);
        if (id == kInvalidTypeId) {
            return id;
        }

        if (!chunk) {
            std::unique_ptr<TypeIdCacheChunk> owned = makeTypeIdCacheChunk();
            if (!owned) {
                return id;
            }
            (*owned)[offset].store(id, std::memory_order_relaxed);
            chunk = owned.get();
            try {
                ownedTypeIdCacheChunks_.push_back(std::move(owned));
            } catch (...) {
                return id;
            }
            typeIdCacheChunks_[chunkIndex].store(chunk, std::memory_order_release);
            return id;
        }

        (*chunk)[offset].store(id, std::memory_order_relaxed);
        return id;
    }

    // -------------------------------------------------------------------------
    // Dependency cycle detection
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
                    throw CTORIUM_NAMESPACE::ResolutionError(
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

    std::array<std::atomic<TypeIdCacheChunk*>, kTypeIdCacheTopCapacity>
        typeIdCacheChunks_{};
    std::vector<std::unique_ptr<TypeIdCacheChunk>> ownedTypeIdCacheChunks_;
    std::mutex typeIdCacheMutex_;
    inline static std::atomic<std::uint32_t> nextTypeIdCacheSlot_{0};

    /// Unique per-instance ID, used by packed per-call-site caches.
    /// Never zero (nextRegistryId_ starts at 1).
    const std::uint32_t registryId_ =
        nextRegistryId_.fetch_add(1, std::memory_order_relaxed);
    inline static std::atomic<std::uint32_t> nextRegistryId_{1};
    inline static std::atomic<std::uint32_t> nextMaterializationThreadToken_{0};
    inline static std::mutex materializationThreadTokenMutex_;
    inline static std::vector<std::uint32_t> freeMaterializationThreadTokens_;

    template <class> friend class CTORIUM_NAMESPACE::Bean;
    friend class CTORIUM_NAMESPACE::AnyBean;
    friend class CTORIUM_NAMESPACE::ScopedContext;
    friend struct TLCleanup;

    /**
     * @brief Executes the full destruction lifecycle for one bean instance.
     *
     * Sequence: onPreDestroy → preDestroy hook → onDestroyed → C++ destructor →
     * `::operator delete` (skipped when `DescriptorCold::size == 0` — externally owned memory).
     * Defined in RegistryRuntimeImpl.hpp on the model of `resolve<T>()`.
     *
     * @param emitterScope Scope of the bean being destroyed, or `ListenerStore::kNoScope`.
     */
    void executeDestructionLifecycle(
        DescriptorId descId,
        void* mem,
        NameId emitterScope
        ) noexcept;

    /**
     * @brief Destroys all session instances owned by `scope` in reverse construction
     * order.  Marks the scope stopped, clears its SessionStore.
     * Called by `ScopedContext::stop()`.  Defined in RegistryRuntimeImpl.hpp.
     */
    void stopScope(CTORIUM_NAMESPACE::ScopedContext& scope) noexcept;

    /**
     * @brief Registers the owning `BeanContext` as a self-injectable singleton.
     *
     * Called during `start()` before `typeInterning_.freeze()`.  Appends a synthetic
     * `Descriptor` with `size == 0` (externally owned memory) and stores `ctx` in
     * the `SingletonStore`.  Dispatches `onInitialized` and `onCreated`.
     * Defined in RegistryRuntimeImpl.hpp.
     */
    void registerContextBean(BeanContext* ctx);

    // -------------------------------------------------------------------------
    // Components
    // -------------------------------------------------------------------------

    /** @brief Pointer to the owning BeanContext; set by registerContextBean(). */
    [[nodiscard]] CTORIUM_NAMESPACE::BeanContext* rootContext() const noexcept { return root_; }

    CTORIUM_NAMESPACE::BeanContext* root_ = nullptr;
    DescriptorId    beanContextDescId_ = kInvalidDescriptorId;
    std::size_t     sessionSlotCount_ = 0;

    DescriptorTable descriptors_;
    TypeInterning   typeInterning_;
    NameInterning   nameInterning_;
    TypeIndex       typeIndex_;
    DefaultsTable   defaults_;
    SingletonStore  singletons_;
    std::unique_ptr<PrototypeStore> prototypes_;
    ListenerStore   listeners_;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    mutable std::mutex writeLock_;
    std::condition_variable cv_; ///< Notified when a singleton or session finishes materializing.

    /// (DescriptorId, ScopedContext*) keys currently being constructed outside the lock.
    /// scope == nullptr for singletons; scope != nullptr for session beans.
    /// Guarded by writeLock_.  Small vector: at most one entry per thread in practice.
    std::vector<MaterializingEntry> materializing_;

    /// Outgoing wait edge per materialization thread token. Index 0 is unused.
    /// Entries are guarded by writeLock_; chunk growth is published by
    /// ensureMaterializationWaitSlot().
    std::vector<MaterializationWaitChunk*> waitingByThreadChunks_;
    std::vector<std::unique_ptr<MaterializationWaitChunk>>
        ownedWaitingByThreadChunks_;
    std::size_t waitingByThreadCapacity_ = 0;

    /// Serializes wait-edge chunk growth performed before publishing under writeLock_.
    mutable std::mutex waitSlotsMutex_;

    /// Segmented scope NameId -> ScopedContext* table.  The top-level array never
    /// moves; published chunks are retained until Registry destruction so Form 2
    /// operator-> can dereference chunk pointers without reclamation.
    mutable std::mutex scopesMutex_;
    std::array<std::atomic<ScopeChunk*>, kScopeTopCapacity> scopeChunks_{};
    std::vector<std::unique_ptr<ScopeChunk>> ownedScopeChunks_;

    /// Thread-local store pointers: one per registered thread.
    /// Protected by tlMutex_.  Each pointer remains valid as long as the owning
    /// thread is alive.  Used by stop() and cleanupCurrentThread() to find and
    /// destroy threadLocal instances without holding writeLock_ (avoids deadlock
    /// because neither path acquires writeLock_ while holding tlMutex_).
    mutable std::mutex tlMutex_;
    std::vector<TLData*> tlThreadStores_;

    struct PendingContribution {
        DiscoverContribution contribution;
        DiscoverOptions      options;
    };
    std::vector<PendingContribution> pending_;

    /// Bound singletons awaiting lifecycle dispatch after deferred-listener flush.
    /// Populated by start() Phase 4.5; consumed by dispatchBoundSingletonLifecycle().
    struct StartBound { DescriptorId descId; void* instance; TypeId exposedTypeId; };
    std::vector<StartBound> startLifecyclePending_;

    /// Pre-start runtime singleton bindings: processed in Phase 1.5 of start().
    struct PendingRuntimeSingleton {
        void*        instance;
        const char*  typeName;
        const std::type_info& (*typeInfoGetter)();
        NameId       nameId;   // already interned via internNameSafe()
        int32_t      priority;
        void       (*destroy)(void*) noexcept;
        void       (*preDestroy)(void*, void*);  // null for non-Ctorium types
        void       (*dealloc)(void*) noexcept;
        std::size_t  size;
        std::size_t  align;
    };
    std::vector<PendingRuntimeSingleton> pendingRuntimeSingletons_;
};

inline std::vector<DescriptorId>& Registry::materializationStack() noexcept {
    thread_local std::vector<DescriptorId> stack;
    return stack;
}

} // namespace CTORIUM_NAMESPACE::detail
