# Guide 08 — Sessions

The three lifetimes from [Guide 01 — Beans](guide-01-beans.md) — singleton, prototype, thread-local — all live directly
under the root context. A **session** bean is the fourth: it lives inside a **scope**, a child runtime keyed under the
root that you start and stop on its own schedule. One scope, one instance; restart the scope, and the session beans
inside it are torn down and rebuilt. It is the lifetime for "things that exist for the duration of an open document, a
connected user, a running job" — anything with a lifespan shorter than the application but longer than a single
resolution.

This guide assumes everything up to [Guide 07 — Listeners](guide-07-listeners.md), and leans especially on
contexts ([Guide 03](guide-03-contexts.md)) and qualifiers ([Guide 04](guide-04-qualifiers.md)).

---

## 1. Declaring a session bean

A session bean carries the `[[=ctr::session{}]]` marker, exactly where the other lifetime markers go:

```cpp
struct [[=ctr::session{}]] DocumentState {
    std::string path;
    bool dirty = false;
};
```

What it does *not* do is live in the root: a session bean has no instance until a scope that can hold it is started, and
it has *one* instance per such scope. Think of it as a bean that refuses to exist until someone actually opens the room
it belongs to.

---

## 2. Scopes: `resolveScope`, start, stop

A **scope** is a `ctr::ScopedContext`, obtained from the root by key — resolve-or-create, just like `resolveContext`:

```cpp
ctr::BeanContext&   root  = ctr::BeanContext::resolveContext();
ctr::ScopedContext& scope = root.resolveScope("editor");
```

A scope shares the root's registry — the *descriptors* come from the root's `discover<...>()`, so session bean types are
discovered on the root like everything else. (A scope never discovers anything itself; `discover<...>()` on a
`ScopedContext` throws `ctr::ContextStateError`.) What the scope adds is its own instance store and its own start/stop
cycle:

```cpp
root.discover<^^app>();
root.start();                 // root must be started first

scope.start();                // sizes the scope's session store
{
    ctr::Bean<DocumentState> doc = scope.resolve<DocumentState>();
}
scope.stop();                 // destroys this scope's session instances
```

`start()` is idempotent and requires the root to be started already (otherwise `ctr::ContextStateError`); `stop()`
destroys the scope's session instances in reverse construction order and clears the store, while preserving the scope's
key, its `userData`, and any handles you still hold. `restart()` is just `stop()` then `start()`. A session bean can be
resolved **only from a started scope** — resolving one from the root, or from a stopped scope, raises
`ctr::ContextStateError`. Sessions do not improvise, and they do not work from home; they keep strict office hours and
lock the door at `stop()`.

---

## 3. The session handle is a live proxy

A handle to a session bean is not the stable pointer a singleton handle carries. It is a **proxy**: every `operator->`
resolves the scope's *current* instance, so the handle stays correct across a `restart()` — after which it points at the
new instance — and returns `nullptr`, rather than throwing, when the scope is stopped:

```cpp
ctr::Bean<DocumentState> doc = scope.resolve<DocumentState>();

doc->dirty = true;            // resolves the current "editor" instance

scope.restart();              // old instance destroyed, new one will be built
doc->path = "untitled";       // same handle, now the fresh instance

scope.stop();
if (doc.operator->() == nullptr) {
    // scope is stopped: the proxy resolves to nothing — check before use
}
```

So a long-lived holder can keep one session handle across scope cycles, but it must treat a `nullptr` dereference target
as "the scope isn't running right now," not as an error. The handle is less a leash on a specific object and more a
season pass: valid whenever the venue is open, politely useless when it's closed.

---

## 4. Wiring sessions to other beans

Within a started scope, session beans wire to each other and to root beans with ordinary `ctr::Bean<U>` injection — a
session bean may depend on another session bean (resolved in the same scope) or on a singleton (resolved from the root),
no annotation required — when `scoped` is absent on a session-to-session dependency, the dependency resolves eagerly
against the consumer's own scope at construction time:

```cpp
struct [[=ctr::session{}]] DocumentHistory {
    explicit DocumentHistory(ctr::Bean<DocumentState> doc,   // same scope
                             ctr::Bean<Logger> logger)        // root singleton
        : doc_(doc), logger_(logger) {}

    ctr::Bean<DocumentState> doc_;
    ctr::Bean<Logger> logger_;
};
```

The one direction that needs help is a **root bean reaching into a scope**. A singleton cannot plainly inject a session
bean — which scope's instance would it even mean? — so it must name the scope with the injection-point qualifier
`[[=ctr::scoped{.name = ...}]]`:

```cpp
struct [[=ctr::singleton{}]] EditorShell {
    explicit EditorShell(
        [[=ctr::scoped{.name = std::define_static_string("editor")}]]
        ctr::Bean<DocumentState> doc)
        : doc_(doc) {}

    ctr::Bean<DocumentState> doc_;     // proxy targeting the "editor" scope
};
```

The name must match the `resolveScope("editor")` key. The injected handle is the same live proxy as in §3: it resolves
the `"editor"` scope's current `DocumentState` on each access and yields `nullptr` while that scope is stopped.
`[[=ctr::scoped]]` may be combined with `[[=ctr::named]]` to pick a named candidate within the scope; the two qualifiers are resolved independently (`scoped` selects the scope, `named` selects the key within it).

**Ctorium** checks both halves of this at `start()` and rejects the contradictions with `ctr::ConfigurationError`:
putting `[[=ctr::scoped]]` on a parameter whose target is *not* a session bean, and injecting a session bean into a
*non-session* consumer *without* `[[=ctr::scoped]]`. The qualifier is the deliberate seam between the timeless root and
the comes-and-goes scope; the validation makes sure you meant to cross it.

---

## 5. Importing an instance: `bindSession`

The scope analog of `bindSingleton` ([Guide 05 — bindSingleton](guide-05-bindSingleton.md)) is `bindSession`, which
imports an object you built yourself as the scope's instance for a type:

```cpp
scope.bindSession<DocumentState>(std::make_unique<DocumentState>());
```

Bound before the scope's `start()`, the instance enters the lifecycle at start; bound while the scope is running, it is
stored immediately. As with `bindSingleton`, `BindOptions{.name, .priority}` apply, and the type must be known to the
root registry after the root has started.

---

## 6. Per-scope data and defaults

Two smaller scope features round things out. A scope can carry **user data** — an external object, attached by
reference, that scope-aware code can read back type-safely:

```cpp
scope.userData(currentUser);                              // attach by reference
if (auto u = scope.userData<User>()) { /* use u->get() */ }   // type-checked read
scope.userData(nullptr);                                 // detach
```

And `defaultNamed<T>(...)` called on a *scope* sets a **scope-local** default named selection, consulted ahead of the
root's default for resolutions made through that scope — the same mechanism
as [Guide 04 — Qualifiers](guide-04-qualifiers.md), narrowed to one scope.

---

## 7. Lifecycle, listeners, and shutdown

A session bean runs the full lifecycle of [Guide 06 — Hooks](guide-06-hooks.md)
and [Guide 07 — Listeners](guide-07-listeners.md): `postConstruct` after construction, listeners at each of the four
phases, `preDestroy` before destruction. The trigger points are the *scope's* transitions — instances are created on
demand after `scope.start()` and destroyed at `scope.stop()` (and so cycled by `restart()`), not at root start/stop.
Stopping the root stops its scopes too. Release session handles, or accept that they will resolve to `nullptr`, once
their scope is stopped.

Listeners observe session beans like any other bean, and scope-targeted listeners mean exactly what they say on the
tin. `scope.on<T>(phase, callback, options)` observes only beans produced or owned by that scope; the global scoped form
`scope.on(phase, callback, options)` does the same with `AnyBean`. A root listener, registered with `ctx.on(...)`,
keeps the wide-angle lens: root beans plus beans from every scope. Priority and registration order still decide dispatch
order for scoped listeners, so the usual listener rules apply; the only thing narrower is the audience.

---

## 8. Full example

An `"editor"` scope holding document session beans, plus a root singleton that reaches into it by name:

```cpp
#include <memory>
#include <string>

#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/ScopedContext.hpp>
#include <ctr/Registration.hpp>
#include <ctr/Markers.hpp>

namespace app {
    struct [[=ctr::singleton{}]] Logger {
        void log(std::string_view message) { /* ... */ }
    };

    struct [[=ctr::session{}]] DocumentState {
        std::string path = "untitled";
        bool dirty = false;
    };

    struct [[=ctr::session{}]] DocumentHistory {
        explicit DocumentHistory(ctr::Bean<DocumentState> doc, ctr::Bean<Logger> logger)
            : doc_(doc), logger_(logger) {}

        ctr::Bean<DocumentState> doc_;
        ctr::Bean<Logger> logger_;
    };

    struct [[=ctr::singleton{}]] EditorShell {
        explicit EditorShell(
            [[=ctr::scoped{.name = std::define_static_string("editor")}]]
            ctr::Bean<DocumentState> doc)
            : doc_(doc) {}

        ctr::Bean<DocumentState> doc_;
    };
}

int main() {
    ctr::BeanContext& root = ctr::BeanContext::resolveContext();
    root.discover<^^app>();
    root.start();

    {
        ctr::Bean<app::EditorShell> shell = root.resolve<app::EditorShell>();

        ctr::ScopedContext& editor = root.resolveScope("editor");
        editor.start();

        {
            ctr::Bean<app::DocumentHistory> history = editor.resolve<app::DocumentHistory>();
            history->doc_->dirty = true;
            // shell->doc_ now resolves this same "editor" DocumentState
        }

        editor.stop();          // DocumentState and DocumentHistory destroyed here
    }

    root.stop();
}
```

`EditorShell` is a root singleton, but its `doc_` is a proxy into the `"editor"` scope: it resolves to the live
`DocumentState` while that scope runs and to `nullptr` after `editor.stop()`.

---

## 9. Mental model

A scope is a child runtime keyed under the root that owns one set of session-bean instances and starts and stops on its
own. Read the moving parts as: the root discovers session bean *types* and owns the registry; `resolveScope("k")` gets a
scope; `scope.start()` makes its session beans resolvable; resolving a session bean returns a *proxy* handle that
re-resolves the scope's current instance on every access (and yields `nullptr` while stopped); session-to-session and
session-to-singleton wiring is plain injection, while a root bean reaching a session bean must name the scope with
`[[=ctr::scoped{.name=...}]]`; `scope.on(...)` observes only that scope's beans while `ctx.on(...)` observes root and
scope beans; and `scope.stop()` destroys that scope's instances, with `restart()` giving a clean set. The session is not
a singleton with a shorter temper — it is a bean whose lifetime is owned by a scope you control.

---

## 10. Checklist

Before moving on to factories, check that:

- session beans carry `[[=ctr::session{}]]` and are discovered on the **root**, not a scope (scopes cannot discover);
- the root is started before any scope is started, and session beans are resolved only from a **started** scope;
- session handles are treated as live proxies: a `nullptr` dereference target means the scope is stopped, not an error;
- session-to-session and session-to-singleton dependencies use plain `ctr::Bean<U>` injection;
- a root (non-session) bean depending on a session bean uses `[[=ctr::scoped{.name = ...}]]`, with the name matching the
  `resolveScope(...)` key;
- `[[=ctr::scoped]]` is used only at injection points and only against session targets (both contradictions are rejected
  at `start()`);
- `bindSession(...)`, `userData(...)`, and scope-local `defaultNamed(...)` are used per scope as needed;
- `scope.on<T>(...)` and `scope.on(...)` listeners are scope-local, while root `ctx.on(...)` listeners observe root and
  all scopes;
- session handles are released, or expected to resolve to `nullptr`, once their scope stops.

Next guide: [Guide 09 — Factories](guide-09-factories.md).
