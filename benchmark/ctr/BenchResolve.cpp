#include <chrono>
#include <vector>

#include <benchmark/benchmark.h>

#include <ctr/Ctorium.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────
//
// One namespace per lifetime/scenario.  Isolated namespaces prevent TypeId
// aliasing when TUs are linked into the same binary, and guarantee that each
// discover<^^ns>() registers exactly the types relevant to its benchmarks.

namespace bench_resolve_singleton {

    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Singleton {
    };

} // namespace bench_resolve_singleton

namespace bench_resolve_prototype {

    struct [[

    =
    ctr::prototype {
    }

    ]
    ]
    Prototype {
    };

} // namespace bench_resolve_prototype

// Prototype with 1 singleton dep.
// Per-call cost over BM_Resolve_Prototype: alloc + 1 × Bean<Dep> copy (Form 1,
// no atomic ops because the dep is a singleton) + std::move in ctor.

namespace bench_resolve_proto1dep {

    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep {
    };
    struct [[

    =
    ctr::prototype {
    }

    ]
    ]
    Proto1Dep {
        ctr::Bean<Dep> dep;
        explicit Proto1Dep (ctr::Bean<Dep> d)
        :
        dep(std::move(d))
        {
        }
    };

} // namespace bench_resolve_proto1dep

// Prototype with 8 singleton deps.
// Compares with BM_Resolve_Prototype_1Dep to isolate the marginal cost of
// each additional singleton injection: alloc + 8 × Bean<DepN> copy (all Form 1,
// no atomics) + 8 × std::move in ctor.

namespace bench_resolve_proto8deps {

    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep0 {
    };
    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep1 {
    };
    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep2 {
    };
    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep3 {
    };
    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep4 {
    };
    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep5 {
    };
    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep6 {
    };
    struct [[

    =
    ctr::singleton {
    }

    ]
    ]
    Dep7 {
    };

    struct [[

    =
    ctr::prototype {
    }

    ]
    ]
    Proto8Deps {
        ctr::Bean<Dep0> d0;
        ctr::Bean<Dep1> d1;
        ctr::Bean<Dep2> d2;
        ctr::Bean<Dep3> d3;
        ctr::Bean<Dep4> d4;
        ctr::Bean<Dep5> d5;
        ctr::Bean<Dep6> d6;
        ctr::Bean<Dep7> d7;

        explicit Proto8Deps (
            ctr::Bean<Dep0> d0_
       ,
        ctr::Bean<Dep1> d1_,
            ctr::Bean<Dep2> d2_, ctr::Bean<Dep3> d3_,
            ctr::Bean<Dep4> d4_, ctr::Bean<Dep5> d5_,
            ctr::Bean<Dep6> d6_, ctr::Bean<Dep7> d7_
        )
        :
        d0(std::move(d0_)), d1(std::move(d1_)),
            d2(std::move(d2_)), d3(std::move(d3_)),
            d4(std::move(d4_)), d5(std::move(d5_)),
            d6(std::move(d6_)), d7(std::move(d7_))
        {
        }
    };

} // namespace bench_resolve_proto8deps

// Session beans: Form 2 proxy handles (object_ == nullptr).
// Shared by BM_Resolve_Session and BM_Access_Session (different context keys).

namespace bench_resolve_session {

    struct [[

    =
    ctr::session {
    }

    ]
    ]
    Session {
    };

} // namespace bench_resolve_session

// Thread-local beans: Form 3 proxy handles (scopeNameId == kThreadLocalSentinel).
// operator->() always calls threadLocalResolve_() — no pointer caching.
// Shared by BM_Resolve_ThreadLocal and BM_Access_ThreadLocal.

namespace bench_resolve_tl {

    struct [[

    =
    ctr::threadLocal {
    }

    ]
    ]
    TLBean {
    };

} // namespace bench_resolve_tl

// Prototype benchmark parameters.
// kBatch  = 2^20 — total resolutions per state iteration (for timing stability).
// kWindow = 2^10 — max concurrent live prototype instances (bounds slot pool usage).
// kChunks = kBatch / kWindow — number of timed chunks per state iteration.
//
// Pattern per outer iteration:
//   for each chunk: chrono → emplace kWindow beans → chrono → beans.clear()
//   SetIterationTime(Σ chunk times) drives the minimum time check on real creation time.
//   Destruction is excluded from all timed sections.
static constexpr std::size_t kBatch = 1u << 20;
static constexpr std::size_t kWindow = 1u << 10;
static constexpr std::size_t kChunks = kBatch / kWindow;


// ═══════════════════════════════════════════════════════════════════════════════
// Group: Resolve
//
// All benchmarks that call ctx.resolve<T>() or scope.resolve<T>().
// Singleton/session/TL variants: standard state loop, DoNotOptimize, global min time from CMake.
// Prototype variants: UseManualTime.  kBatch resolutions per state iteration,
// split into kChunks of kWindow.  Each chunk is timed independently; beans.clear()
// fires between chunks outside the timed window.  SetIterationTime reports total
// creation time; ns/op and op/s are derived from accumulated timings.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── BM_Resolve_Singleton ─────────────────────────────────────────────────────
// Hot-path lookup of an already-materialized singleton.
// Timed path: SingletonStore::find() (lock-free atomic load) + Bean<T> ctor.

static void BM_Resolve_Singleton(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-res-sing");
    ctx.discover<^^bench_resolve_singleton>().start();
    auto warm = ctx.resolve<bench_resolve_singleton::Singleton>();
    benchmark::DoNotOptimize(warm);

    for (auto _ : state) {
        auto bean = ctx.resolve<bench_resolve_singleton::Singleton>();
        benchmark::DoNotOptimize(bean);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
    ctx.stop();
}

BENCHMARK(BM_Resolve_Singleton);

// ─── BM_Resolve_Prototype ─────────────────────────────────────────────────────
// Creation cost of a trivial prototype (no dependencies), dealloc excluded.
// Baseline for BM_Resolve_Prototype_1Dep and BM_Resolve_Prototype_8Deps.

static void BM_Resolve_Prototype(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-res-proto");
    ctx.discover<^^bench_resolve_prototype>().start();

    std::vector<ctr::Bean<bench_resolve_prototype::Prototype>> beans;
    beans.reserve(kWindow);

    double total_creation_s = 0.0;

    for (auto _ : state) {
        double batch_s = 0.0;
        for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < kWindow; ++i) {
                beans.emplace_back(ctx.resolve<bench_resolve_prototype::Prototype>());
            }
            const auto t1 = std::chrono::steady_clock::now();
            batch_s += std::chrono::duration<double>(t1 - t0).count();
            beans.clear(); // destruction excluded from timing
        }
        state.SetIterationTime(batch_s);
        total_creation_s += batch_s;
    }
    const double total_ops = static_cast<double>(state.iterations()) * kBatch;
    state.counters["ns/op"] = benchmark::Counter(total_creation_s * 1e9 / total_ops);
    state.counters["op/s"] = benchmark::Counter(total_ops, benchmark::Counter::kIsRate);
    ctx.stop();
}

BENCHMARK(BM_Resolve_Prototype)->UseManualTime();

// ─── BM_Resolve_Prototype_1Dep ────────────────────────────────────────────────
// Creation cost of a prototype injecting 1 singleton dep, dealloc excluded.
// Singleton dep materialized before state loop; excluded from all timed sections.
// Difference vs BM_Resolve_Prototype → 1-dep injection overhead.

static void BM_Resolve_Prototype_1Dep(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-res-proto-1dep");
    ctx.discover<^^bench_resolve_proto1dep>().start();
    auto warm = ctx.resolve<bench_resolve_proto1dep::Proto1Dep>();
    benchmark::DoNotOptimize(warm.operator->());

    std::vector<ctr::Bean<bench_resolve_proto1dep::Proto1Dep>> beans;
    beans.reserve(kWindow);

    double total_creation_s = 0.0;

    for (auto _ : state) {
        double batch_s = 0.0;
        for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < kWindow; ++i) {
                beans.emplace_back(ctx.resolve<bench_resolve_proto1dep::Proto1Dep>());
            }
            const auto t1 = std::chrono::steady_clock::now();
            batch_s += std::chrono::duration<double>(t1 - t0).count();
            beans.clear();
        }
        state.SetIterationTime(batch_s);
        total_creation_s += batch_s;
    }
    const double total_ops = static_cast<double>(state.iterations()) * kBatch;
    state.counters["ns/op"] = benchmark::Counter(total_creation_s * 1e9 / total_ops);
    state.counters["op/s"] = benchmark::Counter(total_ops, benchmark::Counter::kIsRate);
    ctx.stop();
}

BENCHMARK(BM_Resolve_Prototype_1Dep)->UseManualTime();

// ─── BM_Resolve_Prototype_8Deps ───────────────────────────────────────────────
// Creation cost of a prototype injecting 8 singleton deps, dealloc excluded.
// All 8 singleton deps materialized transitively by warmup; excluded from timing.
// (BM_Resolve_Prototype_8Deps − BM_Resolve_Prototype_1Dep) / 7 ≈ marginal dep cost.

static void BM_Resolve_Prototype_8Deps(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-res-proto-8deps");
    ctx.discover<^^bench_resolve_proto8deps>().start();
    auto warm = ctx.resolve<bench_resolve_proto8deps::Proto8Deps>();
    benchmark::DoNotOptimize(warm.operator->());

    std::vector<ctr::Bean<bench_resolve_proto8deps::Proto8Deps>> beans;
    beans.reserve(kWindow);

    double total_creation_s = 0.0;

    for (auto _ : state) {
        double batch_s = 0.0;
        for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < kWindow; ++i) {
                beans.emplace_back(ctx.resolve<bench_resolve_proto8deps::Proto8Deps>());
            }
            const auto t1 = std::chrono::steady_clock::now();
            batch_s += std::chrono::duration<double>(t1 - t0).count();
            beans.clear();
        }
        state.SetIterationTime(batch_s);
        total_creation_s += batch_s;
    }
    const double total_ops = static_cast<double>(state.iterations()) * kBatch;
    state.counters["ns/op"] = benchmark::Counter(total_creation_s * 1e9 / total_ops);
    state.counters["op/s"] = benchmark::Counter(total_ops, benchmark::Counter::kIsRate);
    ctx.stop();
}

BENCHMARK(BM_Resolve_Prototype_8Deps)->UseManualTime();

// ─── BM_Resolve_Session ───────────────────────────────────────────────────────
// Hot-path resolve of an already-materialized session bean from the same scope.
// Returns a Form 2 proxy handle each call (object_ == nullptr).
// Timed path: TypeId lookup + SessionStore atomic find() + proxy handle ctor.

static void BM_Resolve_Session(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-res-sess");
    ctx.discover<^^bench_resolve_session>().start();
    auto &scope = ctx.resolveScope("bm-res-sess-s");
    scope.start();
    auto warm = scope.resolve<bench_resolve_session::Session>();
    benchmark::DoNotOptimize(warm);

    for (auto _ : state) {
        auto bean = scope.resolve<bench_resolve_session::Session>();
        benchmark::DoNotOptimize(bean);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
    ctx.stop();
}

BENCHMARK(BM_Resolve_Session);

// ─── BM_Resolve_ThreadLocal ───────────────────────────────────────────────────
// Cost of resolve<TLBean>() after the calling thread's instance is live.
// Returns a Form 3 handle (scopeNameId == kThreadLocalSentinel) each call.
// Timed path: TypeId lookup + Form 3 handle ctor (no TL lookup — deferred to
// operator->; compare with BM_Access_ThreadLocal for per-access cost).

static void BM_Resolve_ThreadLocal(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-res-tl");
    ctx.discover<^^bench_resolve_tl>().start();
    auto warm = ctx.resolve<bench_resolve_tl::TLBean>();
    benchmark::DoNotOptimize(warm);

    for (auto _ : state) {
        auto bean = ctx.resolve<bench_resolve_tl::TLBean>();
        benchmark::DoNotOptimize(bean);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
    ctx.stop();
}

BENCHMARK(BM_Resolve_ThreadLocal);


// ═══════════════════════════════════════════════════════════════════════════════
// Group: Access
//
// All benchmarks that call handle.operator->() on a retained handle.
// The handle is resolved once outside the loop; operator->() is the
// sole operation measured per iteration.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── BM_Access_Session ────────────────────────────────────────────────────────
// Per-call cost of operator->() on a retained Form 2 session proxy.
// proxyResolve_() fires every call: SessionStore atomic load (acquire) indexed
// by the stored candidateNameId.  No construction — the object is already live.

static void BM_Access_Session(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-acc-sess");
    ctx.discover<^^bench_resolve_session>().start();
    auto &scope = ctx.resolveScope("bm-acc-sess-s");
    scope.start();
    auto handle = scope.resolve<bench_resolve_session::Session>();

    for (auto _ : state) {
        auto *ptr = handle.operator->();
        benchmark::DoNotOptimize(ptr);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
    ctx.stop();
}

BENCHMARK(BM_Access_Session);

// ─── BM_Access_ThreadLocal ────────────────────────────────────────────────────
// Per-call cost of operator->() on a retained Form 3 thread-local handle.
// threadLocalResolve_() fires every call — the pointer is never cached.
// Compare with BM_Access_Session to see TL-store vs session-store access cost.

static void BM_Access_ThreadLocal(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-acc-tl");
    ctx.discover<^^bench_resolve_tl>().start();
    auto handle = ctx.resolve<bench_resolve_tl::TLBean>();
    benchmark::DoNotOptimize(handle);

    for (auto _ : state) {
        auto *ptr = handle.operator->();
        benchmark::DoNotOptimize(ptr);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
    ctx.stop();
}

BENCHMARK(BM_Access_ThreadLocal);
