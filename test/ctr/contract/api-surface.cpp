// =============================================================================
// PUBLIC API CONTRACT LOCK
//
// This file verifies that the public API surface of Ctorium (types, method
// signatures, return types, and type traits) matches the documented contract.
// It is a compile-time test: if it compiles, the surface is intact.
//
// DO NOT modify this file unless you are explicitly and intentionally changing
// a public API contract. A contract change requires a dedicated spec and
// explicit validation — it is never a side effect of an implementation.
//
// This rule applies to all contributors equally.
// Receiving a failing build from this file is not a reason to edit it.
// It is a reason to stop and report the discrepancy.
// =============================================================================

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include <ctr/Registration.hpp>

namespace api_surface_fixture {

    struct PlainService {
        int value = 0;
    };

    struct BaseService {
        virtual ~BaseService() = default;
    };

    struct DerivedService : BaseService {
    };

    struct [[
    =
    CTORIUM_NAMESPACE::singleton {
    }

    ]
    ]
    SingletonService {
    };

    struct [[
    =
    CTORIUM_NAMESPACE::prototype {
        .
        priority = 1
    }

    ]
    ]
    PrototypeService {
    };

    struct [[
    =
    CTORIUM_NAMESPACE::session {
        .
        priority = 2
    }

    ]
    ]
    SessionService {
    };

    struct [[
    =
    CTORIUM_NAMESPACE::threadLocal {
        .
        priority = 3
    }

    ]
    ]
    ThreadLocalService {
    };

    struct [[
    =
    CTORIUM_NAMESPACE::singleton {
    }

    ]
    ]
    [[
    =
    CTORIUM_NAMESPACE::named {
        .
        name = std::define_static_string("api-surface-named")
    }

    ]
    ]
    NamedService {
    };

    struct [[
    =
    CTORIUM_NAMESPACE::singleton {
    }

    ]
    ]
    HookedService {
        [[=
        CTORIUM_NAMESPACE::postConstruct{}
        ]
        ]
        void initialize();
        [[=
        CTORIUM_NAMESPACE::preDestroy{}
        ]
        ]
        void shutdown();
    };

    struct [[
    =
    CTORIUM_NAMESPACE::singleton {
    }

    ]
    ]
    ScopedConsumer {
        explicit ScopedConsumer (
            [[=CTORIUM_NAMESPACE::scoped
        {
            .
            name = std::define_static_string("api-surface-scope")
        }
        ]
        ]
        CTORIUM_NAMESPACE::Bean<SessionService> service
        )
        ;
    };

    struct FactoryProduct {
    };

    struct [[
    =
    CTORIUM_NAMESPACE::factory {
    }

    ]
    ]
    ProductFactory {
        [[=
        CTORIUM_NAMESPACE::singleton{}
        ]
        ]
        FactoryProduct value();
        [[=
        CTORIUM_NAMESPACE::prototype{}
        ]
        ]
        std::unique_ptr<FactoryProduct> pointer();
    };

    struct TypedListener {
        void operator()(const CTORIUM_NAMESPACE::Bean<PlainService> &) const;
    };

    struct GlobalListener {
        void operator()(const CTORIUM_NAMESPACE::AnyBean &) const;
    };

    const std::type_info &typeInfo();

} // namespace api_surface_fixture

namespace api_surface_contract {

    using PlainService = api_surface_fixture::PlainService;
    using BaseService = api_surface_fixture::BaseService;
    using DerivedService = api_surface_fixture::DerivedService;
    using TypeInfoGetter = const std::type_info& (*)();
    using MetadataLifetime = decltype(std::declval<const CTORIUM_NAMESPACE::BeanMetadata &>().lifetime());
    using MetadataOrigin = decltype(std::declval<const CTORIUM_NAMESPACE::BeanMetadata &>().origin());

    template <class Context, class T>
    concept BindSessionAvailable = requires(Context &context, std::unique_ptr<T> object) {
        context.template bindSession<T>(std::move(object));
    };

    template <class Context, class T>
    concept BindSingletonReturnsRootContext = requires(Context &context, std::unique_ptr<T> object) {
        {
            context.template bindSingleton<T>(std::move(object))
        } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
        {
            context.template bindSingleton<T>(std::move(object), CTORIUM_NAMESPACE::BindOptions{})
        } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
    };

    template <class Context, class T>
    concept ResolveSurface = requires(Context &context) {
        {
            context.template resolve<T>()
        } -> std::same_as<CTORIUM_NAMESPACE::Bean<T>>;
        {
            context.template resolve<T>(CTORIUM_NAMESPACE::named{.name = "api-surface-name"})
        } -> std::same_as<CTORIUM_NAMESPACE::Bean<T>>;
    };

    template <class Context, class T>
    concept DefaultNamedSurface = requires(Context &context) {
        {
            context.template defaultNamed<T>("api-surface-name")
        } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
        {
            context.template defaultNamed<T>(nullptr)
        } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
    };

    template <class Context, class T>
    concept ListenerSurface = requires(Context &context) {
        {
            context.template on<T>(
                CTORIUM_NAMESPACE::onCreated,
                api_surface_fixture::TypedListener{},
                CTORIUM_NAMESPACE::ListenerOptions{}
                )
        } -> std::same_as<CTORIUM_NAMESPACE::ListenerHandle>;
        {
            context.on(
                CTORIUM_NAMESPACE::onDestroyed,
                api_surface_fixture::GlobalListener{},
                CTORIUM_NAMESPACE::ListenerOptions{}
                )
        } -> std::same_as<CTORIUM_NAMESPACE::ListenerHandle>;
    };

    template <class T>
    concept BeanHandleSurface = requires(const CTORIUM_NAMESPACE::Bean<T> &bean) {
        {
            bean.operator->()
        } -> std::same_as<T *>;
        {
            *bean
        } -> std::same_as<T &>;
        {
            bean.value()
        } -> std::same_as<T &>;
        {
            bean.context()
        } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
        {
            bean.metadata()
        } -> std::same_as<CTORIUM_NAMESPACE::BeanMetadata>;
        {
            bean.template exact<T>()
        } -> std::same_as<bool>;
        {
            bean.template compatible<BaseService>()
        } -> std::same_as<bool>;
        {
            bean.template cast<BaseService>()
        } -> std::same_as<CTORIUM_NAMESPACE::Bean<BaseService>>;
        {
            bean.template tryCast<BaseService>()
        } -> std::same_as<std::optional<CTORIUM_NAMESPACE::Bean<BaseService>>>;
        {
            bean == bean
        } -> std::same_as<bool>;
        {
            bean != bean
        } -> std::same_as<bool>;
    };

    template <class T>
    concept AnyBeanSurface = requires(const CTORIUM_NAMESPACE::AnyBean &bean) {
        {
            bean.context()
        } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
        {
            bean.metadata()
        } -> std::same_as<CTORIUM_NAMESPACE::BeanMetadata>;
        {
            bean.template exact<T>()
        } -> std::same_as<bool>;
        {
            bean.template compatible<T>()
        } -> std::same_as<bool>;
        {
            bean.template cast<T>()
        } -> std::same_as<CTORIUM_NAMESPACE::Bean<T>>;
        {
            bean.template tryCast<T>()
        } -> std::same_as<std::optional<CTORIUM_NAMESPACE::Bean<T>>>;
        {
            bean == bean
        } -> std::same_as<bool>;
        {
            bean != bean
        } -> std::same_as<bool>;
    };

    static_assert(std::is_class_v<CTORIUM_NAMESPACE::BeanContext>);
    static_assert(std::has_virtual_destructor_v<CTORIUM_NAMESPACE::BeanContext>);
    static_assert(!std::is_default_constructible_v<CTORIUM_NAMESPACE::BeanContext>);
    static_assert(!std::is_copy_constructible_v<CTORIUM_NAMESPACE::BeanContext>);
    static_assert(!std::is_copy_assignable_v<CTORIUM_NAMESPACE::BeanContext>);
    static_assert(std::is_base_of_v<CTORIUM_NAMESPACE::BeanContext, CTORIUM_NAMESPACE::ScopedContext>);
    static_assert(std::is_convertible_v<CTORIUM_NAMESPACE::ScopedContext *, CTORIUM_NAMESPACE::BeanContext *>);

    static_assert(
        std::same_as<decltype(CTORIUM_NAMESPACE::BeanContext::resolveContext()), CTORIUM_NAMESPACE::BeanContext &>
        );
    static_assert(
        std::same_as<
            decltype(CTORIUM_NAMESPACE::BeanContext::resolveContext(std::declval<std::string_view>())),
            CTORIUM_NAMESPACE::BeanContext &>
        );

    static_assert(
        requires(CTORIUM_NAMESPACE::BeanContext &context) {
            {
                context.discover<^^api_surface_fixture>()
            } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
            {
                context.discover<^^api_surface_fixture>(CTORIUM_NAMESPACE::DiscoverOptions{})
            } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
            {
                context.start()
            } -> std::same_as<CTORIUM_NAMESPACE::BeanContext &>;
            {
                context.stop()
            } -> std::same_as<void>;
            {
                context.resolveScope("api-surface-scope")
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
            {
                context.remove(std::declval<const CTORIUM_NAMESPACE::ListenerHandle &>())
            } -> std::same_as<void>;
        }
        );

    static_assert(ResolveSurface<CTORIUM_NAMESPACE::BeanContext, PlainService>);
    static_assert(DefaultNamedSurface<CTORIUM_NAMESPACE::BeanContext, PlainService>);
    static_assert(ListenerSurface<CTORIUM_NAMESPACE::BeanContext, PlainService>);
    static_assert(
        BindSingletonReturnsRootContext<CTORIUM_NAMESPACE::BeanContext, PlainService>,
        "bindSingleton<T>(...) must return BeanContext& for chaining."
        );
    static_assert(
        !BindSessionAvailable<CTORIUM_NAMESPACE::BeanContext, PlainService>,
        "bindSession<T>(...) must not be available on BeanContext."
        );

    static_assert(
        requires(
        CTORIUM_NAMESPACE::ScopedContext &scope, const CTORIUM_NAMESPACE::ScopedContext &constScope,
        PlainService &service
        ) {
            {
                scope.start()
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
            {
                scope.stop()
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
            {
                scope.restart()
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
            {
                scope.resolveScope("api-surface-sibling")
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
            {
                scope.userData(service)
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
            {
                scope.userData(nullptr)
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
            {
                scope.template userData<PlainService>()
            } -> std::same_as<std::optional<std::reference_wrapper<PlainService>>>;
            {
                constScope.template userData<PlainService>()
            } -> std::same_as<std::optional<std::reference_wrapper<const PlainService>>>;
            {
                scope.template bindSession<PlainService>(std::declval<std::unique_ptr<PlainService>>())
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
            {
                scope.template bindSession<PlainService>(
                    std::declval<std::unique_ptr<PlainService>>(),
                    CTORIUM_NAMESPACE::BindOptions{}
                    )
            } -> std::same_as<CTORIUM_NAMESPACE::ScopedContext &>;
        }
        );

    static_assert(ResolveSurface<CTORIUM_NAMESPACE::ScopedContext, PlainService>);
    static_assert(DefaultNamedSurface<CTORIUM_NAMESPACE::ScopedContext, PlainService>);
    static_assert(ListenerSurface<CTORIUM_NAMESPACE::ScopedContext, PlainService>);
    static_assert(
        BindSingletonReturnsRootContext<CTORIUM_NAMESPACE::ScopedContext, PlainService>,
        "ScopedContext::bindSingleton<T>(...) must keep the inherited BeanContext& chaining contract."
        );

    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::prototype>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::singleton>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::session>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::threadLocal>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::named>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::factory>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::postConstruct>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::preDestroy>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::scoped>);

    static_assert(
        requires {
            CTORIUM_NAMESPACE::prototype{.priority = 1};
            CTORIUM_NAMESPACE::singleton{.priority = 2, .lazy = false};
            CTORIUM_NAMESPACE::session{.priority = 3};
            CTORIUM_NAMESPACE::threadLocal{.priority = 4};
            CTORIUM_NAMESPACE::named{.name = "api-surface-name"};
            CTORIUM_NAMESPACE::factory{};
            CTORIUM_NAMESPACE::postConstruct{};
            CTORIUM_NAMESPACE::preDestroy{};
            CTORIUM_NAMESPACE::scoped{.name = "api-surface-scope"};
        }
        );

    static_assert(CTORIUM_NAMESPACE::prototype{}.priority == 0);
    static_assert(CTORIUM_NAMESPACE::singleton{}.priority == 0);
    static_assert(CTORIUM_NAMESPACE::singleton{}.lazy);
    static_assert(CTORIUM_NAMESPACE::session{}.priority == 0);
    static_assert(CTORIUM_NAMESPACE::threadLocal{}.priority == 0);
    static_assert(CTORIUM_NAMESPACE::named{}.name == nullptr);
    static_assert(CTORIUM_NAMESPACE::scoped{}.name == nullptr);

    static_assert(std::is_enum_v<CTORIUM_NAMESPACE::Lifetime>);
    static_assert(std::is_enum_v<CTORIUM_NAMESPACE::Origin>);
    static_assert(std::same_as<std::underlying_type_t<CTORIUM_NAMESPACE::Lifetime>, std::uint8_t>);
    static_assert(std::same_as<std::underlying_type_t<CTORIUM_NAMESPACE::Origin>, std::uint8_t>);
    static_assert(
        requires {
            CTORIUM_NAMESPACE::Lifetime::Prototype;
            CTORIUM_NAMESPACE::Lifetime::Singleton;
            CTORIUM_NAMESPACE::Lifetime::Session;
            CTORIUM_NAMESPACE::Lifetime::ThreadLocal;
            CTORIUM_NAMESPACE::Origin::AnnotatedType;
            CTORIUM_NAMESPACE::Origin::FactoryProduct;
            CTORIUM_NAMESPACE::Origin::RuntimeBinding;
        }
        );

    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::DiscoverOptions>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::BindOptions>);
    static_assert(std::is_aggregate_v<CTORIUM_NAMESPACE::ListenerOptions>);
    static_assert(
        requires {
            CTORIUM_NAMESPACE::DiscoverOptions{.retainAllMetadata = true};
            CTORIUM_NAMESPACE::BindOptions{.name = "api-surface-name", .priority = 1};
            CTORIUM_NAMESPACE::ListenerOptions{.priority = 2};
        }
        );
    static_assert(!CTORIUM_NAMESPACE::DiscoverOptions{}.retainAllMetadata);
    static_assert(CTORIUM_NAMESPACE::BindOptions{}.name == nullptr);
    static_assert(CTORIUM_NAMESPACE::BindOptions{}.priority == 0);
    static_assert(CTORIUM_NAMESPACE::ListenerOptions{}.priority == 0);

    static_assert(
        std::same_as<
            std::remove_cv_t<decltype(CTORIUM_NAMESPACE::onInitialized)>,
            CTORIUM_NAMESPACE::onInitialized_t>
        );
    static_assert(
        std::same_as<
            std::remove_cv_t<decltype(CTORIUM_NAMESPACE::onCreated)>,
            CTORIUM_NAMESPACE::onCreated_t>
        );
    static_assert(
        std::same_as<
            std::remove_cv_t<decltype(CTORIUM_NAMESPACE::onPreDestroy)>,
            CTORIUM_NAMESPACE::onPreDestroy_t>
        );
    static_assert(
        std::same_as<
            std::remove_cv_t<decltype(CTORIUM_NAMESPACE::onDestroyed)>,
            CTORIUM_NAMESPACE::onDestroyed_t>
        );

    static_assert(std::is_base_of_v<std::runtime_error, CTORIUM_NAMESPACE::CtoriumError>);
    static_assert(std::is_base_of_v<std::runtime_error, CTORIUM_NAMESPACE::ResolutionError>);
    static_assert(std::is_base_of_v<std::runtime_error, CTORIUM_NAMESPACE::ContextStateError>);
    static_assert(std::is_base_of_v<std::runtime_error, CTORIUM_NAMESPACE::ConfigurationError>);
    static_assert(std::is_base_of_v<CTORIUM_NAMESPACE::CtoriumError, CTORIUM_NAMESPACE::ResolutionError>);
    static_assert(std::is_base_of_v<CTORIUM_NAMESPACE::CtoriumError, CTORIUM_NAMESPACE::ContextStateError>);
    static_assert(std::is_base_of_v<CTORIUM_NAMESPACE::CtoriumError, CTORIUM_NAMESPACE::ConfigurationError>);
    static_assert(std::is_constructible_v<CTORIUM_NAMESPACE::ResolutionError, const char *>);
    static_assert(std::is_constructible_v<CTORIUM_NAMESPACE::ContextStateError, std::string>);
    static_assert(std::is_constructible_v<CTORIUM_NAMESPACE::ConfigurationError, const char *>);

    static_assert(std::is_default_constructible_v<CTORIUM_NAMESPACE::ListenerHandle>);
    static_assert(std::is_copy_constructible_v<CTORIUM_NAMESPACE::ListenerHandle>);
    static_assert(std::is_move_constructible_v<CTORIUM_NAMESPACE::ListenerHandle>);
    static_assert(std::is_nothrow_move_constructible_v<CTORIUM_NAMESPACE::ListenerHandle>);
    static_assert(
        requires(CTORIUM_NAMESPACE::ListenerHandle handle) {
            {
                handle.remove()
            } -> std::same_as<void>;
        }
        );
    static_assert(noexcept(std::declval<CTORIUM_NAMESPACE::ListenerHandle &>().remove()));

    static_assert(std::is_default_constructible_v<CTORIUM_NAMESPACE::Bean<PlainService>>);
    static_assert(std::is_copy_constructible_v<CTORIUM_NAMESPACE::Bean<PlainService>>);
    static_assert(std::is_move_constructible_v<CTORIUM_NAMESPACE::Bean<PlainService>>);
    static_assert(std::is_nothrow_move_constructible_v<CTORIUM_NAMESPACE::Bean<PlainService>>);
    static_assert(std::is_copy_assignable_v<CTORIUM_NAMESPACE::Bean<PlainService>>);
    static_assert(std::is_move_assignable_v<CTORIUM_NAMESPACE::Bean<PlainService>>);
    static_assert(std::is_nothrow_move_assignable_v<CTORIUM_NAMESPACE::Bean<PlainService>>);
    static_assert(BeanHandleSurface<PlainService>);
    static_assert(BeanHandleSurface<DerivedService>);

    static_assert(std::is_default_constructible_v<CTORIUM_NAMESPACE::AnyBean>);
    static_assert(std::is_copy_constructible_v<CTORIUM_NAMESPACE::AnyBean>);
    static_assert(std::is_move_constructible_v<CTORIUM_NAMESPACE::AnyBean>);
    static_assert(std::is_nothrow_move_constructible_v<CTORIUM_NAMESPACE::AnyBean>);
    static_assert(std::is_copy_assignable_v<CTORIUM_NAMESPACE::AnyBean>);
    static_assert(std::is_move_assignable_v<CTORIUM_NAMESPACE::AnyBean>);
    static_assert(std::is_nothrow_move_assignable_v<CTORIUM_NAMESPACE::AnyBean>);
    static_assert(AnyBeanSurface<PlainService>);

    static_assert(!std::is_default_constructible_v<CTORIUM_NAMESPACE::BeanMetadata>);
    static_assert(
        std::is_constructible_v<
            CTORIUM_NAMESPACE::BeanMetadata,
            TypeInfoGetter,
            TypeInfoGetter,
            const char *,
            const char *,
            MetadataLifetime,
            MetadataOrigin,
            const CTORIUM_NAMESPACE::BeanReflectiveData *>
        );
    static_assert(
        std::is_constructible_v<
            CTORIUM_NAMESPACE::BeanMetadata,
            TypeInfoGetter,
            TypeInfoGetter,
            const char *,
            MetadataLifetime,
            MetadataOrigin,
            const CTORIUM_NAMESPACE::BeanReflectiveData *>
        );
    static_assert(std::is_enum_v<MetadataLifetime>);
    static_assert(std::is_enum_v<MetadataOrigin>);
    static_assert(std::same_as<MetadataLifetime, CTORIUM_NAMESPACE::Lifetime>);
    static_assert(std::same_as<MetadataOrigin, CTORIUM_NAMESPACE::Origin>);

    static_assert(
        requires(const CTORIUM_NAMESPACE::BeanMetadata &metadata) {
            {
                metadata.observedType()
            } -> std::same_as<const std::type_info &>;
            {
                metadata.exactType()
            } -> std::same_as<const std::type_info &>;
            {
                metadata.name()
            } -> std::same_as<std::string_view>;
            {
                metadata.factoryMethod()
            } -> std::same_as<std::string_view>;
            {
                metadata.lifetime()
            } -> std::same_as<MetadataLifetime>;
            {
                metadata.origin()
            } -> std::same_as<MetadataOrigin>;
            {
                metadata.methods()
            } -> std::same_as<CTORIUM_NAMESPACE::BeanMetadata::MethodsRange>;
        }
        );

    using MethodsRange = CTORIUM_NAMESPACE::BeanMetadata::MethodsRange;
    using MethodView = decltype(*std::declval<MethodsRange::Iterator>());
    using FilteredMethodsRange = CTORIUM_NAMESPACE::BeanMetadata::FilteredMethodsRange<CTORIUM_NAMESPACE::postConstruct>
    ;
    using FilteredMethodView = decltype(*std::declval<FilteredMethodsRange::Iterator>());

    static_assert(
        requires(const MethodsRange &methods) {
            {
                methods.begin()
            } -> std::same_as<MethodsRange::Iterator>;
            {
                methods.end()
            } -> std::same_as<MethodsRange::Iterator>;
            {
                methods.size()
            } -> std::same_as<std::size_t>;
            {
                methods.empty()
            } -> std::same_as<bool>;
            {
                methods.template annotatedWith<CTORIUM_NAMESPACE::postConstruct>()
            } -> std::same_as<FilteredMethodsRange>;
        }
        );

    static_assert(
        requires(MethodsRange::Iterator iterator, MethodsRange::Iterator end) {
            {
                *iterator
            } -> std::same_as<MethodView>;
            {
                ++iterator
            } -> std::same_as<MethodsRange::Iterator &>;
            {
                iterator != end
            } -> std::same_as<bool>;
        }
        );

    static_assert(
        requires(const MethodView &method) {
            {
                method.name()
            } -> std::same_as<const char *>;
            {
                method.parameterCount()
            } -> std::same_as<std::size_t>;
            {
                method.template hasAnnotation<CTORIUM_NAMESPACE::postConstruct>()
            } -> std::same_as<bool>;
            {
                method.template annotation<CTORIUM_NAMESPACE::postConstruct>()
            } -> std::same_as<std::optional<CTORIUM_NAMESPACE::postConstruct>>;
            {
                method.parameters()
            } -> std::same_as<std::span<const CTORIUM_NAMESPACE::BeanParameterRecord>>;
        }
        );

    static_assert(
        requires(
        const FilteredMethodsRange &methods, FilteredMethodsRange::Iterator iterator,
        FilteredMethodsRange::Iterator end, const FilteredMethodView &method
        ) {
            {
                methods.begin()
            } -> std::same_as<FilteredMethodsRange::Iterator>;
            {
                methods.end()
            } -> std::same_as<FilteredMethodsRange::Iterator>;
            {
                methods.empty()
            } -> std::same_as<bool>;
            {
                *iterator
            } -> std::same_as<FilteredMethodView>;
            {
                ++iterator
            } -> std::same_as<FilteredMethodsRange::Iterator &>;
            {
                iterator != end
            } -> std::same_as<bool>;
            {
                method.name()
            } -> std::same_as<const char *>;
            {
                method.parameterCount()
            } -> std::same_as<std::size_t>;
            {
                method.template annotation<CTORIUM_NAMESPACE::postConstruct>()
            } -> std::same_as<std::optional<CTORIUM_NAMESPACE::postConstruct>>;
            {
                method.parameters()
            } -> std::same_as<std::span<const CTORIUM_NAMESPACE::BeanParameterRecord>>;
        }
        );

    static_assert(
        requires(const CTORIUM_NAMESPACE::BeanParameterRecord &parameter) {
            {
                parameter.injectedType()
            } -> std::same_as<const std::type_info &>;
        }
        );

} // namespace api_surface_contract
