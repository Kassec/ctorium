#pragma once

#include "../../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE {
class ScopedContext; // forward declaration — full type not required here
} // namespace CTORIUM_NAMESPACE

namespace CTORIUM_NAMESPACE::detail {

class Registry; // forward declaration

/**
 * @brief Transient, non-owning context passed to all construct, destroy, and hook thunks.
 *
 * Thunk signatures in `ContributedDescriptor` and `Descriptor` use `void*` for
 * the context parameter to avoid a circular include dependency between the
 * `internal/` descriptor headers and this `detail/` header.  At every call site
 * in the engine that `void*` is a pointer to a stack-allocated `ResolutionContext`,
 * obtained via `static_cast<ResolutionContext*>(voidCtx)`.
 *
 * ### Why not raw `Registry&`?
 * Materializing a session bean is scope-dependent: the same thunk must resolve its
 * dependencies against the active `ScopedContext`, not always against the root
 * registry.  `ResolutionContext` carries both the shared registry and the optional
 * active scope in a single, explicit, stack-allocated value — no global or
 * thread-local mutable state is required to propagate scope through the call chain.
 *
 * ### Lifetime contract
 * `ResolutionContext` is non-owning.  Both `registry` and `scope` (when non-null)
 * are guaranteed to outlive the thunk call.  Thunks must not store a pointer or
 * reference to a `ResolutionContext` beyond their own stack frame.
 *
 * ### Forwarding rule
 * A thunk that triggers nested resolutions (constructor injection, hook parameters)
 * must forward the **same** `ResolutionContext` to each nested call.  Constructing
 * a new context mid-chain would lose the active scope and silently break
 * session-scoped dependency injection.
 *
 * ### Thread safety
 * Each resolution path creates its own stack-allocated `ResolutionContext`.
 * The struct contains no shared mutable state; thread safety is governed entirely
 * by the components accessed through `registry` and `scope`.
 */
struct ResolutionContext {
    /**
     * @brief Root-level registry: type index, descriptor table, stores, defaults.
     *
     * Always valid for the duration of the thunk call.  Provides `resolve<T>`,
     * `typeIdFor<T>`, access to all bean stores, listener dispatch, and all
     * shared context state.
     */
    Registry& registry;

    /**
     * @brief Active scope for session-bean resolution; null for root-bean resolution.
     *
     * Non-null only when materializing a session bean within a known `ScopedContext`.
     * Root-bean thunks (singleton, prototype) always receive a null scope and
     * must not dereference this pointer.
     *
     * A thunk resolving session-scoped dependencies uses this field to target the
     * correct scope's `SessionStore` rather than the root store.
     */
    CTORIUM_NAMESPACE::ScopedContext* scope = nullptr;
};

} // namespace CTORIUM_NAMESPACE::detail
