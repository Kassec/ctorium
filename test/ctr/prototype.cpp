#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────
//
// All fixture types live in a single namespace to ensure that the process-global
// typeIdFor<T> cache (static std::atomic<TypeId> per type) is populated from the
// same discovery set across all test contexts, preventing TypeId aliasing.

namespace prototype_fixture {

struct [[=CTORIUM_NAMESPACE::prototype{}]] Proto {};

struct [[=CTORIUM_NAMESPACE::prototype{}]] ProtoDep {};

// ProtoConsumer: prototype that depends on ProtoDep via constructor injection.
// Each resolve<ProtoConsumer>() materialises a new ProtoConsumer and, inside
// constructThunkInjected, a new ProtoDep — independent of other consumers.
struct [[=CTORIUM_NAMESPACE::prototype{}]] ProtoConsumer {
    CTORIUM_NAMESPACE::Bean<ProtoDep> dep;
    explicit ProtoConsumer(CTORIUM_NAMESPACE::Bean<ProtoDep> d) : dep(std::move(d)) {}
};

// ProtoHooked: prototype with a postConstruct hook.
// callCount is incremented once per materialisation; each resolve() produces a
// fresh instance, so each resolved bean has callCount == 1.
struct [[=CTORIUM_NAMESPACE::prototype{}]] ProtoHooked {
    int callCount = 0;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void init() { callCount++; }
};

} // namespace prototype_fixture

// Unregistered type — deliberately not discovered in any test context.
struct ProtoNotRegistered {};

// ─── Lifecycle state tests ────────────────────────────────────────────────────

TEST(Prototype, ResolveBeforeStartRaisesContextStateError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-pre-start");
    ctx.discover<^^prototype_fixture>();
    EXPECT_THROW(ctx.resolve<prototype_fixture::Proto>(), CTORIUM_NAMESPACE::ContextStateError);
    ctx.stop();
}

// ─── Resolution and identity tests ───────────────────────────────────────────

TEST(Prototype, ResolveReturnsValidBean) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-basic-resolve");
    ctx.discover<^^prototype_fixture>().start();
    auto bean = ctx.resolve<prototype_fixture::Proto>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(Prototype, EachResolveReturnsDistinctInstance) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-distinct");
    ctx.discover<^^prototype_fixture>().start();
    auto b1 = ctx.resolve<prototype_fixture::Proto>();
    auto b2 = ctx.resolve<prototype_fixture::Proto>();
    EXPECT_NE(b1.operator->(), b2.operator->());
    ctx.stop();
}

TEST(Prototype, HandleCopySharesInstancePointer) {
    // Verifies that Bean<T> copy retains the same Form 1 object pointer
    // (retainIfPrototype increments the refcount; releaseIfPrototype decrements).
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-copy");
    ctx.discover<^^prototype_fixture>().start();
    auto b1 = ctx.resolve<prototype_fixture::Proto>();
    auto b2 = b1; // copy — retainIfPrototype fires
    EXPECT_EQ(b1.operator->(), b2.operator->());
    ctx.stop();
}

TEST(Prototype, DifferentContextKeysYieldIndependentInstances) {
    auto& ctx1 = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-key-a");
    auto& ctx2 = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-key-b");
    ctx1.discover<^^prototype_fixture>().start();
    ctx2.discover<^^prototype_fixture>().start();
    auto b1 = ctx1.resolve<prototype_fixture::Proto>();
    auto b2 = ctx2.resolve<prototype_fixture::Proto>();
    EXPECT_NE(b1.operator->(), b2.operator->());
    ctx1.stop();
    ctx2.stop();
}

TEST(Prototype, ResolveOfUnregisteredTypeRaisesResolutionError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-unknown-type");
    ctx.discover<^^prototype_fixture>().start();
    EXPECT_THROW(ctx.resolve<ProtoNotRegistered>(), CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

// ─── Hook tests ───────────────────────────────────────────────────────────────

TEST(Prototype, PostConstructHookFiresOncePerInstance) {
    // Each resolve<ProtoHooked>() materialises a fresh instance; the hook fires
    // exactly once per instance.  Two consecutive resolves yield two independent
    // objects, each with callCount == 1.
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-post-construct");
    ctx.discover<^^prototype_fixture>().start();
    auto b1 = ctx.resolve<prototype_fixture::ProtoHooked>();
    EXPECT_EQ(b1->callCount, 1);
    auto b2 = ctx.resolve<prototype_fixture::ProtoHooked>();
    EXPECT_EQ(b2->callCount, 1);        // hook fires once on b2's own instance
    EXPECT_NE(b1.operator->(), b2.operator->()); // distinct objects
    ctx.stop();
}

// ─── Dependency injection tests ───────────────────────────────────────────────

TEST(Prototype, EachConsumerReceivesOwnDependencyInstance) {
    // Two ProtoConsumer resolves each trigger a separate ProtoDep materialisation.
    // The two dep pointers must be distinct.
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-injection");
    ctx.discover<^^prototype_fixture>().start();
    auto c1 = ctx.resolve<prototype_fixture::ProtoConsumer>();
    auto c2 = ctx.resolve<prototype_fixture::ProtoConsumer>();
    EXPECT_NE(c1->dep.operator->(), c2->dep.operator->());
    ctx.stop();
}

TEST(Prototype, LastHandleReleaseFiresDestroyedListener) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("pt-last-release");
    ctx.discover<^^prototype_fixture>().start();

    int destroyCount = 0;
    ctx.on(CTORIUM_NAMESPACE::onDestroyed, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++destroyCount; });

    {
        auto handle = ctx.resolve<prototype_fixture::Proto>();
    }

    EXPECT_EQ(destroyCount, 1);
    ctx.stop();
}