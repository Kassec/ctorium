# Guide 13 — Performance

This guide reports measured performance and turns it into practical advice. One thing up front: **the numbers below are
benchmark results, not guarantees.** They describe one build on one machine; **Ctorium** does not promise any particular
latency, and your figures will differ with hardware, compiler, and workload. Read them as orders of magnitude and as a
guide to *relative* cost, never as a contract.

This guide assumes lifetimes ([Guide 01 — Beans](guide-01-beans.md)) and handle
forms ([Guide 10 — Handles](guide-10-handles.md)).

---

## 1. The benchmark setup

The figures come from a benchmark run on an **AMD Ryzen 9 7950X3D at 4.1 GHz**. They are averages in nanoseconds per
operation. Treat them as a snapshot: a different CPU, clock, allocator, or compiler will move them, and nothing in the
public contract pins them.

---

## 2. Resolution cost by lifetime

Resolving a bean (`resolve<T>()`) costs roughly:

- **Singleton — ~2.62 ns.** A resolved singleton is essentially a lookup of an already-built instance; this is the
  cheapest case.
- **Prototype (empty) — ~27.97 ns**, *including* the `operator new` for the fresh object. Each resolution builds a new
  instance, so allocation dominates.
- **Prototype with 8 injected dependencies — ~56.34 ns.** Each injected `ctr::Bean<U>` parameter must itself be
  resolved, so dependency count adds real cost on the prototype path.
- **Session — ~5.33 ns** and **thread-local — ~5.21 ns** to resolve, between the singleton and prototype ends.

The shape to remember: singletons are near-free to resolve, prototypes pay for allocation plus their dependency graph,
and session/thread-local sit in between. Resolving a prototype with a deep dependency graph in a hot loop is the case to
watch.

---

## 3. Access cost: proxy handles are slower

Resolution is one cost; *accessing* the object through the handle afterward is another. Here the handle form
from [Guide 10 — Handles](guide-10-handles.md) shows through:

- **Session access — ~1.89 ns** per dereference.
- **Thread-local access — ~1.49 ns** per dereference.

A Form 1 (direct) singleton or prototype handle returns its pointer immediately, with no registry interaction, so its
access is cheaper still. Session and thread-local handles are **proxies**: each `operator->` re-resolves the current
instance, which is what those few nanoseconds buy. The advice follows directly — when you dereference a session or
thread-local bean repeatedly in a tight loop, resolve once into a local pointer instead of going through the proxy on
every iteration:

```cpp
auto* doc = sessionHandle.operator->();   // resolve the proxy once (pointer; may be null)
if (doc != nullptr) {
    for (int i = 0; i < n; ++i) {
        doc->step(i);                          // direct access, no re-resolution
    }
}
```

(`value()` returns a reference and assumes a non-null target, so the pointer form above is what lets you check for the
Form 2 `nullptr` case before the loop.)

This optimization is only safe while the resolved instance is **guaranteed stable** for the whole loop. For a session
bean, that means the scope must not `stop()` or `restart()` while the cached pointer is in use — either would destroy
the instance behind the pointer, leaving it dangling. For a thread-local bean, the loop must run entirely on the thread
that resolved the pointer, since the instance is that thread's (see [Guide 10 — Handles](guide-10-handles.md)). If you
cannot guarantee that stability, keep going through the handle and pay the per-access cost — a dangling pointer is a far
worse trade than a few nanoseconds.

---

## 4. Handle copy and move cost

Handles are passed around constantly, so their copy/move cost matters in hot code. Measured:

- **Singleton copy — ~0.53 ns**, **singleton move — ~0.93 ns.** A singleton handle is a cheap struct copy with no atomic
  operations.
- **Prototype copy — ~2.30 ns**, **prototype move — ~0.85 ns.** Prototype *copy* carries an atomic reference-count
  increment (and destruction a decrement), which is the extra cost over a singleton copy; move just transfers, so it is
  as cheap as a singleton move.

So copying a singleton handle is almost free, while copying a prototype handle pays for one atomic operation — relevant
if you copy prototype handles per element in a large container. Moving is cheap in both cases. Where you would otherwise
copy a prototype handle in a loop, prefer moving or referencing it.

---

## 5. Practical guidance

The numbers point to a few habits, none of which require contorting your design:

Singletons are the cheap default for both resolution and handle copying — use them freely. Prototypes cost an allocation
per resolution and an atomic per handle copy, so resolve them outside hot loops where you can and avoid gratuitous
handle copies. Session and thread-local beans resolve cheaply but re-resolve on every access; cache the dereferenced
pointer for a tight loop rather than going through the proxy each time. And injected dependency count shows up on the
prototype path, so a prototype with a large graph resolved per-frame is worth a second look.

None of this is a correctness rule — it is cost-shaping. Measure your own workload before optimizing; the figures here
only tell you *where* the cost tends to sit. Optimizing a path the profiler never visits is just rearranging deck chairs
with excellent discipline.

---

## 6. Mental model

On the measured machine: singleton resolution is ~2.6 ns and its handle copy ~0.5 ns (no atomics); prototype resolution
is ~28 ns and up (allocation, plus ~56 ns with 8 deps) and its handle copy ~2.3 ns (one atomic); session/thread-local
resolve in the ~5 ns range and *access* in the ~1.5–1.9 ns range because their proxy handles re-resolve per dereference.
Cheap to dear: singleton, then session/thread-local, then prototype. These are benchmark observations, not guarantees —
measure your own build.

---

## 7. Checklist

When performance matters, check that:

- singletons are preferred where a single shared instance fits — they are cheapest to resolve and to copy;
- prototypes are resolved outside hot loops where possible (allocation per resolution, atomic per handle copy);
- session/thread-local beans are dereferenced once into a local pointer for tight loops, respecting the Form 2 `nullptr`
  rule;
- prototype handles are moved or referenced rather than copied in hot paths;
- prototype dependency-graph depth is considered on per-frame resolution paths;
- the reported figures are treated as benchmark results on specific hardware, not guarantees, and the real workload is
  measured before optimizing.

Next guide: [Guide 14 — Compilation](guide-14-compilation.md).
