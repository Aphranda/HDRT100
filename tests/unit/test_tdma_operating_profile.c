#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tdma_operating_profile.h"
#include "tdma_pio_spi_phys_timing.h"
#include "tdma_maintenance_runtime.h"

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
    failed += expect_bool("level 19 low-speed",
                          tdma_operating_profile_get(19u, &profile), true);
    failed += expect_u32("1 MHz baud", profile.baud_hz, 1000000u);
    failed += expect_u32("1 MHz period", profile.cycle_period_ns, 2000000u);

    failed += expect_bool("low-rate 10 kHz exists",
                          tdma_operating_profile_get(23u, &profile), true);
    failed += expect_u32("10 kHz baud", profile.baud_hz, 10000u);
    failed += expect_u32("10 kHz slot", profile.cycle_period_ns, 150000000u);
    failed += expect_u32("10 kHz level", profile.level, 23u);
    failed += expect_bool("10 kHz validates",
                          tdma_operating_profile_validate(&profile, &result), true);
    const uint32_t lowrate_10k_crc = profile.profile_crc32;
    failed += expect_bool("low-rate 100 kHz exists",
                          tdma_operating_profile_get(24u, &profile), true);
    failed += expect_u32("100 kHz baud", profile.baud_hz, 100000u);
    failed += expect_u32("100 kHz slot", profile.cycle_period_ns, 15000000u);
    failed += expect_bool("low-rate 5 MHz exists",
                          tdma_operating_profile_get(25u, &profile), true);
    failed += expect_u32("5 MHz baud", profile.baud_hz, 5000000u);
    failed += expect_u32("5 MHz slot", profile.cycle_period_ns, 500000u);
    failed += expect_u32("profile count (4-node image)",
                         TDMA_OPERATING_PROFILE_COUNT, 26u);
    failed += expect_bool("8-node 10 kHz absent",
                          tdma_operating_profile_get(26u, &profile), false);
    failed += expect_bool("find pair 10 kHz / 150 ms",
                          tdma_operating_profile_find_pair(
                              10000u, 150000000u, &profile), true);
    failed += expect_u32("pair level", profile.level, 23u);
    failed += expect_bool("find pair 10 kHz / 260 ms absent",
                          tdma_operating_profile_find_pair(
                              10000u, 260000000u, &profile), false);
    failed += expect_bool("150 ms covers 4 nodes",
                          tdma_operating_profile_slot_sufficient(
                              10000u, 150000000u, 4u), true);
    failed += expect_bool("150 ms rejects 8 nodes",
                          tdma_operating_profile_slot_sufficient(
                              10000u, 150000000u, 8u), false);
    uint64_t slot_min_ns = 0u;
    failed += expect_bool("slot min 4 nodes",
                          tdma_operating_profile_slot_min_ns(
                              10000u, 4u, 1000u, 1u, 0u, &slot_min_ns), true);
    failed += expect_u32("slot min 4 nodes value", (uint32_t)slot_min_ns,
                         139733334u);
    failed += expect_bool("admit 10 kHz / 150 ms / 4 nodes",
                          tdma_operating_profile_admitted_for_ring(
                              10000u, 150000000u, lowrate_10k_crc, 4u), true);
    failed += expect_bool("reject 8 nodes on 4-node profile",
                          tdma_operating_profile_admitted_for_ring(
                              10000u, 150000000u, lowrate_10k_crc, 8u), false);
    failed += expect_bool("legacy 1 MHz range stays admitted",
                          tdma_operating_profile_admitted_for_ring(
                              1000000u, 1000000u, 123u, 4u), true);
    failed += expect_u32("tx margin follows 10 kHz slot",
                         (uint32_t)tdma_pio_spi_phys_tx_completion_margin_ns(
                             150000000u), 300000000u);
    failed += expect_u32("tx margin follows 2 ms slot",
                         (uint32_t)tdma_pio_spi_phys_tx_completion_margin_ns(
                             2000000u), 4000000u);
    failed += expect_u32("tx margin keeps 1 ms floor",
                         (uint32_t)tdma_pio_spi_phys_tx_completion_margin_ns(
                             0u), 1000000u);
    failed += expect_u32("ack timeout keeps 50 ms base",
                         tdma_maintenance_ack_timeout_ms(0u), 50u);
    failed += expect_u32("ack timeout short ring stays base",
                         tdma_maintenance_ack_timeout_ms(8000000u), 50u);
    failed += expect_u32("ack timeout 100 kHz ring",
                         tdma_maintenance_ack_timeout_ms(60000000u), 120u);
    failed += expect_u32("ack timeout 10 kHz ring",
                         tdma_maintenance_ack_timeout_ms(600000000u), 1200u);
    failed += expect_u32("ack timeout clamps at 10 s",
                         tdma_maintenance_ack_timeout_ms(UINT32_MAX), 8590u);

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
