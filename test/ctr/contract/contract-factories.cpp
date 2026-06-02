#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_factories_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Dependency {
    int value = 7;
};

struct Product {
    int value = 0;
};

struct UniqueProduct {
    int value = 0;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] HookedProduct {
    inline static int postConstructCount = 0;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize() { ++postConstructCount; }
};

struct PlainHookShape {
    inline static int postConstructCount = 0;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize() { ++postConstructCount; }
};

struct [[=CTORIUM_NAMESPACE::factory{}]] ProductFactory {
    CTORIUM_NAMESPACE::Bean<Dependency> dependency;

    explicit ProductFactory(CTORIUM_NAMESPACE::Bean<Dependency> injected)
        : dependency(std::move(injected)) {}

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("value")}]]
    Product makeValue() { return Product{.value = dependency->value}; }

    [[=CTORIUM_NAMESPACE::prototype{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("unique")}]]
    std::unique_ptr<UniqueProduct> makeUnique() {
        return std::make_unique<UniqueProduct>(UniqueProduct{.value = 9});
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("hooked")}]]
    HookedProduct makeHooked() { return HookedProduct{}; }

    [[=CTORIUM_NAMESPACE::prototype{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("plain")}]]
    PlainHookShape makePlain() { return PlainHookShape{}; }
};

} // namespace contract_factories_fixture

namespace contract_factories_duplicate_fixture {

struct Product {};

struct [[=CTORIUM_NAMESPACE::factory{}]] ProductFactory {
    [[=CTORIUM_NAMESPACE::singleton{}]] Product first() { return Product{}; }
    [[=CTORIUM_NAMESPACE::singleton{}]] Product second() { return Product{}; }
};

} // namespace contract_factories_duplicate_fixture

namespace contract_factories_named_fixture {

struct Product {
    int value = 0;
};

struct [[=CTORIUM_NAMESPACE::factory{}]] ProductFactory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("first")}]]
    Product first() { return Product{.value = 1}; }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("second")}]]
    Product second() { return Product{.value = 2}; }
};

} // namespace contract_factories_named_fixture

TEST(ContractFactories, Factory_IsSingleton) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-factory-singleton");
    context.discover<^^contract_factories_fixture>().start();
    auto first = context.resolve<contract_factories_fixture::ProductFactory>();
    auto second = context.resolve<contract_factories_fixture::ProductFactory>();
    EXPECT_NE(first.operator->(), nullptr);
    EXPECT_EQ(first.operator->(), second.operator->());
    context.stop();
}

TEST(ContractFactories, Factory_ProducerLifetime_Respected) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-producer-lifetime");
    context.discover<^^contract_factories_fixture>().start();
    auto first = context.resolve<contract_factories_fixture::UniqueProduct>(CTORIUM_NAMESPACE::named{"unique"});
    auto second = context.resolve<contract_factories_fixture::UniqueProduct>(CTORIUM_NAMESPACE::named{"unique"});
    EXPECT_NE(first.operator->(), nullptr);
    EXPECT_NE(second.operator->(), nullptr);
    EXPECT_NE(first.operator->(), second.operator->());
    context.stop();
}

TEST(ContractFactories, Factory_ReturnByValue) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-return-value");
    context.discover<^^contract_factories_fixture>().start();
    auto product = context.resolve<contract_factories_fixture::Product>(CTORIUM_NAMESPACE::named{"value"});
    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(product->value, 7);
    context.stop();
}

TEST(ContractFactories, Factory_ReturnUniquePtr) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-return-unique");
    context.discover<^^contract_factories_fixture>().start();
    auto product = context.resolve<contract_factories_fixture::UniqueProduct>(CTORIUM_NAMESPACE::named{"unique"});
    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(product->value, 9);
    context.stop();
}

TEST(ContractFactories, Factory_SameTypeSameKey_TwoProducers_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-duplicate-producers");
    context.discover<^^contract_factories_duplicate_fixture>();
    EXPECT_THROW(context.start(), CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractFactories, Factory_SameTypeDifferentNames_Allowed) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-named-producers");
    context.discover<^^contract_factories_named_fixture>().start();
    auto first = context.resolve<contract_factories_named_fixture::Product>(CTORIUM_NAMESPACE::named{"first"});
    auto second = context.resolve<contract_factories_named_fixture::Product>(CTORIUM_NAMESPACE::named{"second"});
    ASSERT_NE(first.operator->(), nullptr);
    ASSERT_NE(second.operator->(), nullptr);
    EXPECT_EQ(first->value, 1);
    EXPECT_EQ(second->value, 2);
    context.stop();
}

TEST(ContractFactories, Factory_ReceivesDependencies) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-factory-dependency");
    context.discover<^^contract_factories_fixture>().start();
    auto factory = context.resolve<contract_factories_fixture::ProductFactory>();
    auto dependency = context.resolve<contract_factories_fixture::Dependency>();
    ASSERT_NE(factory.operator->(), nullptr);
    EXPECT_EQ(factory->dependency.operator->(), dependency.operator->());
    context.stop();
}

TEST(ContractFactories, Factory_ProducedType_HooksIfCtorium) {
    contract_factories_fixture::HookedProduct::postConstructCount = 0;
    contract_factories_fixture::PlainHookShape::postConstructCount = 0;
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-product-hooks");
    context.discover<^^contract_factories_fixture>().start();
    (void)context.resolve<contract_factories_fixture::HookedProduct>(CTORIUM_NAMESPACE::named{"hooked"});
    (void)context.resolve<contract_factories_fixture::PlainHookShape>(CTORIUM_NAMESPACE::named{"plain"});
    EXPECT_EQ(contract_factories_fixture::HookedProduct::postConstructCount, 1);
    EXPECT_EQ(contract_factories_fixture::PlainHookShape::postConstructCount, 0);
    context.stop();
}

TEST(ContractFactories, Factory_ProducedType_ListenersAlways) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cf-product-listeners");
    context.discover<^^contract_factories_fixture>().start();
    std::vector<int> phases;
    context.on<contract_factories_fixture::PlainHookShape>(
        CTORIUM_NAMESPACE::onInitialized,
        [&](const CTORIUM_NAMESPACE::Bean<contract_factories_fixture::PlainHookShape>&) { phases.push_back(1); });
    context.on<contract_factories_fixture::PlainHookShape>(
        CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<contract_factories_fixture::PlainHookShape>&) { phases.push_back(2); });
    context.on<contract_factories_fixture::PlainHookShape>(
        CTORIUM_NAMESPACE::onPreDestroy,
        [&](const CTORIUM_NAMESPACE::Bean<contract_factories_fixture::PlainHookShape>&) { phases.push_back(3); });
    context.on<contract_factories_fixture::PlainHookShape>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<contract_factories_fixture::PlainHookShape>&) { phases.push_back(4); });
    {
        auto product = context.resolve<contract_factories_fixture::PlainHookShape>(CTORIUM_NAMESPACE::named{"plain"});
        EXPECT_NE(product.operator->(), nullptr);
    }
    EXPECT_EQ(phases, (std::vector<int>{1, 2, 3, 4}));
    context.stop();
}
