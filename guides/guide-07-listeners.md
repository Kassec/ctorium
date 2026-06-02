# Guide 07 — Listeners

A **listener** is a callback registered on a context to observe bean lifecycle events. Where a
hook ([Guide 06 — Hooks](guide-06-hooks.md)) is the bean doing its *own* lifecycle work, a listener is another system
saying "tell me when beans matching this registration reach this phase." Same timeline, different responsibility — and a
good deal cheaper than arguing with your future self.

This guide builds on beans and their handles ([Guide 01](guide-01-beans.md)), the context
lifecycle ([Guide 03](guide-03-contexts.md)), and qualifiers ([Guide 04](guide-04-qualifiers.md)).

---

## 1. The four phases

A **listener phase** is a named point in the bean lifecycle, and **Ctorium** defines four. They interleave with hooks in
this fixed order:

```text
C++ construction
  → onInitialized   (after construction, before postConstruct)
  → postConstruct hook
  → onCreated       (after postConstruct)
  → bean is alive
  → onPreDestroy    (before preDestroy)
  → preDestroy hook
  → onDestroyed     (after preDestroy, before C++ destruction)
  → C++ destruction
```

This guide covers the four listener phases; the two hook phases belong to [Guide 06 — Hooks](guide-06-hooks.md).

For bound objects (`bindSingleton`, `bindSession`), the order is the same with two differences: `postConstruct` is absent (the object is already constructed), and `preDestroy` fires only when the bound type is a Ctorium type:

```text
C++ construction (by user)
  → onInitialized
  → onCreated
  → bean is alive
  → onPreDestroy
  → preDestroy hook (Ctorium types only)
  → onDestroyed
  → C++ destruction (by Ctorium)
```

---

## 2. Typed listeners

A **typed listener** observes beans compatible with one requested type. `context.on<T>(phase, callback)` registers it,
and the callback receives the bean as a `const ctr::Bean<T>&`:

```cpp
ctr::ListenerHandle handle = context.on<Worker>(ctr::onCreated,
    [](const ctr::Bean<Worker>& worker) {
        worker->ready = true;
    });
```

The handle is passed by `const&`. Copying it inside the callback increases tracking, so copy only when you really mean
to keep the bean beyond the callback — the callback is not a souvenir shop, so do not take handles home by accident.

A typed listener filters by **compatibility**: a listener on a base type observes beans of compatible derived types.
When the callback must react only to one concrete type, do the exact filtering inside it with the cast helpers
from [Guide 01 — Beans](guide-01-beans.md):

```cpp
context.on<KeyboardService>(ctr::onCreated,
    [](const ctr::Bean<KeyboardService>& bean) {
        if (auto concrete = bean.tryCast<ConcreteKeyboardService>()) {
            (*concrete)->enableKeyboardCapture();
        }
    });
```

The rule: `on<T>(...)` filters by compatibility with `T`; exact filtering belongs inside the callback.

---

## 3. Global listeners

A **global listener** observes every bean in the context. `context.on(phase, callback)` registers it, and the callback
receives a `const ctr::AnyBean&` — the tracked, type-erased handle:

```cpp
context.on(ctr::onCreated,
    [](const ctr::AnyBean& bean) {
        if (bean.compatible<RenderPass>()) {
            bean.cast<RenderPass>()->prepare();
        }
    });
```

`AnyBean` offers the same `compatible<T>()`, `exact<T>()`, `cast<T>()`, and `tryCast<T>()` helpers as `ctr::Bean<T>`, so
a global listener can narrow to a type when it needs to. Use a typed listener when you already know the observed type;
reach for a global one only when the callback is genuinely cross-cutting. A global listener is a net — do not use a net
when a fishing rod is enough.

---

## 4. Before or after `start()`

A listener registered **before** `start()` observes future transitions, including those caused by `start()` itself — so
if `start()` materializes eager singletons, a pre-start listener sees their events. Register before `start()` when
startup events matter:

```cpp
context.discover<^^app>();
context.on<Service>(ctr::onCreated,
    [](const ctr::Bean<Service>& service) { service->ready = true; });
context.start();
```

A listener registered **after** `start()` observes only future transitions; past events are not replayed. This is the
shape for tools, debug overlays, plugin systems, and temporary observers. No time travel, no replay theater — just
future events.

---

## 5. Priority and dispatch order

`ctr::ListenerOptions` configures a registration; its option is `priority` (default `0`), where higher runs first:

```cpp
context.on<Worker>(ctr::onCreated,
    [](const ctr::Bean<Worker>& worker) { worker->ready = true; },
    ctr::ListenerOptions{.priority = 10});
```

For one event, **Ctorium** dispatches in priority order, then in registration order at equal priority. Typed and global
listeners share that single order, so a typed listener at priority `10` runs before a global one at priority `0` for the
same bean event. Priority is for ordering *independent* listeners; if two pieces of work must always run in one strict
sequence, put them in a single callback — priority is not a substitute for readable control flow.

---

## 6. Handles and removal

`context.on(...)` returns a `ctr::ListenerHandle` identifying the registration. The handle is **not** RAII: destroying
it does not remove the listener. Removal is explicit and idempotent — calling it twice is harmless:

```cpp
ctr::ListenerHandle handle = context.on<Service>(ctr::onCreated,
    [](const ctr::Bean<Service>& service) { service->ready = true; });

handle.remove();             // or: context.remove(handle);
```

Listener lifetime is deliberate context configuration, not a disappearing magic trick. The listener does not vanish just
because the handle object left scope.

---

## 7. Snapshot dispatch

Dispatch works from a **snapshot**: the set of listeners for an event is selected before any callback runs. So a
listener may remove itself, or add another listener, during its own callback — but those changes apply to the *next*
event, not the current pass:

```cpp
ctr::ListenerHandle handle;
handle = context.on<Service>(ctr::onCreated,
    [&](const ctr::Bean<Service>& service) {
        handle.remove();     // takes effect on the next event
    });
```

Snapshot dispatch keeps callback execution predictable and stops the listener list from turning into a live-action
spreadsheet.

---

## 8. Callback signatures

The supported signatures are exactly one argument each: a typed listener takes `const ctr::Bean<T>&`, a global listener
takes `const ctr::AnyBean&`. A callback with any other shape is rejected at **compile time** — `on<T>` and `on`
static-assert on the callback, so a `[](int)` listener simply does not build:

```cpp
context.on<Worker>(ctr::onCreated, [](int value) {});   // does not compile
```

The container asked for a bean callback; the integer arrived with confidence, and confidence was not enough.

---

## 9. Reaching the context from a callback

The context is not handed to callbacks; retrieve it from the bean, which works the same for both handle types:

```cpp
context.on<Service>(ctr::onCreated,
    [](const ctr::Bean<Service>& service) {
        ctr::BeanContext& owner = service.context();
    });

context.on(ctr::onCreated,
    [](const ctr::AnyBean& bean) {
        ctr::BeanContext& owner = bean.context();
    });
```

Keep that access deliberate: a listener holding the context can register more listeners, resolve beans, or change
runtime configuration ([Guide 03 — Contexts](guide-03-contexts.md)). Useful power — also enough rope to knit a sweater
around your architecture.

---

## 10. Listeners vs hooks

The dividing line is ownership of the behavior. A bean subscribing *itself* to a bus is a `postConstruct` hook — it
belongs to the bean. An outside system reacting to that bean's creation is a listener — it does not belong inside the
bean:

```cpp
context.on<MessageConsumer>(ctr::onCreated,
    [](const ctr::Bean<MessageConsumer>& consumer) {
        // external system reacts to creation
    });
```

Keep self-lifecycle in hooks and observation in listeners; the bean stays easier to read and the observer easier to
remove.

---

## 11. Runtime-bound instances

A runtime-bound instance ([Guide 05 — bindSingleton](guide-05-bindSingleton.md)) takes part in listener dispatch as a
managed runtime bean, so listeners observe it like any other. But it was constructed *before* it joined the context, so
its lifecycle events — `onInitialized` included — are observation of context participation, not proof that **Ctorium**
ran the constructor. Treat the callbacks accordingly; the reason a bound instance does not run `postConstruct` is
in [Guide 06 — Hooks](guide-06-hooks.md).

---

## 12. Thread-safety and exceptions

Listener registration and removal are thread-safe, and dispatch does not hold a long internal lock across callback
execution — running user code under an engine lock would make reentrancy and blocking hard to reason about. Snapshot
dispatch (§7) and explicit removal (§6) keep behavior predictable. Thread-safety of the sibling runtime contributions
lives with them — default named selection in [Guide 04 — Qualifiers](guide-04-qualifiers.md), runtime instance binding
in [Guide 05 — bindSingleton](guide-05-bindSingleton.md).

Callbacks are user code, so exceptions they throw propagate unchanged — a callback's `throw` is *not* wrapped in a *
*Ctorium** exception. **Ctorium** exceptions are reserved for engine errors. The distinction is worth keeping straight:
a throwing callback is a user-code failure, whereas an unsupported callback signature is caught earlier, at compile
time (§8).

---

## 13. Full example

A typed listener for `Worker` (priority `10`), a global listener for every created bean, and explicit removal:

```cpp
#include <ctr/AnyBean.hpp>
#include <ctr/Bean.hpp>
#include <ctr/BeanContext.hpp>
#include <ctr/Registration.hpp>
#include <ctr/ListenerHandle.hpp>
#include <ctr/Markers.hpp>

namespace app {
    struct [[=ctr::singleton{}]] Worker {
        bool ready = false;
    };

    struct [[=ctr::singleton{}]] Scheduler {
        explicit Scheduler(ctr::Bean<Worker> worker) : worker_(worker) {}
        ctr::Bean<Worker> worker_;
    };
}

int main() {
    ctr::BeanContext& context = ctr::BeanContext::resolveContext();

    context.discover<^^app>();

    ctr::ListenerHandle workerListener = context.on<app::Worker>(
        ctr::onCreated,
        [](const ctr::Bean<app::Worker>& worker) { worker->ready = true; },
        ctr::ListenerOptions{.priority = 10});

    ctr::ListenerHandle globalListener = context.on(
        ctr::onCreated,
        [](const ctr::AnyBean& bean) { /* observe every created bean */ });

    context.start();

    {
        ctr::Bean<app::Scheduler> scheduler = context.resolve<app::Scheduler>();
    }

    workerListener.remove();
    globalListener.remove();
    context.stop();
}
```

The typed listener (priority `10`) runs before the global one; `workerListener.remove()` removes it explicitly —
destroying the handle alone would not.

---

## 14. Mental model

`context.on<T>(phase, callback, options)` reads as: the context creates one registration observing one phase;
`on<T>(...)` filters beans compatible with `T`, while `on(...)` observes all beans; priority orders callbacks for
matching events; the callback receives a tracked handle (`ctr::Bean<T>` or `ctr::AnyBean`) by `const&`; removal is
explicit via `handle.remove()` or `context.remove(handle)`; and dispatch uses a snapshot, so additions and removals
during a callback affect only future events. A listener is not a hook and not a dependency — it is a registered observer
of lifecycle events.

---

## 15. Checklist

Before moving on to sessions, check that:

- `on<T>(...)` is used for type-specific observation, `on(...)` only for genuinely global observation;
- the right phase is chosen: `onInitialized` (after construction, before `postConstruct`), `onCreated` (after
  `postConstruct`), `onPreDestroy` (before `preDestroy`), `onDestroyed` (after `preDestroy`, before C++ destruction);
- exact filtering is done inside the callback when compatibility is too broad;
- listener priority is used only when ordering between independent listeners matters;
- listener removal is explicit, and destroying a `ListenerHandle` is not expected to remove the listener;
- callbacks take exactly `const ctr::Bean<T>&` or `const ctr::AnyBean&` (other shapes fail to compile);
- callbacks do not assume the context is passed automatically;
- snapshot-dispatch behavior is expected when listeners are added or removed during callbacks.

Next guide: [Guide 08 — Sessions](guide-08-sessions.md).
