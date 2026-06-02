# Guide 01 — Beans

Guide 00 introduced the word *bean*: an object **Ctorium** creates, keeps alive for as long as its lifetime says, hands out through `ctr::Bean<T>` handles, and destroys when its context shuts down. This guide turns that one sentence into something you can write: how to declare a bean, pick its lifetime, inject one bean into another, use the handles correctly, cast between exposed types, and avoid the usual "why is my constructor angry?" mistakes.

No container wizard robe required. A normal hoodie is fine.

---

## 1. Declaring a bean

A plain C++ `struct` or `class` is just a type. It becomes a **bean type** — something **Ctorium** can manage — the moment you attach a lifetime marker to it:

```cpp
struct [[=ctr::singleton{}]] GameConfig {
    int width = 1280;
    int height = 720;
};
```

Without the attribute, `GameConfig` is ordinary C++ that **Ctorium** ignores. With it, `GameConfig` is a bean type, and `[[=ctr::singleton{}]]` is the **lifetime marker** that says how its instances are created, reused, and destroyed.

---

## 2. The lifetimes

Every bean type carries exactly one lifetime marker. Three cover the common cases, and a fourth, the **session**, is the subject of its own chapter:

| Lifetime marker | One instance per… |
|---|---|
| `[[=ctr::singleton{}]]` | context and key |
| `[[=ctr::prototype{}]]` | resolution |
| `[[=ctr::threadLocal{}]]` | context, key, and thread |
| `[[=ctr::session{}]]` | started scope |

"Per context and key" is the recurring phrase. The **context** is the runtime container holding the beans ([Guide 03 — Contexts](guide-03-contexts.md)), and the **key** is the identity that tells otherwise-identical beans apart ([Guide 04 — Qualifiers](guide-04-qualifiers.md)). Until those guides, read it as "inside this container, for this bean identity."

### Singleton

A singleton has one instance per context and key, so every resolution from the same context and key returns a handle to the *same* object:

```cpp
struct [[=ctr::singleton{}]] GameConfig {
    int width = 1280;
    int height = 720;
};
```

It fits configuration, shared services, registries, and systems that should exist once per application context. It does not fit short-lived data, per-request state, or "I just need a fresh object because vibes."

### Prototype

A prototype produces a brand-new instance on every resolution:

```cpp
struct [[=ctr::prototype{}]] FrameStats {
    int drawCalls = 0;
    int triangles = 0;
};
```

Each `resolve<FrameStats>()` returns its own object. A prototype lives exactly as long as the handles to it: once the last `ctr::Bean<FrameStats>` referencing it is released, it is destroyed. (That tracking is what makes `ctr::Bean<T>` more than a pointer — see §6.)

### Thread-local

A thread-local has one instance per context, key, *and* thread:

```cpp
struct [[=ctr::threadLocal{}]] ThreadScratchpad {
    std::vector<float> temporaryValues;
};
```

Two threads resolving `ThreadScratchpad` each get their own instance, and no thread ever sees another thread's copy. It is not "a global singleton, but spooky" — it is "a singleton, scoped to the thread."

A thread-local instance is destroyed with its full lifecycle (hooks, listeners) when its owning thread exits, on that thread. `root.stop()` destroys all remaining thread-local instances from threads still alive at that point; user code must ensure no concurrent access to those instances at that time.

### Session

A session has one instance per started **scope** — a child runtime keyed under the root, with its own start/stop cycle:

```cpp
struct [[=ctr::session{}]] DocumentState {
    std::string path;
    bool dirty = false;
};
```

It fits state that lives longer than one resolution but shorter than the whole application — an open document, a connected user, a running job. A session bean has no instance until a scope is started, and it is rebuilt when that scope restarts. Sessions, scopes, and the `[[=ctr::scoped]]` injection qualifier have their own chapter: [Guide 08 — Sessions](guide-08-sessions.md).

---

## 3. One marker, and one only

Conflicting lifetimes are not a judgement call **Ctorium** will make for you:

```cpp
struct [[=ctr::singleton{}]]
       [[=ctr::prototype{}]]
ConfusedService {
};
```

A type with two lifetime markers does not compile. **Ctorium** rejects it during discovery with a clear message rather than quietly picking one — the mistake is caught at build time, not left to surprise you at runtime.

---

## 4. Singleton options

The singleton marker can carry options:

```cpp
struct [[=ctr::singleton{.lazy = false, .priority = 10}]] TextureCache {
};
```

`lazy` decides *when* the instance is built. A **lazy** singleton (the default) is created on first use; an **eager** one (`lazy = false`) is created during `start()`. `priority` breaks ties when several beans could satisfy the same request — higher wins. Priority and the rest of the selection rules belong to [Guide 04 — Qualifiers](guide-04-qualifiers.md); for now, just know the knobs exist.

---

## 5. Constructor injection

A bean receives the things it depends on through its constructor, by declaring parameters of type `ctr::Bean<U>`:

```cpp
struct [[=ctr::singleton{}]] GameConfig {
    int width = 1280;
    int height = 720;
};

struct [[=ctr::singleton{}]] Renderer {
    explicit Renderer(ctr::Bean<GameConfig> config)
        : config_(config) {}

    ctr::Bean<GameConfig> config_;
};
```

`Renderer` never builds its own `GameConfig`. When **Ctorium** creates `Renderer`, it resolves `GameConfig` first and passes it in; the constructor only *receives* the dependency. That is the whole trick — the rabbit is just dependency resolution wearing a tiny hat.

---

## 6. Working with `ctr::Bean<T>`

`ctr::Bean<T>` is the typed, tracked handle from Guide 00: it exposes the managed object as `T`, but the object itself is owned by the **Ctorium** runtime according to its lifetime, not by the handle. You reach the object the obvious ways:

```cpp
ctr::Bean<GameConfig> config = context.resolve<GameConfig>();

config->width;      // member access
(*config).height;   // dereference
config.value();     // explicit accessor
```

Because the handle is tracked, copying it keeps tracking the same instance, moving it transfers that tracking, and destroying it releases one reference:

```cpp
ctr::Bean<GameConfig> first  = context.resolve<GameConfig>();
ctr::Bean<GameConfig> second = first;   // both refer to the same bean
```

For singletons this bookkeeping is mostly invisible, since the instance belongs to the context regardless. For prototypes it is the whole game: the instance survives exactly as long as a handle to it does. A handle can also point back at the context that produced it:

```cpp
ctr::BeanContext& owner = config.context();
```

The mental model is one line:

> Keep the handle while you need the bean.

No secret ownership ritual — just do not throw the handle away and then expect the rabbit to still be in the hat. When a bean stores a dependency, it stores the handle as-is; do not strip it down to a raw pointer, because the handle is what carries the tracking **Ctorium** relies on.

Using a `Bean<T>` or `AnyBean` handle after the owning context has been permanently stopped (`root.stop()`) is undefined behavior. Release all handles before stopping the root:

```cpp
context.start();

{
    ctr::Bean<GameConfig> config = context.resolve<GameConfig>();
    config->width;
}   // handle released here

context.stop();   // safe: no dangling handles
```

---

## 7. Casting between exposed types

A bean is often resolved through one type but occasionally needs to be examined as a more specific one — code holding an `InputService` may want to know whether the concrete object is a `KeyboardInputService`. `ctr::Bean<T>` offers four helpers for that:

- `exact<U>()` — is the managed object *exactly* `U`?
- `compatible<U>()` — can the bean be exposed as `U`?
- `cast<U>()` — return a `ctr::Bean<U>`, or throw `ctr::ResolutionError` if the cast is invalid.
- `tryCast<U>()` — return an empty `std::optional` instead of throwing.

Use `cast` when you expect success and want the failure to be loud:

```cpp
ctr::Bean<InputService> input = context.resolve<InputService>();

if (input.compatible<KeyboardInputService>()) {
    ctr::Bean<KeyboardInputService> keyboard =
        input.cast<KeyboardInputService>();

    keyboard->enableKeyboardCapture();
}
```

Use `tryCast` when the type may legitimately be absent:

```cpp
ctr::Bean<InputService> input = context.resolve<InputService>();

std::optional<ctr::Bean<KeyboardInputService>> keyboard =
    input.tryCast<KeyboardInputService>();

if (keyboard.has_value()) {
    (*keyboard)->enableKeyboardCapture();
}
```

Casting keeps tracking the same managed instance and needs no metadata lookup. `AnyBean`, the type-erased handle global listeners receive, follows the same cast model — see [Guide 07 — Listeners](guide-07-listeners.md).

---

## 8. One compatible constructor

For a scanned bean, **Ctorium** looks for a **compatible constructor**: one that either takes no parameters, or whose parameters are *all* `ctr::Bean<U>` dependencies. A bean type must have **exactly one** of them.

```cpp
struct [[=ctr::singleton{}]] AudioSystem {
    explicit AudioSystem(ctr::Bean<GameConfig> config, ctr::Bean<Logger> logger)
        : config_(config), logger_(logger) {}

    ctr::Bean<GameConfig> config_;
    ctr::Bean<Logger> logger_;
};
```

The rule is strict on purpose: when a type exposes two compatible constructors, **Ctorium** does not try to rank them or prefer the one with more parameters — it rejects the type at build time with a *multiple compatible constructors* error. The trap worth knowing is that a default constructor and an injectable constructor are *both* compatible, so having both is already a conflict:

```cpp
struct [[=ctr::singleton{}]] AudioSystem {
    AudioSystem() = default;                              // compatible: no parameters
    explicit AudioSystem(ctr::Bean<GameConfig> config);   // compatible: all ctr::Bean<U>
    // → rejected: two compatible constructors
};
```

Two injectable constructors collide in the same way:

```cpp
struct [[=ctr::singleton{}]] BadMixer {
    explicit BadMixer(ctr::Bean<AudioConfig> config);
    explicit BadMixer(ctr::Bean<AudioDevice> device);
    // → rejected: two compatible constructors
};
```

Guessing which one to call would be convenient right up until it ruins your evening at 2 a.m., so **Ctorium** refuses to guess. Give the type a single compatible constructor and the ambiguity is gone:

```cpp
struct [[=ctr::singleton{}]] AudioMixer {
    AudioMixer(ctr::Bean<AudioConfig> config,
               ctr::Bean<AudioDevice> device)
        : config_(config), device_(device) {}

    ctr::Bean<AudioConfig> config_;
    ctr::Bean<AudioDevice> device_;
};
```

A constructor that takes anything other than `ctr::Bean<U>` is simply *not* compatible, so it never enters this count — which is exactly why the next section exists.

---

## 9. Runtime values are not constructor parameters

A scanned bean's compatible constructor only receives injected dependencies, never values you supply at resolution time. So this does not work as a scanned bean:

```cpp
struct [[=ctr::prototype{}]] TextureRequest {
    explicit TextureRequest(std::string path)   // not a ctr::Bean<U>
        : path_(std::move(path)) {}

    std::string path_;
};
```

`std::string path` is data, not a dependency, so that constructor is not compatible and **Ctorium** will not feed it from a `resolve<TextureRequest>("...")` call. When you need runtime data, reach for one of: a configuration bean, a named bean, a runtime binding, or a factory. Factories — which exist precisely to take user parameters — get their own stage, with possibly dramatic lighting, in [Guide 09 — Factories](guide-09-factories.md). For ordinary dependencies, keep using constructor injection:

```cpp
struct [[=ctr::singleton{}]] AssetConfig {
    std::string root = "assets";
};

struct [[=ctr::singleton{}]] TextureLoader {
    explicit TextureLoader(ctr::Bean<AssetConfig> config)
        : config_(config) {}

    ctr::Bean<AssetConfig> config_;
};
```

---

## 10. Named injection points

When several beans of the same type exist, a constructor can ask for a specific one by name with the `named` attribute:

```cpp
struct [[=ctr::singleton{}]] ReportService {
    explicit ReportService(
        [[=ctr::named{.name = std::define_static_string("audit")}]]
        ctr::Bean<Logger> logger)
        : logger_(logger) {}

    ctr::Bean<Logger> logger_;
};
```

This guide only shows the syntax. The full story — qualifiers, priorities, and the fine art of not asking for "the logger" when three loggers are standing there in fake mustaches — is [Guide 04 — Qualifiers](guide-04-qualifiers.md).

---

## 11. Full example

Three beans: a shared `GameConfig` singleton, a per-resolution `FrameStats` prototype, and a `Hud` singleton that receives its config through constructor injection.

```cpp
#include <ctr/Bean.hpp>
#include <ctr/Markers.hpp>

struct [[=ctr::singleton{}]] GameConfig {
    int width = 1280;
    int height = 720;
};

struct [[=ctr::prototype{}]] FrameStats {
    int drawCalls = 0;
    int triangles = 0;
};

struct [[=ctr::singleton{}]] Hud {
    explicit Hud(ctr::Bean<GameConfig> config)
        : config_(config) {}

    void draw(ctr::Bean<FrameStats> stats) {
        stats->drawCalls += 1;
    }

    ctr::Bean<GameConfig> config_;
};
```

`GameConfig` is shared, `FrameStats` is fresh on every resolution, and `Hud` receives its configuration by constructor. `draw` takes its `FrameStats` as an explicit argument here, to keep the focus on declaration and injection rather than on how the frame stats get there.

---

## 12. The creation sequence

When **Ctorium** builds a bean, it runs through the same steps every time: identify the bean type, read its lifetime marker, select its one compatible constructor, resolve each `ctr::Bean<U>` parameter, call the constructor, then keep or release the instance according to its lifetime.

That is creation, not the full runtime lifecycle — hooks and listeners add more steps, covered in [Guide 06 — Hooks](guide-06-hooks.md) and [Guide 07 — Listeners](guide-07-listeners.md). This much is enough to reason about beans without inviting the entire lifecycle orchestra into the room.

---

## 13. Checklist

Before moving on to discovery, each bean type should:

- carry exactly one lifetime marker;
- expose exactly one compatible constructor (no parameters, or all `ctr::Bean<U>`);
- declare its dependencies as `ctr::Bean<U>` constructor parameters;
- not expect runtime values in a scanned constructor;
- store long-lived dependencies as the `ctr::Bean<T>` handle, not a raw pointer;
- use `exact<T>()`, `compatible<T>()`, `cast<T>()`, or `tryCast<T>()` for type checks;
- pick singleton, prototype, thread-local, or session ([Guide 08](guide-08-sessions.md)) on purpose, not by habit.

Next guide: [Guide 02 — Discovery](guide-02-discovery.md).
