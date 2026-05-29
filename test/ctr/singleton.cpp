#include <gtest/gtest.h>
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

// Named-bean annotations (ctr::named{.name = define_static_string("x")}) require
// GCC 16 to support reflect_constant on class types with const char* members.
// This is not yet stable in GCC 16.1.0: extract<named>(ann) throws 'reflect_constant
// failed'.  Named-bean tests are therefore deferred until compiler support improves.

} // namespace singleton_fixture

// Unregistered type — deliberately not discovered in any test context.
struct NotRegistered {};

// ─── Lifecycle state tests ────────────────────────────────────────────────────

TEST(Singleton, ResolveBeforeStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("st-pre-start");
    ctx.discover<^^singleton_fixture>();
    EXPECT_THROW(ctx.resolve<singleton_fixture::Service>(), ctr::ContextStateError);
    ctx.close();
}

TEST(Singleton, DiscoverAfterStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("st-post-start-discover");
    ctx.discover<^^singleton_fixture>().start();
    EXPECT_THROW(ctx.discover<^^singleton_fixture>(), ctr::ContextStateError);
    ctx.close();
}

TEST(Singleton, StartIsIdempotent) {
    auto& ctx = ctr::BeanContext::resolveContext("st-start-idempotent");
    ctx.discover<^^singleton_fixture>().start();
    EXPECT_NO_THROW(ctx.start());
    ctx.close();
}

// ─── Resolution and identity tests ───────────────────────────────────────────

TEST(Singleton, ResolveReturnsValidBean) {
    auto& ctx = ctr::BeanContext::resolveContext("st-basic-resolve");
    ctx.discover<^^singleton_fixture>().start();
    auto bean = ctx.resolve<singleton_fixture::Service>();
    EXPECT_NE(bean.operator->(), nullptr);
    ctx.close();
}

TEST(Singleton, SameObjectReturnedOnRepeatedResolve) {
    auto& ctx = ctr::BeanContext::resolveContext("st-identity");
    ctx.discover<^^singleton_fixture>().start();
    auto b1 = ctx.resolve<singleton_fixture::Service>();
    auto b2 = ctx.resolve<singleton_fixture::Service>();
    EXPECT_EQ(b1, b2);
    ctx.close();
}

TEST(Singleton, SameContextKeyReturnsSameContextInstance) {
    auto& ctx1 = ctr::BeanContext::resolveContext("st-same-key");
    auto& ctx2 = ctr::BeanContext::resolveContext("st-same-key");
    EXPECT_EQ(&ctx1, &ctx2);
    ctx1.close();
}

TEST(Singleton, DifferentContextKeysYieldIndependentInstances) {
    auto& ctx1 = ctr::BeanContext::resolveContext("st-key-a");
    auto& ctx2 = ctr::BeanContext::resolveContext("st-key-b");
    ctx1.discover<^^singleton_fixture>().start();
    ctx2.discover<^^singleton_fixture>().start();
    auto b1 = ctx1.resolve<singleton_fixture::Service>();
    auto b2 = ctx2.resolve<singleton_fixture::Service>();
    EXPECT_NE(b1, b2);
    ctx1.close();
    ctx2.close();
}

// ─── Named key tests ─────────────────────────────────────────────────────────
// Note: named-bean annotation tests (resolve by name, unnamed-resolve-of-named-only)
// require GCC 16 support for reflect_constant on const char* class members.
// See fixture comment above.

TEST(Singleton, NamedResolveWithUnknownKeyRaisesResolutionError) {
    auto& ctx = ctr::BeanContext::resolveContext("st-unknown-key");
    ctx.discover<^^singleton_fixture>().start();
    // "nonexistent" was never interned as a NameId → ResolutionError.
    EXPECT_THROW(
        ctx.resolve<singleton_fixture::Service>(ctr::named{"nonexistent"}),
        ctr::ResolutionError);
    ctx.close();
}

TEST(Singleton, ResolveOfUnregisteredTypeRaisesResolutionError) {
    auto& ctx = ctr::BeanContext::resolveContext("st-unknown-type");
    ctx.discover<^^singleton_fixture>().start();
    EXPECT_THROW(ctx.resolve<NotRegistered>(), ctr::ResolutionError);
    ctx.close();
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
    ctx.close();
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
    ctx.close();
}

// Shutdown — full destruction lifecycle test (preDestroy + onDestroyed) deferred:
// GCC 16 reflect_constant fails when discovering types whose destructor references
// a non-constexpr global, preventing `Destructible`-style fixtures from compiling.
