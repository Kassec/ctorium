#include <meta>
#include <string_view>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

// ─── Test fixtures ────────────────────────────────────────────────────────────
//
// All fixture types live in a single namespace to share the global typeIdFor<T>
// cache (static std::atomic per type) across test contexts.

namespace casts_fixture {

struct Base {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Impl : Base {};

struct [[=CTORIUM_NAMESPACE::prototype{}]] ProtoType {};

} // namespace casts_fixture

namespace casts_session_proxy_fixture {

struct SessionBase {};

struct [[=CTORIUM_NAMESPACE::session{}]] SessionDerived : SessionBase {
    int value = 1;
};

} // namespace casts_session_proxy_fixture

namespace casts_thread_local_proxy_fixture {

struct ThreadBase {};

struct [[=CTORIUM_NAMESPACE::threadLocal{}]] ThreadDerived : ThreadBase {
    int value = 2;
};

} // namespace casts_thread_local_proxy_fixture

namespace casts_anybean_session_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] SessionType {};

} // namespace casts_anybean_session_fixture

// ─── Bean<T>::context() ───────────────────────────────────────────────────────

TEST(BeanCast, ContextReturnsSingletonOwner) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-context-singleton");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_EQ(&b.context(), &ctx);
    ctx.stop();
}

TEST(BeanCast, ContextReturnsPrototypeOwner) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-context-prototype");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::ProtoType>();
    EXPECT_EQ(&b.context(), &ctx);
    ctx.stop();
}

// ─── Bean<T>::exact<U>() ──────────────────────────────────────────────────────

TEST(BeanCast, ExactTrueForOwnType) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-exact-true");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_TRUE(b.exact<casts_fixture::Impl>());
    ctx.stop();
}

TEST(BeanCast, ExactFalseForOtherType) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-exact-false");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_FALSE(b.exact<casts_fixture::Base>());
    ctx.stop();
}

// ─── Bean<T>::compatible<U>() ─────────────────────────────────────────────────

TEST(BeanCast, CompatibleTrueForExposedType) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-compat-true");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_TRUE(b.compatible<casts_fixture::Impl>());
    ctx.stop();
}

TEST(BeanCast, CompatibleFalseForUnrelatedType) {
    // ProtoType is registered but unrelated to Impl — must return false.
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-compat-false");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_FALSE(b.compatible<casts_fixture::ProtoType>());
    ctx.stop();
}

// ─── Bean<T>::cast<U>() ───────────────────────────────────────────────────────

TEST(BeanCast, CastToCompatibleSucceeds) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-cast-ok");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_NO_THROW({
        auto b2 = b.cast<casts_fixture::Impl>();
        EXPECT_EQ(b2.operator->(), b.operator->());
    });
    ctx.stop();
}

TEST(BeanCast, CastToIncompatibleThrowsResolutionError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-cast-throw");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_THROW({ (void)b.cast<casts_fixture::ProtoType>(); }, CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

// ─── Bean<T>::tryCast<U>() ────────────────────────────────────────────────────

TEST(BeanCast, TryCastToCompatibleReturnsValue) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-trycast-ok");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    auto opt = b.tryCast<casts_fixture::Impl>();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->operator->(), b.operator->());
    ctx.stop();
}

TEST(BeanCast, TryCastToIncompatibleReturnsNullopt) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-trycast-null");
    ctx.discover<^^casts_fixture>().start();
    auto b = ctx.resolve<casts_fixture::Impl>();
    EXPECT_FALSE(b.tryCast<casts_fixture::ProtoType>().has_value());
    ctx.stop();
}

// ─── Prototype cast retains correctly ─────────────────────────────────────────

TEST(BeanCast, PrototypeCastRetainsRefcount) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-proto-cast-retain");
    ctx.discover<^^casts_fixture>().start();

    int destroyCount = 0;
    ctx.on(CTORIUM_NAMESPACE::onDestroyed, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++destroyCount; });

    {
        auto b1 = ctx.resolve<casts_fixture::ProtoType>();
        {
            auto b2 = b1.cast<casts_fixture::ProtoType>(); // retain: refcount = 2
            EXPECT_EQ(destroyCount, 0);
        } // b2 destroyed: refcount = 1
        EXPECT_EQ(destroyCount, 0);
    } // b1 destroyed: refcount = 0 → destruction fires
    EXPECT_EQ(destroyCount, 1);

    ctx.stop();
}

// ─── AnyBean::context() ───────────────────────────────────────────────────────

TEST(AnyBeanCast, ContextReturnsOwner) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-context");
    ctx.discover<^^casts_fixture>().start();

    CTORIUM_NAMESPACE::AnyBean captured;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_EQ(&captured.context(), &ctx);
    ctx.stop();
}

// ─── AnyBean::exact<U>() / compatible<U>() ───────────────────────────────────

TEST(AnyBeanCast, ExactTrueForConcreteType) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-exact-true");
    ctx.discover<^^casts_fixture>().start();

    CTORIUM_NAMESPACE::AnyBean captured;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& b) {
        if (b.exact<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_TRUE(captured.exact<casts_fixture::Impl>());
    ctx.stop();
}

TEST(AnyBeanCast, CompatibleTrueForExposedType) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-compat-true");
    ctx.discover<^^casts_fixture>().start();

    CTORIUM_NAMESPACE::AnyBean captured;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_TRUE(captured.compatible<casts_fixture::Impl>());
    ctx.stop();
}

// ─── AnyBean::cast<U>() / tryCast<U>() ───────────────────────────────────────

TEST(AnyBeanCast, CastToCompatibleSucceeds) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-cast-ok");
    ctx.discover<^^casts_fixture>().start();

    CTORIUM_NAMESPACE::AnyBean captured;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_NO_THROW({ (void)captured.cast<casts_fixture::Impl>(); });
    ctx.stop();
}

TEST(AnyBeanCast, CastToIncompatibleThrows) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-cast-throw");
    ctx.discover<^^casts_fixture>().start();

    CTORIUM_NAMESPACE::AnyBean captured;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_THROW({ (void)captured.cast<casts_fixture::ProtoType>(); }, CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

TEST(AnyBeanCast, TryCastToIncompatibleReturnsNullopt) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-trycast-null");
    ctx.discover<^^casts_fixture>().start();

    CTORIUM_NAMESPACE::AnyBean captured;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& b) {
        if (b.compatible<casts_fixture::Impl>()) captured = b;
    });
    ctx.resolve<casts_fixture::Impl>();
    EXPECT_FALSE(captured.tryCast<casts_fixture::ProtoType>().has_value());
    ctx.stop();
}

// ─── Polymorphic exposure (SPEC-polymorphic-exposure) ────────────────────────

// Each poly sub-fixture in its own namespace to avoid TypeId aliasing.

namespace poly_single_fixture {
struct Base { int x = 10; };
struct [[=CTORIUM_NAMESPACE::singleton{}]] Impl : Base {};
} // namespace poly_single_fixture

namespace poly_multi_fixture {
struct B1 { int a = 1; };
struct B2 { int b = 2; };
struct [[=CTORIUM_NAMESPACE::singleton{}]] Impl : B1, B2 {};
} // namespace poly_multi_fixture

namespace poly_virt_fixture {
struct VBase { virtual ~VBase() = default; virtual int val() { return 99; } };
struct [[=CTORIUM_NAMESPACE::singleton{}]] Impl : virtual VBase {};
} // namespace poly_virt_fixture

namespace poly_proto_fixture {
struct PBase { int px = 20; };
struct [[=CTORIUM_NAMESPACE::prototype{}]] Impl : PBase {};
} // namespace poly_proto_fixture

TEST(BeanCast, PolyExposure_SingleInheritance_ResolveBase) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("poly-single");
    ctx.discover<^^poly_single_fixture>().start();
    auto base = ctx.resolve<poly_single_fixture::Base>();
    EXPECT_NE(base.operator->(), nullptr);
    EXPECT_TRUE(base.exact<poly_single_fixture::Impl>());
    EXPECT_TRUE(base.compatible<poly_single_fixture::Base>());
    ctx.stop();
}

TEST(BeanCast, PolyExposure_SameUnderlyingInstance) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("poly-same-instance");
    ctx.discover<^^poly_single_fixture>().start();
    auto impl = ctx.resolve<poly_single_fixture::Impl>();
    auto base = ctx.resolve<poly_single_fixture::Base>();
    // Both expose the same singleton; exact type is Impl for both.
    EXPECT_TRUE(impl.exact<poly_single_fixture::Impl>());
    EXPECT_TRUE(base.exact<poly_single_fixture::Impl>());
    // For single-inheritance offset-0, both raw pointers are identical.
    EXPECT_EQ(static_cast<void*>(impl.operator->()), static_cast<void*>(base.operator->()));
    ctx.stop();
}

TEST(BeanCast, PolyExposure_MultipleInheritance_AdjustedPointer) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("poly-multi");
    ctx.discover<^^poly_multi_fixture>().start();
    // B2 is the second base — may have a non-zero offset.
    auto b2 = ctx.resolve<poly_multi_fixture::B2>();
    EXPECT_NE(b2.operator->(), nullptr);
    EXPECT_EQ(b2->b, 2); // data member accessible via adjusted B2 pointer
    ctx.stop();
}

TEST(BeanCast, PolyExposure_Compatible_Base) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("poly-compat");
    ctx.discover<^^poly_single_fixture>().start();
    auto impl = ctx.resolve<poly_single_fixture::Impl>();
    EXPECT_TRUE(impl.compatible<poly_single_fixture::Base>());
    EXPECT_TRUE(impl.compatible<poly_single_fixture::Impl>());
    ctx.stop();
}

TEST(BeanCast, PolyExposure_CastBetweenBases_B1ToB2) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("poly-cast-b1b2");
    ctx.discover<^^poly_multi_fixture>().start();
    auto b1 = ctx.resolve<poly_multi_fixture::B1>();
    EXPECT_NE(b1.operator->(), nullptr);
    // Cast B1 → B2 round-trip via concrete.
    auto b2 = b1.cast<poly_multi_fixture::B2>();
    EXPECT_NE(b2.operator->(), nullptr);
    EXPECT_EQ(b2->b, 2);
    ctx.stop();
}

TEST(BeanCast, PolyExposure_VirtualBase_ResolveVBase) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("poly-virtual");
    ctx.discover<^^poly_virt_fixture>().start();
    auto vb = ctx.resolve<poly_virt_fixture::VBase>();
    EXPECT_NE(vb.operator->(), nullptr);
    EXPECT_EQ(vb->val(), 99); // virtual dispatch works through VBase*
    ctx.stop();
}

TEST(BeanCast, PolyExposure_Prototype_DestructionOnLastHandle) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("poly-proto-destruct");
    ctx.discover<^^poly_proto_fixture>().start();

    int destroyCount = 0;
    ctx.on(CTORIUM_NAMESPACE::onDestroyed, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++destroyCount; });

    {
        auto base = ctx.resolve<poly_proto_fixture::PBase>(); // ProtoImpl via alias
        EXPECT_NE(base.operator->(), nullptr);
        EXPECT_EQ(destroyCount, 0);
    } // last handle released → destruction fires
    EXPECT_EQ(destroyCount, 1);

    ctx.stop();
}

// ─── AnyBean prototype refcount: copy + destroy fires one destruction ─────────

TEST(AnyBeanCast, PrototypeCopyThenDestroyTriggersOneDestruction) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-proto-refcount");
    ctx.discover<^^casts_fixture>().start();

    int destroyCount = 0;
    ctx.on(CTORIUM_NAMESPACE::onDestroyed, [&](const CTORIUM_NAMESPACE::AnyBean&) { ++destroyCount; });

    {
        CTORIUM_NAMESPACE::AnyBean captured;
        ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& b) {
            if (b.compatible<casts_fixture::ProtoType>()) captured = b; // retain
        });

        {
            auto bean = ctx.resolve<casts_fixture::ProtoType>(); // refcount = 2
        } // bean destroyed: refcount = 1
        EXPECT_EQ(destroyCount, 0);
    } // captured destroyed: refcount = 0 → destruction fires
    EXPECT_EQ(destroyCount, 1);

    ctx.stop();
}

// ─── Bean metadata (SPEC-bean-metadata) ─────────────────────────────────────

namespace metadata_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] MySingleton {
    int x = 0;
    [[=CTORIUM_NAMESPACE::postConstruct{}]] void init() { ++x; }
};

struct [[=CTORIUM_NAMESPACE::prototype{}]] MyProto {};

struct [[=CTORIUM_NAMESPACE::session{}]] MySession {};

} // namespace metadata_fixture

TEST(BeanMetadata, Lifetime_Singleton) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-lt-singleton");
    ctx.discover<^^metadata_fixture>().start();
    auto b = ctx.resolve<metadata_fixture::MySingleton>();
    EXPECT_EQ(b.metadata().lifetime(), CTORIUM_NAMESPACE::Lifetime::Singleton);
    ctx.stop();
}

TEST(BeanMetadata, Lifetime_Prototype) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-lt-proto");
    ctx.discover<^^metadata_fixture>().start();
    auto b = ctx.resolve<metadata_fixture::MyProto>();
    EXPECT_EQ(b.metadata().lifetime(), CTORIUM_NAMESPACE::Lifetime::Prototype);
    ctx.stop();
}

TEST(BeanMetadata, Origin_AnnotatedType) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-origin");
    ctx.discover<^^metadata_fixture>().start();
    auto b = ctx.resolve<metadata_fixture::MySingleton>();
    EXPECT_EQ(b.metadata().origin(), CTORIUM_NAMESPACE::Origin::AnnotatedType);
    ctx.stop();
}

TEST(BeanMetadata, ObservedTypeMatchesExposedType) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-observed-type");
    ctx.discover<^^metadata_fixture>().start();
    auto b = ctx.resolve<metadata_fixture::MySingleton>();
    EXPECT_EQ(b.metadata().observedType(), typeid(metadata_fixture::MySingleton));
    ctx.stop();
}

TEST(BeanMetadata, Methods_EmptyWithoutRetainAllMetadata) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-methods-empty");
    ctx.discover<^^metadata_fixture>().start(); // retainAllMetadata = false (default)
    auto b = ctx.resolve<metadata_fixture::MySingleton>();
    EXPECT_TRUE(b.metadata().methods().empty());
    ctx.stop();
}

TEST(BeanMetadata, Methods_RetainedWithRetainAllMetadata) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-methods-retained");
    ctx.discover<^^metadata_fixture>({.retainAllMetadata = true}).start();
    auto b = ctx.resolve<metadata_fixture::MySingleton>();
    auto methods = b.metadata().methods();
    EXPECT_FALSE(methods.empty());
    ctx.stop();
}

TEST(BeanMetadata, AnnotatedWith_PostConstruct) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-annotated-postc");
    ctx.discover<^^metadata_fixture>({.retainAllMetadata = true}).start();
    auto b = ctx.resolve<metadata_fixture::MySingleton>();
    auto filtered = b.metadata().methods().annotatedWith<CTORIUM_NAMESPACE::postConstruct>();
    EXPECT_FALSE(filtered.empty());
    // The filtered range should contain the 'init' method.
    bool found = false;
    for (const auto mv : filtered) {
        if (std::string_view{mv.name()} == "init") found = true;
    }
    EXPECT_TRUE(found);
    ctx.stop();
}

TEST(BeanMetadata, ObservedType_BaseHandle) {
    // Via polymorphic exposure: resolve<Base>() → observedType() = Base.
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-observed-base");
    ctx.discover<^^poly_single_fixture>().start();
    auto base = ctx.resolve<poly_single_fixture::Base>();
    EXPECT_EQ(base.metadata().observedType(), typeid(poly_single_fixture::Base));
    EXPECT_EQ(base.metadata().exactType(),    typeid(poly_single_fixture::Impl));
    ctx.stop();
}

TEST(BeanMetadata, Lifetime_Session_Form2) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-lt-session");
    ctx.discover<^^metadata_fixture>().start();
    auto& scope = ctx.resolveScope("s");
    scope.start();
    // Session handle is Form 2; metadata must still report Lifetime::Session.
    auto s = scope.resolve<metadata_fixture::MySession>();
    EXPECT_EQ(s.metadata().lifetime(), CTORIUM_NAMESPACE::Lifetime::Session);
    ctx.stop();
}

namespace bound_fixture {
struct BoundSvc {}; // no Ctorium annotation — will be registered via bindSingleton pre-start
} // namespace bound_fixture

TEST(BeanMetadata, BoundSingleton_NoyauAvailable) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-bound");
    ctx.bindSingleton(std::make_unique<bound_fixture::BoundSvc>()); // pre-start
    ctx.start();
    auto bean = ctx.resolve<bound_fixture::BoundSvc>();
    EXPECT_EQ(bean.metadata().exactType(), typeid(bound_fixture::BoundSvc));
    EXPECT_EQ(bean.metadata().origin(), CTORIUM_NAMESPACE::Origin::RuntimeBinding);
    EXPECT_TRUE(bean.metadata().methods().empty()); // no reflective data for runtime bindings
    ctx.stop();
}

namespace metadata_name_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] UnnamedService {};

struct [[=CTORIUM_NAMESPACE::singleton{}]]
       [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("named")}]]
       NamedService {};

} // namespace metadata_name_fixture

TEST(BeanMetadata, NameReportsNamedKeyAndEmptyForUnnamedBean) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-name");
    ctx.discover<^^metadata_name_fixture>().start();

    auto unnamed = ctx.resolve<metadata_name_fixture::UnnamedService>();
    auto named = ctx.resolve<metadata_name_fixture::NamedService>(
        CTORIUM_NAMESPACE::named{.name = std::define_static_string("named")});

    EXPECT_TRUE(unnamed.metadata().name().empty());
    EXPECT_EQ(named.metadata().name(), std::string_view{"named"});

    ctx.stop();
}

namespace metadata_method_retention_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Dep {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Service {
    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void init(CTORIUM_NAMESPACE::Bean<Dep>) {}
};

} // namespace metadata_method_retention_fixture

TEST(BeanMetadata, RetainedAnnotatedMethodsExposeNameAnnotationAndParameterCount) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-method-retained-details");
    ctx.discover<^^metadata_method_retention_fixture>({.retainAllMetadata = true}).start();

    auto service = ctx.resolve<metadata_method_retention_fixture::Service>();
    auto postConstructMethods =
        service.metadata().methods().annotatedWith<CTORIUM_NAMESPACE::postConstruct>();

    ASSERT_FALSE(postConstructMethods.empty());
    auto method = *postConstructMethods.begin();
    EXPECT_EQ(std::string_view{method.name()}, "init");
    EXPECT_EQ(method.parameterCount(), 1u);

    ctx.stop();
}

TEST(BeanMetadata, MethodAnnotationObjectAndParameterMetadataAreNotInPublicApi) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("meta-method-annotation-parameters");
    ctx.discover<^^metadata_method_retention_fixture>({.retainAllMetadata = true}).start();

    auto service = ctx.resolve<metadata_method_retention_fixture::Service>();
    auto methods = service.metadata().methods();
    ASSERT_FALSE(methods.empty());

    auto method = *methods.begin();
    auto annotation = method.annotation<CTORIUM_NAMESPACE::postConstruct>();
    ASSERT_TRUE(annotation.has_value());
    EXPECT_FALSE(method.annotation<CTORIUM_NAMESPACE::preDestroy>().has_value());

    auto parameters = method.parameters();
    ASSERT_EQ(parameters.size(), 1u);
    EXPECT_EQ(parameters.size(), method.parameterCount());
    EXPECT_EQ(parameters[0].injectedType(), typeid(metadata_method_retention_fixture::Dep));

    auto postConstructMethods =
        service.metadata().methods().annotatedWith<CTORIUM_NAMESPACE::postConstruct>();
    ASSERT_FALSE(postConstructMethods.empty());
    auto postConstructMethod = *postConstructMethods.begin();
    EXPECT_TRUE(postConstructMethod.annotation<CTORIUM_NAMESPACE::postConstruct>().has_value());
    EXPECT_EQ(postConstructMethod.parameters().size(), 1u);
    EXPECT_EQ(postConstructMethod.parameters().size(), postConstructMethod.parameterCount());

    ctx.stop();
}

namespace anybean_equality_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] First {};
struct [[=CTORIUM_NAMESPACE::singleton{}]] Second {};

} // namespace anybean_equality_fixture

TEST(AnyBeanCast, EqualityComparesLogicalBeanIdentity) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-equality");
    ctx.discover<^^anybean_equality_fixture>().start();

    CTORIUM_NAMESPACE::AnyBean first;
    CTORIUM_NAMESPACE::AnyBean second;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<anybean_equality_fixture::First>()) {
            first = bean;
        } else if (bean.compatible<anybean_equality_fixture::Second>()) {
            second = bean;
        }
    });

    (void)ctx.resolve<anybean_equality_fixture::First>();
    (void)ctx.resolve<anybean_equality_fixture::Second>();

    CTORIUM_NAMESPACE::AnyBean sameFirst = first;

    EXPECT_EQ(first, sameFirst);
    EXPECT_NE(first, second);

    ctx.stop();
}

TEST(BeanCast, SessionProxyCastsToCompatibleBaseBeforeMaterialization) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-session-proxy-cast");
    ctx.discover<^^casts_session_proxy_fixture>().start();
    auto& scope = ctx.resolveScope("bc-session-proxy-cast-scope");
    scope.start();

    auto bean = scope.resolve<casts_session_proxy_fixture::SessionDerived>();
    EXPECT_TRUE(bean.compatible<casts_session_proxy_fixture::SessionBase>());
    auto compatible = bean.tryCast<casts_session_proxy_fixture::SessionBase>();
    ASSERT_TRUE(compatible.has_value());

    auto casted = bean.cast<casts_session_proxy_fixture::SessionBase>();
    auto* derivedRaw = bean.operator->();
    auto* baseRaw = casted.operator->();
    EXPECT_NE(derivedRaw, nullptr);
    EXPECT_EQ(static_cast<void*>(baseRaw), static_cast<void*>(derivedRaw));
    ctx.stop();
}

TEST(BeanCast, ThreadLocalProxyCastsToCompatibleBaseBeforeMaterialization) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("bc-threadlocal-proxy-cast");
    ctx.discover<^^casts_thread_local_proxy_fixture>().start();

    auto bean = ctx.resolve<casts_thread_local_proxy_fixture::ThreadDerived>();
    EXPECT_TRUE(bean.compatible<casts_thread_local_proxy_fixture::ThreadBase>());
    auto compatible = bean.tryCast<casts_thread_local_proxy_fixture::ThreadBase>();
    ASSERT_TRUE(compatible.has_value());

    auto casted = bean.cast<casts_thread_local_proxy_fixture::ThreadBase>();
    auto* derivedRaw = bean.operator->();
    auto* baseRaw = casted.operator->();
    EXPECT_NE(derivedRaw, nullptr);
    EXPECT_EQ(static_cast<void*>(baseRaw), static_cast<void*>(derivedRaw));
    ctx.stop();
}

TEST(AnyBeanCast, SessionLifecycleContextIsOwningScope) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("ab-session-context");
    ctx.discover<^^casts_anybean_session_fixture>().start();
    auto& scope = ctx.resolveScope("ab-session-context-scope");
    scope.start();

    CTORIUM_NAMESPACE::AnyBean captured;
    bool capturedSession = false;
    ctx.on(CTORIUM_NAMESPACE::onCreated, [&](const CTORIUM_NAMESPACE::AnyBean& bean) {
        if (bean.compatible<casts_anybean_session_fixture::SessionType>()) {
            captured = bean;
            capturedSession = true;
        }
    });

    auto bean = scope.resolve<casts_anybean_session_fixture::SessionType>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_TRUE(capturedSession);
    EXPECT_EQ(&captured.context(), &scope);
    ctx.stop();
}
