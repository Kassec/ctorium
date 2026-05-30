#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../TypeId.hpp"
#include "../../api/ctr/ListenerHandle.hpp"

namespace ctr::detail {

/**
 * @brief Lifecycle listener registry: registration, removal, and phase dispatch.
 *
 * Stores all typed and global listener registrations for a single root context.
 * Dispatches lifecycle events (`onInitialized`, `onCreated`, `onPreDestroy`,
 * `onDestroyed`) to matching listeners in priority-then-registration order.
 *
 * ### Callback storage model
 * All callbacks are stored as `std::function<void(const void*)>` where the `void*`
 * is a pointer to a `const AnyBean` (erased to avoid a circular include with
 * AnyBean.hpp).  Typed listeners (`context.on<T>(...)`) are pre-wrapped by
 * `BeanContext` to accept `const AnyBean&` and forward to the typed callback only
 * when the bean is compatible with T.  This keeps `ListenerStore` type-agnostic.
 *
 * ### Copy-on-write dispatch strategy (ADR-O4 revised)
 * A raw `const View*` is published via `std::atomic<const View*>` with
 * `acquire`/`release` ordering.  `dispatch()` loads the pointer without holding any
 * lock (no heap allocation, no spinlock), satisfying specs-api §14.5: a listener
 * added or removed during dispatch does not affect the current event.
 *
 * When a listener is added or removed, a new `View` is built under `mutex_`, its
 * `unique_ptr` moved into `allViews_` (persistent storage), then the raw pointer is
 * published.  A View is never destroyed until the `ListenerStore` destructor runs;
 * old Views accumulate in `allViews_` (at most one per add/remove call, bounded).
 *
 * Dispatch visits only global listeners (`kInvalidTypeId`) and typed listeners whose
 * filter matches `beanTypeId`, using a two-pointer merge — no scan of all listeners.
 *
 * ### Thread safety
 * `addListener()` and `removeListener()` acquire `mutex_` (exclusive).
 * `dispatch()` is fully lock-free: it loads the raw pointer with `acquire` ordering
 * and iterates outside any lock, so callbacks can register or remove listeners without
 * deadlock.
 */
class ListenerStore {
public:
    ~ListenerStore() {
        clear();
        allViews_.clear();
    }

    /// Number of distinct lifecycle phases.
    static constexpr std::size_t kPhaseCount = 4;

    /// Opaque callback type.  The void* points to a `const AnyBean` (never null).
    using Callback = std::function<void(const void*)>;

    /**
     * @brief Registers a listener callback for the given phase and type filter.
     *
     * @param phaseIndex   Phase index in `[0, kPhaseCount)`.
     * @param typeId       TypeId filter, or `kInvalidTypeId` for global (all-beans).
     * @param callback     Callback invoked with a `const AnyBean*`.  Must not be empty.
     * @param priority     Dispatch priority.  Higher values execute first.
     * @return `ListenerHandle` that can be used to remove this registration.
     */
    [[nodiscard]] ListenerHandle addListener(std::size_t phaseIndex,
                                             TypeId typeId,
                                             Callback callback,
                                             int priority) {
        assert(phaseIndex < kPhaseCount && "phase index out of range");
        assert(callback && "callback must not be empty");

        std::lock_guard lock(mutex_);
        auto* entry = new Entry{typeId, std::move(callback), priority,
                                nextToken_++, phaseIndex};
        auto& vec = phases_[phaseIndex];
        vec.push_back(entry);
        // Insert into already-sorted [begin, end-1) prefix: O(log N + N), no alloc.
        const auto pos = std::lower_bound(
            vec.begin(), vec.end() - 1, vec.back(),
            [](const Entry* a, const Entry* b) { return a->priority > b->priority; });
        std::rotate(pos, vec.end() - 1, vec.end());
        phaseSizes_[phaseIndex].fetch_add(1, std::memory_order_relaxed);

        rebuildView_();

        return ListenerHandle{this, entry->token};
    }

    /**
     * @brief Dispatches a lifecycle event to all matching registered listeners.
     *
     * Loads the current immutable view without holding any lock (no heap allocation).
     * Dispatches global listeners and typed listeners for `beanTypeId` in
     * priority-descending, registration-order-ascending order via two-pointer merge.
     *
     * @param phaseIndex  Phase index in `[0, kPhaseCount)`.
     * @param beanTypeId  TypeId of the bean being dispatched.
     * @param bean        Pointer to the `const AnyBean` (cast from void* by callers).
     */
    void dispatch(std::size_t phaseIndex,
                  TypeId beanTypeId,
                  const void* bean) const {
        assert(phaseIndex < kPhaseCount);

        // Fast-exit: lock-free; avoids even the atomic load when idle.
        if (phaseSizes_[phaseIndex].load(std::memory_order_relaxed) == 0) return;

        // C2: mark this dispatch as in-flight so rebuildView_() will not purge
        // any view that this dispatch might currently hold.  seq_cst closes the
        // store-buffer race with the rebuildView_() load on dispatchCount_.
        dispatchCount_.fetch_add(1, std::memory_order_seq_cst);

        // Load the published view — no heap allocation, no spinlock.
        const View* view = view_.load(std::memory_order_acquire);
        if (view) {
            const PhaseView& pv = (*view)[phaseIndex];

            static const std::vector<ViewEntry> kEmpty;
            const std::vector<ViewEntry>* typedList = &kEmpty;
            if (!pv.typed.empty()) {
                const auto it = pv.typed.find(beanTypeId);
                if (it != pv.typed.end()) typedList = &it->second;
            }
            const auto& typeds = *typedList;
            const auto& globals = pv.global;

            // Two-pointer merge: both lists sorted by (priority desc, token asc).
            std::size_t gi = 0, ti = 0;
            while (gi < globals.size() && ti < typeds.size()) {
                const auto& g = globals[gi];
                const auto& t = typeds[ti];
                if (g.priority > t.priority
                        || (g.priority == t.priority && g.token < t.token)) {
                    g.callback(bean); ++gi;
                } else {
                    t.callback(bean); ++ti;
                }
            }
            while (gi < globals.size()) globals[gi++].callback(bean);
            while (ti < typeds.size()) typeds[ti++].callback(bean);
        }

        dispatchCount_.fetch_sub(1, std::memory_order_seq_cst);
    }

    /**
     * @brief Removes all registrations.  Called at root shutdown.
     */
    void clear() {
        std::lock_guard lock(mutex_);
        for (auto& phase : phases_) {
            for (auto* e : phase) delete e;
            phase.clear();
        }
        for (std::size_t i = 0; i < kPhaseCount; ++i)
            phaseSizes_[i].store(0, std::memory_order_relaxed);
        view_.store(nullptr, std::memory_order_release);
        // clear() is called by Registry::stop() under exclusive ownership (§18):
        // no concurrent dispatch() can be in-flight at this point.  Freeing
        // allViews_ here reclaims the memory at teardown rather than waiting for
        // the destructor.  The destructor's allViews_.clear() becomes a no-op.
        allViews_.clear();
    }

    /**
     * @brief Removes the listener identified by `token`.  Called by `ListenerHandle::remove()`.
     *
     * No-op if the token is not found (safe for double-remove and for copies that
     * share the same token).  Acquires `mutex_` internally.
     */
    void removeByToken(std::size_t token) {
        std::lock_guard lock(mutex_);
        for (auto& phase : phases_) {
            for (auto it = phase.begin(); it != phase.end(); ++it) {
                if ((*it)->token == token) {
                    const std::size_t phaseIdx = (*it)->phaseIndex;
                    delete *it;
                    phase.erase(it);
                    phaseSizes_[phaseIdx].fetch_sub(1, std::memory_order_relaxed);
                    rebuildView_();
                    return;
                }
            }
        }
    }

    // --- Phase index helpers -------------------------------------------------

    /** @brief Phase index for `onInitialized`. */
    static constexpr std::size_t phaseInitialized() noexcept { return 0; }
    /** @brief Phase index for `onCreated`. */
    static constexpr std::size_t phaseCreated()     noexcept { return 1; }
    /** @brief Phase index for `onPreDestroy`. */
    static constexpr std::size_t phasePreDestroy()  noexcept { return 2; }
    /** @brief Phase index for `onDestroyed`. */
    static constexpr std::size_t phaseDestroyed()   noexcept { return 3; }

private:
    // -------------------------------------------------------------------------
    // Stored entry — master list, protected by mutex_
    // -------------------------------------------------------------------------

    struct Entry {
        TypeId      typeId;       ///< Filter: kInvalidTypeId = global (all beans).
        Callback    callback;
        int         priority;
        std::size_t token;        ///< Unique monotone token: lower = earlier registration.
        std::size_t phaseIndex;
    };

    // -------------------------------------------------------------------------
    // Immutable view — one ViewEntry per registered listener, grouped for dispatch
    // -------------------------------------------------------------------------

    struct ViewEntry {
        Callback    callback;   ///< Copied from Entry on view construction.
        int         priority;
        std::size_t token;
    };

    struct PhaseView {
        std::vector<ViewEntry>                          global;  ///< kInvalidTypeId listeners.
        std::unordered_map<TypeId, std::vector<ViewEntry>> typed; ///< Per-TypeId listeners.
    };

    using View = std::array<PhaseView, kPhaseCount>;

    // -------------------------------------------------------------------------
    // View rebuild — called under mutex_
    // -------------------------------------------------------------------------

    void rebuildView_() {
        auto newView = std::make_unique<View>();
        for (std::size_t p = 0; p < kPhaseCount; ++p) {
            auto& pv = (*newView)[p];
            // phases_[p] is sorted by (priority desc, token asc) — preserved in split.
            for (const Entry* e : phases_[p]) {
                ViewEntry ve{e->callback, e->priority, e->token};
                if (e->typeId == kInvalidTypeId) {
                    pv.global.push_back(std::move(ve));
                } else {
                    pv.typed[e->typeId].push_back(std::move(ve));
                }
            }
        }
        const View* rawPtr = newView.get();
        view_.store(rawPtr, std::memory_order_release);
        allViews_.push_back(std::move(newView));

        // C2 runtime purge: if no dispatch is currently in-flight, free all but the
        // just-published view.  seq_cst closes the store-buffer race: if a concurrent
        // dispatch incremented dispatchCount_ before our store to view_, it will see
        // the new view pointer (not the old one) — safe to release old views.
        // This is correct under mutex_ (no concurrent rebuildView_) + dispatchCount_==0
        // (no in-flight dispatch still holding an old view pointer).
        if (dispatchCount_.load(std::memory_order_seq_cst) == 0
                && allViews_.size() > 1) {
            allViews_.erase(allViews_.begin(), allViews_.end() - 1);
        }
    }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    /// C2 runtime: counts dispatches currently in-flight (seq_cst).
    /// rebuildView_() purges allViews_ only when this is 0.
    mutable std::atomic<std::size_t> dispatchCount_{0};

    mutable std::mutex mutex_;
    std::vector<Entry*>    phases_[kPhaseCount];    ///< Master list, sorted per phase.
    std::atomic<std::size_t> phaseSizes_[kPhaseCount]{};
    std::size_t            nextToken_ = 0;

    /// Published immutable view; loaded lock-free by dispatch() with acquire ordering.
    mutable std::atomic<const View*> view_{nullptr};

    /// Persistent storage for all published Views; entries are never freed until the
    /// destructor runs, ensuring raw pointers loaded by concurrent dispatch() remain valid.
    /// Grows by at most one entry per addListener()/removeListener() call.
    std::vector<std::unique_ptr<const View>> allViews_;
};

} // namespace ctr::detail

// ─────────────────────────────────────────────────────────────────────────────
// ListenerHandle::remove() — defined here because it requires the full
// ListenerStore definition (calls removeByToken()).
// ─────────────────────────────────────────────────────────────────────────────

inline void ctr::ListenerHandle::remove() noexcept {
    if (store_ != nullptr) {
        store_->removeByToken(tokenValue_);
        store_ = nullptr;
    }
}
