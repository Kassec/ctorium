#include <atomic>
#include <stdexcept>
#include <string_view>

#include <gtest/gtest.h>

#include <ctr/Registration.hpp>

namespace exception_lazy_constructor_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] ThrowingService {
    ThrowingService() { throw std::runtime_error("lazy constructor failure"); }
};

} // namespace exception_lazy_constructor_fixture

TEST(ExceptionPolicy, LazyConstructorPropagatesRuntimeErrorUnwrapped) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ex-lazy-constructor");
    ctx.discover<^^exception_lazy_constructor_fixture>().start();

    try {
        (void)ctx.resolve<exception_lazy_constructor_fixture::ThrowingService>();
        FAIL() << "Expected std::runtime_error";
    } catch (const CTORIUM_NAMESPACE::CtoriumError& ex) {
        FAIL() << "Expected user std::runtime_error, got CtoriumError: " << ex.what();
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "lazy constructor failure");
    }

    ctx.stop();
}

namespace exception_factory_producer_fixture {

struct ProductTag {};

template<typename>
struct Product {};

using ExposedProduct = Product<ProductTag>;

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    ExposedProduct make() { throw std::runtime_error("factory producer failure"); }
};

} // namespace exception_factory_producer_fixture

TEST(ExceptionPolicy, FactoryProducerPropagatesRuntimeErrorUnwrapped) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ex-factory-producer");
    ctx.discover<^^exception_factory_producer_fixture>().start();

    try {
        (void)ctx.resolve<exception_factory_producer_fixture::ExposedProduct>();
        FAIL() << "Expected std::runtime_error";
    } catch (const CTORIUM_NAMESPACE::CtoriumError& ex) {
        FAIL() << "Expected user std::runtime_error, got CtoriumError: " << ex.what();
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "factory producer failure");
    }

    ctx.stop();
}

namespace exception_post_construct_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] ThrowingPostConstruct {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void init() { throw std::runtime_error("post construct failure"); }
};

} // namespace exception_post_construct_fixture

TEST(ExceptionPolicy, PostConstructPropagatesRuntimeErrorUnwrapped) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ex-post-construct");
    ctx.discover<^^exception_post_construct_fixture>().start();

    try {
        (void)ctx.resolve<exception_post_construct_fixture::ThrowingPostConstruct>();
        FAIL() << "Expected std::runtime_error";
    } catch (const CTORIUM_NAMESPACE::CtoriumError& ex) {
        FAIL() << "Expected user std::runtime_error, got CtoriumError: " << ex.what();
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "post construct failure");
    }

    ctx.stop();
}

namespace exception_eager_retry_fixture {

std::atomic<bool> shouldThrow{true};

struct [[=CTORIUM_NAMESPACE::singleton{.lazy = false}]] EagerService {
    EagerService() {
        if (shouldThrow.load(std::memory_order_relaxed)) {
            throw std::runtime_error("eager constructor failure");
        }
    }
};

} // namespace exception_eager_retry_fixture

TEST(ExceptionPolicy, EagerStartRollbackContractIsNotCurrentlyObservable) {
    exception_eager_retry_fixture::shouldThrow.store(true, std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ex-eager-retry");
    ctx.discover<^^exception_eager_retry_fixture>();

    EXPECT_THROW(ctx.start(), std::runtime_error);
    EXPECT_THROW(
        ctx.resolve<exception_eager_retry_fixture::EagerService>(),
        CTORIUM_NAMESPACE::ContextStateError);

    exception_eager_retry_fixture::shouldThrow.store(false, std::memory_order_relaxed);

    EXPECT_NO_THROW(ctx.start());
    EXPECT_NO_THROW((void)ctx.resolve<exception_eager_retry_fixture::EagerService>());

    ctx.stop();
}

namespace exception_resolution_base_fixture {

struct Missing {};

} // namespace exception_resolution_base_fixture

TEST(ExceptionPolicy, ResolutionErrorIsCatchableAsCtoriumError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ex-resolution-base");
    ctx.start();

    try {
        (void)ctx.resolve<exception_resolution_base_fixture::Missing>();
        FAIL() << "Expected ctr::ResolutionError";
    } catch (const CTORIUM_NAMESPACE::CtoriumError& ex) {
        EXPECT_NE(std::string_view{ex.what()}.find("requested type"), std::string_view::npos);
    }

    ctx.stop();
}

namespace exception_pre_destroy_terminate_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] ThrowingPreDestroy {
    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void cleanup() { throw std::runtime_error("pre destroy failure"); }
};

} // namespace exception_pre_destroy_terminate_fixture

TEST(ExceptionPolicy, PreDestroyThrowDuringStopTerminates) {
    EXPECT_DEATH(
        {
            auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ex-predestroy-terminate");
            ctx.discover<^^exception_pre_destroy_terminate_fixture>().start();
            (void)ctx.resolve<exception_pre_destroy_terminate_fixture::ThrowingPreDestroy>();
            ctx.stop();
        },
        "");
}
