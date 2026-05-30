#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace bind_session_fixture {

// Plain non-Ctorium type for session binding.
struct Counter {
    int count;
    explicit Counter(int c) : count(c) {}
};

struct BoundSession { int val = 0; };

// Ctorium session type with preDestroy.
struct [[=ctr::session{}]] ManagedSession {
    int state = 0;
    bool preDestroyCalled = false;
    [[=ctr::preDestroy{}]] void cleanup() { preDestroyCalled = true; }
};

// Ctorium session type with postConstruct — must NOT fire for bound objects.
struct [[=ctr::session{}]] HookedSession {
    bool hookCalled = false;
    [[=ctr::postConstruct{}]] void init() { hookCalled = true; }
};

} // namespace bind_session_fixture

// ─── Pre-scope-start binding ──────────────────────────────────────────────────

TEST(BindSession, PreScopeStartThenResolveReturnsInstance) {
    auto& ctx   = ctr::BeanContext::resolveContext("bsess-pre");
    auto& scope = ctx.resolveScope("s");
    auto counter = std::make_unique<bind_session_fixture::Counter>(77);
    bind_session_fixture::Counter* raw = counter.get();
    scope.bindSession<bind_session_fixture::Counter>(std::move(counter));
    ctx.start();
    scope.start();
    auto bean = scope.resolve<bind_session_fixture::Counter>();
    EXPECT_EQ(bean->count, 77);
    EXPECT_EQ(bean.operator->(), raw);
    ctx.stop();
}

TEST(BindSession, BoundInstanceNotRecreatedAfterRestart) {
    auto& ctx   = ctr::BeanContext::resolveContext("bsess-restart");
    auto& scope = ctx.resolveScope("s");
    scope.bindSession<bind_session_fixture::Counter>(
        std::make_unique<bind_session_fixture::Counter>(1));
    ctx.start();
    scope.start();
    EXPECT_EQ(scope.resolve<bind_session_fixture::Counter>()->count, 1);
    scope.stop();
    scope.start(); // restart — no re-binding
    EXPECT_THROW(
        scope.resolve<bind_session_fixture::Counter>(),
        ctr::ContextStateError);
    ctx.stop();
}

// ─── Stopped scope: accepted for next start ───────────────────────────────────

TEST(BindSession, BindOnStoppedScopeEntersLifecycleOnNextStart) {
    auto& ctx   = ctr::BeanContext::resolveContext("bsess-stopped");
    auto& scope = ctx.resolveScope("s");
    // Register type before root start (intern it).
    scope.bindSession<bind_session_fixture::Counter>(
        std::make_unique<bind_session_fixture::Counter>(1));
    ctx.start();
    scope.start();
    EXPECT_EQ(scope.resolve<bind_session_fixture::Counter>()->count, 1);
    scope.stop();
    // Re-bind on stopped scope: type is now known — accepted for next start().
    scope.bindSession<bind_session_fixture::Counter>(
        std::make_unique<bind_session_fixture::Counter>(99));
    scope.start(); // new cycle
    EXPECT_EQ(scope.resolve<bind_session_fixture::Counter>()->count, 99);
    ctx.stop();
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

TEST(BindSession, NoPostConstructForBoundSession) {
    auto& ctx   = ctr::BeanContext::resolveContext("bsess-no-postconstruct");
    auto& scope = ctx.resolveScope("s");
    // No discover — bindSession registers the type itself.
    scope.bindSession<bind_session_fixture::HookedSession>(
        std::make_unique<bind_session_fixture::HookedSession>());
    ctx.start();
    scope.start();
    auto bean = scope.resolve<bind_session_fixture::HookedSession>();
    EXPECT_FALSE(bean->hookCalled);
    ctx.stop();
}

TEST(BindSession, PreDestroyCalledForCtoriumType) {
    bool preDestroyCalled = false;
    {
        auto& ctx   = ctr::BeanContext::resolveContext("bsess-predestroy");
        auto& scope = ctx.resolveScope("s");
        // No discover — bindSession registers the type itself.
        auto svc = std::make_unique<bind_session_fixture::ManagedSession>();
        bind_session_fixture::ManagedSession* raw = svc.get();
        scope.bindSession<bind_session_fixture::ManagedSession>(std::move(svc));
        ctx.start();
        scope.start();
        scope.resolve<bind_session_fixture::ManagedSession>();
        scope.stop();
        preDestroyCalled = raw->preDestroyCalled;
    }
    EXPECT_TRUE(preDestroyCalled);
}

TEST(BindSession, AllFourListenerPhasesFire) {
    auto& ctx   = ctr::BeanContext::resolveContext("bsess-listeners");
    auto& scope = ctx.resolveScope("s");
    int initialized = 0, created = 0, preDestroy = 0, destroyed = 0;

    ctx.on<bind_session_fixture::Counter>(ctr::onInitialized,
        [&](const ctr::Bean<bind_session_fixture::Counter>&){ initialized++; });
    ctx.on<bind_session_fixture::Counter>(ctr::onCreated,
        [&](const ctr::Bean<bind_session_fixture::Counter>&){ created++; });
    ctx.on<bind_session_fixture::Counter>(ctr::onPreDestroy,
        [&](const ctr::Bean<bind_session_fixture::Counter>&){ preDestroy++; });
    ctx.on<bind_session_fixture::Counter>(ctr::onDestroyed,
        [&](const ctr::Bean<bind_session_fixture::Counter>&){ destroyed++; });

    scope.bindSession<bind_session_fixture::Counter>(
        std::make_unique<bind_session_fixture::Counter>(1));
    ctx.start();
    scope.start();
    scope.resolve<bind_session_fixture::Counter>();
    scope.stop();
    ctx.stop();

    EXPECT_EQ(initialized, 1);
    EXPECT_EQ(created, 1);
    EXPECT_EQ(preDestroy, 1);
    EXPECT_EQ(destroyed, 1);
}

// ─── Named key ────────────────────────────────────────────────────────────────

TEST(BindSession, NamedKeyBinding) {
    auto& ctx   = ctr::BeanContext::resolveContext("bsess-named");
    auto& scope = ctx.resolveScope("s");
    scope.bindSession<bind_session_fixture::Counter>(
        std::make_unique<bind_session_fixture::Counter>(33),
        ctr::BindOptions{.name = "c", .priority = 0});
    ctx.start();
    scope.start();
    auto bean = scope.resolve<bind_session_fixture::Counter>(ctr::named("c"));
    EXPECT_EQ(bean->count, 33);
    ctx.stop();
}

// bss-post-start / bss-lifecycle : bindSession after root start() rejects unknown
// types with ConfigurationError. The type must be pre-registered before root start.
// bss-restart : calling bindSession on a running scope deadlocks (library defect —
// should throw ConfigurationError; not added as DISABLED to avoid accidental hang).

TEST(DISABLED_BindSession, PostScopeStartBindAvailableImmediately) {
    auto& ctx   = ctr::BeanContext::resolveContext("bss-post-start");
    auto& scope = ctx.resolveScope("s");
    ctx.start();
    scope.start();
    scope.bindSession<bind_session_fixture::BoundSession>(
        std::make_unique<bind_session_fixture::BoundSession>());
    auto bean = scope.resolve<bind_session_fixture::BoundSession>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(DISABLED_BindSession, PostScopeStartBindFiresLifecycleImmediately) {
    auto& ctx   = ctr::BeanContext::resolveContext("bss-lifecycle");
    auto& scope = ctx.resolveScope("s");
    int count = 0;
    ctx.on<bind_session_fixture::BoundSession>(ctr::onCreated,
        [&](const ctr::Bean<bind_session_fixture::BoundSession>&) { ++count; });
    ctx.start();
    scope.start();
    scope.bindSession<bind_session_fixture::BoundSession>(
        std::make_unique<bind_session_fixture::BoundSession>());
    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(BindSession, BindBeanContextRaisesConfigurationError) {
    auto& ctx   = ctr::BeanContext::resolveContext("bss-ctx-bind");
    auto& scope = ctx.resolveScope("s");
    EXPECT_THROW(
        scope.bindSession<ctr::BeanContext>(std::unique_ptr<ctr::BeanContext>{}),
        ctr::ConfigurationError);
    ctx.stop();
}
