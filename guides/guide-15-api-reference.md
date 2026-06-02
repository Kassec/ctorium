# Guide 15 — API Reference

Every public type, method, option, marker, and error in the `ctr` namespace, in one place. This is a contract reference, not a tutorial — each entry says what it does and what it promises, with a cross-reference to the guide that explains it in context. Read the guides for the *why*; come here for the *what, exactly*.

Organized by type. Signatures are simplified for readability (template noise trimmed, defaults shown).

---

## 1. `ctr::BeanContext`

Root DI context. Not publicly constructible — obtained only through `resolveContext`.

| Method | Returns | Contract |
|---|---|---|
| `resolveContext()` | `BeanContext&` | Creates or retrieves the default root context. Same call = same instance. **Valid until `stop()` is called — dangling after.** ([§5.1](guide-03-contexts.md)) |
| `resolveContext(key)` | `BeanContext&` | Creates or retrieves the root for `key`. Same key = same root; distinct keys = fully isolated roots. **Valid until `stop()` is called — dangling after.** ([§5.1](guide-03-contexts.md)) |
| `discover<^^Roots...>(options)` | `BeanContext&` | Registers compile-time reflection roots. Pre-`start()` only; post-`start()` → `ContextStateError`. Idempotent (deduplicates). ([§6](guide-02-discovery.md)) |
| `start()` | `BeanContext&` | Activates the context runtime. Merges contributions, validates, indexes, materializes eager singletons. Idempotent after success. ([§5.3](guide-03-contexts.md)) |
| `stop()` | `void` | **Terminal.** Destroys all scopes, singletons, thread-locals, releases the global-table entry. **The context reference becomes dangling; all `Bean<T>` handles and `ListenerHandle`s issued from this context must not be used after this call — undefined behavior.** ([§10](guide-03-contexts.md)) |
| `resolve<T>()` | `Bean<T>` | Resolves one bean compatible with `T` from the unnamed space. Materializes if needed. ([§10](guide-04-qualifiers.md)) |
| `resolve<T>(named("x"))` | `Bean<T>` | Resolves `T` from the named space `"x"`. ([§10](guide-04-qualifiers.md)) |
| `defaultNamed<T>("x")` | `BeanContext&` | Sets the runtime default named key for `T`. `x` must be non-empty — empty string → `ConfigurationError`. Affects future resolutions, not existing beans. Requires started context. ([§4](guide-04-qualifiers.md)) |
| `defaultNamed<T>(nullptr)` | `BeanContext&` | Clears the default for `T`. ([§4](guide-04-qualifiers.md)) |
| `bindSingleton<T>(object, options)` | `BeanContext&` | Transfers ownership of `object` as a root singleton. Returns the context for chaining. Duplicate `(T, name, priority)` → `ConfigurationError`. ([§11.1](guide-05-bindSingleton.md)) |
| `resolveScope(key)` | `ScopedContext&` | Creates or retrieves the scope for `key` under this root. ([§5.2](guide-08-sessions.md)) |
| `on<T>(phase, callback, options)` | `ListenerHandle` | Registers a typed listener for a lifecycle phase. Root listener observes all beans (root + all scopes). ([§14](guide-07-listeners.md)) |
| `on(phase, callback, options)` | `ListenerHandle` | Registers a global listener (receives `const AnyBean&`). Root listener observes all beans. ([§14](guide-07-listeners.md)) |
| `remove(handle)` | `void` | Unregisters a listener by handle. Idempotent. ([§14.4](guide-07-listeners.md)) |

**Errors raised by `BeanContext`:**
- `ContextStateError` — `discover` after `start()`; `defaultNamed` before `start()`; `resolve` on a stopped context; `resolve<session>` from root.
- `ResolutionError` — no candidate; ambiguity; cycle; no compatible constructor; incompatible factory.
- `ConfigurationError` — duplicate `(T, name, priority)` binding; `bindSingleton<BeanContext>`; marker conflicts; `defaultNamed<T>("")` (empty string is not a valid qualifier).

**Lifetime rule:** the `BeanContext&` returned by `resolveContext()` is valid only while the context is alive. `stop()` removes the context from the live table: the reference becomes dangling, and all `Bean<T>` handles and `ListenerHandle`s issued from that context must not be used afterward — doing so is undefined behavior. Release handles before calling `stop()`. ([§10](guide-03-contexts.md))

---

## 2. `ctr::ScopedContext`

Scoped context owned by a root. Publicly inherits `BeanContext`. Adds session lifecycle and `userData`.

| Method | Returns | Contract |
|---|---|---|
| `start()` | `ScopedContext&` | Starts the scope's session store. Requires root started. Idempotent. ([§5.3](guide-08-sessions.md)) |
| `stop()` | `ScopedContext&` | Destroys session instances in reverse order. Preserves key, `userData`, tracked handles. Idempotent. ([§5.3](guide-08-sessions.md)) |
| `restart()` | `ScopedContext&` | `stop()` then `start()`. Replaces runtime objects without affecting siblings or root. ([§5.3](guide-08-sessions.md)) |
| `bindSession<T>(object, options)` | `ScopedContext&` | Transfers ownership of `object` as a session instance. Returns the scope for chaining. Duplicate `(T, name, priority)` within the same scope cycle → `ConfigurationError`. ([§11.2](guide-05-bindSingleton.md)) |
| `resolveScope(key)` | `ScopedContext&` | Delegates to the owning root — creates or retrieves a sibling scope. ([§5.2](guide-08-sessions.md)) |
| `userData(value)` | `ScopedContext&` | Attaches mutable user data. Survives `stop()`/`restart()`. ([§5.4](guide-08-sessions.md)) |
| `userData(nullptr)` | `ScopedContext&` | Clears user data without destroying it. ([§5.4](guide-08-sessions.md)) |
| `userData<T>()` | `optional<reference_wrapper<T>>` | Returns user data if type matches exactly; `nullopt` otherwise. ([§5.4](guide-08-sessions.md)) |
| `userData<T>() const` | `optional<reference_wrapper<const T>>` | Const variant. ([§5.4](guide-08-sessions.md)) |

**Inherited from `BeanContext`:** `resolve`, `defaultNamed`, `bindSingleton` (delegates to root), `on`, `remove`.

**Scope-specific behaviors:**
- `discover<>()` → `ContextStateError` (always).
- `on<T>(...)` on a scope → listener observes **only** that scope's beans.
- `defaultNamed<T>("x")` on a scope → default local to that scope; does not propagate to root or siblings.
- `bindSingleton<T>(...)` on a scope → delegates to the owning root.

---

## 3. `ctr::Bean<T>`

Typed tracked handle. Does not own the object.

| Method | Returns | Contract |
|---|---|---|
| `operator->()` | `T*` | Returns the current object. For session/scoped beans, re-resolves per access. Returns `nullptr` for a deferred handle whose scope is stopped. Propagates constructor exceptions for Form 2/3 during lazy materialization. ([§7.1](guide-10-handles.md)) |
| `operator*()` | `T&` | Dereferences the current object. ([§7.1](guide-10-handles.md)) |
| `value()` | `T&` | Returns a reference to the current object. ([§7.1](guide-10-handles.md)) |
| `context()` | `BeanContext&` | Returns the real owner: root for root-owned beans, `ScopedContext` for scoped beans. ([§5.5](guide-03-contexts.md)) |
| `metadata()` | `BeanMetadata` | Returns a const view on the public bean metadata. ([§15](guide-10-handles.md)) |
| `exact<U>()` | `bool` | Tests the exact concrete runtime type. ([§7.2](guide-10-handles.md)) |
| `compatible<U>()` | `bool` | Tests exposed compatibility with `U`. ([§7.2](guide-10-handles.md)) |
| `cast<U>()` | `Bean<U>` | Returns `Bean<U>`. Raises `ResolutionError` if incompatible. ([§7.2](guide-10-handles.md)) |
| `tryCast<U>()` | `optional<Bean<U>>` | Returns the cast result or `nullopt`. No exception. ([§7.2](guide-10-handles.md)) |
| `operator==` / `operator!=` | `bool` | Compares logical handle identity. ([§7.1](guide-10-handles.md)) |

**Copy/move:** copying retains the same logical bean; moving transfers the handle; destroying releases the tracking reference. Prototype copy increments a refcount (atomic); singleton copy is a plain struct copy.

**Lifetime rule:** using a handle after the owning context has been permanently stopped is undefined behavior.

---

## 4. `ctr::AnyBean`

Type-erased tracked handle. Used by global listeners, passed by `const&`.

Same contract as `Bean<T>` for: `context()`, `metadata()`, `exact<U>()`, `compatible<U>()`, `cast<U>()`, `tryCast<U>()`, `operator==`, `operator!=`.

---

## 5. `ctr::ListenerHandle`

Opaque handle returned by listener registration.

| Method | Contract |
|---|---|
| `remove()` | Unregisters the listener. Idempotent. Copies of a handle share the same registration; first `remove()` on any copy unregisters; subsequent calls are no-ops. ([§14.4](guide-07-listeners.md)) |

**Lifetime rule:** destroying the handle does **not** unregister the listener. Calling `remove()` after the owning context has been stopped is undefined behavior.

---

## 6. `ctr::BeanMetadata`

Const, non-owning, zero-allocation view of metadata for a resolved bean. Obtained via `bean.metadata()`.

| Method | Returns | Contract |
|---|---|---|
| `observedType()` | `const type_info&` | Exposed (observed) type. ([§15](guide-10-handles.md)) |
| `exactType()` | `const type_info&` | Concrete instantiated type. ([§15](guide-10-handles.md)) |
| `name()` | `string_view` | Named qualifier, or empty for unnamed beans. |
| `lifetime()` | `Lifetime` | Scope lifetime governing this bean. |
| `origin()` | `Origin` | How the bean was contributed: annotated type, factory product, or runtime binding. |
| `factoryMethod()` | `string_view` | Factory producer method name, or empty. |
| `methods()` | `MethodsRange` | Retained method list. Empty when `retainAllMetadata = false` or non-Ctorium type. ([§15](guide-10-handles.md)) |

**`MethodsRange`:** iterable; `annotatedWith<A>()` returns a lazy-filtered sub-range. Each `MethodView` exposes `name()`, `parameterCount()`, `annotation<A>()`, `parameters()`.

---

## 7. Options

### `ctr::DiscoverOptions`

| Field | Type | Default | Contract |
|---|---|---|---|
| `retainAllMetadata` | `bool` | `false` | When `true`, retains all visible reflection metadata for the provided roots. RAM cost assigned to the caller's choice. ([§6](guide-02-discovery.md)) |

### `ctr::BindOptions`

| Field | Type | Default | Contract |
|---|---|---|---|
| `name` | `const char*` | `nullptr` | Named key for the binding. `nullptr` or `""` = unnamed. |
| `priority` | `int` | `0` | Priority for candidate arbitration. Higher wins. |

### `ctr::ListenerOptions`

| Field | Type | Default | Contract |
|---|---|---|---|
| `priority` | `int` | `0` | Listener execution priority. Higher executes first. |

---

## 8. Lifetime markers

Declared as C++26 attributes on bean types or factory producer methods.

| Marker | Attribute | Fields | Contract |
|---|---|---|---|
| `ctr::prototype` | `[[=ctr::prototype{}]]` | `priority` (default `0`) | One instance per resolution. Destroyed when tracked references reach zero or owner stops. ([§8](guide-01-beans.md)) |
| `ctr::singleton` | `[[=ctr::singleton{}]]` | `priority` (default `0`), `lazy` (default `true`) | One instance per root and key. Lives until root shutdown. ([§8](guide-01-beans.md)) |
| `ctr::session` | `[[=ctr::session{}]]` | `priority` (default `0`) | One instance per scope and key. Lives for one scope cycle. ([§8](guide-08-sessions.md)) |
| `ctr::threadLocal` | `[[=ctr::threadLocal{}]]` | `priority` (default `0`) | One instance per owner, key, and thread. Destroyed on thread exit. ([§8](guide-01-beans.md)) |

---

## 9. Qualifiers and structural markers

| Marker | Attribute | Fields | Contract |
|---|---|---|---|
| `ctr::named` | `[[=ctr::named{.name = std::define_static_string("x")}]]` | `name` | Named qualifier. Empty → `ConfigurationError`. Runtime selector: `ctr::named("x")`. ([§4](guide-04-qualifiers.md)) |
| `ctr::factory` | `[[=ctr::factory{}]]` | — | Marks a factory. The factory itself is a singleton. ([§12](guide-09-factories.md)) |
| `ctr::postConstruct` | `[[=ctr::postConstruct{}]]` | — | Post-construction hook. ([§13](guide-06-hooks.md)) |
| `ctr::preDestroy` | `[[=ctr::preDestroy{}]]` | — | Pre-destruction hook. ([§13](guide-06-hooks.md)) |
| `ctr::scoped` | `[[=ctr::scoped{.name = std::define_static_string("x")}]]` | `name` | Scope injection qualifier. Valid at injection points only. Targets a `session` bean against scope `"x"`. Non-session target → `ConfigurationError`. ([§9](guide-08-sessions.md)) |

---

## 10. Listener phase tags

| Tag | Value | Fires |
|---|---|---|
| `ctr::onInitialized` | `ctr::onInitialized_t{}` | After construction, before `postConstruct`. |
| `ctr::onCreated` | `ctr::onCreated_t{}` | After `postConstruct` (or after `onInitialized` for bound objects). |
| `ctr::onPreDestroy` | `ctr::onPreDestroy_t{}` | Before `preDestroy`. |
| `ctr::onDestroyed` | `ctr::onDestroyed_t{}` | After `preDestroy`, before C++ destruction. |

All four phases apply to scanned types, bound objects, and factory-produced beans, including non-Ctorium objects.

---

## 11. Error types

All derive from `ctr::CtoriumError` (which derives from `std::runtime_error`).

| Error | Meaning | Common triggers |
|---|---|---|
| `ctr::CtoriumError` | Base for all Ctorium errors. | Catch-all. |
| `ctr::ResolutionError` | Resolution cannot be completed. | No candidate; ambiguity; cycle; no compatible constructor; incompatible cast. |
| `ctr::ContextStateError` | Operation invalid for current state. | `discover` after `start()`; `discover` on scope; `resolve` on stopped context; `resolve<session>` from root. |
| `ctr::ConfigurationError` | Configuration is contradictory. | Duplicate `(T, name, priority)` binding; marker conflict; ambiguous factory; `bindSingleton<BeanContext>`; invalid `named`; `scoped` on non-session target. |

Ctorium exceptions carry a message identifying the type, key, operation, or call path involved.

User exceptions from constructors, factories, hooks, and listeners propagate unchanged (pass-through policy). During `stop()`, user exceptions from hooks/listeners call `std::terminate` (`noexcept` shutdown).

---

## 12. Lifecycle order

**Scanned / factory-produced beans:**

Construction → `onInitialized` → `postConstruct` → `onCreated` → alive → `onPreDestroy` → `preDestroy` → `onDestroyed` → destruction.

**Bound objects (`bindSingleton` / `bindSession`):**

Construction (by user) → `onInitialized` → `onCreated` → alive → `onPreDestroy` → [`preDestroy`, Ctorium types only] → `onDestroyed` → destruction (by Ctorium).

---

Previous guide: [Guide 14 — Compilation](guide-14-compilation.md). Next guide: [Guide 16 — Build macros](guide-16-macros.md).
