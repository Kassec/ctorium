#include <utility>

#include <benchmark/benchmark.h>

#include <ctr/Ctorium.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────
//
// Isolated namespace prevents TypeId aliasing with BenchResolve.cpp types
// when both TUs are linked into a single binary.

namespace bench_handle {

    struct [[
    =
    ctr::singleton {
    }

    ]
    ]
    HSingleton {
    };
    struct [[
    =
    ctr::prototype {
    }

    ]
    ]
    HPrototype {
    };

} // namespace bench_handle

// ─── BM_Handle_Singleton_Copy ─────────────────────────────────────────────────
// Singleton Bean<T> copy: Form 1 struct copy, no atomic ops.
// retainIfPrototype() is a no-op for singletons (slot == kInvalidSlotId).
// This sets the lower bound for handle copying cost.

static void BM_Handle_Singleton_Copy(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-hnd-sing-copy");
    ctx.discover<^^bench_handle>().start();
    auto handle = ctx.resolve<bench_handle::HSingleton>();

    for (auto _ : state) {
        auto copy = handle; // retainIfPrototype: no-op for singleton
        benchmark::DoNotOptimize(copy);
        // copy destructor: releaseIfPrototype no-op
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

BENCHMARK(BM_Handle_Singleton_Copy);

// ─── BM_Handle_Singleton_Move ─────────────────────────────────────────────────
// Singleton Bean<T> move: two pointer-field swaps per iteration (move in,
// move back to restore).  No atomic ops, no registry interaction.
// Each iteration performs two moves; subtract singleton-copy cost for one-move cost.

static void BM_Handle_Singleton_Move(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-hnd-sing-move");
    ctx.discover<^^bench_handle>().start();
    auto handle = ctx.resolve<bench_handle::HSingleton>();

    for (auto _ : state) {
        auto moved = std::move(handle); // source fields zeroed
        benchmark::DoNotOptimize(moved);
        handle = std::move(moved); // restore: no alloc, no atomic
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

BENCHMARK(BM_Handle_Singleton_Move);

// ─── BM_Handle_Prototype_Copy ─────────────────────────────────────────────────
// Prototype Bean<T> copy: retainIfPrototype (atomic fetch_add on refcount) on
// copy construction, releaseIfPrototype (atomic fetch_sub; no deallocation
// because source handle is still alive) on copy destruction.
// Difference vs BM_Handle_Singleton_Copy isolates two atomic ops per iteration.

static void BM_Handle_Prototype_Copy(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-hnd-proto-copy");
    ctx.discover<^^bench_handle>().start();
    auto handle = ctx.resolve<bench_handle::HPrototype>();

    for (auto _ : state) {
        auto copy = handle; // retainIfPrototype: atomic fetch_add
        benchmark::DoNotOptimize(copy);
        // copy destructor: releaseIfPrototype atomic fetch_sub (no dealloc)
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

BENCHMARK(BM_Handle_Prototype_Copy);

// ─── BM_Handle_Prototype_Move ─────────────────────────────────────────────────
// Prototype Bean<T> move: ownership transferred, source zeroed.
// No atomic ops — retainIfPrototype and releaseIfPrototype are not called
// on move construction/assignment.
// Each iteration performs two moves; compare with BM_Handle_Prototype_Copy to
// confirm that move avoids the two atomic ops paid by copy.

static void BM_Handle_Prototype_Move(benchmark::State &state) {
    auto &ctx = ctr::BeanContext::resolveContext("bm-hnd-proto-move");
    ctx.discover<^^bench_handle>().start();
    auto handle = ctx.resolve<bench_handle::HPrototype>();

    for (auto _ : state) {
        auto moved = std::move(handle); // no retain: source zeroed
        benchmark::DoNotOptimize(moved);
        handle = std::move(moved); // no retain, no release: restore
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

BENCHMARK(BM_Handle_Prototype_Move);
