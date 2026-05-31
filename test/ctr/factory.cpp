#include <atomic>
#include <memory>
#include <meta>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace factory_rt_fixture {

struct ProductTag {};

template<typename>
struct Product {
    int x = 42;
};

using RuntimeProduct = Product<ProductTag>;

struct [[=ctr::factory{}]] Factory {
    [[=ctr::singleton{}]] RuntimeProduct make() {
        ++makeCallCount;
        return RuntimeProduct{};
    }

    static inline std::atomic<int> makeCallCount{0};
};

} // namespace factory_rt_fixture

// ─── Factory runtime tests ────────────────────────────────────────────────────

TEST(Factory, FactoryProducedBeanResolvable) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-resolve");
    ctx.discover<^^factory_rt_fixture>().start();
    auto bean = ctx.resolve<factory_rt_fixture::RuntimeProduct>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(Factory, FactoryProducedBeanIsSingleton) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-singleton");
    ctx.discover<^^factory_rt_fixture>().start();
    auto b1 = ctx.resolve<factory_rt_fixture::RuntimeProduct>();
    auto b2 = ctx.resolve<factory_rt_fixture::RuntimeProduct>();
    EXPECT_EQ(b1.operator->(), b2.operator->());
    ctx.stop();
}

TEST(Factory, FactoryMethodCalledExactlyOnce) {
    factory_rt_fixture::Factory::makeCallCount.store(0, std::memory_order_relaxed);

    auto& ctx = ctr::BeanContext::resolveContext("ft-make-once");
    ctx.discover<^^factory_rt_fixture>().start();
    auto b1 = ctx.resolve<factory_rt_fixture::RuntimeProduct>();
    auto b2 = ctx.resolve<factory_rt_fixture::RuntimeProduct>();

    EXPECT_NE(b1.operator->(), nullptr);
    EXPECT_EQ(b1.operator->(), b2.operator->());
    EXPECT_EQ(factory_rt_fixture::Factory::makeCallCount.load(std::memory_order_relaxed), 1);

    ctx.stop();
}

namespace factory_singleton_handle_fixture {

std::atomic<int> postConstructCount{0};

struct [[=ctr::factory{}]] Factory {
    [[=ctr::postConstruct{}]]
    void init() {
        postConstructCount.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace factory_singleton_handle_fixture

TEST(Factory, ResolveFactoryReturnsSingletonHandle) {
    factory_singleton_handle_fixture::postConstructCount.store(0, std::memory_order_relaxed);

    auto& ctx = ctr::BeanContext::resolveContext("ft-factory-singleton-handle");
    ctx.discover<^^factory_singleton_handle_fixture>().start();

    auto first = ctx.resolve<factory_singleton_handle_fixture::Factory>();
    auto second = ctx.resolve<factory_singleton_handle_fixture::Factory>();

    EXPECT_NE(first.operator->(), nullptr);
    EXPECT_EQ(first.operator->(), second.operator->());
    EXPECT_EQ(
        factory_singleton_handle_fixture::postConstructCount.load(std::memory_order_relaxed),
        1);

    ctx.stop();
}

namespace factory_constructor_injection_fixture {

struct [[=ctr::singleton{}]] Dep {
    int value = 7;
};

struct [[=ctr::factory{}]] Factory {
    ctr::Bean<Dep> dep;
    explicit Factory(ctr::Bean<Dep> d) : dep(std::move(d)) {}
};

} // namespace factory_constructor_injection_fixture

TEST(Factory, FactoryConstructorReceivesCtoriumDependency) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-factory-constructor-injection");
    ctx.discover<^^factory_constructor_injection_fixture>().start();

    auto dep = ctx.resolve<factory_constructor_injection_fixture::Dep>();
    auto factory = ctx.resolve<factory_constructor_injection_fixture::Factory>();

    ASSERT_NE(factory.operator->(), nullptr);
    EXPECT_EQ(factory->dep.operator->(), dep.operator->());
    EXPECT_EQ(factory->dep->value, 7);

    ctx.stop();
}

namespace factory_return_forms_fixture {

struct ReturnFormsTag {};
struct UniquePtrTag {};

template<typename>
struct Product {
    int value = 0;
};

using ReturnFormsProduct = Product<ReturnFormsTag>;
using UniquePtrProduct = Product<UniquePtrTag>;

struct [[=ctr::factory{}]] Factory {
    [[=ctr::singleton{}]]
    [[=ctr::named{.name = std::define_static_string("value")}]]
    ReturnFormsProduct makeValue() {
        return ReturnFormsProduct{.value = 1};
    }

    [[=ctr::prototype{}]]
    [[=ctr::named{.name = std::define_static_string("unique")}]]
    std::unique_ptr<UniquePtrProduct> makeUnique() {
        return std::make_unique<UniquePtrProduct>(UniquePtrProduct{.value = 2});
    }
};

} // namespace factory_return_forms_fixture

TEST(Factory, ValueProducerReturnFormIsResolvableByName) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-value-return-form");
    ctx.discover<^^factory_return_forms_fixture>().start();

    auto valueProduct = ctx.resolve<factory_return_forms_fixture::ReturnFormsProduct>(
        ctr::named{.name = std::define_static_string("value")});

    ASSERT_NE(valueProduct.operator->(), nullptr);
    EXPECT_EQ(valueProduct->value, 1);

    ctx.stop();
}

TEST(Factory, UniquePtrProducerReturnFormIsBlockedByCurrentDescriptorGeneration) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-unique-ptr-return-form");
    ctx.discover<^^factory_return_forms_fixture>().start();

    auto first = ctx.resolve<factory_return_forms_fixture::UniquePtrProduct>(
        ctr::named{.name = std::define_static_string("unique")});
    auto second = ctx.resolve<factory_return_forms_fixture::UniquePtrProduct>(
        ctr::named{.name = std::define_static_string("unique")});

    ASSERT_NE(first.operator->(), nullptr);
    ASSERT_NE(second.operator->(), nullptr);
    EXPECT_EQ(first->value, 2);
    EXPECT_EQ(first.metadata().exactType(), typeid(factory_return_forms_fixture::UniquePtrProduct));
    EXPECT_NE(first.operator->(), second.operator->());

    ctx.stop();
}

namespace factory_product_hook_fixture {

std::atomic<int> postConstructCount{0};
std::atomic<int> preDestroyCount{0};

struct HookedTag {};
struct PlainTag {};

template<typename>
struct HookedProduct {
    [[=ctr::postConstruct{}]]
    void init();

    [[=ctr::preDestroy{}]]
    void cleanup();
};

template<typename T>
void HookedProduct<T>::init() {
    postConstructCount.fetch_add(1, std::memory_order_relaxed);
}

template<typename T>
void HookedProduct<T>::cleanup() {
    preDestroyCount.fetch_add(1, std::memory_order_relaxed);
}

template void HookedProduct<HookedTag>::init();
template void HookedProduct<HookedTag>::cleanup();

template<typename>
struct PlainProduct {};

using Hooked = HookedProduct<HookedTag>;
using Plain = PlainProduct<PlainTag>;

struct [[=ctr::factory{}]] Factory {
    [[=ctr::singleton{}]]
    [[=ctr::named{.name = std::define_static_string("hooked")}]]
    Hooked makeHooked() {
        return Hooked{};
    }

    [[=ctr::singleton{}]]
    [[=ctr::named{.name = std::define_static_string("plain")}]]
    Plain makePlain() {
        return Plain{};
    }
};

} // namespace factory_product_hook_fixture

TEST(Factory, FactoryProductHooksApplyOnlyWhenProductDeclaresCtoriumHooks) {
    factory_product_hook_fixture::postConstructCount.store(0, std::memory_order_relaxed);
    factory_product_hook_fixture::preDestroyCount.store(0, std::memory_order_relaxed);

    auto& ctx = ctr::BeanContext::resolveContext("ft-product-hooks");
    ctx.discover<^^factory_product_hook_fixture>().start();

    {
        auto hooked = ctx.resolve<factory_product_hook_fixture::Hooked>(
            ctr::named{.name = std::define_static_string("hooked")});
        auto plain = ctx.resolve<factory_product_hook_fixture::Plain>(
            ctr::named{.name = std::define_static_string("plain")});
        EXPECT_NE(hooked.operator->(), nullptr);
        EXPECT_NE(plain.operator->(), nullptr);
        EXPECT_EQ(
            factory_product_hook_fixture::postConstructCount.load(std::memory_order_relaxed),
            1);
    }

    ctx.stop();

    EXPECT_EQ(
        factory_product_hook_fixture::preDestroyCount.load(std::memory_order_relaxed),
        1);
}

namespace factory_product_lifetime_fixture {

struct PrototypeTag {};
struct SessionTag {};

template<typename>
struct PrototypeProduct {};

template<typename>
struct SessionProduct {};

using Prototype = PrototypeProduct<PrototypeTag>;
using Session = SessionProduct<SessionTag>;

struct [[=ctr::factory{}]] Factory {
    [[=ctr::prototype{}]]
    Prototype makePrototype() {
        return Prototype{};
    }

    [[=ctr::session{}]]
    Session makeSession() {
        return Session{};
    }
};

} // namespace factory_product_lifetime_fixture

TEST(Factory, PrototypeProducerCreatesDistinctInstances) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-prototype-product");
    ctx.discover<^^factory_product_lifetime_fixture>().start();

    auto first = ctx.resolve<factory_product_lifetime_fixture::Prototype>();
    auto second = ctx.resolve<factory_product_lifetime_fixture::Prototype>();

    EXPECT_NE(first.operator->(), nullptr);
    EXPECT_NE(second.operator->(), nullptr);
    EXPECT_NE(first.operator->(), second.operator->());

    ctx.stop();
}

TEST(Factory, SessionProducerCreatesOneInstancePerScope) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-session-product");
    ctx.discover<^^factory_product_lifetime_fixture>().start();
    auto& firstScope = ctx.resolveScope("factory-session-a");
    auto& secondScope = ctx.resolveScope("factory-session-b");
    firstScope.start();
    secondScope.start();

    auto firstA = firstScope.resolve<factory_product_lifetime_fixture::Session>();
    auto firstB = firstScope.resolve<factory_product_lifetime_fixture::Session>();
    auto second = secondScope.resolve<factory_product_lifetime_fixture::Session>();

    EXPECT_NE(firstA.operator->(), nullptr);
    EXPECT_EQ(firstA.operator->(), firstB.operator->());
    EXPECT_NE(firstA.operator->(), second.operator->());

    ctx.stop();
}

namespace factory_product_listener_fixture {

struct ProductTag {};

template<typename>
struct Product {};

using ListenerProduct = Product<ProductTag>;

struct [[=ctr::factory{}]] Factory {
    [[=ctr::prototype{}]]
    ListenerProduct make() {
        return ListenerProduct{};
    }
};

} // namespace factory_product_listener_fixture

TEST(Factory, FactoryProductListenerPhasesFireInStandardOrder) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-product-listener-phases");
    ctx.discover<^^factory_product_listener_fixture>().start();

    int phase = 0;
    int initialized = 0;
    int created = 0;
    int preDestroy = 0;
    int destroyed = 0;

    ctx.on<factory_product_listener_fixture::ListenerProduct>(
        ctr::onInitialized,
        [&](const ctr::Bean<factory_product_listener_fixture::ListenerProduct>&) {
            initialized = ++phase;
        });
    ctx.on<factory_product_listener_fixture::ListenerProduct>(
        ctr::onCreated,
        [&](const ctr::Bean<factory_product_listener_fixture::ListenerProduct>&) {
            created = ++phase;
        });
    ctx.on<factory_product_listener_fixture::ListenerProduct>(
        ctr::onPreDestroy,
        [&](const ctr::Bean<factory_product_listener_fixture::ListenerProduct>&) {
            preDestroy = ++phase;
        });
    ctx.on<factory_product_listener_fixture::ListenerProduct>(
        ctr::onDestroyed,
        [&](const ctr::Bean<factory_product_listener_fixture::ListenerProduct>&) {
            destroyed = ++phase;
        });

    {
        auto product = ctx.resolve<factory_product_listener_fixture::ListenerProduct>();
        EXPECT_NE(product.operator->(), nullptr);
        EXPECT_EQ(initialized, 1);
        EXPECT_EQ(created, 2);
    }

    EXPECT_EQ(preDestroy, 3);
    EXPECT_EQ(destroyed, 4);

    ctx.stop();
}

namespace factory_product_metadata_fixture {

struct ProductTag {};

template<typename>
struct Product {};

using MetadataProduct = Product<ProductTag>;

struct [[=ctr::singleton{}]] OrdinaryBean {};

struct [[=ctr::factory{}]] Factory {
    [[=ctr::singleton{}]]
    MetadataProduct make() {
        return MetadataProduct{};
    }
};

} // namespace factory_product_metadata_fixture

TEST(Factory, FactoryProductMetadataOriginIsFactoryProduct) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-product-metadata-origin");
    ctx.discover<^^factory_product_metadata_fixture>().start();

    auto product = ctx.resolve<factory_product_metadata_fixture::MetadataProduct>();

    EXPECT_EQ(product.metadata().origin(), ctr::detail::Origin::FactoryProduct);

    ctx.stop();
}

TEST(Factory, FactoryProductMetadataFactoryMethodIsNotInPublicApi) {
    auto& ctx = ctr::BeanContext::resolveContext("ft-product-metadata-factory-method");
    ctx.discover<^^factory_product_metadata_fixture>().start();

    auto product = ctx.resolve<factory_product_metadata_fixture::MetadataProduct>();
    auto ordinary = ctx.resolve<factory_product_metadata_fixture::OrdinaryBean>();

    EXPECT_EQ(product.metadata().origin(), ctr::detail::Origin::FactoryProduct);
    EXPECT_EQ(product.metadata().factoryMethod(), std::string_view{"make"});
    EXPECT_EQ(ordinary.metadata().origin(), ctr::detail::Origin::AnnotatedType);
    EXPECT_TRUE(ordinary.metadata().factoryMethod().empty());

    ctx.stop();
}
