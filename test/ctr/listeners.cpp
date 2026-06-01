#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────

namespace listener_fixture {

struct [[=ctr::singleton{}]] ServiceA {};
struct [[=ctr::singleton{}]] ServiceB {};
struct [[=ctr::prototype{}]] Widget {};

} // namespace listener_fixture

// ─── Typed listener — fires only for the target type ────────────────────────

TEST(Listener, TypedListenerFiresOnlyForTargetType) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-typed-filter");
    ctx.discover<^^listener_fixture>().start();

    int countA = 0;
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&countA](const ctr::Bean<listener_fixture::ServiceA>&) { ++countA; });

    ctx.resolve<listener_fixture::ServiceA>();
    ctx.resolve<listener_fixture::ServiceB>();

    EXPECT_EQ(countA, 1); // only ServiceA triggered the typed listener
    ctx.stop();
}

TEST(Listener, TypedListenerNotFiredBySubsequentResolvesOfSameSingleton) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-typed-singleton-once");
    ctx.discover<^^listener_fixture>().start();

    int countA = 0;
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&countA](const ctr::Bean<listener_fixture::ServiceA>&) { ++countA; });

    ctx.resolve<listener_fixture::ServiceA>();
    ctx.resolve<listener_fixture::ServiceA>(); // already materialized; no event
    EXPECT_EQ(countA, 1);
    ctx.stop();
}

// ─── Global listener — fires for all beans ───────────────────────────────────

TEST(Listener, GlobalListenerFiresForAllBeans) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-global-all");
    ctx.discover<^^listener_fixture>().start();

    int callCount = 0;
    ctx.on(
        ctr::onCreated,
        [&callCount](const ctr::AnyBean&) { ++callCount; });

    ctx.resolve<listener_fixture::ServiceA>();
    ctx.resolve<listener_fixture::ServiceB>();

    EXPECT_EQ(callCount, 2);
    ctx.stop();
}

// ─── Multiple independent registrations ──────────────────────────────────────

TEST(Listener, MultipleListenerRegistrationsAreIndependent) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-multi-reg");
    ctx.discover<^^listener_fixture>().start();

    int count1 = 0, count2 = 0;
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&count1](const ctr::Bean<listener_fixture::ServiceA>&) { ++count1; });
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&count2](const ctr::Bean<listener_fixture::ServiceA>&) { ++count2; });

    ctx.resolve<listener_fixture::ServiceA>();
    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);
    ctx.stop();
}

// ─── Priority ordering ────────────────────────────────────────────────────────

TEST(Listener, HigherPriorityListenerFiresFirst) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-priority");
    ctx.discover<^^listener_fixture>().start();

    int lastFired = 0;
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&lastFired](const ctr::Bean<listener_fixture::ServiceA>&) { lastFired = 1; },
        ctr::ListenerOptions{.priority = 10});
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&lastFired](const ctr::Bean<listener_fixture::ServiceA>&) { lastFired = 2; },
        ctr::ListenerOptions{.priority = 5});

    ctx.resolve<listener_fixture::ServiceA>();
    // Listener with priority 5 fired last (lower priority executes last).
    EXPECT_EQ(lastFired, 2);
    ctx.stop();
}

// ─── remove — idempotent unregistration ──────────────────────────────────────

TEST(Listener, RemoveByHandlePreventsSubsequentDispatches) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-remove-handle");
    ctx.discover<^^listener_fixture>().start();

    int count = 0;
    auto handle = ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&count](const ctr::Bean<listener_fixture::ServiceA>&) { ++count; });

    handle.remove();

    ctx.resolve<listener_fixture::ServiceA>();
    EXPECT_EQ(count, 0);
    ctx.stop();
}

TEST(Listener, ContextRemoveByHandlePreventsSubsequentDispatches) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-ctx-remove");
    ctx.discover<^^listener_fixture>().start();

    int count = 0;
    auto handle = ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&count](const ctr::Bean<listener_fixture::ServiceA>&) { ++count; });

    ctx.remove(handle);

    ctx.resolve<listener_fixture::ServiceA>();
    EXPECT_EQ(count, 0);
    ctx.stop();
}

TEST(Listener, RemoveIsIdempotent) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-remove-idempotent");
    ctx.discover<^^listener_fixture>().start();

    int count = 0;
    auto handle = ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&count](const ctr::Bean<listener_fixture::ServiceA>&) { ++count; });

    handle.remove();
    handle.remove(); // idempotent — must not crash or throw
    ctx.remove(handle); // also idempotent through BeanContext

    ctx.resolve<listener_fixture::ServiceA>();
    EXPECT_EQ(count, 0);
    ctx.stop();
}

// ─── Pre-start typed listener registration ───────────────────────────────────

TEST(Listener, PreStartTypedListenerFiresOnlyForTargetType) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-prestart-typed-filter");
    ctx.discover<^^listener_fixture>();

    int countA = 0;
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&countA](const ctr::Bean<listener_fixture::ServiceA>&) { ++countA; });

    ctx.start();
    ctx.resolve<listener_fixture::ServiceA>();
    ctx.resolve<listener_fixture::ServiceB>();

    EXPECT_EQ(countA, 1); // must fire for ServiceA, not for ServiceB
    ctx.stop();
}

TEST(Listener, PreStartTypedListenerContinuesWorkingPostStart) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-prestart-post-start");
    ctx.discover<^^listener_fixture>();

    int countA = 0;
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&countA](const ctr::Bean<listener_fixture::ServiceA>&) { ++countA; });

    ctx.start();
    ctx.resolve<listener_fixture::ServiceA>();
    EXPECT_EQ(countA, 1);
    ctx.stop();
}

// ─── All four lifecycle phases ────────────────────────────────────────────────

TEST(Listener, AllFourPhasesFireInOrderForSingleton) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-phases");
    ctx.discover<^^listener_fixture>().start();

    int phaseOrder = 0;
    int initOrder = 0, createdOrder = 0, preDestroyOrder = 0, destroyedOrder = 0;
    ctx.on<listener_fixture::ServiceA>(
        ctr::onInitialized,
        [&](const ctr::Bean<listener_fixture::ServiceA>&) {
            initOrder = ++phaseOrder;
        });
    ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&](const ctr::Bean<listener_fixture::ServiceA>&) {
            createdOrder = ++phaseOrder;
        });
    ctx.on<listener_fixture::ServiceA>(
        ctr::onPreDestroy,
        [&](const ctr::Bean<listener_fixture::ServiceA>&) {
            preDestroyOrder = ++phaseOrder;
        });
    ctx.on<listener_fixture::ServiceA>(
        ctr::onDestroyed,
        [&](const ctr::Bean<listener_fixture::ServiceA>&) {
            destroyedOrder = ++phaseOrder;
        });

    ctx.resolve<listener_fixture::ServiceA>();
    EXPECT_EQ(initOrder, 1);
    EXPECT_EQ(createdOrder, 2);

    ctx.stop(); // triggers preDestroy and destroyed
    EXPECT_EQ(preDestroyOrder, 3);
    EXPECT_EQ(destroyedOrder, 4);
}

TEST(Listener, PrototypeAllFourPhasesFireInOrder) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-proto-phases");
    ctx.discover<^^listener_fixture>().start();

    int phaseOrder = 0;
    int initOrder = 0, createdOrder = 0, preDestroyOrder = 0, destroyedOrder = 0;
    ctx.on<listener_fixture::Widget>(
        ctr::onInitialized,
        [&](const ctr::Bean<listener_fixture::Widget>&) { initOrder = ++phaseOrder; });
    ctx.on<listener_fixture::Widget>(
        ctr::onCreated,
        [&](const ctr::Bean<listener_fixture::Widget>&) { createdOrder = ++phaseOrder; });
    ctx.on<listener_fixture::Widget>(
        ctr::onPreDestroy,
        [&](const ctr::Bean<listener_fixture::Widget>&) { preDestroyOrder = ++phaseOrder; });
    ctx.on<listener_fixture::Widget>(
        ctr::onDestroyed,
        [&](const ctr::Bean<listener_fixture::Widget>&) { destroyedOrder = ++phaseOrder; });

    {
        auto handle = ctx.resolve<listener_fixture::Widget>();
        EXPECT_EQ(initOrder, 1);
        EXPECT_EQ(createdOrder, 2);
    }

    EXPECT_EQ(preDestroyOrder, 3);
    EXPECT_EQ(destroyedOrder, 4);
    ctx.stop();
}

TEST(Listener, GlobalListenerHigherPriorityFiresFirst) {
    auto& ctx = ctr::BeanContext::resolveContext("ls-global-priority");
    ctx.discover<^^listener_fixture>().start();

    int lastFired = 0;
    ctx.on(ctr::onCreated,
        [&lastFired](const ctr::AnyBean&) { lastFired = 1; },
        ctr::ListenerOptions{.priority = 10});
    ctx.on(ctr::onCreated,
        [&lastFired](const ctr::AnyBean&) { lastFired = 2; },
        ctr::ListenerOptions{.priority = 5});

    ctx.resolve<listener_fixture::ServiceA>();
    EXPECT_EQ(lastFired, 2);
    ctx.stop();
}

// ─── A1 — ListenerHandle: copy + double remove (UAF fix) ─────────────────────

TEST(ListenerHandle, CopyThenDoubleRemoveDoesNotCrash) {
    auto& ctx = ctr::BeanContext::resolveContext("lh-copy-double-remove");
    ctx.discover<^^listener_fixture>().start();

    int count = 0;
    auto h1 = ctx.on<listener_fixture::ServiceA>(
        ctr::onCreated,
        [&count](const ctr::Bean<listener_fixture::ServiceA>&) { ++count; });
    auto h2 = h1; // copy: both share the same token in the store

    h1.remove(); // removes the token from the store
    h2.remove(); // token already absent → removeByToken is a no-op, no UAF

    ctx.resolve<listener_fixture::ServiceA>();
    EXPECT_EQ(count, 0); // listener was removed before resolve
    ctx.stop();
}

