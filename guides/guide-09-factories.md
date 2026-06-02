# Guide 09 — Factories

A **factory** is a **Ctorium** bean whose annotated methods create *other* beans. It exists for two main situations:

1. **Wrapping an external object as a bean.** When the object comes from another library, an OS or platform API, or any
   code you do not own and cannot annotate, a factory method constructs it and hands it to **Ctorium** to manage — no *
   *Ctorium** attribute on the foreign type required.
2. **Calling a constructor that takes non-`Bean` parameters.** A scanned bean can only be built through a constructor
   whose parameters are all `ctr::Bean<U>` ([Guide 01 — Beans](guide-01-beans.md)). When the real construction needs
   literals, configuration values, or anything that is not an injected dependency, a factory method writes that
   construction call explicitly and exposes the result as a bean.

In both cases the factory is the clean way to say "this object is still a bean, but build it by calling this method" —
no secret global, no service locator, just a declared producer.

This guide builds on beans and their lifetimes ([Guide 01 — Beans](guide-01-beans.md)),
discovery ([Guide 02 — Discovery](guide-02-discovery.md)), and
qualifiers ([Guide 04 — Qualifiers](guide-04-qualifiers.md)).

---

## 1. What the `factory` annotation triggers

`[[=ctr::factory{}]]` is recognized specifically by **Ctorium**, and it changes how the type's *methods* are read. On an
ordinary annotated class, the compatible constructor is the single thing **Ctorium** calls to create the bean. On a
factory, that role extends to its annotated methods: **each method carrying a lifetime marker becomes a bean-producing
candidate, on exactly the same footing as the constructor of an annotated class — discovered as a descriptor and called
directly by Ctorium, with no further configuration.**

So the mental shift is small but precise. A normal bean answers "how is *this* type built?" with its constructor. A
factory answers "how are *these* types built?" with one annotated method each. Declaring `[[=ctr::factory{}]]` is what
turns those marked methods from plain member functions into registered producers. Methods left *unannotated* do not
bother **Ctorium** at all — it simply ignores them, so a factory can freely hold ordinary helpers, accessors, or private
logic alongside its producers without any of them being mistaken for a bean source.

The factory object itself is a singleton **Ctorium** bean, discovered through `discover<...>()` like any other annotated
type:

```cpp
struct [[=ctr::factory{}]] ExternalFactory {
};
```

```cpp
ctr::BeanContext& context = ctr::BeanContext::resolveContext();

context.discover<^^app>();
context.start();
```

If the factory declaration is visible in the discovery surface, **Ctorium** prepares one descriptor for the factory and
one for each of its producer methods.

---

## 2. Factory constructor injection

Because the factory is an ordinary singleton bean, its own constructor takes dependencies the usual way — `ctr::Bean<U>`
parameters, exactly the model from [Guide 01 — Beans](guide-01-beans.md):

```cpp
struct [[=ctr::singleton{}]] AppConfig {
    std::string path;
};

struct [[=ctr::factory{}]] ExternalFactory {
    explicit ExternalFactory(ctr::Bean<AppConfig> config,
                             ctr::Bean<Logger> logger)
        : config_(config), logger_(logger) {}

    ctr::Bean<AppConfig> config_;
    ctr::Bean<Logger> logger_;
};
```

Keep the constructor for dependencies the factory object itself needs across its producer methods. Readable factories
are good; nobody wants one that looks like it also manufactures excuses.

---

## 3. Producer methods

A **producer method** is a member method of a factory that creates a bean. It carries a lifetime marker — `singleton`,
`prototype`, `threadLocal`, or `session` — and its return type is the concrete produced type:

```cpp
struct [[=ctr::factory{}]] ExternalFactory {
    [[=ctr::singleton{}]]
    ExternalLogger logger() {
        return ExternalLogger{"logs/app.log"};   // non-Bean constructor argument
    }

    [[=ctr::prototype{}]]
    ExternalRequest request() {
        return ExternalRequest{};
    }
};
```

The produced type needs no **Ctorium** annotation of its own:

```cpp
struct ExternalLogger {
    explicit ExternalLogger(std::string path) : path_(std::move(path)) {}
    void log(std::string_view message) {}
    std::string path_;
};
```

`ExternalLogger` has no lifetime attribute and a non-`Bean` constructor, yet a factory method can produce it — that is
exactly the two use cases from the introduction. The producer method, not the type, declares the bean.

---

## 4. Producer method dependencies

A producer method receives its dependencies as `ctr::Bean<U>` parameters, injected by **Ctorium** when the product is
materialized — the same injection model as a constructor:

```cpp
struct [[=ctr::factory{}]] ExternalFactory {
    [[=ctr::singleton{}]]
    ExternalLogger logger(ctr::Bean<AppConfig> config,
                          ctr::Bean<Clock> clock) {
        return ExternalLogger{config->path, clock->now()};
    }
};
```

A parameter can carry `[[=ctr::named{...}]]` to select a specific candidate, just as at any other injection
point ([Guide 04 — Qualifiers](guide-04-qualifiers.md)):

```cpp
struct [[=ctr::factory{}]] ExternalFactory {
    [[=ctr::singleton{}]]
    ExternalLogger logger(
        [[=ctr::named{.name = std::define_static_string("file")}]]
        ctr::Bean<LoggerSink> sink) {
        return ExternalLogger{sink};
    }
};
```

Producer-method parameters are dependencies resolved from the context — never values supplied by caller code. There is
no mechanism to pass runtime arguments into a producer method: it takes injected `ctr::Bean<U>` parameters or none at
all. When construction needs a non-`Bean` value, the producer method supplies it *itself*, from a config bean or a
literal in the method body (as in §3) — that is the whole point of routing such construction through a factory.

---

## 5. Return forms

A producer method returns either `T` by value or `std::unique_ptr<T>`:

```cpp
struct [[=ctr::factory{}]] ExternalFactory {
    [[=ctr::singleton{}]]
    ExternalLogger byValue() {
        return ExternalLogger{"logs/app.log"};
    }

    [[=ctr::singleton{}]]
    std::unique_ptr<ExternalLogger> byPointer() {
        return std::make_unique<ExternalLogger>("logs/app.log");
    }
};
```

Use `T` when returning by value is natural; use `std::unique_ptr<T>` when the factory allocates dynamically and
transfers ownership to **Ctorium**. Either way the produced bean is managed by the context according to the producer
method's lifetime. Return form controls *how the object is delivered*; the lifetime marker controls *how it is reused
and destroyed*. Different knobs, different jobs.

---

## 6. Producer lifetimes

A producer method's lifetime marker governs the produced bean exactly as the corresponding marker governs a declared
bean ([Guide 01 — Beans](guide-01-beans.md)):

- **`singleton`** — one produced bean per context and key; the first resolution materializes it, later resolutions for
  the same type and key return the same object.
- **`prototype`** — a new produced bean on every matching resolution; the producer method runs each time, which fits
  request objects, temporaries, and per-use wrappers.
- **`threadLocal`** — one produced bean per context, key, and thread; the first resolution on a thread materializes that
  thread's instance, and other threads get their own.
- **`session`** — one produced bean per started scope; materialized from a started scope and torn down when that scope
  stops ([Guide 08 — Sessions](guide-08-sessions.md)).

```cpp
struct [[=ctr::factory{}]] ExternalFactory {
    [[=ctr::singleton{}]]   ExternalLogger   logger()     { return ExternalLogger{"logs/app.log"}; }
    [[=ctr::prototype{}]]   ExternalRequest  request()    { return ExternalRequest{}; }
    [[=ctr::threadLocal{}]] ThreadScratchpad scratchpad() { return ThreadScratchpad{}; }
};
```

```cpp
ctr::Bean<ExternalLogger>   logger  = context.resolve<ExternalLogger>();
ctr::Bean<ExternalRequest>  request = context.resolve<ExternalRequest>();
```

---

## 7. Named producer methods

Several producer methods returning the same type must be distinguished with `named`, otherwise the productions are
ambiguous:

```cpp
struct [[=ctr::factory{}]] ExternalFactory {
    [[=ctr::singleton{}]]
    [[=ctr::named{.name = std::define_static_string("console")}]]
    ExternalLogger consoleLogger() { return ExternalLogger{"console"}; }

    [[=ctr::singleton{}]]
    [[=ctr::named{.name = std::define_static_string("file")}]]
    ExternalLogger fileLogger() { return ExternalLogger{"file"}; }
};
```

```cpp
ctr::Bean<ExternalLogger> console = context.resolve<ExternalLogger>(ctr::named("console"));
ctr::Bean<ExternalLogger> file    = context.resolve<ExternalLogger>(ctr::named("file"));
```

Two *unnamed* producer methods returning the same type are reported as `ctr::ConfigurationError`; the fix is to name the
productions. Ambiguity is not a factory feature — it is a bug wearing a lab coat.

---

## 8. Factory lifecycle

The factory object follows the lifecycle of a singleton bean, so it can carry constructor injection, `postConstruct` and
`preDestroy` hooks ([Guide 06 — Hooks](guide-06-hooks.md)), and be observed by lifecycle
listeners ([Guide 07 — Listeners](guide-07-listeners.md)):

```cpp
struct [[=ctr::factory{}]] AssetFactory {
    explicit AssetFactory(ctr::Bean<AssetConfig> config) : config_(config) {}

    [[=ctr::postConstruct{}]]
    void openIndex(ctr::Bean<Logger> logger) { logger->log("asset factory ready"); }

    [[=ctr::preDestroy{}]]
    void closeIndex(ctr::Bean<Logger> logger) { logger->log("asset factory closing"); }

    ctr::Bean<AssetConfig> config_;
};
```

Those hooks apply to the factory object, not to anything it produces. A factory is a bean; a produced bean is another
bean; keep the two lifecycles separate.

---

## 9. Produced bean lifecycle

A produced bean's lifecycle starts with the producer method, then runs the same listener phases as any other bean: call
the producer method, `onInitialized`, `onCreated`, alive, `onPreDestroy`, `onDestroyed`, C++ destruction. Listeners are
context-wide and shared by all beans; there are no factory-specific listeners.

Hooks on the *produced* type apply only when that type is also a scanned **Ctorium** bean:

```cpp
struct [[=ctr::prototype{}]] ExternalRequest {
    [[=ctr::postConstruct{}]]
    void validate(ctr::Bean<Logger> logger) { logger->log("request validated"); }
};

struct [[=ctr::factory{}]] ExternalFactory {
    [[=ctr::prototype{}]]
    ExternalRequest request() { return ExternalRequest{}; }
};
```

Here the hook on `ExternalRequest` runs only because `ExternalRequest` is itself on the scanned discovery surface. If
the produced type is not scanned, **Ctorium** still has its produced-bean descriptor but does not invent hook metadata —
the same metadata rule as [Guide 02 — Discovery](guide-02-discovery.md).

---

## 10. Full example

A factory produces an external logger from configuration, and a service consumes it like any other dependency:

```cpp
#include <string>
#include <string_view>

#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/Registration.hpp>
#include <ctr/Markers.hpp>

namespace app {
    struct [[=ctr::singleton{}]] AppConfig {
        std::string logPath = "logs/app.log";
    };

    struct ExternalLogger {
        explicit ExternalLogger(std::string path) : path_(std::move(path)) {}
        void log(std::string_view message) { /* write message to path_ */ }
        std::string path_;
    };

    struct [[=ctr::factory{}]] ExternalFactory {
        explicit ExternalFactory(ctr::Bean<AppConfig> config) : config_(config) {}

        [[=ctr::singleton{}]]
        ExternalLogger logger() {
            return ExternalLogger{config_->logPath};
        }

        ctr::Bean<AppConfig> config_;
    };

    struct [[=ctr::singleton{}]] GameService {
        explicit GameService(ctr::Bean<ExternalLogger> logger) : logger_(logger) {}
        void start() { logger_->log("game started"); }
        ctr::Bean<ExternalLogger> logger_;
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

`ExternalLogger` is not annotated and is built with a non-`Bean` argument; `ExternalFactory::logger()` declares how *
*Ctorium** produces it, drawing the path from the injected `AppConfig`; `GameService` receives it like any other bean
dependency.

---

## 11. Mental model

`[[=ctr::factory{}]]` makes a type a singleton bean *and* promotes its annotated methods to bean-producing candidates —
each producer method declares a produced-bean descriptor, on the same footing as an annotated class's constructor, and *
*Ctorium** calls it directly with no extra configuration. Read a factory as: the factory type is a singleton bean; each
marked method is a producer; the producer's lifetime marker controls reuse and destruction; the producer's
`ctr::Bean<U>` parameters are injected dependencies resolved when the product is materialized; the produced type needs
no **Ctorium** annotations; and scanned metadata or hooks for the produced type exist only when that type is itself
scanned. A factory is not a service locator — it is a declared provider whose declarations **Ctorium** can inspect and
turn into descriptors.

---

## 12. Checklist

Before finishing the guide series, check that:

- factories are used to wrap an external/unannotated object as a bean, or to construct a bean through a non-`Bean`
  constructor;
- the factory type carries `[[=ctr::factory{}]]` and is visible to discovery;
- the factory itself is treated as a singleton bean (constructor injection, hooks, listeners all apply to it);
- each producer method carries exactly one lifetime marker (`singleton`, `prototype`, `threadLocal`, or `session`);
- produced types are not required to carry **Ctorium** attributes;
- producer methods return `T` or `std::unique_ptr<T>`;
- producer-method parameters are `ctr::Bean<U>` dependencies (optionally `[[=ctr::named]]`), never caller-supplied
  values;
- several producer methods returning the same type are separated with `named`, and two unnamed ones are treated as a
  configuration error;
- factory hooks are understood as hooks on the factory object, and produced-type hooks only when the produced type is
  scanned.

Next guide: [Guide 10 — Handles](guide-10-handles.md).
