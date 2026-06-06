#include <atomic>
#include <memory>
#include <meta>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace factory_rt_fixture {

struct ProductTag {};

template<typename>
struct Product {
    int x = 42;
};

using RuntimeProduct = Product<ProductTag>;

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]] RuntimeProduct make() {
        ++makeCallCount;
        return RuntimeProduct{};
    }

    static inline std::atomic<int> makeCallCount{0};
};

} // namespace factory_rt_fixture

// ─── Factory runtime tests ────────────────────────────────────────────────────

TEST(Factory, FactoryProducedBeanResolvable) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-resolve");
    ctx.discover<^^factory_rt_fixture>().start();
    auto bean = ctx.resolve<factory_rt_fixture::RuntimeProduct>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(Factory, FactoryProducedBeanIsSingleton) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-singleton");
    ctx.discover<^^factory_rt_fixture>().start();
    auto b1 = ctx.resolve<factory_rt_fixture::RuntimeProduct>();
    auto b2 = ctx.resolve<factory_rt_fixture::RuntimeProduct>();
    EXPECT_EQ(b1.operator->(), b2.operator->());
    ctx.stop();
}

TEST(Factory, FactoryMethodCalledExactlyOnce) {
    factory_rt_fixture::Factory::makeCallCount.store(0, std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-make-once");
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

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void init() {
        postConstructCount.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace factory_singleton_handle_fixture

TEST(Factory, ResolveFactoryReturnsSingletonHandle) {
    factory_singleton_handle_fixture::postConstructCount.store(0, std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-factory-singleton-handle");
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

struct [[=CTORIUM_NAMESPACE::singleton{}]] Dep {
    int value = 7;
};

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    CTORIUM_NAMESPACE::Bean<Dep> dep;
    explicit Factory(CTORIUM_NAMESPACE::Bean<Dep> d) : dep(std::move(d)) {}
};

} // namespace factory_constructor_injection_fixture

TEST(Factory, FactoryConstructorReceivesCtoriumDependency) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-factory-constructor-injection");
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

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("value")}]]
    ReturnFormsProduct makeValue() {
        return ReturnFormsProduct{.value = 1};
    }

    [[=CTORIUM_NAMESPACE::prototype{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("unique")}]]
    std::unique_ptr<UniquePtrProduct> makeUnique() {
        return std::make_unique<UniquePtrProduct>(UniquePtrProduct{.value = 2});
    }
};

} // namespace factory_return_forms_fixture

TEST(Factory, ValueProducerReturnFormIsResolvableByName) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-value-return-form");
    ctx.discover<^^factory_return_forms_fixture>().start();

    auto valueProduct = ctx.resolve<factory_return_forms_fixture::ReturnFormsProduct>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("value")});

    ASSERT_NE(valueProduct.operator->(), nullptr);
    EXPECT_EQ(valueProduct->value, 1);

    ctx.stop();
}

TEST(Factory, UniquePtrProducerReturnFormIsResolvableByName) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-unique-ptr-return-form");
    ctx.discover<^^factory_return_forms_fixture>().start();

    auto first = ctx.resolve<factory_return_forms_fixture::UniquePtrProduct>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("unique")});
    auto second = ctx.resolve<factory_return_forms_fixture::UniquePtrProduct>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("unique")});

    ASSERT_NE(first.operator->(), nullptr);
    ASSERT_NE(second.operator->(), nullptr);
    EXPECT_EQ(first->value, 2);
    EXPECT_EQ(first.metadata().exactType(), typeid(factory_return_forms_fixture::UniquePtrProduct));
    EXPECT_NE(first.operator->(), second.operator->());

    ctx.stop();
}

namespace factory_unique_ptr_non_movable_fixture {

struct NonMovable {
    NonMovable() = default;
    NonMovable(NonMovable&&) = delete;
    NonMovable(const NonMovable&) = delete;
    int marker = 42;
};
static_assert(!std::is_move_constructible_v<NonMovable>);

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::prototype{}]]
    std::unique_ptr<NonMovable> makeNonMovable() {
        return std::make_unique<NonMovable>();
    }
};

} // namespace factory_unique_ptr_non_movable_fixture

TEST(Factory, UniquePtrNonMovableProductIsResolvable) {
    // Regression: issue #20 — unique_ptr<T> factory products must not require
    // T to be move-constructible. Without the fix, this test fails to compile.
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-unique-ptr-non-movable");
    ctx.discover<^^factory_unique_ptr_non_movable_fixture>().start();

    auto product = ctx.resolve<factory_unique_ptr_non_movable_fixture::NonMovable>();

    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(product->marker, 42);

    ctx.stop();
}

namespace factory_product_param_injection_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Dep {
    int value = 11;
};

struct NamedDep {
    int value = 0;
};

struct ScopedDep {
    int value = 0;
};

struct ValueProduct {
    Dep* dep = nullptr;
    int value = 0;
};

struct UniqueProduct {
    Dep* dep = nullptr;
    int value = 0;
};

struct NamedProduct {
    NamedDep* dep = nullptr;
    int value = 0;
};

struct ScopedProduct {
    CTORIUM_NAMESPACE::Bean<ScopedDep> dep;
};

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    ValueProduct makeValue(CTORIUM_NAMESPACE::Bean<Dep> dep) {
        return ValueProduct{.dep = dep.operator->(), .value = dep->value};
    }

    [[=CTORIUM_NAMESPACE::prototype{}]]
    std::unique_ptr<UniqueProduct> makeUnique(CTORIUM_NAMESPACE::Bean<Dep> dep) {
        return std::make_unique<UniqueProduct>(
            UniqueProduct{.dep = dep.operator->(), .value = dep->value});
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("primary")}]]
    NamedDep makePrimaryNamed() {
        return NamedDep{.value = 21};
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("secondary")}]]
    NamedDep makeSecondaryNamed() {
        return NamedDep{.value = 22};
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    NamedProduct makeNamed(
        [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("secondary")}]]
        CTORIUM_NAMESPACE::Bean<NamedDep> dep) {
        return NamedProduct{.dep = dep.operator->(), .value = dep->value};
    }

    [[=CTORIUM_NAMESPACE::session{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("primary")}]]
    ScopedDep makePrimaryScoped() {
        return ScopedDep{.value = 31};
    }

    [[=CTORIUM_NAMESPACE::session{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("secondary")}]]
    ScopedDep makeSecondaryScoped() {
        return ScopedDep{.value = 32};
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    ScopedProduct makeScopedNamed(
        [[=CTORIUM_NAMESPACE::scoped{.name = std::define_static_string("factory-product-param-scope")}]]
        [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("primary")}]]
        CTORIUM_NAMESPACE::Bean<ScopedDep> dep) {
        return ScopedProduct{.dep = std::move(dep)};
    }
};

} // namespace factory_product_param_injection_fixture

TEST(Factory, ValueProducerReceivesInjectedParameter) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-value-producer-param");
    ctx.discover<^^factory_product_param_injection_fixture>().start();

    auto product = ctx.resolve<factory_product_param_injection_fixture::ValueProduct>();
    auto dep = ctx.resolve<factory_product_param_injection_fixture::Dep>();

    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(product->dep, dep.operator->());
    EXPECT_EQ(product->value, 11);

    ctx.stop();
}

TEST(Factory, UniquePtrProducerReceivesInjectedParameter) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-unique-producer-param");
    ctx.discover<^^factory_product_param_injection_fixture>().start();

    auto first = ctx.resolve<factory_product_param_injection_fixture::UniqueProduct>();
    auto second = ctx.resolve<factory_product_param_injection_fixture::UniqueProduct>();
    auto dep = ctx.resolve<factory_product_param_injection_fixture::Dep>();

    ASSERT_NE(first.operator->(), nullptr);
    ASSERT_NE(second.operator->(), nullptr);
    EXPECT_EQ(first->dep, dep.operator->());
    EXPECT_EQ(second->dep, dep.operator->());
    EXPECT_EQ(first->value, 11);
    EXPECT_NE(first.operator->(), second.operator->());

    ctx.stop();
}

TEST(Factory, NamedProducerParameterSelectsNamedCandidate) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-named-producer-param");
    ctx.discover<^^factory_product_param_injection_fixture>().start();

    auto product = ctx.resolve<factory_product_param_injection_fixture::NamedProduct>();
    auto primary = ctx.resolve<factory_product_param_injection_fixture::NamedDep>(
        CTORIUM_NAMESPACE::named{"primary"});
    auto secondary = ctx.resolve<factory_product_param_injection_fixture::NamedDep>(
        CTORIUM_NAMESPACE::named{"secondary"});

    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(product->dep, secondary.operator->());
    EXPECT_NE(product->dep, primary.operator->());
    EXPECT_EQ(product->value, 22);

    ctx.stop();
}

TEST(Factory, ScopedNamedProducerParameterUsesDeferredScopedHandle) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-scoped-named-producer-param");
    ctx.discover<^^factory_product_param_injection_fixture>().start();
    auto& scope = ctx.resolveScope("factory-product-param-scope");
    scope.start();

    auto product = ctx.resolve<factory_product_param_injection_fixture::ScopedProduct>();
    auto primary = scope.resolve<factory_product_param_injection_fixture::ScopedDep>(
        CTORIUM_NAMESPACE::named{"primary"});
    auto secondary = scope.resolve<factory_product_param_injection_fixture::ScopedDep>(
        CTORIUM_NAMESPACE::named{"secondary"});

    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(product->dep.operator->(), primary.operator->());
    EXPECT_NE(product->dep.operator->(), secondary.operator->());
    EXPECT_EQ(product->dep->value, 31);

    ctx.stop();
}

// Defined outside the discovery namespace so it is not enumerated as a standalone AnnotatedType
// when discover<^^factory_product_hook_fixture>() is called.
namespace factory_product_hook_bean {

std::atomic<int> postConstructCount{0};
std::atomic<int> preDestroyCount{0};

struct [[=CTORIUM_NAMESPACE::singleton{}]] BeanHookedProduct {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void init();

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void cleanup();
};

void BeanHookedProduct::init() {
    postConstructCount.fetch_add(1, std::memory_order_relaxed);
}

void BeanHookedProduct::cleanup() {
    preDestroyCount.fetch_add(1, std::memory_order_relaxed);
}

} // namespace factory_product_hook_bean

namespace factory_product_hook_fixture {

std::atomic<int> postConstructCount{0};
std::atomic<int> preDestroyCount{0};

struct HookedTag {};
struct PlainTag {};

template<typename>
struct HookedProduct {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void init();

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
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

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("hooked")}]]
    Hooked makeHooked() {
        return Hooked{};
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("plain")}]]
    Plain makePlain() {
        return Plain{};
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("bean-hooked")}]]
    factory_product_hook_bean::BeanHookedProduct makeBeanHooked() {
        return factory_product_hook_bean::BeanHookedProduct{};
    }
};

} // namespace factory_product_hook_fixture

TEST(Factory, FactoryProductHooksExecuteOnlyWhenProductIsABean) {
    factory_product_hook_fixture::postConstructCount.store(0, std::memory_order_relaxed);
    factory_product_hook_fixture::preDestroyCount.store(0, std::memory_order_relaxed);
    factory_product_hook_bean::postConstructCount.store(0, std::memory_order_relaxed);
    factory_product_hook_bean::preDestroyCount.store(0, std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-product-hooks");
    ctx.discover<^^factory_product_hook_fixture>().start();

    {
        auto hooked = ctx.resolve<factory_product_hook_fixture::Hooked>(
            CTORIUM_NAMESPACE::named{.name = std::define_static_string("hooked")});
        auto plain = ctx.resolve<factory_product_hook_fixture::Plain>(
            CTORIUM_NAMESPACE::named{.name = std::define_static_string("plain")});
        auto beanHooked = ctx.resolve<factory_product_hook_bean::BeanHookedProduct>(
            CTORIUM_NAMESPACE::named{.name = std::define_static_string("bean-hooked")});
        EXPECT_NE(hooked.operator->(), nullptr);
        EXPECT_NE(plain.operator->(), nullptr);
        EXPECT_NE(beanHooked.operator->(), nullptr);
        // Non-Bean product: hooks suppressed despite annotations
        EXPECT_EQ(
            factory_product_hook_fixture::postConstructCount.load(std::memory_order_relaxed),
            0);
        // Bean product: postConstruct executed
        EXPECT_EQ(
            factory_product_hook_bean::postConstructCount.load(std::memory_order_relaxed),
            1);
    }

    ctx.stop();

    // Non-Bean product: preDestroy suppressed
    EXPECT_EQ(
        factory_product_hook_fixture::preDestroyCount.load(std::memory_order_relaxed),
        0);
    // Bean product: preDestroy executed
    EXPECT_EQ(
        factory_product_hook_bean::preDestroyCount.load(std::memory_order_relaxed),
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

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::prototype{}]]
    Prototype makePrototype() {
        return Prototype{};
    }

    [[=CTORIUM_NAMESPACE::session{}]]
    Session makeSession() {
        return Session{};
    }
};

} // namespace factory_product_lifetime_fixture

TEST(Factory, PrototypeProducerCreatesDistinctInstances) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-prototype-product");
    ctx.discover<^^factory_product_lifetime_fixture>().start();

    auto first = ctx.resolve<factory_product_lifetime_fixture::Prototype>();
    auto second = ctx.resolve<factory_product_lifetime_fixture::Prototype>();

    EXPECT_NE(first.operator->(), nullptr);
    EXPECT_NE(second.operator->(), nullptr);
    EXPECT_NE(first.operator->(), second.operator->());

    ctx.stop();
}

TEST(Factory, SessionProducerCreatesOneInstancePerScope) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-session-product");
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

namespace factory_threadlocal_product_fixture {

struct ProductTag {};

template<typename>
struct Product {
    std::thread::id createdOnThread;
};

using ThreadLocalProduct = Product<ProductTag>;

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::threadLocal{}]]
    ThreadLocalProduct make() {
        makeCallCount.fetch_add(1, std::memory_order_relaxed);
        return ThreadLocalProduct{.createdOnThread = std::this_thread::get_id()};
    }

    static inline std::atomic<int> makeCallCount{0};
};

} // namespace factory_threadlocal_product_fixture

TEST(Factory, ThreadLocalProducerCreatesDistinctInstancesPerThread) {
    using Product = factory_threadlocal_product_fixture::ThreadLocalProduct;

    factory_threadlocal_product_fixture::Factory::makeCallCount.store(
        0,
        std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-threadlocal-product");
    ctx.discover<^^factory_threadlocal_product_fixture>().start();

    auto first = ctx.resolve<Product>();
    auto second = ctx.resolve<Product>();

    ASSERT_NE(first.operator->(), nullptr);
    ASSERT_NE(second.operator->(), nullptr);
    EXPECT_EQ(first.operator->(), second.operator->());
    EXPECT_EQ(first->createdOnThread, std::this_thread::get_id());
    EXPECT_EQ(
        factory_threadlocal_product_fixture::Factory::makeCallCount.load(
            std::memory_order_relaxed),
        1);

    Product* workerPtr = nullptr;
    std::thread::id workerThreadId;
    std::thread::id workerCreatedOnThread;
    std::thread worker([&ctx, &workerPtr, &workerThreadId, &workerCreatedOnThread] {
        workerThreadId = std::this_thread::get_id();
        auto workerProduct = ctx.resolve<Product>();
        workerPtr = workerProduct.operator->();
        workerCreatedOnThread = workerPtr->createdOnThread;
    });
    worker.join();

    ASSERT_NE(workerPtr, nullptr);
    EXPECT_NE(first.operator->(), workerPtr);
    EXPECT_EQ(workerCreatedOnThread, workerThreadId);
    EXPECT_EQ(
        factory_threadlocal_product_fixture::Factory::makeCallCount.load(
            std::memory_order_relaxed),
        2);

    ctx.stop();
}

TEST(Factory, ThreadLocalProducerMetadataReportsThreadLocalLifetime) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-threadlocal-product-metadata");
    ctx.discover<^^factory_threadlocal_product_fixture>().start();

    auto product = ctx.resolve<factory_threadlocal_product_fixture::ThreadLocalProduct>();

    EXPECT_EQ(product.metadata().lifetime(), CTORIUM_NAMESPACE::Lifetime::ThreadLocal);
    EXPECT_EQ(product.metadata().origin(), CTORIUM_NAMESPACE::Origin::FactoryProduct);

    ctx.stop();
}

namespace factory_session_product_fixture {

struct ProductTag {};

template<typename>
struct Product {
    int value = 42;
};

using SessionProduct = Product<ProductTag>;

std::atomic<int> destroyedCount{0};

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::session{}]]
    SessionProduct make() {
        makeCallCount.fetch_add(1, std::memory_order_relaxed);
        return SessionProduct{};
    }

    static inline std::atomic<int> makeCallCount{0};
};

} // namespace factory_session_product_fixture

TEST(Factory, SessionFactoryProductResolveFromStartedScopeReturnsInstance) {
    factory_session_product_fixture::Factory::makeCallCount.store(
        0,
        std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-session-factory-product-resolve");
    ctx.discover<^^factory_session_product_fixture>().start();
    auto& scope = ctx.resolveScope("session-factory-product-resolve");
    scope.start();

    auto product = scope.resolve<factory_session_product_fixture::SessionProduct>();

    EXPECT_NE(product.operator->(), nullptr);
    EXPECT_EQ(
        factory_session_product_fixture::Factory::makeCallCount.load(
            std::memory_order_relaxed),
        1);

    ctx.stop();
}

TEST(Factory, SessionFactoryProductSameScopeReturnsSameInstance) {
    factory_session_product_fixture::Factory::makeCallCount.store(
        0,
        std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-session-factory-product-same-scope");
    ctx.discover<^^factory_session_product_fixture>().start();
    auto& scope = ctx.resolveScope("session-factory-product-same-scope");
    scope.start();

    auto first = scope.resolve<factory_session_product_fixture::SessionProduct>();
    auto second = scope.resolve<factory_session_product_fixture::SessionProduct>();

    ASSERT_NE(first.operator->(), nullptr);
    EXPECT_EQ(first.operator->(), second.operator->());
    EXPECT_EQ(
        factory_session_product_fixture::Factory::makeCallCount.load(
            std::memory_order_relaxed),
        1);

    ctx.stop();
}

TEST(Factory, SessionFactoryProductDistinctScopesReturnDistinctInstances) {
    factory_session_product_fixture::Factory::makeCallCount.store(
        0,
        std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-session-factory-product-distinct-scopes");
    ctx.discover<^^factory_session_product_fixture>().start();
    auto& firstScope = ctx.resolveScope("session-factory-product-first");
    auto& secondScope = ctx.resolveScope("session-factory-product-second");
    firstScope.start();
    secondScope.start();

    auto first = firstScope.resolve<factory_session_product_fixture::SessionProduct>();
    auto second = secondScope.resolve<factory_session_product_fixture::SessionProduct>();

    ASSERT_NE(first.operator->(), nullptr);
    ASSERT_NE(second.operator->(), nullptr);
    EXPECT_NE(first.operator->(), second.operator->());
    EXPECT_EQ(
        factory_session_product_fixture::Factory::makeCallCount.load(
            std::memory_order_relaxed),
        2);

    ctx.stop();
}

TEST(Factory, SessionFactoryProductResolveFromRootRaisesContextStateError) {
    factory_session_product_fixture::Factory::makeCallCount.store(
        0,
        std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-session-factory-product-root-resolve");
    ctx.discover<^^factory_session_product_fixture>().start();

    EXPECT_THROW(
        ctx.resolve<factory_session_product_fixture::SessionProduct>(),
        CTORIUM_NAMESPACE::ContextStateError);
    EXPECT_EQ(
        factory_session_product_fixture::Factory::makeCallCount.load(
            std::memory_order_relaxed),
        0);

    ctx.stop();
}

TEST(Factory, SessionFactoryProductScopeStopDestroysInstance) {
    factory_session_product_fixture::Factory::makeCallCount.store(
        0,
        std::memory_order_relaxed);
    factory_session_product_fixture::destroyedCount.store(
        0,
        std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-session-factory-product-stop-destroys");
    ctx.discover<^^factory_session_product_fixture>().start();
    ctx.on<factory_session_product_fixture::SessionProduct>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<factory_session_product_fixture::SessionProduct>&) {
            factory_session_product_fixture::destroyedCount.fetch_add(
                1,
                std::memory_order_relaxed);
        });

    auto& scope = ctx.resolveScope("session-factory-product-stop-destroys");
    scope.start();

    auto product = scope.resolve<factory_session_product_fixture::SessionProduct>();
    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(
        factory_session_product_fixture::destroyedCount.load(std::memory_order_relaxed),
        0);

    scope.stop();

    EXPECT_EQ(product.operator->(), nullptr);
    EXPECT_EQ(
        factory_session_product_fixture::destroyedCount.load(std::memory_order_relaxed),
        1);
    EXPECT_EQ(
        factory_session_product_fixture::Factory::makeCallCount.load(
            std::memory_order_relaxed),
        1);

    ctx.stop();
}

namespace factory_product_listener_fixture {

struct ProductTag {};

template<typename>
struct Product {};

using ListenerProduct = Product<ProductTag>;

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::prototype{}]]
    ListenerProduct make() {
        return ListenerProduct{};
    }
};

} // namespace factory_product_listener_fixture

TEST(Factory, FactoryProductListenerPhasesFireInStandardOrder) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-product-listener-phases");
    ctx.discover<^^factory_product_listener_fixture>().start();

    int phase = 0;
    int initialized = 0;
    int created = 0;
    int preDestroy = 0;
    int destroyed = 0;

    ctx.on<factory_product_listener_fixture::ListenerProduct>(
        CTORIUM_NAMESPACE::onInitialized,
        [&](const CTORIUM_NAMESPACE::Bean<factory_product_listener_fixture::ListenerProduct>&) {
            initialized = ++phase;
        });
    ctx.on<factory_product_listener_fixture::ListenerProduct>(
        CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<factory_product_listener_fixture::ListenerProduct>&) {
            created = ++phase;
        });
    ctx.on<factory_product_listener_fixture::ListenerProduct>(
        CTORIUM_NAMESPACE::onPreDestroy,
        [&](const CTORIUM_NAMESPACE::Bean<factory_product_listener_fixture::ListenerProduct>&) {
            preDestroy = ++phase;
        });
    ctx.on<factory_product_listener_fixture::ListenerProduct>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<factory_product_listener_fixture::ListenerProduct>&) {
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

struct [[=CTORIUM_NAMESPACE::singleton{}]] OrdinaryBean {};

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    MetadataProduct make() {
        return MetadataProduct{};
    }
};

} // namespace factory_product_metadata_fixture

TEST(Factory, FactoryProductMetadataOriginIsFactoryProduct) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-product-metadata-origin");
    ctx.discover<^^factory_product_metadata_fixture>().start();

    auto product = ctx.resolve<factory_product_metadata_fixture::MetadataProduct>();

    EXPECT_EQ(product.metadata().origin(), CTORIUM_NAMESPACE::Origin::FactoryProduct);

    ctx.stop();
}

TEST(Factory, FactoryProductMetadataFactoryMethodIsNotInPublicApi) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-product-metadata-factory-method");
    ctx.discover<^^factory_product_metadata_fixture>().start();

    auto product = ctx.resolve<factory_product_metadata_fixture::MetadataProduct>();
    auto ordinary = ctx.resolve<factory_product_metadata_fixture::OrdinaryBean>();

    EXPECT_EQ(product.metadata().origin(), CTORIUM_NAMESPACE::Origin::FactoryProduct);
    EXPECT_EQ(product.metadata().factoryMethod(), std::string_view{"make"});
    EXPECT_EQ(ordinary.metadata().origin(), CTORIUM_NAMESPACE::Origin::AnnotatedType);
    EXPECT_TRUE(ordinary.metadata().factoryMethod().empty());

    ctx.stop();
}

// ─── Issue #19: external product types must not be scanned via scanMembers ───
//
// A factory producer returning an unannotated type (no ctr:: lifetime marker)
// must compile and resolve even when that type has constructors with non-template
// class-type parameters.  Before the fix, scanMembers was called unconditionally;
// isBeanType then called template_arguments_of on a non-template class (ExternalDep),
// which threw inside a consteval context and caused a compile error.

namespace factory_external_product_fixture {

// Non-template, unannotated class used as a constructor parameter.
// This is the trigger: isBeanType(^^ExternalDep) calls template_arguments_of,
// which throws because ExternalDep is not a class-template specialization.
struct ExternalDep {};

// External product returned by value — no Ctorium annotation on the type.
struct ExternalByValue {
    int marker = 1;
    explicit ExternalByValue(ExternalDep /*dep*/) {}
};

// External product returned by unique_ptr — mirrors the grpc::Service scenario.
struct ExternalByUniquePtr {
    int marker = 2;
    explicit ExternalByUniquePtr(ExternalDep /*dep*/) {}
};

// External product with a default constructor — simpler variant also covered.
struct ExternalDefaultCtor {
    int marker = 3;
};

struct [[=CTORIUM_NAMESPACE::factory{}]] ExternalFactory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("by-value")}]]
    ExternalByValue makeByValue() {
        return ExternalByValue{ExternalDep{}};
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("by-unique-ptr")}]]
    std::unique_ptr<ExternalByUniquePtr> makeByUniquePtr() {
        return std::make_unique<ExternalByUniquePtr>(ExternalDep{});
    }

    [[=CTORIUM_NAMESPACE::prototype{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("default-ctor")}]]
    ExternalDefaultCtor makeDefaultCtor() {
        return ExternalDefaultCtor{};
    }
};

} // namespace factory_external_product_fixture

// Regression test: discovery of a factory whose products include unannotated
// external types must compile without errors (issue #19).
TEST(Factory, ExternalProductByValueIsDiscoverableAndResolvable) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-external-by-value");
    ctx.discover<^^factory_external_product_fixture>().start();

    auto product = ctx.resolve<factory_external_product_fixture::ExternalByValue>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("by-value")});

    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(product->marker, 1);

    ctx.stop();
}

// Regression test: unique_ptr<ExternalType> factory product — the exact scenario
// described in issue #19 (analogue of std::unique_ptr<grpc::Service>).
TEST(Factory, ExternalProductByUniquePtrIsDiscoverableAndResolvable) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-external-by-unique-ptr");
    ctx.discover<^^factory_external_product_fixture>().start();

    auto product = ctx.resolve<factory_external_product_fixture::ExternalByUniquePtr>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("by-unique-ptr")});

    ASSERT_NE(product.operator->(), nullptr);
    EXPECT_EQ(product->marker, 2);
    EXPECT_EQ(product.metadata().origin(), CTORIUM_NAMESPACE::Origin::FactoryProduct);

    ctx.stop();
}

// External product resolves as singleton: same instance on repeated resolves.
TEST(Factory, ExternalProductByUniquePtrIsSingleton) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-external-unique-ptr-singleton");
    ctx.discover<^^factory_external_product_fixture>().start();

    auto first = ctx.resolve<factory_external_product_fixture::ExternalByUniquePtr>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("by-unique-ptr")});
    auto second = ctx.resolve<factory_external_product_fixture::ExternalByUniquePtr>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("by-unique-ptr")});

    ASSERT_NE(first.operator->(), nullptr);
    EXPECT_EQ(first.operator->(), second.operator->());

    ctx.stop();
}

// External product with default constructor also covered (simpler path).
TEST(Factory, ExternalProductWithDefaultCtorIsDiscoverableAndResolvable) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ft-external-default-ctor");
    ctx.discover<^^factory_external_product_fixture>().start();

    auto first = ctx.resolve<factory_external_product_fixture::ExternalDefaultCtor>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("default-ctor")});
    auto second = ctx.resolve<factory_external_product_fixture::ExternalDefaultCtor>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("default-ctor")});

    ASSERT_NE(first.operator->(), nullptr);
    EXPECT_EQ(first->marker, 3);
    EXPECT_NE(first.operator->(), second.operator->());

    ctx.stop();
}
