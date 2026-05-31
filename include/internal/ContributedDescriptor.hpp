#pragma once

#include <cstddef>
#include <cstdint>
#include <typeinfo>

#include "Identity.hpp"
#include "Lifetime.hpp"
#include "Origin.hpp"

namespace ctr { struct BeanReflectiveData; } // forward decl for metadata field

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
/**
 * @brief Per-injection-parameter metadata stored alongside `ContributedDescriptor`.
 *
 * Produced at `consteval` time in `BeanDescriptorGen` for each `Bean<U>` constructor
 * parameter.  Carried in a `constexpr static` array with static lifetime.
 */
struct ContributedParamDescriptor {
    /** Qualified name of the injected type `U` (e.g. `"ns::MyService"`). */
    const char* injectedTypeName;
    /** `typeid(U)` getter — allows `lookupByTypeIndex` in `start()` without
     *  a name-based lookup.  Always non-null. */
    const std::type_info& (*injectedTypeInfo)();
    /**
     * @brief True when `[[=ctr::scoped{...}]]` is present on this parameter.
     *
     * Presence detection only; the scope name itself is carried in `scopeName`.
     * Kept as a distinct flag so graph validation can branch on presence without
     * inspecting the (possibly empty) name value.
     */
    bool hasScopedAnnotation;
    /** True when `[[=ctr::named{...}]]` is present on this parameter. */
    bool hasNamedAnnotation;
    /**
     * @brief Scope name from `[[=ctr::scoped{.name=...}]]`; `""` when absent or unnamed.
     *
     * Always a static-storage `const char*`: the consteval descriptor builder
     * normalizes the extracted annotation member through `std::define_static_string`,
     * so the stored pointer is template-argument-equivalent and valid in this
     * `constexpr` descriptor regardless of how the annotation was written at the
     * call site (raw literal or `define_static_string`).  Meaningful only when
     * `hasScopedAnnotation` is true.
     *
     * NOTE: extracting a `const char*` annotation member is supported (see the
     * `MetaExtract` toolchain probe); the earlier claim that `reflect_constant`
     * could not do so was unverified and is contradicted by that probe and by
     * `BeanDescriptorGen::scanAnnotations`.  The remaining work for scoped beans is
     * the session proxy-injection path (specs-internal §10.3, `Bean::makeDeferred`
     * / `proxyResolve_`), not annotation extraction.  Until that path is wired this
     * field feeds graph-validation diagnostics that need to name the scope.
     */
    const char* scopeName;
};

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

    // --- Lazy/eager ---

    /**
     * @brief Whether materialization is deferred until first resolution.
     *
     * Meaningful only for `Lifetime::Singleton`.  Propagated from the `lazy` field
     * of the `[[=ctr::singleton]]` annotation.  `true` = lazy (default); `false` = eager
     * (`materializeEagerSingletons()` constructs the instance at `start()`).
     */
    bool lazy = true;

    // --- Classification ---

    /** Scope lifetime governing instance sharing and destruction. */
    Lifetime     lifetime;
    /** How this descriptor was contributed to the registry. */
    Origin       origin;

    // --- Thunks (see parameter convention in class doc) ---

    /** Placement constructor: `(void* mem, void* ResolutionContext*)`. Must not be null. */
    void       (*construct)(void*, void*);
    /** In-place destructor: `(void* instance)`. Must not be null. */
    void       (*destroy)(void*) noexcept;
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

    /**
     * @brief Unqualified producer method name for factory products.
     *
     * Static-lifetime string set only for `Origin::FactoryProduct`; null for
     * annotated types, factory descriptors, runtime bindings, and aliases.
     */
    const char* factoryMethodName = nullptr;

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
    void       (*dealloc)(void*) noexcept = nullptr;

    // --- Injection parameter metadata (for graph validation at start()) ---

    /**
     * @brief Per-parameter injection metadata for graph validation.
     *
     * Each element describes one `ctr::Bean<U>` constructor parameter of the bean.
     * Populated by `BeanDescriptorGen::makeDescriptorForAnnotatedType` for scanned
     * beans with injectable constructors.  Null (with `paramCount == 0`) for default
     * constructors, factory products, and factories.
     *
     * Used by `Registry::start()` Phase 3.5 to detect:
     *  (a) `[[=ctr::scoped{...}]]` on a non-`session` dependency target.
     *  (b) `session` dependency injected into a non-session bean without `[[=ctr::scoped]]`.
     */
    const ContributedParamDescriptor* params = nullptr;
    /** Number of elements in `params`. Zero when `params` is null. */
    std::size_t paramCount = 0;

    // ── Bean-metadata fields (SPEC-bean-metadata) ────────────────────────────

    /**
     * @brief Retained reflective method data; `nullptr` when `retainAllMetadata = false`.
     * Produced by `makeReflectiveData<T>()` in `BeanDescriptorGen.hpp`.
     */
    const ctr::BeanReflectiveData* reflectiveData = nullptr;

    // ── Polymorphic-exposure fields (SPEC-polymorphic-exposure) ──────────────

    /**
     * @brief True when this descriptor is a polymorphic alias for a base class.
     *
     * Alias descriptors are generated by `makeExposedDescriptors` for each
     * accessible direct public base of a discovered type.  They are linked to
     * their primary (concrete) descriptor during `start()` Phase 2 via
     * `aliasOfPrimaryIdentity`.
     */
    bool isExposedAlias = false;

    /**
     * @brief Identity of the primary (concrete) descriptor to link against.
     *
     * Set for alias descriptors; `kNoFactoryMethod` (zero) for non-aliases.
     * Resolved to a `DescriptorId` (`primaryDescriptor` in `Descriptor`) during
     * `start()` Phase 2 via the local `Identity → DescriptorId` map, mirroring
     * the `factoryMethodIdentity` resolution pattern.
     */
    Identity aliasOfPrimaryIdentity = kNoFactoryMethod;

    /**
     * @brief Upcast thunk: `(void* concrete) → void* base`.
     *
     * Non-null for alias descriptors.  Equivalent to
     * `static_cast<Base*>(static_cast<Concrete*>(p))`.
     */
    void* (*adjustToExposed)(void*) = nullptr;

    /**
     * @brief Downcast thunk: `(void* base) → void* concrete`.
     *
     * Non-null for non-virtual alias descriptors.  `nullptr` for virtual-base
     * aliases (downcast from a virtual base is ill-formed as a static_cast).
     */
    void* (*adjustToConcrete)(void*) noexcept = nullptr;
};

} // namespace ctr::detail
