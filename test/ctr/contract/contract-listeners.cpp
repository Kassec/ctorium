#include <concepts>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_listeners_fixture {

struct ListenerBase {
    virtual ~ListenerBase() = default;
};

struct [[=CTORIUM_NAMESPACE::prototype{}]] ListenerDerived : ListenerBase {};
struct [[=CTORIUM_NAMESPACE::prototype{}]] OtherPrototype {};
struct [[=CTORIUM_NAMESPACE::session{}]] ScopeObject {};

} // namespace contract_listeners_fixture

namespace contract_listeners_contracts {

template <class T>
concept TypedCallbackReceivesConstRef = requires {
    [] (const CTORIUM_NAMESPACE::Bean<T>& bean) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(bean)>>);
    };
};

inline constexpr bool kGlobalCallbackReceivesConstRef = requires {
    [] (const CTORIUM_NAMESPACE::AnyBean& bean) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(bean)>>);
    };
};

} // namespace contract_listeners_contracts

TEST(ContractListeners, Listener_Typed_FiltersCompatible) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-typed-compatible");
    context.discover<^^contract_listeners_fixture>().start();
    int count = 0;
    context.on<contract_listeners_fixture::ListenerBase>(
        CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<contract_listeners_fixture::ListenerBase>&) { ++count; });
    auto bean = context.resolve<contract_listeners_fixture::ListenerDerived>();
    EXPECT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(count, 1);
    context.stop();
}

TEST(ContractListeners, Listener_Global_SeesAll) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-global-all");
    context.discover<^^contract_listeners_fixture>().start();
    int count = 0;
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++count; });
    (void)context.resolve<contract_listeners_fixture::ListenerDerived>();
    (void)context.resolve<contract_listeners_fixture::OtherPrototype>();
    EXPECT_EQ(count, 2);
    context.stop();
}

TEST(ContractListeners, Listener_PriorityOrder) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-priority");
    context.discover<^^contract_listeners_fixture>().start();
    std::vector<int> order;
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { order.push_back(1); },
               CTORIUM_NAMESPACE::ListenerOptions{.priority = 5});
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { order.push_back(2); },
               CTORIUM_NAMESPACE::ListenerOptions{.priority = 10});
    (void)context.resolve<contract_listeners_fixture::ListenerDerived>();
    EXPECT_EQ(order, (std::vector<int>{2, 1}));
    context.stop();
}

TEST(ContractListeners, Listener_SamePriority_RegistrationOrder) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-same-priority");
    context.discover<^^contract_listeners_fixture>().start();
    std::vector<int> order;
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { order.push_back(1); });
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { order.push_back(2); });
    (void)context.resolve<contract_listeners_fixture::ListenerDerived>();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    context.stop();
}

TEST(ContractListeners, Listener_TypedAndGlobal_SharedOrder) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-shared-order");
    context.discover<^^contract_listeners_fixture>().start();
    std::vector<int> order;
    context.on<contract_listeners_fixture::ListenerDerived>(
        CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<contract_listeners_fixture::ListenerDerived>&) { order.push_back(1); });
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { order.push_back(2); });
    (void)context.resolve<contract_listeners_fixture::ListenerDerived>();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    context.stop();
}

TEST(ContractListeners, Listener_BeforeStart_SeesStartTransitions) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-before-start");
    context.discover<^^contract_listeners_fixture>();
    int count = 0;
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<CTORIUM_NAMESPACE::BeanContext>()) ++count;
    });
    context.start();
    EXPECT_EQ(count, 1);
    context.stop();
}

TEST(ContractListeners, Listener_AfterStart_OnlyFutureTransitions) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-after-start");
    context.discover<^^contract_listeners_fixture>().start();
    int count = 0;
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++count; });
    (void)context.resolve<contract_listeners_fixture::ListenerDerived>();
    EXPECT_EQ(count, 1);
    context.stop();
}

TEST(ContractListeners, Listener_SnapshotDispatch) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-snapshot");
    context.discover<^^contract_listeners_fixture>().start();
    std::vector<int> order;
    CTORIUM_NAMESPACE::ListenerHandle second;
    second = context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { order.push_back(2); });
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) {
        order.push_back(1);
        second.remove();
        context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { order.push_back(3); });
    }, CTORIUM_NAMESPACE::ListenerOptions{.priority = 10});
    (void)context.resolve<contract_listeners_fixture::ListenerDerived>();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    order.clear();
    (void)context.resolve<contract_listeners_fixture::OtherPrototype>();
    EXPECT_EQ(order, (std::vector<int>{1, 3}));
    context.stop();
}

TEST(ContractListeners, Listener_Remove_Idempotent) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-remove-idempotent");
    context.discover<^^contract_listeners_fixture>().start();
    int count = 0;
    auto handle = context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++count; });
    EXPECT_NO_THROW(handle.remove());
    EXPECT_NO_THROW(handle.remove());
    (void)context.resolve<contract_listeners_fixture::ListenerDerived>();
    EXPECT_EQ(count, 0);
    context.stop();
}

TEST(ContractListeners, Listener_DestroyHandle_DoesNotUnregister) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-destroy-handle");
    context.discover<^^contract_listeners_fixture>().start();
    int count = 0;
    {
        auto handle = context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++count; });
        (void)handle;
    }
    (void)context.resolve<contract_listeners_fixture::ListenerDerived>();
    EXPECT_EQ(count, 1);
    context.stop();
}

TEST(ContractListeners, Listener_Scoped_SeesOnlyItsScope) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-scoped-only");
    context.discover<^^contract_listeners_fixture>().start();
    auto& first = context.resolveScope("cl-scoped-a");
    auto& second = context.resolveScope("cl-scoped-b");
    first.start();
    second.start();
    int count = 0;
    first.on<contract_listeners_fixture::ScopeObject>(
        CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<contract_listeners_fixture::ScopeObject>&) { ++count; });
    (void)first.resolve<contract_listeners_fixture::ScopeObject>();
    (void)second.resolve<contract_listeners_fixture::ScopeObject>();
    EXPECT_EQ(count, 1);
    context.stop();
}

TEST(ContractListeners, Listener_Root_SeesAllScopes) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cl-root-sees-scopes");
    context.discover<^^contract_listeners_fixture>().start();
    auto& first = context.resolveScope("cl-root-scope-a");
    auto& second = context.resolveScope("cl-root-scope-b");
    first.start();
    second.start();
    int count = 0;
    context.on<contract_listeners_fixture::ScopeObject>(
        CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<contract_listeners_fixture::ScopeObject>&) { ++count; });
    (void)first.resolve<contract_listeners_fixture::ScopeObject>();
    (void)second.resolve<contract_listeners_fixture::ScopeObject>();
    EXPECT_EQ(count, 2);
    context.stop();
}

TEST(ContractListeners, Listener_CallbackReceivesConstRef) {
    EXPECT_TRUE((contract_listeners_contracts::TypedCallbackReceivesConstRef<contract_listeners_fixture::ListenerDerived>));
    EXPECT_TRUE(contract_listeners_contracts::kGlobalCallbackReceivesConstRef);
}
