#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────

namespace context_fixture {

struct [[=ctr::singleton{}]] Svc {};

// ContextAware: singleton whose constructor receives the root BeanContext via
// standard constructor injection.  Validates §5.5: any bean whose constructor
// declares ctr::Bean<ctr::BeanContext> receives the root context at construction
// time, regardless of whether the resolution is issued from a scope.
struct [[=ctr::singleton{}]] ContextAware {
    ctr::Bean<ctr::BeanContext> ctx;
    explicit ContextAware(ctr::Bean<ctr::BeanContext> c) : ctx(std::move(c)) {}
};

} // namespace context_fixture

// ─── Context identity tests ───────────────────────────────────────────────────

TEST(BeanContextIdentity, DefaultKeyEquivalentToEmptyString) {
    // resolveContext() is specified to resolve the default context.
    // Internally it maps to resolveContext(""); both overloads must return the
    // same stable reference.
    auto& def   = ctr::BeanContext::resolveContext();
    auto& empty = ctr::BeanContext::resolveContext("");
    EXPECT_EQ(&def, &empty);
    def.stop();
}

TEST(BeanContextIdentity, StopAndReopenGivesFreshContext) {
    // After stop(), the same key produces a new context that has not been
    // started: discover<>() must not raise ContextStateError.
    {
        auto& ctx = ctr::BeanContext::resolveContext("ctx-reopen");
        ctx.discover<^^context_fixture>().start();
        ctx.stop();
    }
    auto& ctx2 = ctr::BeanContext::resolveContext("ctx-reopen");
    EXPECT_NO_THROW(ctx2.discover<^^context_fixture>());
    ctx2.stop();
}

// ─── Self-injectable BeanContext (§5.5) ───────────────────────────────────────

TEST(BeanContextSelfInjectable, ResolvedHandleIsValid) {
    auto& ctx = ctr::BeanContext::resolveContext("ctx-self-valid");
    ctx.discover<^^context_fixture>().start();
    auto bean = ctx.resolve<ctr::BeanContext>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(BeanContextSelfInjectable, ResolveReturnsSelf) {
    // resolve<ctr::BeanContext>() must return the context itself (§5.5).
    auto& ctx = ctr::BeanContext::resolveContext("ctx-self");
    ctx.discover<^^context_fixture>().start();
    auto bean = ctx.resolve<ctr::BeanContext>();
    EXPECT_EQ(bean.operator->(), &ctx);
    ctx.stop();
}

TEST(BeanContextSelfInjectable, ResolveBeforeStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("ctx-self-pre-start");
    ctx.discover<^^context_fixture>();
    EXPECT_THROW(ctx.resolve<ctr::BeanContext>(), ctr::ContextStateError);
    ctx.stop();
}

TEST(BeanContextSelfInjectable, ContextInjectableIntoBean) {
    // A bean whose constructor takes ctr::Bean<ctr::BeanContext> receives the
    // root context, not the scope (§5.5).
    auto& ctx = ctr::BeanContext::resolveContext("ctx-inject");
    ctx.discover<^^context_fixture>().start();
    auto bean = ctx.resolve<context_fixture::ContextAware>();
    EXPECT_EQ(bean->ctx.operator->(), &ctx);
    ctx.stop();
}

TEST(BeanContextStop, StopPreventsResolve) {
    // After stop() (terminal), the context reference becomes dangling.
    // Verify: a fresh context for the same key is unstarted → resolve throws.
    {
        auto& ctx = ctr::BeanContext::resolveContext("ctx-stop");
        ctx.discover<^^context_fixture>().start();
        ctx.stop(); // terminal; ctx reference is now dangling
    }
    auto& fresh = ctr::BeanContext::resolveContext("ctx-stop");
    EXPECT_THROW(fresh.resolve<context_fixture::Svc>(), ctr::ContextStateError);
    fresh.stop();
}

TEST(BeanContextStop, StopIsTerminal) {
    // stop() releases the global-table entry; a subsequent resolveContext
    // on the same key returns a fresh, unstarted context.
    {
        auto& ctx = ctr::BeanContext::resolveContext("ctx-stop-close");
        ctx.discover<^^context_fixture>().start();
        ctx.stop(); // terminal; ctx reference is now dangling
    }
    auto& fresh = ctr::BeanContext::resolveContext("ctx-stop-close");
    EXPECT_THROW(fresh.resolve<context_fixture::Svc>(), ctr::ContextStateError);
    fresh.stop();
}

TEST(BeanContextDiscover, MultipleDiscoverCallsDeduplicateCandidates) {
    auto& ctx = ctr::BeanContext::resolveContext("ctx-multi-discover");
    ctx.discover<^^context_fixture>();
    ctx.discover<^^context_fixture>();
    ctx.start();
    // If deduplication fails, two candidates with equal priority would cause ambiguity.
    EXPECT_NO_THROW(ctx.resolve<context_fixture::Svc>());
    ctx.stop();
}