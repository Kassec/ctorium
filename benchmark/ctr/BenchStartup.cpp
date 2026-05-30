#include <atomic>
#include <string>

#include <benchmark/benchmark.h>

#include <ctr/Ctorium.hpp>

#include "fixture/SmallGraph.hpp"    // bench_startup_small  — 32 singletons
#include "fixture/MediumGraph.hpp"   // bench_startup_medium — 128 singletons
#include "fixture/LargeGraph.hpp"    // bench_startup_large  — 1024 singletons

// ─── Fixture: session scope lifecycle ─────────────────────────────────────────
//
// Used by BM_Scope_StartStop only.  Kept inline because it is small and not
// shared with the graph-size fixtures.

namespace bench_startup_session {

    struct [[


    =
    ctr::singleton {
    }

    ]
    ]
    RootSingleton {
    };
    struct [[


    =
    ctr::session {
    }

    ]
    ]
    ScopeSession1 {
    };
    struct [[


    =
    ctr::session {
    }

    ]
    ]
    ScopeSession2 {
    };

} // namespace bench_startup_session

// ─── BM_Startup_SmallGraph ────────────────────────────────────────────────────
// Measures: resolveContext() + discover<^^root>() + start() for 32 singletons.
// Each iteration uses a unique context key to avoid the "already started"
// short-circuit.  close() is excluded from timing.
//
// Note: closed contexts accumulate in the global registry for the duration of
// the benchmark run.  Iterations() bounds this to a fixed number of entries.

static void BM_Startup_SmallGraph(benchmark::State &state) {
    static std::atomic<int> counter{0};
    for (auto _ : state) {
        state.PauseTiming();
        std::string key = "bm-su-small-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
        state.ResumeTiming();

        auto &ctx = ctr::BeanContext::resolveContext(key);
        ctx.discover<^^bench_startup_small>().start();

        state.PauseTiming();
        state.counters["ns/op"] = benchmark::Counter(
            static_cast<double>(state.iterations()) / 1e9,
            benchmark::Counter::kIsRate | benchmark::Counter::kInvert
            );
        state.counters["op/s"] = benchmark::Counter(
            static_cast<double>(state.iterations()),
            benchmark::Counter::kIsRate
            );
        ctx.stop();
        state.ResumeTiming();
    }
}

BENCHMARK(BM_Startup_SmallGraph);

// ─── BM_Startup_MediumGraph ───────────────────────────────────────────────────
// Measures resolve + discover + start for 128 singletons.

static void BM_Startup_MediumGraph(benchmark::State &state) {
    static std::atomic<int> counter{0};
    for (auto _ : state) {
        state.PauseTiming();
        std::string key = "bm-su-medium-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
        state.ResumeTiming();

        auto &ctx = ctr::BeanContext::resolveContext(key);
        ctx.discover<^^bench_startup_medium>().start();

        state.PauseTiming();
        state.counters["ns/op"] = benchmark::Counter(
            static_cast<double>(state.iterations()) / 1e9,
            benchmark::Counter::kIsRate | benchmark::Counter::kInvert
            );
        state.counters["op/s"] = benchmark::Counter(
            static_cast<double>(state.iterations()),
            benchmark::Counter::kIsRate
            );
        ctx.stop();
        state.ResumeTiming();
    }
}

BENCHMARK(BM_Startup_MediumGraph);

// ─── BM_Startup_LargeGraph ────────────────────────────────────────────────────
// Measures resolve + discover + start for 1024 singletons.
// Iterations are capped low to bound global registry growth and because each
// iteration is substantially longer at this scale.

static void BM_Startup_LargeGraph(benchmark::State &state) {
    static std::atomic<int> counter{0};
    for (auto _ : state) {
        state.PauseTiming();
        std::string key = "bm-su-large-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
        state.ResumeTiming();

        auto &ctx = ctr::BeanContext::resolveContext(key);
        ctx.discover<^^bench_startup_large>().start();

        state.PauseTiming();
        state.counters["ns/op"] = benchmark::Counter(
            static_cast<double>(state.iterations()) / 1e9,
            benchmark::Counter::kIsRate | benchmark::Counter::kInvert
            );
        state.counters["op/s"] = benchmark::Counter(
            static_cast<double>(state.iterations()),
            benchmark::Counter::kIsRate
            );
        ctx.stop();
        state.ResumeTiming();
    }
}

BENCHMARK(BM_Startup_LargeGraph);

// ─── BM_Scope_StartStop ───────────────────────────────────────────────────────
// Measures the full session lifecycle per scope cycle:
//   ScopedContext::start() — resize SessionStore (fresh atomic array), mark started
//   2× scope.resolve<Session>() — materialize two session instances
//   ScopedContext::stop()  — destroy both instances in reverse construction order,
//                            clear the session store
//
// The root context is started once outside the loop.  This represents the
// per-request cost in a server context where each request starts and stops its
// own session scope.

static void BM_Scope_StartStop(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-scope-cycle");
    ctx.discover<^^bench_startup_session>().start();
    auto &scope = ctx.resolveScope("bm-scope-cycle-s");

    for (auto _ : state) {
        scope.start();
        auto b1 = scope.resolve<bench_startup_session::ScopeSession1>();
        auto b2 = scope.resolve<bench_startup_session::ScopeSession2>();
        benchmark::DoNotOptimize(b1);
        benchmark::DoNotOptimize(b2);
        scope.stop();
        // b1/b2 destructors: Form 2 proxy handles, releaseIfPrototype no-op.
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

BENCHMARK(BM_Scope_StartStop);
