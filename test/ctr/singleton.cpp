#include <gtest/gtest.h>
#include <meta>
#include <ctr/Ctorium.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────
//
// Each test file uses an isolated namespace so that merging into a single
// binary does not expose cross-TU types through the same namespace name.
// The per-type typeIdFor<T> cache (a static std::atomic<TypeId> inside
// Registry::typeIdFor) is process-global; keeping discovery roots isolated
// per file prevents TypeId aliasing between independent registries.
//
// Priority arbitration (ctr::singleton::priority) is a registry feature that
// requires two candidates sharing the same (TypeId, NameId) slot.  With
// annotated types the exposedType is always the concrete type, so two distinct
// structs can never share a slot.  Priority tests will be added when factory
// or bindSingleton support lands.

namespace singleton_fixture {

struct [[=ctr::singleton{}]] Service {};

struct [[=ctr::singleton{}]] Dependency {};

// HookedService: singleton with a postConstruct hook.
// callCount is incremented once on first materialization; the hook is not
// re-fired on subsequent resolve() calls (same instance returned).
// No user destructor, no inline globals: the reflect_constant failure
// documented earlier was specific to the "inline global + user destructor"
// pattern and does not apply here.
struct [[=ctr::singleton{}]] HookedService {
    int callCount = 0;
    [[=ctr::postConstruct{}]] void init() { callCount++; }
};

// Consumer: depends on Dependency via constructor injection.
// The constructor has a single Bean<Dependency> parameter, which scanMembers()
// recognises as a compatible injectable constructor (isBeanType check).
// The two-phase lock pattern in Registry::resolve<T> releases the write lock
// before the construct thunk executes, so the nested resolve<Dependency>()
// inside constructThunkInjected proceeds without deadlocking.
struct [[=ctr::singleton{}]] Consumer {
    ctr::Bean<Dependency> dep;
    explicit Consumer(ctr::Bean<Dependency> d) : dep(std::move(d)) {}
};

} // namespace singleton_fixture

// Named-bean fixture.  Name MUST use define_static_string — raw literals are
// ill-formed on GCC 16.1.0 (docs/gcc-P2996R13.md §3/§4.3).
namespace singleton_named_fixture {

struct [[=ctr::singleton{}]]
       [[=ctr::named{.name = std::define_static_string("console")}]]
       ConsoleSink {};

} // namespace singleton_named_fixture

// Unregistered type — deliberately not discovered in any test context.
struct NotRegistered {};

// ─── Lifecycle state tests ────────────────────────────────────────────────────

TEST(Singleton, ResolveBeforeStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("st-pre-start");
    ctx.discover<^^singleton_fixture>();
    EXPECT_THROW(ctx.resolve<singleton_fixture::Service>(), ctr::ContextStateError);
    ctx.stop();
}

TEST(Singleton, DiscoverAfterStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("st-post-start-discover");
    ctx.discover<^^singleton_fixture>().start();
    EXPECT_THROW(ctx.discover<^^singleton_fixture>(), ctr::ContextStateError);
    ctx.stop();
}

TEST(Singleton, StartIsIdempotent) {
    auto& ctx = ctr::BeanContext::resolveContext("st-start-idempotent");
    ctx.discover<^^singleton_fixture>().start();
    EXPECT_NO_THROW(ctx.start());
    ctx.stop();
}

// ─── Resolution and identity tests ───────────────────────────────────────────

TEST(Singleton, ResolveReturnsValidBean) {
    auto& ctx = ctr::BeanContext::resolveContext("st-basic-resolve");
    ctx.discover<^^singleton_fixture>().start();
    auto bean = ctx.resolve<singleton_fixture::Service>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(Singleton, SameObjectReturnedOnRepeatedResolve) {
    auto& ctx = ctr::BeanContext::resolveContext("st-identity");
    ctx.discover<^^singleton_fixture>().start();
    auto b1 = ctx.resolve<singleton_fixture::Service>();
    auto b2 = ctx.resolve<singleton_fixture::Service>();
    EXPECT_EQ(b1, b2);
    ctx.stop();
}

TEST(Singleton, SameContextKeyReturnsSameContextInstance) {
    auto& ctx1 = ctr::BeanContext::resolveContext("st-same-key");
    auto& ctx2 = ctr::BeanContext::resolveContext("st-same-key");
    EXPECT_EQ(&ctx1, &ctx2);
    ctx1.stop();
}

TEST(Singleton, DifferentContextKeysYieldIndependentInstances) {
    auto& ctx1 = ctr::BeanContext::resolveContext("st-key-a");
    auto& ctx2 = ctr::BeanContext::resolveContext("st-key-b");
    ctx1.discover<^^singleton_fixture>().start();
    ctx2.discover<^^singleton_fixture>().start();
    auto b1 = ctx1.resolve<singleton_fixture::Service>();
    auto b2 = ctx2.resolve<singleton_fixture::Service>();
    EXPECT_NE(b1, b2);
    ctx1.stop();
    ctx2.stop();
}

// ─── Named key tests ─────────────────────────────────────────────────────────

TEST(Singleton, NamedSingletonResolvesSuccessfullyByName) {
    auto& ctx = ctr::BeanContext::resolveContext("sn-named-resolve");
    ctx.discover<^^singleton_named_fixture>().start();
    EXPECT_NO_THROW(ctx.resolve<singleton_named_fixture::ConsoleSink>(ctr::named{"console"}));
    auto bean = ctx.resolve<singleton_named_fixture::ConsoleSink>(ctr::named{"console"});
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.stop();
}

TEST(Singleton, UnnamedResolveOfNamedOnlyBeanRaisesResolutionError) {
    auto& ctx = ctr::BeanContext::resolveContext("sn-named-unnamed-fail");
    ctx.discover<^^singleton_named_fixture>().start();
    EXPECT_THROW(ctx.resolve<singleton_named_fixture::ConsoleSink>(), ctr::ResolutionError);
    ctx.stop();
}

TEST(Singleton, NamedSingletonSameInstanceOnRepeatedResolve) {
    auto& ctx = ctr::BeanContext::resolveContext("sn-named-identity");
    ctx.discover<^^singleton_named_fixture>().start();
    auto b1 = ctx.resolve<singleton_named_fixture::ConsoleSink>(ctr::named{"console"});
    auto b2 = ctx.resolve<singleton_named_fixture::ConsoleSink>(ctr::named{"console"});
    EXPECT_EQ(b1.operator->(), b2.operator->());
    ctx.stop();
}

TEST(Singleton, NamedResolveWithUnknownKeyRaisesResolutionError) {
    auto& ctx = ctr::BeanContext::resolveContext("st-unknown-key");
    ctx.discover<^^singleton_fixture>().start();
    // "nonexistent" was never interned as a NameId → ResolutionError.
    EXPECT_THROW(
        ctx.resolve<singleton_fixture::Service>(ctr::named{"nonexistent"}),
        ctr::ResolutionError);
    ctx.stop();
}

TEST(Singleton, ResolveOfUnregisteredTypeRaisesResolutionError) {
    auto& ctx = ctr::BeanContext::resolveContext("st-unknown-type");
    ctx.discover<^^singleton_fixture>().start();
    EXPECT_THROW(ctx.resolve<NotRegistered>(), ctr::ResolutionError);
    ctx.stop();
}

// ─── Hook tests ───────────────────────────────────────────────────────────────

TEST(Singleton, PostConstructHookFiresOnceOnFirstMaterialization) {
    auto& ctx = ctr::BeanContext::resolveContext("st-post-construct");
    ctx.discover<^^singleton_fixture>().start();
    auto b1 = ctx.resolve<singleton_fixture::HookedService>();
    EXPECT_EQ(b1->callCount, 1);
    auto b2 = ctx.resolve<singleton_fixture::HookedService>();
    EXPECT_EQ(b2->callCount, 1); // hook not re-fired; same instance
    EXPECT_EQ(b1, b2);
    ctx.stop();
}

// ─── Dependency injection tests ───────────────────────────────────────────────

TEST(Singleton, InjectedDependencyIsSameInstanceAsDirectResolve) {
    auto& ctx = ctr::BeanContext::resolveContext("st-injection");
    ctx.discover<^^singleton_fixture>().start();
    // Pre-resolve Dependency to materialise it before Consumer is constructed.
    // See comment on Consumer fixture above for the non-recursive-lock rationale.
    auto dep      = ctx.resolve<singleton_fixture::Dependency>();
    auto consumer = ctx.resolve<singleton_fixture::Consumer>();
    EXPECT_EQ(consumer->dep.operator->(), dep.operator->());
    ctx.stop();
}

// Shutdown — full destruction lifecycle test (preDestroy + onDestroyed) deferred:
// GCC 16 reflect_constant fails when discovering types whose destructor references
// a non-constexpr global, preventing `Destructible`-style fixtures from compiling.

// ─── Eager singleton tests ─────────────────────────────────────────────────────
// Each sub-fixture is in its own namespace so discovering it only materializes
// the types under test, avoiding cross-test interference.

namespace eager_basic_fixture {
inline int eagerCount = 0;
struct [[=ctr::singleton{.lazy = false}]] EagerSvc {
    EagerSvc() { ++eagerCount; }
};
} // namespace eager_basic_fixture

namespace lazy_basic_fixture {
inline int lazyCount = 0;
struct [[=ctr::singleton{}]] LazySvc { // default: lazy = true
    LazySvc() { ++lazyCount; }
};
} // namespace lazy_basic_fixture

namespace eager_dep_fixture {
inline int depCount = 0;
struct [[=ctr::singleton{.lazy = false}]] EagerDep {
    EagerDep() { ++depCount; }
};
struct [[=ctr::singleton{.lazy = false}]] EagerConsumer {
    ctr::Bean<EagerDep> dep;
    explicit EagerConsumer(ctr::Bean<EagerDep> d) : dep(std::move(d)) { ++depCount; }
};
} // namespace eager_dep_fixture

namespace eager_throw_fixture {
struct [[=ctr::singleton{.lazy = false}]] EagerThrowing {
    EagerThrowing() { throw std::runtime_error("eager ctor failed"); }
};
} // namespace eager_throw_fixture

TEST(Singleton, EagerSingleton_BuiltAtStart) {
    eager_basic_fixture::eagerCount = 0;
    auto& ctx = ctr::BeanContext::resolveContext("st-eager-built-at-start");
    ctx.discover<^^eager_basic_fixture>().start();
    EXPECT_EQ(eager_basic_fixture::eagerCount, 1); // built before any resolve
    ctx.stop();
}

TEST(Singleton, LazySingleton_NotBuiltAtStart) {
    lazy_basic_fixture::lazyCount = 0;
    auto& ctx = ctr::BeanContext::resolveContext("st-lazy-not-built-at-start");
    ctx.discover<^^lazy_basic_fixture>().start();
    EXPECT_EQ(lazy_basic_fixture::lazyCount, 0); // not built at start
    ctx.resolve<lazy_basic_fixture::LazySvc>();
    EXPECT_EQ(lazy_basic_fixture::lazyCount, 1); // built on first resolve
    ctx.stop();
}

TEST(Singleton, EagerSingletonWithDep_BothBuiltAtStart) {
    eager_dep_fixture::depCount = 0;
    auto& ctx = ctr::BeanContext::resolveContext("st-eager-with-dep");
    ctx.discover<^^eager_dep_fixture>().start();
    EXPECT_EQ(eager_dep_fixture::depCount, 2); // EagerDep + EagerConsumer both built
    auto b = ctx.resolve<eager_dep_fixture::EagerConsumer>();
    EXPECT_NE(b->dep.operator->(), nullptr);
    ctx.stop();
}

TEST(Singleton, EagerSingleton_ExceptionPropagatesFromStart) {
    auto& ctx = ctr::BeanContext::resolveContext("st-eager-throws");
    ctx.discover<^^eager_throw_fixture>();
    EXPECT_THROW(ctx.start(), std::runtime_error);
    ctx.stop();
}
