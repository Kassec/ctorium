# Guide 05 — bindSingleton

`bindSingleton<T>(...)` imports an object you constructed yourself into a **Ctorium** context. After the call the
context owns that object and exposes it as a **singleton runtime bean** for its binding key — the triple of exposed type,
name, and priority — so every later `resolve<T>()` for that type and key returns the same imported object.

It is for types **Ctorium** does not build itself: a plain C++ type with no annotations, or any object you would rather
construct outside the container — a configuration loaded at startup, a native handle wrapper, a client returned by an
external API. You bring the object; **Ctorium** gives it a badge. Very administrative, very useful.

This guide builds on contexts ([Guide 03](guide-03-contexts.md)) and the qualifier
model ([Guide 04](guide-04-qualifiers.md)).

---

## 1. Ownership transfer

The object enters through a `std::unique_ptr<T>`, so the binding is an ownership transfer: before the call user code
owns the object, after it the context does. The instance is moved in, not copied or rebuilt — the same object, now
managed.

```cpp
std::unique_ptr<AppConfig> config = std::make_unique<AppConfig>();

context.bindSingleton<AppConfig>(std::move(config));

ctr::Bean<AppConfig> resolved = context.resolve<AppConfig>();
```

After `std::move`, the original owner is empty — do not keep using it. There should be nothing left to use anyway;
`std::unique_ptr` saw to that, with legal paperwork and a tiny suitcase. Each binding holds exactly one object, so
repeated resolutions return that same instance; when you need a fresh object per call, that is a factory's
job ([Guide 09 — Factories](guide-09-factories.md)).

---

## 2. Importing a plain type

The bound type needs no **Ctorium** annotation. An ordinary class is imported and then resolved like any other bean —
and other beans can inject it as a `ctr::Bean<T>` dependency:

```cpp
struct ExternalTelemetryClient {
    explicit ExternalTelemetryClient(std::string endpoint) : endpoint_(std::move(endpoint)) {}
    void send(std::string_view message) { /* send to endpoint_ */ }
    std::string endpoint_;
};

context.bindSingleton<ExternalTelemetryClient>(
    std::make_unique<ExternalTelemetryClient>("telemetry.local"));
context.start();

ctr::Bean<ExternalTelemetryClient> telemetry =
    context.resolve<ExternalTelemetryClient>();
```

A bound type can also be a discovered type — for example a type already registered through `discover<>()`. When
the `(T, name, priority)` triple is distinct from every existing candidate, both the discovered bean and the bound
instance coexist as independent candidates and participate in priority arbitration. When the triple collides with an
existing candidate, `ctr::ConfigurationError` is raised immediately (see §3).

For the binding, **Ctorium** creates a **runtime descriptor** — a descriptor built from a runtime contribution rather
than from compile-time discovery. That is enough to resolve the bean and to expose descriptor-level metadata (type,
name, lifetime, origin). It does *not* fabricate scanned method, parameter, or annotation metadata: that only comes from
discovery ([Guide 02 — Discovery](guide-02-discovery.md)). Use discovery when scanned metadata matters; use
`bindSingleton` when importing the instance matters. Different paperwork, same office.

---

## 3. Before or after `start()`

A binding works on both sides of startup with the same rules. Added **before** `start()`, it is queued and integrated
when the context starts — the natural shape for bootstrap objects such as configuration loaded from the command line, or
a logger built before the container exists. Resolve via `resolve<T>()` after `start()`.

```cpp
context.bindSingleton<AppConfig>(std::make_unique<AppConfig>());
context.start();
```

Added **after** `start()`, the binding is stored immediately and resolvable at once — the shape for objects that only
appear at runtime, like a project the user just opened or a handle returned by an external API. A type never seen by
discovery is fine here: the context adopts it on the spot.

```cpp
context.start();
context.bindSingleton<ProjectSession>(std::make_unique<ProjectSession>());
ctr::Bean<ProjectSession> session = context.resolve<ProjectSession>();
```

The duplicate rule is the same before and after `start()`: binding a `(T, name, priority)` triple that already exists
raises `ctr::ConfigurationError` immediately (as does binding one while an identical key is being materialized
concurrently). There is no in-place replacement. Binding `ctr::BeanContext` is also forbidden — it is registered
implicitly at `start()`.

---

## 4. Named and priority options

By default a binding lands in the unnamed space with priority `0`. `ctr::BindOptions` changes that, using the exact
qualifier model from [Guide 04 — Qualifiers](guide-04-qualifiers.md): `.name` places the binding in a named space,
`.priority` arbitrates within a space, and the two combine. Multiple bindings of the same type are allowed — under
distinct names, distinct priorities, or both — before and after `start()`:

```cpp
context.bindSingleton<AppLogger>(std::make_unique<AppLogger>("audit"),
    ctr::BindOptions{.name = "audit"});

context.bindSingleton<AppLogger>(std::make_unique<AppLogger>("metrics"),
    ctr::BindOptions{.name = "metrics", .priority = 20});

context.start();

ctr::Bean<AppLogger> audit   = context.resolve<AppLogger>(ctr::named("audit"));
ctr::Bean<AppLogger> metrics = context.resolve<AppLogger>(ctr::named("metrics"));
```

The names select different spaces — a request for `"audit"` never returns the `"metrics"` binding. The logger stays in
its lane, a rare and beautiful thing.

Multiple bindings under the same name but different priorities are also valid: the highest-priority candidate wins at
resolution, just like discovered beans with priority arbitration.

---

## 5. Lifecycle and thread-safety

A bound instance is a full participant in the context lifecycle: a pre-start binding enters the lifecycle at `start()`,
a post-start binding is exposed at once, lifecycle listeners ([Guide 07 — Listeners](guide-07-listeners.md)) observe it,
and shutdown ([Guide 03 — Contexts](guide-03-contexts.md)) closes it through the registry — running its `preDestroy`
hook if the type defines one ([Guide 06 — Hooks](guide-06-hooks.md)). The boundary is simply: once you bind it, the
object follows the context, not your code.

`bindSingleton<T>(...)` after `start()` is thread-safe. A binding becomes visible to resolutions that begin after the
call returns; a resolution already in flight is not guaranteed to observe it. Thread-safety of the sibling runtime
contributions lives with them — default named selection in [Guide 04](guide-04-qualifiers.md), listeners
in [Guide 07](guide-07-listeners.md).

---

## 6. Full example

```cpp
#include <memory>
#include <string>
#include <string_view>

#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/Options.hpp>

struct ExternalTelemetryClient {
    explicit ExternalTelemetryClient(std::string endpoint) : endpoint_(std::move(endpoint)) {}
    void send(std::string_view message) { /* send to endpoint_ */ }
    std::string endpoint_;
};

int main() {
    ctr::BeanContext& context = ctr::BeanContext::resolveContext();

    context.bindSingleton<ExternalTelemetryClient>(
        std::make_unique<ExternalTelemetryClient>("telemetry.local"));
    context.start();

    {
        ctr::Bean<ExternalTelemetryClient> telemetry =
            context.resolve<ExternalTelemetryClient>();
        telemetry->send("application started");
    }

    context.stop();
}
```

No discovery is needed: the context only imports a runtime instance, and the external object becomes available as a
singleton runtime bean for the unnamed `ExternalTelemetryClient` key.

---

## 7. Mental model

`context.bindSingleton<T>(std::move(object), options)` reads as: user code hands a `std::unique_ptr<T>` to the context;
the binding targets the `(T, name, priority)` key derived from `BindOptions`; **Ctorium** creates a runtime descriptor
and takes ownership; the instance becomes the singleton runtime bean for that binding key. Before `start()` it waits for
startup; after `start()` it is stored immediately. Both return the context for chaining. From then on
`resolve<T>()` can select it, listeners can observe it, and `stop()` closes it. Multiple bindings of the same type are
allowed at distinct names or priorities; a duplicate `(T, name, priority)` is rejected immediately. The binding never
recreates the object and never leaves ownership in user code. It is a runtime bean now; it has joined the organization.

---

## 8. Checklist

Before moving on to hooks, check that:

- `bindSingleton<T>(...)` receives a `std::unique_ptr<T>`, and ownership is meant to pass to the context;
- the imported object is meant to be the singleton runtime bean for its binding key;
- no duplicate `(T, name, priority)` exists in the context — a collision raises `ConfigurationError`;
- named bindings use `BindOptions{.name = "x"}`, and priority uses `BindOptions{.priority = n}` when selection needs it;
- multiple bindings of the same type use distinct names, distinct priorities, or both;
- bindings are added before `start()` for setup objects, after `start()` for objects that appear at runtime — both follow the same rules;
- bound instances are expected to take part in resolution, listeners, and shutdown;
- user code does not keep using the pointer after `std::move`.

Next guide: [Guide 06 — Hooks](guide-06-hooks.md).
