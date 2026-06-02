#include <memory>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_discovery_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Discovered {
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize() {}
};

} // namespace contract_discovery_fixture

namespace contract_discovery_extra_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Extra {};

} // namespace contract_discovery_extra_fixture

namespace contract_discovery_binding_fixture {

struct BoundOnly {
    int value = 0;
};

} // namespace contract_discovery_binding_fixture

TEST(ContractDiscovery, Discover_AfterStart_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cd-after-start");
    context.start();
    EXPECT_THROW(context.discover<^^contract_discovery_fixture>(), CTORIUM_NAMESPACE::ContextStateError);
    context.stop();
}

TEST(ContractDiscovery, Discover_OnScope_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cd-scope");
    auto& scope = context.resolveScope("cd-scope-owner");
    EXPECT_THROW(scope.discover<^^contract_discovery_fixture>(), CTORIUM_NAMESPACE::ContextStateError);
    context.stop();
}

TEST(ContractDiscovery, Discover_Deduplication) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cd-dedup");
    context.discover<^^contract_discovery_fixture>();
    context.discover<^^contract_discovery_fixture>();
    context.start();
    EXPECT_NO_THROW(context.resolve<contract_discovery_fixture::Discovered>());
    context.stop();
}

TEST(ContractDiscovery, Discover_MultipleCalls_Merged) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cd-merged");
    context.discover<^^contract_discovery_fixture>();
    context.discover<^^contract_discovery_extra_fixture>();
    context.start();
    EXPECT_NO_THROW(context.resolve<contract_discovery_fixture::Discovered>());
    EXPECT_NO_THROW(context.resolve<contract_discovery_extra_fixture::Extra>());
    context.stop();
}

TEST(ContractDiscovery, Discover_OptionalForContext) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cd-optional");
    context.bindSingleton<contract_discovery_binding_fixture::BoundOnly>(
        std::make_unique<contract_discovery_binding_fixture::BoundOnly>(
            contract_discovery_binding_fixture::BoundOnly{.value = 3}));
    context.start();
    auto bean = context.resolve<contract_discovery_binding_fixture::BoundOnly>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 3);
    context.stop();
}

TEST(ContractDiscovery, Discover_RetainAllMetadata_MethodsAvailable) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cd-retain-metadata");
    context.discover<^^contract_discovery_fixture>(CTORIUM_NAMESPACE::DiscoverOptions{.retainAllMetadata = true});
    context.start();
    auto bean = context.resolve<contract_discovery_fixture::Discovered>();
    EXPECT_FALSE(bean.metadata().methods().empty());
    context.stop();
}

TEST(ContractDiscovery, Discover_DefaultOptions_MethodsEmpty) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cd-default-metadata");
    context.discover<^^contract_discovery_fixture>().start();
    auto bean = context.resolve<contract_discovery_fixture::Discovered>();
    EXPECT_TRUE(bean.metadata().methods().empty());
    context.stop();
}
