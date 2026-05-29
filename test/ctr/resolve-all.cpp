#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────

namespace resolve_all_fixture {

struct [[=ctr::singleton{}]] Widget {};
struct [[=ctr::singleton{}]] Gadget {};
struct [[=ctr::prototype{}]] Disposable {};

} // namespace resolve_all_fixture

// Unregistered type — deliberately not discovered in any test context.
struct ResolveAllNotRegistered {};

// ─── Basic resolution ─────────────────────────────────────────────────────────

TEST(ResolveAll, ReturnsOneCandidateForRegisteredType) {
    auto& ctx = ctr::BeanContext::resolveContext("ra-one-candidate");
    ctx.discover<^^resolve_all_fixture>().start();

    auto widgets = ctx.resolveAll<resolve_all_fixture::Widget>();
    EXPECT_EQ(widgets.size(), 1u);
    EXPECT_NE(widgets[0].operator->(), nullptr);
    ctx.close();
}

TEST(ResolveAll, ReturnedHandleIsSameInstanceAsDirectResolve) {
    auto& ctx = ctr::BeanContext::resolveContext("ra-same-instance");
    ctx.discover<^^resolve_all_fixture>().start();

    auto all = ctx.resolveAll<resolve_all_fixture::Widget>();
    ASSERT_EQ(all.size(), 1u);

    auto direct = ctx.resolve<resolve_all_fixture::Widget>();
    EXPECT_EQ(all[0], direct);
    ctx.close();
}

TEST(ResolveAll, PrototypeReturnsNewInstanceEachTime) {
    auto& ctx = ctr::BeanContext::resolveContext("ra-prototype");
    ctx.discover<^^resolve_all_fixture>().start();

    auto all1 = ctx.resolveAll<resolve_all_fixture::Disposable>();
    auto all2 = ctx.resolveAll<resolve_all_fixture::Disposable>();
    ASSERT_EQ(all1.size(), 1u);
    ASSERT_EQ(all2.size(), 1u);
    EXPECT_NE(all1[0], all2[0]); // distinct prototype instances
    ctx.close();
}

// ─── Empty cases ─────────────────────────────────────────────────────────────

TEST(ResolveAll, ReturnsEmptyVectorForUnregisteredType) {
    auto& ctx = ctr::BeanContext::resolveContext("ra-unregistered");
    ctx.discover<^^resolve_all_fixture>().start();

    auto result = ctx.resolveAll<ResolveAllNotRegistered>();
    EXPECT_TRUE(result.empty());
    ctx.close();
}

TEST(ResolveAll, NamedReturnsEmptyForUnknownKey) {
    auto& ctx = ctr::BeanContext::resolveContext("ra-named-unknown");
    ctx.discover<^^resolve_all_fixture>().start();

    auto result = ctx.resolveAll<resolve_all_fixture::Widget>(ctr::named{"unknown"});
    EXPECT_TRUE(result.empty());
    ctx.close();
}

// ─── State guard ──────────────────────────────────────────────────────────────

TEST(ResolveAll, ResolveAllBeforeStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("ra-pre-start");
    ctx.discover<^^resolve_all_fixture>();
    EXPECT_THROW(ctx.resolveAll<resolve_all_fixture::Widget>(), ctr::ContextStateError);
    ctx.close();
}
