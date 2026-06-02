#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

#include <memory>
#include <vector>

// --- Fixtures ---------------------------------------------------------------

namespace scoped_listener_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] ScopedSvc {};
struct [[=CTORIUM_NAMESPACE::session{}]] OtherScopedSvc {};
struct [[=CTORIUM_NAMESPACE::singleton{}]] RootSingleton {};
struct [[=CTORIUM_NAMESPACE::prototype{}]] RootProto {};

} // namespace scoped_listener_fixture

// --- ScopedListener: scope listener filtering -------------------------------

TEST(ScopedListener, ScopeListenerFiresOnlyForItsOwnScope_Creation) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-own-scope-creation");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int count = 0;
    scopeA.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&count](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) { ++count; });

    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerDoesNotFireForRootSingleton) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-no-root-singleton");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int count = 0;
    scope.on<scoped_listener_fixture::RootSingleton>(
        CTORIUM_NAMESPACE::onCreated,
        [&count](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::RootSingleton>&) { ++count; });

    ctx.resolve<scoped_listener_fixture::RootSingleton>();

    EXPECT_EQ(count, 0);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerDoesNotFireForPrototype) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-no-root-prototype");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int count = 0;
    scope.on<scoped_listener_fixture::RootProto>(
        CTORIUM_NAMESPACE::onCreated,
        [&count](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::RootProto>&) { ++count; });

    auto bean = ctx.resolve<scoped_listener_fixture::RootProto>();

    EXPECT_EQ(count, 0);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerFiresForAllFourPhasesOfItsScope) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-four-phases");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int phaseOrder = 0;
    int initOrder = 0;
    int createdOrder = 0;
    int preDestroyOrder = 0;
    int destroyedOrder = 0;

    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onInitialized,
        [&](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            initOrder = ++phaseOrder;
        });
    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            createdOrder = ++phaseOrder;
        });
    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onPreDestroy,
        [&](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            preDestroyOrder = ++phaseOrder;
        });
    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
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
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-other-scope-destruction");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int destroyed = 0;
    scopeA.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&destroyed](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++destroyed;
        });

    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.stop();

    EXPECT_EQ(destroyed, 0);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerGlobalFormFiresOnlyForItsScope) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-global-form-scope-only");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int count = 0;
    scope.on(CTORIUM_NAMESPACE::onCreated, [&count](const CTORIUM_NAMESPACE::AnyBean&) { ++count; });

    ctx.resolve<scoped_listener_fixture::RootSingleton>();
    scope.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerPriorityOrderPreservedUnderFiltering) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-priority-filtered");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    std::vector<int> order;
    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&order](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            order.push_back(10);
        },
        CTORIUM_NAMESPACE::ListenerOptions{.priority = 10});
    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&order](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            order.push_back(5);
        },
        CTORIUM_NAMESPACE::ListenerOptions{.priority = 5});

    scope.resolve<scoped_listener_fixture::ScopedSvc>();

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 10);
    EXPECT_EQ(order[1], 5);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerRegisteredPreStartFiresForItsScope) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-prestart-scope-listener");
    auto& scope = ctx.resolveScope("scope");
    ctx.discover<^^scoped_listener_fixture>();

    int count = 0;
    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&count](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) { ++count; });

    ctx.start();
    scope.start();
    scope.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 1);
    ctx.stop();
}

TEST(ScopedListener, ScopeListenerBindSessionPostStartDispatchIsScoped) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-bind-session-post-start");
    auto& scope = ctx.resolveScope("scope");

    int scopedCount = 0;
    int rootCount = 0;
    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&scopedCount](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++scopedCount;
        });
    ctx.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&rootCount](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
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
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-bind-session-pending");
    auto& scope = ctx.resolveScope("scope");

    int count = 0;
    scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&count](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) { ++count; });
    scope.bindSession<scoped_listener_fixture::ScopedSvc>(
        std::make_unique<scoped_listener_fixture::ScopedSvc>());

    ctx.start();
    scope.start();

    EXPECT_EQ(count, 1);
    ctx.stop();
}

// --- RootListenerScope: root behavior remains broad -------------------------

TEST(RootListenerScope, RootListenerFiresForSingletonAndAllScopes) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-root-sees-all-creation");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int count = 0;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&count](const CTORIUM_NAMESPACE::AnyBean& bean) {
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
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-root-sees-all-destroy");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int count = 0;
    ctx.on(CTORIUM_NAMESPACE::onPreDestroy, [&count](const CTORIUM_NAMESPACE::AnyBean& bean) {
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
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-root-typed-any-scope");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int count = 0;
    ctx.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&count](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++count;
        });

    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 2);
    ctx.stop();
}

// --- ScopedListenerIsolation: independent scopes ----------------------------

TEST(ScopedListenerIsolation, TwoScopesWithSeparateListenersSeeOnlyOwnBeans) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-two-scopes-create");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int countA = 0;
    int countB = 0;
    scopeA.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&countA](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++countA;
        });
    scopeB.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&countB](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++countB;
        });

    scopeA.resolve<scoped_listener_fixture::ScopedSvc>();
    scopeB.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 1);
    ctx.stop();
}

TEST(ScopedListenerIsolation, TwoScopesDestructionIsolated) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-two-scopes-destroy");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scopeA = ctx.resolveScope("a");
    auto& scopeB = ctx.resolveScope("b");
    scopeA.start();
    scopeB.start();

    int countA = 0;
    int countB = 0;
    scopeA.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&countA](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++countA;
        });
    scopeB.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&countB](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
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
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("slc-remove-handle");
    ctx.discover<^^scoped_listener_fixture>().start();
    auto& scope = ctx.resolveScope("scope");
    scope.start();

    int count = 0;
    auto handle = scope.on<scoped_listener_fixture::ScopedSvc>(
        CTORIUM_NAMESPACE::onCreated,
        [&count](const CTORIUM_NAMESPACE::Bean<scoped_listener_fixture::ScopedSvc>&) {
            ++count;
        });
    handle.remove();

    scope.resolve<scoped_listener_fixture::ScopedSvc>();

    EXPECT_EQ(count, 0);
    ctx.stop();
}
