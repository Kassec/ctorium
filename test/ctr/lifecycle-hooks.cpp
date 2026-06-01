#include <meta>
#include <vector>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace lifecycle_inherited_hooks_fixture {

inline std::vector<int> calls;

struct Base {
    [[=ctr::postConstruct{}]]
    void baseInit() {
        calls.push_back(1);
    }

    [[=ctr::preDestroy{}]]
    void baseCleanup() {
        calls.push_back(4);
    }
};

struct [[=ctr::singleton{}]] Derived : Base {
    [[=ctr::postConstruct{}]]
    void derivedInit() {
        calls.push_back(2);
    }

    [[=ctr::preDestroy{}]]
    void derivedCleanup() {
        calls.push_back(3);
    }
};

} // namespace lifecycle_inherited_hooks_fixture

TEST(LifecycleHooks, InheritedHooksRunAroundConcreteHooks) {
    lifecycle_inherited_hooks_fixture::calls.clear();

    auto& ctx = ctr::BeanContext::resolveContext("lifecycle-hooks-inherited");
    ctx.discover<^^lifecycle_inherited_hooks_fixture>().start();

    auto bean = ctx.resolve<lifecycle_inherited_hooks_fixture::Derived>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(lifecycle_inherited_hooks_fixture::calls, (std::vector<int>{1, 2}));

    ctx.stop();

    EXPECT_EQ(lifecycle_inherited_hooks_fixture::calls, (std::vector<int>{1, 2, 3, 4}));
}

namespace lifecycle_multiple_hooks_fixture {

inline std::vector<int> calls;

struct [[=ctr::singleton{}]] Service {
    [[=ctr::postConstruct{}]]
    void firstInit() {
        calls.push_back(1);
    }

    [[=ctr::postConstruct{}]]
    void secondInit() {
        calls.push_back(2);
    }

    [[=ctr::preDestroy{}]]
    void firstCleanup() {
        calls.push_back(4);
    }

    [[=ctr::preDestroy{}]]
    void secondCleanup() {
        calls.push_back(3);
    }
};

} // namespace lifecycle_multiple_hooks_fixture

TEST(LifecycleHooks, MultipleHooksOfSameKindAllRunInDefinedOrder) {
    lifecycle_multiple_hooks_fixture::calls.clear();

    auto& ctx = ctr::BeanContext::resolveContext("lifecycle-hooks-multiple");
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
    [[=ctr::postConstruct{}]]
    void baseInit() {
        calls.push_back(1);
    }

    [[=ctr::preDestroy{}]]
    void baseCleanup() {
        calls.push_back(8);
    }
};

struct Left : virtual VirtualBase {
    [[=ctr::postConstruct{}]]
    void leftInit() {
        calls.push_back(2);
    }

    [[=ctr::preDestroy{}]]
    void leftCleanup() {
        calls.push_back(7);
    }
};

struct Right : virtual VirtualBase {
    [[=ctr::postConstruct{}]]
    void rightInit() {
        calls.push_back(3);
    }

    [[=ctr::preDestroy{}]]
    void rightCleanup() {
        calls.push_back(6);
    }
};

struct [[=ctr::singleton{}]] Diamond : Left, Right {
    [[=ctr::postConstruct{}]]
    void diamondInit() {
        calls.push_back(4);
    }

    [[=ctr::preDestroy{}]]
    void diamondCleanup() {
        calls.push_back(5);
    }
};

} // namespace lifecycle_virtual_diamond_hooks_fixture

TEST(LifecycleHooks, VirtualDiamondBaseHookRunsOnce) {
    lifecycle_virtual_diamond_hooks_fixture::calls.clear();

    auto& ctx = ctr::BeanContext::resolveContext("lifecycle-hooks-virtual-diamond");
    ctx.discover<^^lifecycle_virtual_diamond_hooks_fixture>().start();

    auto bean = ctx.resolve<lifecycle_virtual_diamond_hooks_fixture::Diamond>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(lifecycle_virtual_diamond_hooks_fixture::calls, (std::vector<int>{1, 2, 3, 4}));

    ctx.stop();

    EXPECT_EQ(
        lifecycle_virtual_diamond_hooks_fixture::calls,
        (std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
}
