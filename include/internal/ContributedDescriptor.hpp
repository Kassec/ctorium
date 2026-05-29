#pragma once

#include <cstddef>
#include <cstdint>
#include <typeinfo>

#include "Identity.hpp"
#include "Lifetime.hpp"
#include "Origin.hpp"

namespace ctr::detail {

/**
 * @brief Compile-time descriptor produced at each `discover<>()` call site.
 *
 * Stored in a `constexpr static` array with static lifetime; transmitted to the
 * Registry as a `std::span<const ContributedDescriptor>`.  Never copied.
 *
 * ### Compile-time vs runtime fields
 * `ContributedDescriptor` is `constexpr`-constructible, so every field must be
 * computable at the call site instantiation point.  Type and name identification
 * therefore use:
 *  - **Qualified name strings** (`const char*`) via `std::meta::qualified_name_of`
 *    and `std::define_static_string` — compile-time stable, cross-TU consistent.
 *  - **`TypeInfoGetter<T>::get` function pointers** — constant expressions giving
 *    access to `typeid(T)` at runtime, used by `TypeInterning` during `start()` to
 *    populate the `std::type_index → TypeId` reverse map for `typeIdFor<T>()`.
 *
 * Dense `TypeId` and `NameId` indices are **not** stored here; they are assigned
 * by `Registry::start()` during the interning phase.  The runtime `Descriptor`
 * (built from this struct by `start()`) carries the final dense indices.
 *
 * ### Identity and deduplication
 * `identity` is a `consteval` 64-bit hash of: kind + declaring entity + produced
 * type + exposed type + name + factory method signature.  `start()` uses it for
 * O(1) deduplication across all submitted spans.  The `Identity` is consumed by
 * `start()` and does not appear in the runtime `Descriptor`.
 *
 * ### Thunk parameter convention
 * The second parameter of `construct`, `postConstruct`, and `preDestroy` thunks is
 * typed as `void*`.  At every call site in the engine it is a pointer to a
 * stack-allocated `ctr::detail::ResolutionContext` (cast with `static_cast`).
 * The `void*` avoids a circular include dependency between `internal/` headers
 * and `detail/ResolutionContext.hpp`.
 */
struct ContributedDescriptor {
    // --- Identity (consumed at start(), not retained in runtime Descriptor) ---

    /** Data-based 64-bit hash; used by start() for O(1) deduplication then discarded. */
    Identity     identity;

    // --- Type identification (compile-time strings + runtime type_info getters) ---

    /**
     * @brief Qualified name of the exposed type (resolution interface).
     *
     * Compile-time string, e.g. `std::define_static_string(std::meta::qualified_name_of(^^MyService))`.
     * Used by `TypeInterning::internByName()` to assign a dense TypeId.
     * Must not be null or empty.
     */
    const char*  exposedTypeName;

    /**
     * @brief Qualified name of the concrete type (actual instantiated class).
     *
     * May differ from `exposedTypeName` when the bean is registered under a base
     * class or interface.  Must not be null or empty.
     */
    const char*  concreteTypeName;

    /**
     * @brief Function pointer returning `typeid(ExposedType)` at runtime.
     *
     * Set to `&TypeInfoGetter<ExposedType>::get`.  The address is a constant expression.
     * Called by `TypeInterning::internByName()` to register `std::type_index(typeid(T))`
     * in the reverse map that feeds `typeIdFor<T>()` at resolution time.
     */
    const std::type_info& (*exposedTypeInfo)();

    /**
     * @brief Function pointer returning `typeid(ConcreteType)` at runtime.
     *
     * Set to `&TypeInfoGetter<ConcreteType>::get`.  Used during `start()` to
     * intern the concrete type into `TypeInterning`.
     */
    const std::type_info& (*concreteTypeInfo)();

    /**
     * @brief Named qualifier for this bean.  Empty string means the unnamed key.
     *
     * Compile-time string, e.g. `std::define_static_string("userService")` or `""`.
     * Must not be null.  Interned to a `NameId` by `NameInterning::intern()` during
     * `start()`.
     */
    const char*  beanName;

    // --- Scheduling ---

    /** Priority used to arbitrate among candidates sharing the same exposed type and name. */
    std::int32_t priority;

    // --- Classification ---

    /** Scope lifetime governing instance sharing and destruction. */
    Lifetime     lifetime;
    /** How this descriptor was contributed to the registry. */
    Origin       origin;

    // --- Thunks (see parameter convention in class doc) ---

    /** Placement constructor: `(void* mem, void* ResolutionContext*)`. Must not be null. */
    void       (*construct)(void*, void*);
    /** In-place destructor: `(void* instance)`. Must not be null. */
    void       (*destroy)(void*);
    /** Post-construction hook: `(void* instance, void* ResolutionContext*)`. Null when absent. */
    void       (*postConstruct)(void*, void*);
    /** Pre-destruction hook: `(void* instance, void* ResolutionContext*)`. Null when absent. */
    void       (*preDestroy)(void*, void*);

    // --- Allocation parameters ---

    /** `sizeof` the concrete type, in bytes. */
    std::size_t  size;
    /** `alignof` the concrete type, in bytes. */
    std::size_t  align;

    // --- Factory linkage ---

    /**
     * @brief Identity of the factory method source.
     *
     * Set to `kNoFactoryMethod` when this descriptor is not factory-produced.
     * Resolved to a `DescriptorId` (`factoryMethodDescriptor` in `Descriptor`)
     * during `start()` Phase 2 via the local `Identity → DescriptorId` map.
     */
    Identity     factoryMethodIdentity;

    // --- Auto-allocating thunks (unique_ptr<T> factory products, Prototype lifetime) ---

    /**
     * @brief Combined allocate-and-construct thunk.  Non-null only for factory products
     * returning `unique_ptr<T>` with `Lifetime::Prototype`.  Signature: `void*(void* ctx)`.
     * The thunk calls the factory method and releases the `unique_ptr` ownership,
     * returning the live pointer.  Invariant: non-null ↔ `dealloc` non-null.
     */
    void*      (*allocAndConstruct)(void*) = nullptr;

    /**
     * @brief Deallocation thunk matching `allocAndConstruct`.  Calls `T::operator delete(p)`
     * (routes through class-specific deallocation if defined) without invoking the destructor
     * (which was already called by the `destroy` thunk).
     * Non-null ↔ `allocAndConstruct` non-null.
     */
    void       (*dealloc)(void*) = nullptr;
};

} // namespace ctr::detail
