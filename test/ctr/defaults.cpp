#include <gtest/gtest.h>
#include <meta>
#include <ctr/Ctorium.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────
//
// Named-bean annotations (ctr::named{.name=...}) work on GCC 16.1.0 when the name
// is backed by std::define_static_string (the required provenance: a raw string
// literal makes extract throw "reflect_constant failed" — see docs/specs-gcc.md
// §3 / §4.3). The direct acceptance criterion
//   defaultNamed<T>("console") then resolve<T>() selects the "console" candidate
// is exercised below (NamedBeanDiscoversAndStarts / ResolveByNamedSelector /
// DefaultNamedRedirectsUnnamedResolveToNamedCandidate).
//
// The indirect tests on the unnamed `Logger` bean are kept: they validate the
// redirect mechanism when no candidate exists for the defaulted key.

namespace defaults_fixture {

struct [[=ctr::singleton{}]] Logger {};

} // namespace defaults_fixture

namespace defaults_named_fixture {

// Named singleton bean. The name MUST be promoted with define_static_string:
// a raw literal in the annotation is ill-formed on GCC 16.1.0 (docs/specs-gcc.md).
struct [[=ctr::singleton{}]]
       [[=ctr::named{.name = std::define_static_string("console")}]]
       ConsoleSink {};

} // namespace defaults_named_fixture

// Unregistered type — deliberately not discovered in any test context.
struct DefaultsNotRegistered {};

// ─── Pre-start guard ─────────────────────────────────────────────────────────

TEST(Defaults, DefaultNamedBeforeStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-pre-start");
    ctx.discover<^^defaults_fixture>();
    EXPECT_THROW(
        ctx.defaultNamed<defaults_fixture::Logger>("console"),
        ctr::ContextStateError);
    ctx.stop();
}

// ─── Unknown type guard ───────────────────────────────────────────────────────

TEST(Defaults, DefaultNamedUnknownTypeRaisesConfigurationError) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-unknown-type");
    ctx.start();
    EXPECT_THROW(
        ctx.defaultNamed<DefaultsNotRegistered>("console"),
        ctr::ConfigurationError);
    ctx.stop();
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
    ctx.stop();
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
    ctx.stop();
}

TEST(Defaults, ClearDefaultWithEmptyStringRestoresUnnamedResolve) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-clear-empty");
    ctx.discover<^^defaults_fixture>().start();

    ctx.defaultNamed<defaults_fixture::Logger>("console");
    EXPECT_THROW(ctx.resolve<defaults_fixture::Logger>(), ctr::ResolutionError);

    ctx.defaultNamed<defaults_fixture::Logger>("");
    EXPECT_NO_THROW(ctx.resolve<defaults_fixture::Logger>());
    ctx.stop();
}

TEST(Defaults, DefaultNamedNullptrOnUnregisteredTypeIsNoOp) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-nullptr-no-op");
    ctx.start();
    // Clearing a default for a type that was never registered must not throw.
    EXPECT_NO_THROW(ctx.defaultNamed<DefaultsNotRegistered>(nullptr));
    ctx.stop();
}

// ─── Named beans: previously deferred, now exercised directly ─────────────────
//
// These replace the indirect validation described in the file header. They rely
// on define_static_string-backed names (docs/specs-gcc.md §4.3).

TEST(Defaults, NamedBeanDiscoversAndStarts) {
    // The path that was actually blocked: a [[=ctr::named{.name=...}]] bean must
    // discover and start without error (scanAnnotations extracts the name).
    auto& ctx = ctr::BeanContext::resolveContext("dn-named-discover");
    EXPECT_NO_THROW(ctx.discover<^^defaults_named_fixture>().start());
    ctx.stop();
}

TEST(Defaults, ResolveByNamedSelectorReturnsNamedBean) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-named-resolve");
    ctx.discover<^^defaults_named_fixture>().start();
    auto bean = ctx.resolve<defaults_named_fixture::ConsoleSink>(ctr::named{"console"});
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(Defaults, DefaultNamedRedirectsUnnamedResolveToNamedCandidate) {
    auto& ctx = ctr::BeanContext::resolveContext("dn-named-default");
    ctx.discover<^^defaults_named_fixture>().start();

    // No unnamed candidate exists for ConsoleSink → unnamed resolve fails.
    EXPECT_THROW(ctx.resolve<defaults_named_fixture::ConsoleSink>(), ctr::ResolutionError);

    // Default to the existing "console" candidate → unnamed resolve now succeeds.
    ctx.defaultNamed<defaults_named_fixture::ConsoleSink>("console");
    auto byDefault = ctx.resolve<defaults_named_fixture::ConsoleSink>();
    EXPECT_NE(byDefault.operator->(), nullptr);

    // It resolves to the same singleton instance as the explicit named resolve.
    auto byName = ctx.resolve<defaults_named_fixture::ConsoleSink>(ctr::named{"console"});
    EXPECT_EQ(byDefault.operator->(), byName.operator->());
    ctx.stop();
}
