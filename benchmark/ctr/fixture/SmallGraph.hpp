#pragma once
#include <ctr/Ctorium.hpp>

// 32 singleton descriptors — flat, no dependency edges.
// Stresses discovery enumeration and TypeIndex construction at this scale.
namespace bench_startup_small {
    struct [[=ctr::singleton{}]] S0 {};
    struct [[=ctr::singleton{}]] S1 {};
    struct [[=ctr::singleton{}]] S2 {};
    struct [[=ctr::singleton{}]] S3 {};
    struct [[=ctr::singleton{}]] S4 {};
    struct [[=ctr::singleton{}]] S5 {};
    struct [[=ctr::singleton{}]] S6 {};
    struct [[=ctr::singleton{}]] S7 {};
    struct [[=ctr::singleton{}]] S8 {};
    struct [[=ctr::singleton{}]] S9 {};
    struct [[=ctr::singleton{}]] S10 {};
    struct [[=ctr::singleton{}]] S11 {};
    struct [[=ctr::singleton{}]] S12 {};
    struct [[=ctr::singleton{}]] S13 {};
    struct [[=ctr::singleton{}]] S14 {};
    struct [[=ctr::singleton{}]] S15 {};
    struct [[=ctr::singleton{}]] S16 {};
    struct [[=ctr::singleton{}]] S17 {};
    struct [[=ctr::singleton{}]] S18 {};
    struct [[=ctr::singleton{}]] S19 {};
    struct [[=ctr::singleton{}]] S20 {};
    struct [[=ctr::singleton{}]] S21 {};
    struct [[=ctr::singleton{}]] S22 {};
    struct [[=ctr::singleton{}]] S23 {};
    struct [[=ctr::singleton{}]] S24 {};
    struct [[=ctr::singleton{}]] S25 {};
    struct [[=ctr::singleton{}]] S26 {};
    struct [[=ctr::singleton{}]] S27 {};
    struct [[=ctr::singleton{}]] S28 {};
    struct [[=ctr::singleton{}]] S29 {};
    struct [[=ctr::singleton{}]] S30 {};
    struct [[=ctr::singleton{}]] S31 {};
} // namespace bench_startup_small