#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <ctr/Ctorium.hpp>

namespace shutdown_singleton_fixture {

std::atomic<int> preDestroyCallCount{0};

struct [[=ctr::singleton{}]] Singleton {
    [[=ctr::preDestroy{}]] void cleanup() {
        preDestroyCallCount.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace shutdown_singleton_fixture

namespace shutdown_prototype_fixture {

std::atomic<int> preDestroyCallCount{0};

struct [[=ctr::prototype{}]] Proto {
    [[=ctr::preDestroy{}]] void cleanup() {
        preDestroyCallCount.fetch_add(1, std::memory_order_relaxed);
    }
};

struct [[=ctr::singleton{}]] Holder {
    ctr::Bean<Proto> proto;

    explicit Holder(ctr::Bean<Proto> injected)
        : proto(std::move(injected)) {}
};

} // namespace shutdown_prototype_fixture

namespace {

constexpr std::size_t kHeapPressureBlockCount = 256;

struct Probe : ctr::BeanContext {
    explicit Probe(std::string key)
        : ctr::BeanContext(std::move(key)) {}
};

} // namespace

TEST(ShutdownGuarantee, SingletonPreDestroyRunsWhenContextDestroyedWithoutStop) {
    shutdown_singleton_fixture::preDestroyCallCount.store(0, std::memory_order_relaxed);

    {
        Probe context("shutdown-guarantee-singleton-without-stop");
        context.discover<^^shutdown_singleton_fixture>().start();
        auto singleton = context.resolve<shutdown_singleton_fixture::Singleton>();
        ASSERT_NE(singleton.operator->(), nullptr);
    }

    EXPECT_EQ(
        shutdown_singleton_fixture::preDestroyCallCount.load(std::memory_order_relaxed),
        1);
}

TEST(ShutdownGuarantee, SingletonPreDestroyRunsOnExplicitStop) {
    shutdown_singleton_fixture::preDestroyCallCount.store(0, std::memory_order_relaxed);

    Probe context("shutdown-guarantee-singleton-explicit-stop");
    context.discover<^^shutdown_singleton_fixture>().start();
    auto singleton = context.resolve<shutdown_singleton_fixture::Singleton>();
    ASSERT_NE(singleton.operator->(), nullptr);

    context.stop();

    EXPECT_EQ(
        shutdown_singleton_fixture::preDestroyCallCount.load(std::memory_order_relaxed),
        1);
}

TEST(ShutdownGuarantee, PrototypePreDestroyRunsWhenContextDestroyedWithoutStop) {
    shutdown_prototype_fixture::preDestroyCallCount.store(0, std::memory_order_relaxed);

    {
        Probe context("shutdown-guarantee-prototype-without-stop");
        context.discover<^^shutdown_prototype_fixture>().start();
        auto holder = context.resolve<shutdown_prototype_fixture::Holder>();
        ASSERT_NE(holder.operator->(), nullptr);
        ASSERT_NE(holder->proto.operator->(), nullptr);
    }

    EXPECT_EQ(
        shutdown_prototype_fixture::preDestroyCallCount.load(std::memory_order_relaxed),
        1);
}

TEST(ShutdownGuarantee, PrototypeHandleCanBeDestroyedAfterExplicitStop) {
    EXPECT_EXIT(
        {
            {
                auto& context = ctr::BeanContext::resolveContext(
                    "shutdown-guarantee-prototype-handle-after-stop");
                context.discover<^^shutdown_prototype_fixture>().start();
                auto proto = context.resolve<shutdown_prototype_fixture::Proto>();
                if (proto.operator->() == nullptr)
                    std::exit(2);

                context.stop();

                // Reuse recently freed heap blocks before the handle destructor runs.
                void* blocks[kHeapPressureBlockCount]{};
                for (std::size_t i = 0; i < kHeapPressureBlockCount; ++i) {
                    const std::size_t byteSize = 64 + (i % 128) * 32;
                    blocks[i] = ::operator new(byteSize);
                    auto* bytes = static_cast<volatile unsigned char*>(blocks[i]);
                    for (std::size_t j = 0; j < byteSize; ++j)
                        bytes[j] = 0xFFu;
                }
            }
            std::exit(0);
        },
        testing::ExitedWithCode(0),
        "");
}
