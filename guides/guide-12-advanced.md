# Guide 12 — Advanced

This guide covers two topics the earlier chapters used in passing but never treated head-on: the **error model** — the exception hierarchy and when each error fires — and **multiple contexts** — running several isolated containers in one process. Both are part of the public contract; neither is exotic, but both reward a deliberate look.

It assumes contexts ([Guide 03 — Contexts](guide-03-contexts.md)), qualifiers ([Guide 04 — Qualifiers](guide-04-qualifiers.md)), and sessions ([Guide 08 — Sessions](guide-08-sessions.md)).

---

## 1. The error hierarchy

**Ctorium** surfaces failures through typed exceptions, all rooted at `ctr::CtoriumError` (itself a `std::runtime_error`):

- `ctr::CtoriumError` — base for every public API failure; catch this to handle any **Ctorium** error uniformly.
- `ctr::ResolutionError` — bean resolution cannot be completed.
- `ctr::ContextStateError` — an operation is invalid for the context's current lifecycle state.
- `ctr::ConfigurationError` — public configuration is invalid.

Because all three derive from `CtoriumError`, which derives from `std::runtime_error`, a single `catch (const ctr::CtoriumError&)` catches every container error while leaving standard-library exceptions distinct. This is the diagnostics-first posture the library is built around: misuse surfaces as a typed, explicit error at the boundary where it occurs — never a silent fallback ([Guide 00 — Intro](guide-00-intro.md)). An error you can see and name is already half-debugged; one swallowed in silence is the kind you meet again at 3 a.m.

---

## 2. When each error fires

Each type maps to a recognizable failure class seen across the guides:

**`ResolutionError`** — the request cannot be satisfied: no candidate for the requested type and key, two candidates tied at the highest priority ([Guide 04 — Qualifiers](guide-04-qualifiers.md)), an invalid `cast<U>()` ([Guide 10 — Handles](guide-10-handles.md)), a default named selection pointing at a missing bean, or an unresolvable hook dependency ([Guide 06 — Hooks](guide-06-hooks.md)).

**`ContextStateError`** — the operation is wrong for the state: `discover<...>()` after `start()`, a scope started before its root, or a session bean resolved from a stopped or never-started scope ([Guide 08 — Sessions](guide-08-sessions.md)).

**`ConfigurationError`** — the configuration is contradictory: two unnamed producer methods for the same type ([Guide 09 — Factories](guide-09-factories.md)), a duplicate `(T, name, priority)` binding, or a `[[=ctr::scoped]]`/session mismatch caught at `start()`.

```cpp
try {
    ctr::Bean<Logger> logger = context.resolve<Logger>(ctr::named("missing"));
} catch (const ctr::ResolutionError& e) {
    // e.what() identifies the type and key involved
}
```

The messages name the type, key, operation, or call path involved — they are written to be read by someone debugging **Ctorium** without access to its source. Some misuse is caught earlier still, at compile time (multiple lifetime markers, multiple compatible constructors, an unsupported listener signature); those never reach a runtime exception at all.

---

## 3. Multiple contexts

Most programs need exactly one context. When a process genuinely needs several *independent* containers — an application world and a separate editor world, say — pass a **context key**:

```cpp
ctr::BeanContext& game   = ctr::BeanContext::resolveContext("game");
ctr::BeanContext& editor = ctr::BeanContext::resolveContext("editor");
```

`resolveContext(key)` is resolve-or-create per key, and resolving the same key again returns the same instance for as long as it is alive. The default context (no key) is simply one more entry in that table.

---

## 4. Isolation between contexts

Keyed contexts are **parallel peers, not a hierarchy**: there is no parent/child relationship, and nothing leaks between them. Each owns a separate registry, and with it its own descriptors, runtime bindings, listeners, named defaults, managed instances, and lifecycle state. A bean resolved from `"game"` is unrelated to a bean of the same type resolved from `"editor"` — different instances, different singletons, different listeners:

```cpp
game.discover<^^app>();
game.start();

editor.discover<^^app>();
editor.start();

ctr::Bean<Config> gameConfig   = game.resolve<Config>();    // game's singleton
ctr::Bean<Config> editorConfig = editor.resolve<Config>();  // editor's distinct singleton
```

Each context discovers and starts on its own; the same bean *types* can be discovered into both, producing independent instances. This total isolation is the reason to use multiple contexts at all: it is the cleanest way to keep two dependency graphs from sharing state.

A context key is any stable, copyable, hashable, comparable value naming the context. Scopes ([Guide 08 — Sessions](guide-08-sessions.md)) are a different mechanism — a scope shares its root's registry and adds a session-instance store, whereas keyed contexts share *nothing*. Reach for a scope when you want per-session state inside one container; reach for a second context when you want two containers.

---

## 5. Lifecycle across contexts

Each context runs its own `start()`/`stop()` independently, and stopping one never affects another. Stopping a context does stop its own scopes (a scope cannot outlive its root), and removes that context from the live table — a later `resolveContext(sameKey)` then creates a fresh, unstarted context. The per-context lifecycle rules from [Guide 03 — Contexts](guide-03-contexts.md) apply unchanged to each peer.

---

## 6. Thread-local best practices

A `threadLocal` bean has one instance per context, key, and thread. Two rules are easy to overlook:

A thread-local instance is destroyed with its full lifecycle (hooks, listeners) when its owning thread exits, on that exiting thread. This means `preDestroy` and `onDestroyed` run on a thread that is shutting down — keep those callbacks short and avoid blocking.

`root.stop()` destroys all remaining thread-local instances from threads still alive at that point. **Ctorium** does not lock per-bean access to protect user business state, so user code must ensure no concurrent access to thread-local instances while `stop()` runs. The safe pattern: signal worker threads to exit and join them before calling `root.stop()`.

```cpp
worker.join();       // all threads done
context.stop();      // safe: no concurrent TL access
```

The same applies to `scope.stop()` and `restart()` — **Ctorium** does not synchronize user-level concurrent business access during those calls; user code owns that synchronization.

---

## 7. Shutdown and exceptions

**Ctorium** uses a pass-through exception policy for user code: exceptions from constructors, factory methods, hooks, and listeners propagate unchanged and are not wrapped in a **Ctorium** exception.

There is one deliberate exception to this. During `stop()`, the destruction lifecycle is `noexcept`: any exception propagating from a `preDestroy` hook or from an `onPreDestroy`/`onDestroyed` listener callback calls `std::terminate`. Destruction hooks must not throw — this is the same contract C++ applies to destructors, extended to the hooks and listeners that run alongside them. The caller is responsible for ensuring that user hook and listener implementations do not propagate exceptions during shutdown.

---

## 8. Registration sources

A bean exists in a context only when it comes from one of four sources:

a scanned annotated type discovered through `discover<...>()`; a factory method belonging to a discovered factory; an explicit runtime binding via `bindSingleton<T>(...)` (root) or `bindSession<T>(...)` (scope); or the context itself — `ctr::BeanContext` is implicitly registered as a singleton bean of its own root context at `start()` ([Guide 03 — Contexts](guide-03-contexts.md)).

Merely mentioning a type as a C++ return type, field, or local variable does not register it. An external type produced by a factory or bound at runtime receives the minimal metadata needed by the public bean view; detailed method and annotation metadata are available only when the type is included in discovery roots with `retainAllMetadata`.

---

## 9. Mental model

Errors are typed and explicit: `CtoriumError` is the catch-all base; `ResolutionError` means "cannot satisfy the request," `ContextStateError` means "wrong lifecycle state," `ConfigurationError` means "contradictory configuration" — and the worst misuse is rejected at compile time before any exception. During shutdown, exceptions from hooks and listeners call `std::terminate`.

Multiple contexts are fully isolated peers selected by key, each with its own registry and lifecycle, sharing nothing — distinct from scopes, which share a root’s registry. One container is the default; reach for more only when you want genuinely separate worlds.

Thread-local beans are per-thread singletons destroyed on thread exit; user code must join all worker threads before `root.stop()`. A bean exists only from one of four sources (discovery, factory, runtime binding, or the context itself); mentioning a type in code does not register it.

---

## 10. Checklist

When using these features, check that:

- container errors are caught via `ctr::CtoriumError` (or a specific derived type) rather than a generic catch-all;
- the error type is matched to the failure class: resolution, context state, or configuration;
- `preDestroy` hooks and destruction listeners never throw — `std::terminate` otherwise;
- a single default context is used unless genuinely independent containers are needed;
- keyed contexts are treated as isolated peers, not a parent/child hierarchy;
- the same bean type discovered into two contexts is expected to yield independent instances;
- a scope is used for per-session state, a second context for a separate world;
- each context’s `start()`/`stop()` lifecycle is managed independently;
- worker threads using thread-local beans are joined before `root.stop()`;
- user code owns synchronization of scope/session and thread-local business access during `stop()` or `restart()`.

Next guide: [Guide 13 — Performance](guide-13-performance.md).
