#pragma once

// TODO: Implement SessionStore once ScopedContext internals and scope lifecycle
// are defined.
//
// Responsibilities:
//   - Stores one bean instance per (DescriptorId, ScopedContext key) pair.
//   - Destroyed on scope stop() / restart(); not recreated automatically on
//     restart() (the bound object came from user code or a factory with no
//     source to re-invoke — see specs-api §11.2 for bindSession semantics).
//   - Interface mirrors SingletonStore but is owned by ScopedContext, not Registry.
//
// Expected interface:
//   void  materialize(DescriptorId, construct thunk, destroy thunk, size, align, ResolutionContext&)
//   void* find(DescriptorId) const noexcept
//   void  destroyAll() noexcept   // called on scope stop()
