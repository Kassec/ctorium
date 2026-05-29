cmake_minimum_required(VERSION 4.2)
project(ctorium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ctorium INTERFACE)

target_compile_features(ctorium
        INTERFACE
        cxx_std_26
)

target_include_directories(ctorium
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/api
)

target_compile_definitions(ctorium
        INTERFACE
        CTORIUM_API=
)

target_compile_options(ctorium
        INTERFACE
        -freflection
)

target_link_libraries(ctorium
        INTERFACE
        stdc++exp
)

include(CTest)
include(FetchContent)

FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.4
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_subdirectory(benchmark)
add_subdirectory(test)
