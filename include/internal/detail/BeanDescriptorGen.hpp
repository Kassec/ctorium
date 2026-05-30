#pragma once

#ifndef CTORIUM_DYNAMIC_LINK

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../ContributedDescriptor.hpp"
#include "../Discovery.hpp"
#include "../Identity.hpp"
#include "../Lifetime.hpp"
#include "../NameId.hpp"
#include "../Origin.hpp"
#include "../TypeInfoGetter.hpp"
#include "../../api/ctr/Bean.hpp"
#include "../../api/ctr/BeanMetadata.hpp"
#include "../../api/ctr/Errors.hpp"
#include "../../api/ctr/Markers.hpp"
#include "ResolutionContext.hpp"

namespace ctr::detail {

// ─────────────────────────────────────────────────────────────────────────────
// §3.1  FNV-1a 64-bit — consteval
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
/// O(D) character work (D = namespace depth) — collects segments then builds forward.
/// Returns true when `e` is a template specialization.
/// GCC 16.1.0 P2996: template_arguments_of throws std::meta::exception for
/// non-specializations (see docs/gcc-P2996R13.md §3).  try/catch is valid in
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

    // Non-template: collect segments in reverse order (innermost first).
    std::vector<std::string_view> parts;
    parts.push_back(std::meta::identifier_of(entity));

    // B1: walk up parent chain covering both namespaces AND enclosing classes.
    auto parent = std::meta::parent_of(entity);
    while (std::meta::is_namespace(parent)
           || (std::meta::is_type(parent) && std::meta::is_class_type(parent))) {
        if (!std::meta::has_identifier(parent)) break; // anonymous/global or unnamed class
        std::string_view seg;
        if (std::meta::is_type(parent) && std::meta::is_class_type(parent)
                && isTemplateSpecialization(parent)) {
            // Template class parent: display_string_of is fully qualified.
            // Append it as the final outer prefix and stop the loop.
            seg = std::meta::display_string_of(parent);
            if (seg.empty()) break;
            parts.push_back(seg);
            break; // outer name is already fully qualified
        }
        seg = std::meta::identifier_of(parent);
        if (seg.empty()) break; // global namespace
        parts.push_back(seg);
        parent = std::meta::parent_of(parent);
    }
    // Pre-compute total length and build outermost → innermost in one pass.
    std::size_t total = 0;
    for (std::string_view sv : parts) total += sv.size();
    if (parts.size() > 1) total += (parts.size() - 1) * 2; // "::" separators
    std::string result;
    result.reserve(total);
    for (std::size_t i = parts.size(); i > 0; --i) {
        if (i != parts.size()) result += "::";
        result += parts[i - 1];
    }
    return std::define_static_string(std::string_view(result));
}

// ─────────────────────────────────────────────────────────────────────────────
// §3.3  Identity computation — consteval
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
// §4  Annotation extraction — single-pass consteval scan (D5)
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
        if (std::meta::is_same_type(t, ^^ctr::singleton)) {
            if (r.hasLifetimeMarker) r.lifetimeConflict = true;
            r.hasLifetimeMarker = true;
            const auto v = std::meta::extract<ctr::singleton>(ann);
            r.lifetime = Lifetime::Singleton; r.priority = v.priority; r.lazy = v.lazy;
        } else if (std::meta::is_same_type(t, ^^ctr::prototype)) {
            if (r.hasLifetimeMarker) r.lifetimeConflict = true;
            r.hasLifetimeMarker = true;
            const auto v = std::meta::extract<ctr::prototype>(ann);
            r.lifetime = Lifetime::Prototype; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^ctr::session)) {
            if (r.hasLifetimeMarker) r.lifetimeConflict = true;
            r.hasLifetimeMarker = true;
            const auto v = std::meta::extract<ctr::session>(ann);
            r.lifetime = Lifetime::Session; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^ctr::threadLocal)) {
            if (r.hasLifetimeMarker) r.lifetimeConflict = true;
            r.hasLifetimeMarker = true;
            const auto v = std::meta::extract<ctr::threadLocal>(ann);
            r.lifetime = Lifetime::ThreadLocal; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^ctr::named)) {
            const char* n = std::meta::extract<ctr::named>(ann).name;
            if (n == nullptr || n[0] == '\0') r.emptyNameError = true; // B8
            r.beanName = std::define_static_string(
                std::string_view{(n != nullptr) ? n : ""});
        }
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.0  Polymorphic-exposure helpers (SPEC-polymorphic-exposure)
// ─────────────────────────────────────────────────────────────────────────────

// No-op thunks for alias descriptors (never called; alias → primary redirect).
inline void noopConstruct(void*, void*) noexcept {}
inline void noopDestroy(void*) noexcept {}

// Upcast thunk: concrete* → base*.  Valid for all base types (virtual or not).
template<typename Concrete, typename Base>
void* adjustToExposedThunk(void* p) {
    return static_cast<void*>(static_cast<Base*>(static_cast<Concrete*>(p)));
}

// Downcast thunk: base* → concrete*.  Only instantiated for non-virtual bases;
// the if constexpr guard in makeExposedDescriptors prevents instantiation when
// IsVirtual = true (static_cast<Concrete*>(virtual_base*) is ill-formed).
template<typename Concrete, typename Base>
void* adjustToConcreteThunk(void* p) noexcept {
    return static_cast<void*>(static_cast<Concrete*>(static_cast<Base*>(p)));
}

// Identity for exposed (alias) descriptors; includes both base and concrete names
// so that two different concretes exposing the same base hash differently.
consteval Identity computeIdentityForExposedType(
        const char* baseName,
        const char* concreteName,
        std::string_view beanName,
        Lifetime lifetime) {
    auto h = fnv1a64Byte(2u, 14695981039346656037ULL); // kind = 2 (exposed alias)
    h = fnv1a64(baseName, h);
    h = fnv1a64(";", h);
    h = fnv1a64(concreteName, h);
    h = fnv1a64(";", h);
    h = fnv1a64(beanName, h);
    h = fnv1a64(";", h);
    h = fnv1a64Byte(static_cast<std::uint8_t>(lifetime), h);
    return h;
}

// Generates alias ContributedDescriptors for every accessible direct public base
// of the annotated type referenced by `entity`.
template<DiscoveredEntity entity>
consteval void makeExposedDescriptors(std::vector<ContributedDescriptor>& result) {
    static_assert(entity.kind == EntityKind::AnnotatedType);
    using T = [:entity.entity:];

    constexpr auto ann = scanAnnotations(entity.entity);
    constexpr const char* concreteName = qualifiedNameOf(entity.entity);
    constexpr Identity primaryIdentity =
        computeIdentityForType(concreteName, ann.beanName, ann.lifetime);

    static constexpr auto kBases =
        std::define_static_array(
            std::meta::bases_of(entity.entity, std::meta::access_context::unchecked()));

    template for (constexpr auto base_rel : kBases) {
        if constexpr (std::meta::is_public(base_rel)) {
            constexpr auto baseTypeInfo = std::meta::dealias(std::meta::type_of(base_rel));
            using Base = [:baseTypeInfo:];
            constexpr const char* baseName = qualifiedNameOf(baseTypeInfo);
            // is_virtual_base_of_type(base, derived): true if base is a virtual base of derived.
            constexpr bool kIsVirtual = std::meta::is_virtual_base_of_type(baseTypeInfo, entity.entity);

            ContributedDescriptor cd;
            cd.identity            = computeIdentityForExposedType(
                                         baseName, concreteName, ann.beanName, ann.lifetime);
            cd.exposedTypeName     = baseName;
            cd.concreteTypeName    = concreteName;
            cd.exposedTypeInfo     = &TypeInfoGetter<Base>::get;
            cd.concreteTypeInfo    = &TypeInfoGetter<T>::get;
            cd.beanName            = ann.beanName;
            cd.priority            = ann.priority;
            cd.lazy                = ann.lazy;
            cd.lifetime            = ann.lifetime;
            cd.origin              = Origin::AnnotatedType;
            cd.construct           = &noopConstruct;
            cd.destroy             = &noopDestroy;
            cd.postConstruct       = nullptr;
            cd.preDestroy          = nullptr;
            cd.size                = 0;
            cd.align               = 1;
            cd.factoryMethodIdentity = kNoFactoryMethod;
            cd.isExposedAlias      = true;
            cd.aliasOfPrimaryIdentity = primaryIdentity;
            cd.adjustToExposed     = &adjustToExposedThunk<T, Base>;
            if constexpr (kIsVirtual) {
                cd.adjustToConcrete = nullptr; // virtual base: downcast ill-formed
            } else {
                cd.adjustToConcrete = &adjustToConcreteThunk<T, Base>;
            }
            result.push_back(std::move(cd));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.1  destroyThunk
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
void destroyThunk(void* mem) noexcept {
    static_cast<T*>(mem)->~T();
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.2  constructThunkDefault
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
void constructThunkDefault(void* mem, void* /*vctx*/) {
    new (mem) T();
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.3  injectParam — per-parameter injection helper
// ─────────────────────────────────────────────────────────────────────────────

template<typename T, std::meta::info Ctor, std::size_t ParamIndex>
auto injectParam(ResolutionContext& ctx) {
    static constexpr auto kParams =
        std::define_static_array(std::meta::parameters_of(Ctor));
    static constexpr auto kParamInfo = kParams[ParamIndex];
    static constexpr auto kParamType =
        std::meta::dealias(std::meta::type_of(kParamInfo));
    static constexpr auto kBeanArg =
        std::meta::template_arguments_of(kParamType)[0];
    using U = [:kBeanArg:];

    static constexpr auto kAnns =
        std::define_static_array(std::meta::annotations_of(kParamInfo));

    static constexpr bool kHasScoped = []{
        template for (constexpr auto ann : kAnns) {
            if constexpr (std::meta::is_same_type(
                    std::meta::remove_const(std::meta::type_of(ann)), ^^ctr::scoped)) {
                return true;
            }
        }
        return false;
    }();

    static constexpr bool kHasNamed = []{
        template for (constexpr auto ann : kAnns) {
            if constexpr (std::meta::is_same_type(
                    std::meta::remove_const(std::meta::type_of(ann)), ^^ctr::named)) {
                return true;
            }
        }
        return false;
    }();

    if constexpr (kHasScoped) {
        // Scoped injection (specs-internal §10.3): produce a deferred Form 2 handle
        // targeting the named scope.  No eager resolution at construction time.

        // Extract scope name at consteval time.
        static constexpr const char* kScopeNameStr = []{
            template for (constexpr auto ann : kAnns) {
                if constexpr (std::meta::is_same_type(
                        std::meta::remove_const(std::meta::type_of(ann)), ^^ctr::scoped)) {
                    return std::meta::extract<ctr::scoped>(ann).name;
                }
            }
            return static_cast<const char*>(nullptr);
        }();

        // Intern scope name via the per-call-site 64-bit packed cache.
        static std::atomic<std::uint64_t> scopeCache{0};
        const NameId scopeNameId =
            ctx.registry.internScopeNameCached(kScopeNameStr, scopeCache);

        // Candidate name: from [[=ctr::named]] if also present; kUnnamed otherwise.
        NameId candidateNameId = kUnnamed;
        if constexpr (kHasNamed) {
            static constexpr const char* kCandidateNameStr = []{
                template for (constexpr auto ann : kAnns) {
                    if constexpr (std::meta::is_same_type(
                            std::meta::remove_const(std::meta::type_of(ann)), ^^ctr::named)) {
                        return std::meta::extract<ctr::named>(ann).name;
                    }
                }
                return static_cast<const char*>(nullptr);
            }();
            static std::atomic<std::uint64_t> candidateCache{0};
            const std::uint64_t ce  = candidateCache.load(std::memory_order_relaxed);
            const std::uint32_t rid = ctx.registry.registryId();
            if ((ce >> 32) == static_cast<std::uint64_t>(rid)) {
                candidateNameId = static_cast<NameId>(ce & 0xFFFF'FFFFu);
            } else {
                candidateNameId = ctx.registry.nameInterning().lookup(
                    kCandidateNameStr ? std::string_view{kCandidateNameStr}
                                      : std::string_view{});
                if (candidateNameId == kInvalidNameId) {
                    throw ctr::ResolutionError(
                        std::string("injectParam: unknown named qualifier '")
                        + std::string(kCandidateNameStr ? kCandidateNameStr : "") + "'.");
                }
                candidateCache.store(
                    (static_cast<std::uint64_t>(rid) << 32)
                    | static_cast<std::uint64_t>(candidateNameId),
                    std::memory_order_relaxed);
            }
        }

        return Registry::makeDeferredHandle<U>(scopeNameId, candidateNameId, &ctx.registry);
    } else if constexpr (kHasNamed) {
        // Named injection: intern name and resolve.
        static constexpr const char* kNameStr = []{
            template for (constexpr auto ann : kAnns) {
                if constexpr (std::meta::is_same_type(
                        std::meta::remove_const(std::meta::type_of(ann)), ^^ctr::named)) {
                    return std::meta::extract<ctr::named>(ann).name;
                }
            }
            return static_cast<const char*>(nullptr);
        }();
        // Cache: upper 32 bits = registry ID, lower 32 bits = NameId.
        static std::atomic<std::uint64_t> nameCache{0};
        const std::uint64_t e   = nameCache.load(std::memory_order_relaxed);
        const std::uint32_t rid = ctx.registry.registryId();
        NameId nid;
        if ((e >> 32) == static_cast<std::uint64_t>(rid)) {
            nid = static_cast<NameId>(e & 0xFFFF'FFFFu);
        } else {
            nid = ctx.registry.nameInterning().lookup(
                kNameStr ? std::string_view{kNameStr} : std::string_view{});
            if (nid == kInvalidNameId) {
                throw ctr::ResolutionError(
                    std::string("injectParam: unknown named qualifier '")
                    + std::string(kNameStr ? kNameStr : "") + "'.");
            }
            nameCache.store(
                (static_cast<std::uint64_t>(rid) << 32)
                | static_cast<std::uint64_t>(nid),
                std::memory_order_relaxed);
        }
        return ctx.registry.resolve<U>(nid, ctx);
    } else {
        // Unnamed injection: resolve with default NameId lookup.
        return ctx.registry.resolve<U>(kUnnamed, ctx);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.3b  makeParamDescriptors<Ctor> — builds ContributedParamDescriptor array
//
// Consteval helper: one entry per injectable constructor parameter, recording:
//   - injectedTypeName/injectedTypeInfo: the U type in Bean<U>
//   - scopeName: from [[=ctr::scoped{.name=...}]] or nullptr
//   - namedKey:  from [[=ctr::named{.name=...}]] or nullptr
// Used by Registry::start() Phase 3.75 for graph validation.
// ─────────────────────────────────────────────────────────────────────────────

template<std::meta::info Ctor>
consteval std::vector<ContributedParamDescriptor> makeParamDescriptors() {
    static constexpr auto kParams =
        std::define_static_array(std::meta::parameters_of(Ctor));
    std::vector<ContributedParamDescriptor> result;
    result.reserve(kParams.size());
    template for (constexpr auto p : kParams) {
        constexpr auto paramType = std::meta::dealias(std::meta::type_of(p));
        constexpr auto beanArg   = std::meta::template_arguments_of(paramType)[0];
        using U = [:beanArg:];
        // Detect scoped/named presence and extract the scope name.
        // Extracting a const char* annotation member is supported (toolchain probe
        // MetaExtract); the scope name is normalized through define_static_string so
        // the pointer has static storage and is template-argument-equivalent,
        // mirroring scanAnnotations() for beanName.
        static constexpr auto panns = std::define_static_array(std::meta::annotations_of(p));
        bool hasScopedAnn = false;
        bool hasNamedAnn  = false;
        const char* scopeName = std::define_static_string(std::string_view{""});
        template for (constexpr auto ann : panns) {
            constexpr auto t = std::meta::remove_const(std::meta::type_of(ann));
            if constexpr (std::meta::is_same_type(t, ^^ctr::scoped)) {
                hasScopedAnn = true;
                constexpr auto v = std::meta::extract<ctr::scoped>(ann);
                scopeName = std::define_static_string(
                    std::string_view{(v.name != nullptr) ? v.name : ""});
            } else if constexpr (std::meta::is_same_type(t, ^^ctr::named)) {
                hasNamedAnn = true;
            }
        }
        result.push_back({
            qualifiedNameOf(beanArg),
            &TypeInfoGetter<U>::get,
            hasScopedAnn,
            hasNamedAnn,
            scopeName
        });
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.3  constructThunkInjected — injectable constructor thunk
// ─────────────────────────────────────────────────────────────────────────────

// Pack-expansion helper: not a lambda, so P2564 §13b.1 cannot promote it.
// GCC 16 aggressively promotes lambdas that contain calls to functions with
// std::meta::info NTTPs (P3603R0 consteval-only type), even when 'constexpr'
// is added to the call operator.  A named function template is not subject to
// §13b.1 and therefore stays callable at runtime.
template<typename T, std::meta::info Ctor, std::size_t... Is>
void constructThunkInjectedImpl(void* mem, ResolutionContext& ctx,
                                 std::index_sequence<Is...>) {
    new (mem) T(injectParam<T, Ctor, Is>(ctx)...);
}

template<typename T, std::meta::info Ctor>
void constructThunkInjected(void* mem, void* vctx) {
    auto& ctx = *static_cast<ResolutionContext*>(vctx);
    static constexpr auto kParams =
        std::define_static_array(std::meta::parameters_of(Ctor));
    constructThunkInjectedImpl<T, Ctor>(
        mem, ctx, std::make_index_sequence<kParams.size()>{});
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.4  isBeanType — predicate used by scanMembers
// ─────────────────────────────────────────────────────────────────────────────

consteval bool isBeanType(std::meta::info paramType) {
    const auto d = std::meta::dealias(paramType);
    // template_arguments_of precondition: d must be a class-type template specialisation.
    // Guard with is_class_type to avoid precondition violations on reference/pointer/
    // primitive params (e.g. copy/move-ctor params like const T& or T&&).
    if (!std::meta::is_type(d) || !std::meta::is_class_type(d)) return false;
    const auto args = std::meta::template_arguments_of(d);
    if (args.size() != 1) return false;
    return std::meta::template_of(d) == ^^ctr::Bean;
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.5  postConstruct / preDestroy thunks
// ─────────────────────────────────────────────────────────────────────────────

template<typename T, std::meta::info Method>
void postConstructThunkImpl(void* instance, void* vctx) {
    auto& ctx = *static_cast<ResolutionContext*>(vctx);
    static constexpr auto kParams =
        std::define_static_array(std::meta::parameters_of(Method));
    T* obj = static_cast<T*>(instance);
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        constexpr auto pmf = &[:Method:];
        (obj->*pmf)(injectParam<T, Method, Is>(ctx)...);
    }(std::make_index_sequence<kParams.size()>{});
}

template<typename T, std::meta::info Method>
void preDestroyThunkImpl(void* instance, void* vctx) {
    auto& ctx = *static_cast<ResolutionContext*>(vctx);
    static constexpr auto kParams =
        std::define_static_array(std::meta::parameters_of(Method));
    T* obj = static_cast<T*>(instance);
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        constexpr auto pmf = &[:Method:];
        (obj->*pmf)(injectParam<T, Method, Is>(ctx)...);
    }(std::make_index_sequence<kParams.size()>{});
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.4+5.5  scanMembers<Type>() — single-pass member scan (D4)
//
// Combines findCompatibleCtor<T>() and the two findHookMethod calls into one
// members_of traversal per type, instead of three separate calls.
// ─────────────────────────────────────────────────────────────────────────────

struct MemberScan {
    std::meta::info ctor{};          ///< Compatible constructor; info{} if absent.
    std::meta::info postConstruct{}; ///< postConstruct hook; info{} if absent.
    std::meta::info preDestroy{};    ///< preDestroy hook; info{} if absent.
};

template<std::meta::info Type>
consteval MemberScan scanMembers() {
    static constexpr auto kMembers =
        std::define_static_array(
            std::meta::members_of(Type, std::meta::access_context::unchecked()));
    MemberScan r;
    template for (constexpr auto m : kMembers) {
        if constexpr (std::meta::is_constructor(m)) {
            static constexpr auto kParams =
                std::define_static_array(std::meta::parameters_of(m));
            constexpr bool compatible = []{
                if constexpr (kParams.size() == 0) return true;
                template for (constexpr auto p : kParams) {
                    if constexpr (!isBeanType(std::meta::type_of(p))) return false;
                }
                return true;
            }();
            if constexpr (compatible) { r.ctor = m; }
        } else if constexpr (!std::meta::is_type(m)
                          && !std::meta::is_special_member_function(m)) {
            constexpr bool isPostConstruct = []{
                for (auto ann : std::meta::annotations_of(m)) {
                    if (std::meta::is_same_type(
                            std::meta::remove_const(std::meta::type_of(ann)),
                            ^^ctr::postConstruct)) {
                        return true;
                    }
                }
                return false;
            }();
            constexpr bool isPreDestroy = []{
                for (auto ann : std::meta::annotations_of(m)) {
                    if (std::meta::is_same_type(
                            std::meta::remove_const(std::meta::type_of(ann)),
                            ^^ctr::preDestroy)) {
                        return true;
                    }
                }
                return false;
            }();
            if constexpr (isPostConstruct) { r.postConstruct = m; }
            if constexpr (isPreDestroy)    { r.preDestroy = m; }
        }
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// §6.3 helpers — factory product thunks
// ─────────────────────────────────────────────────────────────────────────────

// Unwraps unique_ptr<T> → T; returns returnType unchanged otherwise.
consteval std::meta::info unwrapUniquePtr(std::meta::info returnType) {
    const auto d = std::meta::dealias(returnType);
    const auto args = std::meta::template_arguments_of(d);
    if (args.size() == 1) {
        const auto tmpl = std::meta::template_of(d);
        if (tmpl == ^^std::unique_ptr) {
            return args[0];
        }
    }
    return returnType;
}

// Standard construct thunk: placement-new from factory call (value return or
// singleton unique_ptr path — the engine pre-allocates mem).
template<typename T, std::meta::info FactoryType, std::meta::info Method>
void constructFactoryProductThunk(void* mem, void* vctx) {
    auto& ctx = *static_cast<ResolutionContext*>(vctx);
    using Factory = [:FactoryType:];
    auto factoryBean = ctx.registry.resolve<Factory>(kUnnamed, ctx);
    constexpr auto pmf = &[:Method:];
    Factory* fp = factoryBean.operator->();
    if constexpr (std::meta::is_same_type(
            std::meta::dealias(std::meta::return_type_of(Method)),
            std::meta::dealias(^^std::unique_ptr<T>))) {
        auto ptr = (fp->*pmf)();
        new (mem) T(std::move(*ptr));
    } else {
        new (mem) T((fp->*pmf)());
    }
}

// Auto-allocating thunk for unique_ptr<T> factory products with Prototype lifetime.
// Calls the factory method, releases the unique_ptr, and returns the raw pointer.
// No pre-allocated mem involved; allocation is performed by the factory's operator new.
// Used only by the prototype path of resolve<T> when allocAndConstruct != nullptr.
template<typename T, std::meta::info FactoryType, std::meta::info Method>
void* allocAndConstructFactoryProductThunk(void* vctx) {
    auto& ctx = *static_cast<ResolutionContext*>(vctx);
    using Factory = [:FactoryType:];
    auto factoryBean = ctx.registry.resolve<Factory>(kUnnamed, ctx);
    constexpr auto pmf = &[:Method:];
    Factory* fp = factoryBean.operator->();
    return (fp->*pmf)().release(); // unique_ptr<T>: transfer ownership; never throws
}

// Deallocation thunk for unique_ptr<T> factory products.
// Called by executeDestructionLifecycle after destroy (i.e. ~T() already ran).
// Routes through T::operator delete if T defines one (e.g. custom allocators).
// Deviation from spec literal (`delete static_cast<T*>(p)`): that would double-destroy
// since destroy already called ~T(). T::operator delete(p) performs only deallocation.
template<typename T>
void deallocFactoryProductThunk(void* p) noexcept {
    T::operator delete(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.8  makeReflectiveData — compile-time projection of method metadata
//
// Generates a BeanReflectiveData struct (static lifetime) for the annotated type
// when retainAllMetadata = true.  Scans all non-constructor, non-special-member,
// non-type members and records name, Ctorium annotation presence, and param count.
// ─────────────────────────────────────────────────────────────────────────────

template<std::meta::info Type>
consteval const ctr::BeanReflectiveData* makeReflectiveData() {
    static constexpr auto kMembers =
        std::define_static_array(
            std::meta::members_of(Type, std::meta::access_context::unchecked()));

    // Count qualifying methods first.
    constexpr std::size_t kCount = []{
        std::size_t n = 0;
        template for (constexpr auto m : kMembers) {
            if constexpr (!std::meta::is_type(m)
                       && !std::meta::is_special_member_function(m)
                       && !std::meta::is_constructor(m)) {
                ++n;
            }
        }
        return n;
    }();

    if constexpr (kCount == 0) {
        return nullptr;
    } else {
        // Build array of BeanMethodRecord.
        constexpr auto kRecords = []{
            std::array<ctr::BeanMethodRecord, kCount> arr{};
            std::size_t idx = 0;
            template for (constexpr auto m : kMembers) {
                if constexpr (!std::meta::is_type(m)
                           && !std::meta::is_special_member_function(m)
                           && !std::meta::is_constructor(m)) {
                    bool isPC = false, isPD = false;
                    template for (constexpr auto ann : std::define_static_array(std::meta::annotations_of(m))) {
                        constexpr auto t = std::meta::remove_const(std::meta::type_of(ann));
                        if constexpr (std::meta::is_same_type(t, ^^ctr::postConstruct)) isPC = true;
                        if constexpr (std::meta::is_same_type(t, ^^ctr::preDestroy))    isPD = true;
                    }
                    // parameters_of throws for non-function members (e.g. data fields).
                    std::size_t pcount = 0;
                    try { pcount = std::meta::parameters_of(m).size(); } catch (...) {}
                    arr[idx++] = {
                        std::define_static_string(std::meta::identifier_of(m)),
                        isPC,
                        isPD,
                        pcount
                    };
                }
            }
            return arr;
        }();

        static constexpr auto kStaticRecords = kRecords; // static lifetime

        static constexpr ctr::BeanReflectiveData kData{
            kStaticRecords.data(),
            kCount
        };
        return &kData;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// §6.1  makeDescriptorForAnnotatedType
// ─────────────────────────────────────────────────────────────────────────────

template<DiscoveredEntity entity, bool RetainMeta = false>
consteval ContributedDescriptor makeDescriptorForAnnotatedType() {
    static_assert(entity.kind == EntityKind::AnnotatedType);
    using T = [:entity.entity:];

    // Single-pass annotation + member scans: 1 traversal each (D4+D5).
    constexpr auto ann     = scanAnnotations(entity.entity);
    static_assert(!ann.lifetimeConflict,
        "Ctorium: multiple lifetime annotations on the same type are invalid "
        "(specs-api §7 step 6 condition 1).");
    static_assert(!ann.emptyNameError,
        "Ctorium: ctr::named annotation with an empty key is invalid (specs-api §4).");
    constexpr auto members = scanMembers<entity.entity>();
    // qualifiedNameOf computed once and reused for identity + type names (D1).
    constexpr const char* typeName = qualifiedNameOf(entity.entity);

    constexpr bool hasParams = (members.ctor != std::meta::info{})
        && (std::meta::parameters_of(members.ctor).size() > 0);

    void (*constructFn)(void*, void*);
    if constexpr (hasParams) {
        constructFn = &constructThunkInjected<T, members.ctor>;
    } else {
        constructFn = &constructThunkDefault<T>;
    }

    void (*postConstructFn)(void*, void*) = nullptr;
    if constexpr (members.postConstruct != std::meta::info{}) {
        postConstructFn = &postConstructThunkImpl<T, members.postConstruct>;
    }

    void (*preDestroyFn)(void*, void*) = nullptr;
    if constexpr (members.preDestroy != std::meta::info{}) {
        preDestroyFn = &preDestroyThunkImpl<T, members.preDestroy>;
    }

    // Build param descriptors for graph validation (session/scoped checks in start()).
    const ContributedParamDescriptor* paramPtr = nullptr;
    std::size_t paramCnt = 0;
    if constexpr (hasParams) {
        constexpr auto kParamDescs =
            std::define_static_array(makeParamDescriptors<members.ctor>());
        paramPtr = kParamDescs.data();
        paramCnt = kParamDescs.size();
    }

    return ContributedDescriptor{
        .identity          = computeIdentityForType(typeName, ann.beanName, ann.lifetime),
        .exposedTypeName   = typeName,
        .concreteTypeName  = typeName,
        .exposedTypeInfo   = &TypeInfoGetter<T>::get,
        .concreteTypeInfo  = &TypeInfoGetter<T>::get,
        .beanName          = ann.beanName,
        .priority          = ann.priority,
        .lazy              = ann.lazy,
        .lifetime          = ann.lifetime,
        .origin            = Origin::AnnotatedType,
        .construct         = constructFn,
        .destroy           = &destroyThunk<T>,
        .postConstruct     = postConstructFn,
        .preDestroy        = preDestroyFn,
        .size              = sizeof(T),
        .align             = alignof(T),
        .factoryMethodIdentity = kNoFactoryMethod,
        .params            = paramPtr,
        .paramCount        = paramCnt,
        .reflectiveData    = RetainMeta ? makeReflectiveData<entity.entity>() : nullptr,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// §6.2  makeDescriptorForFactory
// ─────────────────────────────────────────────────────────────────────────────

template<DiscoveredEntity entity>
consteval ContributedDescriptor makeDescriptorForFactory() {
    static_assert(entity.kind == EntityKind::Factory);
    using T = [:entity.entity:];

    // Factory lifetime is always Singleton; no annotation scan needed.
    // Member scan to find compatible constructor (D4).
    constexpr auto members   = scanMembers<entity.entity>();
    constexpr const char* typeName = qualifiedNameOf(entity.entity); // D1: compute once
    constexpr Lifetime lifetime = Lifetime::Singleton;
    constexpr bool hasParams = (members.ctor != std::meta::info{})
        && (std::meta::parameters_of(members.ctor).size() > 0);

    void (*constructFn)(void*, void*);
    if constexpr (hasParams) {
        constructFn = &constructThunkInjected<T, members.ctor>;
    } else {
        constructFn = &constructThunkDefault<T>;
    }

    return ContributedDescriptor{
        .identity          = computeIdentityForType(typeName, "", lifetime),
        .exposedTypeName   = typeName,
        .concreteTypeName  = typeName,
        .exposedTypeInfo   = &TypeInfoGetter<T>::get,
        .concreteTypeInfo  = &TypeInfoGetter<T>::get,
        .beanName          = std::define_static_string(std::string_view{""}),
        .priority          = 0,
        .lifetime          = lifetime,
        .origin            = Origin::AnnotatedType,
        .construct         = constructFn,
        .destroy           = &destroyThunk<T>,
        .postConstruct     = nullptr,
        .preDestroy        = nullptr,
        .size              = sizeof(T),
        .align             = alignof(T),
        .factoryMethodIdentity = kNoFactoryMethod,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// §6.3  makeDescriptorForProduct
// ─────────────────────────────────────────────────────────────────────────────

template<DiscoveredEntity entity>
consteval ContributedDescriptor makeDescriptorForProduct() {
    static_assert(entity.kind == EntityKind::FactoryProduct);

    constexpr auto rawReturn   = std::meta::return_type_of(entity.entity);
    constexpr auto productType = unwrapUniquePtr(rawReturn);
    using T = [:productType:];

    // Single-pass annotation scan on the producer method (D5).
    constexpr auto ann         = scanAnnotations(entity.entity);
    static_assert(!ann.lifetimeConflict,
        "Ctorium: multiple lifetime annotations on the same factory product are invalid "
        "(specs-api §7 step 6 condition 1).");
    static_assert(!ann.emptyNameError,
        "Ctorium: ctr::named annotation with an empty key is invalid (specs-api §4).");
    // Single-pass member scan on the product type for hooks (D4).
    constexpr auto members     = scanMembers<productType>();
    // qualifiedNameOf computed once per entity (D1).
    constexpr const char* productName = qualifiedNameOf(productType);
    constexpr const char* factoryName = qualifiedNameOf(entity.declaringFactory);

    constexpr auto factoryIdentity = computeIdentityForType(factoryName, "", Lifetime::Singleton);

    void (*postConstructFn)(void*, void*) = nullptr;
    if constexpr (members.postConstruct != std::meta::info{}) {
        postConstructFn = &postConstructThunkImpl<T, members.postConstruct>;
    }

    void (*preDestroyFn)(void*, void*) = nullptr;
    if constexpr (members.preDestroy != std::meta::info{}) {
        preDestroyFn = &preDestroyThunkImpl<T, members.preDestroy>;
    }

    // Detect whether the return type is unique_ptr<T>.
    constexpr bool isUniquePtrReturn = std::meta::is_same_type(
        std::meta::dealias(rawReturn), std::meta::dealias(^^std::unique_ptr<T>));

    // Fill allocAndConstruct + dealloc only for Prototype+unique_ptr:
    // – Prototype: allocation happens per-resolve, single allocation matters.
    // – Singleton: the engine pre-allocates; construct handles the unique_ptr path.
    constexpr bool useAllocAndConstruct =
        isUniquePtrReturn && (ann.lifetime == Lifetime::Prototype);

    void* (*allocAndConstructFn)(void*)        = nullptr;
    void  (*deallocFn)(void*) noexcept         = nullptr;
    if constexpr (useAllocAndConstruct) {
        allocAndConstructFn =
            &allocAndConstructFactoryProductThunk<T, entity.declaringFactory, entity.entity>;
        deallocFn = &deallocFactoryProductThunk<T>;
    }

    return ContributedDescriptor{
        .identity          = computeIdentityForProduct(entity.entity, factoryName, ann.beanName, ann.lifetime),
        .exposedTypeName   = productName,
        .concreteTypeName  = productName,
        .exposedTypeInfo   = &TypeInfoGetter<T>::get,
        .concreteTypeInfo  = &TypeInfoGetter<T>::get,
        .beanName          = ann.beanName,
        .priority          = ann.priority,
        .lifetime          = ann.lifetime,
        .origin            = Origin::FactoryProduct,
        .construct         = &constructFactoryProductThunk<T, entity.declaringFactory, entity.entity>,
        .destroy           = &destroyThunk<T>,
        .postConstruct     = postConstructFn,
        .preDestroy        = preDestroyFn,
        .size              = sizeof(T),
        .align             = alignof(T),
        .factoryMethodIdentity = factoryIdentity,
        .allocAndConstruct = allocAndConstructFn,
        .dealloc           = deallocFn,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// §7  makeAllDescriptors
// ─────────────────────────────────────────────────────────────────────────────

template<bool RetainMeta, auto... Roots>
consteval std::vector<ContributedDescriptor> makeAllDescriptors() {
    static constexpr auto kEntities =
        std::define_static_array(enumerateDiscovery<Roots...>());
    std::vector<ContributedDescriptor> result;
    result.reserve(kEntities.size());
    template for (constexpr auto entity : kEntities) {
        if constexpr (entity.kind == EntityKind::AnnotatedType) {
            result.push_back(makeDescriptorForAnnotatedType<entity, RetainMeta>());
            // Also generate alias descriptors for each accessible direct public base.
            makeExposedDescriptors<entity>(result);
        } else if constexpr (entity.kind == EntityKind::Factory) {
            result.push_back(makeDescriptorForFactory<entity>());
        } else if constexpr (entity.kind == EntityKind::FactoryProduct) {
            result.push_back(makeDescriptorForProduct<entity>());
        }
    }
    return result;
}

} // namespace ctr::detail

#endif // CTORIUM_DYNAMIC_LINK
