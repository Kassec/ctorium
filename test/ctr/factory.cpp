#include <atomic>
#include <memory>

#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace factory_rt_fixture {

struct Product { int x = 42; };
struct [[=ctr::factory{}]] Factory {
    [[=ctr::singleton{}]] std::unique_ptr<Product> make() {
        ++makeCallCount;
        return std::make_unique<Product>();
    }

    static inline std::atomic<int> makeCallCount{0};
};

} // namespace factory_rt_fixture

// ─── Factory runtime tests ────────────────────────────────────────────────────

TEST(Factory, FactoryProducedBeanResolvable) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-resolve");
    ctx.discover<^^factory_rt_fixture>().start();
    auto bean = ctx.resolve<factory_rt_fixture::Product>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(Factory, FactoryProducedBeanIsSingleton) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-singleton");
    ctx.discover<^^factory_rt_fixture>().start();
    auto b1 = ctx.resolve<factory_rt_fixture::Product>();
    auto b2 = ctx.resolve<factory_rt_fixture::Product>();
    EXPECT_EQ(b1.operator->(), b2.operator->());
    ctx.stop();
}

TEST(Factory, FactoryMethodCalledExactlyOnce) {
    factory_rt_fixture::Factory::makeCallCount.store(0, std::memory_order_relaxed);

    auto& ctx = ctr::BeanContext::resolveContext("ft-make-once");
    ctx.discover<^^factory_rt_fixture>().start();
    auto b1 = ctx.resolve<factory_rt_fixture::Product>();
    auto b2 = ctx.resolve<factory_rt_fixture::Product>();

    EXPECT_NE(b1.operator->(), nullptr);
    EXPECT_EQ(b1.operator->(), b2.operator->());
    EXPECT_EQ(factory_rt_fixture::Factory::makeCallCount.load(std::memory_order_relaxed), 1);

    ctx.stop();
}
