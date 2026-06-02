# Guide 03 — Contexts

A **context** is one isolated dependency-injection runtime: the live object that owns everything needed to resolve
beans — descriptors, runtime bindings, listeners, named defaults, managed instances, and lifecycle state. In user code
its type is `ctr::BeanContext`, and you normally hold it as a plain reference obtained from the static accessor. It is
not a magic global soup bowl; it is a real managed object with a lifecycle. Soup remains outside the roadmap.

The `ctr::Bean<T>` handle model is introduced in [Guide 01 — Beans](guide-01-beans.md), and discovery — the setup step
most of this guide assumes you will call — in [Guide 02 — Discovery](guide-02-discovery.md).

---

## 1. The default context

Most applications have one dependency graph, one set of beans, one runtime container — and so need exactly one context.
You get it from the static accessor, with no key:

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();
```

Read that as *"give me the default Ctorium context, creating it if it does not exist yet."* It hands back a **reference
**, not a tracked handle: the context is owned by **Ctorium** itself and lives in a static table for as long as it is
alive, so you use it directly through `.`. (The beans it produces come back as `ctr::Bean<T>` handles you use through
`->` — the one place where container and contents deliberately differ.) Calling `resolveContext()` again while that
context is alive returns a reference to the very same instance, which is exactly what you want when several parts of an
application need the same runtime:

```cpp
ctr::BeanContext& first  = ctr::BeanContext::resolveContext();
ctr::BeanContext& second = ctr::BeanContext::resolveContext();  // same context as `first`
```

The normal path through that one context is short, direct, and unlikely to scare the intern:

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();

context.discover<^^app>();
context.start();

{
    ctr::Bean<app::Game> game = context.resolve<app::Game>();
}

context.stop();
```

---

## 2. Multiple contexts via keys

When one process genuinely needs several independent containers, pass a **context key** — a stable, copyable, hashable,
comparable value that names the context:

```cpp
ctr::BeanContext& gameContext   = ctr::BeanContext::resolveContext("game");
ctr::BeanContext& editorContext = ctr::BeanContext::resolveContext("editor");
```

These are parallel peers, not a parent/child pair: each has its own registry, descriptors, listeners, runtime bindings,
defaults, and managed instances, and nothing leaks between them. Multiple contexts are an advanced feature for cases
that need real isolation. For the rest of this guide, `context` means the single default context.

---

## 3. The lifecycle

A context moves through a small, deliberately explicit lifecycle:

```text
created → started → shuttingDown → stopped
```

It begins **created** and accepts setup operations. `start()` moves it to **started**, where it resolves beans. `stop()`
moves it through **shuttingDown** to **stopped**, releasing everything it owns. The transitions are visible on purpose —
a container that started and stopped invisibly would not be convenient, it would be a haunted basement.

The two halves that follow — configuring a *created* context, then *starting* it — are simply the first two transitions
seen up close.

---

## 4. Configuring a created context

A **created** context is one that exists but has not started. This is the setup phase, where you declare what the
context will manage: call `discover<...>()`, register listeners, and bind runtime instances.

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();

context.discover<^^app>();
context.on<Logger>(ctr::onCreated,
    [](const ctr::Bean<Logger>& logger) { logger->log("logger created"); });

context.start();
```

Each of those operations has its own guide — discovery in [Guide 02 — Discovery](guide-02-discovery.md), runtime
instance binding in [Guide 05 — bindSingleton](guide-05-bindSingleton.md), and lifecycle listeners
in [Guide 07 — Listeners](guide-07-listeners.md). Default named selections are *not* in this list: they require an
assigned `TypeId`, which only exists after `start()`, so `defaultNamed<T>(...)` is a runtime contribution made after the
context starts (§8, and [Guide 04 — Qualifiers](guide-04-qualifiers.md)). The only context-level rule that matters here
is the one [Guide 02](guide-02-discovery.md) already established: **discover before `start()`**.

---

## 5. Starting a context

`start()` activates the context. It creates the runtime registry if needed, registers the context itself as an internal
singleton (which is what makes context injection in §7 possible), turns the discovered bean and factory descriptors into
resolvable entries, integrates any runtime bindings declared before startup, and creates the eager singletons — the ones
marked to be built at start rather than on first use.

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();

context.discover<^^app>();
context.start();
```

Discovery is optional here: a context with no discovery is valid and can run on runtime bindings alone (
see [Guide 05 — bindSingleton](guide-05-bindSingleton.md)). What is *not* optional is the order — once a context has
started, discovery is closed, and a later `discover<...>()` is rejected with `ctr::ContextStateError` (§9). A second
`start()`, by contrast, is harmless: it is a no-op, not an error.

---

## 6. Resolving from a started context

A **resolution** is a request for a bean, and the bean it returns belongs to the context that resolved it:

```cpp
ctr::Bean<app::GameService> service = context.resolve<app::GameService>();
```

Resolution draws on the context's runtime state, so the rule is simply: resolve from the context that owns the graph you
want. The finer points — named resolution and priority — are covered in [Guide 04 — Qualifiers](guide-04-qualifiers.md).

---

## 7. The context and its beans

Because `start()` registers the context as an internal singleton, a bean can receive its own context by ordinary
constructor injection, as a `ctr::Bean<ctr::BeanContext>`:

```cpp
struct [[=ctr::singleton{}]] Service {
    explicit Service(ctr::Bean<ctr::BeanContext> context)
        : context_(context) {}

    ctr::Bean<ctr::BeanContext> context_;
};
```

This is useful when a bean needs context-level operations — registering listeners, or resolving from the same context.
Use it deliberately, though: a bean that stores the context can reach the whole container, which is far broader access
than a bean that receives only the precise dependencies it needs. Power is useful, and it also makes architecture
reviews more interesting.

Often you do not need to inject it at all, because any handle can produce the context that emitted it:

```cpp
context.on<Service>(ctr::onCreated,
    [](const ctr::Bean<Service>& service) {
        ctr::BeanContext& owner = service.context();
    });
```

`ctr::Bean<T>::context()` returns a `BeanContext&`. The handle stores no extra context pointer; it recovers the context
through its registry, which keeps `ctr::Bean<T>` lightweight while preserving access. `AnyBean`, used by global listener
callbacks, offers the same access — see [Guide 07 — Listeners](guide-07-listeners.md).

---

## 8. Runtime contributions after start

Some configuration can still be added once the context is running — listeners, runtime default named selections, and
runtime instance bindings — applied directly to the live context:

```cpp
context.on<Worker>(ctr::onCreated,
    [](const ctr::Bean<Worker>& worker) { worker->ready = true; });

context.defaultNamed<Logger>("console");
```

These take effect going forward: a listener registered after `start()` observes only future events, a changed default
affects only future resolutions, and a runtime binding materializes its bean at bind time. Beans already materialized
are never rewired retroactively — surprise rewiring belongs in horror films and certain legacy frameworks, not here.

---

## 9. State errors

Operations performed in the wrong lifecycle state raise `ctr::ContextStateError`. The two you are most likely to meet:
calling `discover<...>()` after `start()`,

```cpp
context.start();
context.discover<^^app>();   // ctr::ContextStateError — discovery is closed after start
```

and resolving from a context that has been stopped,

```cpp
context.stop();
auto service = context.resolve<GameService>();   // outside the contract — context is gone
```

These errors keep the lifecycle honest: a context is either being configured, active, shutting down, or stopped —
never "a little bit started if you squint."

A related concurrency rule belongs in the same place. `start()` and `stop()` change the context's state, and `stop()` is
exclusive: while a context is shutting down it is busy closing managed beans, so do not design code that keeps resolving
from it concurrently. Start before use, stop after use, and do not use the context as it is stopping.

---

## 10. Stopping, and handles at shutdown

`stop()` shuts the context down: it runs destruction (destruction listeners fire and `preDestroy` hooks run —
see [Guide 06 — Hooks](guide-06-hooks.md) and [Guide 07 — Listeners](guide-07-listeners.md)), cleans up the managed
beans, and removes the context from the live table so its reference becomes dangling.

A stopped context stays stopped — it is never restarted. If a new application phase needs a context again, ask for one
again; what you get back is a fresh lifecycle, not a revived corpse:

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();
context.start();
context.stop();

ctr::BeanContext& next = ctr::BeanContext::resolveContext();   // new lifecycle, not a restart
```

This matters for your own handles too. The context owns its managed instances; user code only holds tracked
`ctr::Bean<T>` handles, and using one after its emitting context has shut down is outside the contract. So release
handles before shutdown — the tidy shape is to let them leave scope first:

```cpp
{
    ctr::Bean<GameService> service = context.resolve<GameService>();
    service->run();
}

context.stop();
```

Boring and correct, which wins a shocking number of engineering arguments.

---

## 11. Full example

The default context, end to end — discover, start, resolve inside a scope so the handle is released before shutdown,
then stop:

```cpp
#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/Registration.hpp>
#include <ctr/Markers.hpp>

namespace app {
    struct [[=ctr::singleton{}]] GameConfig {
        bool debug = true;
    };

    struct [[=ctr::singleton{}]] GameService {
        explicit GameService(ctr::Bean<GameConfig> config)
            : config_(config) {}

        void start() {
            if (config_->debug) {
                // Start in debug mode.
            }
        }

        ctr::Bean<GameConfig> config_;
    };
}

int main() {
    ctr::BeanContext& context = ctr::BeanContext::resolveContext();

    context.discover<^^app>();
    context.start();

    {
        ctr::Bean<app::GameService> game = context.resolve<app::GameService>();
        game->start();
    }

    context.stop();
}
```

Read the flow as five steps: `resolveContext()` retrieves or creates the default context; `discover<^^app>()` prepares
its descriptors; `start()` activates it and builds the registry; `resolve<...>()` asks it for a bean; `stop()` shuts it
down and removes it from the live table. One context is one dependency-injection runtime, and the context boundary is
the dependency-injection boundary.

---

## 12. Checklist

Before moving on to qualifiers, check that:

- the default context (`resolveContext()`) is enough for your application, or that you genuinely need keyed contexts;
- `discover<...>()` is called before `start()` whenever annotated beans are used;
- runtime-only contexts rely on runtime bindings directly;
- `start()` runs before resolution, and `stop()` runs when the context should shut down;
- beans are resolved from the context that owns their dependency graph;
- `ctr::Bean<ctr::BeanContext>` is injected only when a bean truly needs context-level access;
- user-held `ctr::Bean<T>` handles are released before context shutdown;
- stopped contexts are not reused.

Next guide: [Guide 04 — Qualifiers](guide-04-qualifiers.md).
