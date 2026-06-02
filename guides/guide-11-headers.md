# Guide 11 — Headers

**Ctorium** is a header-only library exposed through the `ctr/` include directory. This guide explains which header gives you which capability, the one include rule that actually matters (`Registration.hpp` for reflective operations), the umbrella header, and how to keep includes lean. Knowing the header map turns "why doesn't `discover` compile?" into a one-line fix.

This guide assumes you have met the operations in the earlier guides; here it is only about *where each one lives*.

---

## 1. The public headers

Every public header lives under `ctr/`. Each one carries a focused slice of the API:

- `<ctr/Markers.hpp>` — the annotation markers: `singleton`, `prototype`, `threadLocal`, `session`, `factory`, `named`, `scoped`, `postConstruct`, `preDestroy`. Needed wherever you *declare* a bean.
- `<ctr/Bean.hpp>` — the typed handle `ctr::Bean<T>`. Needed wherever you hold or inject a bean.
- `<ctr/AnyBean.hpp>` — the type-erased handle `ctr::AnyBean`, used by global listeners.
- `<ctr/BeanContext.hpp>` — `ctr::BeanContext`: `resolveContext`, `start`/`stop`, `resolve`, `defaultNamed`, `bindSingleton`, `resolveScope`, `on`/`remove`.
- `<ctr/ScopedContext.hpp>` — `ctr::ScopedContext` for sessions ([Guide 08 — Sessions](guide-08-sessions.md)).
- `<ctr/Options.hpp>` — `DiscoverOptions`, `BindOptions`, `ListenerOptions`.
- `<ctr/Errors.hpp>` — the exception hierarchy ([Guide 12 — Advanced](guide-12-advanced.md) covers the model).
- `<ctr/ListenerHandle.hpp>` — `ctr::ListenerHandle` and the listener phase tags.
- `<ctr/BeanMetadata.hpp>` — the metadata view (API still settling; see [Guide 02 — Discovery](guide-02-discovery.md)).
- `<ctr/Registration.hpp>` — the reflective-operation enabler (§3).
- `<ctr/Ctorium.hpp>` — the umbrella header (§4).

---

## 2. Declaration vs. use

Most translation units split cleanly into two roles, and the includes follow that split.

A header that *declares* beans needs the markers and the handle type — `<ctr/Markers.hpp>` for the attributes, `<ctr/Bean.hpp>` for the `ctr::Bean<U>` constructor parameters:

```cpp
#include <ctr/Bean.hpp>
#include <ctr/Markers.hpp>

struct [[=ctr::singleton{}]] Renderer {
    explicit Renderer(ctr::Bean<GameConfig> config) : config_(config) {}
    ctr::Bean<GameConfig> config_;
};
```

A translation unit that *drives* the container needs `<ctr/BeanContext.hpp>` and, crucially, `<ctr/Registration.hpp>` (§3) — plus the headers of the beans it intends to discover.

---

## 3. The one rule: `Registration.hpp` for reflective operations

The reflective operations — `discover<...>()` and the reflective form of `bindSingleton<T>(...)` — are not defined in `<ctr/BeanContext.hpp>` alone. They require the machinery that turns reflection roots into descriptors, and that machinery is pulled in by `<ctr/Registration.hpp>`:

```cpp
#include <ctr/BeanContext.hpp>
#include <ctr/Registration.hpp>   // enables discover<...>() and reflective bindSingleton

context.discover<^^app>();
context.start();
```

`Registration.hpp` itself includes the umbrella `Ctorium.hpp` plus the internal descriptor generators, so including it gives you the full public API as well. The rule is simple: **the translation unit that calls `discover<...>()` (or reflective `bindSingleton`) includes `<ctr/Registration.hpp>`.** A unit that only resolves and uses beans does not need it. Forgetting it is the usual cause of "`discover` is not a member" style errors — the method is there in spirit, just not in that translation unit.

---

## 4. The umbrella header

`<ctr/Ctorium.hpp>` includes the whole public surface at once — handles, context, scoped context, markers, options, errors, listener handle, metadata — and wires in the inline implementation headers. Reach for it when you want everything without curating a list:

```cpp
#include <ctr/Ctorium.hpp>
```

Note that the umbrella does *not* by itself enable the reflective operations: `<ctr/Registration.hpp>` is still the header that pulls in the discovery and reflective-bind generators (and it, in turn, includes the umbrella). So in the unit that discovers, including `Registration.hpp` is sufficient and includes everything; elsewhere, prefer the focused headers from §1.

---

## 5. Keeping includes lean

Include order follows one rule from [Guide 02 — Discovery](guide-02-discovery.md): a bean's header must be visible *before* the `discover<...>()` call that should find it. Beyond that, prefer focused headers in bean-declaring code (markers + `Bean.hpp`) and reserve the umbrella or `Registration.hpp` for the driving unit. This keeps the compile-time surface of leaf headers small — relevant given C++26 reflection's compile cost ([Guide 14 — Compilation](guide-14-compilation.md)) — without changing any behaviour.

---

## 6. Mental model

`ctr/` is split by role: markers and `Bean.hpp` for *declaring* beans; `BeanContext.hpp` + `Registration.hpp` for *driving* the container; `Options.hpp`, `Errors.hpp`, `ListenerHandle.hpp`, `AnyBean.hpp`, `ScopedContext.hpp` for the corresponding features; `Ctorium.hpp` to take it all at once. The single load-bearing rule is that reflective operations (`discover`, reflective `bindSingleton`) require `Registration.hpp` in that translation unit; everything else is convenience.

---

## 7. Checklist

When organizing includes, check that:

- bean-declaring headers include `<ctr/Markers.hpp>` and `<ctr/Bean.hpp>`;
- the translation unit calling `discover<...>()` (or reflective `bindSingleton`) includes `<ctr/Registration.hpp>`;
- bean headers are included before the `discover<...>()` call that must find them;
- `<ctr/Ctorium.hpp>` is used when a unit wants the full surface, focused headers otherwise;
- feature headers (`Options`, `Errors`, `ListenerHandle`, `AnyBean`, `ScopedContext`) are included where those features are used.

Next guide: [Guide 12 — Advanced](guide-12-advanced.md).
