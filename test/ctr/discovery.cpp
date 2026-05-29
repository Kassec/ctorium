#include <string_view>
#include <gtest/gtest.h>

#include "Discovery.hpp"

// --- Test fixtures -------------------------------------------------------

namespace discovery_fixture {

namespace inner {
struct [[=ctr::session{}]] SessionBean {};
} // namespace inner

struct [[=ctr::singleton{}]] SingletonBean {};
struct [[=ctr::prototype{}]] PrototypeBean {};
struct PlainBean {}; // No annotation — must be ignored.

struct [[=ctr::factory{}]] BeanFactory {
    [[=ctr::singleton{}]] SingletonBean* makeSingleton();
    void helperMethod(); // No lifetime marker — must be ignored.
};

} // namespace discovery_fixture

// --- (a) Namespace root with nested-namespace recursion ------------------
// Expected order: inner::SessionBean(AT), SingletonBean(AT), PrototypeBean(AT),
//                 BeanFactory(F), BeanFactory::makeSingleton(FP)  → 5 entities.
consteval bool testNamespaceRoot() {
    auto result = ctr::detail::enumerateDiscovery<^^discovery_fixture>();

    if (result.size() != 5) return false;

    if (result[0].kind != ctr::detail::EntityKind::AnnotatedType) return false;
    if (result[0].entity != ^^discovery_fixture::inner::SessionBean) return false;

    if (result[1].kind != ctr::detail::EntityKind::AnnotatedType) return false;
    if (result[1].entity != ^^discovery_fixture::SingletonBean) return false;

    if (result[2].kind != ctr::detail::EntityKind::AnnotatedType) return false;
    if (result[2].entity != ^^discovery_fixture::PrototypeBean) return false;

    if (result[3].kind != ctr::detail::EntityKind::Factory) return false;
    if (result[3].entity != ^^discovery_fixture::BeanFactory) return false;

    if (result[4].kind != ctr::detail::EntityKind::FactoryProduct) return false;
    if (result[4].declaringFactory != ^^discovery_fixture::BeanFactory) return false;
    if (std::string_view(std::meta::identifier_of(result[4].entity)) != "makeSingleton") return false;

    return true;
}
TEST(EnumerateDiscovery, NamespaceRootWithNestedNsRecursion) { static_assert(testNamespaceRoot()); }

// --- (b) Single class root (factory type) --------------------------------
// Expected: [BeanFactory(F), makeSingleton(FP)]  → 2 entities.
consteval bool testClassRoot() {
    auto result = ctr::detail::enumerateDiscovery<^^discovery_fixture::BeanFactory>();

    if (result.size() != 2) return false;

    if (result[0].kind != ctr::detail::EntityKind::Factory) return false;
    if (result[0].entity != ^^discovery_fixture::BeanFactory) return false;

    if (result[1].kind != ctr::detail::EntityKind::FactoryProduct) return false;
    if (result[1].declaringFactory != ^^discovery_fixture::BeanFactory) return false;
    if (std::string_view(std::meta::identifier_of(result[1].entity)) != "makeSingleton") return false;

    return true;
}
TEST(EnumerateDiscovery, SingleClassFactoryRoot) { static_assert(testClassRoot()); }

// --- (c) Overlapping roots — duplicates preserved, no deduplication ------
// Roots: ^^discovery_fixture (5 entities) + ^^discovery_fixture::BeanFactory (2 entities) = 7.
// BeanFactory and its product each appear twice — deduplication is NOT performed.
consteval bool testOverlappingRoots() {
    auto result = ctr::detail::enumerateDiscovery<^^discovery_fixture, ^^discovery_fixture::BeanFactory>();

    if (result.size() != 7) return false;

    // First 5 come from ^^discovery_fixture (already validated above; spot-check here).
    if (result[3].kind != ctr::detail::EntityKind::Factory) return false;
    if (result[3].entity != ^^discovery_fixture::BeanFactory) return false;

    // Entries 5-6 are the duplicate from ^^discovery_fixture::BeanFactory.
    if (result[5].kind != ctr::detail::EntityKind::Factory) return false;
    if (result[5].entity != ^^discovery_fixture::BeanFactory) return false;

    if (result[6].kind != ctr::detail::EntityKind::FactoryProduct) return false;
    if (result[6].declaringFactory != ^^discovery_fixture::BeanFactory) return false;

    // BeanFactory must appear at both index 3 and index 5 (no dedup).
    if (result[3].entity != result[5].entity) return false;

    return true;
}
TEST(EnumerateDiscovery, OverlappingRootsPreserveDuplicates) { static_assert(testOverlappingRoots()); }
