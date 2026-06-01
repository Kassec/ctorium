#include <benchmark/benchmark.h>

#include <ctr/Registration.hpp>

// -------------------------------------------------------------------------
// Fixtures
// -------------------------------------------------------------------------
//
// TU-local fixture namespaces stay distinct from BenchResolve.cpp namespaces.
// They avoid TypeId aliasing when benchmark TUs are linked into one binary.

namespace bench_contended_session {

    struct [[=ctr::session{}]] SessionBean {
    };

} // namespace bench_contended_session

namespace bench_contended_thread_local {

    struct [[=ctr::threadLocal{}]] ThreadLocalBean {
    };

} // namespace bench_contended_thread_local

namespace {

    static ctr::BeanContext *gAccessSessionContext = nullptr;
    static ctr::ScopedContext *gAccessSessionScope = nullptr;
    static ctr::Bean<bench_contended_session::SessionBean> gAccessSessionHandle;

    static ctr::BeanContext *gResolveSessionContext = nullptr;
    static ctr::ScopedContext *gResolveSessionScope = nullptr;

    static ctr::BeanContext *gAccessThreadLocalContext = nullptr;
    static ctr::Bean<bench_contended_thread_local::ThreadLocalBean> gAccessThreadLocalHandle;

    static ctr::BeanContext *gResolveThreadLocalContext = nullptr;

    static void setupAccessSession(const benchmark::State &) {
        gAccessSessionContext =
            &ctr::BeanContext::resolveContext("bm-contended-access-session");
        gAccessSessionContext->discover<^^bench_contended_session>().start();
        gAccessSessionScope =
            &gAccessSessionContext->resolveScope("bm-contended-access-session-scope");
        gAccessSessionScope->start();
        gAccessSessionHandle =
            gAccessSessionScope->resolve<bench_contended_session::SessionBean>();
    }

    static void teardownAccessSession(const benchmark::State &) {
        gAccessSessionHandle = {};
        gAccessSessionScope = nullptr;
        gAccessSessionContext->stop();
        gAccessSessionContext = nullptr;
    }

    static void setupResolveSession(const benchmark::State &) {
        gResolveSessionContext =
            &ctr::BeanContext::resolveContext("bm-contended-resolve-session");
        gResolveSessionContext->discover<^^bench_contended_session>().start();
        gResolveSessionScope =
            &gResolveSessionContext->resolveScope("bm-contended-resolve-session-scope");
        gResolveSessionScope->start();

        auto warm =
            gResolveSessionScope->resolve<bench_contended_session::SessionBean>();
        benchmark::DoNotOptimize(warm.operator->());
    }

    static void teardownResolveSession(const benchmark::State &) {
        gResolveSessionScope = nullptr;
        gResolveSessionContext->stop();
        gResolveSessionContext = nullptr;
    }

    static void setupAccessThreadLocal(const benchmark::State &) {
        gAccessThreadLocalContext =
            &ctr::BeanContext::resolveContext("bm-contended-access-thread-local");
        gAccessThreadLocalContext
            ->discover<^^bench_contended_thread_local>()
            .start();
        gAccessThreadLocalHandle =
            gAccessThreadLocalContext
                ->resolve<bench_contended_thread_local::ThreadLocalBean>();
    }

    static void teardownAccessThreadLocal(const benchmark::State &) {
        gAccessThreadLocalHandle = {};
        gAccessThreadLocalContext->stop();
        gAccessThreadLocalContext = nullptr;
    }

    static void setupResolveThreadLocal(const benchmark::State &) {
        gResolveThreadLocalContext =
            &ctr::BeanContext::resolveContext("bm-contended-resolve-thread-local");
        gResolveThreadLocalContext
            ->discover<^^bench_contended_thread_local>()
            .start();

        auto warm =
            gResolveThreadLocalContext
                ->resolve<bench_contended_thread_local::ThreadLocalBean>();
        benchmark::DoNotOptimize(warm);
    }

    static void teardownResolveThreadLocal(const benchmark::State &) {
        gResolveThreadLocalContext->stop();
        gResolveThreadLocalContext = nullptr;
    }

} // namespace

// -------------------------------------------------------------------------
// Group: Access
//
// Shared handle benchmarks resolve the handle once in Setup. The timed body is
// only operator-> on that same file-static handle, called by every worker.
// `ns/op` is averaged by thread because GBM aggregates iterations across all
// workers before finishing custom counters.
// -------------------------------------------------------------------------

// BM_Access_Session_Contended
// Timed path: shared Form 2 session handle operator-> only.

static void BM_Access_Session_Contended(benchmark::State &state) {
    const auto &handle = gAccessSessionHandle;
    for (auto _ : state) {
        auto *ptr = handle.operator->();
        benchmark::DoNotOptimize(ptr);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate
            | benchmark::Counter::kAvgThreads
            | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
}

BENCHMARK(BM_Access_Session_Contended)
    ->Name("BM_Access_Session_Contended_T1")
    ->Threads(1)
    ->UseRealTime()
    ->Setup(setupAccessSession)
    ->Teardown(teardownAccessSession);
BENCHMARK(BM_Access_Session_Contended)
    ->Name("BM_Access_Session_Contended_T2")
    ->Threads(2)
    ->UseRealTime()
    ->Setup(setupAccessSession)
    ->Teardown(teardownAccessSession);
BENCHMARK(BM_Access_Session_Contended)
    ->Name("BM_Access_Session_Contended_T4")
    ->Threads(4)
    ->UseRealTime()
    ->Setup(setupAccessSession)
    ->Teardown(teardownAccessSession);
BENCHMARK(BM_Access_Session_Contended)
    ->Name("BM_Access_Session_Contended_T8")
    ->Threads(8)
    ->UseRealTime()
    ->Setup(setupAccessSession)
    ->Teardown(teardownAccessSession);

// BM_Access_ThreadLocal_Contended
// Timed path: shared Form 3 thread-local handle operator-> only.

static void BM_Access_ThreadLocal_Contended(benchmark::State &state) {
    const auto &handle = gAccessThreadLocalHandle;
    for (auto _ : state) {
        auto *ptr = handle.operator->();
        benchmark::DoNotOptimize(ptr);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate
            | benchmark::Counter::kAvgThreads
            | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
}

BENCHMARK(BM_Access_ThreadLocal_Contended)
    ->Name("BM_Access_ThreadLocal_Contended_T1")
    ->Threads(1)
    ->UseRealTime()
    ->Setup(setupAccessThreadLocal)
    ->Teardown(teardownAccessThreadLocal);
BENCHMARK(BM_Access_ThreadLocal_Contended)
    ->Name("BM_Access_ThreadLocal_Contended_T2")
    ->Threads(2)
    ->UseRealTime()
    ->Setup(setupAccessThreadLocal)
    ->Teardown(teardownAccessThreadLocal);
BENCHMARK(BM_Access_ThreadLocal_Contended)
    ->Name("BM_Access_ThreadLocal_Contended_T4")
    ->Threads(4)
    ->UseRealTime()
    ->Setup(setupAccessThreadLocal)
    ->Teardown(teardownAccessThreadLocal);
BENCHMARK(BM_Access_ThreadLocal_Contended)
    ->Name("BM_Access_ThreadLocal_Contended_T8")
    ->Threads(8)
    ->UseRealTime()
    ->Setup(setupAccessThreadLocal)
    ->Teardown(teardownAccessThreadLocal);

// -------------------------------------------------------------------------
// Group: Resolve
//
// Per-thread handle benchmarks resolve a fresh handle in the timed body, then
// immediately call operator-> on that handle. `ns/op` is averaged by thread;
// `op/s` remains aggregate throughput across all workers.
// -------------------------------------------------------------------------

// BM_Resolve_Session_Contended
// Timed path: scope.resolve<SessionBean>() + operator->.

static void BM_Resolve_Session_Contended(benchmark::State &state) {
    for (auto _ : state) {
        auto handle =
            gResolveSessionScope->resolve<bench_contended_session::SessionBean>();
        auto *ptr = handle.operator->();
        benchmark::DoNotOptimize(ptr);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate
            | benchmark::Counter::kAvgThreads
            | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
}

BENCHMARK(BM_Resolve_Session_Contended)
    ->Name("BM_Resolve_Session_Contended_T1")
    ->Threads(1)
    ->UseRealTime()
    ->Setup(setupResolveSession)
    ->Teardown(teardownResolveSession);
BENCHMARK(BM_Resolve_Session_Contended)
    ->Name("BM_Resolve_Session_Contended_T2")
    ->Threads(2)
    ->UseRealTime()
    ->Setup(setupResolveSession)
    ->Teardown(teardownResolveSession);
BENCHMARK(BM_Resolve_Session_Contended)
    ->Name("BM_Resolve_Session_Contended_T4")
    ->Threads(4)
    ->UseRealTime()
    ->Setup(setupResolveSession)
    ->Teardown(teardownResolveSession);
BENCHMARK(BM_Resolve_Session_Contended)
    ->Name("BM_Resolve_Session_Contended_T8")
    ->Threads(8)
    ->UseRealTime()
    ->Setup(setupResolveSession)
    ->Teardown(teardownResolveSession);

// BM_Resolve_ThreadLocal_Contended
// Timed path: ctx.resolve<ThreadLocalBean>() + operator->.

static void BM_Resolve_ThreadLocal_Contended(benchmark::State &state) {
    for (auto _ : state) {
        auto handle =
            gResolveThreadLocalContext
                ->resolve<bench_contended_thread_local::ThreadLocalBean>();
        auto *ptr = handle.operator->();
        benchmark::DoNotOptimize(ptr);
    }
    state.counters["ns/op"] = benchmark::Counter(
        static_cast<double>(state.iterations()) / 1e9,
        benchmark::Counter::kIsRate
            | benchmark::Counter::kAvgThreads
            | benchmark::Counter::kInvert
        );
    state.counters["op/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
        );
}

BENCHMARK(BM_Resolve_ThreadLocal_Contended)
    ->Name("BM_Resolve_ThreadLocal_Contended_T1")
    ->Threads(1)
    ->UseRealTime()
    ->Setup(setupResolveThreadLocal)
    ->Teardown(teardownResolveThreadLocal);
BENCHMARK(BM_Resolve_ThreadLocal_Contended)
    ->Name("BM_Resolve_ThreadLocal_Contended_T2")
    ->Threads(2)
    ->UseRealTime()
    ->Setup(setupResolveThreadLocal)
    ->Teardown(teardownResolveThreadLocal);
BENCHMARK(BM_Resolve_ThreadLocal_Contended)
    ->Name("BM_Resolve_ThreadLocal_Contended_T4")
    ->Threads(4)
    ->UseRealTime()
    ->Setup(setupResolveThreadLocal)
    ->Teardown(teardownResolveThreadLocal);
BENCHMARK(BM_Resolve_ThreadLocal_Contended)
    ->Name("BM_Resolve_ThreadLocal_Contended_T8")
    ->Threads(8)
    ->UseRealTime()
    ->Setup(setupResolveThreadLocal)
    ->Teardown(teardownResolveThreadLocal);
