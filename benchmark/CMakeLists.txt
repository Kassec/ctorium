add_executable(bench-resolve
        ctr/BenchResolve.cpp
)

target_compile_features(bench-resolve
        PRIVATE
        cxx_std_26
)

target_link_libraries(bench-resolve
        PRIVATE
        ctorium
        benchmark::benchmark_main
)
