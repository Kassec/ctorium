#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_bind_fixture {

struct Item {
    int value = 0;
};

struct OtherItem {
    int value = 0;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] HookedSingleton {
    inline static int postConstructCount = 0;
    inline static int preDestroyCount = 0;

    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize() { ++postConstructCount; }
    [[=CTORIUM_NAMESPACE::preDestroy{}]] void shutdown() { ++preDestroyCount; }
};

struct PlainWithSameShape {
    inline static int destructorCount = 0;
    ~PlainWithSameShape() { ++destructorCount; }
};

struct [[=CTORIUM_NAMESPACE::session{}]] HookedSession {
    inline static int postConstructCount = 0;
    inline static int preDestroyCount = 0;

    int value = 0;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize() { ++postConstructCount; }
    [[=CTORIUM_NAMESPACE::preDestroy{}]] void shutdown() { ++preDestroyCount; }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ConcurrentSingleton {};

} // namespace contract_bind_fixture

namespace contract_bind_contracts {

template <class Context, class T>
concept BindSingletonReturnsBeanContextRef = requires(Context& context, std::unique_ptr<T> object) {
    { context.template bindSingleton<T>(std::move(object)) } -> std::same_as<CTORIUM_NAMESPACE::BeanContext&>;
    { context.template bindSingleton<T>(std::move(object), CTORIUM_NAMESPACE::BindOptions{}) }
        -> std::same_as<CTORIUM_NAMESPACE::BeanContext&>;
};

template <class Context, class T>
concept BindSessionAvailable = requires(Context& context, std::unique_ptr<T> object) {
    context.template bindSession<T>(std::move(object));
};

template <class Context, class T>
concept BindSessionReturnsScopedContextRef = requires(Context& context, std::unique_ptr<T> object) {
    { context.template bindSession<T>(std::move(object)) } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext&>;
    { context.template bindSession<T>(std::move(object), CTORIUM_NAMESPACE::BindOptions{}) }
        -> std::same_as<CTORIUM_NAMESPACE::ScopedContext&>;
};

} // namespace contract_bind_contracts

TEST(ContractBind, BindSingleton_ReturnsBeanContextRef) {
    EXPECT_TRUE((contract_bind_contracts::BindSingletonReturnsBeanContextRef<
        CTORIUM_NAMESPACE::BeanContext,
        contract_bind_fixture::Item>));
    EXPECT_TRUE((contract_bind_contracts::BindSingletonReturnsBeanContextRef<
        CTORIUM_NAMESPACE::ScopedContext,
        contract_bind_fixture::Item>));
}

TEST(ContractBind, BindSingleton_DuplicateKeyPreStart_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-dup-pre");
    context.bindSingleton<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>());
    EXPECT_THROW(
        context.bindSingleton<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>()),
        CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractBind, BindSingleton_DuplicateKeyPostStart_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-dup-post");
    context.start();
    context.bindSingleton<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>());
    EXPECT_THROW(
        context.bindSingleton<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>()),
        CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractBind, BindSingleton_SameTypeDifferentNames_Allowed) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-names");
    context.bindSingleton<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 1}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("first")});
    context.bindSingleton<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 2}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("second")});
    context.start();

    auto first = context.resolve<contract_bind_fixture::Item>(CTORIUM_NAMESPACE::named{"first"});
    auto second = context.resolve<contract_bind_fixture::Item>(CTORIUM_NAMESPACE::named{"second"});
    ASSERT_NE(first.operator->(), nullptr);
    ASSERT_NE(second.operator->(), nullptr);
    EXPECT_EQ(first->value, 1);
    EXPECT_EQ(second->value, 2);
    EXPECT_NE(first.operator->(), second.operator->());

    context.stop();
}

TEST(ContractBind, BindSingleton_SameNameDifferentPriorities_Allowed) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-priorities");
    context.bindSingleton<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 5}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("same"), .priority = 5});
    context.bindSingleton<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 10}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("same"), .priority = 10});
    context.start();

    auto selected = context.resolve<contract_bind_fixture::Item>(CTORIUM_NAMESPACE::named{"same"});
    ASSERT_NE(selected.operator->(), nullptr);
    EXPECT_EQ(selected->value, 10);

    context.stop();
}

TEST(ContractBind, BindSingleton_SameNameDifferentPriorities_PostStart_Allowed) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-priorities-post");
    context.start();
    context.bindSingleton<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 5}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("same"), .priority = 5});
    context.bindSingleton<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 10}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("same"), .priority = 10});

    auto selected = context.resolve<contract_bind_fixture::Item>(CTORIUM_NAMESPACE::named{"same"});
    ASSERT_NE(selected.operator->(), nullptr);
    EXPECT_EQ(selected->value, 10);

    context.stop();
}

TEST(ContractBind, BindSingleton_PreStart_ResolveAfterStart) {
    auto object = std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 7});
    auto* raw = object.get();
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-pre-resolve");
    context.bindSingleton<contract_bind_fixture::Item>(std::move(object));
    context.start();

    auto resolved = context.resolve<contract_bind_fixture::Item>();
    EXPECT_EQ(resolved.operator->(), raw);

    context.stop();
}

TEST(ContractBind, BindSingleton_PostStart_ImmediatelyResolvable) {
    auto object = std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 9});
    auto* raw = object.get();
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-post-resolve");
    context.start();
    context.bindSingleton<contract_bind_fixture::Item>(std::move(object));

    auto resolved = context.resolve<contract_bind_fixture::Item>();
    EXPECT_EQ(resolved.operator->(), raw);

    context.stop();
}

TEST(ContractBind, BindSingleton_BeanContext_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-context");
    EXPECT_THROW(
        context.bindSingleton<CTORIUM_NAMESPACE::BeanContext>(std::unique_ptr<CTORIUM_NAMESPACE::BeanContext>{}),
        CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractBind, BindSingleton_ConcurrentMaterialization_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-concurrent");
    context.discover<^^contract_bind_fixture>();
    context.on<contract_bind_fixture::ConcurrentSingleton>(
        CTORIUM_NAMESPACE::onInitialized,
        [&](const CTORIUM_NAMESPACE::Bean<contract_bind_fixture::ConcurrentSingleton>&) {
            EXPECT_THROW(
                context.bindSingleton<contract_bind_fixture::ConcurrentSingleton>(
                    std::make_unique<contract_bind_fixture::ConcurrentSingleton>()),
                CTORIUM_NAMESPACE::ConfigurationError);
        });
    context.start();
    (void)context.resolve<contract_bind_fixture::ConcurrentSingleton>();
    context.stop();
}

TEST(ContractBind, BindSingleton_LivesUntilRootShutdown) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-lifetime");
    context.bindSingleton<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>());
    context.start();
    auto bean = context.resolve<contract_bind_fixture::Item>();
    EXPECT_NE(bean.operator->(), nullptr);
    context.stop();
}

TEST(ContractBind, BindSingleton_FromScope_DelegatesToRoot) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-singleton-from-scope");
    auto& scope = context.resolveScope("cb-bind-singleton-from-scope-owner");
    scope.bindSingleton<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 44}));
    context.start();

    auto bean = context.resolve<contract_bind_fixture::Item>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 44);
    EXPECT_EQ(&bean.context(), &context);

    context.stop();
}

TEST(ContractBind, BindSession_ReturnsScopedContextRef) {
    EXPECT_TRUE((contract_bind_contracts::BindSessionReturnsScopedContextRef<
        CTORIUM_NAMESPACE::ScopedContext,
        contract_bind_fixture::Item>));
}

TEST(ContractBind, BindSession_OnRootContext_NotAvailable) {
    EXPECT_FALSE((contract_bind_contracts::BindSessionAvailable<
        CTORIUM_NAMESPACE::BeanContext,
        contract_bind_fixture::Item>));
}

TEST(ContractBind, BindSession_DuplicateKeySameCycle_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-session-dup-cycle");
    auto& scope = context.resolveScope("cb-bind-session-dup-cycle-scope");
    scope.bindSession<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>());
    EXPECT_THROW(
        scope.bindSession<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>()),
        CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractBind, BindSession_SameKeyAcrossCycles_Allowed) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-session-across-cycles");
    auto& scope = context.resolveScope("cb-bind-session-across-cycles-scope");
    scope.bindSession<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 1}));
    context.start();
    scope.start();
    EXPECT_EQ(scope.resolve<contract_bind_fixture::Item>()->value, 1);
    scope.stop();

    scope.bindSession<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 2}));
    scope.start();
    EXPECT_EQ(scope.resolve<contract_bind_fixture::Item>()->value, 2);

    context.stop();
}

TEST(ContractBind, BindSession_SameTypeDifferentNames_Allowed) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-session-names");
    auto& scope = context.resolveScope("cb-bind-session-names-scope");
    scope.bindSession<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 1}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("first")});
    scope.bindSession<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 2}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("second")});
    context.start();
    scope.start();

    EXPECT_EQ(scope.resolve<contract_bind_fixture::Item>(CTORIUM_NAMESPACE::named{"first"})->value, 1);
    EXPECT_EQ(scope.resolve<contract_bind_fixture::Item>(CTORIUM_NAMESPACE::named{"second"})->value, 2);

    context.stop();
}

TEST(ContractBind, BindSession_SameNameDifferentPriorities_Allowed) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-session-priorities");
    auto& scope = context.resolveScope("cb-bind-session-priorities-scope");
    scope.bindSession<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 5}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("same"), .priority = 5});
    scope.bindSession<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 10}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("same"), .priority = 10});
    context.start();
    scope.start();

    auto selected = scope.resolve<contract_bind_fixture::Item>(CTORIUM_NAMESPACE::named{"same"});
    ASSERT_NE(selected.operator->(), nullptr);
    EXPECT_EQ(selected->value, 10);

    context.stop();
}

TEST(ContractBind, BindSession_DestroyedOnStop_NotRecreatedOnStart) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-session-not-recreated");
    auto& scope = context.resolveScope("cb-bind-session-not-recreated-scope");
    scope.bindSession<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>());
    context.start();
    scope.start();
    auto bean = scope.resolve<contract_bind_fixture::Item>();
    ASSERT_NE(bean.operator->(), nullptr);

    scope.stop();
    EXPECT_EQ(bean.operator->(), nullptr);
    scope.start();
    EXPECT_THROW(scope.resolve<contract_bind_fixture::Item>(), CTORIUM_NAMESPACE::ContextStateError);

    context.stop();
}

TEST(ContractBind, BindSession_StoppedScope_AcceptedForNextStart) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-session-stopped-next");
    auto& scope = context.resolveScope("cb-bind-session-stopped-next-scope");
    context.start();
    scope.start();
    scope.stop();
    scope.bindSession<contract_bind_fixture::Item>(
        std::make_unique<contract_bind_fixture::Item>(contract_bind_fixture::Item{.value = 88}));
    scope.start();

    EXPECT_EQ(scope.resolve<contract_bind_fixture::Item>()->value, 88);

    context.stop();
}

TEST(ContractBind, BindSession_BeanContext_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bind-session-context");
    auto& scope = context.resolveScope("cb-bind-session-context-scope");
    EXPECT_THROW(
        scope.bindSession<CTORIUM_NAMESPACE::BeanContext>(std::unique_ptr<CTORIUM_NAMESPACE::BeanContext>{}),
        CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractBind, BoundObject_NoPostConstruct) {
    contract_bind_fixture::HookedSingleton::postConstructCount = 0;
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bound-no-postconstruct");
    context.bindSingleton<contract_bind_fixture::HookedSingleton>(
        std::make_unique<contract_bind_fixture::HookedSingleton>());
    context.start();
    (void)context.resolve<contract_bind_fixture::HookedSingleton>();
    EXPECT_EQ(contract_bind_fixture::HookedSingleton::postConstructCount, 0);
    context.stop();
}

TEST(ContractBind, BoundObject_PreDestroy_CtoriumTypeOnly) {
    contract_bind_fixture::HookedSingleton::preDestroyCount = 0;
    contract_bind_fixture::PlainWithSameShape::destructorCount = 0;

    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bound-predestroy");
    context.bindSingleton<contract_bind_fixture::HookedSingleton>(
        std::make_unique<contract_bind_fixture::HookedSingleton>());
    context.bindSingleton<contract_bind_fixture::PlainWithSameShape>(
        std::make_unique<contract_bind_fixture::PlainWithSameShape>());
    context.start();
    (void)context.resolve<contract_bind_fixture::HookedSingleton>();
    (void)context.resolve<contract_bind_fixture::PlainWithSameShape>();
    context.stop();

    EXPECT_EQ(contract_bind_fixture::HookedSingleton::preDestroyCount, 1);
    EXPECT_EQ(contract_bind_fixture::PlainWithSameShape::destructorCount, 1);
}

TEST(ContractBind, BoundObject_AllFourListenerPhases) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cb-bound-listener-phases");
    std::vector<int> phases;
    context.on<contract_bind_fixture::Item>(
        CTORIUM_NAMESPACE::onInitialized,
        [&](const CTORIUM_NAMESPACE::Bean<contract_bind_fixture::Item>&) { phases.push_back(1); });
    context.on<contract_bind_fixture::Item>(
        CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<contract_bind_fixture::Item>&) { phases.push_back(2); });
    context.on<contract_bind_fixture::Item>(
        CTORIUM_NAMESPACE::onPreDestroy,
        [&](const CTORIUM_NAMESPACE::Bean<contract_bind_fixture::Item>&) { phases.push_back(3); });
    context.on<contract_bind_fixture::Item>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<contract_bind_fixture::Item>&) { phases.push_back(4); });

    context.bindSingleton<contract_bind_fixture::Item>(std::make_unique<contract_bind_fixture::Item>());
    context.start();
    (void)context.resolve<contract_bind_fixture::Item>();
    context.stop();

    EXPECT_EQ(phases, (std::vector<int>{1, 2, 3, 4}));
}
