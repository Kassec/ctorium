#pragma once

#include <bit>
#include <cstdint>
#include <optional>

#include "Config.hpp"

#include "Bean.hpp"

namespace CTORIUM_NAMESPACE {

    class BeanContext;

    namespace detail {
        class Registry;
        class PrototypeStore;
    } // namespace detail

    /**
     * @brief Type-erased tracked handle to a resolved bean.
     *
     * Used by global listeners and passed as `const AnyBean&` in lifecycle callbacks.
     * Follows the same internal layout as `Bean<T>` (Form 1 / 2 / 3), with
     * `void*` instead of `T*` for the instance pointer.
     *
     * ### Relationship to Bean<T>
     * `AnyBean` and `Bean<T>` share the same internal layout so that `cast<T>()` and
     * `tryCast<T>()` can be implemented as cheap reinterpretation without an extra
     * copy.  Form 1 gets the concrete type from the `Descriptor` resolved through
     * the Registry; Form 2 uses scoped proxy resolution; Form 3 uses thread-local
     * resolution.
     * Prototype Form 1 handles anchor to the surviving PrototypeStore block.
     *
     * ### Tracking semantics
     * Identical to `Bean<T>`: copy retains, move transfers, destroy releases.
     * Prototype refcount operations use the same `SlotId` field as `Bean<T>`.
     */
    class AnyBean {
    public:
        /** @brief Builds an empty (null) handle. */
        constexpr AnyBean() noexcept = default;

        AnyBean(const AnyBean &other) noexcept
            : object_(other.object_),
              bits_(other.bits_),
              registry_(other.registry_) {
            if (static_cast<std::uint32_t>(bits_.u64) != detail::kInvalidSlotId && object_ != nullptr)
                retainIfPrototype();
        }

        AnyBean(AnyBean &&other) noexcept
            : object_(other.object_),
              bits_(other.bits_),
              registry_(other.registry_) {
            other.object_ = nullptr;
            other.bits_ = Bits{};
            other.registry_ = nullptr;
        }

        AnyBean &operator=(const AnyBean &other) noexcept {
            if (this != &other) {
                if (static_cast<std::uint32_t>(bits_.u64) != detail::kInvalidSlotId && object_ != nullptr)
                    releaseIfPrototype();
                object_ = other.object_;
                bits_ = other.bits_;
                registry_ = other.registry_;
                if (static_cast<std::uint32_t>(bits_.u64) != detail::kInvalidSlotId && object_ != nullptr)
                    retainIfPrototype();
            }
            return *this;
        }

        AnyBean &operator=(AnyBean &&other) noexcept {
            if (this != &other) {
                if (static_cast<std::uint32_t>(bits_.u64) != detail::kInvalidSlotId && object_ != nullptr)
                    releaseIfPrototype();
                object_ = other.object_;
                bits_ = other.bits_;
                registry_ = other.registry_;
                other.object_ = nullptr;
                other.bits_ = Bits{};
                other.registry_ = nullptr;
            }
            return *this;
        }

        ~AnyBean() noexcept {
            if (static_cast<std::uint32_t>(bits_.u64) != detail::kInvalidSlotId && object_ != nullptr)
                releaseIfPrototype();
        }

        // -------------------------------------------------------------------------
        // Context
        // -------------------------------------------------------------------------

        /**
         * @brief Returns the owning resolution context.
         * TODO: implement after the BeanContext ↔ Registry bridge is in place.
         */
        [[nodiscard]] BeanContext &context() const noexcept;

        /**
         * @brief Returns a metadata view for this bean.
         *
         * Zero-allocation; safe to call from lifecycle-listener callbacks.
         */
        [[nodiscard]] class BeanMetadata metadata() const noexcept;
        // Defined in HandleInlineImpl.hpp.

        // -------------------------------------------------------------------------
        // Casts
        // -------------------------------------------------------------------------

        /**
         * @brief Tests whether the exact concrete runtime type is U.
         * @tparam U Target type.
         */
        template <class U>
        [[nodiscard]] bool exact() const noexcept; // TODO: HandleInlineImpl.hpp

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
        [[nodiscard]] Bean<U> cast() const; // TODO

        /**
         * @brief Attempts to cast to `Bean<U>`; returns `std::nullopt` if incompatible.
         * @tparam U Target type.
         */
        template <class U>
        [[nodiscard]] std::optional<Bean<U>> tryCast() const; // TODO

        // -------------------------------------------------------------------------
        // Identity comparison
        // -------------------------------------------------------------------------

        [[nodiscard]] bool operator==(const AnyBean &other) const noexcept {
            return object_ == other.object_
                && std::bit_cast<std::uint64_t>(bits_) == std::bit_cast<std::uint64_t>(other.bits_)
                && registry_ == other.registry_;
        }

        [[nodiscard]] bool operator!=(const AnyBean &other) const noexcept {
            return !(*this == other);
        }

    private:
        friend class BeanContext;
        friend class ScopedContext;
        friend class detail::Registry;
        template <class U>
        friend class Bean;

        void retainIfPrototype() noexcept; // defined in HandleInlineImpl.hpp
        void releaseIfPrototype() noexcept; // defined in HandleInlineImpl.hpp
        [[nodiscard]] detail::Registry* registry() const noexcept;

        void *object_ = nullptr;

        union Bits {
            struct F1 {
                detail::SlotId slot;
                detail::DescriptorId descId;
            } f1;

            struct F2 {
                detail::NameId scopeNameId;
                detail::DescriptorId descId;
            } f2;

            std::uint64_t u64 = 0;
        } bits_{};

        void* registry_ = nullptr;
    };

} // namespace CTORIUM_NAMESPACE
