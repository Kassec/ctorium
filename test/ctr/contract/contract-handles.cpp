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

TEST(ContractHandles, Bean_Equality_SameSingleton) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("ch-eq-singleton");
    context.discover<^^contract_handles_fixture>().start();
    auto first = context.resolve<contract_handles_fixture::SingletonObject>();
    auto second = context.resolve<contract_handles_fixture::SingletonObject>();
    EXPECT_EQ(first, second);
    context.stop();
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
