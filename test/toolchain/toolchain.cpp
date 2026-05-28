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
            "MetaInfoAsNTTA",
            R"cpp(
#include <meta>
struct sample {};
struct Entity {
    std::meta::info entity;
};
template <Entity entity>
consteval bool check() {
    return std::meta::is_type(entity.entity);
}
int main() {
    static_assert(check<Entity{^^sample}>());
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
            "MetaIdentifierOf",
            R"cpp(
#include <meta>
#include <string_view>
namespace sample {
struct type {};
}
int main() {
    constexpr auto identifier = std::meta::identifier_of(^^sample::type);
    static_assert(std::string_view(identifier) == std::string_view("type"));
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
            "MetaSizeofSplicer",
            R"cpp(
#include <meta>
struct alignas(16) sample {
    int value;
};
int main() {
    constexpr auto token = ^^sample;
    static_assert(sizeof(typename [:token:]) == sizeof(sample));
    static_assert(alignof(typename [:token:]) == alignof(sample));
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
            "UserConstevalDefineStaticArray",
            R"cpp(
#include <meta>
#include <vector>
struct first {};
struct second {};
consteval std::vector<std::meta::info> collect() {
    return {^^first, ^^second};
}
int main() {
    static constexpr auto items = std::define_static_array(collect());
    static_assert(items.size() == 2U);
    static_assert(std::meta::is_same_type(items[0], ^^first));
    static_assert(std::meta::is_same_type(items[1], ^^second));
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
            "MetaAnnotationsOfParameter",
            R"cpp(
#include <meta>
struct marker {
    const char* name = "";
};
struct dependency {};
struct sample {
    explicit sample([[=marker{.name = std::define_static_string("alpha")}]] dependency) {}
};
consteval bool check() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_constructor(member)) {
            static constexpr auto parameters = std::define_static_array(std::meta::parameters_of(member));
            static constexpr auto annotations = std::define_static_array(std::meta::annotations_of(parameters[0]));
            return annotations.size() == 1U;
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
            "MetaTemplateArgumentsOf",
            R"cpp(
#include <meta>
template <class T>
struct Bean {};
struct Logger {};
int main() {
    static constexpr auto arguments = std::define_static_array(std::meta::template_arguments_of(^^Bean<Logger>));
    static_assert(arguments.size() == 1U);
    static_assert(std::meta::is_same_type(arguments[0], ^^Logger));
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
            "MetaIsSpecialMemberFunction",
            R"cpp(
#include <meta>
struct sample {
    sample() = default;
    void regular() {}
};
consteval bool check() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^sample, std::meta::access_context::unchecked()));
    bool hasSpecial = false;
    bool hasRegular = false;
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_special_member_function(member)) {
            hasSpecial = true;
        } else if constexpr (std::meta::is_function(member)) {
            hasRegular = true;
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
struct product {};
struct factory {
    product create();
};
consteval bool check() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(^^factory, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_function(member) && !std::meta::is_constructor(member) &&
                      !std::meta::is_destructor(member)) {
            constexpr auto result = std::meta::return_type_of(member);
            if constexpr (std::meta::is_same_type(result, ^^product)) {
                return true;
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
