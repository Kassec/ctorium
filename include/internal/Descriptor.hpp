#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <typeinfo>

#include "../api/ctr/Config.hpp"

#include "DescriptorId.hpp"
#include "Lifetime.hpp"
#include "NameId.hpp"
#include "Origin.hpp"
#include "TypeId.hpp"

namespace CTORIUM_NAMESPACE { struct BeanReflectiveData; } // forward decl

namespace CTORIUM_NAMESPACE::detail {

using SessionSlot = std::uint32_t;
inline constexpr SessionSlot kInvalidSessionSlot = std::numeric_limits<SessionSlot>::max();

// The second parameter of construct/postConstruct/preDestroy thunks is void*.
// At every call site in the engine it is a ctr::detail::ResolutionContext*
// (cast with static_cast). The void* avoids a circular include dependency
// between this internal header and detail/ResolutionContext.hpp.

/**
 * @brief Cold runtime descriptor data stored out of the hot descriptor table.
 *
 * Fields here are read only during metadata inspection, casting, materialization,
 * destruction, eager-start selection, or startup validation. `DescriptorTable`
 * stores these blocks in a parallel vector indexed by the same `DescriptorId`
 * as the hot `Descriptor`.
 */
struct DescriptorCold {
    /** TypeId of the type exposed to resolution callers. */
    TypeId       exposedType;
    /** TypeId of the actual instantiated concrete type. */
    TypeId       concreteType;
    /** NameId qualifier; kUnnamed when no named annotation is present. */
    NameId       name;
    /**
     * sizeof the concrete type, in bytes.
     * When `size == 0`, the memory is externally owned;
     * `executeDestructionLifecycle` skips `::operator delete` for this descriptor.
     */
    std::size_t  size;
    /** alignof the concrete type, in bytes. */
    std::size_t  align;
    /** DescriptorId of the source factory method, or kInvalidDescriptorId when not factory-produced. */
    DescriptorId factoryMethodDescriptor = kInvalidDescriptorId;
    /** Unqualified producer method name for `BeanMetadata::factoryMethod()`. */
    const char*  factoryMethodName = nullptr;
    /** How this descriptor was contributed to the registry. */
    Origin       origin;

    /**
     * @brief Whether materialization is deferred until first resolution.
     *
     * Propagated from `[[=ctr::singleton{.lazy = ...}]]`; always `true` for non-singleton
     * lifetimes (lazy is meaningless for prototype/session/threadLocal).
     * `false` means `materializeEagerSingletons()` constructs this instance at `start()`.
     */
    bool lazy = true;

    /** Placement constructor thunk: (void* mem, void* registry). Must not be null. */
    void       (*construct)(void*, void*);
    /** In-place destructor thunk: (void* instance). Must not be null. */
    void       (*destroy)(void*) noexcept;
    /** Post-construction callback thunk: (void* instance, void* registry). Nullable. */
    void       (*postConstruct)(void*, void*);
    /** Pre-destruction callback thunk: (void* instance, void* registry). Nullable. */
    void       (*preDestroy)(void*, void*);

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
     * `::operator delete(mem, size, align_val)`. Calls `T::operator delete(p)`
     * (plain deallocation, no destructor because `destroy` already called `~T()`).
     * Non-null iff `allocAndConstruct` is non-null.
     */
    void       (*dealloc)(void*) noexcept = nullptr;

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
     * Propagated from `ContributedDescriptor::beanName`. Never null (may be "").
     */
    const char* nameStr = nullptr;

    /**
     * @brief Retained reflective method data, or `nullptr` when not retained.
     * Non-null only when `DiscoverOptions::retainAllMetadata = true` and the
     * descriptor originates from an annotated type (not a factory product or binding).
     */
    const CTORIUM_NAMESPACE::BeanReflectiveData* reflectiveData = nullptr;

    /**
     * @brief Downcast thunk: `(void* base) -> void* concrete`.
     *
     * Used by `cast<U>()` / `tryCast<U>()` to recover the concrete pointer before
     * re-adjusting to a different exposed type.
     * `nullptr` for primary descriptors and for virtual-base aliases.
     */
    void*      (*adjustToConcrete)(void*) noexcept = nullptr;
};

/**
 * @brief Hot runtime descriptor stored in the registry descriptor table.
 *
 * Built by start() from a ContributedDescriptor: the Identity is consumed for deduplication
 * and discarded; factoryMethodIdentity is resolved to DescriptorCold::factoryMethodDescriptor.
 *
 * This block keeps only fields needed by resolution arbitration and handle
 * dereference forms. Rarely-read metadata, casting, construction, and destruction
 * fields live in `DescriptorCold`.
 */
struct Descriptor {
    /** Priority used to arbitrate among candidates sharing the same exposedType and name. */
    std::int32_t priority;
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
    /** Dense per-session descriptor slot; kInvalidSessionSlot for non-session descriptors. */
    SessionSlot  sessionSlot = kInvalidSessionSlot;
    /** Scope lifetime governing instance sharing. */
    Lifetime     lifetime;

    /**
     * @brief Upcast thunk: `(void* concrete) -> void* base`.
     *
     * Applied by `Bean<T>::operator->` forms 2/3 after the primary instance is
     * found or materialized. Kept hot to avoid a cold-block load on dereference.
     * `nullptr` for primary descriptors (the concrete ptr is the exposed ptr).
     */
    void*      (*adjustToExposed)(void*) = nullptr;
};

} // namespace CTORIUM_NAMESPACE::detail
