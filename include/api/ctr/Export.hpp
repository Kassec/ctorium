#pragma once

/**
 * @brief Public export/import decoration for Ctorium API symbols.
 *
 * This macro is intentionally configured by CMake. In the current header-only
 * mode, it defaults to an empty value.
 */
#ifndef CTORIUM_API
#define CTORIUM_API
#endif

/**
 * @brief Force-inline decoration for tiny hot-path helpers (header-only mode).
 *
 * The prototype refcount helpers (`Bean<T>` / `AnyBean` retain/release) must be
 * inlined into the handle special members so the singleton/empty fast path
 * carries no call (benchmarks: BM_Handle_Singleton_Copy / _Move).  GCC's
 * size-based auto-inliner is sensitive to unrelated TU changes, which once
 * flipped the decision and regressed BM_Handle_Singleton_Move (0.93 → 1.70 ns);
 * pinning the decision keeps the fast path stable.
 *
 * In dynamic-link mode the helpers live in the compiled library (no body is
 * visible to callers), so the macro must expand to nothing.
 */
#ifndef CTORIUM_HOT_INLINE
#ifdef CTORIUM_DYNAMIC_LINK
#define CTORIUM_HOT_INLINE
#else
#define CTORIUM_HOT_INLINE [[gnu::always_inline]] inline
#endif
#endif
