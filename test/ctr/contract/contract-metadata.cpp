#include <memory>
#include <string_view>
#include <typeinfo>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_metadata_fixture {

struct MetadataBase {
    virtual ~MetadataBase() = default;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Concrete : MetadataBase {
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void initialize() {}
};

struct [[=CTORIUM_NAMESPACE::prototype{}]] PrototypeObject {};

struct [[=CTORIUM_NAMESPACE::singleton{}]]
       [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("named")}]]
NamedConcrete {};

struct Product {};

struct [[=CTORIUM_NAMESPACE::factory{}]] ProductFactory {
    [[=CTORIUM_NAMESPACE::singleton{}]] Product makeProduct() { return Product{}; }
};

struct BoundObject {};

} // namespace contract_metadata_fixture

TEST(ContractMetadata, Metadata_ObservedType_ReturnsExposedType) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-observed-type");
    context.discover<^^contract_metadata_fixture>().start();
    auto bean = context.resolve<contract_metadata_fixture::MetadataBase>();
    EXPECT_EQ(bean.metadata().observedType(), typeid(contract_metadata_fixture::MetadataBase));
    context.stop();
}

TEST(ContractMetadata, Metadata_ExactType_ReturnsConcrete) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-exact-type");
    context.discover<^^contract_metadata_fixture>().start();
    auto bean = context.resolve<contract_metadata_fixture::MetadataBase>();
    EXPECT_EQ(bean.metadata().exactType(), typeid(contract_metadata_fixture::Concrete));
    context.stop();
}

TEST(ContractMetadata, Metadata_Name_ReturnsKey) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-name");
    context.discover<^^contract_metadata_fixture>().start();
    auto named = context.resolve<contract_metadata_fixture::NamedConcrete>(CTORIUM_NAMESPACE::named{"named"});
    auto unnamed = context.resolve<contract_metadata_fixture::Concrete>();
    EXPECT_EQ(named.metadata().name(), std::string_view{"named"});
    EXPECT_TRUE(unnamed.metadata().name().empty());
    context.stop();
}

TEST(ContractMetadata, Metadata_Lifetime_MatchesMarker) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-lifetime");
    context.discover<^^contract_metadata_fixture>().start();
    auto singleton = context.resolve<contract_metadata_fixture::Concrete>();
    auto prototype = context.resolve<contract_metadata_fixture::PrototypeObject>();
    EXPECT_EQ(singleton.metadata().lifetime(), CTORIUM_NAMESPACE::detail::Lifetime::Singleton);
    EXPECT_EQ(prototype.metadata().lifetime(), CTORIUM_NAMESPACE::detail::Lifetime::Prototype);
    context.stop();
}

TEST(ContractMetadata, Metadata_Origin_Annotated) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-origin-annotated");
    context.discover<^^contract_metadata_fixture>().start();
    auto bean = context.resolve<contract_metadata_fixture::Concrete>();
    EXPECT_EQ(bean.metadata().origin(), CTORIUM_NAMESPACE::detail::Origin::AnnotatedType);
    context.stop();
}

TEST(ContractMetadata, Metadata_Origin_Factory) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-origin-factory");
    context.discover<^^contract_metadata_fixture>().start();
    auto bean = context.resolve<contract_metadata_fixture::Product>();
    EXPECT_EQ(bean.metadata().origin(), CTORIUM_NAMESPACE::detail::Origin::FactoryProduct);
    context.stop();
}

TEST(ContractMetadata, Metadata_Origin_RuntimeBinding) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-origin-runtime");
    context.bindSingleton<contract_metadata_fixture::BoundObject>(std::make_unique<contract_metadata_fixture::BoundObject>());
    context.start();
    auto bean = context.resolve<contract_metadata_fixture::BoundObject>();
    EXPECT_EQ(bean.metadata().origin(), CTORIUM_NAMESPACE::detail::Origin::RuntimeBinding);
    context.stop();
}

TEST(ContractMetadata, Metadata_FactoryMethod_ReturnsName) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-factory-method");
    context.discover<^^contract_metadata_fixture>().start();
    auto bean = context.resolve<contract_metadata_fixture::Product>();
    EXPECT_EQ(bean.metadata().factoryMethod(), std::string_view{"makeProduct"});
    context.stop();
}

TEST(ContractMetadata, Metadata_Methods_RetainedWhenRequested) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-methods-retained");
    context.discover<^^contract_metadata_fixture>(CTORIUM_NAMESPACE::DiscoverOptions{.retainAllMetadata = true});
    context.start();
    auto bean = context.resolve<contract_metadata_fixture::Concrete>();
    EXPECT_FALSE(bean.metadata().methods().empty());
    context.stop();
}

TEST(ContractMetadata, Metadata_Methods_EmptyByDefault) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-methods-default-empty");
    context.discover<^^contract_metadata_fixture>().start();
    auto bean = context.resolve<contract_metadata_fixture::Concrete>();
    EXPECT_TRUE(bean.metadata().methods().empty());
    context.stop();
}

TEST(ContractMetadata, Metadata_Methods_AnnotatedWith_Filters) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cm-methods-filter");
    context.discover<^^contract_metadata_fixture>(CTORIUM_NAMESPACE::DiscoverOptions{.retainAllMetadata = true});
    context.start();
    auto bean = context.resolve<contract_metadata_fixture::Concrete>();
    auto methods = bean.metadata().methods().annotatedWith<CTORIUM_NAMESPACE::postConstruct>();
    EXPECT_FALSE(methods.empty());
    context.stop();
}
