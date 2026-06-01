#pragma once

namespace ctr::detail {
    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::dispatchBoundSingletonLifecycle
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::dispatchBoundSingletonLifecycle() {
        for (const auto &sb : startLifecyclePending_) {
            ctr::AnyBean anyBean;
            anyBean.object_ = sb.instance;
            anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
            anyBean.bits_.f1.descId = sb.descId;
            anyBean.registry_ = this;
            listeners_.dispatch(ListenerStore::phaseInitialized(), sb.exposedTypeId, &anyBean);
            listeners_.dispatch(ListenerStore::phaseCreated(), sb.exposedTypeId, &anyBean);
        }
        startLifecyclePending_.clear();
    }


    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::~Registry
    // ─────────────────────────────────────────────────────────────────────────────

    inline Registry::~Registry() noexcept {
        (void)stopRegistryOwnedBeans();

        // Destroy pending runtime singletons that were never started into the lifecycle.
        for (const auto &pb : pendingRuntimeSingletons_) {
            if (pb.instance) {
                pb.destroy(pb.instance);
                if (pb.dealloc) {
                    pb.dealloc(pb.instance);
                } else if (pb.size != 0) {
                    ::operator delete(pb.instance, pb.size, std::align_val_t{pb.align});
                }
            }
        }
        PrototypeStore* store = prototypes_.release();
        store->markRegistryDeadAndReleaseHold();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::materializeEagerSingletons
    //
    // Collects all Singleton, non-RuntimeBinding, lazy=false descriptors, sorts by
    // descending priority, and materializes each.  Exceptions propagate (pass-through
    // policy).
    // NOTE: when polymorphic-exposure lands, add a guard to skip alias descriptors
    // (primaryDescriptor != self) to avoid double-triggering on exposed aliases.
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::materializeEagerSingletons() {
        std::vector<std::pair<int32_t, DescriptorId>> eager;
        const std::size_t count = descriptors_.size();
        eager.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const DescriptorId id = static_cast<DescriptorId>(i);
            const Descriptor &d = descriptors_.at(id);
            const DescriptorCold &cold = descriptors_.coldAt(id);
            if (d.lifetime == Lifetime::Singleton
                && cold.origin != Origin::RuntimeBinding
                && !cold.lazy
                && d.primaryDescriptor == id) { // skip aliases (primaryDescriptor != self)
                eager.push_back({d.priority, id});
            }
        }
        std::sort(
            eager.begin(),
            eager.end(),
            [](const auto &a, const auto &b) {
                return a.first > b.first;
            }
            );

        ResolutionContext ctx{*this};
        for (const auto &[priority, id] : eager) {
            (void)priority;
            (void)materializeOneImpl(id, ctx);
        }
    }

} // namespace ctr::detail
