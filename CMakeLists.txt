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

add_subdirectory(test/toolchain)
