#include <memory>

#include <gtest/gtest.h>
#include <meta>

#include <ctr/Registration.hpp>

namespace priority_higher_binding_fixture {

struct Service {
    int value;
    explicit Service(int v) : value(v) {}
};

} // namespace priority_higher_binding_fixture

TEST(Priority, HigherPriorityBindingWinsForSameTypeAndKey) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("prio-bind-higher");
    ctx.bindSingleton<priority_higher_binding_fixture::Service>(
        std::make_unique<priority_higher_binding_fixture::Service>(1),
        CTORIUM_NAMESPACE::BindOptions{.priority = 1});
    ctx.bindSingleton<priority_higher_binding_fixture::Service>(
        std::make_unique<priority_higher_binding_fixture::Service>(2),
        CTORIUM_NAMESPACE::BindOptions{.priority = 10});

    ctx.start();

    auto bean = ctx.resolve<priority_higher_binding_fixture::Service>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 2);

    ctx.stop();
}

namespace priority_ambiguous_binding_fixture {

struct Service {
    int value;
    explicit Service(int v) : value(v) {}
};

} // namespace priority_ambiguous_binding_fixture

TEST(Priority, EqualPriorityBindingsForSameTypeAndKeyAreAmbiguous) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("prio-bind-ambiguous");
    ctx.bindSingleton<priority_ambiguous_binding_fixture::Service>(
        std::make_unique<priority_ambiguous_binding_fixture::Service>(1),
        CTORIUM_NAMESPACE::BindOptions{.priority = 5});
    ctx.bindSingleton<priority_ambiguous_binding_fixture::Service>(
        std::make_unique<priority_ambiguous_binding_fixture::Service>(2),
        CTORIUM_NAMESPACE::BindOptions{.priority = 5});

    ctx.start();

    EXPECT_THROW(
        ctx.resolve<priority_ambiguous_binding_fixture::Service>(),
        CTORIUM_NAMESPACE::ResolutionError);

    ctx.stop();
}

namespace priority_named_space_fixture {

struct Service {
    int value;
    explicit Service(int v) : value(v) {}
};

} // namespace priority_named_space_fixture

TEST(Priority, NamedAndUnnamedCandidateSpacesAreSeparate) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("prio-bind-named-space");
    ctx.bindSingleton<priority_named_space_fixture::Service>(
        std::make_unique<priority_named_space_fixture::Service>(1));
    ctx.bindSingleton<priority_named_space_fixture::Service>(
        std::make_unique<priority_named_space_fixture::Service>(2),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("named")});

    ctx.start();

    auto unnamed = ctx.resolve<priority_named_space_fixture::Service>();
    auto named = ctx.resolve<priority_named_space_fixture::Service>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("named")});

    ASSERT_NE(unnamed.operator->(), nullptr);
    ASSERT_NE(named.operator->(), nullptr);
    EXPECT_EQ(unnamed->value, 1);
    EXPECT_EQ(named->value, 2);
    EXPECT_NE(unnamed.operator->(), named.operator->());

    ctx.stop();
}
