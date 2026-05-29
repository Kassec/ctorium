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
consteval const char* qualifiedNameOf(std::meta::info entity) {
    // Collect segments in reverse order (innermost first).
    std::vector<std::string_view> parts;
    parts.push_back(std::meta::identifier_of(entity));
    auto parent = std::meta::parent_of(entity);
    while (std::meta::is_namespace(parent)) {
        if (!std::meta::has_identifier(parent)) break; // anonymous/global namespace
        std::string_view parentId = std::meta::identifier_of(parent);
        if (parentId.empty()) break; // global namespace
        parts.push_back(parentId);
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
};

consteval AnnotationScan scanAnnotations(std::meta::info entity) {
    AnnotationScan r;
    r.beanName = std::define_static_string(std::string_view{""});
    for (auto ann : std::meta::annotations_of(entity)) {
        const auto t = std::meta::remove_const(std::meta::type_of(ann));
        if (std::meta::is_same_type(t, ^^ctr::singleton)) {
            const auto v = std::meta::extract<ctr::singleton>(ann);
            r.lifetime = Lifetime::Singleton; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^ctr::prototype)) {
            const auto v = std::meta::extract<ctr::prototype>(ann);
            r.lifetime = Lifetime::Prototype; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^ctr::session)) {
            const auto v = std::meta::extract<ctr::session>(ann);
            r.lifetime = Lifetime::Session; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^ctr::threadLocal)) {
            const auto v = std::meta::extract<ctr::threadLocal>(ann);
            r.lifetime = Lifetime::ThreadLocal; r.priority = v.priority;
        } else if (std::meta::is_same_type(t, ^^ctr::named)) {
            const char* n = std::meta::extract<ctr::named>(ann).name;
            r.beanName = std::define_static_string(
                std::string_view{(n != nullptr) ? n : ""});
        }
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// §5.1  destroyThunk
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
void destroyThunk(void* mem) {
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

    static constexpr bool kHasNamed = []{
        template for (constexpr auto ann : kAnns) {
            if constexpr (std::meta::is_same_type(
                    std::meta::remove_const(std::meta::type_of(ann)), ^^ctr::named)) {
                return true;
            }
        }
        return false;
    }();

    if constexpr (kHasNamed) {
        static constexpr auto kNameStr = []{
            template for (constexpr auto ann : kAnns) {
                if constexpr (std::meta::is_same_type(
                        std::meta::remove_const(std::meta::type_of(ann)), ^^ctr::named)) {
                    return std::meta::extract<ctr::named>(ann).name;
                }
            }
            return std::string_view{};
        }();
        // Cache: upper 32 bits = registry ID, lower 32 bits = NameId.
        // Correct across independent Registry instances (each has a unique ID).
        static std::atomic<std::uint64_t> nameCache{0};
        const std::uint64_t e   = nameCache.load(std::memory_order_relaxed);
        const std::uint32_t rid = ctx.registry.registryId();
        NameId nid;
        if ((e >> 32) == static_cast<std::uint64_t>(rid)) {
            nid = static_cast<NameId>(e & 0xFFFF'FFFFu);
        } else {
            nid = ctx.registry.nameInterning().intern(kNameStr);
            const std::uint64_t packed =
                (static_cast<std::uint64_t>(rid) << 32)
                | static_cast<std::uint64_t>(nid);
            nameCache.store(packed, std::memory_order_relaxed);
        }
        return ctx.registry.resolve<U>(nid, ctx);
    } else {
        return ctx.registry.resolve<U>(kUnnamed, ctx);
    }
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
// §6.1  makeDescriptorForAnnotatedType
// ─────────────────────────────────────────────────────────────────────────────

template<DiscoveredEntity entity>
consteval ContributedDescriptor makeDescriptorForAnnotatedType() {
    static_assert(entity.kind == EntityKind::AnnotatedType);
    using T = [:entity.entity:];

    // Single-pass annotation + member scans: 1 traversal each (D4+D5).
    constexpr auto ann     = scanAnnotations(entity.entity);
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

    return ContributedDescriptor{
        .identity          = computeIdentityForType(typeName, ann.beanName, ann.lifetime),
        .exposedTypeName   = typeName,
        .concreteTypeName  = typeName,
        .exposedTypeInfo   = &TypeInfoGetter<T>::get,
        .concreteTypeInfo  = &TypeInfoGetter<T>::get,
        .beanName          = ann.beanName,
        .priority          = ann.priority,
        .lifetime          = ann.lifetime,
        .origin            = Origin::AnnotatedType,
        .construct         = constructFn,
        .destroy           = &destroyThunk<T>,
        .postConstruct     = postConstructFn,
        .preDestroy        = preDestroyFn,
        .size              = sizeof(T),
        .align             = alignof(T),
        .factoryMethodIdentity = kNoFactoryMethod,
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

    void* (*allocAndConstructFn)(void*) = nullptr;
    void  (*deallocFn)(void*)           = nullptr;
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

template<auto... Roots>
consteval std::vector<ContributedDescriptor> makeAllDescriptors() {
    static constexpr auto kEntities =
        std::define_static_array(enumerateDiscovery<Roots...>());
    std::vector<ContributedDescriptor> result;
    result.reserve(kEntities.size());
    template for (constexpr auto entity : kEntities) {
        if constexpr (entity.kind == EntityKind::AnnotatedType) {
            result.push_back(makeDescriptorForAnnotatedType<entity>());
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
