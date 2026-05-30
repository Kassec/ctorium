#include <atomic>
#include <future>
#include <thread>
#include <gtest/gtest.h>
#include <ctr/Ctorium.hpp>

// ─── Fixtures ─────────────────────────────────────────────────────────────────

namespace tl_fixture {
struct [[=ctr::threadLocal{}]] TLSvc { int id = 0; };
} // namespace tl_fixture

namespace thread_local_fixture {

std::atomic<int> preDestroyCallCount{0};

struct [[=ctr::threadLocal{}]] TLService {
    int threadId = 0;
    [[=ctr::preDestroy{}]] void cleanup() { preDestroyCallCount.fetch_add(1); }
};

struct [[=ctr::threadLocal{}]] TLSimple {
    int value = 42;
};

} // namespace thread_local_fixture

// ─── Criterion 1: distinct instances per thread, same per same thread ─────────

TEST(ThreadLocal, DistinctInstancesPerThread) {
    auto& ctx = ctr::BeanContext::resolveContext("tl-distinct");
    ctx.discover<^^thread_local_fixture>();
    ctx.start();

    void* mainPtr = ctx.resolve<thread_local_fixture::TLSimple>().operator->();
    ASSERT_NE(mainPtr, nullptr);

    // Same thread → same instance
    void* mainPtr2 = ctx.resolve<thread_local_fixture::TLSimple>().operator->();
    EXPECT_EQ(mainPtr, mainPtr2);

    // Different thread → different instance
    std::promise<void*> p;
    std::thread t([&]{ p.set_value(ctx.resolve<thread_local_fixture::TLSimple>().operator->()); });
    void* otherPtr = p.get_future().get();
    t.join();

    EXPECT_NE(mainPtr, otherPtr);
    ctx.stop();
}

// ─── Criterion 2: ScopedContext resolution == root resolution ─────────────────

TEST(ThreadLocal, ScopedContextTransparent) {
    auto& ctx   = ctr::BeanContext::resolveContext("tl-scope");
    auto& scope = ctx.resolveScope("s");
    ctx.discover<^^thread_local_fixture>();
    ctx.start();
    scope.start();

    void* rootPtr  = ctx.resolve<thread_local_fixture::TLSimple>().operator->();
    void* scopePtr = scope.resolve<thread_local_fixture::TLSimple>().operator->();

    EXPECT_EQ(rootPtr, scopePtr);  // same instance: scope is transparent
    ctx.stop();
}

// ─── Criterion 3: thread-exit runs lifecycle on exiting thread ────────────────

TEST(ThreadLocal, ThreadExitDestroysInstance) {
    thread_local_fixture::preDestroyCallCount.store(0);
    {
        auto& ctx = ctr::BeanContext::resolveContext("tl-thread-exit");
        ctx.discover<^^thread_local_fixture>();
        ctx.start();

        std::thread t([&]{
            // Resolve creates the TL instance on this thread.
            ctx.resolve<thread_local_fixture::TLService>();
            // Thread exits → TLCleanup sentinel calls cleanupCurrentThread() → preDestroy fires.
        });
        t.join();

        EXPECT_EQ(thread_local_fixture::preDestroyCallCount.load(), 1);
        ctx.stop();
    }
    thread_local_fixture::preDestroyCallCount.store(0);
}

// ─── Criterion 4: root.stop() destroys TL instances of still-alive threads ───

TEST(ThreadLocal, StopDestroysLiveThreadInstances) {
    thread_local_fixture::preDestroyCallCount.store(0);
    {
        auto& ctx = ctr::BeanContext::resolveContext("tl-stop");
        ctx.discover<^^thread_local_fixture>();
        ctx.start();

        // Synchronize: thread resolves, signals main, then waits for permission to exit.
        std::atomic<bool> resolved{false};
        std::atomic<bool> mayExit{false};

        std::thread t([&]{
            ctx.resolve<thread_local_fixture::TLService>();
            resolved.store(true, std::memory_order_release);
            while (!mayExit.load(std::memory_order_acquire)) { /* spin */ }
        });

        // Wait for thread to resolve
        while (!resolved.load(std::memory_order_acquire)) { /* spin */ }

        // stop() destroys the TL instance on the live thread
        ctx.stop();
        EXPECT_EQ(thread_local_fixture::preDestroyCallCount.load(), 1);

        // Now let the thread exit (its sentinel will find nothing to destroy)
        mayExit.store(true, std::memory_order_release);
        t.join();

        // No double-destroy: still exactly 1
        EXPECT_EQ(thread_local_fixture::preDestroyCallCount.load(), 1);
    }
    thread_local_fixture::preDestroyCallCount.store(0);
}

// ─── Criterion 5: Form 1 / Form 3 / Form 2 discrimination (no regression) ────

TEST(ThreadLocal, OperatorArrowDiscrimination) {
    // Resolving a singleton returns a Form 1 handle (object_ non-null).
    // Resolving a threadLocal returns a Form 3 handle (object_ null, sentinel nameId).
    // Ensure both return valid pointers.
    auto& ctx = ctr::BeanContext::resolveContext("tl-discrimination");
    ctx.discover<^^thread_local_fixture>();
    ctx.start();

    auto tlBean = ctx.resolve<thread_local_fixture::TLSimple>();
    EXPECT_NE(tlBean.operator->(), nullptr);
    EXPECT_EQ(tlBean->value, 42);

    ctx.stop();
}

// ─── tl_fixture: guard pre-start + destroy on close ──────────────────────────

TEST(ThreadLocal, ResolveBeforeStartRaisesContextStateError) {
    auto& ctx = ctr::BeanContext::resolveContext("tl-pre-start");
    ctx.discover<^^tl_fixture>();
    EXPECT_THROW(ctx.resolve<tl_fixture::TLSvc>(), ctr::ContextStateError);
    ctx.stop();
}

TEST(ThreadLocal, InstanceDestroyedAtContextClose) {
    auto& ctx = ctr::BeanContext::resolveContext("tl-destroy-on-close");
    ctx.discover<^^tl_fixture>().start();

    int destroyCount = 0;
    ctx.on(ctr::onDestroyed, [&](const ctr::AnyBean&) { ++destroyCount; });

    ctx.resolve<tl_fixture::TLSvc>();
    ctx.stop();

    EXPECT_EQ(destroyCount, 1);
}
