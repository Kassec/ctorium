#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <meta>

#include <ctr/Registration.hpp>

namespace concurrency_lazy_singleton_fixture {

std::atomic<int> constructionCount{0};

struct [[=ctr::singleton{}]] Service {
    Service() { constructionCount.fetch_add(1, std::memory_order_relaxed); }
};

} // namespace concurrency_lazy_singleton_fixture

TEST(Concurrency, ConcurrentLazySingletonResolveConstructsExactlyOnce) {
    constexpr int kThreadCount = 8;

    concurrency_lazy_singleton_fixture::constructionCount.store(0, std::memory_order_relaxed);

    auto& ctx = ctr::BeanContext::resolveContext("conc-lazy-singleton");
    ctx.discover<^^concurrency_lazy_singleton_fixture>().start();

    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::array<concurrency_lazy_singleton_fixture::Service*, kThreadCount> pointers{};
    std::array<std::thread, kThreadCount> threads;

    for (int i = 0; i < kThreadCount; ++i) {
        threads[i] = std::thread([&, i] {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {}

            auto bean = ctx.resolve<concurrency_lazy_singleton_fixture::Service>();
            pointers[i] = bean.operator->();
        });
    }

    while (ready.load(std::memory_order_acquire) != kThreadCount) {}
    go.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_NE(pointers[0], nullptr);
    for (auto* pointer : pointers) {
        EXPECT_EQ(pointer, pointers[0]);
    }
    EXPECT_EQ(
        concurrency_lazy_singleton_fixture::constructionCount.load(std::memory_order_relaxed),
        1);

    ctx.stop();
}

namespace concurrency_listener_churn_fixture {

struct [[=ctr::prototype{}]] Service {};

} // namespace concurrency_listener_churn_fixture

TEST(Concurrency, ConcurrentListenerRegistrationRemovalDuringResolveDoesNotDropPersistentDispatch) {
    constexpr int kResolverThreadCount = 4;
    constexpr int kResolveIterations = 64;

    auto& ctx = ctr::BeanContext::resolveContext("conc-listener-churn");
    ctx.discover<^^concurrency_listener_churn_fixture>().start();

    std::atomic<int> persistentEvents{0};
    std::atomic<int> materializations{0};
    std::atomic<bool> done{false};

    auto persistent = ctx.on<concurrency_listener_churn_fixture::Service>(
        ctr::onCreated,
        [&](const ctr::Bean<concurrency_listener_churn_fixture::Service>&) {
            persistentEvents.fetch_add(1, std::memory_order_relaxed);
        });

    std::thread churner([&] {
        while (!done.load(std::memory_order_acquire)) {
            auto handle = ctx.on<concurrency_listener_churn_fixture::Service>(
                ctr::onCreated,
                [](const ctr::Bean<concurrency_listener_churn_fixture::Service>&) {});
            ctx.remove(handle);
        }
    });

    std::array<std::thread, kResolverThreadCount> resolvers;
    for (auto& resolver : resolvers) {
        resolver = std::thread([&] {
            for (int i = 0; i < kResolveIterations; ++i) {
                auto bean = ctx.resolve<concurrency_listener_churn_fixture::Service>();
                if (bean.operator->() != nullptr) {
                    materializations.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& resolver : resolvers) {
        resolver.join();
    }
    done.store(true, std::memory_order_release);
    churner.join();

    EXPECT_GE(
        persistentEvents.load(std::memory_order_relaxed),
        materializations.load(std::memory_order_relaxed));

    ctx.remove(persistent);
    ctx.stop();
}

namespace concurrency_default_named_fixture {

struct Service {
    int value;
    explicit Service(int v) : value(v) {}
};

} // namespace concurrency_default_named_fixture

TEST(Concurrency, ConcurrentDefaultNamedUpdatesResolveOldOrNewDefaultOnly) {
    constexpr int kResolverThreadCount = 4;
    constexpr int kResolveIterations = 128;

    auto& ctx = ctr::BeanContext::resolveContext("conc-default-named");
    ctx.bindSingleton<concurrency_default_named_fixture::Service>(
        std::make_unique<concurrency_default_named_fixture::Service>(1),
        ctr::BindOptions{.name = std::define_static_string("old")});
    ctx.bindSingleton<concurrency_default_named_fixture::Service>(
        std::make_unique<concurrency_default_named_fixture::Service>(2),
        ctr::BindOptions{.name = std::define_static_string("new")});
    ctx.start();
    ctx.defaultNamed<concurrency_default_named_fixture::Service>(
        std::define_static_string("old"));

    std::atomic<bool> done{false};
    std::atomic<int> oldSeen{0};
    std::atomic<int> newSeen{0};
    std::atomic<int> unexpected{0};

    std::thread updater([&] {
        while (!done.load(std::memory_order_acquire)) {
            ctx.defaultNamed<concurrency_default_named_fixture::Service>(
                std::define_static_string("new"));
            ctx.defaultNamed<concurrency_default_named_fixture::Service>(
                std::define_static_string("old"));
        }
    });

    std::array<std::thread, kResolverThreadCount> resolvers;
    for (auto& resolver : resolvers) {
        resolver = std::thread([&] {
            for (int i = 0; i < kResolveIterations; ++i) {
                try {
                    auto bean = ctx.resolve<concurrency_default_named_fixture::Service>();
                    if (bean->value == 1) {
                        oldSeen.fetch_add(1, std::memory_order_relaxed);
                    } else if (bean->value == 2) {
                        newSeen.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        unexpected.fetch_add(1, std::memory_order_relaxed);
                    }
                } catch (...) {
                    unexpected.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& resolver : resolvers) {
        resolver.join();
    }
    done.store(true, std::memory_order_release);
    updater.join();

    EXPECT_EQ(unexpected.load(std::memory_order_relaxed), 0);
    EXPECT_GT(oldSeen.load(std::memory_order_relaxed) + newSeen.load(std::memory_order_relaxed), 0);

    ctx.stop();
}
