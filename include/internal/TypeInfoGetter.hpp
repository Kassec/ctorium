#pragma once

#include <typeinfo>

#include "../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Compile-time source of `std::type_info` for a given C++ type.
 *
 * Provides a function pointer that returns `typeid(T)` at runtime.
 * The address `&TypeInfoGetter<T>::get` is a constant expression and can
 * therefore be stored in `constexpr` structures such as `ContributedDescriptor`.
 *
 * ### Rationale
 * `ContributedDescriptor` is a `constexpr static` array produced at the
 * `discover<>()` call site.  It must carry enough information for `start()` to
 * build both:
 *  - the dense TypeId interning table, keyed by qualified type name;
 *  - the `std::type_index → TypeId` reverse map, required by `typeIdFor<T>()`.
 *
 * `typeid(T)` cannot appear in a constant-evaluated context for polymorphic types
 * in C++26.  Using a pointer to `TypeInfoGetter<T>::get` sidesteps that restriction:
 * the pointer itself is a constant expression; the call to `get()` is deferred to
 * runtime (during `start()` or on the first `typeIdFor<T>()` access).
 *
 * ### Why not `&typeid(T)` directly?
 * Taking the address of a `typeid` expression yields `const std::type_info*`, but
 * `std::type_info` is a non-literal type and the resulting pointer may not be
 * accepted as a constant expression across all C++26 compiler implementations
 * (behaviour varies between Bloomberg Clang P2996 and others).  The
 * function-pointer indirection is universally portable.
 *
 * ### Usage in a constexpr ContributedDescriptor
 * ```cpp
 * .exposedTypeInfo  = &TypeInfoGetter<MyService>::get,
 * .concreteTypeInfo = &TypeInfoGetter<MyServiceImpl>::get,
 * ```
 */
template <typename T>
struct TypeInfoGetter {
    /**
     * @brief Returns the `std::type_info` for T.
     *
     * Called at runtime by `TypeInterning` during `start()` to populate the
     * `std::type_index → TypeId` reverse map, and by `typeIdFor<T>()` on first
     * resolution of T.
     *
     * The function pointer `&TypeInfoGetter<T>::get` is a constant expression.
     */
    static const std::type_info& get() noexcept { return typeid(T); }
};

} // namespace CTORIUM_NAMESPACE::detail
