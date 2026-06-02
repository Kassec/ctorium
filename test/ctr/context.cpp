#include <atomic>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────

namespace context_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Svc {};

// ContextAware: singleton whose constructor receives the root BeanContext via
// standard constructor injection.  Validates that any bean whose constructor
// declares ctr::Bean<ctr::BeanContext> receives the root context at construction
// time, regardless of whether the resolution is issued from a scope.
struct [[=CTORIUM_NAMESPACE::singleton{}]] ContextAware {
    CTORIUM_NAMESPACE::Bean<CTORIUM_NAMESPACE::BeanContext> ctx;
    explicit ContextAware(CTORIUM_NAMESPACE::Bean<CTORIUM_NAMESPACE::BeanContext> c) : ctx(std::move(c)) {}
};

} // namespace context_fixture

// ─── Context identity tests ───────────────────────────────────────────────────

TEST(BeanContextIdentity, DefaultKeyEquivalentToEmptyString) {
    // resolveContext() is specified to resolve the default context.
    // Internally it maps to resolveContext(""); both overloads must return the
    // same stable reference.
    auto& def   = CTORIUM_NAMESPACE::BeanContext::resolveContext();
    auto& empty = CTORIUM_NAMESPACE::BeanContext::resolveContext("");
    EXPECT_EQ(&def, &empty);
    def.stop();
}

TEST(BeanContextIdentity, StopAndReopenGivesFreshContext) {
    // After stop(), the same key produces a new context that has not been
    // started: discover<>() must not raise ContextStateError.
    {
        auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-reopen");
        ctx.discover<^^context_fixture>().start();
        ctx.stop();
    }
    auto& ctx2 = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-reopen");
    EXPECT_NO_THROW(ctx2.discover<^^context_fixture>());
    ctx2.stop();
}

// ─── Self-injectable BeanContext ──────────────────────────────────────────────

TEST(BeanContextSelfInjectable, ResolvedHandleIsValid) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-self-valid");
    ctx.discover<^^context_fixture>().start();
    auto bean = ctx.resolve<CTORIUM_NAMESPACE::BeanContext>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(BeanContextSelfInjectable, ResolveReturnsSelf) {
    // resolve<ctr::BeanContext>() must return the context itself.
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-self");
    ctx.discover<^^context_fixture>().start();
    auto bean = ctx.resolve<CTORIUM_NAMESPACE::BeanContext>();
    EXPECT_EQ(bean.operator->(), &ctx);
    ctx.stop();
}

TEST(BeanContextSelfInjectable, ResolveBeforeStartRaisesContextStateError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-self-pre-start");
    ctx.discover<^^context_fixture>();
    EXPECT_THROW(ctx.resolve<CTORIUM_NAMESPACE::BeanContext>(), CTORIUM_NAMESPACE::ContextStateError);
    ctx.stop();
}

TEST(BeanContextSelfInjectable, ContextInjectableIntoBean) {
    // A bean whose constructor takes ctr::Bean<ctr::BeanContext> receives the
    // root context, not the scope.
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-inject");
    ctx.discover<^^context_fixture>().start();
    auto bean = ctx.resolve<context_fixture::ContextAware>();
    EXPECT_EQ(bean->ctx.operator->(), &ctx);
    ctx.stop();
}

TEST(BeanContextStop, StopPreventsResolve) {
    // After stop() (terminal), the context reference becomes dangling.
    // Verify: a fresh context for the same key is unstarted → resolve throws.
    {
        auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-stop");
        ctx.discover<^^context_fixture>().start();
        ctx.stop(); // terminal; ctx reference is now dangling
    }
    auto& fresh = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-stop");
    EXPECT_THROW(fresh.resolve<context_fixture::Svc>(), CTORIUM_NAMESPACE::ContextStateError);
    fresh.stop();
}

TEST(BeanContextStop, StopIsTerminal) {
    // stop() releases the global-table entry; a subsequent resolveContext
    // on the same key returns a fresh, unstarted context.
    {
        auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-stop-close");
        ctx.discover<^^context_fixture>().start();
        ctx.stop(); // terminal; ctx reference is now dangling
    }
    auto& fresh = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-stop-close");
    EXPECT_THROW(fresh.resolve<context_fixture::Svc>(), CTORIUM_NAMESPACE::ContextStateError);
    fresh.stop();
}

TEST(BeanContextDiscover, MultipleDiscoverCallsDeduplicateCandidates) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-multi-discover");
    ctx.discover<^^context_fixture>();
    ctx.discover<^^context_fixture>();
    ctx.start();
    // If deduplication fails, two candidates with equal priority would cause ambiguity.
    EXPECT_NO_THROW(ctx.resolve<context_fixture::Svc>());
    ctx.stop();
}

TEST(BeanContextScope, ResolveScopeFromScopeDelegatesToRootAndCreatesSibling) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-scope-delegates-root");
    auto& first = root.resolveScope("first");
    auto& siblingFromScope = first.resolveScope("sibling");
    auto& siblingFromRoot = root.resolveScope("sibling");

    EXPECT_NE(&first, &siblingFromScope);
    EXPECT_EQ(&siblingFromScope, &siblingFromRoot);

    root.stop();
}

TEST(BeanContextScope, SameScopeKeyUnderDifferentRootsCreatesDistinctScopes) {
    auto& firstRoot = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-root-a");
    auto& secondRoot = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-root-b");

    auto& firstScope = firstRoot.resolveScope("shared");
    auto& secondScope = secondRoot.resolveScope("shared");

    EXPECT_NE(&firstScope, &secondScope);

    firstRoot.stop();
    secondRoot.stop();
}

namespace context_scope_discover_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Service {};

} // namespace context_scope_discover_fixture

TEST(BeanContextScope, DiscoverOnScopeRaisesContextStateError) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-scope-discover");
    auto& scope = root.resolveScope("scope");

    EXPECT_THROW(scope.discover<^^context_scope_discover_fixture>(), CTORIUM_NAMESPACE::ContextStateError);

    root.stop();
}

namespace context_scope_identity_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] SessionService {
    int value = 1;
};

} // namespace context_scope_identity_fixture

TEST(BeanContextScope, ScopeIdentityAndTrackedHandlesSurviveStopStartCycle) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-scope-identity");
    root.discover<^^context_scope_identity_fixture>().start();
    auto& scope = root.resolveScope("stable");
    scope.start();

    auto session = scope.resolve<context_scope_identity_fixture::SessionService>();
    ASSERT_NE(session.operator->(), nullptr);
    auto* scopeAddress = &scope;

    scope.stop();
    EXPECT_EQ(session.operator->(), nullptr);

    scope.start();
    EXPECT_EQ(&scope, scopeAddress);
    EXPECT_EQ(&root.resolveScope("stable"), scopeAddress);
    EXPECT_NE(session.operator->(), nullptr);
    EXPECT_EQ(session->value, 1);

    root.stop();
}

namespace context_sibling_restart_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] SessionService {
    int value = 0;
};

} // namespace context_sibling_restart_fixture

TEST(BeanContextScope, RestartingOneScopeDoesNotAffectSiblingSessionInstance) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-scope-restart-sibling");
    root.discover<^^context_sibling_restart_fixture>().start();
    auto& first = root.resolveScope("first");
    auto& sibling = root.resolveScope("sibling");
    first.start();
    sibling.start();

    auto siblingBean = sibling.resolve<context_sibling_restart_fixture::SessionService>();
    auto* originalSibling = siblingBean.operator->();
    ASSERT_NE(originalSibling, nullptr);

    first.restart();

    EXPECT_EQ(siblingBean.operator->(), originalSibling);

    root.stop();
}

namespace context_root_stop_scope_sessions_fixture {

std::atomic<int> preDestroyCount{0};

struct [[=CTORIUM_NAMESPACE::session{}]] SessionService {
    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void cleanup() {
        preDestroyCount.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace context_root_stop_scope_sessions_fixture

TEST(BeanContextScope, RootStopDoesNotCurrentlyDestroyLiveScopeSessions) {
    context_root_stop_scope_sessions_fixture::preDestroyCount.store(
        0,
        std::memory_order_relaxed);

    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-root-stop-live-scope-sessions");
    root.discover<^^context_root_stop_scope_sessions_fixture>().start();
    auto& scope = root.resolveScope("ctx-root-stop-session-scope");
    scope.start();

    int destroyedCount = 0;
    root.on<context_root_stop_scope_sessions_fixture::SessionService>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<context_root_stop_scope_sessions_fixture::SessionService>&) {
            ++destroyedCount;
        });

    auto session = scope.resolve<context_root_stop_scope_sessions_fixture::SessionService>();
    ASSERT_NE(session.operator->(), nullptr);

    root.stop();

    EXPECT_EQ(
        context_root_stop_scope_sessions_fixture::preDestroyCount.load(
            std::memory_order_relaxed),
        1);
    EXPECT_EQ(destroyedCount, 1);
    EXPECT_EQ(session.operator->(), nullptr);
}

namespace context_destruction_order_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] UserSingleton {};

} // namespace context_destruction_order_fixture

TEST(BeanContextStop, ContextBeanDestroyedAfterUserSingletons) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-context-bean-last");
    root.discover<^^context_destruction_order_fixture>().start();

    std::vector<int> destroyedOrder;
    root.on(CTORIUM_NAMESPACE::onDestroyed, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<context_destruction_order_fixture::UserSingleton>()) {
            destroyedOrder.push_back(1);
        } else if (bean.compatible<CTORIUM_NAMESPACE::BeanContext>()) {
            destroyedOrder.push_back(2);
        }
    });

    (void)root.resolve<context_destruction_order_fixture::UserSingleton>();
    root.stop();

    ASSERT_EQ(destroyedOrder.size(), 2u);
    EXPECT_EQ(destroyedOrder[0], 1);
    EXPECT_EQ(destroyedOrder[1], 2);
}

TEST(BeanContextSelfInjectable, ContextBeanListenerPhasesFireInStandardOrder) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-context-bean-phases");

    std::vector<int> phases;
    root.on(CTORIUM_NAMESPACE::onInitialized, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<CTORIUM_NAMESPACE::BeanContext>()) phases.push_back(1);
    });
    root.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<CTORIUM_NAMESPACE::BeanContext>()) phases.push_back(2);
    });
    root.on(CTORIUM_NAMESPACE::onPreDestroy, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<CTORIUM_NAMESPACE::BeanContext>()) phases.push_back(3);
    });
    root.on(CTORIUM_NAMESPACE::onDestroyed, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<CTORIUM_NAMESPACE::BeanContext>()) phases.push_back(4);
    });

    root.start();
    root.stop();

    ASSERT_EQ(phases.size(), 4u);
    EXPECT_EQ(phases[0], 1);
    EXPECT_EQ(phases[1], 2);
    EXPECT_EQ(phases[2], 3);
    EXPECT_EQ(phases[3], 4);
}

namespace context_singleton_via_scope_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonService {};

} // namespace context_singleton_via_scope_fixture

TEST(BeanContextSelfInjectable, SingletonResolvedThroughScopeReportsRootContext) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ctx-singleton-via-scope-root");
    root.discover<^^context_singleton_via_scope_fixture>().start();
    auto& scope = root.resolveScope("singleton-scope");
    scope.start();

    auto bean = scope.resolve<context_singleton_via_scope_fixture::SingletonService>();

    EXPECT_EQ(&bean.context(), &root);

    root.stop();
}
