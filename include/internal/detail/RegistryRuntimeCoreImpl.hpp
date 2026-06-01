#pragma once

namespace ctr::detail {
    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::executeDestructionLifecycle
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::executeDestructionLifecycle(DescriptorId descId, void *mem) noexcept {
        const DescriptorCold &cold = descriptors_.coldAt(descId);
        ResolutionContext ctx{*this};

        const bool dispatchPreDestroy =
            listeners_.hasListeners(ListenerStore::phasePreDestroy());
        const bool dispatchDestroyed =
            listeners_.hasListeners(ListenerStore::phaseDestroyed());
        if (dispatchPreDestroy || dispatchDestroyed) {
            ctr::AnyBean anyBean;
            anyBean.object_ = mem;
            anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
            anyBean.bits_.f1.descId = descId;
            anyBean.registry_ = this;

            if (dispatchPreDestroy) {
                listeners_.dispatch(
                    ListenerStore::phasePreDestroy(),
                    cold.exposedType,
                    &anyBean);
            }

            if (cold.preDestroy) {
                cold.preDestroy(mem, static_cast<void *>(&ctx));
            }

            cold.destroy(mem);

            if (dispatchDestroyed) {
                // anyBean.object_ points to destroyed memory; listeners must not dereference it.
                listeners_.dispatch(
                    ListenerStore::phaseDestroyed(),
                    cold.exposedType,
                    &anyBean);
            }
        } else {
            if (cold.preDestroy) {
                cold.preDestroy(mem, static_cast<void *>(&ctx));
            }

            cold.destroy(mem);
        }

        if (cold.dealloc != nullptr) {
            cold.dealloc(mem);
        } else if (cold.size != 0) {
            ::operator delete(mem, cold.size, std::align_val_t{cold.align});
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::releasePrototypeLast
    // ─────────────────────────────────────────────────────────────────────────────

    [[gnu::cold, gnu::noinline]] inline void Registry::releasePrototypeLast(
        SlotId slot,
        PrototypeStore* store
        ) noexcept {
        const auto [mem, descId] = store->takeSlotMetaForDestruction(slot);
        if (mem != nullptr)
            executeDestructionLifecycle(descId, mem);
        store->reclaimSlot(slot);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::registerContextBean
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::registerContextBean(BeanContext *ctx) {
        const TypeId typeId = typeInterning_.internByName(
            "ctr::BeanContext",
            TypeInfoGetter<ctr::BeanContext>::get
            );

        Descriptor d;
        DescriptorCold cold;
        cold.exposedType = typeId;
        cold.concreteType = typeId;
        cold.name = kUnnamed;
        d.priority = 0;
        d.lifetime = Lifetime::Singleton;
        cold.origin = Origin::RuntimeBinding;
        cold.construct = [](void *, void *) noexcept {
        };
        cold.destroy = [](void *) noexcept {
        };
        cold.postConstruct = nullptr;
        cold.preDestroy = nullptr;
        cold.size = 0; // externally owned; executeDestructionLifecycle skips ::operator delete
        cold.align = alignof(ctr::BeanContext);
        cold.factoryMethodDescriptor = kInvalidDescriptorId;

        cold.observedTypeGetter = &TypeInfoGetter<ctr::BeanContext>::get;
        cold.exactTypeGetter = &TypeInfoGetter<ctr::BeanContext>::get;
        cold.nameStr = "ctr::BeanContext";

        beanContextDescId_ = descriptors_.append(std::move(d), std::move(cold));
        typeIndex_.insertCandidate(typeId, kUnnamed, beanContextDescId_);
        typeIndex_.updateSingleUnnamed(typeId);

        singletons_.resize(descriptors_.size()); // extend one slot for BeanContext
        singletons_.store(beanContextDescId_, static_cast<void *>(ctx));

        root_ = ctx;
        // Lifecycle dispatch (phaseInitialized / phaseCreated) is performed by start()
        // after releasing writeLock_ (A5: prevent deadlock from listener callbacks).
    }


} // namespace ctr::detail
