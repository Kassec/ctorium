#include <atomic>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_contexts_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] RootObject {
    int value = 1;
};

struct [[=CTORIUM_NAMESPACE::session{}]] ScopeObject {
    int value = 2;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ContextConsumer {
    CTORIUM_NAMESPACE::Bean<CTORIUM_NAMESPACE::BeanContext> context;
    explicit ContextConsumer(CTORIUM_NAMESPACE::Bean<CTORIUM_NAMESPACE::BeanContext> injected)
        : context(std::move(injected)) {}
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] UserSingleton {};

inline int throwingAttempts = 0;

struct [[=CTORIUM_NAMESPACE::singleton{.lazy = false}]] ThrowingOnFirstStart {
    ThrowingOnFirstStart() {
        ++throwingAttempts;
        if (throwingAttempts == 1) {
            throw std::runtime_error("first start fails");
        }
    }
};

} // namespace contract_contexts_fixture

TEST(ContractContexts, ResolveContext_Default_ReturnsSame) {
    auto& first = CTORIUM_NAMESPACE::BeanContext::resolveContext();
    auto& second = CTORIUM_NAMESPACE::BeanContext::resolveContext();
    EXPECT_EQ(&first, &second);
    first.stop();
}

TEST(ContractContexts, ResolveContext_SameKey_ReturnsSame) {
    auto& first = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-same-key");
    auto& second = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-same-key");
    EXPECT_EQ(&first, &second);
    first.stop();
}

TEST(ContractContexts, ResolveContext_DifferentKeys_ReturnsDifferent) {
    auto& first = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-root-a");
    auto& second = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-root-b");
    EXPECT_NE(&first, &second);
    first.stop();
    second.stop();
}

TEST(ContractContexts, Stop_IsPermanent) {
    {
        auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-stop-permanent");
        context.discover<^^contract_contexts_fixture>().start();
        context.stop();
    }
    auto& fresh = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-stop-permanent");
    EXPECT_THROW(fresh.resolve<contract_contexts_fixture::RootObject>(), CTORIUM_NAMESPACE::ContextStateError);
    fresh.stop();
}

TEST(ContractContexts, Start_Idempotent) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-start-idempotent");
    context.discover<^^contract_contexts_fixture>();
    EXPECT_NO_THROW(context.start().start());
    context.stop();
}

TEST(ContractContexts, ResolveScope_SameKey_ReturnsSame) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-same-root");
    auto& first = root.resolveScope("cc-scope-same");
    auto& second = root.resolveScope("cc-scope-same");
    EXPECT_EQ(&first, &second);
    root.stop();
}

TEST(ContractContexts, ResolveScope_SameKeyDifferentRoots_ReturnsDifferent) {
    auto& firstRoot = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-root-a");
    auto& secondRoot = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-root-b");
    auto& first = firstRoot.resolveScope("cc-shared-scope");
    auto& second = secondRoot.resolveScope("cc-shared-scope");
    EXPECT_NE(&first, &second);
    firstRoot.stop();
    secondRoot.stop();
}

TEST(ContractContexts, ResolveScope_FromScope_DelegatesToRoot) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-delegate-root");
    auto& first = root.resolveScope("cc-scope-first");
    auto& siblingFromScope = first.resolveScope("cc-scope-sibling");
    auto& siblingFromRoot = root.resolveScope("cc-scope-sibling");
    EXPECT_NE(&first, &siblingFromScope);
    EXPECT_EQ(&siblingFromScope, &siblingFromRoot);
    root.stop();
}

TEST(ContractContexts, Scope_Discover_Throws) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-discover");
    auto& scope = root.resolveScope("cc-scope-discover-owner");
    EXPECT_THROW(scope.discover<^^contract_contexts_fixture>(), CTORIUM_NAMESPACE::ContextStateError);
    root.stop();
}

TEST(ContractContexts, Scope_ScopesAreFlat) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-flat");
    auto& first = root.resolveScope("cc-flat-a");
    auto& sibling = first.resolveScope("cc-flat-b");
    EXPECT_EQ(&sibling, &root.resolveScope("cc-flat-b"));
    EXPECT_NE(&first, &sibling);
    root.stop();
}

TEST(ContractContexts, Scope_Stop_PreservesKey_UserData_Handles) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-stop-preserves");
    root.discover<^^contract_contexts_fixture>().start();
    auto& scope = root.resolveScope("cc-preserved-scope");
    int userData = 42;
    scope.userData(userData);
    scope.start();
    auto handle = scope.resolve<contract_contexts_fixture::ScopeObject>();
    ASSERT_NE(handle.operator->(), nullptr);

    scope.stop();
    EXPECT_EQ(&scope, &root.resolveScope("cc-preserved-scope"));
    ASSERT_TRUE(scope.userData<int>().has_value());
    EXPECT_EQ(scope.userData<int>()->get(), 42);
    EXPECT_EQ(handle.operator->(), nullptr);

    scope.start();
    EXPECT_NE(handle.operator->(), nullptr);
    root.stop();
}

TEST(ContractContexts, Scope_Stop_Idempotent) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-stop-idempotent");
    root.start();
    auto& scope = root.resolveScope("cc-stop-idempotent-scope");
    scope.start();
    EXPECT_NO_THROW(scope.stop().stop());
    root.stop();
}

TEST(ContractContexts, Scope_Restart_EqualsStopThenStart) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-scope-restart");
    root.discover<^^contract_contexts_fixture>().start();
    auto& scope = root.resolveScope("cc-scope-restart-owner");
    scope.start();
    auto before = scope.resolve<contract_contexts_fixture::ScopeObject>();
    auto* beforePtr = before.operator->();
    ASSERT_NE(beforePtr, nullptr);
    scope.restart();
    auto after = scope.resolve<contract_contexts_fixture::ScopeObject>();
    EXPECT_NE(after.operator->(), nullptr);
    EXPECT_NE(after.operator->(), beforePtr);
    root.stop();
}

TEST(ContractContexts, Start_ExceptionRollback) {
    contract_contexts_fixture::throwingAttempts = 0;
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-start-rollback");
    root.discover<^^contract_contexts_fixture>();
    EXPECT_THROW(root.start(), std::runtime_error);
    EXPECT_NO_THROW(root.start());
    auto bean = root.resolve<contract_contexts_fixture::ThrowingOnFirstStart>();
    EXPECT_NE(bean.operator->(), nullptr);
    root.stop();
}

TEST(ContractContexts, BeanContext_ImplicitSingleton) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-context-implicit");
    root.start();
    auto bean = root.resolve<CTORIUM_NAMESPACE::BeanContext>();
    EXPECT_EQ(bean.operator->(), &root);
    root.stop();
}

TEST(ContractContexts, BeanContext_InjectedInConstructor) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-context-injected");
    root.discover<^^contract_contexts_fixture>().start();
    auto& scope = root.resolveScope("cc-context-injected-scope");
    scope.start();
    auto consumer = scope.resolve<contract_contexts_fixture::ContextConsumer>();
    ASSERT_NE(consumer.operator->(), nullptr);
    EXPECT_EQ(consumer->context.operator->(), &root);
    root.stop();
}

TEST(ContractContexts, BeanContext_LastDestroyedAtShutdown) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("cc-context-last-destroyed");
    root.discover<^^contract_contexts_fixture>().start();
    std::vector<int> order;
    root.on(CTORIUM_NAMESPACE::onDestroyed, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<contract_contexts_fixture::UserSingleton>()) order.push_back(1);
        if (bean.compatible<CTORIUM_NAMESPACE::BeanContext>()) order.push_back(2);
    });
    (void)root.resolve<contract_contexts_fixture::UserSingleton>();
    root.stop();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}
