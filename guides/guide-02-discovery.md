# Guide 02 — Discovery

**Discovery** is the step where **Ctorium** inspects the C++ declarations you make visible to it and turns the beans and
factories it finds into the descriptors a context will later use. It happens at compile time, driven entirely by your
declarations — **Ctorium** is clever, not psychic, so it needs declarations, not riddles.

One idea sits underneath everything in this guide: a bean can be discovered only if the compiler can *see* it at the
point where `discover<...>()` is instantiated. The rest is detail.

---

## 1. What a discovery call does

A **reflection root** is a C++26 reflection value naming a scope to look in; `^^app` is "the reflection value for the
namespace `app`." You hand one or more roots to `discover<...>()`:

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();

context.discover<^^app>();
context.start();
```

Read `discover<^^app>()` as: *for this context, inspect the declarations visible under `app`, and record the bean
descriptors and annotation metadata.* That work runs **during compilation** — the compiler walks the declarations
reachable from your roots and builds an optimized **catalog** of descriptors (one compact description per resolvable
bean) and metadata. "Discovery surface" is just a name for that set of visible declarations.

This is not a filesystem scan and not a code-generation step. It is reflection over the scopes you named. And it only
builds the *map*: no bean is instantiated here. Instances arrive later, when the context starts or resolves something —
showing up during the catalog build would be rude.

---

## 2. Visibility: include first, discover after

A bean or factory must be visible *before* the discovery call that should find it. In practice that means including the
headers (or importing the modules) that declare your beans ahead of `discover<...>()`:

```cpp
#include "game/GameConfig.hpp"
#include "game/Renderer.hpp"
#include "game/Hud.hpp"

#include <ctr/BeanContext.hpp>
#include <ctr/Registration.hpp>

int main() {
    ctr::BeanContext& context = ctr::BeanContext::resolveContext();

    context.discover<^^game>();   // GameConfig, Renderer, Hud are all visible
    context.start();
}
```

Move those bean headers *below* the discovery call and the declarations become visible too late — `discover<^^game>()`
then sees an empty `game`, and nothing is discovered. The rule is simple, boring, and extremely useful: **include first,
discover after.** Boring rules are the tiny seatbelts of dependency injection.

---

## 3. Listing roots and discovering several namespaces

A context can discover any number of roots. The roots all share one context, so a bean from one root can depend on a
bean from another — a `plugins` bean depending on a `game` bean is fine:

```cpp
namespace game {
    struct [[=ctr::singleton{}]] GameConfig {
        bool debug = true;
    };
}

namespace plugins {
    struct [[=ctr::singleton{}]] DebugOverlay {
        explicit DebugOverlay(ctr::Bean<game::GameConfig> config)
            : config_(config) {}

        ctr::Bean<game::GameConfig> config_;
    };
}

context.discover<^^game, ^^plugins>();
context.start();
```

You *can* split discovery across several pre-start calls — they accumulate, and **Ctorium** merges and deduplicates
everything when the context starts:

```cpp
context.discover<^^game>();
context.discover<^^plugins>();   // valid: contributions accumulate until start()
context.start();
```

But prefer one call listing every root:

```cpp
context.discover<^^app, ^^game, ^^ui, ^^plugins>();
context.start();
```

It reads as a single statement of what the context contains, keeps the surface in one place, and spares the reader from
hunting for stray `discover<...>()` calls. One context, one obvious discovery call, many roots — peace restored.

---

## 4. Discovery happens before start

Discovery is part of setup, so it must run before `start()` activates the context:

```cpp
context.discover<^^game>();   // correct: surface first
context.start();
```

Once a context has started, it has prepared its runtime registry, and accepting a new discovery surface would make that
registry inconsistent. So a `discover<...>()` call *after* `start()` is rejected with a `ctr::ContextStateError`. (This
is the real constraint; the number of pre-start calls is not.)

---

## 5. Discovery is the map, not the journey

Discovery answers one question — *"what can this context know how to create?"* — and nothing more. Materialization (
creating the actual object) is a separate question, *"what should this context create now?"*, answered later by
`start()` building eager singletons, by `resolve<T>()` on first use, or by factory-backed resolution.

```cpp
context.discover<^^game>();              // prepares descriptors for Renderer, Hud, BossFightManager…
context.start();                         // may create eager singletons
auto renderer = context.resolve<Renderer>();   // creates or reuses on demand
```

Keeping those two phases distinct is what makes the model predictable.

---

## 6. Discovery vs. runtime contributions

Discovery is a compile-time reflection step for *declarations*. Everything you add to a live context instead —
registering a listener, choosing a default named bean, importing an existing instance — is a **runtime contribution**,
applied directly to the context without rebuilding the discovery surface:

```cpp
context.on<System>(ctr::onCreated,
    [](const ctr::Bean<System>& system) { system->enable(); });

context.defaultNamed<Logger>("console");

context.bindSingleton<AppConfig>(std::make_unique<AppConfig>());
```

Use discovery for declared beans; use runtime contributions for runtime configuration. A context can even skip discovery
entirely and rely on runtime bindings alone — useful when it only needs imported instances:

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();

context.bindSingleton<AppConfig>(std::make_unique<AppConfig>());
context.start();

ctr::Bean<AppConfig> config = context.resolve<AppConfig>();
```

Most applications with annotated beans will call discovery; this just shows it is not mandatory. Each of these
operations gets its own guide — listeners in [Guide 07 — Listeners](guide-07-listeners.md), named defaults
in [Guide 04 — Qualifiers](guide-04-qualifiers.md), and `bindSingleton(...)`
in [Guide 05 — bindSingleton](guide-05-bindSingleton.md).

---

## 7. What actually gets discovered

A declaration becomes a descriptor when it is one of three things, visible under a root:

1. an **annotated bean type** — a type with one lifetime marker;
2. an **annotated factory** — a type with `[[=ctr::factory{}]]`;
3. a **factory producer method** on such a factory.

```cpp
struct [[=ctr::singleton{}]] GameConfig {
};

struct [[=ctr::factory{}]] AssetFactory {
    [[=ctr::prototype{}]]
    TextureRequest request(std::string path);
};
```

Crucially, *merely mentioning* a type does not make it resolvable. A `Texture` field, a `Texture` parameter, or a
`Texture` return type creates no descriptor on its own:

```cpp
struct [[=ctr::singleton{}]] Renderer {
    Texture currentTexture;   // Texture is mentioned, not discovered
};
```

A type becomes resolvable only when **Ctorium** has a descriptor for it — from an annotated bean type, a factory
producer method, or a runtime binding. That boundary is deliberate: it keeps discovery predictable and stops one
innocent header from pulling half the universe into your container. The universe should at least ask first.

Factories get their full treatment in [Guide 09 — Factories](guide-09-factories.md); for discovery, the only thing to
remember is that the factory and its producer methods need a descriptor, while a *produced* type does not need a
lifetime marker of its own.

---

## 8. Roots are explicit, not transitive

It is tempting to imagine discovery as "start at `game` and walk into everything reachable." It is not. **Ctorium**
inspects the declarations visible *under the roots you pass* — it does not follow `game` into every library `game` uses,
then into their libraries, and eventually into the operating system.

So if a namespace matters to a context, name it:

```cpp
context.discover<^^game, ^^ui, ^^plugins>();
```

If the same type shows up through two roots — say `game` and `game_plugins` both expose `game::GameConfig` — **Ctorium**
deduplicates it and creates the descriptor once, which makes broad root lists safe. (Safe to *compile*, at least; one
type wearing two badges still isn't a design award.) Explicit roots are boring, and boring is excellent in dependency
injection.

---

## 9. Scanning non-bean types for metadata

By default, discovery only records the beans and factories it finds. `ctr::DiscoverOptions` can widen that:

```cpp
ctr::DiscoverOptions options{
    .retainAllMetadata = true
};
```

With `retainAllMetadata = true`, **Ctorium** also retains reflected metadata for visible *non-bean* types — types
reachable from your roots that carry no **Ctorium** lifetime marker. You would do this when you care about metadata:
user annotations on support types, methods, or parameters that application code wants to inspect later. The cost is
real — more retained metadata means more memory, and the tiny goblin in charge of RAM notices these things — so turn it
on only when you actually read that metadata.

Concretely, a type can carry a *user* annotation — one defined by your code, not by **Ctorium**:

```cpp
struct [[=ctr::singleton{}]] KeyboardListener {
    [[=busSubscription{.priority = 10}]]
    bool onKeyboardKeyPressed(KeyboardKeyPressed message) {
        return true;
    }
};
```

With `retainAllMetadata = true`, if `KeyboardListener` is part of a discovered root, **Ctorium** preserves the reflected
information about `onKeyboardKeyPressed` and its `busSubscription` annotation rather than discarding it. Metadata
follows the scanned surface: a fully scanned type retains the metadata discovered for it, while a type that only
received a minimal descriptor keeps descriptor metadata and nothing invented — invented metadata would be fan fiction,
and **Ctorium** writes containers, not novels. (The API for *reading* that metadata back is still settling and is
intentionally left out of these guides for now.)

---

## 10. Practical layout and full example

A common layout keeps application and plugin beans in their own namespaces and headers:

```text
app/
  GameConfig.hpp
  Renderer.hpp
  Hud.hpp
plugins/
  DebugOverlay.hpp
  Profiler.hpp
main.cpp
```

`main.cpp` includes the bean headers before discovery, then runs the now-familiar sequence — declare the surface, start,
resolve, stop:

```cpp
#include "app/GameConfig.hpp"
#include "app/Renderer.hpp"
#include "app/Hud.hpp"

#include "plugins/DebugOverlay.hpp"
#include "plugins/Profiler.hpp"

#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/Registration.hpp>
#include <ctr/Markers.hpp>

namespace app {
    struct [[=ctr::singleton{}]] GameConfig {
        bool debug = true;
    };

    struct [[=ctr::singleton{}]] Renderer {
        explicit Renderer(ctr::Bean<GameConfig> config)
            : config_(config) {}

        ctr::Bean<GameConfig> config_;
    };
}

namespace plugins {
    struct [[=ctr::singleton{}]] DebugOverlay {
        explicit DebugOverlay(ctr::Bean<app::GameConfig> config)
            : config_(config) {}

        ctr::Bean<app::GameConfig> config_;
    };
}

int main() {
    ctr::BeanContext& context = ctr::BeanContext::resolveContext();

    context.discover<^^app, ^^plugins>();
    context.start();

    {
        ctr::Bean<app::Renderer> renderer = context.resolve<app::Renderer>();
        ctr::Bean<plugins::DebugOverlay> overlay =
            context.resolve<plugins::DebugOverlay>();
    }

    context.stop();
}
```

The whole guide reduces to that ordering: include what the context must discover, call `discover<...>()`, then
`start()`, then `resolve<...>()`.

---

## 11. Common mistakes

Most discovery problems are one of three, and each has a one-line fix:

- **A bean's header isn't included before discovery.** The declaration exists but isn't visible, so it isn't discovered.
  Fix: include it ahead of `discover<...>()`.
- **Discovery is called after `start()`.** This one really is an error (`ctr::ContextStateError`). Fix: discover first,
  then start.
- **Runtime configuration is treated as discovery.** `defaultNamed(...)` and `bindSingleton(...)` are runtime
  contributions added directly to the context — they never require re-running discovery.

Splitting roots across several pre-start `discover<...>()` calls is *not* in this list: it works (the contributions
accumulate). Listing the roots together is a readability preference, not a correctness rule.

---

## 12. Mental model

`context.discover<^^app, ^^plugins>();` reads, step by step, as: **Ctorium** receives the roots `app` and `plugins`;
uses compile-time reflection over the declarations visible under them; finds the annotated bean types, factories, and
producer methods; builds compact descriptors and an optimized metadata catalog during compilation; and hands the runtime
context that prepared catalog. Bean instances come later — from `start()`, `resolve(...)`, `bindSingleton(...)`, or
factory-backed resolution.

That is discovery: the map, not the journey. And definitely not the dragon — the dragon is usually build configuration.

---

## 13. Checklist

Before moving on to contexts, check that:

- every bean header is included (or module imported) before discovery;
- every namespace used as a root is visible before discovery;
- `discover<...>()` is called before `start()`;
- every discovered bean type has exactly one lifetime marker, and every discovered factory has `[[=ctr::factory{}]]`;
- runtime configuration is added directly to the context, not folded into discovery;
- `retainAllMetadata = true` is set only when you actually read metadata from non-bean types.

Next guide: [Guide 03 — Contexts](guide-03-contexts.md).
