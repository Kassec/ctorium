add_executable(toolchain-check
        toolchain.cpp
)

target_compile_features(toolchain-check
        PRIVATE
        cxx_std_26
)

if (CMAKE_CXX_EXTENSIONS)
    set(TOOLCHAIN_CHECK_CXX_STANDARD_FLAG "-std=gnu++26")
else ()
    set(TOOLCHAIN_CHECK_CXX_STANDARD_FLAG "-std=c++26")
endif ()

target_compile_definitions(toolchain-check
        PRIVATE
        TOOLCHAIN_CHECK_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
        TOOLCHAIN_CHECK_CXX_STANDARD_FLAG="${TOOLCHAIN_CHECK_CXX_STANDARD_FLAG}"
        TOOLCHAIN_CHECK_REFLECTION_FLAG="-freflection"
        TOOLCHAIN_CHECK_EXPERIMENTAL_LIBRARY="stdc++exp"
)

if (BUILD_TESTING)
    add_test(
            NAME toolchain-check
            COMMAND toolchain-check
    )

    set_tests_properties(toolchain-check
            PROPERTIES
            FIXTURES_SETUP CtoriumToolchain
    )
endif ()
