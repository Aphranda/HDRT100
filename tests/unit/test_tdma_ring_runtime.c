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

typedef struct {
    bool started;
    uint32_t start_count;
    uint32_t stop_count;
    tdma_ring_adapter_status_t status;
} fake_ring_adapter_t;

static bool fake_ring_start(void *context,
                            const tdma_ring_runtime_config_t *config)
{
    fake_ring_adapter_t *adapter = (fake_ring_adapter_t *)context;
    if (adapter == NULL || config == NULL || config->enabled == 0u) {
        return false;
    }
    adapter->started = true;
    adapter->start_count++;
    return true;
}

static void fake_ring_stop(void *context)
{
    fake_ring_adapter_t *adapter = (fake_ring_adapter_t *)context;
    if (adapter != NULL && adapter->started) {
        adapter->started = false;
        adapter->stop_count++;
    }
}

static bool fake_ring_service(void *context,
                              uint64_t now_ns,
                              tdma_ring_adapter_status_t *status)
{
    fake_ring_adapter_t *adapter = (fake_ring_adapter_t *)context;
    (void)now_ns;
    if (adapter == NULL || status == NULL || !adapter->started) {
        return false;
    }
    *status = adapter->status;
    return true;
}

static const tdma_ring_adapter_ops_t s_fake_ring_ops = {
    .start = fake_ring_start,
    .stop = fake_ring_stop,
    .service = fake_ring_service,
};

int main(void)
{
    int failed = 0;
    tdma_ring_runtime_t runtime;
    tdma_ring_runtime_snapshot_t snapshot;
    fake_ring_adapter_t adapter = {0};
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
        .feedback_timeout_ns = 10000u,
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
    failed += expect_bool("snapshot without adapter",
                          tdma_ring_runtime_get_snapshot(&runtime, &snapshot),
                          true);
    failed += expect_u32("config seq", snapshot.config_seq, 1u);
    failed += expect_u32("service seq", snapshot.service_seq, 1u);
    failed += expect_u32("up not running without adapter", snapshot.up_running, 0u);
    failed += expect_u32("down not running without adapter", snapshot.down_running, 0u);
    failed += expect_u32("adapter missing reason",
                         snapshot.last_reason,
                         TDMA_RING_RUNTIME_REASON_ADAPTER_MISSING);
    failed += expect_u32("no fake feedback",
                         snapshot.simultaneous_feedback_loop_evidence,
                         0u);

    adapter.status.up_configured = 1u;
    adapter.status.down_configured = 1u;
    adapter.status.up_running = 1u;
    adapter.status.down_running = 1u;
    adapter.status.up_tx_sequence = 7u;
    adapter.status.down_rx_sequence = 7u;
    adapter.status.up_tx_frame_crc32 = 0xAABBCCDDu;
    adapter.status.down_rx_frame_crc32 = 0xAABBCCDDu;
    adapter.status.schedule_crc32 = valid.schedule_crc32;
    adapter.status.timestamp_resolution_ns = 1000u;
    adapter.status.timestamp_flags =
        TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    adapter.status.reference_tx_timestamp_ns = 1000000ull;
    adapter.status.feedback_rx_timestamp_ns = 1000500ull;
    adapter.status.idle_beacon_tx_count = 3u;
    adapter.status.idle_beacon_rx_count = 2u;
    failed += expect_bool("bind adapter",
                          tdma_ring_runtime_bind_adapter(&runtime,
                                                         &s_fake_ring_ops,
                                                         &adapter),
                          true);
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("adapter started", snapshot.adapter_started, 1u);
    failed += expect_u32("up running with adapter", snapshot.up_running, 1u);
    failed += expect_u32("down running with adapter", snapshot.down_running, 1u);
    failed += expect_u32("diagnostic timestamp rejected",
                         snapshot.simultaneous_feedback_loop_evidence,
                         0u);
    failed += expect_u32("timestamp missing reason",
                         snapshot.last_reason,
                         TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING);

    adapter.status.timestamp_resolution_ns = 100u;
    adapter.status.timestamp_flags =
        TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    adapter.status.up_tx_sequence = 8u;
    adapter.status.down_rx_sequence = 8u;
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("hardware feedback accepted",
                         snapshot.simultaneous_feedback_loop_evidence,
                         1u);
    failed += expect_u32("feedback round trip",
                         snapshot.feedback_round_trip_ns,
                         500u);
    failed += expect_u32("idle tx evidence",
                         snapshot.idle_beacon_tx_count,
                         3u);
    failed += expect_u32("idle rx evidence",
                         snapshot.idle_beacon_rx_count,
                         2u);

    adapter.status.feedback_rx_timestamp_ns = 1020000ull;
    adapter.status.up_tx_sequence = 9u;
    adapter.status.down_rx_sequence = 9u;
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("feedback timeout rejects evidence",
                         snapshot.simultaneous_feedback_loop_evidence,
                         0u);
    failed += expect_u32("feedback timeout reason",
                         snapshot.last_reason,
                         TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING);

    adapter.status.feedback_rx_timestamp_ns = 1000500ull;
    adapter.status.up_tx_sequence = 10u;
    adapter.status.down_rx_sequence = 11u;
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("sequence mismatch rejects feedback",
                         snapshot.simultaneous_feedback_loop_evidence,
                         0u);
    failed += expect_u32("sequence mismatch reason",
                         snapshot.last_reason,
                         TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING);

    failed += expect_bool("disable",
                          tdma_ring_runtime_configure(&runtime, NULL),
                          true);
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("disabled", snapshot.enabled, 0u);
    failed += expect_u32("disabled legs",
                         snapshot.up_running | snapshot.down_running,
                         0u);
    failed += expect_u32("adapter stopped", adapter.stop_count, 1u);

    if (failed != 0) {
        return 1;
    }
    puts("tdma_ring_runtime tests passed");
    return 0;
}
