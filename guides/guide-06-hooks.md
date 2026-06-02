# Guide 06 — Hooks

A **hook** is a method **Ctorium** calls during a bean's lifecycle. There are two, declared by attribute:
`[[=ctr::postConstruct{}]]` runs *after* C++ construction, and `[[=ctr::preDestroy{}]]` runs *before* C++ destruction.
They exist for lifecycle work that does not belong in a constructor or destructor — and their defining advantage over a
plain destructor is that, like a constructor, they can receive `ctr::Bean<U>` dependencies from the context. The
constructor builds the object and the destructor tears it down; the hooks are where **Ctorium** politely knocks and
asks, "do you need dependencies for that?"

This guide assumes beans ([Guide 01](guide-01-beans.md)), discovery ([Guide 02](guide-02-discovery.md)), and the context
lifecycle ([Guide 03](guide-03-contexts.md)).

---

## 1. The two hooks

A `postConstruct` hook runs initialization that needs the object to exist as a fully constructed C++ object and may need
managed collaborators — subscribing to a bus, registering with a service, loading resources through an injected
dependency. A `preDestroy` hook runs the matching cleanup before C++ destruction, while the object and its collaborators
are still alive:

```cpp
struct [[=ctr::singleton{}]] MessageConsumer {
    [[=ctr::postConstruct{}]]
    void subscribe(ctr::Bean<MessageBus> bus) {
        token_ = bus->subscribe(listenerId_);
    }

    [[=ctr::preDestroy{}]]
    void unsubscribe(ctr::Bean<MessageBus> bus) {
        bus->unsubscribe(token_);
    }

    int listenerId_ = 7;
    ListenerToken token_;
};
```

`subscribe(...)` runs after the object is constructed; `unsubscribe(...)` runs before it is destroyed. A destructor
would have only the object itself, whereas `unsubscribe` can take the `MessageBus` it needs. Keep the constructor
focused on construction and let the hooks do the context-dependent work — boring constructors age beautifully.

---

## 2. Hook parameters and return values

Hook parameters are dependencies, written as `ctr::Bean<U>` and resolved by the same context that owns the bean when the
hook runs:

```cpp
struct [[=ctr::singleton{}]] DiagnosticsService {
    [[=ctr::postConstruct{}]]
    void start(ctr::Bean<Logger> logger, ctr::Bean<Clock> clock) {
        logger->log("diagnostics started");
        startedAt_ = clock->now();
    }

    TimePoint startedAt_;
};
```

They are not user-supplied arguments — runtime values come from
factories ([Guide 09 — Factories](guide-09-factories.md)). A parameter that is anything other than `ctr::Bean<U>` is
rejected at **compile time**, the same way a non-injectable constructor is: a hook taking a bare `int` does not build.
The hook wanted a bean handle and brought a plain integer; the bouncer noticed before the doors even opened.

A hook's return value is ignored — **Ctorium** never reads it — so prefer `void`, which makes the contract obvious. A
hook performs lifecycle work; it does not report a selection result or a tiny prophecy about the future.

Hook dependencies must be resolvable when the hook runs: if a required one cannot be resolved, the lifecycle operation
that triggered the hook fails with `ctr::ResolutionError`. A hook may depend on other beans, but those dependencies must
be available at that moment — a good reason to keep hooks small. Tiny hooks are friendly; giant hooks tend to become
surprise bootloaders.

---

## 3. One of each, on the bean type

Hooks live on scanned bean types — a type in the context's discovery
surface ([Guide 02 — Discovery](guide-02-discovery.md)) — and the hook metadata comes from that scan:

```cpp
namespace app {
    struct [[=ctr::singleton{}]] AudioSystem {
        [[=ctr::postConstruct{}]]
        void initialize(ctr::Bean<AudioDevice> device) { device_ = device; }
        ctr::Bean<AudioDevice> device_;
    };
}

context.discover<^^app>();
context.start();
```

A bean has **at most one** `postConstruct` and **at most one** `preDestroy`, declared on the bean type itself. **Ctorium
** records a single hook of each kind and calls it once; it does not collect several methods carrying the same
attribute, and it does not gather hooks from base classes. So if ordered initialization steps exist, put them in
sequence inside one hook rather than spreading them across several — there is no defined order between separate hook
methods because only one is used:

```cpp
struct [[=ctr::singleton{}]] OrderedStartup {
    [[=ctr::postConstruct{}]]
    void start(ctr::Bean<Logger> logger, ctr::Bean<ResourceLoader> resources) {
        logger->log("starting");
        resources->load();
    }
};
```

One hook, one explicit sequence, no guessing. The compiler cannot read your dramatic intentions, and neither should the
container.

---

## 4. Lifecycle order

For a scanned bean, hooks interleave with listeners ([Guide 07 — Listeners](guide-07-listeners.md)) in a fixed order:

```text
C++ construction
  → onInitialized listeners
  → postConstruct hook
  → onCreated listeners
  → bean is alive
  → onPreDestroy listeners
  → preDestroy hook
  → onDestroyed listeners
  → C++ destruction
```

The hook-only view is the inner skeleton of that sequence: construct, `postConstruct`, alive, `preDestroy`, destruct.
Reach for a hook when the behavior belongs to the bean itself; reach for a listener when it belongs to an outside
observer of the bean's lifecycle.

---

## 5. Hooks on factories and produced beans

A factory ([Guide 09 — Factories](guide-09-factories.md)) is itself a singleton **Ctorium** bean, so it can carry its
own hooks, which apply to the factory object:

```cpp
struct [[=ctr::factory{}]] AssetFactory {
    explicit AssetFactory(ctr::Bean<AssetConfig> config) : config_(config) {}

    [[=ctr::postConstruct{}]]
    void openIndex(ctr::Bean<Logger> logger) { logger->log("asset factory ready"); }

    [[=ctr::preDestroy{}]]
    void closeIndex(ctr::Bean<Logger> logger) { logger->log("asset factory closing"); }

    [[=ctr::prototype{}]]
    TextureRequest request(std::string path) { return TextureRequest{std::move(path)}; }

    ctr::Bean<AssetConfig> config_;
};
```

A factory *product* is different: hooks declared on the produced type apply only when that type is *also* a scanned *
*Ctorium** bean. A produced type with no lifetime marker has no scanned hooks; the same type declared as a bean (e.g.
`[[=ctr::prototype{}]] TextureRequest` with a `postConstruct`) does, because discovery sees them. The produced type must
be on the scanned surface for **Ctorium** to know its hooks.

---

## 6. Hooks on runtime-bound instances

A runtime-bound instance ([Guide 05 — bindSingleton](guide-05-bindSingleton.md)) is already constructed before it joins
the context, so `postConstruct` is **never** called for it — the context did not build it, and there is nothing to
initialize-after-construction. Its `preDestroy`, however, *can* run at shutdown, but only when **Ctorium** scanned the
type and recorded that hook:

```cpp
namespace app {
    struct [[=ctr::singleton{}]] ImportedLogger {
        [[=ctr::preDestroy{}]]
        void detach(ctr::Bean<LogSink> sink) { sink->flush(); }
    };
}

context.discover<^^app>();      // scans ImportedLogger, including its preDestroy
context.bindSingleton<app::ImportedLogger>(std::make_unique<app::ImportedLogger>());
context.start();
context.stop();                  // detach(...) runs here
```

A plain external type that was never scanned has no hook metadata at all: binding it makes it resolvable, but its
methods are not scanned just because the instance was imported. In short — binding skips `postConstruct`; `preDestroy`
runs for a bound instance only if its hook was scanned; unscanned external types have no hooks. Use discovery when hook
metadata matters, `bindSingleton` when importing the instance matters.

---

## 7. Hooks or listeners?

Use a hook when the behavior is part of the bean — `MessageConsumer` subscribing itself belongs to `MessageConsumer`.
Use a listener when the behavior belongs to an outside observer:

```cpp
context.on<MessageConsumer>(ctr::onCreated,
    [](const ctr::Bean<MessageConsumer>& consumer) {
        // external observer reacts to creation
    });
```

Hooks are self-lifecycle; listeners are external lifecycle observation. Less funny than a goblin joke, but more useful.

---

## 8. Full example

A `MessageConsumer` that subscribes after construction and unsubscribes before destruction, using injected `MessageBus`
and `Logger`:

```cpp
#include <string_view>

#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/Registration.hpp>
#include <ctr/Markers.hpp>

namespace app {
    struct ListenerToken { int value = 0; };

    struct [[=ctr::singleton{}]] Logger {
        void log(std::string_view message) { /* write message */ }
    };

    struct [[=ctr::singleton{}]] MessageBus {
        ListenerToken subscribe(int listenerId) { return ListenerToken{listenerId}; }
        void unsubscribe(ListenerToken token) { /* remove listener */ }
    };

    struct [[=ctr::singleton{}]] MessageConsumer {
        [[=ctr::postConstruct{}]]
        void subscribe(ctr::Bean<MessageBus> bus, ctr::Bean<Logger> logger) {
            token_ = bus->subscribe(listenerId_);
            logger->log("message consumer subscribed");
        }

        [[=ctr::preDestroy{}]]
        void unsubscribe(ctr::Bean<MessageBus> bus, ctr::Bean<Logger> logger) {
            bus->unsubscribe(token_);
            logger->log("message consumer unsubscribed");
        }

        int listenerId_ = 7;
        ListenerToken token_;
    };
}

int main() {
    ctr::BeanContext& context = ctr::BeanContext::resolveContext();

    context.discover<^^app>();
    context.start();

    {
        ctr::Bean<app::MessageConsumer> consumer =
            context.resolve<app::MessageConsumer>();
    }

    context.stop();
}
```

**Ctorium** constructs `MessageConsumer`, then runs `subscribe(...)` in the post-construction phase; at shutdown,
`unsubscribe(...)` runs in the pre-destruction phase. Both receive their dependencies as `ctr::Bean<U>` parameters.

---

## 9. Mental model

When **Ctorium** materializes a scanned bean with hooks: C++ constructs the object, the `postConstruct` hook runs, the
bean is alive, the `preDestroy` hook runs, then C++ destroys it. When writing hooks, hold onto a short list — hooks
belong to the bean type; their parameters are `ctr::Bean<U>` dependencies; their return values are ignored; there is at
most one `postConstruct` and one `preDestroy` per bean, declared on that type (no base-class hooks, no multiple hooks of
one kind); hook metadata comes from scanning; runtime-bound instances skip `postConstruct` and run `preDestroy` only
when it was scanned. The hook is not a constructor and not a listener — it is the bean's own lifecycle appointment with
the container.

---

## 10. Checklist

Before moving on to listeners, check that:

- `postConstruct` is used for initialization after C++ construction, `preDestroy` for cleanup before C++ destruction;
- each hook is declared on a scanned **Ctorium** bean type, with at most one `postConstruct` and one `preDestroy` per
  type;
- ordered steps are placed inside a single hook, since separate hooks have no guaranteed order and only one of each kind
  is used;
- hook parameters are `ctr::Bean<U>` (anything else fails to compile), and return values are not relied on;
- factory hooks are understood to apply to the factory object, and produced-type hooks only when the produced type is
  also scanned;
- runtime-bound instances are not expected to run `postConstruct`, and run `preDestroy` only when that hook was scanned
  for the bound type.

Next guide: [Guide 07 — Listeners](guide-07-listeners.md).
