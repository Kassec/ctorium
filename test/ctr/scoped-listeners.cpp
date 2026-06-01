#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

#include <memory>
#include <vector>

// --- Fixtures ---------------------------------------------------------------

namespace scoped_listener_fixture {

struct [[=ctr::session{}]] ScopedSvc {};
struct [[=ctr::session{}]] OtherScopedSvc {};
struct [[=ctr::singleton{}]] RootSingleton {};
struct [[=ctr::prototype{}]] RootProto {};

} // namespace scoped_listener_fixture

// --- ScopedListener: scope listener filtering -------------------------------

TEST(ScopedListener, ScopeListenerFiresOnlyForItsOwnScope_Creation) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-own-scope-creation");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int count = 0;
    scopeA.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&count](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) { ++count; });

    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerDoesNotFireForRootSingleton) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-no-root-singleton");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int count = 0;
    scope.on<scoped_listener_fixture::RootSingleton>(
        ctr::onCreated,
        [&count](const ctr::Bean<scoped_listener_fixture::RootSingleton>&) { ++count; });

    ctx.resolve<scoped_listener_fixture::RootSingleton>();

    EXPECT_EQ(count, 0);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerDoesNotFireForPrototype) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-no-root-prototype");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int count = 0;
    scope.on<scoped_listener_fixture::RootProto>(
        ctr::onCreated,
        [&count](const ctr::Bean<scoped_listener_fixture::RootProto>&) { ++count; });

    auto bean = ctx.resolve<scoped_listener_fixture::RootProto>();

    EXPECT_EQ(count, 0);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerFiresForAllFourPhasesOfItsScope) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-four-phases");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int phaseOrder = 0;
    int initOrder = 0;
    int createdOrder = 0;
    int preDestroyOrder = 0;
    int destroyedOrder = 0;

    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onInitialized,
        [&](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            initOrder = ++phaseOrder;
        });
    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            createdOrder = ++phaseOrder;
        });
    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onPreDestroy,
        [&](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            preDestroyOrder = ++phaseOrder;
        });
    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onDestroyed,
        [&](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            destroyedOrder = ++phaseOrder;
        });

    scope.resolve<scoped_listener_fixture::ScopedSvc>();
    scope.stop();

    EXPECT_EQ(initOrder, 1);
    EXPECT_EQ(createdOrder, 2);
    EXPECT_EQ(preDestroyOrder, 3);
    EXPECT_EQ(destroyedOrder, 4);
    EXPECT_LT(initOrder, createdOrder);
    EXPECT_LT(createdOrder, preDestroyOrder);
    EXPECT_LT(preDestroyOrder, destroyedOrder);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerDoesNotFireOnDestructionOfOtherScope) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-other-scope-destruction");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int destroyed = 0;
    scopeA.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onDestroyed,
        [&destroyed](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++destroyed;
        });

    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.stop();

    EXPECT_EQ(destroyed, 0);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerGlobalFormFiresOnlyForItsScope) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-global-form-scope-only");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int count = 0;
    scope.on(ctr::onCreated, [&count](const ctr::AnyBean&) { ++count; });

    ctx.resolve<scoped_listener_fixture::RootSingleton>();
    scope.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerPriorityOrderPreservedUnderFiltering) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-priority-filtered");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    std::vector<int> order;
    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&order](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            order.push_back(10);
        },
        ctr::ListenerOptions{.priority = 10});
    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&order](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            order.push_back(5);
        },
        ctr::ListenerOptions{.priority = 5});

    scope.resolve<scoped_listener_fixture::ScopedSvc>();

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 10);
    EXPECT_EQ(order[1], 5);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerRegisteredPreStartFiresForItsScope) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-prestart-scope-listener");
    auto& scope = ctx.resolveScope("scope");
    ctx.discover<^^scoped_listener_fixture>();

    int count = 0;
    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&count](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) { ++count; });

    ctx.start();
    scope.start();
    scope.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerBindSessionPostStartDispatchIsScoped) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-bind-session-post-start");
    auto& scope = ctx.resolveScope("scope");

    int scopedCount = 0;
    int rootCount = 0;
    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&scopedCount](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++scopedCount;
        });
    ctx.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&rootCount](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++rootCount;
        });

    ctx.start();
    scope.start();
    scope.bindSession<scoped_listener_fixture::ScopedSvc>(
        std::make_unique<scoped_listener_fixture::ScopedSvc>());

    EXPECT_EQ(scopedCount, 1);
    EXPECT_EQ(rootCount, 1);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerBindSessionPendingAtStartDispatchIsScoped) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-bind-session-pending");
    auto& scope = ctx.resolveScope("scope");

    int count = 0;
    scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&count](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) { ++count; });
    scope.bindSession<scoped_listener_fixture::ScopedSvc>(
        std::make_unique<scoped_listener_fixture::ScopedSvc>());

    ctx.start();
    scope.start();

    EXPECT_EQ(count, 1);
    ctx.stop();
}

// --- RootListenerScope: root behavior remains broad -------------------------

TEST(RootListenerScope, RootListenerFiresForSingletonAndAllScopes) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-root-sees-all-creation");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int count = 0;
    ctx.on(ctr::onCreated, [&count](const ctr::AnyBean& bean) {
        if (bean.compatible<scoped_listener_fixture::RootSingleton>()
                || bean.compatible<scoped_listener_fixture::ScopedSvc>()) {
            ++count;
        }
    });

    ctx.resolve<scoped_listener_fixture::RootSingleton>();
    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 3);
    ctx.stop();
}

TEST(RootListenerScope, RootListenerFiresForAllDestructionPaths) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-root-sees-all-destroy");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int count = 0;
    ctx.on(ctr::onPreDestroy, [&count](const ctr::AnyBean& bean) {
        if (bean.compatible<scoped_listener_fixture::RootSingleton>()
                || bean.compatible<scoped_listener_fixture::ScopedSvc>()) {
            ++count;
        }
    });

    ctx.resolve<scoped_listener_fixture::RootSingleton>();
    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();
    ctx.stop();

    EXPECT_EQ(count, 3);
}

TEST(RootListenerScope, RootTypedListenerFiresForSessionBeanInAnyScope) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-root-typed-any-scope");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int count = 0;
    ctx.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&count](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++count;
        });

    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 2);
    ctx.stop();
}

// --- ScopedListenerIsolation: independent scopes ----------------------------

TEST(ScopedListenerIsolation, TwoScopesWithSeparateListenersSeeOnlyOwnBeans) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-two-scopes-create");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int countA = 0;
    int countB = 0;
    scopeA.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&countA](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++countA;
        });
    scopeB.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&countB](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++countB;
        });

    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 1);
    ctx.stop();
}

TEST(ScopedListenerIsolation, TwoScopesDestructionIsolated) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-two-scopes-destroy");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int countA = 0;
    int countB = 0;
    scopeA.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onDestroyed,
        [&countA](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++countA;
        });
    scopeB.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onDestroyed,
        [&countB](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++countB;
        });

    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();

    scopeA.stop();
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 0);

    scopeB.stop();
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 1);
    ctx.stop();
}

// --- ScopedListenerHandle: scoped listener removal --------------------------

TEST(ScopedListenerHandle, ScopedListenerRemoveByHandleStopsDispatches) {
    auto& ctx = ctr::BeanContext::resolveContext("slc-remove-handle");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int count = 0;
    auto handle = scope.on<scoped_listener_fixture::ScopedSvc>(
        ctr::onCreated,
        [&count](const ctr::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++count;
        });
    handle.remove();

    scope.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 0);
    ctx.stop();
}
