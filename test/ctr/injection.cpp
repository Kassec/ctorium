#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <gtest/gtest.h>
#include <meta>
#include <utility>

#include <ctr/Registration.hpp>

namespace injection_lazy_cycle_fixture {

struct B;

struct [[=CTORIUM_NAMESPACE::prototype{}]] A {
    CTORIUM_NAMESPACE::Bean<B> b;
    explicit A(CTORIUM_NAMESPACE::Bean<B> dep) : b(std::move(dep)) {}
};

struct [[=CTORIUM_NAMESPACE::prototype{}]] B {
    CTORIUM_NAMESPACE::Bean<A> a;
    explicit B(CTORIUM_NAMESPACE::Bean<A> dep) : a(std::move(dep)) {}
};

} // namespace injection_lazy_cycle_fixture

namespace injection_self_cycle_fixture {

struct [[=CTORIUM_NAMESPACE::prototype{}]] S {
    CTORIUM_NAMESPACE::Bean<S> self;
    explicit S(CTORIUM_NAMESPACE::Bean<S> dep) : self(std::move(dep)) {}
};

} // namespace injection_self_cycle_fixture

namespace injection_eager_cycle_fixture {

struct B;

struct [[=CTORIUM_NAMESPACE::singleton{.lazy = false}]] A {
    CTORIUM_NAMESPACE::Bean<B> b;
    explicit A(CTORIUM_NAMESPACE::Bean<B> dep) : b(std::move(dep)) {}
};

struct [[=CTORIUM_NAMESPACE::singleton{.lazy = false}]] B {
    CTORIUM_NAMESPACE::Bean<A> a;
    explicit B(CTORIUM_NAMESPACE::Bean<A> dep) : a(std::move(dep)) {}
};

} // namespace injection_eager_cycle_fixture

namespace injection_multi_dep_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Dep1 { int value = 1; };
struct [[=CTORIUM_NAMESPACE::singleton{}]] Dep2 { int value = 2; };
struct [[=CTORIUM_NAMESPACE::singleton{}]] Dep3 { int value = 3; };

struct [[=CTORIUM_NAMESPACE::prototype{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<Dep1> dep1;
    CTORIUM_NAMESPACE::Bean<Dep2> dep2;
    CTORIUM_NAMESPACE::Bean<Dep3> dep3;

    Consumer(CTORIUM_NAMESPACE::Bean<Dep1> a, CTORIUM_NAMESPACE::Bean<Dep2> b, CTORIUM_NAMESPACE::Bean<Dep3> c)
        : dep1(std::move(a)), dep2(std::move(b)), dep3(std::move(c)) {}
};

} // namespace injection_multi_dep_fixture

namespace injection_proto_singleton_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonDep {};

struct [[=CTORIUM_NAMESPACE::prototype{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<SingletonDep> dep;
    explicit Consumer(CTORIUM_NAMESPACE::Bean<SingletonDep> d) : dep(std::move(d)) {}
};

} // namespace injection_proto_singleton_fixture

namespace injection_singleton_proto_fixture {

struct [[=CTORIUM_NAMESPACE::prototype{}]] PrototypeDep {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<PrototypeDep> dep;
    explicit Consumer(CTORIUM_NAMESPACE::Bean<PrototypeDep> d) : dep(std::move(d)) {}
};

} // namespace injection_singleton_proto_fixture

namespace injection_session_singleton_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonDep {};

struct [[=CTORIUM_NAMESPACE::session{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<SingletonDep> dep;
    explicit Consumer(CTORIUM_NAMESPACE::Bean<SingletonDep> d) : dep(std::move(d)) {}
};

} // namespace injection_session_singleton_fixture

namespace injection_thread_local_singleton_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonDep {};

struct [[=CTORIUM_NAMESPACE::threadLocal{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<SingletonDep> dep;
    explicit Consumer(CTORIUM_NAMESPACE::Bean<SingletonDep> d) : dep(std::move(d)) {}
};

} // namespace injection_thread_local_singleton_fixture

namespace injection_factory_singleton_fixture {

struct ProductTag {};

template<typename>
struct Product {};

using FactoryProduct = Product<ProductTag>;

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    FactoryProduct make() { return FactoryProduct{}; }
};

struct [[=CTORIUM_NAMESPACE::prototype{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<FactoryProduct> dep;
    explicit Consumer(CTORIUM_NAMESPACE::Bean<FactoryProduct> d) : dep(std::move(d)) {}
};

} // namespace injection_factory_singleton_fixture

namespace injection_post_construct_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Dep {};

struct [[=CTORIUM_NAMESPACE::singleton{}]] Target {
    Dep* received = nullptr;
    int callCount = 0;

    [[=CTORIUM_NAMESPACE::postConstruct{}]]
    void init(CTORIUM_NAMESPACE::Bean<Dep> dep) {
        received = dep.operator->();
        ++callCount;
    }
};

} // namespace injection_post_construct_fixture

namespace injection_pre_destroy_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] SingletonDep {};

struct [[=CTORIUM_NAMESPACE::prototype{}]] Target {
    SingletonDep* received = nullptr;
    int callCount = 0;

    [[=CTORIUM_NAMESPACE::preDestroy{}]]
    void cleanup(CTORIUM_NAMESPACE::Bean<SingletonDep> dep) {
        received = dep.operator->();
        ++callCount;
    }
};

} // namespace injection_pre_destroy_fixture

namespace injection_named_param_fixture {

struct DepTag {};

template<typename>
struct Dep {
    int value = 0;
};

using ExposedDep = Dep<DepTag>;

struct [[=CTORIUM_NAMESPACE::factory{}]] Factory {
    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("a")}]]
    ExposedDep makeA() {
        ExposedDep dep;
        dep.value = 1;
        return dep;
    }

    [[=CTORIUM_NAMESPACE::singleton{}]]
    [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("b")}]]
    ExposedDep makeB() {
        ExposedDep dep;
        dep.value = 2;
        return dep;
    }
};

struct [[=CTORIUM_NAMESPACE::prototype{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<ExposedDep> dep;
    explicit Consumer(
        [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("a")}]]
        CTORIUM_NAMESPACE::Bean<ExposedDep> d) : dep(std::move(d)) {}
};

} // namespace injection_named_param_fixture

namespace injection_missing_dep_fixture {

struct MissingDep {};

struct [[=CTORIUM_NAMESPACE::prototype{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<MissingDep> dep;
    explicit Consumer(CTORIUM_NAMESPACE::Bean<MissingDep> d) : dep(std::move(d)) {}
};

} // namespace injection_missing_dep_fixture

namespace injection_unknown_named_fixture {

struct [[=CTORIUM_NAMESPACE::singleton{}]] Dep {};

struct [[=CTORIUM_NAMESPACE::prototype{}]] Consumer {
    CTORIUM_NAMESPACE::Bean<Dep> dep;
    explicit Consumer(
        [[=CTORIUM_NAMESPACE::named{.name = std::define_static_string("absent")}]]
        CTORIUM_NAMESPACE::Bean<Dep> d) : dep(std::move(d)) {}
};

} // namespace injection_unknown_named_fixture

TEST(Injection, LazyConstructorCycleRaisesResolutionError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-lazy-cycle");
    ctx.discover<^^injection_lazy_cycle_fixture>().start();
    EXPECT_THROW(ctx.resolve<injection_lazy_cycle_fixture::A>(), CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

TEST(Injection, SelfConstructorCycleRaisesResolutionError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-self-cycle");
    ctx.discover<^^injection_self_cycle_fixture>().start();
    EXPECT_THROW(ctx.resolve<injection_self_cycle_fixture::S>(), CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

TEST(Injection, EagerSingletonCycleRaisesResolutionErrorAtStart) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-eager-cycle");
    ctx.discover<^^injection_eager_cycle_fixture>();
    EXPECT_THROW(ctx.start(), CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

TEST(Injection, MultipleConstructorDependenciesEachResolvedDistinctly) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-multi-dep");
    ctx.discover<^^injection_multi_dep_fixture>().start();

    {
        auto consumer = ctx.resolve<injection_multi_dep_fixture::Consumer>();
        auto dep1 = ctx.resolve<injection_multi_dep_fixture::Dep1>();
        auto dep2 = ctx.resolve<injection_multi_dep_fixture::Dep2>();
        auto dep3 = ctx.resolve<injection_multi_dep_fixture::Dep3>();

        EXPECT_EQ(consumer->dep1.operator->(), dep1.operator->());
        EXPECT_EQ(consumer->dep2.operator->(), dep2.operator->());
        EXPECT_EQ(consumer->dep3.operator->(), dep3.operator->());
        EXPECT_NE(consumer->dep1.operator->(), nullptr);
        EXPECT_NE(consumer->dep2.operator->(), nullptr);
        EXPECT_NE(consumer->dep3.operator->(), nullptr);
        EXPECT_NE(static_cast<void*>(consumer->dep1.operator->()), static_cast<void*>(consumer->dep2.operator->()));
        EXPECT_NE(static_cast<void*>(consumer->dep1.operator->()), static_cast<void*>(consumer->dep3.operator->()));
        EXPECT_NE(static_cast<void*>(consumer->dep2.operator->()), static_cast<void*>(consumer->dep3.operator->()));
        EXPECT_EQ(consumer->dep1->value, 1);
        EXPECT_EQ(consumer->dep2->value, 2);
        EXPECT_EQ(consumer->dep3->value, 3);
    }

    ctx.stop();
}

TEST(Injection, PrototypeConsumerSharesSingletonDependency) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-proto-singleton");
    ctx.discover<^^injection_proto_singleton_fixture>().start();

    {
        auto dep = ctx.resolve<injection_proto_singleton_fixture::SingletonDep>();
        auto c1 = ctx.resolve<injection_proto_singleton_fixture::Consumer>();
        auto c2 = ctx.resolve<injection_proto_singleton_fixture::Consumer>();

        EXPECT_NE(c1.operator->(), c2.operator->());
        EXPECT_EQ(c1->dep.operator->(), dep.operator->());
        EXPECT_EQ(c2->dep.operator->(), dep.operator->());
    }

    ctx.stop();
}

TEST(Injection, SingletonConsumerHoldsPrototypeDependency) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-singleton-proto");
    ctx.discover<^^injection_singleton_proto_fixture>().start();

    {
        auto c1 = ctx.resolve<injection_singleton_proto_fixture::Consumer>();
        auto c2 = ctx.resolve<injection_singleton_proto_fixture::Consumer>();
        auto freshDep = ctx.resolve<injection_singleton_proto_fixture::PrototypeDep>();

        EXPECT_EQ(c1.operator->(), c2.operator->());
        EXPECT_NE(c1->dep.operator->(), nullptr);
        EXPECT_NE(c1->dep.operator->(), freshDep.operator->());
    }

    ctx.stop();
}

TEST(Injection, SessionConsumerReceivesSingletonDependency) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-session-singleton");
    ctx.discover<^^injection_session_singleton_fixture>().start();
    auto& scope = ctx.resolveScope("inj-session-singleton-scope");
    scope.start();

    {
        auto dep = ctx.resolve<injection_session_singleton_fixture::SingletonDep>();
        auto consumer = scope.resolve<injection_session_singleton_fixture::Consumer>();
        EXPECT_EQ(consumer->dep.operator->(), dep.operator->());
    }

    ctx.stop();
}

TEST(Injection, ThreadLocalConsumerReceivesSingletonDependency) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-thread-local-singleton");
    ctx.discover<^^injection_thread_local_singleton_fixture>().start();

    {
        auto dep = ctx.resolve<injection_thread_local_singleton_fixture::SingletonDep>();
        auto consumer = ctx.resolve<injection_thread_local_singleton_fixture::Consumer>();
        EXPECT_EQ(consumer->dep.operator->(), dep.operator->());
    }

    ctx.stop();
}

TEST(Injection, ConsumerReceivesFactoryProducedSingletonDependency) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-factory-singleton");
    ctx.discover<^^injection_factory_singleton_fixture>().start();

    {
        auto product = ctx.resolve<injection_factory_singleton_fixture::FactoryProduct>();
        auto consumer = ctx.resolve<injection_factory_singleton_fixture::Consumer>();
        EXPECT_EQ(consumer->dep.operator->(), product.operator->());
    }

    ctx.stop();
}

TEST(Injection, PostConstructHookReceivesInjectedDependency) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-post-construct-param");
    ctx.discover<^^injection_post_construct_fixture>().start();

    {
        auto target1 = ctx.resolve<injection_post_construct_fixture::Target>();
        auto dep = ctx.resolve<injection_post_construct_fixture::Dep>();
        auto target2 = ctx.resolve<injection_post_construct_fixture::Target>();

        EXPECT_EQ(target1.operator->(), target2.operator->());
        EXPECT_EQ(target1->received, dep.operator->());
        EXPECT_EQ(target1->callCount, 1);
    }

    ctx.stop();
}

TEST(Injection, PreDestroyHookReceivesInjectedSingletonDependency) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-pre-destroy-param");
    ctx.discover<^^injection_pre_destroy_fixture>().start();

    injection_pre_destroy_fixture::SingletonDep* expected = nullptr;
    injection_pre_destroy_fixture::SingletonDep* received = nullptr;
    int hookCallCount = 0;
    int destroyedCount = 0;

    ctx.on<injection_pre_destroy_fixture::Target>(
        CTORIUM_NAMESPACE::onDestroyed,
        [&](const CTORIUM_NAMESPACE::Bean<injection_pre_destroy_fixture::Target>& bean) {
            received = bean->received;
            hookCallCount = bean->callCount;
            ++destroyedCount;
        });

    {
        auto singleton = ctx.resolve<injection_pre_destroy_fixture::SingletonDep>();
        expected = singleton.operator->();

        {
            auto handle = ctx.resolve<injection_pre_destroy_fixture::Target>();
            EXPECT_NE(handle.operator->(), nullptr);
        }

        EXPECT_EQ(received, expected);
        EXPECT_EQ(hookCallCount, 1);
        EXPECT_EQ(destroyedCount, 1);
    }

    ctx.stop();
}

TEST(Injection, NamedConstructorParameterSelectsNamedCandidate) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-named-param");
    ctx.discover<^^injection_named_param_fixture>().start();

    {
        auto consumer = ctx.resolve<injection_named_param_fixture::Consumer>();
        auto depA = ctx.resolve<injection_named_param_fixture::ExposedDep>(CTORIUM_NAMESPACE::named{"a"});
        auto depB = ctx.resolve<injection_named_param_fixture::ExposedDep>(CTORIUM_NAMESPACE::named{"b"});

        EXPECT_EQ(consumer->dep.operator->(), depA.operator->());
        EXPECT_NE(consumer->dep.operator->(), depB.operator->());
        EXPECT_EQ(consumer->dep->value, 1);
        EXPECT_EQ(depB->value, 2);
    }

    ctx.stop();
}

TEST(Injection, MissingDependencyRaisesResolutionError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-missing-dep");
    ctx.discover<^^injection_missing_dep_fixture>().start();
    EXPECT_THROW(ctx.resolve<injection_missing_dep_fixture::Consumer>(), CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

TEST(Injection, UnknownNamedQualifierRaisesResolutionError) {
    auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext("inj-unknown-named");
    ctx.discover<^^injection_unknown_named_fixture>().start();
    EXPECT_THROW(ctx.resolve<injection_unknown_named_fixture::Consumer>(), CTORIUM_NAMESPACE::ResolutionError);
    ctx.stop();
}

// ─── Risque 2 : cycle singleton concurrent — deadlock sans détection cross-thread ───
//
// Deux singletons lazy en dépendance mutuelle : A injecte B, B injecte A.
// Depuis deux threads rendez-vous, chacun déclenche la matérialisation d'un côté
// du cycle. Sans détection cross-thread la cv_ crée un deadlock.
// Le test attendu vert (après correctif) : au moins un resolve() lève ResolutionError.

namespace injection_concurrent_singleton_cycle_fixture {

struct B;

struct [[=CTORIUM_NAMESPACE::singleton{}]] A {
    CTORIUM_NAMESPACE::Bean<B> b;
    explicit A(CTORIUM_NAMESPACE::Bean<B> dep) : b(std::move(dep)) {}
};

struct [[=CTORIUM_NAMESPACE::singleton{}]] B {
    CTORIUM_NAMESPACE::Bean<A> a;
    explicit B(CTORIUM_NAMESPACE::Bean<A> dep) : a(std::move(dep)) {}
};

} // namespace injection_concurrent_singleton_cycle_fixture

TEST(Injection, ConcurrentSingletonCycleCausesMutualWait) {
    using namespace std::chrono_literals;
    constexpr int kAttempts = 30;
    constexpr auto kWatchdog = std::chrono::seconds{2};

    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        std::string ctxKey = "inj-conc-cycle-" + std::to_string(attempt);
        auto& ctx = CTORIUM_NAMESPACE::BeanContext::resolveContext(ctxKey);
        ctx.discover<^^injection_concurrent_singleton_cycle_fixture>().start();

        std::atomic<bool> aThrew{false}, bThrew{false};
        std::atomic<bool> doneA{false}, doneB{false};
        std::atomic<int> rendezvous{0};

        auto taskA = [&] {
            rendezvous.fetch_add(1, std::memory_order_release);
            while (rendezvous.load(std::memory_order_acquire) < 2) {}
            try {
                ctx.resolve<injection_concurrent_singleton_cycle_fixture::A>();
            } catch (const CTORIUM_NAMESPACE::ResolutionError&) {
                aThrew.store(true, std::memory_order_release);
            } catch (...) {}
            doneA.store(true, std::memory_order_release);
        };

        auto taskB = [&] {
            rendezvous.fetch_add(1, std::memory_order_release);
            while (rendezvous.load(std::memory_order_acquire) < 2) {}
            try {
                ctx.resolve<injection_concurrent_singleton_cycle_fixture::B>();
            } catch (const CTORIUM_NAMESPACE::ResolutionError&) {
                bThrew.store(true, std::memory_order_release);
            } catch (...) {}
            doneB.store(true, std::memory_order_release);
        };

        std::thread t1{taskA};
        std::thread t2{taskB};

        auto deadline = std::chrono::steady_clock::now() + kWatchdog;
        bool timedOut = false;
        while (!(doneA.load(std::memory_order_acquire) &&
                 doneB.load(std::memory_order_acquire))) {
            if (std::chrono::steady_clock::now() > deadline) {
                timedOut = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }

        if (timedOut) {
            t1.detach();
            t2.detach();
            // Ne pas appeler ctx.stop() : writeLock_ peut être détenu par un thread bloqué.
            FAIL() << "Deadlock détecté à l'itération " << attempt
                   << " : cycle singleton concurrent non résolu sous "
                   << kWatchdog.count() << "s "
                   << "(détection cross-thread non implémentée).";
            return;
        }

        t1.join();
        t2.join();

        EXPECT_TRUE(aThrew.load() || bThrew.load())
            << "Itération " << attempt
            << " : au moins un resolve() devrait lever ctr::ResolutionError "
               "(cycle cross-thread non détecté).";

        ctx.stop();
    }
}
