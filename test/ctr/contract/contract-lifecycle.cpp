#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>
#include <ctr/Registration.hpp>

namespace contract_lifecycle_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonObject {
    int value = 1;
};

struct [[=CTORIUM_NAMESPACE::prototype{}]] PrototypeObject {
    int value = 2;
};

struct [[=CTORIUM_NAMESPACE::session{}]] SessionObject {
    int value = 3;
};

struct [[=CTORIUM_NAMESPACE::threadLocal{}]] ThreadLocalObject {
    int value = 4;
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ThrowingConstructor {
    ThrowingConstructor() { throw std::runtime_error("constructor failure"); }
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] ThrowingPreDestroy {
    [[=CTORIUM_NAMESPACE::preDestroy{}]] void shutdown() { throw std::runtime_error("shutdown failure"); }
};

} // namespace contract_lifecycle_fixture

namespace contract_sources_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] AnnotatedSource {
    int value = 10;
};

struct FactoryProduct {
    int value = 20;
};

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]] FactoryProduct make() {
        return FactoryProduct{};
    }
};

struct RuntimeBoundSource {
    int value = 30;
};

} // namespace contract_sources_fixture

namespace contract_tl_exit_fixture {

inline std::atomic<int> preDestroyCount{0};
inline std::thread::id preDestroyThreadId{};
inline std::atomic<int> listenerPreDestroyCount{0};
inline std::atomic<int> listenerDestroyedCount{0};

struct [[=CTORIUM_NAMESPACE::threadLocal{}]] PreDestroyThreadLocal {
    [[=CTORIUM_NAMESPACE::preDestroy{}]] void shutdown() {
        preDestroyThreadId = std::this_thread::get_id();
        preDestroyCount.fetch_add(1, std::memory_order_relaxed);
    }
};

struct [[=CTORIUM_NAMESPACE::threadLocal{}]] ListenerThreadLocal {};

} // namespace contract_tl_exit_fixture

namespace contract_tl_stop_fixture {

inline std::atomic<int> preDestroyCount{0};
inline std::atomic<int> destroyedCount{0};

struct [[=CTORIUM_NAMESPACE::threadLocal{}]] StopThreadLocal {
    [[=CTORIUM_NAMESPACE::preDestroy{}]] void shutdown() {
        preDestroyCount.fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace contract_tl_stop_fixture

TEST(ContractLifecycle, RegistrationSources_UndiscoveredType_NotResolvable) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-src-undiscovered");
    context.start();
    EXPECT_THROW(
        context.resolve<contract_sources_fixture::AnnotatedSource>(),
        CTORIUM_NAMESPACE::ResolutionError);
    context.stop();
}

TEST(ContractLifecycle, RegistrationSources_DiscoveredType_Resolvable) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-src-discovered");
    context.discover<^^contract_sources_fixture>().start();

    auto bean = context.resolve<contract_sources_fixture::AnnotatedSource>();

    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 10);
    context.stop();
}

TEST(ContractLifecycle, RegistrationSources_FactoryProduct_Resolvable) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-src-factory-product");
    context.discover<^^contract_sources_fixture>().start();

    auto bean = context.resolve<contract_sources_fixture::FactoryProduct>();

    ASSERT_NE(bean.operator->(), nullptr);
    EXPECT_EQ(bean->value, 20);
    context.stop();
}

TEST(ContractLifecycle, RegistrationSources_RuntimeBinding_Resolvable) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-src-runtime-binding");
    auto source = std::make_unique<contract_sources_fixture::RuntimeBoundSource>();
    auto* expected = source.get();
    context.bindSingleton<contract_sources_fixture::RuntimeBoundSource>(std::move(source));
    context.start();

    auto bean = context.resolve<contract_sources_fixture::RuntimeBoundSource>();

    EXPECT_EQ(bean.operator->(), expected);
    EXPECT_EQ(bean->value, 30);
    context.stop();
}

TEST(ContractLifecycle, RegistrationSources_ImplicitContext_Resolvable) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-src-implicit-context");
    context.start();

    auto bean = context.resolve<CTORIUM_NAMESPACE::BeanContext>();

    EXPECT_EQ(bean.operator->(), &context);
    context.stop();
}

TEST(ContractLifecycle, ThreadLocal_DestroyedOnThreadExit_PreDestroyRunsOnExitingThread) {
    contract_tl_exit_fixture::preDestroyCount.store(0, std::memory_order_relaxed);
    contract_tl_exit_fixture::preDestroyThreadId = std::thread::id{};

    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-tl-exit-predestroy");
    context.discover<^^contract_tl_exit_fixture>().start();

    std::thread::id workerThreadId{};
    std::thread worker([&] {
        workerThreadId = std::this_thread::get_id();
        (void)context.resolve<contract_tl_exit_fixture::PreDestroyThreadLocal>();
    });
    worker.join();

    EXPECT_EQ(contract_tl_exit_fixture::preDestroyCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(contract_tl_exit_fixture::preDestroyThreadId, workerThreadId);
    context.stop();
}

TEST(ContractLifecycle, ThreadLocal_DestroyedOnThreadExit_ListenersFireOnExitingThread) {
    contract_tl_exit_fixture::listenerPreDestroyCount.store(0, std::memory_order_relaxed);
    contract_tl_exit_fixture::listenerDestroyedCount.store(0, std::memory_order_relaxed);

    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-tl-exit-listeners");
    context.discover<^^contract_tl_exit_fixture>().start();

    std::thread::id workerThreadId{};
    std::thread::id preDestroyThreadId{};
    std::thread::id destroyedThreadId{};

    context.on<contract_tl_exit_fixture::ListenerThreadLocal>(
        CTORIUM_NAMESPACE::onPreDestroy,
        [&](const CTORIUM_NAMESPACE::Bean<contract_tl_exit_fixture::ListenerThreadLocal>&) {
            preDestroyThreadId = std::this_thread::get_id();
            contract_tl_exit_fixture::listenerPreDestroyCount.fetch_add(1, std::memory_order_relaxed);
        });
    context.on<contract_tl_exit_fixture::ListenerThreadLocal>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<contract_tl_exit_fixture::ListenerThreadLocal>&) {
            destroyedThreadId = std::this_thread::get_id();
            contract_tl_exit_fixture::listenerDestroyedCount.fetch_add(1, std::memory_order_relaxed);
        });

    std::thread worker([&] {
        workerThreadId = std::this_thread::get_id();
        (void)context.resolve<contract_tl_exit_fixture::ListenerThreadLocal>();
    });
    worker.join();

    EXPECT_EQ(contract_tl_exit_fixture::listenerPreDestroyCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(contract_tl_exit_fixture::listenerDestroyedCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(preDestroyThreadId, workerThreadId);
    EXPECT_EQ(destroyedThreadId, workerThreadId);
    context.stop();
}

TEST(ContractLifecycle, ThreadLocal_RootStop_DestroysLivingThreadInstances) {
    contract_tl_stop_fixture::preDestroyCount.store(0, std::memory_order_relaxed);
    contract_tl_stop_fixture::destroyedCount.store(0, std::memory_order_relaxed);

    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-tl-stop-living");
    context.discover<^^contract_tl_stop_fixture>().start();
    context.on<contract_tl_stop_fixture::StopThreadLocal>(
        CTORIUM_NAMESPACE::onDestroyed,
        [](const CTORIUM_NAMESPACE::Bean<contract_tl_stop_fixture::StopThreadLocal>&) {
            contract_tl_stop_fixture::destroyedCount.fetch_add(1, std::memory_order_relaxed);
        });

    std::promise<void*> resolved;
    std::promise<void> mayExit;
    auto resolvedFuture = resolved.get_future();
    auto mayExitFuture = mayExit.get_future();
    std::thread worker([&] {
        auto bean = context.resolve<contract_tl_stop_fixture::StopThreadLocal>();
        resolved.set_value(bean.operator->());
        mayExitFuture.wait();
    });

    void* workerInstance = resolvedFuture.get();
    EXPECT_NE(workerInstance, nullptr);

    context.stop();

    EXPECT_EQ(contract_tl_stop_fixture::preDestroyCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(contract_tl_stop_fixture::destroyedCount.load(std::memory_order_relaxed), 1);

    mayExit.set_value();
    worker.join();
}

TEST(ContractLifecycle, ThreadLocal_RootStop_NoAccessAfterStop_Pattern) {
    contract_tl_stop_fixture::preDestroyCount.store(0, std::memory_order_relaxed);
    contract_tl_stop_fixture::destroyedCount.store(0, std::memory_order_relaxed);

    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-tl-stop-joined");
    context.discover<^^contract_tl_stop_fixture>().start();
    context.on<contract_tl_stop_fixture::StopThreadLocal>(
        CTORIUM_NAMESPACE::onDestroyed,
        [](const CTORIUM_NAMESPACE::Bean<contract_tl_stop_fixture::StopThreadLocal>&) {
            contract_tl_stop_fixture::destroyedCount.fetch_add(1, std::memory_order_relaxed);
        });

    std::thread worker([&] {
        (void)context.resolve<contract_tl_stop_fixture::StopThreadLocal>();
    });
    worker.join();

    EXPECT_EQ(contract_tl_stop_fixture::preDestroyCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(contract_tl_stop_fixture::destroyedCount.load(std::memory_order_relaxed), 1);

    context.stop();

    EXPECT_EQ(contract_tl_stop_fixture::preDestroyCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(contract_tl_stop_fixture::destroyedCount.load(std::memory_order_relaxed), 1);
}

TEST(ContractLifecycle, Singleton_OneInstancePerRootAndKey) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-singleton");
    context.discover<^^contract_lifecycle_fixture>().start();
    auto first = context.resolve<contract_lifecycle_fixture::SingletonObject>();
    auto second = context.resolve<contract_lifecycle_fixture::SingletonObject>();
    EXPECT_EQ(first.operator->(), second.operator->());
    context.stop();
}

TEST(ContractLifecycle, Prototype_OneInstancePerResolution) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-prototype");
    context.discover<^^contract_lifecycle_fixture>().start();
    auto first = context.resolve<contract_lifecycle_fixture::PrototypeObject>();
    auto second = context.resolve<contract_lifecycle_fixture::PrototypeObject>();
    EXPECT_NE(first.operator->(), second.operator->());
    context.stop();
}

TEST(ContractLifecycle, Session_OneInstancePerScopeCycle) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-session");
    context.discover<^^contract_lifecycle_fixture>().start();
    auto& scope = context.resolveScope("clc-session-scope");
    scope.start();
    auto first = scope.resolve<contract_lifecycle_fixture::SessionObject>();
    auto second = scope.resolve<contract_lifecycle_fixture::SessionObject>();
    auto* firstCycle = first.operator->();
    EXPECT_EQ(first.operator->(), second.operator->());
    scope.restart();
    auto third = scope.resolve<contract_lifecycle_fixture::SessionObject>();
    EXPECT_NE(third.operator->(), firstCycle);
    context.stop();
}

TEST(ContractLifecycle, ThreadLocal_OneInstancePerThread) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-thread-local");
    context.discover<^^contract_lifecycle_fixture>().start();
    auto mainThread = context.resolve<contract_lifecycle_fixture::ThreadLocalObject>();
    void* workerPointer = nullptr;
    std::thread worker([&] {
        auto workerBean = context.resolve<contract_lifecycle_fixture::ThreadLocalObject>();
        workerPointer = workerBean.operator->();
    });
    worker.join();
    EXPECT_NE(mainThread.operator->(), nullptr);
    EXPECT_NE(workerPointer, nullptr);
    EXPECT_NE(static_cast<void*>(mainThread.operator->()), workerPointer);
    context.stop();
}

TEST(ContractLifecycle, ThreadLocal_ResolveFromScopeUsesRootOwner) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-thread-local-scope");
    context.discover<^^contract_lifecycle_fixture>().start();
    auto& scope = context.resolveScope("clc-thread-local-scope-owner");
    scope.start();
    auto rootBean = context.resolve<contract_lifecycle_fixture::ThreadLocalObject>();
    auto scopeBean = scope.resolve<contract_lifecycle_fixture::ThreadLocalObject>();
    EXPECT_EQ(rootBean.operator->(), scopeBean.operator->());
    EXPECT_EQ(&scopeBean.context(), &context);
    context.stop();
}

TEST(ContractLifecycle, UserConstructorException_PropagatesUnwrapped) {
    auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-constructor-exception");
    context.discover<^^contract_lifecycle_fixture>().start();
    EXPECT_THROW(context.resolve<contract_lifecycle_fixture::ThrowingConstructor>(), std::runtime_error);
    context.stop();
}

#if GTEST_HAS_DEATH_TEST
TEST(ContractLifecycle, ShutdownThrowingPreDestroy_Death) {
    EXPECT_DEATH(
        {
            auto& context = CTORIUM_NAMESPACE::BeanContext::resolveContext("clc-shutdown-death");
            context.discover<^^contract_lifecycle_fixture>().start();
            (void)context.resolve<contract_lifecycle_fixture::ThrowingPreDestroy>();
            context.stop();
        },
        "");
}
#else
TEST(ContractLifecycle, DISABLED_ShutdownThrowingPreDestroy_Death) {}
#endif
