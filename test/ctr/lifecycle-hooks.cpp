#include <meta>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace lifecycle_inherited_hooks_fixture {

inline std::vector<int> calls;

struct Base {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void baseInit() {
        calls.push_back(1);
    }

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void baseCleanup() {
        calls.push_back(4);
    }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Derived : Base {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void derivedInit() {
        calls.push_back(2);
    }

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void derivedCleanup() {
        calls.push_back(3);
    }
};

} // namespace lifecycle_inherited_hooks_fixture

TEST(LifecycleHooks, InheritedHooksRunAroundConcreteHooks) {
    lifecycle_inherited_hooks_fixture::calls.clear();

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("lifecycle-hooks-inherited");
    ctx.discover<^^lifecycle_inherited_hooks_fixture>().start();

    auto bean = ctx.resolve<lifecycle_inherited_hooks_fixture::Derived>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(lifecycle_inherited_hooks_fixture::calls, (std::vector<int>{1, 2}));

    ctx.stop();

    EXPECT_EQ(lifecycle_inherited_hooks_fixture::calls, (std::vector<int>{1, 2, 3, 4}));
}

namespace lifecycle_multiple_hooks_fixture {

inline std::vector<int> calls;

struct [[=CTORIUM_NAMESPACE::singleton{}]] Service {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void firstInit() {
        calls.push_back(1);
    }

    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void secondInit() {
        calls.push_back(2);
    }

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void firstCleanup() {
        calls.push_back(4);
    }

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void secondCleanup() {
        calls.push_back(3);
    }
};

} // namespace lifecycle_multiple_hooks_fixture

TEST(LifecycleHooks, MultipleHooksOfSameKindAllRunInDefinedOrder) {
    lifecycle_multiple_hooks_fixture::calls.clear();

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("lifecycle-hooks-multiple");
    ctx.discover<^^lifecycle_multiple_hooks_fixture>().start();

    auto bean = ctx.resolve<lifecycle_multiple_hooks_fixture::Service>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(lifecycle_multiple_hooks_fixture::calls, (std::vector<int>{1, 2}));

    ctx.stop();

    EXPECT_EQ(lifecycle_multiple_hooks_fixture::calls, (std::vector<int>{1, 2, 3, 4}));
}

namespace lifecycle_virtual_diamond_hooks_fixture {

inline std::vector<int> calls;

struct VirtualBase {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void baseInit() {
        calls.push_back(1);
    }

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void baseCleanup() {
        calls.push_back(8);
    }
};

struct Left : virtual VirtualBase {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void leftInit() {
        calls.push_back(2);
    }

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void leftCleanup() {
        calls.push_back(7);
    }
};

struct Right : virtual VirtualBase {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void rightInit() {
        calls.push_back(3);
    }

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void rightCleanup() {
        calls.push_back(6);
    }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Diamond : Left, Right {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void diamondInit() {
        calls.push_back(4);
    }

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void diamondCleanup() {
        calls.push_back(5);
    }
};

} // namespace lifecycle_virtual_diamond_hooks_fixture

TEST(LifecycleHooks, VirtualDiamondBaseHookRunsOnce) {
    lifecycle_virtual_diamond_hooks_fixture::calls.clear();

    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("lifecycle-hooks-virtual-diamond");
    ctx.discover<^^lifecycle_virtual_diamond_hooks_fixture>().start();

    auto bean = ctx.resolve<lifecycle_virtual_diamond_hooks_fixture::Diamond>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(lifecycle_virtual_diamond_hooks_fixture::calls, (std::vector<int>{1, 2, 3, 4}));

    ctx.stop();

    EXPECT_EQ(
        lifecycle_virtual_diamond_hooks_fixture::calls,
        (std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
}
