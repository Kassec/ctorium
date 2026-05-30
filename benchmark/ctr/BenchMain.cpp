#include <benchmark/benchmark.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef BENCH_MIN_TIME
#  define BENCH_MIN_TIME 3.0
#endif

namespace {

    class MinimalReporter final : public benchmark::BenchmarkReporter {
    public:
        bool ReportContext(const Context &) override {
            std::printf("%-64s%10s%12s\n", "Benchmark", "ns/op", "op/s");
            std::printf("%s\n", std::string(86, '-').c_str());
            return true;
        }

        void ReportRuns(const std::vector<Run> &reports) override {
            for (const auto &run : reports) {
                if (run.skipped)
                    continue;
                std::string name = run.benchmark_name();
                const auto slash = name.find('/');
                if (slash != std::string::npos)
                    name.resize(slash);
                std::printf("%-64s", name.c_str());
                print(run, "ns/op", "%10.4g");
                print(run, "op/s", "%12.4g");
                std::printf("\n");
            }
        }

        void Finalize() override {
        }

    private:
        static void print(const Run &run, const char *key, const char *fmt) {
            const auto it = run.counters.find(key);
            if (it != run.counters.end())
                std::printf(fmt, it->second.value);
            else
                std::printf("%12s", "n/a");
        }
    };

} // namespace

int main(int argc, char **argv) {
    // Inject --benchmark_min_time before user arguments so the user can still
    // override it at runtime.  std::to_string produces "3.000000" which GBM accepts.
    const std::string kMinTimeArg =
        "--benchmark_min_time=" + std::to_string(BENCH_MIN_TIME) + "s";

    std::vector<char *> args;
    args.push_back(argv[0]);
    args.push_back(const_cast<char *>(kMinTimeArg.c_str()));
    for (int i = 1; i < argc; ++i)
        args.push_back(argv[i]);
    int newArgc = static_cast<int>(args.size());

    benchmark::Initialize(&newArgc, args.data());
    if (benchmark::ReportUnrecognizedArguments(newArgc, args.data()))
        return 1;
    MinimalReporter reporter;
    benchmark::RunSpecifiedBenchmarks(&reporter);
    benchmark::Shutdown();
    return 0;
}
