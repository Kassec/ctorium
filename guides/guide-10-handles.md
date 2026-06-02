# Guide 10 — Handles

A **handle** is the object user code holds in place of a managed bean: `ctr::Bean<T>` (typed) and `ctr::AnyBean` (type-erased). Earlier guides used handles constantly — resolving returns one, injection stores one, listeners receive one. This guide consolidates what a handle *is*: its internal forms, its tracking semantics, the cast helpers, and how it finds its context. You never own the managed object; you hold a handle to it, and the handle is the contract.

This guide assumes beans and lifetimes ([Guide 01 — Beans](guide-01-beans.md)), sessions ([Guide 08 — Sessions](guide-08-sessions.md)), and listeners ([Guide 07 — Listeners](guide-07-listeners.md)).

---

## 1. Three forms behind one type

`ctr::Bean<T>` looks like one type, but internally it takes one of three mutually exclusive **forms**, chosen by the bean's lifetime. The form is invisible at the call site — every form answers `operator->`, `operator*`, and `value()` — but it explains the behaviour you observe.

**Form 1 — Direct.** Used for singleton and prototype beans, whose resolved instance is stable. The handle holds the object pointer directly, so `operator->` returns it immediately with no registry interaction. This is the common case and the fast one.

**Form 2 — Proxy.** Used for all session beans, whether resolved directly or injected through `[[=ctr::scoped]]` ([Guide 08 — Sessions](guide-08-sessions.md)). The handle holds no object pointer; `operator->` resolves the scope's *current* session instance on every call, which is why a session handle tracks across a scope `restart()` and returns `nullptr` — never throws — while the scope is stopped.

**Form 3 — ThreadLocal.** Used for thread-local beans. `operator->` resolves the calling thread's instance from the thread-local store on every dereference and never caches the pointer, so two threads holding "the same" handle reach their own instances. This makes a thread-local handle *thread-portable but thread-relative*: passing it to another thread is allowed, and dereferencing it there resolves **that** thread's instance. Crucially, if the receiving thread has no instance yet, **the dereference materializes one on the spot** — it runs the bean's full construction lifecycle (constructor, `postConstruct`, `onCreated`) on the receiving thread, right there in the `operator->` call. So handing a thread-local handle to a new thread is not a cheap read: the first access on each thread is a *creation*. The handle never points back to the originating thread's object; it always follows — and lazily populates — the thread that dereferences it.

The practical upshot: a Form 1 dereference is a stable pointer read; Form 2 and Form 3 re-resolve per access. That difference matters for hot-loop access (see [Guide 13 — Performance](guide-13-performance.md)) and is why a session handle can legitimately yield `nullptr`.

---

## 2. Dereferencing, and the nullptr rule

All three forms expose the object the same way:

```cpp
ctr::Bean<GameConfig> config = context.resolve<GameConfig>();

config->width;      // operator->
(*config).height;   // operator*
config.value();     // value()
```

For Form 1 the pointer is always valid while the handle is non-empty. For Form 2 (session), `operator->` returns `nullptr` when the target scope is stopped, so a long-lived session handle must be checked before use:

```cpp
if (sessionHandle.operator->() != nullptr) {
    sessionHandle->doWork();
}
```

`operator*` and `value()` have a precondition: `operator->()` must be non-null. Dereferencing an empty handle, or a Form 2 handle whose scope is inactive, is undefined behaviour — check first. The container will not throw to protect you here; this is the one spot where a handle expects you to look before you leap.

---

## 3. Tracking semantics

A handle is **tracked**: copy, move, and destruction participate in the bean's lifetime, but how much work that is depends on the form.

For **singletons** the instance belongs to the context, so handle copy and move are cheap struct copies with no atomic operations and no refcount — the handle's lifetime does not govern the bean's. For **prototypes**, the handle *is* the lifetime: copy increments an atomic reference count, destruction decrements it, and the object is destroyed when the count reaches zero. Move transfers ownership and leaves the source empty in every form.

```cpp
ctr::Bean<FrameStats> first  = context.resolve<FrameStats>();  // prototype, refcount = 1
ctr::Bean<FrameStats> second = first;                          // refcount = 2
// last handle destroyed → instance destroyed
```

This is why [Guide 01](guide-01-beans.md) insists on storing the handle, not a raw pointer: the handle is what keeps a prototype alive and what tells the container a session or thread-local handle still refers to something. An empty or moved-from handle (both `object_` and registry null) must not be dereferenced.

---

## 4. Casting between exposed types

A bean resolved through one type can be examined as a more specific one. Both `Bean<T>` and `AnyBean` expose the same four helpers:

- `exact<U>()` — is the concrete runtime type *exactly* `U`?
- `compatible<U>()` — can the bean be exposed as `U`?
- `cast<U>()` — return `Bean<U>`, or throw `ctr::ResolutionError` if incompatible.
- `tryCast<U>()` — return `std::optional<Bean<U>>`, empty if incompatible.

```cpp
ctr::Bean<InputService> input = context.resolve<InputService>();

if (auto keyboard = input.tryCast<KeyboardInputService>()) {
    (*keyboard)->enableKeyboardCapture();
}
```

Use `cast` when you expect success and want a loud failure; use `tryCast` when the type may legitimately be absent. A cast keeps tracking the same managed instance — it does not re-resolve or allocate — so casting a prototype handle shares its refcount rather than producing a second object. The handle changes its mind about *what* it's looking at, never about *which* object.

---

## 5. `AnyBean`: the type-erased handle

`ctr::AnyBean` is the type-erased counterpart that global listeners receive (`context.on(phase, [](const ctr::AnyBean& bean){ ... })`, [Guide 07 — Listeners](guide-07-listeners.md)). It shares `Bean<T>`'s internal layout and the same three forms, holding `void*` instead of `T*`, which is exactly why `cast<U>()` and `tryCast<U>()` from an `AnyBean` are cheap reinterpretations rather than copies:

```cpp
context.on(ctr::onCreated,
    [](const ctr::AnyBean& bean) {
        if (auto pass = bean.tryCast<RenderPass>()) {
            (*pass)->prepare();
        }
    });
```

`AnyBean` carries the same tracking semantics as `Bean<T>` (copy retains, move transfers, destroy releases, prototype refcount included), so the rules from §3 apply unchanged. Use `AnyBean` when the observing code is genuinely cross-cutting and narrows to a type only when needed; use a typed `Bean<T>` everywhere the type is known.

---

## 6. Reaching the context from a handle

Any handle can produce the context that emitted it, in every form:

```cpp
ctr::BeanContext& owner = config.context();
```

For root-owned beans (singleton, prototype) this is the root `BeanContext`; for a session bean it is the owning `ScopedContext`. The handle stores no extra context pointer — it recovers the context through its registry anchor — which keeps the handle small (24 bytes on 64-bit) while preserving access. That omission is deliberate, not a micro-optimization stumbled into: a context owner on every handle would enlarge `ctr::Bean<T>` and add ownership work to the common singleton/prototype path for a relationship the registry already encodes. `context()` accordingly returns a non-owning `BeanContext&` — it does not extend the context's lifetime, so the shutdown rule from [Guide 03 — Contexts](guide-03-contexts.md) still applies: do not use a recovered context, or the handle it came from, after `stop()`. Treat that access deliberately, as [Guide 03 — Contexts](guide-03-contexts.md) notes: a handle that reaches its context can reach the whole container.

---

## 7. Identity comparison

Handles compare by *logical identity*: two are equal when they refer to the same logical bean — same form, same object pointer (Form 1) or same scope-and-descriptor pair (Form 2/3), same registry:

```cpp
ctr::Bean<GameConfig> a = context.resolve<GameConfig>();
ctr::Bean<GameConfig> b = context.resolve<GameConfig>();
bool same = (a == b);   // true for a singleton: same instance
```

For a singleton, two resolutions compare equal; for a prototype, each resolution is a distinct bean and compares unequal. Comparison reads handle fields only — it does not dereference — so it is safe even on a Form 2 handle whose scope is stopped.

---

## 8. Mental model

A handle is a small tracked reference, not the object and not ownership. Read it as: `Bean<T>` (typed) and `AnyBean` (erased) share one 24-byte layout and three forms — Direct (singleton/prototype, stable pointer), Proxy (session, re-resolved per access, `nullptr` when the scope is stopped), and ThreadLocal (per-thread, re-resolved per access); copy/move/destroy track the bean, cheaply for singletons and via atomic refcount for prototypes; `exact/compatible/cast/tryCast` reinterpret the same instance without re-resolving; and `context()` recovers the owning context through the registry. Hold the handle while you need the bean, check Form 2 for `nullptr`, and never dereference an empty or moved-from handle.

---

## 9. Checklist

When working with handles, check that:

- the object is reached only through `operator->`, `operator*`, or `value()`, never a stored raw pointer;
- session (Form 2) handles are checked for a `nullptr` dereference target before use;
- `operator*` / `value()` are called only when `operator->()` is non-null;
- prototype handles are kept alive for as long as the instance is needed (handle lifetime *is* the bean lifetime);
- `cast`/`tryCast` are used for type narrowing, with `tryCast` when absence is legitimate;
- `AnyBean` is used for cross-cutting observation and narrowed with the same cast helpers;
- `context()` is used to recover the owning context rather than storing one separately;
- empty or moved-from handles are never dereferenced.

Next guide: [Guide 11 — Headers](guide-11-headers.md).
