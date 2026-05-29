#pragma once

#ifndef CTORIUM_DYNAMIC_LINK

#include <shared_mutex>
#include "Registry.hpp"
#include "BeanDescriptorGen.hpp"
#include "HashUtils.hpp"
#include "../../api/ctr/BeanContext.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/ScopedContext.hpp"

namespace ctr {

// -------------------------------------------------------------------------
// Process-wide root registry
// -------------------------------------------------------------------------

// Heterogeneous map: resolveContext(string_view) performs no heap allocation on hit.
inline std::unordered_map<std::string, std::unique_ptr<BeanContext>,
                           detail::StringViewHash, std::equal_to<>> g_roots;
inline std::mutex g_rootsMutex;

// -------------------------------------------------------------------------
// BeanContext constructors and destructor
// -------------------------------------------------------------------------

inline BeanContext::BeanContext(std::string key)
    : registry_(std::make_shared<ctr::detail::Registry>()), key_(std::move(key)) {}

inline BeanContext::BeanContext(std::shared_ptr<ctr::detail::Registry> registry)
    : registry_(std::move(registry)) {}

inline BeanContext::~BeanContext() = default;

// -------------------------------------------------------------------------
// BeanContext::assertCanDiscover_()
// -------------------------------------------------------------------------

inline void BeanContext::assertCanDiscover_() const {
    if (registry_->started()) {
        throw ContextStateError(
            "discover<>() called after start() — contributions must be "
            "submitted before the context is started.");
    }
}

// -------------------------------------------------------------------------
// BeanContext::resolveContext()
// -------------------------------------------------------------------------

inline BeanContext& BeanContext::resolveContext() {
    return resolveContext("");
}

inline BeanContext& BeanContext::resolveContext(std::string_view key) {
    std::lock_guard lock(g_rootsMutex);
    // Transparent find: no std::string allocated on hit.
    auto it = g_roots.find(key);
    if (it != g_roots.end()) return *it->second;
    auto [jt, _] = g_roots.emplace(std::string(key),
        std::unique_ptr<BeanContext>(new BeanContext(std::string(key))));
    return *jt->second;
}

// -------------------------------------------------------------------------
// BeanContext lifecycle
// -------------------------------------------------------------------------

inline BeanContext& BeanContext::start() {
    core().start(this);
    return *this;
}

inline BeanContext& BeanContext::stop() {
    core().stop();
    return *this;
}

inline void BeanContext::close() {
    core().stop();
    std::lock_guard lock(g_rootsMutex);
    g_roots.erase(key_);
}

// -------------------------------------------------------------------------
// BeanContext::resolveScope()
// -------------------------------------------------------------------------

inline ScopedContext& BeanContext::resolveScope(std::string_view key) {
    // Fast path: existing scope — check under lock without constructing a ScopedContext.
    {
        std::shared_lock<std::shared_mutex> lock(scopesMutex_);
        const auto it = scopes_.find(key);
        if (it != scopes_.end()) return *it->second;
    }
    // Slow path: construct outside the lock so the shared_ptr refcount increment
    // (inside ScopedContext's constructor) does not extend the critical section.
    auto candidate = std::unique_ptr<ScopedContext>(new ScopedContext(registry_, this));
    std::unique_lock<std::shared_mutex> lock(scopesMutex_);
    // try_emplace: does NOT move candidate if key already exists
    // (concurrent thread may have created the scope between the two lock acquisitions).
    auto [it, inserted] = scopes_.try_emplace(std::string(key), std::move(candidate));
    return *it->second;
}

// -------------------------------------------------------------------------
// ScopedContext
// -------------------------------------------------------------------------

inline ScopedContext::ScopedContext(std::shared_ptr<ctr::detail::Registry> registry,
                                    BeanContext* root)
    : BeanContext(std::move(registry)), root_(root) {}

inline ScopedContext& ScopedContext::resolveScope(std::string_view key) {
    return root_->resolveScope(key);
}

// -------------------------------------------------------------------------
// detail::submitDiscoveryContribution — §8
// -------------------------------------------------------------------------

namespace detail {

template <auto... Roots>
inline void submitDiscoveryContribution(BeanContext& context, DiscoverOptions options) {
    context.assertCanDiscover_();
    static constexpr auto kDescriptors =
        std::define_static_array(makeAllDescriptors<Roots...>());
    context.core().submitContribution(
        std::span<const ContributedDescriptor>(kDescriptors.data(), kDescriptors.size()),
        options);
}

} // namespace detail

} // namespace ctr

#endif // CTORIUM_DYNAMIC_LINK
