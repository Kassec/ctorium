#include <utility>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_scoped_injection_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Target {
    int value = 1;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] NamedScopedValidationConsumer {
    CTORIUM_NAMESPACE::Bean<Target> target;
    explicit NamedScopedValidationConsumer(
        [[=CTORIUM_NAMESPACE::scoped{.name = std::define_static_string("csi-target-scope")}]]
        [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("session")}]]
        CTORIUM_NAMESPACE::Bean<Target> injected)
        : target(std::move(injected)) {}
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] NamedScopedInvalidConsumer {
    CTORIUM_NAMESPACE::Bean<Target> target;
    explicit NamedScopedInvalidConsumer(
        [[=CTORIUM_NAMESPACE::scoped{.name = std::define_static_string("csi-target-scope")}]]
        [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("singleton")}]]
        CTORIUM_NAMESPACE::Bean<Target> injected)
        : target(std::move(injected)) {}
};

struct [[=CTORIUM_NAMESPACE::session{}]] SessionObject {
    int value = 1;
};

struct [[=CTORIUM_NAMESPACE::session{}]]
       [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("primary")}]]
PrimarySessionObject {
    int value = 2;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ScopedConsumer {
    CTORIUM_NAMESPACE::Bean<SessionObject> session;
    explicit ScopedConsumer(
        [[=CTORIUM_NAMESPACE::scoped{.name = std::define_static_string("csi-scope")}]]
        CTORIUM_NAMESPACE::Bean<SessionObject> injected)
        : session(std::move(injected)) {}
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ScopedNamedConsumer {
    CTORIUM_NAMESPACE::Bean<PrimarySessionObject> session;
    explicit ScopedNamedConsumer(
        [[=CTORIUM_NAMESPACE::scoped{.name = std::define_static_string("csi-scope")}]]
        [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("primary")}]]
        CTORIUM_NAMESPACE::Bean<PrimarySessionObject> injected)
        : session(std::move(injected)) {}
};

struct [[=CTORIUM_NAMESPACE::session{}]] SameScopeConsumer {
    CTORIUM_NAMESPACE::Bean<SessionObject> session;
    explicit SameScopeConsumer(CTORIUM_NAMESPACE::Bean<SessionObject> injected)
        : session(std::move(injected)) {}
};

} // namespace contract_scoped_injection_fixture

namespace contract_scoped_injection_missing_fixture {

struct [[=CTORIUM_NAMESPACE::session{}]] SessionObject {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] BadSingletonConsumer {
    CTORIUM_NAMESPACE::Bean<SessionObject> session;
    explicit BadSingletonConsumer(CTORIUM_NAMESPACE::Bean<SessionObject> injected)
        : session(std::move(injected)) {}
};

} // namespace contract_scoped_injection_missing_fixture

namespace contract_scoped_injection_invalid_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonObject {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] BadScopedConsumer {
    CTORIUM_NAMESPACE::Bean<SingletonObject> object;
    explicit BadScopedConsumer([[=CTORIUM_NAMESPACE::scoped{}]] CTORIUM_NAMESPACE::Bean<SingletonObject> injected)
        : object(std::move(injected)) {}
};

} // namespace contract_scoped_injection_invalid_fixture

TEST(ContractScopedInjection, ScopedHandle_NullBeforeScopeStart) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("csi-before-start");
    context.discover<^^contract_scoped_injection_fixture>().start();
    auto consumer = context.resolve<contract_scoped_injection_fixture::ScopedConsumer>();
    EXPECT_EQ(consumer->session.operator->(), nullptr);
    context.stop();
}

TEST(ContractScopedInjection, ScopedHandle_ResolvesAfterScopeStart) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("csi-after-start");
    context.discover<^^contract_scoped_injection_fixture>().start();
    auto& scope = context.resolveScope("csi-scope");
    scope.start();
    auto consumer = context.resolve<contract_scoped_injection_fixture::ScopedConsumer>();
    ASSERT_NE(consumer->session.operator->(), nullptr);
    EXPECT_EQ(consumer->session->value, 1);
    context.stop();
}

TEST(ContractScopedInjection, ScopedHandle_NullAfterScopeStop) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("csi-after-stop");
    context.discover<^^contract_scoped_injection_fixture>().start();
    auto& scope = context.resolveScope("csi-scope");
    scope.start();
    auto consumer = context.resolve<contract_scoped_injection_fixture::ScopedConsumer>();
    ASSERT_NE(consumer->session.operator->(), nullptr);
    scope.stop();
    EXPECT_EQ(consumer->session.operator->(), nullptr);
    context.stop();
}

TEST(ContractScopedInjection, ScopedNamed_TargetsNamedSession) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("csi-named");
    context.discover<^^contract_scoped_injection_fixture>().start();
    auto& scope = context.resolveScope("csi-scope");
    scope.start();
    auto consumer = context.resolve<contract_scoped_injection_fixture::ScopedNamedConsumer>();
    ASSERT_NE(consumer->session.operator->(), nullptr);
    EXPECT_EQ(consumer->session->value, 2);
    context.stop();
}

TEST(ContractScopedInjection, NamedScopedValidationUsesNamedSessionCandidate) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("csi-named-scoped-validation");
    context.discover<^^contract_scoped_injection_fixture>();
    auto& scope = context.resolveScope("csi-target-scope");
    scope.bindSession<contract_scoped_injection_fixture::Target>(
        std::make_unique<contract_scoped_injection_fixture::Target>(77),
        CTORIUM_NAMESPACE::BindOptions{.name = "session", .priority = 0});

    context.start();
    scope.start();

    auto consumer = context.resolve<contract_scoped_injection_fixture::NamedScopedValidationConsumer>();
    ASSERT_NE(consumer->target.operator->(), nullptr);
    EXPECT_EQ(consumer->target->value, 77);
    context.stop();
}

TEST(ContractScopedInjection, NamedScopedValidationRejectsNamedNonSessionCandidate) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext(
        "csi-named-scoped-validation-rejects");
    context.discover<^^contract_scoped_injection_fixture>();
    context.bindSingleton<contract_scoped_injection_fixture::Target>(
        std::make_unique<contract_scoped_injection_fixture::Target>(11),
        CTORIUM_NAMESPACE::BindOptions{.name = "singleton", .priority = 0});
    auto& scope = context.resolveScope("csi-target-scope-reject");
    scope.bindSession<contract_scoped_injection_fixture::Target>(
        std::make_unique<contract_scoped_injection_fixture::Target>(99));
    EXPECT_THROW(context.start(), CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractScopedInjection, MissingScopedQualifierOnRootConsumer_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("csi-missing-scoped");
    context.discover<^^contract_scoped_injection_missing_fixture>();
    EXPECT_THROW(context.start(), CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractScopedInjection, ScopedQualifierOnNonSessionTarget_Throws) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("csi-invalid-scoped-target");
    context.discover<^^contract_scoped_injection_invalid_fixture>();
    EXPECT_THROW(context.start(), CTORIUM_NAMESPACE::ConfigurationError);
    context.stop();
}

TEST(ContractScopedInjection, SessionConsumer_CanInjectSameScopeSessionWithoutScoped) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("csi-session-consumer");
    context.discover<^^contract_scoped_injection_fixture>().start();
    auto& scope = context.resolveScope("csi-session-consumer-scope");
    scope.start();
    auto consumer = scope.resolve<contract_scoped_injection_fixture::SameScopeConsumer>();
    auto dependency = scope.resolve<contract_scoped_injection_fixture::SessionObject>();
    ASSERT_NE(consumer.operator->(), nullptr);
    EXPECT_EQ(consumer->session.operator->(), dependency.operator->());
    context.stop();
}
