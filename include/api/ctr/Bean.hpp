#pragma once

#include <bit>
#include <cstdint>
#include <optional>

#include "../../internal/DescriptorId.hpp"
#include "../../internal/NameId.hpp"
#include "../../internal/SlotId.hpp"
#include "../../internal/TypeId.hpp"

namespace ctr {

    class BeanContext;
    class AnyBean;

    namespace detail {
        class Registry;
        class PrototypeStore;
    } // namespace detail

    /**
     * @brief Typed tracked handle to a resolved bean.
     *
     * `Bean<T>` exists in three mutually exclusive internal forms, discriminated by
     * `object_` and, when `object_` is null, by `f2_.scopeNameId`.
     *
     * ### Form 1 — Direct  (`object_ != nullptr`)
     * Used for singleton and prototype beans whose resolved instance is stable.
     * `operator->` returns `object_` directly; no registry interaction.
     * Fields: `object_` (non-null), `f1_.slot` (`SlotId`), and `registry_`.
     * Singleton anchors are `Registry*`; prototype anchors are the surviving
     * `PrototypeStore` block.
     *
     * Prototype beans use the `SlotId` for reference counting (retain/release
     * on copy/destroy).  Singleton beans set `f1_.slot = kInvalidSlotId`; their
     * lifetime is governed by root shutdown, not by handle refcounting.
     *
     * ### Form 2 — Proxy  (`object_ == nullptr`)
     * Used for all session beans (directly resolved or injected via `[[=ctr::scoped]]`).
     * `operator->` resolves the current session object on every call; the result may
     * change across scope cycles.  If the target scope is stopped, `operator->` and
     * `value()` return `nullptr` — no exception.
     * Fields: `f2_.scopeNameId`, `f2_.descId`, `registry_` as `Registry*`.
     *
     * ### Form 3 — ThreadLocal  (`object_ == nullptr`, `f2_.scopeNameId == kThreadLocalSentinel`)
     * Used for thread-local beans. `operator->` resolves the calling thread's
     * instance from the thread-local store on every dereference and never caches
     * the pointer. Field `f2_.descId` identifies the descriptor.
     *
     * All three forms share the same 24-byte layout on 64-bit:
     * `void*(8) + union{uint32_t,uint32_t}(8) + anchor(8)`.
     *
     * ### Empty / moved-from state
     * Both `object_` and `registry_` are null; the union fields are zero.
     * Accessing an empty or moved-from handle is undefined behaviour.
     *
     * ### Tracking semantics
     * - Copy: retains the same logical bean (prototype: atomic refcount increment).
     * - Move: transfers ownership; source becomes empty.
     * - Destroy: releases tracking (prototype: atomic refcount decrement; may trigger
     *   object destruction if the count reaches zero).
     * - Singleton handles: copy/destroy are cheap struct copies; no atomic ops.
     */
    template <class T>
    class Bean {
    public:
        /** @brief Builds an empty (null) handle. */
        constexpr Bean() noexcept = default;

        Bean(const Bean &other) noexcept
            : object_(other.object_),
              bits_(other.bits_),
              registry_(other.registry_) {
            if (static_cast<std::uint32_t>(bits_.u64) != detail::kInvalidSlotId && object_ != nullptr)
                retainIfPrototype();
        }

        Bean(Bean &&other) noexcept
            : object_(other.object_),
              bits_(other.bits_),
              registry_(other.registry_) {
            other.object_ = nullptr;
            other.bits_ = Bits{};
            other.registry_ = nullptr;
        }

        Bean &operator=(const Bean &other) noexcept {
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

        Bean &operator=(Bean &&other) noexcept {
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

        ~Bean() noexcept {
            if (static_cast<std::uint32_t>(bits_.u64) != detail::kInvalidSlotId && object_ != nullptr)
                releaseIfPrototype();
        }

        // -------------------------------------------------------------------------
        // Dereference
        // -------------------------------------------------------------------------

        /**
         * @brief Accesses the managed object.
         *
         * Form 1 (direct): returns `object_` immediately — no registry call, O(1).
         * Form 2 (proxy):  resolves the current session object from the active scope.
         *   Returns `nullptr` if the scope is stopped or missing.
         *   The proxy path is implemented in `HandleInlineImpl.hpp` once `Registry` is
         *   complete; until then it returns `nullptr` with a TODO marker.
         *
         * @return Pointer to the managed object, or `nullptr` for a stopped scope (Form 2).
         */
        [[nodiscard]] T *operator->() const noexcept {
            if (object_ != nullptr) [[likely]] {
                // Form 1 fast path: stable pointer, no registry interaction.
                return static_cast<T *>(object_);
            }
            if (registry_ == nullptr)
                return nullptr; // empty handle

            // Form 3: thread-local — resolve against calling thread's store.
            if (bits_.f2.scopeNameId == detail::kThreadLocalSentinel) [[unlikely]]
                return threadLocalResolve_();

            // Form 2 proxy path: resolve current session object.
            // Defined in HandleInlineImpl.hpp (requires full ScopedContext definition).
            return proxyResolve_();
        }

        /**
         * @brief Dereferences the managed object.
         *
         * Precondition: `operator->() != nullptr`.
         * Undefined behaviour if the handle is Form 2 with an inactive scope, or
         * Form 1 empty.  The caller must check `operator->()` before calling
         * `operator*()`.
         *
         * @return Reference to the managed object.
         */
        [[nodiscard]] T &operator*() const noexcept {
            return *operator->();
        }

        /**
         * @brief Returns the managed object reference (same as `operator*`).
         *
         * Precondition: `operator->() != nullptr`.
         * Undefined behaviour if the handle is Form 2 with an inactive scope, or
         * Form 1 empty.  The caller must check `operator->()` before calling
         * `value()`.
         *
         * @return Reference to the managed object.
         */
        [[nodiscard]] T &value() const noexcept {
            return *operator->();
        }

        // -------------------------------------------------------------------------
        // Context
        // -------------------------------------------------------------------------

        /**
         * @brief Returns the owning resolution context.
         *
         * For root-owned beans (singleton, prototype): the root `BeanContext`.
         * For scope-owned beans (session): the `ScopedContext` that owns this bean.
         *
         * TODO: implement after the BeanContext ↔ Registry bridge is in place.
         */
        [[nodiscard]] BeanContext &context() const noexcept;

        /**
         * @brief Returns a metadata view for this bean.
         *
         * Zero-allocation; safe to call from lifecycle-listener callbacks.
         * The core (type, name, lifetime, origin) is always available.
         * Reflective metadata (methods) is available only when the bean was
         * discovered with `DiscoverOptions{.retainAllMetadata = true}`.
         */
        [[nodiscard]] class BeanMetadata metadata() const noexcept;
        // Defined in HandleInlineImpl.hpp (requires BeanMetadata.hpp via Ctorium.hpp).

        // -------------------------------------------------------------------------
        // Identity comparison
        // -------------------------------------------------------------------------

        /**
         * @brief Compares logical handle identity.
         *
         * Two handles are equal when they refer to the same logical bean: same Form,
         * same object pointer (Form 1) or same scope/descriptor pair (Form 2).
         */
        [[nodiscard]] bool operator==(const Bean &other) const noexcept {
            return object_ == other.object_
                && std::bit_cast<std::uint64_t>(bits_) == std::bit_cast<std::uint64_t>(other.bits_)
                && registry_ == other.registry_;
        }

        [[nodiscard]] bool operator!=(const Bean &other) const noexcept {
            return !(*this == other);
        }

        // -------------------------------------------------------------------------
        // Casts
        // -------------------------------------------------------------------------

        /**
         * @brief Tests whether the exact concrete runtime type is U.
         * @tparam U Target type.
         */
        template <class U>
        [[nodiscard]] bool exact() const noexcept; // TODO: implemented in HandleInlineImpl.hpp

        /**
         * @brief Tests whether this bean is compatible with (convertible to) U.
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

    private:
        template <class U>
        friend class Bean;
        friend class AnyBean;
        friend class BeanContext;
        friend class detail::Registry;

        // -------------------------------------------------------------------------
        // Internal constructors (called by Registry)
        // -------------------------------------------------------------------------

        /**
         * @brief Constructs a Form 1 (direct) handle for singleton or prototype beans.
         *
         * @param instance Non-null pointer to the live bean instance.
         * @param slot     `kInvalidSlotId` for singletons (no refcount management);
         *                 a valid `SlotId` for prototypes (refcount already = 1).
         * @param descId   Descriptor index for this bean (used by cast/context queries).
         * @param reg      Registry that owns this bean.
         */
        static Bean makeDirect(
            T *instance, detail::SlotId slot,
            detail::DescriptorId descId,
            detail::Registry *reg
            ) noexcept;

        /**
         * @brief Constructs a Form 2 (proxy) handle for session beans.
         *
         * The handle does not resolve the object at construction time; resolution
         * occurs on each `operator->` call against the active scope.
         *
         * @param scopeNameId     NameId of the target scope.
         * @param descId          DescriptorId of the target session bean.
         * @param reg             Root Registry.
         */
        static Bean makeProxy(
            detail::NameId scopeNameId, detail::DescriptorId descId,
            detail::Registry *reg
            ) noexcept {
            Bean b;
            b.object_ = nullptr;
            b.bits_.f2.scopeNameId = scopeNameId;
            b.bits_.f2.descId = descId;
            b.registry_ = reg;
            return b;
        }

        /**
         * @brief Constructs a deferred Form 2 handle.  Alias of `makeProxy()`.
         *
         * Intended call site: scoped-deferred injection via a
         * `[[=ctr::scoped{...}]]` parameter.  Behaves identically to `makeProxy()`; the distinct
         * name communicates intent at the injection site.
         *
         * @param scopeNameId     NameId of the target scope.
         * @param descId          DescriptorId of the target session bean.
         * @param reg             Root Registry.
         */
        static Bean makeDeferred(
            detail::NameId scopeNameId, detail::DescriptorId descId,
            detail::Registry *reg
            ) noexcept {
            return makeProxy(scopeNameId, descId, reg);
        }

        /**
         * @brief Constructs a Form 3 (thread-local) handle.
         *
         * `operator->` on this handle always resolves the calling thread's instance
         * by looking up the thread-local store — it never caches the pointer.
         *
         * @param descId DescriptorId of the target thread-local bean.
         * @param reg             Root Registry.
         */
        static Bean makeThreadLocal(
            detail::DescriptorId descId,
            detail::Registry *reg
            ) noexcept {
            Bean b;
            b.object_ = nullptr;
            b.bits_.f2.scopeNameId = detail::kThreadLocalSentinel;
            b.bits_.f2.descId = descId;
            b.registry_ = reg;
            return b;
        }

        // -------------------------------------------------------------------------
        // Prototype refcount helpers
        // -------------------------------------------------------------------------

        /**
         * @brief Prototype refcount helpers (copy/move/destroy tracking).
         *
         * Defined in HandleInlineImpl.hpp (need the full `Registry` definition).
         * Callers must guard with
         * `bits_.f1.slot != kInvalidSlotId && object_ != nullptr`; the helper
         * body assumes a prototype handle and keeps only prototype tracking work.
         */
        void retainIfPrototype() noexcept;
        void releaseIfPrototype() noexcept;
        [[nodiscard]] detail::Registry* registry() const noexcept;
        T *proxyResolve_() const noexcept; // Form 2 proxy — defined in HandleInlineImpl.hpp
        T *threadLocalResolve_() const noexcept; // Form 3 TL — defined in HandleInlineImpl.hpp

        // -------------------------------------------------------------------------
        // Layout  (24 bytes on 64-bit, overloaded anchor)
        // -------------------------------------------------------------------------

        /** Non-null: Form 1 (direct).  Null: Form 2 (proxy), Form 3 (thread-local), or empty. */
        void *object_ = nullptr;

        /**
         * @brief Form-dependent 8-byte field sharing the same storage.
         *
         * Form 1: `f1.slot` (4 bytes) + `f1.descId` (4 bytes).
         * Form 2/3: `f2.scopeNameId` (4 bytes) + `f2.descId` (4 bytes).
         *
         * The active form is determined by `object_`, then by `f2.scopeNameId`
         * when `object_` is null (`kThreadLocalSentinel` selects Form 3).
         * `u64` provides a single 64-bit value for identity comparison and zeroing.
         */
        union Bits {
            struct F1 {
                detail::SlotId slot;
                detail::DescriptorId descId;
            } f1;

            struct F2 {
                detail::NameId scopeNameId;
                detail::DescriptorId descId;
            } f2;

            std::uint64_t u64 = 0; // default-initializes both forms to zero
        } bits_{};

        void* registry_ = nullptr;
    };

} // namespace ctr
