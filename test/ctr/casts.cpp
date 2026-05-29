#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────
//
// All fixture types live in a single namespace to share the global typeIdFor<T>
// cache (static std::atomic per type) across test contexts.

namespace casts_fixture {

struct Base {};

struct [[=ctr::singleton{}]] Impl : Base {};

struct [[=ctr::prototype{}]] ProtoType {};

} // namespace casts_fixture

// ─── Bean<T>::context() ───────────────────────────────────────────────────────

TEST(BeanCast, ContextReturnsSingletonOwner) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-context-singleton");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_EQ(&b.context(), &ctx);
    ctx.close();
}

TEST(BeanCast, ContextReturnsPrototypeOwner) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-context-prototype");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::ProtoType>();
    EXPECT_EQ(&b.context(), &ctx);
    ctx.close();
}

// ─── Bean<T>::exact<U>() ──────────────────────────────────────────────────────

TEST(BeanCast, ExactTrueForOwnType) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-exact-true");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_TRUE(b.exact<casts_fixture::Impl>());
    ctx.close();
}

TEST(BeanCast, ExactFalseForOtherType) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-exact-false");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_FALSE(b.exact<casts_fixture::Base>());
    ctx.close();
}

// ─── Bean<T>::compatible<U>() ─────────────────────────────────────────────────

TEST(BeanCast, CompatibleTrueForExposedType) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-compat-true");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_TRUE(b.compatible<casts_fixture::Impl>());
    ctx.close();
}

TEST(BeanCast, CompatibleFalseForOtherType) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-compat-false");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_FALSE(b.compatible<casts_fixture::Base>());
    ctx.close();
}

// ─── Bean<T>::cast<U>() ───────────────────────────────────────────────────────

TEST(BeanCast, CastToCompatibleSucceeds) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-cast-ok");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_NO_THROW({
        auto b2 = b.cast<casts_fixture::Impl>();
        EXPECT_EQ(b2.operator->(), b.operator->());
    });
    ctx.close();
}

TEST(BeanCast, CastToIncompatibleThrowsResolutionError) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-cast-throw");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_THROW({ (void)b.cast<casts_fixture::Base>(); }, ctr::ResolutionError);
    ctx.close();
}

// ─── Bean<T>::tryCast<U>() ────────────────────────────────────────────────────

TEST(BeanCast, TryCastToCompatibleReturnsValue) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-trycast-ok");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    auto opt = b.tryCast<casts_fixture::Impl>();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->operator->(), b.operator->());
    ctx.close();
}

TEST(BeanCast, TryCastToIncompatibleReturnsNullopt) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-trycast-null");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_FALSE(b.tryCast<casts_fixture::Base>().has_value());
    ctx.close();
}

// ─── Prototype cast retains correctly ─────────────────────────────────────────

TEST(BeanCast, PrototypeCastRetainsRefcount) {
    auto& ctx = ctr::BeanContext::resolveContext("bc-proto-cast-retain");
    ctx.discover<^^casts_fixture>().start();

    int destroyCount = 0;
    ctx.on(ctr::onDestroyed, [&](const ctr::AnyBean&) { ++destroyCount; });

    {
        auto b1 = ctx.resolve<casts_fixture::ProtoType>();
        {
            auto b2 = b1.cast<casts_fixture::ProtoType>(); // retain: refcount = 2
            EXPECT_EQ(destroyCount, 0);
        } // b2 destroyed: refcount = 1
        EXPECT_EQ(destroyCount, 0);
    } // b1 destroyed: refcount = 0 → destruction fires
    EXPECT_EQ(destroyCount, 1);

    ctx.close();
}

// ─── AnyBean::context() ───────────────────────────────────────────────────────

TEST(AnyBeanCast, ContextReturnsOwner) {
    auto& ctx = ctr::BeanContext::resolveContext("ab-context");
    ctx.discover<^^casts_fixture>().start();

    ctr::AnyBean captured;
    ctx.on(ctr::onCreated, [&](const ctr::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_EQ(&captured.context(), &ctx);
    ctx.close();
}

// ─── AnyBean::exact<U>() / compatible<U>() ───────────────────────────────────

TEST(AnyBeanCast, ExactTrueForConcreteType) {
    auto& ctx = ctr::BeanContext::resolveContext("ab-exact-true");
    ctx.discover<^^casts_fixture>().start();

    ctr::AnyBean captured;
    ctx.on(ctr::onCreated, [&](const ctr::AnyBean& b) {
        if (b.exact<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_TRUE(captured.exact<casts_fixture::Impl>());
    ctx.close();
}

TEST(AnyBeanCast, CompatibleTrueForExposedType) {
    auto& ctx = ctr::BeanContext::resolveContext("ab-compat-true");
    ctx.discover<^^casts_fixture>().start();

    ctr::AnyBean captured;
    ctx.on(ctr::onCreated, [&](const ctr::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_TRUE(captured.compatible<casts_fixture::Impl>());
    ctx.close();
}

// ─── AnyBean::cast<U>() / tryCast<U>() ───────────────────────────────────────

TEST(AnyBeanCast, CastToCompatibleSucceeds) {
    auto& ctx = ctr::BeanContext::resolveContext("ab-cast-ok");
    ctx.discover<^^casts_fixture>().start();

    ctr::AnyBean captured;
    ctx.on(ctr::onCreated, [&](const ctr::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_NO_THROW({ (void)captured.cast<casts_fixture::Impl>(); });
    ctx.close();
}

TEST(AnyBeanCast, CastToIncompatibleThrows) {
    auto& ctx = ctr::BeanContext::resolveContext("ab-cast-throw");
    ctx.discover<^^casts_fixture>().start();

    ctr::AnyBean captured;
    ctx.on(ctr::onCreated, [&](const ctr::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_THROW({ (void)captured.cast<casts_fixture::Base>(); }, ctr::ResolutionError);
    ctx.close();
}

TEST(AnyBeanCast, TryCastToIncompatibleReturnsNullopt) {
    auto& ctx = ctr::BeanContext::resolveContext("ab-trycast-null");
    ctx.discover<^^casts_fixture>().start();

    ctr::AnyBean captured;
    ctx.on(ctr::onCreated, [&](const ctr::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_FALSE(captured.tryCast<casts_fixture::Base>().has_value());
    ctx.close();
}

// ─── AnyBean prototype refcount: copy + destroy fires one destruction ─────────

TEST(AnyBeanCast, PrototypeCopyThenDestroyTriggersOneDestruction) {
    auto& ctx = ctr::BeanContext::resolveContext("ab-proto-refcount");
    ctx.discover<^^casts_fixture>().start();

    int destroyCount = 0;
    ctx.on(ctr::onDestroyed, [&](const ctr::AnyBean&) { ++destroyCount; });

    {
        ctr::AnyBean captured;
        ctx.on(ctr::onCreated, [&](const ctr::AnyBean& b) {
            if (b.compatible<casts_fixture::ProtoType>()) captured = b; // retain
        });

        {
            auto bean = ctx.resolve<casts_fixture::ProtoType>(); // refcount = 2
        } // bean destroyed: refcount = 1
        EXPECT_EQ(destroyCount, 0);
    } // captured destroyed: refcount = 0 → destruction fires
    EXPECT_EQ(destroyCount, 1);

    ctx.close();
}
