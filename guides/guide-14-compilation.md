# Guide 14 — Compilation

**Ctorium** is built on C++26 reflection (P2996 and related papers). That foundation is its power and, today, its main
constraint: the facility is new and not yet widely supported. This guide states what the library needs from a toolchain
as of **June 2026**, which compiler is known to work, the build flags involved, and how to check a toolchain before
committing to it.

This guide assumes the include rules from [Guide 11 — Headers](guide-11-headers.md).

---

## 1. The state of C++26 reflection support (June 2026)

As of June 2026, compiler support for C++26 reflection is **not broad**. The feature set **Ctorium** depends on — `^^`
reflection, `[: :]` splicers, `template for`, the `std::meta::*` query functions, `std::define_static_string` /
`std::define_static_array`, and `std::meta::info` as a non-type template parameter — is only beginning to appear in
mainstream toolchains. Notably, **MSVC is still completing its C++23 rollout** and does not yet offer this C++26
reflection surface, so it is not currently an option for building **Ctorium**.

Plan accordingly: this is a leading-edge dependency. Treat any toolchain's reflection support as young, and verify it (§3) rather than assuming it. Living on the frontier has its rewards; a stable build is not yet reliably one of them.

---

## 2. Known-good toolchain: GCC 16.1.0

The verified working configuration is **GCC 16.1.0 with the experimental library**. Reflection landed in the GCC 16 line
via the experimental implementation and is what **Ctorium** is developed and tested against. Compile against it with:

```
-std=c++26 -freflection -lstdc++exp
```

`-freflection` enables the reflection facility, and `-lstdc++exp` links the experimental standard-library component the
reflection support depends on. This is the baseline; treat its edges as new, since the implementation is recent and
still settling.

A note on **sanitizers** on this toolchain, for when you validate runtime behaviour: on the Windows GCC 16.1.0 build (
MinGW-w64, MSYS2 UCRT64), **AddressSanitizer and ThreadSanitizer are not available** — GCC's sanitizer runtimes are not
built for the mingw-w64 target, and ThreadSanitizer has no Windows port under any compiler. Trap-mode UBSan (
`-fsanitize-trap=undefined`) does work there. Memory- and data-race validation therefore needs a Linux toolchain (in CI,
for example), not the Windows build.

---

## 3. Verify a toolchain before relying on it

Because support is young and uneven, do not assume a compiler works — **check it.** **Ctorium** ships a toolchain checking tool for exactly this: you run it against a candidate toolchain, and it produces a detailed report, topic by topic, of which reflection capabilities the toolchain supports and which it does not. It is a diagnostic you read, not a build that simply breaks — so it tells you *what* is missing rather than only *that* something is. Run it before adopting a toolchain, and again after any compiler update.

The principle behind the probe is worth adopting in your own code too: every claim of the form "this toolchain cannot do
X" should map to a probe that reproduces X, rather than to an assumption. A capability merely *assumed* unavailable,
with no failing probe, is not a reason to avoid it — the library's own history includes a "the compiler can't extract
this" belief that turned out to be false once probed.

---

## 4. One authoring rule that is a hard compile requirement

One **Ctorium** usage rule is enforced by the compiler itself, so it belongs here. A string value carried by an
annotation **must** be wrapped in `std::define_static_string`:

```cpp
// Required — compiles:
struct [[=ctr::named{.name = std::define_static_string("db")}]] DbService {};

// Ill-formed on GCC 16.1.0 — the raw literal fails at compile time:
struct [[=ctr::named{.name = "db"}]] DbService2 {};
```

The reason is a language rule, not a quirk: an annotation's value must be *template-argument-equivalent*, and a
`const char*` pointing at a raw string literal is not. `define_static_string` promotes the string into static storage
that qualifies. A null name (a default-initialized `[[=ctr::scoped{}]]`, for instance) needs no wrapping. Every guide
that shows a named or scoped annotation uses `define_static_string` for this reason — it is not stylistic, it is
required to compile.

---

## 5. Mental model

**Ctorium** rides C++26 reflection, which in June 2026 is leading-edge and narrowly supported — MSVC is not yet an
option. The known-good build is GCC 16.1.0 with `-std=c++26 -freflection -lstdc++exp`. Because support is young, verify
any toolchain with the shipped probe rather than assuming it, and pin "cannot do X" claims to a reproducing probe. At
the source level, the one compiler-enforced rule is that annotation string values must go through
`std::define_static_string`.

---

## 6. Checklist

When setting up a build, check that:

- the toolchain provides C++26 reflection (MSVC does not, as of June 2026);
- compilation with GCC 16.1.0 uses `-std=c++26 -freflection -lstdc++exp`;
- the toolchain checking tool is run before adopting a compiler and after each update;
- annotation string values are wrapped in `std::define_static_string` (raw literals are ill-formed);
- the translation unit calling `discover<...>()` includes
  `<ctr/Registration.hpp>` ([Guide 11 — Headers](guide-11-headers.md));
- ASan/TSan validation is run on a Linux toolchain, since they are unavailable on the Windows GCC build.

Next guide: [Guide 15 — API Reference](guide-15-api-reference.md).
