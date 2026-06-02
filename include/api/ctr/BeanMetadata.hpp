#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <typeinfo>

#include "Config.hpp"

#include "Lifetime.hpp"
#include "Markers.hpp"
#include "Origin.hpp"

namespace CTORIUM_NAMESPACE {

/**
 * @brief Runtime record for a retained method parameter.
 *
 * Stored in a `constexpr static` array produced by `makeReflectiveData`
 * when `DiscoverOptions::retainAllMetadata = true`.
 */
struct BeanParameterRecord {
    /** Function pointer returning `typeid(InjectedType)` at runtime. */
    const std::type_info& (*injectedTypeInfo)();

    /** Type_info of the injected parameter type. */
    [[nodiscard]] const std::type_info& injectedType() const noexcept {
        return injectedTypeInfo ? injectedTypeInfo() : typeid(void);
    }
};

/**
 * @brief Runtime record for a single retained bean method.
 *
 * Stored in a `constexpr static` array produced by `makeExposedDescriptors`
 * when `DiscoverOptions::retainAllMetadata = true`.  All pointers have static
 * storage duration and remain valid for the lifetime of the process.
 */
struct BeanMethodRecord {
    /** Identifier (unqualified) of the method. */
    const char*  name;
    /** True when the method carries `[[=ctr::postConstruct{}]]`. */
    bool         isPostConstruct;
    /** True when the method carries `[[=ctr::preDestroy{}]]`. */
    bool         isPreDestroy;
    /** Number of parameters. */
    std::size_t  paramCount;
    /** Static-lifetime parameter records; null when `paramCount == 0`. */
    const BeanParameterRecord* params = nullptr;
};

/**
 * @brief Retained reflective method table for a bean type.
 *
 * Stored inside the runtime `Descriptor` when `retainAllMetadata = true`;
 * `nullptr` otherwise.  All data has static storage duration.
 */
struct BeanReflectiveData {
    const BeanMethodRecord* methods;
    std::size_t             methodCount;
};

/**
 * @brief Const, non-owning view of metadata for a resolved bean.
 *
 * Construction is zero-allocation (reads directly from the runtime descriptor).
 * Usable from lifecycle-listener callbacks on the `onInitialized`/`onCreated` hot path.
 *
 * ### Core (always available)
 * `observedType()`, `exactType()`, `name()`, `lifetime()`, `origin()`.
 *
 * ### Reflective (available only when `retainAllMetadata = true`)
 * `methods()` — may be filtered with `annotatedWith<A>()`.
 * Empty when the bean was not discovered with `retainAllMetadata`, or is a
 * runtime-bound object (non-Ctorium type).
 */
class BeanMetadata {
public:
    /**
     * @brief Thin lazy-filtered view over a method list.
     *
     * Obtained from `MethodsRange::annotatedWith<A>()`.  Iterates only over
     * methods that carry annotation `A`.  No allocation.
     */
    template<class A>
    class FilteredMethodsRange {
    public:
        FilteredMethodsRange(const BeanMethodRecord* begin,
                             const BeanMethodRecord* end) noexcept
            : begin_(begin), end_(end) {}

        struct MethodView {
            const BeanMethodRecord& rec;
            [[nodiscard]] const char*  name()           const noexcept { return rec.name; }
            [[nodiscard]] std::size_t  parameterCount() const noexcept { return rec.paramCount; }

            /**
             * @brief Returns annotation `Annotation` when this method carries it.
             */
            template<class Annotation>
            [[nodiscard]] std::optional<Annotation> annotation() const noexcept {
                if constexpr (std::is_same_v<Annotation, CTORIUM_NAMESPACE::postConstruct>) {
                    if (rec.isPostConstruct) return Annotation{};
                } else if constexpr (std::is_same_v<Annotation, CTORIUM_NAMESPACE::preDestroy>) {
                    if (rec.isPreDestroy) return Annotation{};
                }
                return std::nullopt;
            }

            /**
             * @brief Returns retained parameter records for this method.
             */
            [[nodiscard]] std::span<const BeanParameterRecord> parameters() const noexcept {
                return {rec.params, rec.paramCount};
            }
        };

        struct Iterator {
            const BeanMethodRecord* ptr;
            const BeanMethodRecord* end;
            static bool matches(const BeanMethodRecord& r) noexcept {
                if constexpr (std::is_same_v<A, CTORIUM_NAMESPACE::postConstruct>) return r.isPostConstruct;
                if constexpr (std::is_same_v<A, CTORIUM_NAMESPACE::preDestroy>)    return r.isPreDestroy;
                return false;
            }
            void advance() noexcept { while (ptr != end && !matches(*ptr)) ++ptr; }
            [[nodiscard]] MethodView operator*() const noexcept { return {*ptr}; }
            Iterator& operator++() noexcept { ++ptr; advance(); return *this; }
            [[nodiscard]] bool operator==(const Iterator& o) const noexcept { return ptr == o.ptr; }
            [[nodiscard]] bool operator!=(const Iterator& o) const noexcept { return ptr != o.ptr; }
        };

        [[nodiscard]] Iterator begin() const noexcept {
            Iterator it{begin_, end_};
            it.advance();
            return it;
        }
        [[nodiscard]] Iterator end() const noexcept { return {end_, end_}; }
        [[nodiscard]] bool empty() const noexcept { return begin() == end(); }

    private:
        const BeanMethodRecord* begin_;
        const BeanMethodRecord* end_;
    };

    /**
     * @brief Unfiltered view over all retained methods.
     *
     * Obtained from `BeanMetadata::methods()`.
     */
    class MethodsRange {
    public:
        struct MethodView {
            const BeanMethodRecord& rec;
            [[nodiscard]] const char*  name()           const noexcept { return rec.name; }
            [[nodiscard]] std::size_t  parameterCount() const noexcept { return rec.paramCount; }

            template<class A>
            [[nodiscard]] bool hasAnnotation() const noexcept {
                if constexpr (std::is_same_v<A, CTORIUM_NAMESPACE::postConstruct>) return rec.isPostConstruct;
                if constexpr (std::is_same_v<A, CTORIUM_NAMESPACE::preDestroy>)    return rec.isPreDestroy;
                return false;
            }

            /**
             * @brief Returns annotation `Annotation` when this method carries it.
             */
            template<class Annotation>
            [[nodiscard]] std::optional<Annotation> annotation() const noexcept {
                if constexpr (std::is_same_v<Annotation, CTORIUM_NAMESPACE::postConstruct>) {
                    if (rec.isPostConstruct) return Annotation{};
                } else if constexpr (std::is_same_v<Annotation, CTORIUM_NAMESPACE::preDestroy>) {
                    if (rec.isPreDestroy) return Annotation{};
                }
                return std::nullopt;
            }

            /**
             * @brief Returns retained parameter records for this method.
             */
            [[nodiscard]] std::span<const BeanParameterRecord> parameters() const noexcept {
                return {rec.params, rec.paramCount};
            }
        };

        struct Iterator {
            const BeanMethodRecord* ptr;
            [[nodiscard]] MethodView operator*() const noexcept { return {*ptr}; }
            Iterator& operator++() noexcept { ++ptr; return *this; }
            [[nodiscard]] bool operator!=(const Iterator& o) const noexcept { return ptr != o.ptr; }
        };

        MethodsRange(const BeanMethodRecord* begin, std::size_t count) noexcept
            : begin_(begin), count_(count) {}

        [[nodiscard]] Iterator    begin() const noexcept { return {begin_}; }
        [[nodiscard]] Iterator    end()   const noexcept { return {begin_ + count_}; }
        [[nodiscard]] std::size_t size()  const noexcept { return count_; }
        [[nodiscard]] bool        empty() const noexcept { return count_ == 0; }

        /** Returns a lazy-filtered range over methods annotated with `A`. */
        template<class A>
        [[nodiscard]] FilteredMethodsRange<A> annotatedWith() const noexcept {
            return {begin_, begin_ + count_};
        }

    private:
        const BeanMethodRecord* begin_;
        std::size_t             count_;
    };

    // ── Construction ─────────────────────────────────────────────────────────

    /**
     * @brief Constructs a metadata view from runtime descriptor fields.
     *
     * @param observedTypeGetter  `typeid` of the exposed type.
     * @param exactTypeGetter     `typeid` of the concrete type.
     * @param nameStr             Bean name string (static lifetime, may be empty string).
     * @param factoryMethodName   Unqualified factory producer method name, or empty string.
     * @param lt                  Scope lifetime.
     * @param orig                Descriptor origin.
     * @param reflective          Retained reflective data, or `nullptr`.
     */
    BeanMetadata(const std::type_info& (*observedTypeGetter)(),
                 const std::type_info& (*exactTypeGetter)(),
                 const char*            nameStr,
                 const char*            factoryMethodName,
                 Lifetime               lt,
                 Origin                 orig,
                 const BeanReflectiveData* reflective) noexcept
        : observedTypeGetter_(observedTypeGetter)
        , exactTypeGetter_   (exactTypeGetter)
        , nameStr_           (nameStr)
        , factoryMethodName_ (factoryMethodName)
        , lifetime_          (lt)
        , origin_            (orig)
        , reflective_        (reflective)
    {}

    /**
     * @brief Constructs a metadata view without a factory producer method name.
     */
    BeanMetadata(const std::type_info& (*observedTypeGetter)(),
                 const std::type_info& (*exactTypeGetter)(),
                 const char*            nameStr,
                 Lifetime               lt,
                 Origin                 orig,
                 const BeanReflectiveData* reflective) noexcept
        : BeanMetadata(observedTypeGetter, exactTypeGetter, nameStr, "",
                       lt, orig, reflective)
    {}

    // ── Core ──────────────────────────────────────────────────────────────

    /** Type_info of the exposed (observed) type (as in `bean.operator->()` return type). */
    [[nodiscard]] const std::type_info& observedType() const noexcept {
        return observedTypeGetter_ ? observedTypeGetter_()
                                   : typeid(void);
    }

    /** Type_info of the concrete (exact) instantiated type. */
    [[nodiscard]] const std::type_info& exactType() const noexcept {
        return exactTypeGetter_ ? exactTypeGetter_()
                                : typeid(void);
    }

    /** Named qualifier, or empty string_view for unnamed beans. */
    [[nodiscard]] std::string_view name() const noexcept {
        return nameStr_ ? std::string_view{nameStr_} : std::string_view{};
    }

    /** Unqualified factory producer method name, or empty string_view otherwise. */
    [[nodiscard]] std::string_view factoryMethod() const noexcept {
        return factoryMethodName_ ? std::string_view{factoryMethodName_} : std::string_view{};
    }

    /** Scope lifetime governing instance sharing and destruction. */
    [[nodiscard]] Lifetime lifetime() const noexcept { return lifetime_; }

    /** How this bean was contributed (annotated type, factory product, runtime binding). */
    [[nodiscard]] Origin origin() const noexcept { return origin_; }

    // ── Reflective ────────────────────────────────────────────────────────

    /**
     * @brief Returns the retained method list.
     *
     * Empty when `retainAllMetadata = false` (the default), or when the bean
     * is a runtime-bound object (non-Ctorium type).
     */
    [[nodiscard]] MethodsRange methods() const noexcept {
        if (!reflective_) return {nullptr, 0};
        return {reflective_->methods, reflective_->methodCount};
    }

private:
    const std::type_info& (*observedTypeGetter_)();
    const std::type_info& (*exactTypeGetter_)();
    const char*            nameStr_;
    const char*            factoryMethodName_;
    Lifetime               lifetime_;
    Origin                 origin_;
    const BeanReflectiveData* reflective_;
};

} // namespace CTORIUM_NAMESPACE
