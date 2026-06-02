#pragma once

#include "../../api/ctr/Config.hpp"

namespace CTORIUM_NAMESPACE::detail {
// ─────────────────────────────────────────────────────────────────────────────
// makeReflectiveData — compile-time projection of method metadata
//
// Generates a BeanReflectiveData struct (static lifetime) for the annotated type
// when retainAllMetadata = true.  Scans all non-constructor, non-special-member,
// non-type members and records name, Ctorium annotation presence, and param count.
// ─────────────────────────────────────────────────────────────────────────────

template<std::meta::info Type>
consteval const CTORIUM_NAMESPACE::BeanReflectiveData* makeReflectiveData() {
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
            std::array<CTORIUM_NAMESPACE::BeanMethodRecord, kCount> arr{};
            std::size_t idx = 0;
            template for (constexpr auto m : kMembers) {
                if constexpr (!std::meta::is_type(m)
                           && !std::meta::is_special_member_function(m)
                           && !std::meta::is_constructor(m)) {
                    bool isPC = false, isPD = false;
                    template for (constexpr auto ann : std::define_static_array(std::meta::annotations_of(m))) {
                        constexpr auto t = std::meta::remove_const(std::meta::type_of(ann));
                        if constexpr (std::meta::is_same_type(t, ^^CTORIUM_NAMESPACE::postConstruct)) isPC = true;
                        if constexpr (std::meta::is_same_type(t, ^^CTORIUM_NAMESPACE::preDestroy))    isPD = true;
                    }
                    // parameters_of throws for non-function members (e.g. data fields).
                    std::size_t pcount = 0;
                    const CTORIUM_NAMESPACE::BeanParameterRecord* params = nullptr;
                    if constexpr (hasRetainedParameterList<m>()) {
                        constexpr auto kParams =
                            std::define_static_array(makeMethodParameterRecords<m>());
                        pcount = kParams.size();
                        params = kParams.data();
                    }
                    arr[idx++] = {
                        std::define_static_string(std::meta::identifier_of(m)),
                        isPC,
                        isPD,
                        pcount,
                        params
                    };
                }
            }
            return arr;
        }();

        static constexpr auto kStaticRecords = kRecords; // static lifetime

        static constexpr CTORIUM_NAMESPACE::BeanReflectiveData kData{
            kStaticRecords.data(),
            kCount
        };
        return &kData;
    }
}

} // namespace CTORIUM_NAMESPACE::detail
