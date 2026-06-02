# Guide 17 — API contract

What the **Ctorium** public API promises, what it deliberately does not promise, and how those boundaries are drawn. This is the guide to read when evaluating whether a change is breaking, whether a behavior is guaranteed, or whether an observed detail is safe to rely on.

---

## 1. Three surfaces

Every public element lives on one of three independent surfaces:

**Usage surface — kept maximal.** Public APIs stay permissive and preserve caller intent. A boundary value that can be safely adjusted without changing semantics is normalized, not rejected. No avoidable error is forced on the user. An API is never restricted to prevent hypothetical misuse.

**Diagnostic surface — locked.** Every invalid input, ambiguous request, unsupported operation, failed precondition, or resource failure produces an explicit typed Ctorium error at the earliest boundary where the information exists — never a silent fallback, silent no-op, or hidden downgrade.

**Commitment surface — kept minimal.** Anything not exposed stays free to evolve. The less the API promises, the more room it has to improve without breaking callers.

Decision test for any public element: does it let the caller do more without forcing an avoidable error (keep it); does it leave an invalid case silent (add an explicit typed error); does it promise an internal detail (remove it from the public surface). When freedom and evolution conflict on the same element, evolution wins by withdrawing the guarantee, not by forbidding the act.

---

## 2. Stability guarantee

Within a major version, the public contract is preserved: existing valid usage keeps compiling and keeps meaning the same thing. Any incompatible change is deferred to an explicit major version.

---

## 3. What the API does not promise

The public API never guarantees:

- concrete types behind handles or internal identifiers,
- handle layout, size, or field order,
- iteration or dispatch order beyond what is documented (descending priority, then registration order for listeners),
- in-memory layout or addresses of managed instances,
- ownership of a context: `resolveContext()` and `ctr::Bean<T>::context()` hand back a non-owning `BeanContext&`, never an owning handle, and a context's lifetime is never extended to outlive its `stop()` to match a caller,
- diagnostic message text (messages identify the type, key, and operation involved, but their exact wording may change),
- timing of lazy materialization beyond "on first resolution."

An observed behavior not listed in the contract is an implementation detail, not a guarantee.

---

## 4. Checklist

When evaluating a change against the contract:

- does it break existing valid usage that compiles and means the same thing today?
- does it silence a case that currently produces an explicit error?
- does it expose an internal detail that the API currently does not promise?
- does it restrict a usage that is currently permitted without forcing an avoidable error?

A "yes" to any of these is a breaking change and belongs in a major version.

Previous guide: [Guide 16 — Build macros](guide-16-macros.md).
