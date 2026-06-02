#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_hooks_fixture {

inline std::vector<int> calls;

struct [[=CTORIUM_NAMESPACE::singleton{}]] Dependency {
    int value = 7;
};

struct HookBase {
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void baseInit() { calls.push_back(1); }
    [[=CTORIUM_NAMESPACE::preDestroy{}]] void baseCleanup() { calls.push_back(4); }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Hooked : HookBase {
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void derivedInit() { calls.push_back(2); }
    [[=CTORIUM_NAMESPACE::preDestroy{}]] void derivedCleanup() { calls.push_back(3); }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] InjectableHook {
    Dependency* dependency = nullptr;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize(CTORIUM_NAMESPACE::Bean<Dependency> injected) {
        dependency = injected.operator->();
    }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ReturningHook {
    inline static int count = 0;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] int initialize() {
        ++count;
        return 42;
    }
};

struct NonCtoriumProduct {
    inline static int postConstructCount = 0;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize() { ++postConstructCount; }
};

struct [[=CTORIUM_NAMESPACE::factory{}]] ProductFactory {
    [[=CTORIUM_NAMESPACE::singleton{}]] NonCtoriumProduct make() { return NonCtoriumProduct{}; }
};

} // namespace contract_hooks_fixture

TEST(ContractHooks, PostConstruct_RunsAfterConstruction) {
    contract_hooks_fixture::calls.clear();
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ck-postconstruct");
    context.discover<^^contract_hooks_fixture>().start();
    (void)context.resolve<contract_hooks_fixture::Hooked>();
    EXPECT_EQ(contract_hooks_fixture::calls, (std::vector<int>{1, 2}));
    context.stop();
}

TEST(ContractHooks, PreDestroy_RunsBeforeDestruction) {
    contract_hooks_fixture::calls.clear();
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ck-predestroy");
    context.discover<^^contract_hooks_fixture>().start();
    (void)context.resolve<contract_hooks_fixture::Hooked>();
    context.stop();
    EXPECT_EQ(contract_hooks_fixture::calls, (std::vector<int>{1, 2, 3, 4}));
}

TEST(ContractHooks, PostConstruct_InjectableParameters) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ck-hook-injection");
    context.discover<^^contract_hooks_fixture>().start();
    auto dependency = context.resolve<contract_hooks_fixture::Dependency>();
    auto bean = context.resolve<contract_hooks_fixture::InjectableHook>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->dependency, dependency.operator->());
    context.stop();
}

TEST(ContractHooks, Hook_ParentBeforeChild) {
    contract_hooks_fixture::calls.clear();
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ck-parent-before-child");
    context.discover<^^contract_hooks_fixture>().start();
    (void)context.resolve<contract_hooks_fixture::Hooked>();
    ASSERT_GE(contract_hooks_fixture::calls.size(), 2u);
    EXPECT_EQ(contract_hooks_fixture::calls[0], 1);
    EXPECT_EQ(contract_hooks_fixture::calls[1], 2);
    context.stop();
}

TEST(ContractHooks, Hook_ReturnValueIgnored) {
    contract_hooks_fixture::ReturningHook::count = 0;
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ck-return-ignored");
    context.discover<^^contract_hooks_fixture>().start();
    (void)context.resolve<contract_hooks_fixture::ReturningHook>();
    EXPECT_EQ(contract_hooks_fixture::ReturningHook::count, 1);
    context.stop();
}

TEST(ContractHooks, Hook_NonCtoriumType_NoHooks) {
    contract_hooks_fixture::NonCtoriumProduct::postConstructCount = 0;
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ck-non-ctorium-product");
    context.discover<^^contract_hooks_fixture>().start();
    auto product = context.resolve<contract_hooks_fixture::NonCtoriumProduct>();
    EXPECT_NE(product.operator->(), nullptr);
    EXPECT_EQ(contract_hooks_fixture::NonCtoriumProduct::postConstructCount, 0);
    context.stop();
}
