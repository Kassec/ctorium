#pragma once

#include "../../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {

consteval std::string_view compatibleConstructorConflictMessage(std::string_view typeName) {
    constexpr std::string_view kPrefix = "Ctorium: type ";
    constexpr std::string_view kSuffix =
        " declares multiple compatible constructors for injection.";

    std::vector<char> buffer;
    buffer.reserve(kPrefix.size() + typeName.size() + kSuffix.size());
    for (char c : kPrefix) buffer.push_back(c);
    for (char c : typeName) buffer.push_back(c);
    for (char c : kSuffix) buffer.push_back(c);

    return std::string_view{
        std::define_static_string(std::string_view{buffer.data(), buffer.size()}),
        buffer.size()
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// makeDescriptorForAnnotatedType
// ─────────────────────────────────────────────────────────────────────────────

template<DiscoveredEntity entity, bool RetainMeta = false>
consteval ContributedDescriptor makeDescriptorForAnnotatedType() {
    static_assert(entity.kind == EntityKind::AnnotatedType);
    using T = [:entity.entity:];

    // Single-pass annotation + member scans: 1 traversal each (D4+D5).
    constexpr auto ann     = scanAnnotations(entity.entity);
    static_assert(!ann.lifetimeConflict,
        "Ctorium: multiple lifetime annotations on the same type are invalid.");
    static_assert(!ann.emptyNameError,
        "Ctorium: ctr::named annotation with an empty key is invalid.");
    constexpr auto members = scanMembers<entity.entity>();
    // qualifiedNameOf computed once and reused for identity + type names (D1).
    constexpr const char* typeName = qualifiedNameOf(entity.entity);
    static_assert(!members.compatibleConstructorConflict,
        compatibleConstructorConflictMessage(typeName));

    constexpr bool hasParams = (members.ctor != std::meta::info{})
        && (std::meta::parameters_of(members.ctor).size() > 0);

    void (*constructFn)(void*, void*);
    if constexpr (hasParams) {
        constructFn = &constructThunkInjected<T, members.ctor>;
    } else {
        constructFn = &constructThunkDefault<T>;
    }

    void (*postConstructFn)(void*, void*) = nullptr;
    if constexpr (!members.postConstruct.empty()) {
        postConstructFn = &postConstructThunkImpl<T>;
    }

    void (*preDestroyFn)(void*, void*) = nullptr;
    if constexpr (!members.preDestroy.empty()) {
        preDestroyFn = &preDestroyThunkImpl<T>;
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
// makeDescriptorForFactory
// ─────────────────────────────────────────────────────────────────────────────

template<DiscoveredEntity entity>
consteval ContributedDescriptor makeDescriptorForFactory() {
    static_assert(entity.kind == EntityKind::Factory);
    using T = [:entity.entity:];

    // Factory lifetime is always Singleton; no annotation scan needed.
    // Member scan to find compatible constructor (D4).
    constexpr auto members   = scanMembers<entity.entity>();
    constexpr const char* typeName = qualifiedNameOf(entity.entity); // D1: compute once
    static_assert(!members.compatibleConstructorConflict,
        compatibleConstructorConflictMessage(typeName));
    constexpr Lifetime lifetime = Lifetime::Singleton;
    constexpr bool hasParams = (members.ctor != std::meta::info{})
        && (std::meta::parameters_of(members.ctor).size() > 0);

    void (*constructFn)(void*, void*);
    if constexpr (hasParams) {
        constructFn = &constructThunkInjected<T, members.ctor>;
    } else {
        constructFn = &constructThunkDefault<T>;
    }

    void (*postConstructFn)(void*, void*) = nullptr;
    if constexpr (!members.postConstruct.empty()) {
        postConstructFn = &postConstructThunkImpl<T>;
    }

    void (*preDestroyFn)(void*, void*) = nullptr;
    if constexpr (!members.preDestroy.empty()) {
        preDestroyFn = &preDestroyThunkImpl<T>;
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
        .postConstruct     = postConstructFn,
        .preDestroy        = preDestroyFn,
        .size              = sizeof(T),
        .align             = alignof(T),
        .factoryMethodIdentity = kNoFactoryMethod,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// makeDescriptorForProduct
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
        "Ctorium: multiple lifetime annotations on the same factory product are invalid.");
    static_assert(!ann.emptyNameError,
        "Ctorium: ctr::named annotation with an empty key is invalid.");
    // Single-pass member scan on the product type for hooks (D4).
    constexpr auto members     = scanMembers<productType>();
    // Single-pass annotation scan on the product type to check for Bean status.
    constexpr auto productAnn  = scanAnnotations(productType);
    // qualifiedNameOf computed once per entity (D1).
    constexpr const char* productName = qualifiedNameOf(productType);
    constexpr const char* factoryName = qualifiedNameOf(entity.declaringFactory);
    constexpr const char* factoryMethodName =
        std::define_static_string(std::meta::identifier_of(entity.entity));

    constexpr auto factoryIdentity = computeIdentityForType(factoryName, "", Lifetime::Singleton);

    void (*postConstructFn)(void*, void*) = nullptr;
    if constexpr (productAnn.hasLifetimeMarker && !members.postConstruct.empty()) {
        postConstructFn = &postConstructThunkImpl<T>;
    }

    void (*preDestroyFn)(void*, void*) = nullptr;
    if constexpr (productAnn.hasLifetimeMarker && !members.preDestroy.empty()) {
        preDestroyFn = &preDestroyThunkImpl<T>;
    }

    // Detect whether the return type is unique_ptr<T>.
    constexpr bool isUniquePtrReturn = isUniquePtrType(rawReturn);

    void* (*allocAndConstructFn)(void*)        = nullptr;
    void  (*deallocFn)(void*) noexcept         = nullptr;
    if constexpr (isUniquePtrReturn) {
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
        .factoryMethodName = factoryMethodName,
        .allocAndConstruct = allocAndConstructFn,
        .dealloc           = deallocFn,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// makeAllDescriptors
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

} // namespace CTORIUM_NAMESPACE::detail
