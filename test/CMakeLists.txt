add_executable(toolchain-check
        toolchain/toolchain.cpp
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

add_executable(ctorium-tests
        ctr/bean.cpp
        ctr/bind-session.cpp
        ctr/bind-singleton.cpp
        ctr/casts.cpp
        ctr/context.cpp
        ctr/concurrency.cpp
        ctr/defaults.cpp
        ctr/discovery.cpp
        ctr/exception-policy.cpp
        ctr/factory.cpp
        ctr/graph-validation.cpp
        ctr/injection.cpp
        ctr/lifecycle-hooks.cpp
        ctr/listeners.cpp
        ctr/priority.cpp
        ctr/prototype.cpp
        ctr/session.cpp
        ctr/shutdown-guarantee.cpp
        ctr/singleton.cpp
        ctr/scoped-listeners.cpp
        ctr/tl-lifetime-safety.cpp
        ctr/thread-local.cpp
        ctr/user-data.cpp
)

target_compile_features(ctorium-tests
        PRIVATE
        cxx_std_26
)

target_link_libraries(ctorium-tests
        PRIVATE
        ctorium
        GTest::gtest_main
)

target_include_directories(ctorium-tests
        PRIVATE
        ${PROJECT_SOURCE_DIR}/include/internal
)

if (BUILD_TESTING)
    gtest_discover_tests(ctorium-tests)
endif ()

add_executable(ctorium-prototype-max-slots-tests
        ctr/prototype-max-slots.cpp
)

target_compile_features(ctorium-prototype-max-slots-tests
        PRIVATE
        cxx_std_26
)

target_compile_definitions(ctorium-prototype-max-slots-tests
        PRIVATE
        CTORIUM_PROTOTYPE_MAX_SLOTS=4
)

target_link_libraries(ctorium-prototype-max-slots-tests
        PRIVATE
        ctorium
        GTest::gtest_main
)

if (BUILD_TESTING)
    gtest_discover_tests(ctorium-prototype-max-slots-tests)
endif ()
