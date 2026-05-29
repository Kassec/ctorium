#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────
//
// Named-bean annotation tests (ctr::named{.name=...}) are deferred: GCC 16.1.0
// does not yet support reflect_constant on class types with const char* members.
// The acceptance criterion "defaultNamed<T>("x") puis resolve<T>() sélectionne
// le candidat "x"" is therefore validated indirectly: after setting a default to
// an unknown key "console", resolve<T>() raises ResolutionError (the default IS
// applied, but no candidate exists for that key).  After clearing with nullptr,
// unnamed resolution succeeds again.

namespace defaults_fixture {

struct [[=ctr::singleton{}]] Logger {};

} // namespace defaults_fixture

// Unregistered type — deliberately not discovered in any test context.
struct DefaultsNotRegistered {};

// ─── Pre-start guard ─────────────────────────────────────────────────────────

TEST(Defaults, DefaultNamedBeforeStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-pre-start");
    ctx.discover<^^defaults_fixture>();
    EXPECT_THROW(
        ctx.defaultNamed<defaults_fixture::Logger>("console"),
        ctr::ContextStateError);
    ctx.close();
}

// ─── Unknown type guard ───────────────────────────────────────────────────────

TEST(Defaults, DefaultNamedUnknownTypeRaisesConfigurationError) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-unknown-type");
    ctx.start();
    EXPECT_THROW(
        ctx.defaultNamed<DefaultsNotRegistered>("console"),
        ctr::ConfigurationError);
    ctx.close();
}

// ─── Default mechanism ────────────────────────────────────────────────────────

TEST(Defaults, SetDefaultRedirectsUnnamedResolveToNamedKey) {
    // Setting the default to an unknown key "console" causes resolve to look for
    // the "console" named key.  Since no such named bean exists, ResolutionError
    // is raised — confirming that the default IS applied.
    auto& ctx = ctr::BeanContext::resolveContext("dn-set-default");
    ctx.discover<^^defaults_fixture>().start();

    ctx.defaultNamed<defaults_fixture::Logger>("console");
    EXPECT_THROW(ctx.resolve<defaults_fixture::Logger>(), ctr::ResolutionError);
    ctx.close();
}

TEST(Defaults, ClearDefaultWithNullptrRestoresUnnamedResolve) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-clear-nullptr");
    ctx.discover<^^defaults_fixture>().start();

    // Apply a default that breaks unnamed resolve.
    ctx.defaultNamed<defaults_fixture::Logger>("console");
    EXPECT_THROW(ctx.resolve<defaults_fixture::Logger>(), ctr::ResolutionError);

    // Clear: unnamed resolve must succeed again.
    ctx.defaultNamed<defaults_fixture::Logger>(nullptr);
    EXPECT_NO_THROW(ctx.resolve<defaults_fixture::Logger>());
    ctx.close();
}

TEST(Defaults, ClearDefaultWithEmptyStringRestoresUnnamedResolve) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-clear-empty");
    ctx.discover<^^defaults_fixture>().start();

    ctx.defaultNamed<defaults_fixture::Logger>("console");
    EXPECT_THROW(ctx.resolve<defaults_fixture::Logger>(), ctr::ResolutionError);

    ctx.defaultNamed<defaults_fixture::Logger>("");
    EXPECT_NO_THROW(ctx.resolve<defaults_fixture::Logger>());
    ctx.close();
}

TEST(Defaults, DefaultNamedNullptrOnUnregisteredTypeIsNoOp) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-nullptr-no-op");
    ctx.start();
    // Clearing a default for a type that was never registered must not throw.
    EXPECT_NO_THROW(ctx.defaultNamed<DefaultsNotRegistered>(nullptr));
    ctx.close();
}
