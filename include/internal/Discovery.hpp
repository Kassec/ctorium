#pragma once

#include <cstdint>
#include <meta>
#include <vector>

#include "../api/ctr/Config.hpp"

#include <ctr/Markers.hpp>

namespace CTORIUM_NAMESPACE::detail {

/**
 * @brief Classification of a compile-time entity discovered during enumeration.
 */
enum class EntityKind : std::uint8_t {
    /** A class/struct carrying at least one lifetime-marker annotation. */
    AnnotatedType,
    /** A class/struct carrying the [[=ctr::factory{}]] annotation. */
    Factory,
    /** A member method of a factory type carrying a lifetime-marker annotation. */
    FactoryProduct,
};

/**
 * @brief A single compile-time entity found by enumerateDiscovery().
 *
 * - AnnotatedType / Factory: @c entity is the type; @c declaringFactory is invalid (default).
 * - FactoryProduct: @c entity is the producer member function;
 *   @c declaringFactory is the enclosing factory type.
 */
struct DiscoveredEntity {
    /** Reflected type (AnnotatedType/Factory) or producer method (FactoryProduct). */
    std::meta::info entity;
    /** Classification of this entity. */
    EntityKind      kind;
    /** Factory type enclosing the producer method; invalid when kind != FactoryProduct. */
    std::meta::info declaringFactory;
};

/// @cond INTERNAL
/** Never defined. Calling it from a consteval body causes a compile-time diagnostic whose
 *  name conveys the constraint: every discover<> root must be a namespace or a class type. */
void ctoriumDiscoveryRequiresNamespaceOrClassType();
/// @endcond

namespace {

/** Returns true when @p entity carries an annotation whose non-const type equals @p markerType. */
consteval bool hasAnnotationOfType(std::meta::info entity, std::meta::info markerType) {
    for (auto annotation : std::meta::annotations_of(entity)) {
        if (std::meta::is_same_type(
                std::meta::remove_const(std::meta::type_of(annotation)), markerType)) {
            return true;
        }
    }
    return false;
}

/** Returns true when @p entity carries at least one lifetime-marker annotation. */
consteval bool hasLifetimeMarker(std::meta::info entity) {
    return hasAnnotationOfType(entity, ^^CTORIUM_NAMESPACE::singleton)
        || hasAnnotationOfType(entity, ^^CTORIUM_NAMESPACE::prototype)
        || hasAnnotationOfType(entity, ^^CTORIUM_NAMESPACE::session)
        || hasAnnotationOfType(entity, ^^CTORIUM_NAMESPACE::threadLocal);
}

/** Classifies a class/struct type and appends matching entities to @p result. */
consteval void classifyType(std::meta::info type, std::vector<DiscoveredEntity>& result) {
    if (hasAnnotationOfType(type, ^^CTORIUM_NAMESPACE::factory)) {
        // B7: a factory must not carry a lifetime marker (singleton/prototype/session/threadLocal).
        if (hasLifetimeMarker(type)) {
            throw "Ctorium: a type annotated with [[=ctr::factory{}]] must not "
                  "carry a lifetime marker (singleton/prototype/session/threadLocal) "
                  "— factory types are always singleton beans implicitly.";
        }
        result.push_back(DiscoveredEntity{type, EntityKind::Factory, std::meta::info{}});
        // Scan member methods for lifetime-marker annotations.
        // Guard: is_type() is safe for any reflection; is_special_member_function()
        // is only called after confirming the member is NOT a type (short-circuit &&).
        for (auto member : std::meta::members_of(type, std::meta::access_context::unchecked())) {
            if (!std::meta::is_type(member)
                    && !std::meta::is_special_member_function(member)
                    && hasLifetimeMarker(member)) {
                result.push_back(DiscoveredEntity{member, EntityKind::FactoryProduct, type});
            }
        }
    } else if (hasLifetimeMarker(type)) {
        result.push_back(DiscoveredEntity{type, EntityKind::AnnotatedType, std::meta::info{}});
    }
}

/**
 * Recursively processes @p root.
 *
 * - Namespace: iterates declared members depth-first; recurses into nested namespaces,
 *   classifies class types, ignores everything else.
 * - Class type: classifies directly.
 * - Anything else: triggers a compile-time error via ctoriumDiscoveryRequiresNamespaceOrClassType().
 *
 * Guard pattern: is_type() is called before is_class_type() because is_class_type()
 * throws in this P2996 implementation when the reflection is not a type entity.
 */
consteval void processRoot(std::meta::info root, std::vector<DiscoveredEntity>& result) {
    if (std::meta::is_namespace(root)) {
        for (auto member : std::meta::members_of(root, std::meta::access_context::unchecked())) {
            if (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
                processRoot(member, result);
            } else if (std::meta::is_type(member)
                    && std::meta::is_class_type(member)
                    && !std::meta::is_type_alias(member)) {
                classifyType(member, result);
            }
            // Other members (functions, variables, enums, aliases, …): ignore.
        }
    } else if (std::meta::is_type(root) && std::meta::is_class_type(root)) {
        classifyType(root, result);
    } else {
        // Root is not a namespace or class type — this is a compile-time programming error.
        ctoriumDiscoveryRequiresNamespaceOrClassType();
    }
}

} // anonymous namespace

/**
 * @brief Enumerates all annotated entities reachable from the given compile-time roots.
 *
 * Traversal is deterministic and stable: roots are visited in pack order; within a namespace,
 * members are visited in declaration order; nested namespaces are traversed depth-first before
 * sibling members.  Duplicate roots produce duplicate entries — no deduplication is performed.
 *
 * @tparam Roots Compile-time reflections of namespaces or class types. Any other kind triggers
 *               a compile-time error.
 * @return Ordered vector of discovered entities. Duplicates are preserved.
 */
template <auto... Roots>
consteval std::vector<DiscoveredEntity> enumerateDiscovery() {
    std::vector<DiscoveredEntity> result;
    (processRoot(Roots, result), ...);
    return result;
}

} // namespace CTORIUM_NAMESPACE::detail
