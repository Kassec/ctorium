# Ctorium

**Ctorium is a C++26 Inversion of Control (IoC) and Dependency Injection (DI) library powered by compile-time (AoT)
reflection.**

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

**C++ 26** capable compiler (**gcc 16.1.0** is fine).

Compile and run `toolchain-check` to check your toolchain against the C++ 26 reflexion API needed by **Ctorium**.

## Example

```cpp
#include <string_view>
#include <ctorium/ctorium.hpp>

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
    ctr::Bean<ctr::BeanContext> context = ctr::BeanContext::findOrCreate("game");

    context->discover<^^app>();
    context->start();

    ctr::Bean<app::GameService> game = context->resolve<app::GameService>();
    game->start();

    context->stop();
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

Want to find out more? Dive into our [guide](../docs/public/docs/guide-00-intro.md).

## **Ctorium** Capabilities

- [x] C++26 dependency injection powered by compile-time reflection
    - [x] no external scanner
    - [x] no separate generator executable
    - [x] no generated source files to commit or include
    - [x] toolchain check tool
    - [x] header-only library
    - [ ] dynamic link library

- [ ] Easy attribute-based bean declaration model
    - [ ] `singleton`
    - [ ] `prototype`
    - [ ] `threadLocal`
    - [ ] `session`
    - [ ] `named`
    - [ ] `factory`
    - [ ] `postConstruct`
    - [ ] `preDestroy`

- [ ] Compile-time discovery
    - [ ] discovery from visible headers, modules, and namespaces
    - [ ] multiple reflection roots in one `discover<...>()` call
    - [ ] per-context descriptor and annotation catalog

- [ ] Injection models
    - [ ] constructor injection through `ctr::Bean<T>`
    - [ ] setter injection through `ctr::Bean<T>`
    - [ ] named injection points
    - [ ] automatic constructor selection
    - [ ] deterministic error on ambiguous constructor candidates

- [ ] Tracked lightweight bean handles
    - [ ] typed handles with `ctr::Bean<T>`
    - [ ] type-erased handles with `ctr::AnyBean`
    - [ ] safe cast helpers with `exact<T>()`, `compatible<T>()`, `cast<T>()` and `tryCast<T>()`

- [ ] Lifetime models
    - [ ] singleton
    - [ ] eager and lazy singletons
    - [ ] prototype
    - [ ] thread-local singleton, one instance per context, key and thread (destroyed at thread end or context
      stop/shutdown)
    - [ ] session

- [ ] Runtime bean contexts
    - [ ] default context for most applications
    - [ ] fully isolated parallel contexts for advanced use cases
    - [ ] explicit `start()` / `stop()` lifecycle
    - [ ] `ctr::BeanContext` can be injected like any other bean
    - [ ] a bean can retrieve its owning context with `bean.context()`
    - [ ] expose their lifecycle state

- [ ] Qualifier-based resolution
    - [ ] named qualifiers
    - [ ] priority-based selection
    - [ ] runtime default named selection with `defaultNamed<T>(...)`

- [ ] Dependency resolution
    - [ ] single bean resolution with `resolve<T>()`
    - [ ] multiple bean resolution with `resolveAll<T>()`
    - [ ] automatic dependency graph resolution
    - [ ] deterministic error on missing, ambiguous, or cyclic dependencies

- [ ] Runtime instance binding
    - [ ] adopt external instances with `bindInstance<T>()`
    - [ ] context-owned bound instances
    - [ ] named and prioritized runtime bindings
    - [ ] bound instances participate in resolution, lifecycle, listeners, and shutdown

- [ ] Bean lifecycle hooks
    - [ ] initialization hooks with `postConstruct`
    - [ ] destruction hooks with `preDestroy`
    - [ ] hook dependency injection through `ctr::Bean<T>`

- [ ] Bean lifecycle listeners
    - [ ] typed listeners
    - [ ] global listeners
    - [ ] `onInitialized`, before `postConstruct`
    - [ ] `onCreated`, after `postConstruct`
    - [ ] `onPreDestroy`, before `preDestroy`
    - [ ] `onDestroyed`, after `preDestroy`
    - [ ] listener priorities
    - [ ] explicit listener removal
    - [ ] snapshot-based listener dispatch
    - [ ] metadata-aware listener callbacks

- [ ] Reflection metadata
    - [ ] metadata for resolved beans and descriptors
    - [ ] scanned annotations on types, methods, and parameters
    - [ ] metadata views for typed and type-erased listeners
    - [ ] factory method metadata for factory-produced beans

- [ ] Factories
    - [ ] factories are singleton beans with constructor-injected dependencies
    - [ ] producer methods can create singleton, prototype, session or thread-local beans
    - [ ] produced types do not need to be annotated
    - [ ] producer methods can return `T` or `std::unique_ptr<T>`
    - [ ] user parameters can be passed with `resolve<T>(args...)`
    - [ ] default user parameters can be configured with `factoryDefaultArgs<T>(...)`
    - [ ] produced beans participate in lifecycle, listeners, and metadata

- [ ] Deterministic and clear error model
    - [ ] deterministic **Ctorium** errors for configuration, state, and resolution failures
    - [ ] user exceptions are propagated unchanged

- [ ] Thread-safety
    - [ ] listener registration
    - [ ] listener removal
    - [ ] runtime default named selection
    - [ ] runtime bindings
    - [ ] factory defaults
