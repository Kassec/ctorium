#include <atomic>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

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

namespace exception_form2_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] ThrowingSessionSvc {
    ThrowingSessionSvc() { throw std::runtime_error("form2 constructor failure"); }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<ThrowingSessionSvc> session;

    explicit Consumer(
        [[=CTORIUM_NAMESPACE::scoped{.name = std::define_static_string("ex-form2-scope")}]]
        CTORIUM_NAMESPACE::Bean<ThrowingSessionSvc> injected)
        : session(std::move(injected)) {}
};

} // namespace exception_form2_fixture

TEST(ExceptionPolicy, Form2LazyConstructorPropagatesRuntimeErrorUnwrapped) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ex-form2-constructor");
    ctx.discover<^^exception_form2_fixture>().start();
    auto& scope = ctx.resolveScope("ex-form2-scope");
    auto consumer = ctx.resolve<exception_form2_fixture::Consumer>();
    scope.start();

    try {
        (void)consumer->session.operator->();
        FAIL() << "Expected std::runtime_error";
    } catch (const CTORIUM_NAMESPACE::CtoriumError& ex) {
        FAIL() << "Expected user std::runtime_error, got CtoriumError: " << ex.what();
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "form2 constructor failure");
    }

    ctx.stop();
}

namespace exception_form3_fixture {

std::atomic<bool> throwOnConstruct{false};

struct [[=CTORIUM_NAMESPACE::threadLocal{}]] ThrowingThreadLocalSvc {
    ThrowingThreadLocalSvc() {
        if (throwOnConstruct.load(std::memory_order_relaxed)) {
            throw std::runtime_error("form3 constructor failure");
        }
    }
};

} // namespace exception_form3_fixture

TEST(ExceptionPolicy, Form3LazyConstructorPropagatesRuntimeErrorUnwrapped) {
    exception_form3_fixture::throwOnConstruct.store(false, std::memory_order_relaxed);

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ex-form3-constructor");
    ctx.discover<^^exception_form3_fixture>().start();
    auto bean = ctx.resolve<exception_form3_fixture::ThrowingThreadLocalSvc>();

    exception_form3_fixture::throwOnConstruct.store(true, std::memory_order_relaxed);
    std::exception_ptr caught;
    std::thread worker([&] {
        try {
            (void)bean.operator->();
        } catch (...) {
            caught = std::current_exception();
        }
    });
    worker.join();

    ASSERT_NE(caught, nullptr);
    try {
        std::rethrow_exception(caught);
        FAIL() << "Expected std::runtime_error";
    } catch (const CTORIUM_NAMESPACE::CtoriumError& ex) {
        FAIL() << "Expected user std::runtime_error, got CtoriumError: " << ex.what();
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "form3 constructor failure");
    }

    exception_form3_fixture::throwOnConstruct.store(false, std::memory_order_relaxed);
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
