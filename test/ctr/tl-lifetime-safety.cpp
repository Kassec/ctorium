#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ctr/Ctorium.hpp>
#include <detail/ThreadLocalStore.hpp>

namespace tl_lifetime_m1_fixture {

inline constexpr int kDefaultSentinel = 17;
inline constexpr int kGhostSentinel = 0x5A17;

struct [[=ctr::threadLocal{}]] TLService {
    int sentinel = kDefaultSentinel;
};

} // namespace tl_lifetime_m1_fixture

namespace tl_lifetime_m2_fixture {

struct [[=ctr::threadLocal{}]] TLService {};

} // namespace tl_lifetime_m2_fixture

namespace {

using Registry = ctr::detail::Registry;

struct Probe : ctr::BeanContext {
    explicit Probe(std::string key)
        : ctr::BeanContext(std::move(key)) {}

    explicit Probe(std::shared_ptr<Registry> registry)
        : ctr::BeanContext(std::move(registry)) {}

    [[nodiscard]] Registry* registry() noexcept {
        return &core();
    }
};

class ControlledRegistryStorage {
public:
    [[nodiscard]] std::shared_ptr<Registry> construct() {
        auto* registry = ::new (static_cast<void*>(storage_)) Registry();
        live_ = true;
        return std::shared_ptr<Registry>(
            registry,
            [this](Registry* current) noexcept {
                current->~Registry();
                live_ = false;
            });
    }

    [[nodiscard]] bool live() const noexcept {
        return live_;
    }

private:
    alignas(Registry) std::byte storage_[sizeof(Registry)];
    bool live_ = false;
};

[[nodiscard]] std::uintptr_t addressOf(const Registry* registry) noexcept {
    return reinterpret_cast<std::uintptr_t>(registry);
}

[[nodiscard]] bool hasCallableThreadRegistrationFor(
        std::uint32_t registryId,
        std::uintptr_t registryAddress) {
    const auto& registered = ctr::detail::tlCleanup().registered;
    return std::any_of(
        registered.begin(), registered.end(),
        [registryId, registryAddress](const auto& entry) {
            return entry.first == registryId || addressOf(entry.second) == registryAddress;
        });
}

template <class T>
void collectAndDestroyThreadLocalStateFor(std::uintptr_t registryAddress) {
    auto& tl = ctr::detail::tlData();
    std::vector<void*> instances;

    for (const auto& key : tl.order) {
        if (addressOf(key.first) == registryAddress) {
            const auto it = tl.instances.find(key);
            if (it != tl.instances.end()) {
                instances.push_back(it->second);
                tl.instances.erase(it);
            }
        }
    }
    tl.order.erase(
        std::remove_if(
            tl.order.begin(), tl.order.end(),
            [registryAddress](const auto& key) {
                return addressOf(key.first) == registryAddress;
            }),
        tl.order.end());

    auto& cleanup = ctr::detail::tlCleanup();
    for (auto it = cleanup.registered.begin(); it != cleanup.registered.end();) {
        if (addressOf(it->second) == registryAddress) {
            it = cleanup.registered.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = instances.rbegin(); it != instances.rend(); ++it) {
        auto* instance = static_cast<T*>(*it);
        instance->~T();
        ::operator delete(instance, sizeof(T), std::align_val_t{alignof(T)});
    }
}

void releaseWorker(std::mutex& mutex, std::condition_variable& condition, bool& released) {
    {
        std::lock_guard lock(mutex);
        released = true;
    }
    condition.notify_one();
}

} // namespace

TEST(ThreadLocalLifetimeSafety, ReoccupiedRegistryAddressReturnsFreshThreadLocalInstance) {
    using TLService = tl_lifetime_m1_fixture::TLService;

    ControlledRegistryStorage storage;
    Registry* firstRegistry = nullptr;
    std::uintptr_t firstRegistryAddress = 0;
    std::uint32_t firstRegistryId = 0;
    TLService* firstInstance = nullptr;

    {
        auto registry = storage.construct();
        firstRegistry = registry.get();
        firstRegistryAddress = addressOf(firstRegistry);
        firstRegistryId = firstRegistry->registryId();

        Probe ctx(std::move(registry));
        ctx.discover<^^tl_lifetime_m1_fixture>().start();

        auto bean = ctx.resolve<TLService>();
        firstInstance = bean.operator->();
        ASSERT_NE(firstInstance, nullptr);
        firstInstance->sentinel = tl_lifetime_m1_fixture::kGhostSentinel;
    }

    ASSERT_FALSE(storage.live());

    Registry* secondRegistry = nullptr;
    TLService* secondInstance = nullptr;
    int secondSentinel = 0;

    {
        auto registry = storage.construct();
        secondRegistry = registry.get();
        EXPECT_EQ(addressOf(secondRegistry), firstRegistryAddress);
        EXPECT_NE(secondRegistry->registryId(), firstRegistryId);

        Probe ctx(std::move(registry));
        ctx.discover<^^tl_lifetime_m1_fixture>().start();

        auto bean = ctx.resolve<TLService>();
        secondInstance = bean.operator->();
        ASSERT_NE(secondInstance, nullptr);
        secondSentinel = secondInstance->sentinel;

        EXPECT_EQ(secondSentinel, tl_lifetime_m1_fixture::kDefaultSentinel);
        EXPECT_NE(secondInstance, firstInstance);
    }

    ASSERT_FALSE(storage.live());

    collectAndDestroyThreadLocalStateFor<TLService>(firstRegistryAddress);
}

TEST(ThreadLocalLifetimeSafety, RegistryDestroyedWithoutStopLeavesNoCallableThreadRegistration) {
    using TLService = tl_lifetime_m2_fixture::TLService;

    ControlledRegistryStorage storage;
    std::mutex mutex;
    std::condition_variable condition;
    bool resolved = false;
    bool mayCheck = false;
    bool hasResidualRegistration = true;
    void* workerInstance = nullptr;
    std::exception_ptr workerException;
    std::thread worker;
    std::uintptr_t registryAddress = 0;
    std::uint32_t registryId = 0;

    {
        auto registry = storage.construct();
        registryAddress = addressOf(registry.get());
        registryId = registry->registryId();

        Probe ctx(std::move(registry));
        ctx.discover<^^tl_lifetime_m2_fixture>().start();

        worker = std::thread([&] {
            try {
                {
                    auto bean = ctx.resolve<TLService>();
                    workerInstance = bean.operator->();
                }

                {
                    std::lock_guard lock(mutex);
                    resolved = true;
                }
                condition.notify_one();

                {
                    std::unique_lock lock(mutex);
                    condition.wait(lock, [&] { return mayCheck; });
                }

                hasResidualRegistration =
                    hasCallableThreadRegistrationFor(registryId, registryAddress);
                collectAndDestroyThreadLocalStateFor<TLService>(registryAddress);
            } catch (...) {
                workerException = std::current_exception();
                {
                    std::lock_guard lock(mutex);
                    resolved = true;
                }
                condition.notify_one();
            }
        });

        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [&] { return resolved; });
        }

        if (workerInstance == nullptr) {
            releaseWorker(mutex, condition, mayCheck);
            worker.join();
            if (workerException != nullptr) {
                std::rethrow_exception(workerException);
            }
            FAIL() << "thread-local resolution returned null in the worker thread";
        }
    }

    ASSERT_FALSE(storage.live());
    releaseWorker(mutex, condition, mayCheck);
    worker.join();

    if (workerException != nullptr) {
        std::rethrow_exception(workerException);
    }

    EXPECT_FALSE(hasResidualRegistration);
}
