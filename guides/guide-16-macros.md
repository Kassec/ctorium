# Guide 16 — Build macros

**Ctorium** exposes a small set of compile-time macros that tune runtime behavior without changing the public API. All macros must be defined consistently across every translation unit that includes a Ctorium header — define them before the first Ctorium include.

This guide assumes contexts ([Guide 03](guide-03-contexts.md)) and lifetimes ([Guide 01](guide-01-beans.md)).

---

## 1. `CTORIUM_NAMESPACE`

Sets the namespace under which the entire Ctorium API (public and internal) is declared. Defaults to `ctr`.

```cpp
#define CTORIUM_NAMESPACE mylib
#include <ctr/Ctorium.hpp>

mylib::BeanContext& ctx = mylib::BeanContext::resolveContext();
```

When not defined, all types live under `ctr` as shown throughout the guides. Redefining the namespace is useful when embedding Ctorium inside another library that must not expose `ctr` to its own callers, or when two versions of Ctorium coexist in the same process.

The macro must be defined **before** the first Ctorium include, and consistently across every translation unit.

---

## 2. `CTORIUM_PROTOTYPE_MAX_SLOTS`

Caps the number of concurrent prototype bean instances per root context. When defined as a positive integer constant, the prototype store enforces this limit at allocation time: attempting to create a slot beyond the value raises `ctr::ResolutionError` with a message identifying the limit.

```cpp
#define CTORIUM_PROTOTYPE_MAX_SLOTS 65536
#include <ctr/Ctorium.hpp>
```

When not defined, the effective upper bound is the range of the internal slot type (`uint32_t`, approximately 4.29 × 10⁹) — effectively unlimited on any real workload.

The cap applies to **live** slots, not cumulative allocations: a prototype released (all handles destroyed) frees its slot for reuse. The limit therefore bounds memory at any point in time, not over the lifetime of the process.

Typical use: embedded systems or servers where runaway prototype creation would exhaust memory before the OS kills the process. Set it to the highest count your workload legitimately needs, with margin; the macro is a safety net, not a tuning knob.

```cpp
// Test configuration — deliberately low to verify the error path
// (used in ctorium-tests with CTORIUM_PROTOTYPE_MAX_SLOTS=4)
```

---

## 3. Mental model

Two macros: `CTORIUM_NAMESPACE` renames the library namespace (default `ctr`) for embedding or co-existence; `CTORIUM_PROTOTYPE_MAX_SLOTS` bounds live prototype slots and raises `ResolutionError` when exceeded. Define them before the first Ctorium include, consistently across all TUs.

---

## 4. Checklist

- macros are defined before the first `#include <ctr/...>`;
- macros are defined consistently across all translation units (mismatched definitions are ODR violations);
- `CTORIUM_NAMESPACE` is left undefined unless the default `ctr` conflicts with another library in the same binary;
- `CTORIUM_PROTOTYPE_MAX_SLOTS` is set to a value above the peak concurrent prototype count of the workload, with margin.

Previous guide: [Guide 15 — API Reference](guide-15-api-reference.md). Next guide: [Guide 17 — API contract](guide-17-api-contract.md).
