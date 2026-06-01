#pragma once

namespace ctr::detail {
// ─────────────────────────────────────────────────────────────────────────────
// Polymorphic-exposure helpers
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
// destroyThunk
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
void destroyThunk(void* mem) noexcept {
    static_cast<T*>(mem)->~T();
}

// ─────────────────────────────────────────────────────────────────────────────
// constructThunkDefault
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
void constructThunkDefault(void* mem, void* /*vctx*/) {
    new (mem) T();
}

// ─────────────────────────────────────────────────────────────────────────────
// injectParam — per-parameter injection helper
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
        // Scoped injection: produce a deferred Form 2 handle
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

        const TypeId targetTypeId = ctx.registry.typeIdFor<U>();
        if (targetTypeId == kInvalidTypeId) {
            throw ctr::ResolutionError(
                "Registry::resolve: the requested type was not registered in "
                "this context via discover<>() or bindSingleton()."
                );
        }
        const auto* candidates =
            ctx.registry.typeIndex().candidatesFor(targetTypeId, candidateNameId);
        if (!candidates || candidates->empty()) {
            throw ctr::ResolutionError(
                "Registry::resolve: no bean registered for the requested "
                "type and named key."
                );
        }
        if (candidates->size() >= 2
            && ctx.registry.descriptorTable().at((*candidates)[0]).priority
                == ctx.registry.descriptorTable().at((*candidates)[1]).priority) {
            throw ctr::ResolutionError(
                "Registry::resolve: ambiguous resolution — two candidates share "
                "the highest priority for the requested type and named key."
                );
        }
        const DescriptorId descId = (*candidates)[0];
        return Registry::makeDeferredHandle<U>(scopeNameId, descId, &ctx.registry);
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
// makeParamDescriptors<Ctor> — builds ContributedParamDescriptor array
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
// constructThunkInjected — injectable constructor thunk
// ─────────────────────────────────────────────────────────────────────────────

// Pack-expansion helper: not a lambda, so P2564 §13b.1 cannot promote it.
// GCC 16 aggressively promotes lambdas that contain calls to functions with
// std::meta::info NTTPs (P3603R0 consteval-only type), even when 'constexpr'
// is added to the call operator.  A named function template is not subject to
// P2564 §13b.1 and therefore stays callable at runtime.
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
// isBeanType — predicate used by scanMembers
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
// postConstruct / preDestroy thunks
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
// scanMembers<Type>() — single-pass member scan (D4)
//
// Combines findCompatibleCtor<T>() and the two findHookMethod calls into one
// members_of traversal per type, instead of three separate calls.
// ─────────────────────────────────────────────────────────────────────────────

struct MemberScan {
    std::meta::info ctor{};          ///< Compatible constructor; info{} if absent.
    std::meta::info postConstruct{}; ///< postConstruct hook; info{} if absent.
    std::meta::info preDestroy{};    ///< preDestroy hook; info{} if absent.
    bool compatibleConstructorConflict = false; ///< Multiple compatible constructors.
};

template<std::meta::info Type>
consteval MemberScan scanMembers() {
    static constexpr auto kMembers =
        std::define_static_array(
            std::meta::members_of(Type, std::meta::access_context::unchecked()));
    MemberScan r;
    std::size_t compatibleConstructorCount = 0;
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
            if constexpr (compatible) {
                ++compatibleConstructorCount;
                if (compatibleConstructorCount >= 2) r.compatibleConstructorConflict = true;
                r.ctor = m;
            }
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
// factory product thunks
// ─────────────────────────────────────────────────────────────────────────────

// Detects unique_ptr specializations, including the defaulted deleter argument.
consteval bool isUniquePtrType(std::meta::info returnType) {
    const auto d = std::meta::dealias(returnType);
    try {
        const auto args = std::meta::template_arguments_of(d);
        return !args.empty() && std::meta::template_of(d) == ^^std::unique_ptr;
    } catch (...) {
        return false;
    }
}

// Unwraps unique_ptr<T> to T; returns returnType unchanged otherwise.
consteval std::meta::info unwrapUniquePtr(std::meta::info returnType) {
    const auto d = std::meta::dealias(returnType);
    if (isUniquePtrType(d)) {
        return std::meta::template_arguments_of(d)[0];
    }
    return returnType;
}

// Standard construct thunk: placement-new from factory call for value returns
// and fallback paths where the engine pre-allocates mem.
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

// Auto-allocating thunk for unique_ptr<T> factory products.
// Calls the factory method, releases the unique_ptr, and returns the raw pointer.
// No pre-allocated mem involved; allocation is performed by the factory's operator new.
// Used by materialization paths when allocAndConstruct != nullptr.
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
// Routes through T::operator delete when T defines one; otherwise uses global delete.
// Deviation from spec literal (`delete static_cast<T*>(p)`): that would double-destroy
// since destroy already called ~T(). This thunk performs only deallocation.
template<typename T>
void deallocFactoryProductThunk(void* p) noexcept {
    if constexpr (requires { T::operator delete(p); }) {
        T::operator delete(p);
    } else {
        ::operator delete(p);
    }
}

template<std::meta::info ParamType>
consteval bool isRetainedBeanParameter() {
    constexpr auto d = std::meta::dealias(ParamType);
    if constexpr (!std::meta::is_type(d) || !std::meta::is_class_type(d)) {
        return false;
    } else {
        try {
            const auto args = std::meta::template_arguments_of(d);
            if (args.size() != 1) return false;
            return std::meta::template_of(d) == ^^ctr::Bean;
        } catch (...) {
            return false;
        }
    }
}

template<std::meta::info ParamType>
consteval std::meta::info retainedParameterType() {
    if constexpr (isRetainedBeanParameter<ParamType>()) {
        return std::meta::template_arguments_of(std::meta::dealias(ParamType))[0];
    } else {
        return ParamType;
    }
}

template<std::meta::info Method>
consteval std::vector<ctr::BeanParameterRecord> makeMethodParameterRecords() {
    static constexpr auto kParams =
        std::define_static_array(std::meta::parameters_of(Method));
    std::vector<ctr::BeanParameterRecord> result;
    result.reserve(kParams.size());
    template for (constexpr auto p : kParams) {
        constexpr auto paramType = std::meta::dealias(std::meta::type_of(p));
        constexpr auto injectedType = retainedParameterType<paramType>();
        using U = [:injectedType:];
        result.push_back({&TypeInfoGetter<U>::get});
    }
    return result;
}

template<std::meta::info Member>
consteval bool hasRetainedParameterList() {
    try {
        (void) std::meta::parameters_of(Member).size();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace ctr::detail
