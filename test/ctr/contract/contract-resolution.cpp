#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_resolution_fixture {

struct ResolveBase {
    virtual ~ResolveBase() = default;
    int value = 0;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Compatible : ResolveBase {
    Compatible() { value = 1; }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]]
       [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("x")}]]
NamedX {
    int value = 11;
};

struct [[=CTORIUM_NAMESPACE::singleton{.priority = 5}]] LowPriority : ResolveBase {
    LowPriority() { value = 5; }
};

struct [[=CTORIUM_NAMESPACE::singleton{.priority = 10}]] HighPriority : ResolveBase {
    HighPriority() { value = 10; }
};

} // namespace contract_resolution_fixture

namespace contract_resolution_ambiguous_fixture {

struct Base {
    virtual ~Base() = default;
};

struct [[=CTORIUM_NAMESPACE::singleton{.priority = 7}]] First : Base {};
struct [[=CTORIUM_NAMESPACE::singleton{.priority = 7}]] Second : Base {};

} // namespace contract_resolution_ambiguous_fixture

namespace contract_resolution_scope_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] RootObject {
    int value = 1;
};

struct [[=CTORIUM_NAMESPACE::session{}]] ScopeObject {
    int value = 2;
};

} // namespace contract_resolution_scope_fixture

namespace contract_resolution_default_fixture {

struct Product {
    int value = 0;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]]
       [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("x")}]]
DefaultX : Product {
    DefaultX() { value = 1; }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]]
       [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("y")}]]
DefaultY : Product {
    DefaultY() { value = 2; }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ConsumerDefault {
    CTORIUM_NAMESPACE::Bean<Product> product;
    explicit ConsumerDefault(CTORIUM_NAMESPACE::Bean<Product> injected) : product(std::move(injected)) {}
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ConsumerNamed {
    CTORIUM_NAMESPACE::Bean<Product> product;
    explicit ConsumerNamed(
        [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("y")}]]
        CTORIUM_NAMESPACE::Bean<Product> injected)
        : product(std::move(injected)) {}
};

} // namespace contract_resolution_default_fixture

namespace contract_resolution_cycle_fixture {

struct B;

struct [[=CTORIUM_NAMESPACE::singleton{}]] A {
    CTORIUM_NAMESPACE::Bean<B> b;
    explicit A(CTORIUM_NAMESPACE::Bean<B> injected) : b(std::move(injected)) {}
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] B {
    CTORIUM_NAMESPACE::Bean<A> a;
    explicit B(CTORIUM_NAMESPACE::Bean<A> injected) : a(std::move(injected)) {}
};

} // namespace contract_resolution_cycle_fixture

namespace contract_resolution_missing_named_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] UnnamedOnly {};

} // namespace contract_resolution_missing_named_fixture

namespace contract_resolution_empty_named_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] UnnamedOnly {};

struct [[=CTORIUM_NAMESPACE::singleton{}]]
       [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("named-only")}]]
NamedOnly {};

} // namespace contract_resolution_empty_named_fixture

TEST(ContractResolution, Resolve_Unnamed_ReturnsCompatibleBean) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-unnamed-compatible");
    context.discover<^^contract_resolution_fixture>().start();

    auto bean = context.resolve<contract_resolution_fixture::ResolveBase>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_TRUE(bean.compatible<contract_resolution_fixture::ResolveBase>());

    context.stop();
}

TEST(ContractResolution, Resolve_Named_ReturnsNamedBean) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-named");
    context.discover<^^contract_resolution_fixture>().start();

    auto bean = context.resolve<contract_resolution_fixture::NamedX>(CTORIUM_NAMESPACE::named{"x"});
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 11);

    context.stop();
}

TEST(ContractResolution, Resolve_HighestPriorityWins) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-priority");
    context.discover<^^contract_resolution_fixture>().start();

    auto bean = context.resolve<contract_resolution_fixture::ResolveBase>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 10);

    context.stop();
}

TEST(ContractResolution, Resolve_AmbiguousPriority_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-ambiguous");
    context.discover<^^contract_resolution_ambiguous_fixture>().start();
    EXPECT_THROW(context.resolve<contract_resolution_ambiguous_fixture::Base>(), CTORIUM_NAMESPACE::ResolutionError);
    context.stop();
}

TEST(ContractResolution, Resolve_NoCandidateUnnamed_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-no-unnamed");
    context.discover<^^contract_resolution_default_fixture>().start();
    EXPECT_THROW(context.resolve<contract_resolution_default_fixture::Product>(), CTORIUM_NAMESPACE::ResolutionError);
    context.stop();
}

TEST(ContractResolution, Resolve_NoCandidateNamed_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-no-named");
    context.discover<^^contract_resolution_missing_named_fixture>().start();
    EXPECT_THROW(
        context.resolve<contract_resolution_missing_named_fixture::UnnamedOnly>(CTORIUM_NAMESPACE::named{"missing"}),
        CTORIUM_NAMESPACE::ResolutionError);
    context.stop();
}

TEST(ContractResolution, Resolve_EmptyNamedKey_UsesUnnamedCandidate) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-empty-named-unnamed");
    context.discover<^^contract_resolution_empty_named_fixture>().start();

    auto unnamed = context.resolve<contract_resolution_empty_named_fixture::UnnamedOnly>();
    auto emptyNamed = context.resolve<contract_resolution_empty_named_fixture::UnnamedOnly>(
        CTORIUM_NAMESPACE::named{""});
    EXPECT_EQ(unnamed.operator->(), emptyNamed.operator->());

    context.stop();
}

TEST(ContractResolution, Resolve_EmptyNamedKey_ThrowsWhenUnnamedSpaceIsEmpty) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-empty-named-no-unnamed");
    context.discover<^^contract_resolution_empty_named_fixture>().start();

    EXPECT_THROW(
        context.resolve<contract_resolution_empty_named_fixture::NamedOnly>(),
        CTORIUM_NAMESPACE::ResolutionError);
    EXPECT_THROW(
        context.resolve<contract_resolution_empty_named_fixture::NamedOnly>(CTORIUM_NAMESPACE::named{""}),
        CTORIUM_NAMESPACE::ResolutionError);

    context.stop();
}

TEST(ContractResolution, Resolve_SessionFromScope_ReturnsScopeBean) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-session-from-scope");
    context.discover<^^contract_resolution_scope_fixture>().start();
    auto& first = context.resolveScope("cr-session-scope-a");
    auto& second = context.resolveScope("cr-session-scope-b");
    first.start();
    second.start();

    auto firstBean = first.resolve<contract_resolution_scope_fixture::ScopeObject>();
    auto secondBean = second.resolve<contract_resolution_scope_fixture::ScopeObject>();
    EXPECT_NE(firstBean.operator->(), nullptr);
    EXPECT_NE(secondBean.operator->(), nullptr);
    EXPECT_NE(firstBean.operator->(), secondBean.operator->());
    EXPECT_EQ(&firstBean.context(), &first);

    context.stop();
}

TEST(ContractResolution, Resolve_RootBeanFromScope_ReturnsRootBean) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-root-from-scope");
    context.discover<^^contract_resolution_scope_fixture>().start();
    auto& scope = context.resolveScope("cr-root-from-scope-owner");
    scope.start();

    auto rootBean = context.resolve<contract_resolution_scope_fixture::RootObject>();
    auto scopedResolve = scope.resolve<contract_resolution_scope_fixture::RootObject>();
    EXPECT_EQ(rootBean.operator->(), scopedResolve.operator->());
    EXPECT_EQ(&scopedResolve.context(), &context);

    context.stop();
}

TEST(ContractResolution, Resolve_SessionFromRoot_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-session-from-root");
    context.discover<^^contract_resolution_scope_fixture>().start();
    EXPECT_THROW(context.resolve<contract_resolution_scope_fixture::ScopeObject>(), CTORIUM_NAMESPACE::ContextStateError);
    context.stop();
}

TEST(ContractResolution, Resolve_OnStoppedScope_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-stopped-scope");
    context.discover<^^contract_resolution_scope_fixture>().start();
    auto& scope = context.resolveScope("cr-stopped-scope-owner");
    scope.start();
    scope.stop();
    EXPECT_THROW(scope.resolve<contract_resolution_scope_fixture::ScopeObject>(), CTORIUM_NAMESPACE::ContextStateError);
    context.stop();
}

TEST(ContractResolution, Resolve_DefaultNamed_Applied) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-default-applied");
    context.discover<^^contract_resolution_default_fixture>().start();
    context.defaultNamed<contract_resolution_default_fixture::Product>(std::define_static_string("x"));

    auto bean = context.resolve<contract_resolution_default_fixture::Product>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 1);

    context.stop();
}

TEST(ContractResolution, Resolve_ExplicitNamed_IgnoresDefault) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-explicit-ignores-default");
    context.discover<^^contract_resolution_default_fixture>().start();
    context.defaultNamed<contract_resolution_default_fixture::Product>(std::define_static_string("x"));

    auto bean = context.resolve<contract_resolution_default_fixture::Product>(CTORIUM_NAMESPACE::named{"y"});
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 2);

    context.stop();
}

TEST(ContractResolution, Resolve_InjectionWithoutNamed_UsesDefault) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-injection-default");
    context.discover<^^contract_resolution_default_fixture>().start();
    context.defaultNamed<contract_resolution_default_fixture::Product>(std::define_static_string("x"));

    auto consumer = context.resolve<contract_resolution_default_fixture::ConsumerDefault>();
    ASSERT_NE(consumer.operator->(), nullptr);
    EXPECT_EQ(consumer->product->value, 1);

    context.stop();
}

TEST(ContractResolution, Resolve_InjectionWithNamed_IgnoresDefault) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-injection-named");
    context.discover<^^contract_resolution_default_fixture>().start();
    context.defaultNamed<contract_resolution_default_fixture::Product>(std::define_static_string("x"));

    auto consumer = context.resolve<contract_resolution_default_fixture::ConsumerNamed>();
    ASSERT_NE(consumer.operator->(), nullptr);
    EXPECT_EQ(consumer->product->value, 2);

    context.stop();
}

TEST(ContractResolution, Resolve_DependencyCycle_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-cycle");
    context.discover<^^contract_resolution_cycle_fixture>().start();
    EXPECT_THROW(context.resolve<contract_resolution_cycle_fixture::A>(), CTORIUM_NAMESPACE::ResolutionError);
    context.stop();
}

namespace contract_resolution_never_started_scope_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] SessionType {};

} // namespace contract_resolution_never_started_scope_fixture

TEST(ContractResolution, Resolve_FromNeverStartedScope_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("cr-never-started-scope-root");
    context.discover<^^contract_resolution_never_started_scope_fixture>().start();
    auto& scope = context.resolveScope("cr-never-started-scope-owner");

    EXPECT_THROW(
        scope.resolve<contract_resolution_never_started_scope_fixture::SessionType>(),
        CTORIUM_NAMESPACE::ContextStateError);

    context.stop();
}
