#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────
//
// Each test uses unique context keys to avoid registry collisions.
// Named/scoped annotations require const char* structural type (GCC 16.1.0+).

namespace session_fixture {

struct [[=ctr::session{}]] SessionSvc { int id = 0; };

struct [[=ctr::session{}]] SessionDep { int val = 99; };

// Consumer whose constructor injects a session dep implicitly (no scoped).
// Valid because the consumer itself is a session bean.
struct [[=ctr::session{}]] SessionConsumer {
    ctr::Bean<SessionDep> dep;
    explicit SessionConsumer(ctr::Bean<SessionDep> d) : dep(std::move(d)) {}
};

} // namespace session_fixture

// Separate namespace so that graph-validation tests don't pollute the main fixture.
namespace session_graphval {

struct [[=ctr::session{}]] GvSessionDep { int v = 0; };

// Condition (b): session dep injected into singleton without [[=ctr::scoped]].
struct [[=ctr::singleton{}]] GvBadConsumer {
    ctr::Bean<GvSessionDep> dep;
    explicit GvBadConsumer(ctr::Bean<GvSessionDep> d) : dep(std::move(d)) {}
};

} // namespace session_graphval

// Condition (a): [[=ctr::scoped]] on a non-session target.
// The scoped annotation is present on a parameter whose injected type is a singleton.
// Graph validation detects hasScopedAnnotation && injLt != Session → ConfigurationError.
namespace session_graphval_a {

struct [[=ctr::singleton{}]] GvSingleton { int x = 0; };

struct [[=ctr::singleton{}]] GvScopedOnSingleton {
    ctr::Bean<GvSingleton> dep;
    explicit GvScopedOnSingleton([[=ctr::scoped{}]] ctr::Bean<GvSingleton> d)
        : dep(std::move(d)) {}
};

} // namespace session_graphval_a

// ─── Criterion: same scope → same instance; distinct scopes → distinct instances ───

TEST(Session, SameScopeReturnsSameInstance) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-same-scope");
    ctx.discover<^^session_fixture>().start();
    auto& scope = ctx.resolveScope("s1");
    scope.start();
    auto b1 = scope.resolve<session_fixture::SessionSvc>();
    auto b2 = scope.resolve<session_fixture::SessionSvc>();
    EXPECT_EQ(b1.operator->(), b2.operator->());
    ctx.stop();
}

TEST(Session, DistinctScopesTwoInstances) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-two-scopes");
    ctx.discover<^^session_fixture>().start();
    auto& s1 = ctx.resolveScope("a");
    auto& s2 = ctx.resolveScope("b");
    s1.start();
    s2.start();
    auto b1 = s1.resolve<session_fixture::SessionSvc>();
    auto b2 = s2.resolve<session_fixture::SessionSvc>();
    EXPECT_NE(b1.operator->(), b2.operator->());
    ctx.stop();
}

// ─── Criterion: root resolve → ContextStateError ──────────────────────────────

TEST(Session, ResolveFromRootRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-from-root");
    ctx.discover<^^session_fixture>().start();
    EXPECT_THROW(ctx.resolve<session_fixture::SessionSvc>(), ctr::ContextStateError);
    ctx.stop();
}

// ─── Criterion: restart() → new instance; stop() → ContextStateError ──────────

TEST(Session, RestartReplacesInstance) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-restart");
    ctx.discover<^^session_fixture>().start();
    auto& scope = ctx.resolveScope("run");
    scope.start();

    // Mutate the first instance so we can detect re-construction.
    auto b1 = scope.resolve<session_fixture::SessionSvc>();
    EXPECT_NE(b1.operator->(), nullptr);
    b1->id = 99; // mutate; fresh instance has id = 0

    scope.restart(); // destroys old instance, creates new store

    // b1 is a Form 2 proxy: operator-> resolves the NEW instance after restart.
    // The new instance is default-initialized: id == 0, not 99.
    auto* newPtr = b1.operator->();
    EXPECT_NE(newPtr, nullptr);
    EXPECT_EQ(newPtr->id, 0); // freshly constructed — mutation is gone

    ctx.stop();
}

TEST(Session, ResolveOnStoppedScopeRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-stopped");
    ctx.discover<^^session_fixture>().start();
    auto& scope = ctx.resolveScope("run2");
    scope.start();
    scope.stop();
    EXPECT_THROW(scope.resolve<session_fixture::SessionSvc>(), ctr::ContextStateError);
    ctx.stop();
}

// ─── Criterion: Form 2 proxy → nullptr on stopped scope (no exception) ─────────

TEST(Session, ProxyHandleReturnNullptrOnStoppedScope) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-proxy-stop");
    ctx.discover<^^session_fixture>().start();
    auto& scope = ctx.resolveScope("run3");
    scope.start();

    // Resolve to get a Form 2 handle.
    auto bean = scope.resolve<session_fixture::SessionSvc>();
    EXPECT_NE(bean.operator->(), nullptr); // live scope: resolves correctly

    scope.stop();

    // After stop: operator-> must return nullptr without throwing.
    EXPECT_EQ(bean.operator->(), nullptr);
    ctx.stop();
}

// ─── Criterion: implicit session injection (session consumer) ─────────────────

TEST(Session, ImplicitSessionInjectionInSameScope) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-implicit-inject");
    ctx.discover<^^session_fixture>().start();
    auto& scope = ctx.resolveScope("run4");
    scope.start();

    auto consumer = scope.resolve<session_fixture::SessionConsumer>();
    EXPECT_NE(consumer.operator->(), nullptr);

    // The injected dep must be the same instance as a direct resolve.
    auto dep = scope.resolve<session_fixture::SessionDep>();
    EXPECT_EQ(consumer->dep.operator->(), dep.operator->());

    ctx.stop();
}

// ─── Criterion: bean.context() returns the owning ScopedContext ───────────────

TEST(Session, ContextReturnsScopedContext) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-context");
    ctx.discover<^^session_fixture>().start();
    auto& scope = ctx.resolveScope("run5");
    scope.start();

    auto bean = scope.resolve<session_fixture::SessionSvc>();
    EXPECT_EQ(&bean.context(), &scope);

    ctx.stop();
}

// ─── Criterion: graph validation condition (b) ────────────────────────────────
// Session dep injected into non-session consumer without [[=ctr::scoped]]
// → ConfigurationError at start().
//
// Note: condition (a) (scoped on non-session target) requires [[=ctr::scoped]]
// annotation; deferred pending GCC verify (structural type confirmed with
// const char* markers — separate probe recommended before enabling).

TEST(Session, GraphValidationSessionDepInNonSessionConsumerThrows) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-graphval-b");
    ctx.discover<^^session_graphval>();
    EXPECT_THROW(ctx.start(), ctr::ConfigurationError);
    ctx.stop();
}

// ─── ScopedContext::start() idempotent ────────────────────────────────────────

TEST(Session, ScopeStartIsIdempotent) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-idempotent");
    ctx.discover<^^session_fixture>().start();
    auto& scope = ctx.resolveScope("idem");
    scope.start();
    EXPECT_NO_THROW(scope.start()); // idempotent
    ctx.stop();
}

// ─── ScopedContext::stop() idempotent ─────────────────────────────────────────

TEST(Session, ScopeStopIsIdempotent) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-stop-idem");
    ctx.discover<^^session_fixture>().start();
    auto& scope = ctx.resolveScope("idem2");
    scope.start();
    scope.stop();
    EXPECT_NO_THROW(scope.stop()); // idempotent
    ctx.stop();
}

// ─── Criterion 7a: [[=ctr::scoped]] on a non-session target → ConfigurationError
// Uses [[=ctr::scoped{}]] (default null name) on a singleton dep.
// Boolean hasScopedAnnotation detection is sufficient — no name extraction needed.

TEST(Session, GraphValidationScopedOnNonSessionTargetThrows) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-graphval-a");
    ctx.discover<^^session_graphval_a>();
    EXPECT_THROW(ctx.start(), ctr::ConfigurationError);
    ctx.stop();
}

// ─── Lifecycle on scope stop/restart ──────────────────────────────────────────

namespace session_lifecycle_fixture {
struct [[=ctr::session{}]] LifecycleSvc {};
} // namespace session_lifecycle_fixture

TEST(Session, StopFiresPreDestroyThenDestroyed) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-stop-lifecycle");
    ctx.discover<^^session_lifecycle_fixture>().start();
    auto& scope = ctx.resolveScope("run-slc");

    int phase = 0, preDestroyOrder = 0, destroyedOrder = 0;
    ctx.on<session_lifecycle_fixture::LifecycleSvc>(
        ctr::onPreDestroy,
        [&](const ctr::Bean<session_lifecycle_fixture::LifecycleSvc>&) {
            preDestroyOrder = ++phase;
        });
    ctx.on<session_lifecycle_fixture::LifecycleSvc>(
        ctr::onDestroyed,
        [&](const ctr::Bean<session_lifecycle_fixture::LifecycleSvc>&) {
            destroyedOrder = ++phase;
        });

    scope.start();
    scope.resolve<session_lifecycle_fixture::LifecycleSvc>();
    scope.stop();

    EXPECT_GT(preDestroyOrder, 0);
    EXPECT_GT(destroyedOrder, 0);
    EXPECT_LT(preDestroyOrder, destroyedOrder);
    ctx.stop();
}

TEST(Session, RestartFiresDestroyThenCreateLifecycle) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-restart-lifecycle");
    ctx.discover<^^session_lifecycle_fixture>().start();
    auto& scope = ctx.resolveScope("run-rlc");

    int phaseOrder = 0;
    int initOrder1 = 0, createdOrder1 = 0, preDestroyOrder = 0, destroyedOrder = 0;
    int initOrder2 = 0, createdOrder2 = 0;

    ctx.on<session_lifecycle_fixture::LifecycleSvc>(
        ctr::onInitialized,
        [&](const ctr::Bean<session_lifecycle_fixture::LifecycleSvc>&) {
            int v = ++phaseOrder;
            if (initOrder1 == 0) initOrder1 = v; else initOrder2 = v;
        });
    ctx.on<session_lifecycle_fixture::LifecycleSvc>(
        ctr::onCreated,
        [&](const ctr::Bean<session_lifecycle_fixture::LifecycleSvc>&) {
            int v = ++phaseOrder;
            if (createdOrder1 == 0) createdOrder1 = v; else createdOrder2 = v;
        });
    ctx.on<session_lifecycle_fixture::LifecycleSvc>(
        ctr::onPreDestroy,
        [&](const ctr::Bean<session_lifecycle_fixture::LifecycleSvc>&) {
            preDestroyOrder = ++phaseOrder;
        });
    ctx.on<session_lifecycle_fixture::LifecycleSvc>(
        ctr::onDestroyed,
        [&](const ctr::Bean<session_lifecycle_fixture::LifecycleSvc>&) {
            destroyedOrder = ++phaseOrder;
        });

    scope.start();
    scope.resolve<session_lifecycle_fixture::LifecycleSvc>();
    EXPECT_EQ(initOrder1, 1);
    EXPECT_EQ(createdOrder1, 2);

    scope.restart();
    EXPECT_EQ(preDestroyOrder, 3);
    EXPECT_EQ(destroyedOrder, 4);

    // Session beans are materialized lazily on first resolve after restart.
    scope.resolve<session_lifecycle_fixture::LifecycleSvc>();
    EXPECT_EQ(initOrder2, 5);
    EXPECT_EQ(createdOrder2, 6);

    ctx.stop();
}

// ─── Scoped injection (SPEC-scoped-injection) ─────────────────────────────────
// Singleton with [[=ctr::scoped{.name=...}]] parameter: deferred Form 2 handle.

namespace scoped_inject_fixture {

struct [[=ctr::session{}]] GameSvc { int id = 42; };

struct [[=ctr::session{}]]
       [[=ctr::named{.name = std::define_static_string("primary")}]]
       PrimaryGameSvc { int id = 99; };

struct [[=ctr::singleton{}]] GameConsumer {
    ctr::Bean<GameSvc> svc;
    explicit GameConsumer(
        [[=ctr::scoped{.name = std::define_static_string("game")}]]
        ctr::Bean<GameSvc> s) : svc(std::move(s)) {}
};

struct [[=ctr::singleton{}]] NamedGameConsumer {
    ctr::Bean<PrimaryGameSvc> svc;
    explicit NamedGameConsumer(
        [[=ctr::scoped{.name = std::define_static_string("game")}]]
        [[=ctr::named{.name = std::define_static_string("primary")}]]
        ctr::Bean<PrimaryGameSvc> s) : svc(std::move(s)) {}
};

} // namespace scoped_inject_fixture

TEST(Session, ScopedInjection_HandleIsNullBeforeScopeStart) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-scoped-before-start");
    ctx.discover<^^scoped_inject_fixture>().start();
    // GameConsumer is lazy (default singleton): construct it now.
    // The scope "game" has not been started → deferred handle returns nullptr.
    auto consumer = ctx.resolve<scoped_inject_fixture::GameConsumer>();
    EXPECT_EQ(consumer->svc.operator->(), nullptr);
    ctx.stop();
}

TEST(Session, ScopedInjection_ResolvesAfterScopeStart) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-scoped-after-start");
    ctx.discover<^^scoped_inject_fixture>().start();
    auto& scope = ctx.resolveScope("game");
    scope.start();
    auto consumer = ctx.resolve<scoped_inject_fixture::GameConsumer>();
    EXPECT_NE(consumer->svc.operator->(), nullptr);
    EXPECT_EQ(consumer->svc->id, 42);
    ctx.stop();
}

TEST(Session, ScopedInjection_NullptrWhenScopeStopped) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-scoped-scope-stopped");
    ctx.discover<^^scoped_inject_fixture>().start();
    auto& scope = ctx.resolveScope("game");
    scope.start();
    auto consumer = ctx.resolve<scoped_inject_fixture::GameConsumer>();
    EXPECT_NE(consumer->svc.operator->(), nullptr);
    scope.stop();
    EXPECT_EQ(consumer->svc.operator->(), nullptr); // no exception
    ctx.stop();
}

TEST(Session, ScopedNamedInjection_TargetsNamedBean) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-scoped-named");
    ctx.discover<^^scoped_inject_fixture>().start();
    auto& scope = ctx.resolveScope("game");
    scope.start();
    auto consumer = ctx.resolve<scoped_inject_fixture::NamedGameConsumer>();
    EXPECT_NE(consumer->svc.operator->(), nullptr);
    EXPECT_EQ(consumer->svc->id, 99);
    ctx.stop();
}

// ─── Risk 1: polymorphic exposure of a session bean ───────────────────────────
//
// Resolving the session bean through its base type (exposed alias) must return
// a non-null handle on the correctly constructed primary instance.
// These remain red until Session alias→primary redirection is implemented in
// materializeOne.

namespace session_poly_fixture {

struct SessionPolyBase {
    int sentinel = 42;
};

struct [[=ctr::session{}]] SessionPolyConcrete : public SessionPolyBase {};

} // namespace session_poly_fixture

TEST(Session, PolymorphicExposureResolvesBase) {
    auto& ctx = ctr::BeanContext::resolveContext("ss-poly-expose");
    ctx.discover<^^session_poly_fixture>().start();
    auto& scope = ctx.resolveScope("poly-scope");
    scope.start();

    auto baseBean = scope.resolve<session_poly_fixture::SessionPolyBase>();
    ASSERT_NE(baseBean.operator->(), nullptr);
    EXPECT_EQ(baseBean->sentinel, 42);

    auto concreteBean = scope.resolve<session_poly_fixture::SessionPolyConcrete>();
    ASSERT_NE(concreteBean.operator->(), nullptr);

    // Simple public inheritance offset-0: raw base pointer == raw concrete pointer.
    EXPECT_EQ(
        static_cast<void*>(baseBean.operator->()),
        static_cast<void*>(concreteBean.operator->())
    );

    ctx.stop();
}
