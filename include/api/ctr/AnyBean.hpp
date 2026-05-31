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
     * Prototype Form 1 handles also retain the Registry liveness block.
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
              registry_(other.registry_),
              registryLiveness_(other.registryLiveness_) {
            retainIfPrototype();
        }

        AnyBean(AnyBean &&other) noexcept
            : object_(other.object_),
              bits_(other.bits_),
              registry_(other.registry_),
              registryLiveness_(other.registryLiveness_) {
            other.object_ = nullptr;
            other.bits_ = Bits{};
            other.registry_ = nullptr;
            other.registryLiveness_ = nullptr;
        }

        AnyBean &operator=(const AnyBean &other) noexcept {
            if (this != &other) {
                releaseIfPrototype();
                object_ = other.object_;
                bits_ = other.bits_;
                registry_ = other.registry_;
                registryLiveness_ = other.registryLiveness_;
                retainIfPrototype();
            }
            return *this;
        }

        AnyBean &operator=(AnyBean &&other) noexcept {
            if (this != &other) {
                releaseIfPrototype();
                object_ = other.object_;
                bits_ = other.bits_;
                registry_ = other.registry_;
                registryLiveness_ = other.registryLiveness_;
                other.object_ = nullptr;
                other.bits_ = Bits{};
                other.registry_ = nullptr;
                other.registryLiveness_ = nullptr;
            }
            return *this;
        }

        ~AnyBean() noexcept {
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
        // Defined in BeanInlineImpl.hpp.

        // -------------------------------------------------------------------------
        // Casts
        // -------------------------------------------------------------------------

        /**
         * @brief Tests whether the exact concrete runtime type is U.
         * @tparam U Target type.
         */
        template <class U>
        [[nodiscard]] bool exact() const noexcept; // TODO: BeanInlineImpl.hpp

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

        void retainIfPrototype() noexcept; // defined in BeanInlineImpl.hpp
        void releaseIfPrototype() noexcept; // defined in BeanInlineImpl.hpp

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

        detail::Registry *registry_ = nullptr;
        detail::RegistryLiveness* registryLiveness_ = nullptr;
    };

} // namespace ctr
