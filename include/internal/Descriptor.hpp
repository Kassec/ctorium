#pragma once

#include <cstddef>
#include <cstdint>
#include <typeinfo>

#include "DescriptorId.hpp"
#include "Lifetime.hpp"
#include "NameId.hpp"
#include "Origin.hpp"
#include "TypeId.hpp"

namespace ctr { struct BeanReflectiveData; } // forward decl

namespace ctr::detail {

// The second parameter of construct/postConstruct/preDestroy thunks is void*.
// At every call site in the engine it is a ctr::detail::ResolutionContext*
// (cast with static_cast). The void* avoids a circular include dependency
// between this internal header and detail/ResolutionContext.hpp.

/**
 * @brief Runtime descriptor stored in the registry descriptor table. See specs.md §28.1.
 *
 * Built by start() from a ContributedDescriptor: the Identity is consumed for deduplication
 * and discarded; factoryMethodIdentity is resolved to factoryMethodDescriptor (DescriptorId).
 */
struct Descriptor {
    /** TypeId of the type exposed to resolution callers. */
    TypeId       exposedType;
    /** TypeId of the actual instantiated concrete type. */
    TypeId       concreteType;
    /** NameId qualifier; kUnnamed when no named annotation is present. */
    NameId       name;
    /** Priority used to arbitrate among candidates sharing the same exposedType and name. */
    std::int32_t priority;
    /** Scope lifetime governing instance sharing and destruction. */
    Lifetime     lifetime;
    /** How this descriptor was contributed to the registry. */
    Origin       origin;
    /** Placement constructor thunk: (void* mem, void* registry). Must not be null. */
    void       (*construct)(void*, void*);
    /** In-place destructor thunk: (void* instance). Must not be null. */
    void       (*destroy)(void*) noexcept;
    /** Post-construction callback thunk: (void* instance, void* registry). Nullable. */
    void       (*postConstruct)(void*, void*);
    /** Pre-destruction callback thunk: (void* instance, void* registry). Nullable. */
    void       (*preDestroy)(void*, void*);
    /**
     * sizeof the concrete type, in bytes.
     * When `size == 0`, the memory is externally owned;
     * `executeDestructionLifecycle` skips `::operator delete` for this descriptor.
     */
    std::size_t  size;
    /** alignof the concrete type, in bytes. */
    std::size_t  align;
    /** DescriptorId of the source factory method, or kInvalidDescriptorId when not factory-produced. */
    DescriptorId factoryMethodDescriptor;

    /**
     * @brief Whether materialization is deferred until first resolution.
     *
     * Propagated from `[[=ctr::singleton{.lazy = ...}]]`; always `true` for non-singleton
     * lifetimes (lazy is meaningless for prototype/session/threadLocal).
     * `false` → `materializeEagerSingletons()` constructs this instance at `start()`.
     */
    bool lazy = true;

    /**
     * @brief Combined allocate-and-construct thunk for `unique_ptr<T>` factory products.
     *
     * When non-null, materialization skips engine pre-allocation and calls this thunk
     * instead; the thunk returns the live pointer released from the factory product.
     * Invariant: `allocAndConstruct != nullptr` iff `dealloc != nullptr`.
     * Null for annotated types, factories, value-return products, and runtime bindings.
     */
    void*      (*allocAndConstruct)(void*) = nullptr;

    /**
     * @brief Deallocation thunk matching `allocAndConstruct`.
     * When non-null, `executeDestructionLifecycle` calls this instead of
     * `::operator delete(mem, size, align_val)`.  Calls `T::operator delete(p)`
     * (plain deallocation, no destructor — `destroy` already called `~T()`).
     * Non-null ↔ `allocAndConstruct` non-null.
     */
    void       (*dealloc)(void*) noexcept = nullptr;

    // ── Bean-metadata fields (SPEC-bean-metadata) ────────────────────────────

    /**
     * @brief Returns `typeid(ExposedType)` at runtime for `BeanMetadata::observedType()`.
     * Propagated from `ContributedDescriptor::exposedTypeInfo`.
     */
    const std::type_info& (*observedTypeGetter)() = nullptr;

    /**
     * @brief Returns `typeid(ConcreteType)` at runtime for `BeanMetadata::exactType()`.
     * Propagated from `ContributedDescriptor::concreteTypeInfo`.
     */
    const std::type_info& (*exactTypeGetter)() = nullptr;

    /**
     * @brief Static-lifetime bean name string for `BeanMetadata::name()`.
     * Propagated from `ContributedDescriptor::beanName`.  Never null (may be "").
     */
    const char* nameStr = nullptr;

    /**
     * @brief Retained reflective method data, or `nullptr` when not retained.
     * Non-null only when `DiscoverOptions::retainAllMetadata = true` and the
     * descriptor originates from an annotated type (not a factory product or binding).
     */
    const ctr::BeanReflectiveData* reflectiveData = nullptr;

    // ── Polymorphic-exposure fields (SPEC-polymorphic-exposure) ──────────────

    /**
     * @brief DescriptorId of the primary (concrete-typed) descriptor.
     *
     * For a primary descriptor: equals `this` descriptor's own DescriptorId.
     * For an alias (exposed-base) descriptor: equals the concrete type's DescriptorId.
     * `kInvalidDescriptorId` before `start()` initialises it.
     *
     * Used by `materializeOne` to redirect alias resolution to the primary,
     * by `compatible<U>()` to walk the alias graph, and by
     * `materializeEagerSingletons()` to skip alias descriptors.
     */
    DescriptorId primaryDescriptor = kInvalidDescriptorId;

    /**
     * @brief Upcast thunk: `(void* concrete) → void* base`.
     *
     * Applied after the primary instance is materialized to yield the exposed pointer.
     * Equivalent to `static_cast<Base*>(static_cast<Concrete*>(p))`.
     * `nullptr` for primary descriptors (the concrete ptr IS the exposed ptr).
     */
    void*      (*adjustToExposed)(void*) = nullptr;

    /**
     * @brief Downcast thunk: `(void* base) → void* concrete`.
     *
     * Used by `cast<U>()` / `tryCast<U>()` to recover the concrete pointer before
     * re-adjusting to a different exposed type.
     * Equivalent to `static_cast<Concrete*>(static_cast<Base*>(p))`.
     * `nullptr` for primary descriptors AND for virtual-base aliases (downcast
     * from a virtual base is not expressible as a static_cast).
     */
    void*      (*adjustToConcrete)(void*) noexcept = nullptr;
};

} // namespace ctr::detail
