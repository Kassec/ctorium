#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ctr/Ctorium.hpp>

namespace prototype_max_slots_limit_fixture {

struct [[=ctr::prototype{}]] Service {};

} // namespace prototype_max_slots_limit_fixture

TEST(PrototypeMaxSlots, AllocationBeyondConfiguredLimitRaisesResolutionError) {
    auto& ctx = ctr::BeanContext::resolveContext("proto-max-slots-limit");
    ctx.discover<^^prototype_max_slots_limit_fixture>().start();

    std::vector<ctr::Bean<prototype_max_slots_limit_fixture::Service>> handles;
    handles.reserve(CTORIUM_PROTOTYPE_MAX_SLOTS);
    for (int i = 0; i < CTORIUM_PROTOTYPE_MAX_SLOTS; ++i) {
        handles.push_back(ctx.resolve<prototype_max_slots_limit_fixture::Service>());
        ASSERT_NE(handles.back().operator->(), nullptr);
    }

    try {
        (void)ctx.resolve<prototype_max_slots_limit_fixture::Service>();
        FAIL() << "Expected ctr::ResolutionError";
    } catch (const ctr::ResolutionError& ex) {
        EXPECT_NE(std::string{ex.what()}.find("CTORIUM_PROTOTYPE_MAX_SLOTS"), std::string::npos);
    }

    handles.clear();
    ctx.stop();
}

namespace prototype_max_slots_reuse_fixture {

struct [[=ctr::prototype{}]] Service {};

} // namespace prototype_max_slots_reuse_fixture

TEST(PrototypeMaxSlots, ReleasingAHandleRecyclesSlotForLaterAllocation) {
    auto& ctx = ctr::BeanContext::resolveContext("proto-max-slots-reuse");
    ctx.discover<^^prototype_max_slots_reuse_fixture>().start();

    std::vector<ctr::Bean<prototype_max_slots_reuse_fixture::Service>> handles;
    handles.reserve(CTORIUM_PROTOTYPE_MAX_SLOTS);
    for (int i = 0; i < CTORIUM_PROTOTYPE_MAX_SLOTS; ++i) {
        handles.push_back(ctx.resolve<prototype_max_slots_reuse_fixture::Service>());
        ASSERT_NE(handles.back().operator->(), nullptr);
    }

    handles.pop_back();

    auto replacement = ctx.resolve<prototype_max_slots_reuse_fixture::Service>();
    EXPECT_NE(replacement.operator->(), nullptr);

    handles.clear();
    replacement = {};
    ctx.stop();
}
