#include <gtest/gtest.h>
#include <memory>
#include <meta>
#include <ctr/Registration.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────
//
// Named-bean annotations (ctr::named{.name=...}) work on GCC 16.1.0 when the name
// is backed by std::define_static_string (the required provenance); a raw string
// literal makes extract throw "reflect_constant failed". The direct acceptance criterion
//   defaultNamed<T>("console") then resolve<T>() selects the "console" candidate
// is exercised below (NamedBeanDiscoversAndStarts / ResolveByNamedSelector /
// DefaultNamedRedirectsUnnamedResolveToNamedCandidate).
//
// The indirect tests on the unnamed `Logger` bean are kept: they validate the
// redirect mechanism when no candidate exists for the defaulted key.

namespace defaults_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Logger {};

} // namespace defaults_fixture

namespace defaults_named_fixture {

// Named singleton bean. The name MUST be promoted with define_static_string:
// a raw literal in the annotation is ill-formed on GCC 16.1.0.
struct [[=CTORIUM_NAMESPACE::singleton{}]]
       [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("console")}]]
       ConsoleSink {};

} // namespace defaults_named_fixture

// Unregistered type — deliberately not discovered in any test context.
struct DefaultsNotRegistered {};

// ─── Pre-start guard ─────────────────────────────────────────────────────────

TEST(Defaults, DefaultNamedBeforeStartRaisesContextStateError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-pre-start");
    ctx.discover<^^defaults_fixture>();
    EXPECT_THROW(
        ctx.defaultNamed<defaults_fixture::Logger>("console"),
        CTORIUM_NAMESPACE::ContextStateError);
    ctx.stop();
}

// ─── Unknown type guard ───────────────────────────────────────────────────────

TEST(Defaults, DefaultNamedUnknownTypeRaisesConfigurationError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-unknown-type");
    ctx.start();
    EXPECT_THROW(
        ctx.defaultNamed<DefaultsNotRegistered>("console"),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

TEST(Defaults, EmptyStringDefaultNamedThrowsConfigurationError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-empty-string-unregistered");
    ctx.start();
    EXPECT_THROW(
        ctx.defaultNamed<DefaultsNotRegistered>(""),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

TEST(Defaults, SetDefaultOnPostStartBindSingletonTypeThrowsConfigurationError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-post-start-set-overflow");
    ctx.start();
    ctx.bindSingleton<DefaultsNotRegistered>(
        std::make_unique<DefaultsNotRegistered>());
    EXPECT_THROW(
        ctx.defaultNamed<DefaultsNotRegistered>("x"),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

TEST(Defaults, ClearDefaultOnPostStartBindSingletonTypeIsNoOp) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-post-start-clear-overflow");
    ctx.start();
    ctx.bindSingleton<DefaultsNotRegistered>(
        std::make_unique<DefaultsNotRegistered>());
    EXPECT_NO_THROW(ctx.defaultNamed<DefaultsNotRegistered>(nullptr));
    EXPECT_THROW(
        ctx.defaultNamed<DefaultsNotRegistered>(""),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

// ─── Default mechanism ────────────────────────────────────────────────────────

TEST(Defaults, SetDefaultRedirectsUnnamedResolveToNamedKey) {
    // Setting the default to an unknown key "console" causes resolve to look for
    // the "console" named key.  Since no such named bean exists, ResolutionError
    // is raised — confirming that the default IS applied.
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-set-default");
    ctx.discover<^^defaults_fixture>().start();

    ctx.defaultNamed<defaults_fixture::Logger>("console");
    EXPECT_THROW(ctx.resolve<defaults_fixture::Logger>(), CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

TEST(Defaults, ClearDefaultWithNullptrRestoresUnnamedResolve) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-clear-nullptr");
    ctx.discover<^^defaults_fixture>().start();

    // Apply a default that breaks unnamed resolve.
    ctx.defaultNamed<defaults_fixture::Logger>("console");
    EXPECT_THROW(ctx.resolve<defaults_fixture::Logger>(), CTORIUM_NAMESPACE::ResolutionError);

    // Clear: unnamed resolve must succeed again.
    ctx.defaultNamed<defaults_fixture::Logger>(nullptr);
    EXPECT_NO_THROW(ctx.resolve<defaults_fixture::Logger>());
    ctx.stop();
}

TEST(Defaults, EmptyStringDefaultNamedThrowsConfigurationErrorForRegisteredType) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-empty-string-registered");
    ctx.discover<^^defaults_fixture>().start();
    EXPECT_THROW(
        ctx.defaultNamed<defaults_fixture::Logger>(""),
        CTORIUM_NAMESPACE::ConfigurationError);
    ctx.stop();
}

TEST(Defaults, DefaultNamedNullptrOnUnregisteredTypeIsNoOp) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-nullptr-no-op");
    ctx.start();
    // Clearing a default for a type that was never registered must not throw.
    EXPECT_NO_THROW(ctx.defaultNamed<DefaultsNotRegistered>(nullptr));
    ctx.stop();
}

// ─── Named beans: previously deferred, now exercised directly ─────────────────
//
// These replace the indirect validation described in the file header. They rely
// on define_static_string-backed names.

TEST(Defaults, NamedBeanDiscoversAndStarts) {
    // The path that was actually blocked: a [[=ctr::named{.name=...}]] bean must
    // discover and start without error (scanAnnotations extracts the name).
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-named-discover");
    EXPECT_NO_THROW(ctx.discover<^^defaults_named_fixture>().start());
    ctx.stop();
}

TEST(Defaults, ResolveByNamedSelectorReturnsNamedBean) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-named-resolve");
    ctx.discover<^^defaults_named_fixture>().start();
    auto bean = ctx.resolve<defaults_named_fixture::ConsoleSink>(CTORIUM_NAMESPACE::named{"console"});
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(Defaults, DefaultNamedRedirectsUnnamedResolveToNamedCandidate) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-named-default");
    ctx.discover<^^defaults_named_fixture>().start();

    // No unnamed candidate exists for ConsoleSink → unnamed resolve fails.
    EXPECT_THROW(ctx.resolve<defaults_named_fixture::ConsoleSink>(), CTORIUM_NAMESPACE::ResolutionError);

    // Default to the existing "console" candidate → unnamed resolve now succeeds.
    ctx.defaultNamed<defaults_named_fixture::ConsoleSink>("console");
    auto byDefault = ctx.resolve<defaults_named_fixture::ConsoleSink>();
    EXPECT_NE(byDefault.operator->(), nullptr);

    // It resolves to the same singleton instance as the explicit named resolve.
    auto byName = ctx.resolve<defaults_named_fixture::ConsoleSink>(CTORIUM_NAMESPACE::named{"console"});
    EXPECT_EQ(byDefault.operator->(), byName.operator->());
    ctx.stop();
}

TEST(Defaults, ScopedDefaultNamedIsNotCurrentlyScopeLocal) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-scope-local-default");
    root.discover<^^defaults_named_fixture>().start();
    auto& scope = root.resolveScope("dn-scope-local-owner");
    auto& sibling = root.resolveScope("dn-scope-local-sibling");
    scope.start();
    sibling.start();

    scope.defaultNamed<defaults_named_fixture::ConsoleSink>(
        std::define_static_string("console"));

    auto scopedDefault = scope.resolve<defaults_named_fixture::ConsoleSink>();
    EXPECT_NE(scopedDefault.operator->(), nullptr);
    EXPECT_THROW(root.resolve<defaults_named_fixture::ConsoleSink>(), CTORIUM_NAMESPACE::ResolutionError);
    EXPECT_THROW(sibling.resolve<defaults_named_fixture::ConsoleSink>(), CTORIUM_NAMESPACE::ResolutionError);

    root.stop();
}

namespace defaults_future_resolution_fixture {

struct Product {
    int value = 0;
};

} // namespace defaults_future_resolution_fixture

namespace defaults_scope_priority_fixture {

struct Product {
    int value = 0;
};

} // namespace defaults_scope_priority_fixture

TEST(Defaults, ScopeLocalDefaultNamedTakesPrecedenceOverRootDefault) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-scope-default-precedence");
    root.bindSingleton<defaults_scope_priority_fixture::Product>(
        std::make_unique<defaults_scope_priority_fixture::Product>(
            defaults_scope_priority_fixture::Product{.value = 1}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("root-default")});
    root.bindSingleton<defaults_scope_priority_fixture::Product>(
        std::make_unique<defaults_scope_priority_fixture::Product>(
            defaults_scope_priority_fixture::Product{.value = 2}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("scope-default")});
    root.start();
    root.defaultNamed<defaults_scope_priority_fixture::Product>(
        std::define_static_string("root-default"));

    auto& scope = root.resolveScope("dn-scope-default-precedence-owner");
    scope.start();
    scope.defaultNamed<defaults_scope_priority_fixture::Product>(
        std::define_static_string("scope-default"));

    auto scopedDefault = scope.resolve<defaults_scope_priority_fixture::Product>();
    auto rootDefault = root.resolve<defaults_scope_priority_fixture::Product>();

    ASSERT_NE(scopedDefault.operator->(), nullptr);
    ASSERT_NE(rootDefault.operator->(), nullptr);
    EXPECT_EQ(scopedDefault->value, 2);
    EXPECT_EQ(rootDefault->value, 1);

    root.stop();
}

TEST(Defaults, ScopeLocalDefaultNamedFallsBackToRootDefaultWhenNotSet) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-scope-default-root-fallback");
    root.bindSingleton<defaults_scope_priority_fixture::Product>(
        std::make_unique<defaults_scope_priority_fixture::Product>(
            defaults_scope_priority_fixture::Product{.value = 10}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("global")});
    root.start();
    root.defaultNamed<defaults_scope_priority_fixture::Product>(
        std::define_static_string("global"));

    auto& scope = root.resolveScope("dn-scope-default-root-fallback-owner");
    scope.start();

    auto scopedDefault = scope.resolve<defaults_scope_priority_fixture::Product>();

    ASSERT_NE(scopedDefault.operator->(), nullptr);
    EXPECT_EQ(scopedDefault->value, 10);

    root.stop();
}

TEST(Defaults, ChangingDefaultAffectsOnlyFutureResolutions) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-future-only");
    ctx.bindSingleton<defaults_future_resolution_fixture::Product>(
        std::make_unique<defaults_future_resolution_fixture::Product>(
            defaults_future_resolution_fixture::Product{.value = 1}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("a")});
    ctx.bindSingleton<defaults_future_resolution_fixture::Product>(
        std::make_unique<defaults_future_resolution_fixture::Product>(
            defaults_future_resolution_fixture::Product{.value = 2}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("b")});
    ctx.start();

    ctx.defaultNamed<defaults_future_resolution_fixture::Product>(
        std::define_static_string("a"));
    auto oldDefault = ctx.resolve<defaults_future_resolution_fixture::Product>();

    ctx.defaultNamed<defaults_future_resolution_fixture::Product>(
        std::define_static_string("b"));
    auto newDefault = ctx.resolve<defaults_future_resolution_fixture::Product>();

    ASSERT_NE(oldDefault.operator->(), nullptr);
    ASSERT_NE(newDefault.operator->(), nullptr);
    EXPECT_EQ(oldDefault->value, 1);
    EXPECT_EQ(newDefault->value, 2);
    EXPECT_NE(oldDefault.operator->(), newDefault.operator->());

    ctx.stop();
}

namespace defaults_snapshot_fixture {

struct Product {
    int value = 0;
};

} // namespace defaults_snapshot_fixture

TEST(Defaults, DefaultNamedSequentialSnapshotObservesOldOrNewValueOnly) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("dn-snapshot-sequential");
    ctx.bindSingleton<defaults_snapshot_fixture::Product>(
        std::make_unique<defaults_snapshot_fixture::Product>(
            defaults_snapshot_fixture::Product{.value = 10}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("old")});
    ctx.bindSingleton<defaults_snapshot_fixture::Product>(
        std::make_unique<defaults_snapshot_fixture::Product>(
            defaults_snapshot_fixture::Product{.value = 20}),
        CTORIUM_NAMESPACE::BindOptions{.name = std::define_static_string("new")});
    ctx.start();

    ctx.defaultNamed<defaults_snapshot_fixture::Product>(
        std::define_static_string("old"));
    auto before = ctx.resolve<defaults_snapshot_fixture::Product>();

    ctx.defaultNamed<defaults_snapshot_fixture::Product>(
        std::define_static_string("new"));
    auto after = ctx.resolve<defaults_snapshot_fixture::Product>();

    ASSERT_NE(before.operator->(), nullptr);
    ASSERT_NE(after.operator->(), nullptr);
    EXPECT_TRUE(before->value == 10 || before->value == 20);
    EXPECT_TRUE(after->value == 10 || after->value == 20);
    EXPECT_EQ(before->value, 10);
    EXPECT_EQ(after->value, 20);

    ctx.stop();
}
