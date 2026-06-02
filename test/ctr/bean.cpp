#include <utility>
#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────

namespace bean_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Svc {};
struct [[=CTORIUM_NAMESPACE::prototype{}]] Proto {};

} // namespace bean_fixture

// ─── Bean<T> handle semantics — singleton (Form 1, kInvalidSlotId) ───────────
//
// All tests in this file exercise the public handle contract
// independently of bean lifetime semantics, which are covered in singleton.cpp
// and prototype.cpp.
//
// Singleton handles carry kInvalidSlotId: copy and destroy are cheap struct copies
// with no atomic refcount operations.

TEST(BeanHandle, MoveConstructionTransfersPointer) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bh-move-ctor");
    ctx.discover<^^bean_fixture>().start();
    auto b1   = ctx.resolve<bean_fixture::Svc>();
    auto* ptr = b1.operator->();
    auto b2   = std::move(b1);
    EXPECT_EQ(b2.operator->(), ptr);
    ctx.stop();
}

TEST(BeanHandle, MoveAssignmentTransfersPointer) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bh-move-assign");
    ctx.discover<^^bean_fixture>().start();
    auto b1   = ctx.resolve<bean_fixture::Svc>();
    auto* ptr = b1.operator->();
    CTORIUM_NAMESPACE::Bean<bean_fixture::Svc> b2;
    b2 = std::move(b1);
    EXPECT_EQ(b2.operator->(), ptr);
    ctx.stop();
}

TEST(BeanHandle, OperatorStarEquivalentToArrow) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bh-star");
    ctx.discover<^^bean_fixture>().start();
    auto b = ctx.resolve<bean_fixture::Svc>();
    EXPECT_EQ(&(*b), b.operator->());
    ctx.stop();
}

TEST(BeanHandle, ValueEquivalentToArrow) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bh-value");
    ctx.discover<^^bean_fixture>().start();
    auto b = ctx.resolve<bean_fixture::Svc>();
    EXPECT_EQ(&b.value(), b.operator->());
    ctx.stop();
}

TEST(BeanHandle, CopyAssignmentPreservesPointerAndEquality) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bh-copy-assign");
    ctx.discover<^^bean_fixture>().start();
    auto b1 = ctx.resolve<bean_fixture::Svc>();
    CTORIUM_NAMESPACE::Bean<bean_fixture::Svc> b2;
    b2 = b1;
    EXPECT_EQ(b1.operator->(), b2.operator->());
    EXPECT_EQ(b1, b2);
    ctx.stop();
}

TEST(BeanHandle, DefaultConstructedHandleHasNullPointer) {
    CTORIUM_NAMESPACE::Bean<bean_fixture::Svc> b;
    EXPECT_EQ(b.operator->(), nullptr);
}

TEST(BeanHandle, MoveConstructionLeavesSourceNull) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bh-move-null-ctor");
    ctx.discover<^^bean_fixture>().start();
    auto b1 = ctx.resolve<bean_fixture::Svc>();
    auto b2 = std::move(b1);
    EXPECT_EQ(b1.operator->(), nullptr);
    ctx.stop();
}

TEST(BeanHandle, MoveAssignmentLeavesSourceNull) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bh-move-null-assign");
    ctx.discover<^^bean_fixture>().start();
    auto b1 = ctx.resolve<bean_fixture::Svc>();
    CTORIUM_NAMESPACE::Bean<bean_fixture::Svc> b2;
    b2 = std::move(b1);
    EXPECT_EQ(b1.operator->(), nullptr);
    ctx.stop();
}

TEST(BeanHandle, InequalityOperatorReturnsTrueForDistinctBeans) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bh-neq");
    ctx.discover<^^bean_fixture>().start();
    auto b1 = ctx.resolve<bean_fixture::Proto>();
    auto b2 = ctx.resolve<bean_fixture::Proto>();
    EXPECT_TRUE(b1 != b2);
    ctx.stop();
}