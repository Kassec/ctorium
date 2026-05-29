#pragma once

#include <bit>
#include <cstdint>
#include <optional>

#include "Bean.hpp"

namespace ctr {

class BeanContext;

namespace detail {
class Registry;
} // namespace detail

/**
 * @brief Type-erased tracked handle to a resolved bean.
 *
 * Used by global listeners and passed as `const AnyBean&` in lifecycle callbacks.
 * Follows the same Form 1 / Form 2 ADR-O3 layout as `Bean<T>`, with `void*`
 * instead of `T*` for the instance pointer.
 *
 * ### Relationship to Bean<T>
 * `AnyBean` and `Bean<T>` share the same internal layout so that `cast<T>()` and
 * `tryCast<T>()` can be implemented as cheap reinterpretation without an extra
 * copy.  The concrete type is known via the `Descriptor` looked up through the
 * Registry using `bits_.f1.slot` (Form 1) or the proxy resolution (Form 2).
 *
 * ### Tracking semantics
 * Identical to `Bean<T>`: copy retains, move transfers, destroy releases.
 * Prototype refcount operations use the same `SlotId` field as `Bean<T>`.
 */
class AnyBean {
public:
    /** @brief Builds an empty (null) handle. */
    constexpr AnyBean() noexcept = default;

    AnyBean(const AnyBean&) noexcept = default;
    AnyBean(AnyBean&&)      noexcept = default;
    AnyBean& operator=(const AnyBean&) noexcept = default;
    AnyBean& operator=(AnyBean&&)      noexcept = default;
    ~AnyBean() noexcept = default;
    // TODO: add retain/release for prototype refcount once BeanInlineImpl.hpp is in place.

    // -------------------------------------------------------------------------
    // Context
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the owning resolution context.
     * TODO: implement after the BeanContext ↔ Registry bridge is in place.
     */
    [[nodiscard]] BeanContext& context() const noexcept;

    // -------------------------------------------------------------------------
    // Casts
    // -------------------------------------------------------------------------

    /**
     * @brief Tests whether the exact concrete runtime type is U.
     * @tparam U Target type.
     */
    template <class U>
    [[nodiscard]] bool exact() const noexcept;  // TODO: BeanInlineImpl.hpp

    /**
     * @brief Tests whether this bean is compatible with U.
     * @tparam U Target type.
     */
    template <class U>
    [[nodiscard]] bool compatible() const noexcept; // TODO

    /**
     * @brief Casts to `Bean<U>`; raises `ResolutionError` if incompatible.
     * @tparam U Target type.
     */
    template <class U>
    [[nodiscard]] Bean<U> cast() const;  // TODO

    /**
     * @brief Attempts to cast to `Bean<U>`; returns `std::nullopt` if incompatible.
     * @tparam U Target type.
     */
    template <class U>
    [[nodiscard]] std::optional<Bean<U>> tryCast() const;  // TODO

    // -------------------------------------------------------------------------
    // Identity comparison
    // -------------------------------------------------------------------------

    [[nodiscard]] bool operator==(const AnyBean& other) const noexcept {
        return object_ == other.object_
            && std::bit_cast<std::uint64_t>(bits_) == std::bit_cast<std::uint64_t>(other.bits_)
            && registry_ == other.registry_;
    }

    [[nodiscard]] bool operator!=(const AnyBean& other) const noexcept {
        return !(*this == other);
    }

private:
    friend class BeanContext;
    friend class detail::Registry;
    template <class U> friend class Bean;

    void*              object_  = nullptr;

    union Bits {
        struct F1 { std::uint32_t slot; std::uint32_t unused_; } f1;
        struct F2 { std::uint32_t scopeNameId; std::uint32_t candidateNameId; } f2;
        std::uint64_t u64 = 0;
    } bits_{};

    detail::Registry*  registry_ = nullptr;
};

} // namespace ctr
