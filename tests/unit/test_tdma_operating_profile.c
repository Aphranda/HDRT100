#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tdma_operating_profile.h"

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %lu expected %lu\n",
            name, (unsigned long)actual, (unsigned long)expected);
    return 1;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    return expect_u32(name, actual ? 1u : 0u, expected ? 1u : 0u);
}

int main(void)
{
    int failed = 0;
    tdma_operating_profile_t profile;
    tdma_operating_profile_result_t result;
    tdma_operating_profile_manager_t manager;

    failed += expect_bool("level 0", tdma_operating_profile_get(0u, &profile), true);
    failed += expect_u32("safe baud", profile.baud_hz, 10000000u);
    failed += expect_u32("safe period", profile.cycle_period_ns, 2000000u);
    failed += expect_u32("safe candidate", profile.flags, 0u);
    failed += expect_bool("safe validates",
                          tdma_operating_profile_validate(&profile, &result), true);
    failed += expect_u32("safe validation result", result,
                         TDMA_OPERATING_PROFILE_OK);

    failed += expect_bool("level 1", tdma_operating_profile_get(1u, &profile), true);
    failed += expect_u32("25 MHz candidate", profile.flags, 0u);

    failed += expect_bool("level 6", tdma_operating_profile_get(6u, &profile), true);
    failed += expect_u32("fast baud", profile.baud_hz, 50000000u);
    failed += expect_u32("fast period", profile.cycle_period_ns, 2000000u);
    failed += expect_u32("fast candidate", profile.flags, 0u);
    failed += expect_bool("level 18", tdma_operating_profile_get(18u, &profile), true);
    failed += expect_u32("100 us baud", profile.baud_hz, 50000000u);
    failed += expect_u32("100 us period", profile.cycle_period_ns, 100000u);
    failed += expect_bool("level 19 rejected",
                          tdma_operating_profile_get(19u, &profile), false);
    failed += expect_bool("find 35 MHz",
                          tdma_operating_profile_find_baud(35000000u, &profile), true);
    failed += expect_u32("35 MHz level", profile.level, 3u);
    failed += expect_bool("reject arbitrary baud",
                          tdma_operating_profile_find_baud(33000000u, &profile), false);

    failed += expect_bool("manager init",
                          tdma_operating_profile_manager_init(&manager, 0u), true);
    failed += expect_bool("stage level 4",
                          tdma_operating_profile_manager_stage(&manager, 4u), true);
    failed += expect_bool("busy apply rejected",
                          tdma_operating_profile_manager_apply(&manager, false), false);
    failed += expect_u32("active unchanged", manager.active.level, 0u);
    failed += expect_u32("busy result", manager.last_result,
                         TDMA_OPERATING_PROFILE_BUSY);
    failed += expect_bool("stopped apply",
                          tdma_operating_profile_manager_apply(&manager, true), true);
    failed += expect_u32("active updated", manager.active.level, 4u);

    profile = manager.active;
    profile.cycle_period_ns++;
    failed += expect_bool("mutated profile rejected",
                          tdma_operating_profile_validate(&profile, &result), false);

    if (failed != 0) {
        return 1;
    }
    puts("tdma_operating_profile tests passed");
    return 0;
}
