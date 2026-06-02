#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace bind_singleton_fixture {

// Plain non-Ctorium type.
struct Widget {
    int value;
    explicit Widget(int v) : value(v) {}
    ~Widget() { value = -1; } // sentinel: verify destructor called once
};

// Ctorium singleton with preDestroy.
struct [[=CTORIUM_NAMESPACE::singleton{}]] ManagedService {
    int state  = 0;
    bool destroyed = false;
    [[=CTORIUM_NAMESPACE::preDestroy{}]] void cleanup() { destroyed = true; }
};

// Ctorium singleton without preDestroy.
struct [[=CTORIUM_NAMESPACE::singleton{}]] PlainService {
    int x = 42;
};

// Ctorium singleton with postConstruct — never called for bound objects.
struct [[=CTORIUM_NAMESPACE::singleton{}]] HookedService {
    bool hookCalled = false;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void init() { hookCalled = true; }
};

} // namespace bind_singleton_fixture

// ─── Pre-start binding ────────────────────────────────────────────────────────

TEST(BindSingleton, PreStartThenResolveReturnsInstance) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-pre-start");
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
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-pre-start-named");
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(7),
        CTORIUM_NAMESPACE::BindOptions{.name = "w", .priority = 0});
    ctx.start();
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::named("w"));
    EXPECT_EQ(bean->value, 7);
    ctx.stop();
}

// ─── Lifecycle: no postConstruct, preDestroy only for Ctorium types ───────────

TEST(BindSingleton, NoPostConstructForBoundObject) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-no-postconstruct");
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
        auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-predestroy-ctorium");
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
        auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-no-predestroy");
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
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-listeners");
    int initialized = 0, created = 0, preDestroy = 0, destroyed = 0;

    ctx.on<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::onInitialized,
        [&](const CTORIUM_NAMESPACE::Bean<bind_singleton_fixture::Widget>&){ initialized++; });
    ctx.on<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<bind_singleton_fixture::Widget>&){ created++; });
    ctx.on<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::onPreDestroy,
        [&](const CTORIUM_NAMESPACE::Bean<bind_singleton_fixture::Widget>&){ preDestroy++; });
    ctx.on<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<bind_singleton_fixture::Widget>&){ destroyed++; });

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
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-unknown");
    ctx.start();
    auto widget = std::make_unique<bind_singleton_fixture::Widget>(0);
    bind_singleton_fixture::Widget* raw = widget.get();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(std::move(widget));
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>();
    EXPECT_EQ(bean.operator->(), raw);
    ctx.stop();
}

TEST(BindSingleton, PostStartAlreadyInstantiatedThrowsConfigurationError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-instantiated");
    ctx.discover<^^bind_singleton_fixture>();
    ctx.start();
    ctx.resolve<bind_singleton_fixture::PlainService>(); // materialize first
    EXPECT_THROW(
        ctx.bindSingleton<bind_singleton_fixture::PlainService>(
            std::make_unique<bind_singleton_fixture::PlainService>()),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

TEST(BindSingleton, PostStartMultipleDistinctNamesAllowed) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-distinct-names");
    ctx.start();
    EXPECT_NO_THROW(
        ctx.bindSingleton<bind_singleton_fixture::Widget>(
            std::make_unique<bind_singleton_fixture::Widget>(11),
            CTORIUM_NAMESPACE::BindOptions{.name = "audit", .priority = 0}));
    EXPECT_NO_THROW(
        ctx.bindSingleton<bind_singleton_fixture::Widget>(
            std::make_unique<bind_singleton_fixture::Widget>(22),
            CTORIUM_NAMESPACE::BindOptions{.name = "metrics", .priority = 0}));
    auto audit = ctx.resolve<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::named("audit"));
    auto metrics = ctx.resolve<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::named("metrics"));
    EXPECT_EQ(audit->value, 11);
    EXPECT_EQ(metrics->value, 22);
    ctx.stop();
}

TEST(BindSingleton, PostStartDoubleBindSameNameThrows) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-same-name");
    ctx.start();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(11),
        CTORIUM_NAMESPACE::BindOptions{.name = "audit", .priority = 0});
    EXPECT_THROW(
        ctx.bindSingleton<bind_singleton_fixture::Widget>(
            std::make_unique<bind_singleton_fixture::Widget>(22),
            CTORIUM_NAMESPACE::BindOptions{.name = "audit", .priority = 0}),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

TEST(BindSingleton, ExplicitBeanContextBindingThrowsConfigurationError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-beancontext");
    EXPECT_THROW(
        ctx.bindSingleton<CTORIUM_NAMESPACE::BeanContext>(
            std::unique_ptr<CTORIUM_NAMESPACE::BeanContext>(nullptr)),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

// ─── ScopedContext delegation to root ─────────────────────────────────────────

TEST(BindSingleton, ViaScopedContextDelegatesToRoot) {
    auto& ctx   = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-scope-delegate");
    auto& scope = ctx.resolveScope("s");
    scope.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(55));
    ctx.start();
    // Resolvable from root.
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>();
    EXPECT_EQ(bean->value, 55);
    ctx.stop();
}

// ─── Pre-start lifecycle ──────────────────────────────────────────────────────

TEST(BindSingleton, PreStartLifecycleFiresAfterStartWithDeferredListeners) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-pre-start-lifecycle");
    int count = 0;
    ctx.on<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<bind_singleton_fixture::Widget>&) { ++count; });
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(1));
    ctx.start();
    EXPECT_EQ(count, 1);
    ctx.stop();
}

// Post-start binding cases: bindSingleton adopts a runtime-bound singleton type
// after the root context is already started.

TEST(BindSingleton, PostStartBindResolvable) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-start-resolve");
    ctx.start();
    auto widget = std::make_unique<bind_singleton_fixture::Widget>(77);
    bind_singleton_fixture::Widget* raw = widget.get();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(std::move(widget));
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>();
    EXPECT_EQ(bean.operator->(), raw);
    ctx.stop();
}

TEST(BindSingleton, PostStartBindFiresLifecycleImmediately) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-start-lifecycle");
    int count = 0;
    ctx.on<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<bind_singleton_fixture::Widget>&) { ++count; });
    ctx.start();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(2));
    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(BindSingleton, PostStartBindWithNameResolvableByName) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-start-named");
    ctx.start();
    ctx.bindSingleton<bind_singleton_fixture::Widget>(
        std::make_unique<bind_singleton_fixture::Widget>(99),
        CTORIUM_NAMESPACE::BindOptions{.name = "primary", .priority = 0});
    auto bean = ctx.resolve<bind_singleton_fixture::Widget>(CTORIUM_NAMESPACE::named("primary"));
    EXPECT_EQ(bean->value, 99);
    EXPECT_THROW(ctx.resolve<bind_singleton_fixture::Widget>(), CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

TEST(BindSingleton, PostStartHigherPriorityRuntimeBindingWinsOverDiscovered) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-start-higher-priority-wins");
    ctx.discover<^^bind_singleton_fixture>();
    ctx.start();
    auto runtime = std::make_unique<bind_singleton_fixture::PlainService>();
    runtime->x = 123;
    bind_singleton_fixture::PlainService* raw = runtime.get();
    ctx.bindSingleton<bind_singleton_fixture::PlainService>(
        std::move(runtime),
        CTORIUM_NAMESPACE::BindOptions{.priority = 10});
    auto bean = ctx.resolve<bind_singleton_fixture::PlainService>();
    EXPECT_EQ(bean.operator->(), raw);
    EXPECT_EQ(bean->x, 123);
    ctx.stop();
}

TEST(BindSingleton, PostStartEqualPriorityRuntimeBindingMakesDiscoveredAmbiguous) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bs-post-start-equal-priority-ambiguous");
    ctx.discover<^^bind_singleton_fixture>();
    ctx.start();
    auto runtime = std::make_unique<bind_singleton_fixture::PlainService>();
    runtime->x = 456;
    EXPECT_THROW(
        ctx.bindSingleton<bind_singleton_fixture::PlainService>(
            std::move(runtime),
            CTORIUM_NAMESPACE::BindOptions{.priority = 0}),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

TEST(BindSingleton, PostStartSameNameDifferentPriorityAllowedAfterLowerWasResolved) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext(
        "bs-post-start-same-name-different-priority");
    ctx.start();
    auto first = std::make_unique<bind_singleton_fixture::PlainService>();
    first->x = 1;
    bind_singleton_fixture::PlainService* firstRaw = first.get();
    ctx.bindSingleton<bind_singleton_fixture::PlainService>(
        std::move(first),
        CTORIUM_NAMESPACE::BindOptions{.name = "same", .priority = 1});
    auto resolvedFirst = ctx.resolve<bind_singleton_fixture::PlainService>(
        CTORIUM_NAMESPACE::named("same"));
    EXPECT_EQ(resolvedFirst.operator->(), firstRaw);
    EXPECT_EQ(resolvedFirst->x, 1);

    auto second = std::make_unique<bind_singleton_fixture::PlainService>();
    second->x = 2;
    bind_singleton_fixture::PlainService* secondRaw = second.get();
    ctx.bindSingleton<bind_singleton_fixture::PlainService>(
        std::move(second),
        CTORIUM_NAMESPACE::BindOptions{.name = "same", .priority = 10});
    auto resolvedSecond = ctx.resolve<bind_singleton_fixture::PlainService>(
        CTORIUM_NAMESPACE::named("same"));
    EXPECT_EQ(resolvedSecond.operator->(), secondRaw);
    EXPECT_EQ(resolvedSecond->x, 2);
    EXPECT_NE(resolvedSecond.operator->(), resolvedFirst.operator->());
    ctx.stop();
}
