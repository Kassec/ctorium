#include <gtest/gtest.h>
#include <meta>
#include <utility>

#include <ctr/Registration.hpp>

namespace graph_validation_session_into_singleton_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] SessionDep {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonConsumer {
    CTORIUM_NAMESPACE::Bean<SessionDep> dep;
    explicit SingletonConsumer(CTORIUM_NAMESPACE::Bean<SessionDep> d) : dep(std::move(d)) {}
};

} // namespace graph_validation_session_into_singleton_fixture

TEST(GraphValidation, SessionDependencyInNonSessionConsumerThrowsAtStart) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("gv-session-into-singleton");
    ctx.discover<^^graph_validation_session_into_singleton_fixture>();

    EXPECT_THROW(ctx.start(), CTORIUM_NAMESPACE::ConfigurationError);

    ctx.stop();
}

namespace graph_validation_scoped_non_session_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonDep {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonConsumer {
    CTORIUM_NAMESPACE::Bean<SingletonDep> dep;
    explicit SingletonConsumer(
        [[=CTORIUM_NAMESPACE::scoped{.name = std::define_static_string("request")}]]
        CTORIUM_NAMESPACE::Bean<SingletonDep> d)
        : dep(std::move(d)) {}
};

} // namespace graph_validation_scoped_non_session_fixture

TEST(GraphValidation, ScopedAnnotationTargetingNonSessionThrowsAtStart) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("gv-scoped-non-session");
    ctx.discover<^^graph_validation_scoped_non_session_fixture>();

    EXPECT_THROW(ctx.start(), CTORIUM_NAMESPACE::ConfigurationError);

    ctx.stop();
}

namespace graph_validation_duplicate_factory_fixture {

struct ProductTag {};

template<typename>
struct Product {};

using ExposedProduct = Product<ProductTag>;

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    ExposedProduct makeA() { return ExposedProduct{}; }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    ExposedProduct makeB() { return ExposedProduct{}; }
};

} // namespace graph_validation_duplicate_factory_fixture

TEST(GraphValidation, DuplicateFactoryProductsForSameTypeAndKeyThrowAtStart) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("gv-duplicate-factory-products");
    ctx.discover<^^graph_validation_duplicate_factory_fixture>();

    EXPECT_THROW(ctx.start(), CTORIUM_NAMESPACE::ConfigurationError);

    ctx.stop();
}
