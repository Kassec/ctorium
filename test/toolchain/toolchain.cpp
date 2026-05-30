#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef TOOLCHAIN_CHECK_CXX_COMPILER
#define TOOLCHAIN_CHECK_CXX_COMPILER ""
#endif

#ifndef TOOLCHAIN_CHECK_CXX_STANDARD_FLAG
#define TOOLCHAIN_CHECK_CXX_STANDARD_FLAG "-std=c++26"
#endif

#ifndef TOOLCHAIN_CHECK_REFLECTION_FLAG
#define TOOLCHAIN_CHECK_REFLECTION_FLAG "-freflection"
#endif

#ifndef TOOLCHAIN_CHECK_EXPERIMENTAL_LIBRARY
#define TOOLCHAIN_CHECK_EXPERIMENTAL_LIBRARY "stdc++exp"
#endif

namespace toolchain_check {
    struct Probe {
        const char *name = "";
        const char *source = "";
    };

    [[nodiscard]] std::string quoteArgument(const std::string_view value) {
        std::string result{};
        result.reserve(value.size() + 2U);
        result.push_back('"');
        for (const char c : value) {
            result.push_back(c);
        }
        result.push_back('"');
        return result;
    }

    [[nodiscard]] bool containsWhitespace(const std::string_view value) {
        for (const unsigned char c : value) {
            if (std::isspace(c) != 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool containsDoubleQuote(const std::string_view value) {
        return value.find('"') != std::string_view::npos;
    }

    [[nodiscard]] std::filesystem::path basePath() {
        std::error_code error{};
        const auto tempPath = std::filesystem::temp_directory_path(error);
        if (!error && !tempPath.empty()) {
            return tempPath;
        }

        const auto currentPath = std::filesystem::current_path(error);
        if (!error && !currentPath.empty()) {
            return currentPath;
        }

        return {};
    }

    [[nodiscard]] std::filesystem::path createTempDirectory() {
        const auto base = basePath();
        if (base.empty()) {
            return {};
        }

        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto path = base / ("ctorium-toolchain-check-" + std::to_string(now) + "-" + std::to_string(attempt));
            std::error_code error{};
            if (std::filesystem::create_directory(path, error)) {
                return path;
            }
        }
        return {};
    }

    [[nodiscard]] bool writeSourceFile(const std::filesystem::path& path, const std::string_view sourceText) {
        std::ofstream source(path, std::ios::binary | std::ios::trunc);
        if (!source) {
            return false;
        }
        source.write(sourceText.data(), static_cast<std::streamsize>(sourceText.size()));
        return static_cast<bool>(source);
    }

    [[nodiscard]] bool buildCommand(
        const std::filesystem::path& compilerPath,
        const std::string_view standardFlag,
        const std::string_view reflectionFlag,
        const std::string_view experimentalLibrary,
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& outputPath,
        std::string& command
    ) {
        if (compilerPath.empty()) {
            std::cerr << "Compiler path is empty.\n";
            return false;
        }
        if (containsDoubleQuote(compilerPath.string())) {
            std::cerr << "Compiler path contains a double quote that cannot be quoted portably.\n";
            return false;
        }
        if (containsDoubleQuote(standardFlag) || containsDoubleQuote(reflectionFlag) ||
            containsDoubleQuote(experimentalLibrary)) {
            std::cerr << "Compile token contains a double quote that cannot be quoted portably.\n";
            return false;
        }

        command.clear();
        const std::vector<std::string> tokens{
            compilerPath.string(),
            std::string(standardFlag),
            std::string(reflectionFlag),
            sourcePath.string(),
            "-o",
            outputPath.string(),
            std::string("-l") + std::string(experimentalLibrary)
        };

        for (std::size_t index = 0U; index < tokens.size(); ++index) {
            if (index != 0U) {
                command.push_back(' ');
            }
            const auto& token = tokens[index];
            if (!containsWhitespace(token)) {
                command += token;
            } else {
                command += quoteArgument(token);
            }
        }

        return true;
    }

    [[nodiscard]] bool compileAndLinkProbe(
        const Probe& probe,
        const std::filesystem::path& compilerPath,
        const std::string_view standardFlag,
        const std::string_view reflectionFlag,
        const std::string_view experimentalLibrary,
        const std::filesystem::path& directory,
        const int index
    ) {
        const auto sourcePath = directory / ("probe-" + std::to_string(index) + ".cpp");
        const auto outputPath = directory / ("probe-" + std::to_string(index));

        if (!writeSourceFile(sourcePath, probe.source)) {
            return false;
        }

        std::string command{};
        if (!buildCommand(
            compilerPath,
            standardFlag,
            reflectionFlag,
            experimentalLibrary,
            sourcePath,
            outputPath,
            command
        )) {
            return false;
        }

        return std::system(command.c_str()) == 0;
    }
} // namespace toolchain_check

int main() {
    constexpr std::string_view compilerValue = TOOLCHAIN_CHECK_CXX_COMPILER;
    constexpr std::string_view standardFlag = TOOLCHAIN_CHECK_CXX_STANDARD_FLAG;
    constexpr std::string_view reflectionFlag = TOOLCHAIN_CHECK_REFLECTION_FLAG;
    constexpr std::string_view experimentalLibrary = TOOLCHAIN_CHECK_EXPERIMENTAL_LIBRARY;
    if (compilerValue.empty()) {
        std::cerr << "TOOLCHAIN_CHECK_CXX_COMPILER is empty.\n";
        return 1;
    }
    const std::filesystem::path compilerPath{std::string(compilerValue)};

    using toolchain_check::Probe;

    static const std::vector<Probe> probes{
        {
            "DefineStaticArrayWithMetaInfo",
            R"cpp(
#include <meta>
#include <cstdint>
#include <vector>
enum class Kind : uint8_t { A, B };
struct Entity {
    std::meta::info entity;
    Kind kind;
    std::meta::info extra;
};
struct sample {};
consteval std::vector<Entity> discover() {
    return {{^^sample, Kind::A, std::meta::info{}}};
}
int main() {
    static constexpr auto entities = std::define_static_array(discover());
    static_assert(entities.size() == 1);
    static_assert(entities[0].kind == Kind::A);
    static_assert(std::meta::is_type(entities[0].entity));
    return 0;
}
)cpp"
        },
        {
            "ConstevalVectorWithTypeInfoGetter",
            R"cpp(
#include <meta>
#include <vector>
#include <typeinfo>
template<typename T>
const std::type_info& getter() { return typeid(T); }
struct Entry {
    const std::type_info& (*typeInfo)() = nullptr;
    const char* name = nullptr;
    void (*thunk)(void*) = nullptr;
    unsigned size = 0;
};
struct sample {};
template<typename T>
void constructThunk(void* mem) { new (mem) T(); }
consteval std::vector<Entry> makeEntries() {
    const char* n = std::define_static_string(std::string_view("sample"));
    return {{&getter<sample>, n, &constructThunk<sample>, sizeof(sample)}};
}
int main() {
    static constexpr auto entries = std::define_static_array(makeEntries());
    static_assert(entries.size() == 1);
    static_assert(entries[0].size == sizeof(sample));
    return 0;
}
)cpp"
        },
        {
            "DefineStaticArrayWithDefineStaticString",
            R"cpp(
#include <meta>
#include <vector>
#include <string_view>
struct Entry {
    const char* name = nullptr;
    unsigned size = 0;
};
consteval std::vector<Entry> makeEntries() {
    const char* n = std::define_static_string(std::string_view("hello"));
    return {{n, 5}};
}
int main() {
    static constexpr auto entries = std::define_static_array(makeEntries());
    static_assert(entries.size() == 1);
    static_assert(entries[0].size == 5);
    return 0;
}
)cpp"
        },
        {
            "ConstevalVectorWithFunctionPointers",
            R"cpp(
#include <meta>
#include <vector>
template<typename T>
void thunk(void* mem) { new (mem) T(); }
struct Descriptor {
    void (*construct)(void*) = nullptr;
    unsigned size = 0;
};
struct sample {};
consteval std::vector<Descriptor> makeDescriptors() {
    return {{&thunk<sample>, sizeof(sample)}};
}
int main() {
    static constexpr auto descriptors = std::define_static_array(makeDescriptors());
    static_assert(descriptors.size() == 1);
    static_assert(descriptors[0].size == sizeof(sample));
    static_assert(descriptors[0].construct != nullptr);
    // Actually call it at runtime
    alignas(sample) unsigned char buf[sizeof(sample)];
    descriptors[0].construct(buf);
    return 0;
}
)cpp"
        },
        {
            "ReflectionExpression",
            R"cpp(
struct sample {};
consteval bool check() {
    constexpr auto token = ^^sample;
    (void)token;
    return true;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "ReflectionSplice",
            R"cpp(
#include <type_traits>
struct sample {};
consteval bool check() {
    constexpr auto token = ^^sample;
    using reflected_type = [:token:];
    return std::is_same_v<reflected_type, sample>;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "DefineStaticString",
            R"cpp(
#include <meta>
#include <string_view>
int main() {
    constexpr const char* value = std::define_static_string("toolchain");
    static_assert(std::string_view(value) == std::string_view("toolchain"));
    return 0;
}
)cpp"
        },
        {
            "MetaAnnotationsOf",
            R"cpp(
#include <meta>
struct marker {};
struct [[=marker{}]] sample {};
consteval bool check() {
    const auto annotations = std::meta::annotations_of(^^sample);
    unsigned count = 0U;
    for (const auto annotation : annotations) {
        (void)annotation;
        ++count;
    }
    return count == 1U;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaTypeOf",
            R"cpp(
#include <meta>
struct marker {};
struct [[=marker{}]] sample {};
consteval bool check() {
    static constexpr auto annotations = std::define_static_array(std::meta::annotations_of(^^sample));
    template for (constexpr auto annotation : annotations) {
        constexpr auto type = std::meta::type_of(annotation);
        if constexpr (std::meta::is_type(type)) {
            return true;
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaRemoveConst",
            R"cpp(
#include <meta>
struct marker {};
struct [[=marker{}]] sample {};
consteval bool check() {
    static constexpr auto annotations = std::define_static_array(std::meta::annotations_of(^^sample));
    template for (constexpr auto annotation : annotations) {
        constexpr auto type = std::meta::remove_const(std::meta::type_of(annotation));
        if constexpr (std::meta::is_same_type(type, ^^marker)) {
            return true;
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaIsSameType",
            R"cpp(
#include <meta>
struct sample {};
int main() {
    static_assert(std::meta::is_same_type(^^sample, ^^sample));
    return 0;
}
)cpp"
        },
        {
            "MetaExtract",
            R"cpp(
#include <meta>
#include <string_view>
struct marker {
    const char* name = "";
};
struct [[=marker{.name = std::define_static_string("alpha")}]] sample {};
consteval bool check() {
    static constexpr auto annotations = std::define_static_array(std::meta::annotations_of(^^sample));
    template for (constexpr auto annotation : annotations) {
        constexpr auto type = std::meta::remove_const(std::meta::type_of(annotation));
        if constexpr (std::meta::is_same_type(type, ^^marker)) {
            constexpr auto value = std::meta::extract<marker>(annotation);
            return std::string_view(value.name) == std::string_view("alpha");
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaIsTypeAlias",
            R"cpp(
#include <meta>
struct sample {};
using alias = sample;
int main() {
    static_assert(std::meta::is_type_alias(^^alias));
    return 0;
}
)cpp"
        },
        {
            "MetaIsNamespaceAlias",
            R"cpp(
#include <meta>
namespace source {}
namespace alias = source;
int main() {
    static_assert(std::meta::is_namespace_alias(^^alias));
    return 0;
}
)cpp"
        },
        {
            "MetaDealias",
            R"cpp(
#include <meta>
#include <type_traits>
struct sample {};
using alias = sample;
int main() {
    constexpr auto token = std::meta::dealias(^^alias);
    using dealiased_type = [:token:];
    static_assert(std::is_same_v<dealiased_type, sample>);
    return 0;
}
)cpp"
        },
        {
            "MetaIsType",
            R"cpp(
#include <meta>
struct sample {};
int main() {
    static_assert(std::meta::is_type(^^sample));
    return 0;
}
)cpp"
        },
        {
            "MetaIsClassType",
            R"cpp(
#include <meta>
struct sample {};
int main() {
    static_assert(std::meta::is_class_type(^^sample));
    return 0;
}
)cpp"
        },
        {
            "MetaIsNamespace",
            R"cpp(
#include <meta>
namespace sample {}
int main() {
    static_assert(std::meta::is_namespace(^^sample));
    return 0;
}
)cpp"
        },
        {
            "MetaAccessContextUnchecked",
            R"cpp(
#include <meta>
namespace sample {
struct value {};
}
consteval bool check() {
    const auto members = std::meta::members_of(^^sample, std::meta::access_context::unchecked());
    (void)members;
    return true;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaMembersOf",
            R"cpp(
#include <meta>
namespace sample {
struct value {};
}
consteval bool check() {
    const auto members = std::meta::members_of(^^sample, std::meta::access_context::unchecked());
    unsigned count = 0U;
    for (const auto member : members) {
        (void)member;
        ++count;
    }
    return count == 1U;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "DefineStaticArray",
            R"cpp(
#include <meta>
namespace sample {
struct value {};
}
int main() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    static_assert(members.size() == 1U);
    return 0;
}
)cpp"
        },
        {
            "TemplateFor",
            R"cpp(
#include <meta>
namespace sample {
struct value {};
}
consteval bool check() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    unsigned count = 0U;
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member)) {
            ++count;
        }
    }
    return count == 1U;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "ExpansionStatement",
            R"cpp(
#include <meta>
namespace sample {
struct first {};
struct second {};
}
consteval bool check() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    unsigned count = 0U;
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member)) {
            ++count;
        }
    }
    return count == 2U;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaParametersOf",
            R"cpp(
#include <meta>
#include <type_traits>
struct dependency {};
struct sample {
    explicit sample(dependency) {}
};
consteval bool check() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_constructor(member)) {
            static constexpr auto parameters = std::define_static_array(std::meta::parameters_of(member));
            if constexpr (parameters.size() == 1U) {
                using parameter_type = [:std::meta::type_of(parameters[0]):];
                if constexpr (std::is_same_v<parameter_type, dependency>) {
                    return true;
                }
            }
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaIsConstructor",
            R"cpp(
#include <meta>
struct sample {
    sample() = default;
};
consteval bool check() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_constructor(member)) {
            return true;
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaBasesOf",
            R"cpp(
#include <meta>
struct base {};
struct sample : base {};
int main() {
    static constexpr auto bases =
        std::define_static_array(std::meta::bases_of(^^sample, std::meta::access_context::unchecked()));
    static_assert(bases.size() == 1U);
    return 0;
}
)cpp"
        },
        {
            "MetaIsPublic",
            R"cpp(
#include <meta>
struct base {};
struct sample : base {};
consteval bool check() {
    static constexpr auto bases =
        std::define_static_array(std::meta::bases_of(^^sample, std::meta::access_context::unchecked()));
    template for (constexpr auto base_relation : bases) {
        if constexpr (std::meta::is_public(base_relation)) {
            return true;
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaIdentifierOf",
            R"cpp(
#include <meta>
#include <string_view>
namespace ns { struct sample {}; }
int main() {
    constexpr const char* name = std::define_static_string(std::meta::identifier_of(^^ns::sample));
    static_assert(std::string_view(name) == std::string_view("sample"));
    return 0;
}
)cpp"
        },
        {
            "MetaParentOf",
            R"cpp(
#include <meta>
#include <string_view>
namespace ns { struct sample {}; }
int main() {
    constexpr auto parent = std::meta::parent_of(^^ns::sample);
    static_assert(std::meta::is_namespace(parent));
    constexpr const char* parentName = std::define_static_string(std::meta::identifier_of(parent));
    static_assert(std::string_view(parentName) == std::string_view("ns"));
    return 0;
}
)cpp"
        },
        {
            "MetaInfoAsNTTA",
            R"cpp(
#include <meta>
#include <type_traits>
struct sample {};
struct EntityHolder { std::meta::info entity; };
template<EntityHolder e>
using Reflected = [:e.entity:];
int main() {
    constexpr EntityHolder h{ ^^sample };
    static_assert(std::is_same_v<Reflected<h>, sample>);
    return 0;
}
)cpp"
        },
        {
            "UserConstevalDefineStaticArray",
            R"cpp(
#include <meta>
#include <vector>
struct item { int value; };
consteval std::vector<item> makeItems() {
    return { {1}, {2}, {3} };
}
int main() {
    static constexpr auto items = std::define_static_array(makeItems());
    static_assert(items.size() == 3);
    static_assert(items[1].value == 2);
    return 0;
}
)cpp"
        },
        {
            "MetaSizeofSplicer",
            R"cpp(
#include <meta>
#include <cstddef>
struct sample { int x; int y; };
int main() {
    constexpr auto token = ^^sample;
    static_assert(sizeof(typename [: token :]) == sizeof(sample));
    static_assert(alignof(typename [: token :]) == alignof(sample));
    return 0;
}
)cpp"
        },
        {
            "MetaTemplateArgumentsOf",
            R"cpp(
#include <meta>
#include <type_traits>
template<typename T> struct Wrapper {};
struct Inner {};
int main() {
    constexpr auto wrapperType = ^^Wrapper<Inner>;
    static constexpr auto args = std::define_static_array(std::meta::template_arguments_of(wrapperType));
    static_assert(args.size() == 1);
    static_assert(std::is_same_v<typename [:args[0]:], Inner>);
    return 0;
}
)cpp"
        },
        {
            "MetaAnnotationsOfParameter",
            R"cpp(
#include <meta>
struct marker { int id = 0; };
struct dep {};
struct sample {
    explicit sample([[=marker{.id = 42}]] dep) {}
};
consteval bool check() {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_constructor(member)) {
            static constexpr auto params = std::define_static_array(std::meta::parameters_of(member));
            if constexpr (params.size() == 1) {
                static constexpr auto anns = std::define_static_array(
                    std::meta::annotations_of(params[0]));
                if constexpr (anns.size() == 1) {
                    constexpr auto val = std::meta::extract<marker>(anns[0]);
                    return val.id == 42;
                }
            }
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaIsSpecialMemberFunction",
            R"cpp(
#include <meta>
struct sample {
    sample() = default;
    void regularMethod() {}
};
consteval bool check() {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    bool hasSpecial = false;
    bool hasRegular = false;
    template for (constexpr auto member : members) {
        if constexpr (!std::meta::is_type(member)) {
            if constexpr (std::meta::is_special_member_function(member)) {
                hasSpecial = true;
            } else {
                hasRegular = true;
            }
        }
    }
    return hasSpecial && hasRegular;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaReturnTypeOf",
            R"cpp(
#include <meta>
struct result {};
struct factory {
    result produce();
};
consteval bool check() {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^factory, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (!std::meta::is_type(member) && !std::meta::is_special_member_function(member)) {
            return std::meta::is_same_type(std::meta::return_type_of(member), ^^result);
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "SpliceMemberFunctionCall",
            R"cpp(
#include <meta>
struct target {
    int compute() { return 42; }
};
consteval std::meta::info getMethod() {
    for (auto m : std::meta::members_of(^^target, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) && !std::meta::is_special_member_function(m))
            return m;
    }
    return std::meta::info{};
}
int main() {
    target t;
    constexpr auto m = getMethod();
    // P2996R13: &[:r:] for a non-static member function = pointer to member
    constexpr auto pmf = &[:m:];
    return (t.*pmf)() == 42 ? 0 : 1;
}
)cpp"
        },
        {
            "ConstevalStdString",
            R"cpp(
#include <meta>
#include <string>
#include <string_view>
namespace outer { namespace inner { struct sample {}; } }
consteval const char* buildName(std::meta::info entity) {
    std::string result(std::meta::identifier_of(entity));
    auto parent = std::meta::parent_of(entity);
    while (std::meta::is_namespace(parent)) {
        if (!std::meta::has_identifier(parent)) break;
        std::string_view pid = std::meta::identifier_of(parent);
        result = std::string(pid) + "::" + result;
        parent = std::meta::parent_of(parent);
    }
    return std::define_static_string(std::string_view(result));
}
int main() {
    constexpr const char* name = buildName(^^outer::inner::sample);
    static_assert(std::string_view(name) == std::string_view("outer::inner::sample"));
    return 0;
}
)cpp"
        },
        {
            "MetaInfoDefaultAndEquality",
            R"cpp(
#include <meta>
struct sample {};
consteval std::meta::info findOrInvalid(bool found) {
    if (found) return ^^sample;
    return std::meta::info{};
}
int main() {
    constexpr auto valid   = findOrInvalid(true);
    constexpr auto invalid = findOrInvalid(false);
    static_assert(valid   != std::meta::info{});
    static_assert(invalid == std::meta::info{});
    return 0;
}
)cpp"
        },
        {
            "ReflectTemplateSpecialisationWithTypeParam",
            R"cpp(
#include <meta>
#include <memory>
#include <type_traits>
template<typename T>
consteval bool check() {
    constexpr auto reflected = ^^std::unique_ptr<T>;
    return std::meta::is_type(reflected);
}
int main() {
    static_assert(check<int>());
    return 0;
}
)cpp"
        },
        {
            "MetaTemplateOf",
            R"cpp(
#include <meta>
#include <string_view>
template<typename T> struct Wrapper {};
struct Inner {};
int main() {
    constexpr auto tmpl = std::meta::template_of(^^Wrapper<Inner>);
    constexpr const char* name = std::define_static_string(std::meta::identifier_of(tmpl));
    static_assert(std::string_view(name) == std::string_view("Wrapper"));
    // P2996R13: two reflections of the same entity compare equal
    static_assert(tmpl == ^^Wrapper);
    return 0;
}
)cpp"
        },
        {
            "MetaInfoNttpLambdaNotEscalated",
            R"cpp(
// Validates the fix for P2564 immediate escalation in constructThunkInjected.
// std::meta::info is a consteval-only type (P3603R0 §2.2): expressions
// involving std::meta::info NTTPs are immediate-escalating (P2564 §13a.1).
// A lambda (P2564 §13b.1) containing such expressions is an immediate-escalating
// function; GCC 16 promotes it to consteval even with 'constexpr' on the call
// operator when the body calls a function with a std::meta::info NTTP.
// Fix: use a named pack-expansion helper function — §13b.1 only applies to
// lambda call operators, so named functions are not subject to this promotion.
#include <meta>
#include <utility>
#include <new>

struct Dep { int v; };
struct Consumer {
    Dep dep;
    explicit Consumer(Dep d) : dep(std::move(d)) {}
};

template<typename T, std::meta::info Ctor, std::size_t I>
T resolveArg() { return T{42}; }

// Named helper avoids P2564 §13b.1 lambda promotion.
template<typename T, std::meta::info Ctor, std::size_t... Is>
void constructInjImpl(void* mem, std::index_sequence<Is...>) {
    new (mem) T(resolveArg<Dep, Ctor, Is>()...);
}

template<typename T, std::meta::info Ctor>
void constructInj(void* mem) {
    static constexpr auto kParams =
        std::define_static_array(std::meta::parameters_of(Ctor));
    constructInjImpl<T, Ctor>(mem, std::make_index_sequence<kParams.size()>{});
}

consteval std::meta::info findInjectableCtor() {
    static constexpr auto kMembers = std::define_static_array(
        std::meta::members_of(^^Consumer,
                              std::meta::access_context::unchecked()));
    template for (constexpr auto m : kMembers) {
        if constexpr (std::meta::is_constructor(m)) {
            static constexpr auto kParams =
                std::define_static_array(std::meta::parameters_of(m));
            if constexpr (kParams.size() == 1) return m;
        }
    }
    return std::meta::info{};
}

int main() {
    alignas(Consumer) unsigned char buf[sizeof(Consumer)];
    constexpr auto ctor = findInjectableCtor();
    constructInj<Consumer, ctor>(static_cast<void*>(buf));
    return reinterpret_cast<Consumer*>(buf)->dep.v == 42 ? 0 : 1;
}
)cpp"
        },
        {
            "MetaAnnotationsOfMethod",
            R"cpp(
// Validates that annotations_of(m) works when m is a non-special, non-type
// member function (i.e. a regular method), and that extract<T> can be called
// on such an annotation. This is the exact path scanMembers() uses to detect
// [[=ctr::postConstruct{}]] and [[=ctr::preDestroy{}]] hooks.
#include <meta>
struct marker {};
struct sample {
    [[=marker{}]] void regularMethod() {}
};
consteval bool check() {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    template for (constexpr auto m : members) {
        if constexpr (!std::meta::is_type(m) && !std::meta::is_special_member_function(m)) {
            static constexpr auto anns = std::define_static_array(
                std::meta::annotations_of(m));
            if constexpr (anns.size() == 1) {
                constexpr auto t = std::meta::remove_const(std::meta::type_of(anns[0]));
                if constexpr (std::meta::is_same_type(t, ^^marker)) {
                    return true;
                }
            }
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
        {
            "MetaExtractScopedParamName",
            R"cpp(
// Reproduces the exact path BeanDescriptorGen::makeParamDescriptors uses for
// [[=ctr::scoped{.name=...}]]: extract a const char* member from a PARAMETER
// annotation in consteval context. Validates that scoped name extraction works
// (mirrors MetaAnnotationsOfParameter, which does the same for an int member).
// The name uses define_static_string, matching the normalized provenance the
// descriptor builder relies on.
#include <meta>
#include <string_view>
struct sc { const char* name = nullptr; };
struct dep {};
struct sample {
    explicit sample([[=sc{.name = std::define_static_string("db")}]] dep) {}
};
consteval bool check() {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_constructor(member)) {
            static constexpr auto params = std::define_static_array(std::meta::parameters_of(member));
            if constexpr (params.size() == 1) {
                static constexpr auto anns = std::define_static_array(
                    std::meta::annotations_of(params[0]));
                if constexpr (anns.size() == 1) {
                    constexpr auto val = std::meta::extract<sc>(anns[0]);
                    return std::string_view(val.name) == std::string_view("db");
                }
            }
        }
    }
    return false;
}
int main() {
    static_assert(check());
    return 0;
}
)cpp"
        },
    };

    const auto tempDirectory = toolchain_check::createTempDirectory();
    if (tempDirectory.empty()) {
        int code = 1;
        for (const auto& probe : probes) {
            std::cout << "[FAIL] " << probe.name << " code=" << code << '\n';
            ++code;
        }
        return 1;
    }

    int firstFailure = 0;
    int code = 1;
    for (const auto& probe : probes) {
        const bool passed = toolchain_check::compileAndLinkProbe(
            probe,
            compilerPath,
            standardFlag,
            reflectionFlag,
            experimentalLibrary,
            tempDirectory,
            code
        );
        if (passed) {
            std::cout << "[PASS] " << probe.name << '\n';
        } else {
            std::cout << "[FAIL] " << probe.name << " code=" << code << '\n';
            if (firstFailure == 0) {
                firstFailure = code;
            }
        }
        ++code;
    }

    std::error_code cleanupError{};
    std::filesystem::remove_all(tempDirectory, cleanupError);
    return firstFailure;
}
