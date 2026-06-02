# Ctorium

**Ctorium** is a modern C++ dependency injection container focused on easy declarative discovery, explicit bean handles,
lifecycle metadata, and application-level runtime composition. Its Inversion of Control (IoC) and Dependency Injection (
DI) runtime is powered by compile-time (AoT) reflection.

It lets you declare application services directly in C++ with attributes, discover them through visible reflection
roots, and resolve them at runtime through a tracked bean context.

**Ctorium** is designed around a simple rule: the C++ annotated source is the source of truth. There is no external scan
step and no compiled generator.

## IoC, DI and AoT

Inversion-of-Control (IoC) is the broader principle: application objects do not control how their dependencies are
created, selected, wired, or managed. That responsibility is moved to the container.

Dependency-Injection (DI) is a concrete way to apply IoC: objects declare what they need, and those dependencies are
supplied from the outside through constructors, factories, or other injection points.

Ahead-of-Time (AoT) means compile-time preparation. In **Ctorium**, dependency metadata and construction information can
be produced before the program runs, instead of being discovered lazily during runtime resolution. The runtime still
resolves and manages objects, but it consumes a model that was already prepared at compile time.

In short: IoC decides who controls construction, wiring, resolution, and lifecycle. DI defines how objects receive their
dependencies. AoT defines when dependency metadata and construction structure are prepared: at compile time, before
runtime resolution.

**Ctorium** provides all three. `ctr::BeanContext` controls resolution, runtime bindings, and lifecycle, while objects
and factories declare dependencies through `ctr::Bean<T>` parameters. AoT means **Ctorium** prepares dependency metadata
and construction paths at compile time, then uses that prepared information during runtime resolution.

## Toolchain requirements

**C++ 26 with reflection** capable compiler (**gcc 16.1.0+** is fine).

Compile and run `toolchain-check` to check your toolchain against the C++ 26 reflexion API needed by **Ctorium**.

## Example

```cpp
#include <string_view>
#include <ctr/Ctorium.hpp>

namespace app {

struct Logger {
    virtual ~Logger() = default;
    virtual void log(std::string_view message) = 0;
};

struct [[=ctr::singleton{}]] ConsoleLogger : Logger {
    void log(std::string_view message) override;
};

struct [[=ctr::singleton{}]] GameService {
    explicit GameService(ctr::Bean<Logger> logger)
        : logger_(logger) {}

    void start() {
        logger_->log("game started");
    }

    ctr::Bean<Logger> logger_;
};

} // namespace app

int main() {
    ctr::BeanContext& context = ctr::BeanContext::resolveContext("game");

    context.discover<^^app>();
    context.start();

    ctr::Bean<app::GameService> game = context.resolve<app::GameService>();
    game->start();

    context.stop();
}
```

Interesting stuff in this example :

- ConsoleLogger is declared as a singleton bean
- ConsoleLogger implements Logger
- GameService is declared as a singleton bean
- GameService declares a dependency on a Logger through its constructor

In our main :

- we create an IoC context named "game"
- we discover the `app` namespace as the reflection root.
- we start the context
- we ask for a `GameService`.

This is where the magic happens. The **Ctorium**'s IoC context resolves the object graph for you:

- it knows `GameService` needs a `Logger`
- it knows a `ConsoleLogger` can fit the request
- it creates, if needed, a `ConsoleLogger`
- it creates, if needed, a `GameService` with the `ConsoleLogger` injected
- it returns the ready-to-go `GameService` to the caller

Then we start the `GameService` logic and finally properly stops the `context` to clean things up.

The `main` function stays clean, simple, and focused on application flow.

You focus on your business logic: object discovery, dependency resolution, wiring, lifecycle, and tracked handles are
taken care of by **Ctorium**.

Boiler plate vanished and tedious wiring is now a simple declaration.

Want to find out more? Dive into our [guide](guides/guide-00-intro.md).

## **Ctorium** Capabilities

- [x] C++26 dependency injection powered by compile-time reflection
    - [x] no external scanner
    - [x] no separate generator executable
    - [x] no generated source files to commit or include
    - [x] toolchain check tool
    - [x] header-only library
    - [ ] ~~dynamic link library~~

- [x] Easy attribute-based bean declaration model
    - [x] `singleton`
    - [x] `prototype`
    - [x] `threadLocal`
    - [x] `session`
    - [x] `named`
    - [x] `factory`
    - [x] `postConstruct`
    - [x] `preDestroy`

- [x] Compile-time discovery
    - [x] discovery from visible headers, modules, and namespaces
    - [x] multiple reflection roots in one `discover<...>()` call
    - [ ] per-context descriptor and annotation catalog

- [x] Injection models
    - [x] constructor injection through `ctr::Bean<T>`
    - [ ] setter injection through `ctr::Bean<T>`
    - [x] named injection points
    - [x] automatic constructor selection
    - [x] deterministic error on ambiguous constructor candidates

- [x] Tracked lightweight bean handles
    - [x] typed handles with `ctr::Bean<T>`
    - [x] type-erased handles with `ctr::AnyBean`
    - [x] safe cast helpers with `exact<T>()`, `compatible<T>()`, `cast<T>()` and `tryCast<T>()`
    - [x] `bean.context()` returns the owning context

- [x] Lifetime models
    - [x] singleton
    - [x] eager and lazy singletons
    - [x] prototype
    - [x] thread-local singleton, one instance per context, key and thread
    - [x] session

- [x] Runtime bean contexts
    - [x] default context for most applications
    - [x] fully isolated parallel contexts for advanced use cases
    - [x] explicit `start()` / `stop()` lifecycle
    - [x] `ctr::BeanContext` can be injected like any other bean
    - [x] scoped contexts with session lifetime (`resolveScope`, `start`/`stop`/`restart`)
    - [x] scope `userData` association
    - [x] a bean can retrieve its owning context with `bean.context()`
    - [ ] expose their lifecycle state
    - [ ] destroyBean() api

- [x] Qualifier-based resolution
    - [x] named qualifiers
    - [x] priority-based selection
    - [x] runtime default named selection with `defaultNamed<T>(...)`

- [x] Dependency resolution
    - [x] single bean resolution with `resolve<T>()`
    - [x] automatic dependency graph resolution
    - [x] deterministic error on missing, ambiguous, or cyclic dependencies

- [x] Runtime instance binding
    - [x] adopt external singleton instances with `bindSingleton<T>()`
    - [x] adopt external session instances with `bindSession<T>()`
    - [x] context-owned bound instances
    - [x] named and prioritized runtime bindings
    - [x] bound instances participate in resolution, lifecycle, listeners, and shutdown

- [x] Bean lifecycle hooks
    - [x] initialization hooks with `postConstruct`
    - [x] destruction hooks with `preDestroy`
    - [x] hook dependency injection through `ctr::Bean<T>`

- [x] Beans lifecycle listener
    - [x] typed listener registration with `on<T>()`
    - [x] global listener registration with `on()`
    - [x] explicit listener removal with `remove(handle)`
    - [x] `onInitialized`, before `postConstruct`
    - [x] `onCreated`, after `postConstruct`
    - [x] `onPreDestroy`, before `preDestroy`
    - [x] `onDestroyed`, after `preDestroy`
    - [x] listener priorities
    - [x] snapshot-based listener dispatch
    - [ ] metadata-aware listener callbacks

- [ ] Reflection metadata
    - [ ] metadata for resolved beans and descriptors
    - [ ] scanned annotations on types, methods, and parameters
    - [ ] metadata views for typed and type-erased listeners
    - [ ] factory method metadata for factory-produced beans

- [x] Factories
    - [x] factories are singleton beans with constructor-injected dependencies
    - [x] producer methods can create `singleton` beans
    - [x] producer methods can create `prototype` beans
    - [x] producer methods can create `session` beans
    - [x] producer methods can create `threadLocal` beans
    - [x] produced types do not need to be annotated
    - [x] producer methods can return `T` or `std::unique_ptr<T>`
    - [x] produced beans participate in lifecycle hooks and listener phases
    - [ ] produced beans participate in metadata

- [x] Deterministic and clear error model
    - [x] deterministic **Ctorium** errors for configuration, state, and resolution failures
    - [x] user exceptions are propagated unchanged

- [ ] Thread-safety
    - [x] concurrent singleton resolution (double-checked locking)
    - [x] snapshot-based listener dispatch (lock-free reads)
    - [ ] listener registration and removal API
    - [ ] runtime default named selection
    - [ ] runtime bindings