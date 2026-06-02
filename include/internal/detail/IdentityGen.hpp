#pragma once

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <vector>

#include "../../api/ctr/Config.hpp"

#include "../Identity.hpp"
#include "../Lifetime.hpp"
#include "../../api/ctr/Markers.hpp"

namespace CTORIUM_NAMESPACE::detail {
// ─────────────────────────────────────────────────────────────────────────────
// FNV-1a 64-bit — consteval
// ─────────────────────────────────────────────────────────────────────────────

consteval Identity fnv1a64(
        std::string_view data,
        Identity seed = 14695981039346656037ULL) {
    Identity hash = seed;
    for (unsigned char c : data) {
        hash ^= static_cast<Identity>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Hash a single byte into an existing FNV-1a state.
// Equivalent to fnv1a64({byte_ptr, 1}, seed) but without reinterpret_cast,
// which is forbidden in a constant expression ([expr.const]).
consteval Identity fnv1a64Byte(std::uint8_t b, Identity seed) {
    return (seed ^ static_cast<Identity>(b)) * 1099511628211ULL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Qualified name — consteval, using only probed primitives
// ─────────────────────────────────────────────────────────────────────────────

/// Builds the qualified name ("ns::Type") by walking up the scope hierarchy.
/// Uses only identifier_of + parent_of + is_namespace, all validated by probes.
/// Returns a const char* with static lifetime via define_static_string.
/// Counts the exact size first, then emits a transient character container to define_static_string.
/// Returns true when `e` is a template specialization.
/// GCC 16.1.0 P2996: template_arguments_of throws std::meta::exception for
/// non-specializations.  try/catch is valid in
/// consteval since C++23.
consteval bool isTemplateSpecialization(std::meta::info e) {
    try {
        return std::meta::template_arguments_of(e).size() > 0;
    } catch (...) {
        return false;
    }
}

consteval const char* qualifiedNameOf(std::meta::info entity) {
    // B1: template specialization fast-path.
    // On GCC 16, display_string_of returns the FULLY QUALIFIED name
    // (e.g. "ns::Foo<int>") — no parent walking needed or it would double-qualify.
    if (isTemplateSpecialization(entity)) {
        return std::define_static_string(std::meta::display_string_of(entity));
    }

    auto parentInScope = [](std::meta::info parent) consteval {
        return std::meta::is_namespace(parent)
               || (std::meta::is_type(parent) && std::meta::is_class_type(parent));
    };

    auto parentSegment = [](std::meta::info parent, bool& stopAfterSegment) consteval -> std::string_view {
        stopAfterSegment = false;
        if (!std::meta::has_identifier(parent)) return {};
        if (std::meta::is_type(parent) && std::meta::is_class_type(parent)
                && isTemplateSpecialization(parent)) {
            stopAfterSegment = true;
            return std::meta::display_string_of(parent);
        }
        return std::meta::identifier_of(parent);
    };

    // Non-template: first pass counts only segment length and segment count.
    std::size_t segmentLength = std::meta::identifier_of(entity).size();
    std::size_t segmentCount = 1;

    // B1: walk up parent chain covering both namespaces AND enclosing classes.
    auto parent = std::meta::parent_of(entity);
    while (parentInScope(parent)) {
        bool stopAfterSegment = false;
        const std::string_view segment = parentSegment(parent, stopAfterSegment);
        if (segment.empty()) break;
        segmentLength += segment.size();
        ++segmentCount;
        if (stopAfterSegment) break; // outer name is already fully qualified
        parent = std::meta::parent_of(parent);
    }

    const std::size_t totalLength =
        segmentLength + (segmentCount > 1 ? (segmentCount - 1) * 2 : 0);

    // Second pass writes each segment directly to its final slot in an automatic buffer.
    std::vector<char> buffer(totalLength);
    std::size_t writeEnd = totalLength;

    const std::string_view entitySegment = std::meta::identifier_of(entity);
    writeEnd -= entitySegment.size();
    for (std::size_t i = 0; i < entitySegment.size(); ++i) {
        buffer[writeEnd + i] = entitySegment[i];
    }

    parent = std::meta::parent_of(entity);
    while (parentInScope(parent)) {
        bool stopAfterSegment = false;
        const std::string_view segment = parentSegment(parent, stopAfterSegment);
        if (segment.empty()) break;

        if (writeEnd > 0) {
            writeEnd -= 2;
            buffer[writeEnd] = ':';
            buffer[writeEnd + 1] = ':';
        }
        writeEnd -= segment.size();
        for (std::size_t i = 0; i < segment.size(); ++i) {
            buffer[writeEnd + i] = segment[i];
        }

        if (stopAfterSegment) break;
        parent = std::meta::parent_of(parent);
    }

    return std::define_static_string(std::string_view{buffer.data(), totalLength});
}

// ─────────────────────────────────────────────────────────────────────────────
// Identity computation — consteval
// ─────────────────────────────────────────────────────────────────────────────

// typeName is passed pre-computed so callers need not call qualifiedNameOf twice.
consteval Identity computeIdentityForType(
        const char* typeName,
        std::string_view beanName,
        Lifetime lifetime) {
    auto h = fnv1a64Byte(0u, 14695981039346656037ULL); // kind = 0 (type/factory)
    h = fnv1a64(typeName, h);
    h = fnv1a64(";", h);
    h = fnv1a64(beanName, h);
    h = fnv1a64(";", h);
    h = fnv1a64Byte(static_cast<std::uint8_t>(lifetime), h);
    return h;
}

// factoryName is passed pre-computed so callers need not call qualifiedNameOf twice.
consteval Identity computeIdentityForProduct(
        std::meta::info producerMethod,
        const char* factoryName,
        std::string_view beanName,
        Lifetime lifetime) {
    auto h = fnv1a64Byte(1u, 14695981039346656037ULL); // kind = 1 (factory product)
    h = fnv1a64(factoryName, h);
    h = fnv1a64("::", h);
    h = fnv1a64(std::meta::identifier_of(producerMethod), h);
    h = fnv1a64(";", h);
    h = fnv1a64(beanName, h);
    h = fnv1a64(";", h);
    h = fnv1a64Byte(static_cast<std::uint8_t>(lifetime), h);
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Annotation extraction — single-pass consteval scan (D5)
//
// Replaces the three separate lifetimeOf / priorityOf / namedKeyOf helpers.
// One traversal of annotations_of per entity instead of up to three.
// ─────────────────────────────────────────────────────────────────────────────

struct AnnotationScan {
    Lifetime    lifetime = Lifetime::Singleton;
    int32_t     priority = 0;
    const char* beanName = "";
    bool        lazy              = true;  // true = lazy (default), false = eager at start()
    bool        lifetimeConflict  = false; // B6: multiple lifetime markers
    bool        hasLifetimeMarker = false; // B6: at least one lifetime marker seen
    bool        emptyNameError    = false; // B8: ctr::named with empty/null key
};

consteval AnnotationScan scanAnnotations(std::meta::info entity) {
    AnnotationScan r;
    r.beanName = std::define_static_string(std::string_view{""});
    for (auto ann : std::meta::annotations_of(entity)) {
        const auto t = std::meta::remove_const(std::meta::type_of(ann));
        if (std::meta::is_same_type(t, ^^CTORIUM_NAMESPACE::singleton)) {
            if (r.hasLifetimeMarker) r.lifetimeConflict = true;
            r.hasLifetimeMarker = true;
            const auto v = std::meta::extract<CTORIUM_NAMESPACE::singleton>(ann);
            r.lifetime = Lifetime::Singleton; r.priority = v.priority; r.lazy = v.lazy;
        } else if (std::meta::is_same_type(t, ^^CTORIUM_NAMESPACE::prototype)) {
            if (r.hasLifetimeMarker) r.lifetimeConflict = true;
            r.hasLifetimeMarker = true;
            const auto v = std::meta::extract<CTORIUM_NAMESPACE::prototype>(ann);
            r.lifetime = Lifetime::Prototype; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^CTORIUM_NAMESPACE::session)) {
            if (r.hasLifetimeMarker) r.lifetimeConflict = true;
            r.hasLifetimeMarker = true;
            const auto v = std::meta::extract<CTORIUM_NAMESPACE::session>(ann);
            r.lifetime = Lifetime::Session; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^CTORIUM_NAMESPACE::threadLocal)) {
            if (r.hasLifetimeMarker) r.lifetimeConflict = true;
            r.hasLifetimeMarker = true;
            const auto v = std::meta::extract<CTORIUM_NAMESPACE::threadLocal>(ann);
            r.lifetime = Lifetime::ThreadLocal; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^CTORIUM_NAMESPACE::named)) {
            const char* n = std::meta::extract<CTORIUM_NAMESPACE::named>(ann).name;
            if (n == nullptr || n[0] == '\0') r.emptyNameError = true; // B8
            r.beanName = std::define_static_string(
                std::string_view{(n != nullptr) ? n : ""});
        }
    }
    return r;
}

} // namespace CTORIUM_NAMESPACE::detail
