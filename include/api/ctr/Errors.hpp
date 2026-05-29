#pragma once

#include <stdexcept>
#include <string>

namespace ctr {

/**
 * @brief Base exception for public Ctorium API failures.
 */
class CtoriumError : public std::runtime_error {
public:
    /**
     * @brief Builds an error with an explicit message.
     * @param message User-facing error message.
     */
    explicit CtoriumError(std::string message)
        : std::runtime_error(std::move(message)) {}

    /**
     * @brief Builds an error with an explicit message.
     * @param message User-facing error message.
     */
    explicit CtoriumError(const char* message)
        : std::runtime_error(message) {}
};

/**
 * @brief Error raised when bean resolution cannot be completed.
 */
class ResolutionError : public CtoriumError {
public:
    /**
     * @brief Builds an error with an explicit message.
     * @param message User-facing error message.
     */
    explicit ResolutionError(std::string message)
        : CtoriumError(std::move(message)) {}

    /**
     * @brief Builds an error with an explicit message.
     * @param message User-facing error message.
     */
    explicit ResolutionError(const char* message)
        : CtoriumError(message) {}
};

/**
 * @brief Error raised when an operation is invalid for the current context state.
 */
class ContextStateError : public CtoriumError {
public:
    /**
     * @brief Builds an error with an explicit message.
     * @param message User-facing error message.
     */
    explicit ContextStateError(std::string message)
        : CtoriumError(std::move(message)) {}

    /**
     * @brief Builds an error with an explicit message.
     * @param message User-facing error message.
     */
    explicit ContextStateError(const char* message)
        : CtoriumError(message) {}
};

/**
 * @brief Error raised when public configuration is invalid.
 */
class ConfigurationError : public CtoriumError {
public:
    /**
     * @brief Builds an error with an explicit message.
     * @param message User-facing error message.
     */
    explicit ConfigurationError(std::string message)
        : CtoriumError(std::move(message)) {}

    /**
     * @brief Builds an error with an explicit message.
     * @param message User-facing error message.
     */
    explicit ConfigurationError(const char* message)
        : CtoriumError(message) {}
};

} // namespace ctr
