#pragma once

#include <string_view>

#include "Config.hpp"

namespace CTORIUM_NAMESPACE {

/**
 * @brief Marks a bean as prototype scoped.
 */
struct prototype {
    /** Priority used to arbitrate among candidates. */
    int priority = 0;
};

/**
 * @brief Marks a bean as singleton scoped.
 */
struct singleton {
    /** Priority used to arbitrate among candidates. */
    int priority = 0;
    /** Whether creation is deferred until first resolution. true = lazy (default), false = eager. */
    bool lazy = true;
};

/**
 * @brief Marks a bean as session scoped.
 */
struct session {
    /** Priority used to arbitrate among candidates. */
    int priority = 0;
};

/**
 * @brief Marks a bean as thread-local scoped.
 */
struct threadLocal {
    /** Priority used to arbitrate among candidates. */
    int priority = 0;
};

/**
 * @brief Named qualifier marker and runtime selector.
 *
 * Empty names are rejected by runtime/configuration validation.
 */
// named is an aggregate (no user-provided constructors) so that:
//  - it is a structural type and can be used as an annotation value (P2996), and
//  - GCC's reflect_constant can extract it from annotation info.
// const char* is a structural type ([temp.param]); std::string_view is NOT structural
// in GCC 16.1.0 (private internal members disqualify it). Static storage duration
// is required but not sufficient: only define_static_string provenance is
// template-argument-equivalent and extractible as an annotation value. A raw
// literal has static storage duration but fails extraction. Use define_static_string
// for every annotation value.
struct named {
    /** Name key; nullptr or "" means the unnamed key. */
    const char* name = nullptr;
};

/**
 * @brief Marks a bean as a factory source.
 */
struct factory {};

/**
 * @brief Marks a post-construction lifecycle callback.
 */
struct postConstruct {};

/**
 * @brief Marks a pre-destruction lifecycle callback.
 */
struct preDestroy {};

/**
 * @brief Scope-injection qualifier. Valid at injection points only, never on bean declarations.
 *
 * Targets a session dependency against the named scope. Using scoped on a non-session
 * target raises ConfigurationError.
 */
struct scoped {
    /** Name of the scope the session dependency is resolved against. */
    const char* name = nullptr;
};

} // namespace CTORIUM_NAMESPACE
