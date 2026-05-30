set(BENCH_MIN_TIME "3.0" CACHE STRING "Minimum benchmark run time in seconds")

add_executable(bench-resolve
        ctr/BenchResolve.cpp
        ctr/BenchMain.cpp
)
target_compile_features(bench-resolve PRIVATE cxx_std_26)
target_compile_definitions(bench-resolve PRIVATE BENCH_MIN_TIME=${BENCH_MIN_TIME})
target_link_libraries(bench-resolve PRIVATE ctorium benchmark::benchmark)

add_executable(bench-handle
        ctr/BenchHandle.cpp
        ctr/BenchMain.cpp
)
target_compile_features(bench-handle PRIVATE cxx_std_26)
target_compile_definitions(bench-handle PRIVATE BENCH_MIN_TIME=${BENCH_MIN_TIME})
target_link_libraries(bench-handle PRIVATE ctorium benchmark::benchmark)

add_executable(bench-startup
        ctr/BenchStartup.cpp
        ctr/BenchMain.cpp
)
target_compile_features(bench-startup PRIVATE cxx_std_26)
target_compile_definitions(bench-startup PRIVATE BENCH_MIN_TIME=${BENCH_MIN_TIME})
target_link_libraries(bench-startup PRIVATE ctorium benchmark::benchmark)