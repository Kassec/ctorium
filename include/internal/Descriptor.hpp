#pragma once

#include <cstddef>
#include <cstdint>

#include "DescriptorId.hpp"
#include "Lifetime.hpp"
#include "NameId.hpp"
#include "Origin.hpp"
#include "TypeId.hpp"

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
    void       (*destroy)(void*);
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
     * @brief Combined allocate-and-construct thunk for `unique_ptr<T>` factory products
     * with `Lifetime::Prototype`.  When non-null, the prototype path of `resolve<T>`
     * skips `::operator new` and calls this thunk instead; the thunk returns the
     * live pointer.  Invariant: `allocAndConstruct != nullptr ↔ dealloc != nullptr`.
     * Null for all other descriptors (annotated types, factories, value-return products,
     * singleton factory products).
     */
    void*      (*allocAndConstruct)(void*) = nullptr;

    /**
     * @brief Deallocation thunk matching `allocAndConstruct`.
     * When non-null, `executeDestructionLifecycle` calls this instead of
     * `::operator delete(mem, size, align_val)`.  Calls `T::operator delete(p)`
     * (plain deallocation, no destructor — `destroy` already called `~T()`).
     * Non-null ↔ `allocAndConstruct` non-null.
     */
    void       (*dealloc)(void*) = nullptr;
};

} // namespace ctr::detail
