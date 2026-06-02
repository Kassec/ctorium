#pragma once

/**
 * @brief Configurable namespace for the Ctorium library.
 *
 * Define CTORIUM_NAMESPACE before including any Ctorium header to place
 * the entire public and internal API under a custom namespace.
 * Defaults to `ctr` when not defined.
 *
 * Must be defined consistently across all translation units.
 */
#ifndef CTORIUM_NAMESPACE
#define CTORIUM_NAMESPACE ctr
#endif
