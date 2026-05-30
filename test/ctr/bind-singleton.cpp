#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace bind_singleton_fixture {

// Plain non-Ctorium type.
struct Widget {
    int value;
    explicit Widget(int v) : value(v) {}
    ~Widget() { value = -1; } // sentinel: verify destructor called once
};

// Ctorium singleton with preDestroy.
struct [[=ctr::singleton{}]] ManagedService {
    int state  = 0;
    bool destroyed = false;
    [[=ctr::preDestroy{}]] void cleanup() { destroyed = true; }
};

// Ctorium singleton without preDestroy.
struct [[=ctr::singleton{}]] PlainService {
    int x = 42;
};

// Ctorium singleton with postConstruct — never called for bound objects.
struct [[=ctr::singleton{}]] HookedService {
    bool hookCalled = false;
    [[=ctr::postConstruct{}]] void init() { hookCalled = true; }
};

} // namespace bind_singleton_fixture

// ─── Pre-start binding ────────────────────────────────────────────────────────

TEST(BindSingleton, PreStartThenResolveReturnsInstance) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-pre-start");
    auto widget = std::make_unique<bind_singleton_fixture::Widget>(99);
    bind_singleton_fixture::Widget* raw = widget.get();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(std::move(widget));
    ctx.start();
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>();
    EXPECT_EQ(bean->value, 99);
    EXPECT_EQ(bean.operator->(), raw);
    ctx.stop();
}

TEST(BindSingleton, PreStartWithNamedKey) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-pre-start-named");
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(7),
        ctr::BindOptions{.name = "w", .priority = 0});
    ctx.start();
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>(ctr::named("w"));
    EXPECT_EQ(bean->value, 7);
    ctx.stop();
}

// ─── Lifecycle: no postConstruct, preDestroy only for Ctorium types ───────────

TEST(BindSingleton, NoPostConstructForBoundObject) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-no-postconstruct");
    ctx.bindSingleton<bind_singleton_fixture::HookedService>(
        std::make_unique<bind_singleton_fixture::HookedService>());
    ctx.start();
    auto bean = ctx.resolve<bind_singleton_fixture::HookedService>();
    EXPECT_FALSE(bean->hookCalled); // postConstruct must NOT fire for bound objects
    ctx.stop();
}

TEST(BindSingleton, PreDestroyCalledForCtoriumType) {
    bool destroyCalled = false;
    {
        auto& ctx = ctr::BeanContext::resolveContext("bs-predestroy-ctorium");
        auto svc = std::make_unique<bind_singleton_fixture::ManagedService>();
        bind_singleton_fixture::ManagedService* raw = svc.get();
        ctx.bindSingleton<bind_singleton_fixture::ManagedService>(std::move(svc));
        ctx.start();
        ctx.resolve<bind_singleton_fixture::ManagedService>(); // materialize
        ctx.stop();
        destroyCalled = raw->destroyed;
    }
    EXPECT_TRUE(destroyCalled);
}

TEST(BindSingleton, NoPreDestroyForNonCtoriumType) {
    // Widget has no Ctorium markers; its destructor sets value=-1 once.
    // We verify no double-destruction or crash.
    {
        auto& ctx = ctr::BeanContext::resolveContext("bs-no-predestroy");
        ctx.bindSingleton<bind_singleton_fixture::Widget>(
            std::make_unique<bind_singleton_fixture::Widget>(42));
        ctx.start();
        auto bean = ctx.resolve<bind_singleton_fixture::Widget>();
        EXPECT_EQ(bean->value, 42);
        ctx.stop();
        // If Widget is double-destroyed, value would be -1 the second time → crash/UB.
        // Reaching here without crash is the test.
    }
}

// ─── Listener phases ──────────────────────────────────────────────────────────

TEST(BindSingleton, AllFourListenerPhasesFire) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-listeners");
    int initialized = 0, created = 0, preDestroy = 0, destroyed = 0;

    ctx.on<bind_singleton_fixture::Widget>(ctr::onInitialized,
        [&](const ctr::Bean<bind_singleton_fixture::Widget>&){ initialized++; });
    ctx.on<bind_singleton_fixture::Widget>(ctr::onCreated,
        [&](const ctr::Bean<bind_singleton_fixture::Widget>&){ created++; });
    ctx.on<bind_singleton_fixture::Widget>(ctr::onPreDestroy,
        [&](const ctr::Bean<bind_singleton_fixture::Widget>&){ preDestroy++; });
    ctx.on<bind_singleton_fixture::Widget>(ctr::onDestroyed,
        [&](const ctr::Bean<bind_singleton_fixture::Widget>&){ destroyed++; });

    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(1));
    ctx.start();
    ctx.resolve<bind_singleton_fixture::Widget>();
    ctx.stop();

    EXPECT_EQ(initialized, 1);
    EXPECT_EQ(created, 1);
    EXPECT_EQ(preDestroy, 1);
    EXPECT_EQ(destroyed, 1);
}

// ─── Post-start binding guards ────────────────────────────────────────────────

TEST(BindSingleton, PostStartUnknownTypeAdoptsAndResolves) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-post-unknown");
    ctx.start();
    auto widget = std::make_unique<bind_singleton_fixture::Widget>(0);
    bind_singleton_fixture::Widget* raw = widget.get();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(std::move(widget));
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>();
    EXPECT_EQ(bean.operator->(), raw);
    ctx.stop();
}

TEST(BindSingleton, PostStartAlreadyInstantiatedThrowsConfigurationError) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-post-instantiated");
    ctx.discover<^^bind_singleton_fixture>();
    ctx.start();
    ctx.resolve<bind_singleton_fixture::PlainService>(); // materialize first
    EXPECT_THROW(
        ctx.bindSingleton<bind_singleton_fixture::PlainService>(
            std::make_unique<bind_singleton_fixture::PlainService>()),
        ctr::ConfigurationError);
    ctx.stop();
}

TEST(BindSingleton, ExplicitBeanContextBindingThrowsConfigurationError) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-beancontext");
    EXPECT_THROW(
        ctx.bindSingleton<ctr::BeanContext>(
            std::unique_ptr<ctr::BeanContext>(nullptr)),
        ctr::ConfigurationError);
    ctx.stop();
}

// ─── ScopedContext delegation to root ─────────────────────────────────────────

TEST(BindSingleton, ViaScopedContextDelegatesToRoot) {
    auto& ctx   = ctr::BeanContext::resolveContext("bs-scope-delegate");
    auto& scope = ctx.resolveScope("s");
    scope.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(55));
    ctx.start();
    // Resolvable from root.
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>();
    EXPECT_EQ(bean->value, 55);
    ctx.stop();
}

// ─── Pre-start handle contract ────────────────────────────────────────────────

TEST(BindSingleton, PreStartBindReturnsEmptyHandle) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-pre-start-handle");
    auto handle = ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(0));
    EXPECT_EQ(handle.operator->(), nullptr);
    ctx.stop();
}

TEST(BindSingleton, PreStartLifecycleFiresAfterStartWithDeferredListeners) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-pre-start-lifecycle");
    int count = 0;
    ctx.on<bind_singleton_fixture::Widget>(ctr::onCreated,
        [&](const ctr::Bean<bind_singleton_fixture::Widget>&) { ++count; });
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(1));
    ctx.start();
    EXPECT_EQ(count, 1);
    ctx.stop();
}

// Post-start binding cases: bindSingleton adopts a runtime-bound singleton type
// after the root context is already started.

TEST(BindSingleton, PostStartBindResolvable) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-post-start-resolve");
    ctx.start();
    auto widget = std::make_unique<bind_singleton_fixture::Widget>(77);
    bind_singleton_fixture::Widget* raw = widget.get();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(std::move(widget));
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>();
    EXPECT_EQ(bean.operator->(), raw);
    ctx.stop();
}

TEST(BindSingleton, PostStartBindFiresLifecycleImmediately) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-post-start-lifecycle");
    int count = 0;
    ctx.on<bind_singleton_fixture::Widget>(ctr::onCreated,
        [&](const ctr::Bean<bind_singleton_fixture::Widget>&) { ++count; });
    ctx.start();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(2));
    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(BindSingleton, PostStartBindWithNameResolvableByName) {
    auto& ctx = ctr::BeanContext::resolveContext("bs-post-start-named");
    ctx.start();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(99),
        ctr::BindOptions{.name = "primary", .priority = 0});
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>(ctr::named("primary"));
    EXPECT_EQ(bean->value, 99);
    EXPECT_THROW(ctx.resolve<bind_singleton_fixture::Widget>(), ctr::ResolutionError);
    ctx.stop();
}
