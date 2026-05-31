#include <gtest/gtest.h>
#include <meta>
#include <utility>

#include <ctr/Registration.hpp>

namespace graph_validation_session_into_singleton_fixture {

struct [[=ctr::session{}]] SessionDep {};

struct [[=ctr::singleton{}]] SingletonConsumer {
    ctr::Bean<SessionDep> dep;
    explicit SingletonConsumer(ctr::Bean<SessionDep> d) : dep(std::move(d)) {}
};

} // namespace graph_validation_session_into_singleton_fixture

TEST(GraphValidation, SessionDependencyInNonSessionConsumerThrowsAtStart) {
    auto& ctx = ctr::BeanContext::resolveContext("gv-session-into-singleton");
    ctx.discover<^^graph_validation_session_into_singleton_fixture>();

    EXPECT_THROW(ctx.start(), ctr::ConfigurationError);

    ctx.stop();
}

namespace graph_validation_scoped_non_session_fixture {

struct [[=ctr::singleton{}]] SingletonDep {};

struct [[=ctr::singleton{}]] SingletonConsumer {
    ctr::Bean<SingletonDep> dep;
    explicit SingletonConsumer(
        [[=ctr::scoped{.name = std::define_static_string("request")}]]
        ctr::Bean<SingletonDep> d)
        : dep(std::move(d)) {}
};

} // namespace graph_validation_scoped_non_session_fixture

TEST(GraphValidation, ScopedAnnotationTargetingNonSessionThrowsAtStart) {
    auto& ctx = ctr::BeanContext::resolveContext("gv-scoped-non-session");
    ctx.discover<^^graph_validation_scoped_non_session_fixture>();

    EXPECT_THROW(ctx.start(), ctr::ConfigurationError);

    ctx.stop();
}

namespace graph_validation_duplicate_factory_fixture {

struct ProductTag {};

template<typename>
struct Product {};

using ExposedProduct = Product<ProductTag>;

struct [[=ctr::factory{}]] Factory {
    [[=ctr::singleton{}]]
    ExposedProduct makeA() { return ExposedProduct{}; }

    [[=ctr::singleton{}]]
    ExposedProduct makeB() { return ExposedProduct{}; }
};

} // namespace graph_validation_duplicate_factory_fixture

TEST(GraphValidation, DuplicateFactoryProductsForSameTypeAndKeyThrowAtStart) {
    auto& ctx = ctr::BeanContext::resolveContext("gv-duplicate-factory-products");
    ctx.discover<^^graph_validation_duplicate_factory_fixture>();

    EXPECT_THROW(ctx.start(), ctr::ConfigurationError);

    ctx.stop();
}
