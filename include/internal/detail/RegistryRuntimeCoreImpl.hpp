#pragma once

namespace ctr::detail {
    // ─────────────────────────────────────────────────────────────────────────────
    // Registry::executeDestructionLifecycle
    // ─────────────────────────────────────────────────────────────────────────────

    inline void Registry::executeDestructionLifecycle(DescriptorId descId, void *mem) noexcept {
        const Descriptor &d = descriptors_.at(descId);
        ResolutionContext ctx{*this};

        ctr::AnyBean anyBean;
        anyBean.object_ = mem;
        anyBean.bits_.f1.slot = static_cast<std::uint32_t>(kInvalidSlotId);
        anyBean.bits_.f1.descId = descId;
        anyBean.registry_ = this;

        listeners_.dispatch(ListenerStore::phasePreDestroy(), d.exposedType, &anyBean);

        if (d.preDestroy) {
            d.preDestroy(mem, static_cast<void *>(&ctx));
        }

        d.destroy(mem);

        // anyBean.object_ points to freed memory from here; listeners must not dereference it.
        listeners_.dispatch(ListenerStore::phaseDestroyed(), d.exposedType, &anyBean);

        if (d.dealloc != nullptr) {
            d.dealloc(mem);
        } else if (d.size != 0) {
            ::operator delete(mem, d.size, std::align_val_t{d.align});
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
        d.exposedType = typeId;
        d.concreteType = typeId;
        d.name = kUnnamed;
        d.priority = 0;
        d.lifetime = Lifetime::Singleton;
        d.origin = Origin::RuntimeBinding;
        d.construct = [](void *, void *) noexcept {
        };
        d.destroy = [](void *) noexcept {
        };
        d.postConstruct = nullptr;
        d.preDestroy = nullptr;
        d.size = 0; // externally owned — executeDestructionLifecycle skips ::operator delete
        d.align = alignof(ctr::BeanContext);
        d.factoryMethodDescriptor = kInvalidDescriptorId;

        d.observedTypeGetter = &TypeInfoGetter<ctr::BeanContext>::get;
        d.exactTypeGetter = &TypeInfoGetter<ctr::BeanContext>::get;
        d.nameStr = "ctr::BeanContext";

        beanContextDescId_ = descriptors_.append(std::move(d));
        typeIndex_.insertCandidate(typeId, kUnnamed, beanContextDescId_);
        typeIndex_.updateSingleUnnamed(typeId);

        singletons_.resize(descriptors_.size()); // extend one slot for BeanContext
        singletons_.store(beanContextDescId_, static_cast<void *>(ctx));

        root_ = ctx;
        // Lifecycle dispatch (phaseInitialized / phaseCreated) is performed by start()
        // after releasing writeLock_ (A5: prevent deadlock from listener callbacks).
    }


} // namespace ctr::detail
