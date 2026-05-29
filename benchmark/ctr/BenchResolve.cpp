#include <benchmark/benchmark.h>

#include <ctr/Ctorium.hpp>

namespace bench {

struct [[=ctr::singleton{}]] BenchSingleton {};
struct [[=ctr::prototype{}]] BenchPrototype {};

} // namespace bench

static void BM_ResolveSingleton(benchmark::State& state) {
    auto& ctx = ctr::BeanContext::resolveContext("bm-singleton");
    ctx.discover<^^bench::BenchSingleton>().start();
    for (auto _ : state) {
        auto result = ctx.resolve<bench::BenchSingleton>();
        benchmark::DoNotOptimize(result);
    }
    ctx.close();
}
BENCHMARK(BM_ResolveSingleton)->MinTime(3.0);

static void BM_ResolvePrototype(benchmark::State& state) {
    auto& ctx = ctr::BeanContext::resolveContext("bm-prototype");
    ctx.discover<^^bench::BenchPrototype>().start();
    for (auto _ : state) {
        auto result = ctx.resolve<bench::BenchPrototype>();
        benchmark::DoNotOptimize(result);
    }
    ctx.close();
}
BENCHMARK(BM_ResolvePrototype)->MinTime(3.0);
