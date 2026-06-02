# Guide 04 — Qualifiers

A **qualifier** is selection information used to choose one bean when several are compatible with the requested type.
Two mechanisms do the work, and they answer different questions. A **named key** answers *which named space should
resolution draw from?* A **priority** answers *if several compatible beans live in that space, which one wins?* The name
picks the room; priority picks the chair in it. Tiny difference, large consequences — as usual.

This guide assumes the context and resolution model from [Guide 03 — Contexts](guide-03-contexts.md), and the
descriptors that discovery produces from [Guide 02 — Discovery](guide-02-discovery.md).

---

## 1. The default (unnamed) space

A **resolution space** is a group of candidates considered together; a **candidate** is a bean descriptor compatible
with the requested type. By default a bean carries no name and so belongs to the **unnamed** space, and a request with
no explicit key resolves from that space — unless a runtime default has been configured (§5):

```cpp
struct Logger {
    virtual ~Logger() = default;
    virtual void log(std::string_view message) = 0;
};

struct [[=ctr::singleton{}]] ConsoleLogger : Logger {
    void log(std::string_view message) override {}
};

ctr::Bean<Logger> logger = context.resolve<Logger>();   // unnamed space
```

When several candidates share that space, priority (§6) decides between them.

---

## 2. Named keys on beans

A bean can declare a named key with `[[=ctr::named{...}]]`, which places it in a named space instead of the unnamed one:

```cpp
struct [[=ctr::singleton{}]]
       [[=ctr::named{.name = std::define_static_string("console")}]]
ConsoleLogger : Logger {
    void log(std::string_view message) override {}
};

struct [[=ctr::singleton{}]]
       [[=ctr::named{.name = std::define_static_string("file")}]]
FileLogger : Logger {
    void log(std::string_view message) override {}
};
```

The unnamed space and each named space are separate, so a request for `"console"` will never pick `"file"`. Logs should
not teleport into random files unless you are debugging a cursed build.

---

## 3. Selecting a name at resolution: `ctr::named(...)`

`ctr::named("x")` is the runtime selector. Passed as the first argument to `resolve<T>(...)`, it chooses the named space
to resolve from:

```cpp
ctr::Bean<Logger> console = context.resolve<Logger>(ctr::named("console"));
ctr::Bean<Logger> file    = context.resolve<Logger>(ctr::named("file"));
```

That first-position `ctr::named(...)` is a *resolution key*, not a user parameter — user parameters for factory-backed
resolution are a different thing, covered in [Guide 09 — Factories](guide-09-factories.md). Pass no selector for the
unnamed space; pass a non-empty name for a named one. An empty runtime key (`ctr::named("")`) is invalid.

---

## 4. Named injection points

A constructor parameter can request a specific named key by carrying the same annotation, which makes the dependency
resolve as if you had written `resolve<Logger>(ctr::named("file"))`:

```cpp
struct [[=ctr::singleton{}]] ReportService {
    explicit ReportService(
        [[=ctr::named{.name = std::define_static_string("file")}]]
        ctr::Bean<Logger> logger)
        : logger_(logger) {}

    ctr::Bean<Logger> logger_;
};
```

The key belongs to the injection point and is explicit, so it keeps using `"file"` even if a runtime default for
`Logger` exists. That is the general rule, and it is worth stating directly: an explicit name always wins over the
runtime default. A top-level `resolve<Logger>(ctr::named("file"))` ignores a `"console"` default in exactly the same
way. Explicit means explicit; the container politely stays in its lane.

---

## 5. The runtime default named selection

When most of an application should use one named implementation, you can set a default so that *unqualified* resolutions
follow it:

```cpp
context.defaultNamed<Logger>("console");

ctr::Bean<Logger> logger = context.resolve<Logger>();   // resolves from "console"
```

One timing rule matters: `defaultNamed<T>(...)` operates on the type's runtime `TypeId`, which only exists once the
context is started, so **it must be called after `start()`** — calling it earlier raises `ctr::ContextStateError`. It is
a runtime contribution, not a created-state setup step. The default is stored per context and per requested type, and it
affects only *future* resolutions:

```cpp
context.start();

context.defaultNamed<Logger>("console");
ctr::Bean<Logger> a = context.resolve<Logger>();   // "console"

context.defaultNamed<Logger>("file");
ctr::Bean<Logger> b = context.resolve<Logger>();   // "file"
```

A bean already materialized keeps the dependencies it received: if a `GameService` was built while the default was
`"console"`, switching the default to `"file"` afterwards does not rewire that existing `GameService` — only later
materializations see the new default. No invisible rewiring; that is how containers become haunted basements again.

Because an unnamed *injection point* (a `ctr::Bean<U>` parameter with no `[[=ctr::named]]`) also resolves through the
unnamed-key path, it follows the runtime default too. That is exactly why the explicit-name rule in §4 is worth stating:
a named injection point opts out of the default; an unnamed one opts in.

### Removing the default

Clear the default by passing `nullptr`; an unqualified resolution then returns to the unnamed space:

```cpp
context.defaultNamed<Logger>(nullptr);
ctr::Bean<Logger> unnamed = context.resolve<Logger>();
```

### When the default points nowhere

`defaultNamed<T>("x")` only records the selection; whether a bean actually answers to `"x"` is checked when a resolution
uses it. A default aimed at a missing bean therefore fails at the *resolution*, with `ctr::ResolutionError`, not at the
configuration call:

```cpp
context.defaultNamed<Logger>("missing");
ctr::Bean<Logger> logger = context.resolve<Logger>();   // ctr::ResolutionError here
```

This keeps setup forgiving while tying the failure to the request that actually needs the bean.

---

## 6. Priority

A **priority** is the number that chooses one candidate among several compatible ones in the selected space. It rides on
the lifetime marker:

```cpp
struct [[=ctr::singleton{.priority = 10}]] ConsoleLogger : Logger {
    void log(std::string_view message) override {}
};

struct [[=ctr::singleton{.priority = 1}]] SilentLogger : Logger {
    void log(std::string_view message) override {}
};

ctr::Bean<Logger> logger = context.resolve<Logger>();   // ConsoleLogger wins (10 > 1)
```

Arbitration is per space. Named spaces and the unnamed space are each sorted independently, and priority never merges
them: a high-priority `"file"` bean is simply not a candidate for a `"console"` request. Runtime bindings can also carry
a priority through their binding options — see [Guide 05 — bindSingleton](guide-05-bindSingleton.md).

---

## 7. Priority ambiguity

When the two best candidates in the selected space share the same highest priority, **Ctorium** does not guess — it
raises `ctr::ResolutionError`:

```cpp
struct [[=ctr::singleton{.priority = 10}]] ConsoleLogger : Logger { /* ... */ };
struct [[=ctr::singleton{.priority = 10}]] FileLogger    : Logger { /* ... */ };

ctr::Bean<Logger> logger = context.resolve<Logger>();   // two best candidates → error
```

Guessing is fun in party games, less so in dependency graphs. Resolve it either by breaking the tie (give one a higher
priority) or by separating the candidates into named spaces and asking for one explicitly with `ctr::named("...")`.

---

## 8. Thread-safety of the runtime default

`defaultNamed<T>(...)` is safe to call concurrently with resolution. If one thread changes the default for a type while
another resolves it, the resolving thread observes a clean snapshot — either the old default or the new one, never a
half-written value. Thread-safety of the other runtime contributions is covered where they live: runtime bindings
in [Guide 05 — bindSingleton](guide-05-bindSingleton.md) and listeners in [Guide 07 — Listeners](guide-07-listeners.md).

---

## 9. Full example

Two named loggers, a runtime default set *after* `start()`, a service that injects an unnamed `Logger` (and so follows
the default), and one explicit named resolution:

```cpp
#include <string_view>

#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/Registration.hpp>
#include <ctr/Markers.hpp>

namespace app {
    struct Logger {
        virtual ~Logger() = default;
        virtual void log(std::string_view message) = 0;
    };

    struct [[=ctr::singleton{.priority = 10}]]
           [[=ctr::named{.name = std::define_static_string("console")}]]
    ConsoleLogger : Logger {
        void log(std::string_view message) override {}
    };

    struct [[=ctr::singleton{}]]
           [[=ctr::named{.name = std::define_static_string("file")}]]
    FileLogger : Logger {
        void log(std::string_view message) override {}
    };

    struct [[=ctr::singleton{}]] GameService {
        explicit GameService(ctr::Bean<Logger> logger)
            : logger_(logger) {}

        void start() { logger_->log("game started"); }

        ctr::Bean<Logger> logger_;
    };
}

int main() {
    ctr::BeanContext& context = ctr::BeanContext::resolveContext();

    context.discover<^^app>();
    context.start();

    context.defaultNamed<app::Logger>("console");   // after start(): TypeId now exists

    {
        ctr::Bean<app::GameService> game = context.resolve<app::GameService>();
        game->start();   // its injected Logger followed the "console" default

        ctr::Bean<app::Logger> fileLogger =
            context.resolve<app::Logger>(ctr::named("file"));
        fileLogger->log("written through the explicit file logger");
    }

    context.stop();
}
```

`GameService` requests an unnamed `ctr::Bean<Logger>`, so the runtime default routes that dependency to `"console"`; the
final resolution names `"file"` explicitly and bypasses the default. Note the ordering: `defaultNamed` comes after
`start()`, and `GameService` (a lazy singleton) is materialized later still, so it observes the default that was set.

---

## 10. Mental model

Resolving one bean follows a fixed path. Begin with the requested type; choose the resolution space in priority order —
an explicit `ctr::named("x")`, then a named injection point, then the runtime default, otherwise the unnamed space; find
the candidates compatible with the type in that space; take the highest-priority one. If the chosen space has no
candidate, or two candidates tie for the highest priority, the result is `ctr::ResolutionError`. The name decides
*where* to look; priority decides *which* compatible candidate wins there.

---

## 11. Checklist

Before moving on to runtime instance binding, check that:

- unnamed beans are used when one default implementation suffices, named keys when several must be selected explicitly;
- a named injection point is used for a dependency that needs one precise key;
- `defaultNamed<T>(...)` is called only after `start()`, and only when unqualified resolutions of `T` should follow a
  runtime default;
- a changed default is expected to affect future resolutions only;
- explicit `ctr::named("x")` is used when a request must ignore the runtime default;
- priority is used only to choose among compatible candidates in the same space;
- an equal highest priority within one space is treated as an error to fix.

Next guide: [Guide 05 — bindSingleton](guide-05-bindSingleton.md).
