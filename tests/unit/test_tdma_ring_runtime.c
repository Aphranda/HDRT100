#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tdma_ring_runtime.h"

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %u expected %u\n",
            name, actual ? 1u : 0u, expected ? 1u : 0u);
    return 1;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %lu expected %lu\n",
            name, (unsigned long)actual, (unsigned long)expected);
    return 1;
}

int main(void)
{
    int failed = 0;
    tdma_ring_runtime_t runtime;
    tdma_ring_runtime_snapshot_t snapshot;
    const tdma_ring_runtime_config_t bad_direction = {
        .enabled = 1u,
        .node_count = 4u,
        .local_slot_id = 1u,
        .reference_slot_id = 0u,
        .up_group_id = 1u,
        .down_group_id = 1u,
        .flags = TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN,
        .ring_profile_crc32 = 0x11223344u,
        .schedule_crc32 = 0x55667788u,
    };
    tdma_ring_runtime_config_t valid = bad_direction;
    valid.down_group_id = 2u;

    failed += expect_bool("init", tdma_ring_runtime_init(&runtime), true);
    failed += expect_bool("reject direction conflict",
                          tdma_ring_runtime_configure(&runtime, &bad_direction),
                          false);
    failed += expect_bool("snapshot after reject",
                          tdma_ring_runtime_get_snapshot(&runtime, &snapshot),
                          true);
    failed += expect_u32("config reject count",
                         snapshot.config_reject_count,
                         1u);
    failed += expect_u32("direction reason",
                         snapshot.last_reason,
                         TDMA_RING_RUNTIME_REASON_DIRECTION_CONFLICT);

    failed += expect_bool("configure valid",
                          tdma_ring_runtime_configure(&runtime, &valid),
                          true);
    tdma_ring_runtime_service(&runtime);
    failed += expect_bool("snapshot running",
                          tdma_ring_runtime_get_snapshot(&runtime, &snapshot),
                          true);
    failed += expect_u32("config seq", snapshot.config_seq, 1u);
    failed += expect_u32("service seq", snapshot.service_seq, 1u);
    failed += expect_u32("up running", snapshot.up_running, 1u);
    failed += expect_u32("down running", snapshot.down_running, 1u);
    failed += expect_u32("ring seq", snapshot.ring_seq, 1u);
    failed += expect_u32("no fake feedback",
                         snapshot.simultaneous_feedback_loop_evidence,
                         0u);

    failed += expect_bool("disable",
                          tdma_ring_runtime_configure(&runtime, NULL),
                          true);
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("disabled", snapshot.enabled, 0u);
    failed += expect_u32("disabled legs",
                         snapshot.up_running | snapshot.down_running,
                         0u);

    if (failed != 0) {
        return 1;
    }
    puts("tdma_ring_runtime tests passed");
    return 0;
}
