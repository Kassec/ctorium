#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_userdata_fixture {

struct Base {
    int value = 0;
};

struct Derived : Base {};

struct Data {
    int value = 0;
};

struct Replacement {
    int value = 0;
};

struct [[=CTORIUM_NAMESPACE::session{}]] ScopeObject {};

struct LifecycleShape {
    inline static int postConstructCount = 0;
    inline static int destructorCount = 0;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize() { ++postConstructCount; }
    ~LifecycleShape() { ++destructorCount; }
};

} // namespace contract_userdata_fixture

TEST(ContractUserData, UserData_Attach_Retrieve) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cu-attach");
    auto& scope = context.resolveScope("cu-attach-scope");
    contract_userdata_fixture::Data data{.value = 1};
    scope.userData(data);
    auto result = scope.userData<contract_userdata_fixture::Data>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(&result->get(), &data);
    context.stop();
}

TEST(ContractUserData, UserData_Replace) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cu-replace");
    auto& scope = context.resolveScope("cu-replace-scope");
    contract_userdata_fixture::Data first{.value = 1};
    contract_userdata_fixture::Data second{.value = 2};
    scope.userData(first).userData(second);
    auto result = scope.userData<contract_userdata_fixture::Data>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(&result->get(), &second);
    EXPECT_EQ(result->get().value, 2);
    context.stop();
}

TEST(ContractUserData, UserData_Clear_Nullptr) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cu-clear");
    auto& scope = context.resolveScope("cu-clear-scope");
    contract_userdata_fixture::Data data{};
    scope.userData(data);
    ASSERT_TRUE(scope.userData<contract_userdata_fixture::Data>().has_value());
    scope.userData(nullptr);
    EXPECT_FALSE(scope.userData<contract_userdata_fixture::Data>().has_value());
    context.stop();
}

TEST(ContractUserData, UserData_SurvivesStop) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cu-survives-stop");
    context.discover<^^contract_userdata_fixture>().start();
    auto& scope = context.resolveScope("cu-survives-stop-scope");
    contract_userdata_fixture::Data data{.value = 3};
    scope.userData(data);
    scope.start();
    scope.stop();
    ASSERT_TRUE(scope.userData<contract_userdata_fixture::Data>().has_value());
    EXPECT_EQ(scope.userData<contract_userdata_fixture::Data>()->get().value, 3);
    context.stop();
}

TEST(ContractUserData, UserData_SurvivesRestart) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cu-survives-restart");
    context.discover<^^contract_userdata_fixture>().start();
    auto& scope = context.resolveScope("cu-survives-restart-scope");
    contract_userdata_fixture::Data data{.value = 4};
    scope.userData(data);
    scope.start();
    scope.restart();
    ASSERT_TRUE(scope.userData<contract_userdata_fixture::Data>().has_value());
    EXPECT_EQ(scope.userData<contract_userdata_fixture::Data>()->get().value, 4);
    context.stop();
}

TEST(ContractUserData, UserData_ExactTypeMatch) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cu-exact-type");
    auto& scope = context.resolveScope("cu-exact-type-scope");
    contract_userdata_fixture::Derived derived;
    scope.userData(derived);
    EXPECT_FALSE(scope.userData<contract_userdata_fixture::Base>().has_value());
    EXPECT_TRUE(scope.userData<contract_userdata_fixture::Derived>().has_value());
    context.stop();
}

TEST(ContractUserData, UserData_NoCtriumLifecycle) {
    contract_userdata_fixture::LifecycleShape::postConstructCount = 0;
    contract_userdata_fixture::LifecycleShape::destructorCount = 0;
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cu-no-lifecycle");
    auto& scope = context.resolveScope("cu-no-lifecycle-scope");
    int listenerCount = 0;
    context.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++listenerCount; });
    contract_userdata_fixture::LifecycleShape data;
    scope.userData(data);
    context.start();
    scope.start();
    scope.stop();
    EXPECT_EQ(contract_userdata_fixture::LifecycleShape::postConstructCount, 0);
    EXPECT_EQ(contract_userdata_fixture::LifecycleShape::destructorCount, 0);
    EXPECT_EQ(listenerCount, 1); // only the implicit BeanContext enters lifecycle.
    context.stop();
}
