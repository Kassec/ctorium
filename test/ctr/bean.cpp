#include <utility>
#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────

namespace bean_fixture {

struct [[=ctr::singleton{}]] Svc {};

} // namespace bean_fixture

// ─── Bean<T> handle semantics — singleton (Form 1, kInvalidSlotId) ───────────
//
// All tests in this file exercise the public handle contract (specs-api §7.1)
// independently of bean lifetime semantics, which are covered in singleton.cpp
// and prototype.cpp.
//
// Singleton handles carry kInvalidSlotId: copy and destroy are cheap struct copies
// with no atomic refcount operations.

TEST(BeanHandle, MoveConstructionTransfersPointer) {
    auto& ctx = ctr::BeanContext::resolveContext("bh-move-ctor");
    ctx.discover<^^bean_fixture>().start();
    auto b1   = ctx.resolve<bean_fixture::Svc>();
    auto* ptr = b1.operator->();
    auto b2   = std::move(b1);
    EXPECT_EQ(b2.operator->(), ptr);
    ctx.close();
}

TEST(BeanHandle, MoveAssignmentTransfersPointer) {
    auto& ctx = ctr::BeanContext::resolveContext("bh-move-assign");
    ctx.discover<^^bean_fixture>().start();
    auto b1   = ctx.resolve<bean_fixture::Svc>();
    auto* ptr = b1.operator->();
    ctr::Bean<bean_fixture::Svc> b2;
    b2 = std::move(b1);
    EXPECT_EQ(b2.operator->(), ptr);
    ctx.close();
}

TEST(BeanHandle, OperatorStarEquivalentToArrow) {
    auto& ctx = ctr::BeanContext::resolveContext("bh-star");
    ctx.discover<^^bean_fixture>().start();
    auto b = ctx.resolve<bean_fixture::Svc>();
    EXPECT_EQ(&(*b), b.operator->());
    ctx.close();
}

TEST(BeanHandle, ValueEquivalentToArrow) {
    auto& ctx = ctr::BeanContext::resolveContext("bh-value");
    ctx.discover<^^bean_fixture>().start();
    auto b = ctx.resolve<bean_fixture::Svc>();
    EXPECT_EQ(&b.value(), b.operator->());
    ctx.close();
}

TEST(BeanHandle, CopyAssignmentPreservesPointerAndEquality) {
    auto& ctx = ctr::BeanContext::resolveContext("bh-copy-assign");
    ctx.discover<^^bean_fixture>().start();
    auto b1 = ctx.resolve<bean_fixture::Svc>();
    ctr::Bean<bean_fixture::Svc> b2;
    b2 = b1;
    EXPECT_EQ(b1.operator->(), b2.operator->());
    EXPECT_EQ(b1, b2);
    ctx.close();
}