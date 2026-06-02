#include <optional>
#include <utility>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_handles_fixture {

struct HandleBase {
    virtual ~HandleBase() = default;
    int value = 0;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonObject : HandleBase {
    SingletonObject() { value = 1; }
};

struct [[=CTORIUM_NAMESPACE::prototype{}]] PrototypeObject {
    int value = 2;
};

struct [[=CTORIUM_NAMESPACE::session{}]] SessionObject {
    int value = 3;
};

struct Unrelated {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] DeferredConsumer {
    CTORIUM_NAMESPACE::Bean<SessionObject> session;
    explicit DeferredConsumer(
        [[=CTORIUM_NAMESPACE::scoped{.name = std::define_static_string("ch-deferred-scope")}]]
        CTORIUM_NAMESPACE::Bean<SessionObject> injected)
        : session(std::move(injected)) {}
};

} // namespace contract_handles_fixture

namespace contract_handles_cast_forms_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] SessionObject {};
struct [[=CTORIUM_NAMESPACE::threadLocal{}]] ThreadLocalObject {};
struct [[=CTORIUM_NAMESPACE::singleton{}]] Unrelated {};

} // namespace contract_handles_cast_forms_fixture

TEST(ContractHandles, Bean_CopyRetainsSameBean) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-copy");
    context.discover<^^contract_handles_fixture>().start();
    auto first = context.resolve<contract_handles_fixture::SingletonObject>();
    auto copy = first;
    EXPECT_EQ(first, copy);
    EXPECT_EQ(first.operator->(), copy.operator->());
    context.stop();
}

TEST(ContractHandles, Bean_MoveTransfers) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-move");
    context.discover<^^contract_handles_fixture>().start();
    auto first = context.resolve<contract_handles_fixture::SingletonObject>();
    auto* raw = first.operator->();
    auto moved = std::move(first);
    EXPECT_EQ(first.operator->(), nullptr);
    EXPECT_EQ(moved.operator->(), raw);
    context.stop();
}

TEST(ContractHandles, Bean_DestroyReleasesTracking) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-destroy-releases");
    context.discover<^^contract_handles_fixture>().start();
    int destroyed = 0;
    context.on<contract_handles_fixture::PrototypeObject>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<contract_handles_fixture::PrototypeObject>&) { ++destroyed; });
    {
        auto bean = context.resolve<contract_handles_fixture::PrototypeObject>();
        EXPECT_NE(bean.operator->(), nullptr);
    }
    EXPECT_EQ(destroyed, 1);
    context.stop();
}

TEST(ContractHandles, Bean_PrototypeCopy_AtomicRefcount) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-prototype-copy");
    context.discover<^^contract_handles_fixture>().start();
    int destroyed = 0;
    context.on<contract_handles_fixture::PrototypeObject>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<contract_handles_fixture::PrototypeObject>&) { ++destroyed; });
    {
        auto first = context.resolve<contract_handles_fixture::PrototypeObject>();
        {
            auto copy = first;
            EXPECT_EQ(first, copy);
            EXPECT_EQ(destroyed, 0);
        }
        EXPECT_EQ(destroyed, 0);
    }
    EXPECT_EQ(destroyed, 1);
    context.stop();
}

TEST(ContractHandles, Bean_PrototypeLastRelease_DestroysInstance) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-prototype-last-release");
    context.discover<^^contract_handles_fixture>().start();
    int destroyed = 0;
    context.on<contract_handles_fixture::PrototypeObject>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<contract_handles_fixture::PrototypeObject>&) { ++destroyed; });
    {
        auto bean = context.resolve<contract_handles_fixture::PrototypeObject>();
        EXPECT_NE(bean.operator->(), nullptr);
    }
    EXPECT_EQ(destroyed, 1);
    context.stop();
}

TEST(ContractHandles, Bean_Context_ReturnsOwner) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-context-owner");
    context.discover<^^contract_handles_fixture>().start();
    auto& scope = context.resolveScope("ch-context-owner-scope");
    scope.start();

    auto rootBean = scope.resolve<contract_handles_fixture::SingletonObject>();
    auto scopedBean = scope.resolve<contract_handles_fixture::SessionObject>();
    EXPECT_EQ(&rootBean.context(), &context);
    EXPECT_EQ(&scopedBean.context(), &scope);

    context.stop();
}

TEST(ContractHandles, Bean_Metadata_ReturnsValidView) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-metadata");
    context.discover<^^contract_handles_fixture>().start();
    auto bean = context.resolve<contract_handles_fixture::SingletonObject>();
    EXPECT_EQ(bean.metadata().exactType(), typeid(contract_handles_fixture::SingletonObject));
    context.stop();
}

TEST(ContractHandles, Bean_Exact_MatchesConcrete) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-exact");
    context.discover<^^contract_handles_fixture>().start();
    auto bean = context.resolve<contract_handles_fixture::SingletonObject>();
    EXPECT_TRUE(bean.exact<contract_handles_fixture::SingletonObject>());
    EXPECT_FALSE(bean.exact<contract_handles_fixture::HandleBase>());
    context.stop();
}

TEST(ContractHandles, Bean_Compatible_MatchesBase) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-compatible");
    context.discover<^^contract_handles_fixture>().start();
    auto bean = context.resolve<contract_handles_fixture::SingletonObject>();
    EXPECT_TRUE(bean.compatible<contract_handles_fixture::HandleBase>());
    context.stop();
}

TEST(ContractHandles, Bean_Cast_SuccessReturnsTyped) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-cast-success");
    context.discover<^^contract_handles_fixture>().start();
    auto bean = context.resolve<contract_handles_fixture::SingletonObject>();
    auto base = bean.cast<contract_handles_fixture::HandleBase>();
    EXPECT_EQ(base.operator->(), static_cast<contract_handles_fixture::HandleBase*>(bean.operator->()));
    context.stop();
}

TEST(ContractHandles, Bean_Cast_IncompatibleThrows) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-cast-incompatible");
    context.discover<^^contract_handles_fixture>().start();
    auto bean = context.resolve<contract_handles_fixture::SingletonObject>();
    EXPECT_THROW(bean.cast<contract_handles_fixture::Unrelated>(), CTORIUM_NAMESPACE::ResolutionError);
    context.stop();
}

TEST(ContractHandles, Bean_TryCast_IncompatibleReturnsNullopt) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-trycast-incompatible");
    context.discover<^^contract_handles_fixture>().start();
    auto bean = context.resolve<contract_handles_fixture::SingletonObject>();
    EXPECT_EQ(bean.tryCast<contract_handles_fixture::Unrelated>(), std::nullopt);
    context.stop();
}

TEST(ContractHandles, Bean_SessionHandle_CastHelpersMatchConcrete) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-session-cast-helpers");
    root.discover<^^contract_handles_cast_forms_fixture>().start();
    auto& scope = root.resolveScope("ch-session-cast-helpers-scope");
    scope.start();

    auto bean = scope.resolve<contract_handles_cast_forms_fixture::SessionObject>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_TRUE(bean.exact<contract_handles_cast_forms_fixture::SessionObject>());
    EXPECT_TRUE(bean.compatible<contract_handles_cast_forms_fixture::SessionObject>());
    EXPECT_FALSE(bean.compatible<contract_handles_cast_forms_fixture::Unrelated>());

    auto casted = bean.cast<contract_handles_cast_forms_fixture::SessionObject>();
    EXPECT_EQ(casted.operator->(), bean.operator->());
    EXPECT_THROW(
        { (void)bean.cast<contract_handles_cast_forms_fixture::Unrelated>(); },
        CTORIUM_NAMESPACE::ResolutionError);
    EXPECT_EQ(bean.tryCast<contract_handles_cast_forms_fixture::Unrelated>(), std::nullopt);

    root.stop();
}

TEST(ContractHandles, Bean_ThreadLocalHandle_CastHelpersMatchConcrete) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-thread-local-cast-helpers");
    root.discover<^^contract_handles_cast_forms_fixture>().start();

    auto bean = root.resolve<contract_handles_cast_forms_fixture::ThreadLocalObject>();
    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_TRUE(bean.exact<contract_handles_cast_forms_fixture::ThreadLocalObject>());
    EXPECT_TRUE(bean.compatible<contract_handles_cast_forms_fixture::ThreadLocalObject>());
    EXPECT_FALSE(bean.compatible<contract_handles_cast_forms_fixture::Unrelated>());

    auto casted = bean.cast<contract_handles_cast_forms_fixture::ThreadLocalObject>();
    EXPECT_EQ(casted.operator->(), bean.operator->());
    EXPECT_THROW(
        { (void)bean.cast<contract_handles_cast_forms_fixture::Unrelated>(); },
        CTORIUM_NAMESPACE::ResolutionError);
    EXPECT_EQ(bean.tryCast<contract_handles_cast_forms_fixture::Unrelated>(), std::nullopt);

    root.stop();
}

TEST(ContractHandles, Bean_Equality_SameSingleton) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-eq-singleton");
    context.discover<^^contract_handles_fixture>().start();
    auto first = context.resolve<contract_handles_fixture::SingletonObject>();
    auto second = context.resolve<contract_handles_fixture::SingletonObject>();
    EXPECT_EQ(first, second);
    context.stop();
}

TEST(ContractHandles, Bean_SessionHandle_EqualitySameScope) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-session-eq-same-scope");
    root.discover<^^contract_handles_cast_forms_fixture>().start();
    auto& scope = root.resolveScope("ch-session-eq-same-scope-owner");
    scope.start();

    auto first = scope.resolve<contract_handles_cast_forms_fixture::SessionObject>();
    auto second = scope.resolve<contract_handles_cast_forms_fixture::SessionObject>();
    EXPECT_EQ(first, second);
    EXPECT_FALSE(first != second);

    root.stop();
}

TEST(ContractHandles, Bean_SessionHandle_EqualityDistinctScopes) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-session-eq-distinct-scopes");
    root.discover<^^contract_handles_cast_forms_fixture>().start();
    auto& firstScope = root.resolveScope("ch-session-eq-scope-a");
    auto& secondScope = root.resolveScope("ch-session-eq-scope-b");
    firstScope.start();
    secondScope.start();

    auto first = firstScope.resolve<contract_handles_cast_forms_fixture::SessionObject>();
    auto second = secondScope.resolve<contract_handles_cast_forms_fixture::SessionObject>();
    EXPECT_NE(first, second);
    EXPECT_TRUE(first != second);

    root.stop();
}

TEST(ContractHandles, Bean_SessionHandle_EqualityAfterScopeStopDoesNotDereference) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-session-eq-stopped-scope");
    root.discover<^^contract_handles_cast_forms_fixture>().start();
    auto& scope = root.resolveScope("ch-session-eq-stopped-scope-owner");
    scope.start();

    auto first = scope.resolve<contract_handles_cast_forms_fixture::SessionObject>();
    auto second = scope.resolve<contract_handles_cast_forms_fixture::SessionObject>();
    ASSERT_NE(first.operator->(), nullptr);

    scope.stop();

    EXPECT_NO_THROW({
        const bool equal = first == second;
        EXPECT_TRUE(equal);
    });

    root.stop();
}

TEST(ContractHandles, Bean_ThreadLocalHandle_EqualitySameThread) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-thread-local-eq-same-thread");
    root.discover<^^contract_handles_cast_forms_fixture>().start();

    auto first = root.resolve<contract_handles_cast_forms_fixture::ThreadLocalObject>();
    auto second = root.resolve<contract_handles_cast_forms_fixture::ThreadLocalObject>();
    EXPECT_EQ(first, second);
    EXPECT_FALSE(first != second);

    root.stop();
}

TEST(ContractHandles, Bean_SessionHandle_DereferenceAccessorsMatchOperatorArrow) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-session-deref-accessors");
    root.discover<^^contract_handles_cast_forms_fixture>().start();
    auto& scope = root.resolveScope("ch-session-deref-accessors-scope");
    scope.start();

    auto bean = scope.resolve<contract_handles_cast_forms_fixture::SessionObject>();
    auto* pointer = bean.operator->();
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(&(*bean), pointer);
    EXPECT_EQ(&bean.value(), pointer);

    root.stop();
}

TEST(ContractHandles, Bean_ThreadLocalHandle_DereferenceAccessorsAndMetadataMatch) {
    auto& root = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-thread-local-deref-metadata");
    root.discover<^^contract_handles_cast_forms_fixture>().start();

    auto bean = root.resolve<contract_handles_cast_forms_fixture::ThreadLocalObject>();
    auto* pointer = bean.operator->();
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(&(*bean), pointer);
    EXPECT_EQ(&bean.value(), pointer);
    EXPECT_EQ(bean.metadata().lifetime(), CTORIUM_NAMESPACE::Lifetime::ThreadLocal);

    root.stop();
}

TEST(ContractHandles, Bean_Equality_DifferentPrototypes) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-eq-prototype");
    context.discover<^^contract_handles_fixture>().start();
    auto first = context.resolve<contract_handles_fixture::PrototypeObject>();
    auto second = context.resolve<contract_handles_fixture::PrototypeObject>();
    EXPECT_NE(first, second);
    context.stop();
}

TEST(ContractHandles, Bean_DeferredHandle_NullptrWhenScopeStopped) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-deferred");
    context.discover<^^contract_handles_fixture>().start();
    auto& scope = context.resolveScope("ch-deferred-scope");
    scope.start();
    auto consumer = context.resolve<contract_handles_fixture::DeferredConsumer>();
    ASSERT_NE(consumer->session.operator->(), nullptr);
    scope.stop();
    EXPECT_EQ(consumer->session.operator->(), nullptr);
    context.stop();
}

TEST(ContractHandles, Bean_AfterRootStop_IsUB) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-normal-release-before-stop");
    context.discover<^^contract_handles_fixture>().start();
    {
        auto bean = context.resolve<contract_handles_fixture::SingletonObject>();
        EXPECT_NE(bean.operator->(), nullptr);
    }
    context.stop();
}
