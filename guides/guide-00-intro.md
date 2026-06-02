# Guide 00 — Read this before wiring the spaceship

The README gives the first quick taste of **Ctorium**. This guide does not repeat that tour; it slows down and explains
the model you need before writing real **Ctorium** code.

**Ctorium** is not a spellbook. It is closer to a very strict stage manager: you declare the actors, their roles, and
the stage entrance, and **Ctorium** makes sure the right actor arrives at the right time, ideally without tripping over
a cable.

## The goal of this guide

By the end of this page you should understand what a bean is, what a context does, why discovery is explicit, how
resolution creates or retrieves objects, and why `ctr::Bean<T>` is more than a pointer wearing a hat.

With that model in place, the later guides can focus on code patterns instead of stopping every three lines to define a
word. Nobody wants a tutorial that says "simply resolve the descriptor metadata of a scoped provider" before breakfast.

## Vocabulary before code

Almost everything in **Ctorium** comes down to one question asked in several ways: *who creates this object, how long
does it live, and who is allowed to ask for it?* The terms below are just precise names for the parts of that question.

A **bean** is an object **Ctorium** manages: it creates the object, reuses it according to its lifetime, exposes it
through a handle, notifies lifecycle listeners, and destroys it when its context shuts down. The objects a bean needs in
order to work are its **dependencies**, and the full set of beans pulled in to build one requested object —
`Hud → FrameClock → TimerBackend` — is its **dependency graph**.

You never touch a managed object directly. Instead you hold a **handle**, the typed `ctr::Bean<T>`. It is a **tracked**
handle: **Ctorium** records every copy, move, and destruction, which is how it knows whether anyone still needs a given
object. So `ctr::Bean<T>` is not a raw pointer — it takes part in the object's lifetime.

A **context** is one isolated runtime world. `ctr::BeanContext` owns the **registry** — the storage holding the managed
instances — together with the descriptors, listeners, runtime bindings, named defaults, and lifecycle state that belong
to that world. Most programs need exactly one.

A **lifetime** is the rule for when an object is created, reused, and destroyed; you declare it with a **lifetime marker
**, a C++ attribute on the type. Three markers cover the common cases, and a fourth covers scope-bound state:

```cpp
struct [[=ctr::prototype{}]]   RequestId {};        // one new instance per resolve
struct [[=ctr::singleton{}]]   Configuration {};    // one instance per context and key
struct [[=ctr::threadLocal{}]] ThreadScratchpad {}; // one instance per context, key, and thread
struct [[=ctr::session{}]]     DocumentState {};    // one instance per started scope
```

The session is a bean whose lifetime is owned by a **scope** — a child runtime under the root that starts and stops on its own; it has its own chapter, [Guide 08 — Sessions](guide-08-sessions.md).

When several beans could satisfy the same request, a **key** selects between them. A bean with no name uses the unnamed
key; a **named key** is declared with `[[=ctr::named{...}]]` or chosen at runtime with `ctr::named("...")`.

Before a context can manage anything, it has to learn what exists. **Discovery** is the step where the context reads one
or more **reflection roots** — visible C++ scopes, for example a namespace, passed with the C++26 syntax `^^scope` — and
builds a **descriptor** for each bean and factory it finds. A descriptor is **Ctorium**'s compact description of one
resolvable bean: enough information for resolution, lifecycle, priority, naming, metadata, and shutdown, and nothing
more.

Once a context is running, **resolution** is the act of asking it for a bean, through `context.resolve<T>()`. A *
*lifecycle** is the ordered sequence of creation, initialization, use, pre-destruction, and destruction events a managed
bean goes through.

That is the whole vocabulary. It looks like a lot, but it is mostly the same idea told politely several times.

## Step 1 — Declare a contract

A **contract** is the type your callers ask for. It is often an interface-like base class, though a concrete type works
too. Here is a small contract for something that can report the current frame number:

```cpp
namespace app {

struct FrameClock {
    virtual ~FrameClock() = default;
    virtual int frame() const = 0;
};

} // namespace app
```

At this point **Ctorium** knows nothing about `FrameClock`. It is plain C++.

## Step 2 — Declare a bean implementation

To let **Ctorium** manage a concrete implementation, give it exactly one lifetime marker:

```cpp
namespace app {

struct [[=ctr::singleton{}]] GameFrameClock : FrameClock {
    int frame() const override { return currentFrame_; }

    int currentFrame_ = 0;
};

} // namespace app
```

`GameFrameClock` is now a singleton bean *candidate*. The word matters: it becomes available to a context only after
discovery sees it.

## Step 3 — Declare a dependent bean

**Ctorium** treats any constructor parameter of type `ctr::Bean<U>` as a dependency to inject:

```cpp
namespace app {

struct [[=ctr::singleton{}]] Hud {
    explicit Hud(ctr::Bean<FrameClock> clock)
        : clock_(clock) {}

    void draw() {
        int currentFrame = clock_->frame();
        // The pixels are outside this guide. They escaped.
    }

    ctr::Bean<FrameClock> clock_;
};

} // namespace app
```

`Hud` never constructs `GameFrameClock`; it only states that it needs something compatible with `FrameClock`. **Ctorium
** does the wiring when `Hud` is resolved.

## Step 4 — Get the default context

The **default context** is the one you get from `ctr::BeanContext::resolveContext()` when you pass no key. For most
applications it is the only context the program ever needs — one container, one runtime, one fewer chance to
accidentally invent a multiverse before lunch.

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();
```

`resolveContext()` hands back a reference, not a tracked handle. The context is owned by **Ctorium** itself and lives in
a static context table for as long as it is alive, so you use it directly through `.`, while the beans it produces come
back as `ctr::Bean<T>` handles you use through `->`. This is the one place where the container and its contents
deliberately do *not* share the same access model.

If you ever need more than one container, pass a key: `resolveContext("editor")` returns an independent context with its
own registry, descriptors, listeners, bindings, defaults, and lifecycle. Parallel contexts are peers — none is the
parent of another, and nothing leaks between them. Context keys get their own treatment
in [Guide 03 — Contexts](guide-03-contexts.md).

## Step 5 — Discover visible declarations

Before calling `discover<...>()`, include the headers or modules that declare your beans, then pass one or more
reflection roots:

```cpp
#include <ctr/Registration.hpp>   // enables discover<...>()
#include "app/Hud.hpp"            // makes your bean declarations visible

context.discover<^^app>();
```

Discovery only builds descriptors — no `Hud` is born yet; the nursery opens later. It is deliberately explicit: *
*Ctorium** never scans your whole program and never needs an external generator step. Your C++ declarations are the
single source of truth, and the roots you pass decide exactly what the context can see.

## Step 6 — Start the context

```cpp
context.start();
```

`start()` activates the context: it prepares runtime resolution and creates any eager singletons — singletons configured
to be built at start rather than on first use. A context starts once, and once it has started, discovery is closed for
it.

## Step 7 — Resolve a bean

Resolving asks the context for a managed object compatible with a type:

```cpp
ctr::Bean<app::Hud> hud = context.resolve<app::Hud>();
hud->draw();
```

To produce that one handle, **Ctorium** walks the dependency graph: `Hud` is requested, `Hud` needs a `FrameClock`,
`GameFrameClock` is compatible, so `GameFrameClock` is created or reused per its lifetime, then `Hud` is created or
reused per its lifetime, and finally a tracked `ctr::Bean<app::Hud>` comes back. You receive a handle, never ownership
of the raw object — the context and its registry stay responsible for the managed lifetime.

## Step 8 — Stop the context

```cpp
context.stop();
```

Stopping shuts down the managed runtime world: destruction listeners fire, `preDestroy` hooks run where applicable,
managed beans are cleaned up, and the context is removed from the static context table. Release your `ctr::Bean<T>`
handles before that shutdown — using a bean after its context is gone is outside the public contract.

## A complete first example

Everything here is intentionally small; the point is the flow, not the frame counter. A heroic frame counter still
counts as a frame counter.

```cpp
#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/Markers.hpp>
#include <ctr/Registration.hpp>

namespace app {

struct FrameClock {
    virtual ~FrameClock() = default;
    virtual int frame() const = 0;
};

struct [[=ctr::singleton{}]] GameFrameClock : FrameClock {
    int frame() const override {
        return currentFrame_;
    }

    int currentFrame_ = 0;
};

struct [[=ctr::singleton{}]] Hud {
    explicit Hud(ctr::Bean<FrameClock> clock)
        : clock_(clock) {}

    void draw() {
        int currentFrame = clock_->frame();
        (void)currentFrame;
    }

    ctr::Bean<FrameClock> clock_;
};

} // namespace app

int main() {
    ctr::BeanContext& context =
        ctr::BeanContext::resolveContext();

    context.discover<^^app>();
    context.start();

    {
        ctr::Bean<app::Hud> hud = context.resolve<app::Hud>();
        hud->draw();
    }

    context.stop();
}
```

If the flow still feels mysterious, read it from bottom to top: `resolve<app::Hud>()` asks for `Hud`, which asks for
`FrameClock`, which `GameFrameClock` can satisfy; `discover<^^app>()` made both declarations visible to the context;
`start()` activated it before any resolution; and `stop()` closed the runtime world afterward.

## What **Ctorium** does not do for you

It does not scan your whole program, and it does not require a code generator that runs before your build. It also does
not paper over mistakes with **silent fallbacks**: a missing or ambiguous dependency is reported through a **Ctorium**
exception rather than quietly substituted. Exceptions thrown by *your own* constructors, factory methods, hooks,
listeners, or destructors pass through unchanged.

The rule is simple: **Ctorium** manages what you declare, in the context where you declare it, and keeps errors visible
instead of sweeping your dependency graph under the rug.

## Building Ctorium into your project

`ctorium` is an INTERFACE (header-only) CMake target. Pull it in with `add_subdirectory(...)` or `FetchContent`, then
`target_link_libraries(your_target PRIVATE ctorium)`. Consumed this way it adds only that one target: its own tests and
benchmarks stay out of your build, and it fetches neither GoogleTest nor Google Benchmark.

Two options govern that. Both default to `ON` only when Ctorium is the top-level project — that is, when you build
Ctorium itself — and to `OFF` otherwise, so a consuming build pays for neither by default:

- `CTORIUM_BUILD_TESTS` — build the test suite (fetches GoogleTest when enabled);
- `CTORIUM_BUILD_BENCHMARKS` — build the benchmarks (fetches Google Benchmark when enabled).

Enable either explicitly with `-DCTORIUM_BUILD_TESTS=ON` or `-DCTORIUM_BUILD_BENCHMARKS=ON` to run them from a consuming
build. Build flags and toolchain setup get their own treatment in [Guide 14 — Compilation](guide-14-compilation.md).

## Guide map

A **qualifier** is information used during resolution to choose between compatible beans; named keys and priority are
the first ones the documentation covers. The guides build the model in order, and they are meant to be read that way
rather than all at once:

- [Guide 01 — Beans](guide-01-beans.md): bean types, lifecycles, lifetime markers, and constructor injection.
- [Guide 02 — Discovery](guide-02-discovery.md): `discover<...>()`, required headers or modules, multiple roots, and
  visible declarations.
- [Guide 03 — Contexts](guide-03-contexts.md): the default context, explicit context keys, lifecycle, shutdown, and
  isolation between parallel containers.
- [Guide 04 — Qualifiers](guide-04-qualifiers.md): named keys and priority.
- [Guide 05 — bindSingleton](guide-05-bindSingleton.md): importing existing objects into a context with
  `bindSingleton(...)`.
- [Guide 06 — Hooks](guide-06-hooks.md): `postConstruct`, `preDestroy`, and resource handoff points.
- [Guide 07 — Listeners](guide-07-listeners.md): lifecycle listeners, listener handles, and dispatch order.
- [Guide 08 — Sessions](guide-08-sessions.md): the session lifetime, scopes, `ScopedContext`, and the `[[=ctr::scoped]]`
  injection qualifier.
- [Guide 09 — Factories](guide-09-factories.md): factory beans, producer methods, and produced beans.
- [Guide 10 — Handles](guide-10-handles.md): `ctr::Bean<T>` and `ctr::AnyBean` — handle forms, tracking, and casts.
- [Guide 11 — Headers](guide-11-headers.md): the `ctr/` header map and the `Registration.hpp` rule.
- [Guide 12 — Advanced](guide-12-advanced.md): the error model and multiple isolated contexts.
- [Guide 13 — Performance](guide-13-performance.md): measured costs by lifetime and handle form.
- [Guide 14 — Compilation](guide-14-compilation.md): C++26 reflection support, toolchain, and build flags.

Trying to learn all of it at once leads to "I built a container to understand the container," and nobody comes back from
that unchanged.

## Quick checklist

Before you run your first **Ctorium** code, confirm that:

- every discovered bean type has exactly one lifetime marker;
- the headers or modules declaring beans are visible before `discover<...>()`;
- `discover<...>()` runs before `start()`;
- `start()` runs before normal resolution;
- constructor-injected dependencies use `ctr::Bean<U>`;
- `stop()` runs when the context is no longer needed;
- no handle is used after its context has shut down.

You now have the mental model needed for the rest of the guide.
