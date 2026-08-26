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
    uint32_t train_count;
    uint32_t train_service_count;
    uint32_t train_cycles;
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

static bool fake_ring_train(void *context, uint32_t cycles)
{
    fake_ring_adapter_t *adapter = (fake_ring_adapter_t *)context;
    if (adapter == NULL || !adapter->started || cycles == 0u) {
        return false;
    }
    adapter->train_count++;
    adapter->train_cycles = cycles;
    return true;
}

static void fake_ring_train_service(void *context, uint64_t now_ns)
{
    fake_ring_adapter_t *adapter = (fake_ring_adapter_t *)context;
    (void)now_ns;
    if (adapter != NULL && adapter->started) {
        adapter->train_service_count++;
    }
}

static const tdma_ring_adapter_ops_t s_fake_ring_ops = {
    .start = fake_ring_start,
    .stop = fake_ring_stop,
    .train_clock = fake_ring_train,
    .train_clock_service = fake_ring_train_service,
    .service = fake_ring_service,
};

int main(void)
{
    int failed = 0;
    tdma_ring_runtime_t runtime;
    tdma_ring_runtime_snapshot_t snapshot;
    tdma_ring_calibration_stage_t calibration = {0};
    tdma_ring_runtime_reason_t calibration_reason =
        TDMA_RING_RUNTIME_REASON_NONE;
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
        .operating_profile_crc32 = 0x99AABBCCu,
        .baud_hz = 10000000u,
        .cycle_period_ns = 1000u,
        .loop_delay_ns = 700u,
        .loop_delay_tolerance_ns = 300u,
        .feedback_timeout_ns = 10000u,
        .tx_dma_channel_id = TDMA_PROFILE_DEFAULT_TX_DMA_CHANNEL_ID,
        .rx_dma_channel_id = TDMA_PROFILE_DEFAULT_RX_DMA_CHANNEL_ID,
    };
    tdma_ring_runtime_config_t valid = bad_direction;
    valid.down_group_id = 2u;

    failed += expect_bool("init", tdma_ring_runtime_init(&runtime), true);
    calibration.enabled = 1u;
    calibration.node_count = 4u;
    calibration.evidence_flags = TDMA_RING_CALIBRATION_REQUIRED_FLAGS;
    calibration.calibration_generation = 61u;
    calibration.topology_generation = 13u;
    calibration.topology_crc32 = 0x11223344u;
    calibration.profile_crc32 = 0x55667788u;
    calibration.schedule_crc32 = 0x99AABBCCu;
    for (uint32_t i = 0u; i < calibration.node_count; i++) {
        calibration.links[i].valid = 1u;
        calibration.links[i].link_index = i;
        calibration.links[i].evidence_flags =
            TDMA_RING_CALIBRATION_REQUIRED_FLAGS;
        calibration.links[i].calibration_generation =
            calibration.calibration_generation;
        calibration.links[i].topology_generation =
            calibration.topology_generation;
        calibration.links[i].topology_crc32 = calibration.topology_crc32;
        calibration.links[i].profile_crc32 = calibration.profile_crc32;
        calibration.links[i].schedule_crc32 = calibration.schedule_crc32;
        calibration.links[i].pio_persona = 1u;
        calibration.links[i].clkdiv_q16 = 1u << 16;
        calibration.links[i].clk_sys_hz = 250000000u;
        calibration.links[i].instruction_period_ns = 4u;
        calibration.links[i].bit_cycles = 6u;
        calibration.links[i].marker_to_data_cycles = 10u;
        calibration.links[i].forward_residence_cycles = 1u;
        calibration.links[i].rx_arm_lead_cycles = 1u;
        calibration.links[i].codeword_cycles = 32u;
        calibration.links[i].guard_cycles = 2u;
        calibration.links[i].link_budget_cycles = 64u;
        calibration.links[i].loop_delay_cycles = 8u;
        calibration.links[i].sample_period_ns = 4u;
        calibration.links[i].link_base_delay_ns = 40u;
        calibration.links[i].marker_phase_delay_cycles = 10u;
        calibration.links[i].sck_phase_delay_cycles = 10u;
        calibration.links[i].data_phase_delay_cycles = 1u;
    }
    failed += expect_bool("valid calibration stage",
                          tdma_ring_runtime_validate_calibration_stage(
                              &calibration, 4u, &calibration_reason),
                          true);
    calibration.links[2].profile_crc32++;
    failed += expect_bool("mixed calibration identity rejected",
                          tdma_ring_runtime_validate_calibration_stage(
                              &calibration, 4u, &calibration_reason),
                          false);
    failed += expect_u32("calibration gate reason",
                         calibration_reason,
                         TDMA_RING_RUNTIME_REASON_BAD_CONFIG);
    calibration.links[2].profile_crc32 = calibration.profile_crc32;
    calibration.links[0].link_budget_cycles = 1u;
    failed += expect_bool("unreplayable budget rejected",
                          tdma_ring_runtime_validate_calibration_stage(
                              &calibration, 4u, &calibration_reason),
                          false);
    calibration.links[0].link_budget_cycles = 64u;
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
    adapter.status.feedback_reference_sequence = 7u;
    adapter.status.feedback_reference_frame_crc32 = 0xAABBCCDDu;
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
    failed += expect_bool("start staged data",
                          tdma_ring_runtime_set_data_enabled(&runtime, true),
                          true);
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
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
    adapter.status.feedback_reference_sequence = 8u;
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("hardware feedback accepted",
                         snapshot.simultaneous_feedback_loop_evidence,
                         1u);
    failed += expect_u32("feedback round trip",
                         snapshot.feedback_round_trip_ns,
                         500u);
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("hardware feedback event is not counted twice",
                         snapshot.simultaneous_feedback_loop_evidence,
                         0u);
    failed += expect_u32("feedback round trip remains readable",
                         snapshot.feedback_round_trip_ns,
                         500u);
    failed += expect_u32("loop delay staged",
                         snapshot.loop_delay_ns,
                         700u);
    failed += expect_u32("loop delay tolerance staged",
                         snapshot.loop_delay_tolerance_ns,
                         300u);
    failed += expect_u32("idle tx evidence",
                         snapshot.idle_beacon_tx_count,
                         3u);
    failed += expect_u32("idle rx evidence",
                         snapshot.idle_beacon_rx_count,
                         2u);

    adapter.status.feedback_rx_timestamp_ns = 1020000ull;
    adapter.status.up_tx_sequence = 9u;
    adapter.status.down_rx_sequence = 9u;
    adapter.status.feedback_reference_sequence = 9u;
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("feedback timeout rejects evidence",
                         snapshot.simultaneous_feedback_loop_evidence,
                         0u);
    failed += expect_u32("feedback timeout reason",
                         snapshot.last_reason,
                         TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING);

    adapter.status.feedback_rx_timestamp_ns = 1000300ull;
    adapter.status.up_tx_sequence = 12u;
    adapter.status.down_rx_sequence = 12u;
    adapter.status.feedback_reference_sequence = 12u;
    tdma_ring_runtime_service(&runtime);
    (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
    failed += expect_u32("loop delay lower bound rejects early feedback",
                         snapshot.simultaneous_feedback_loop_evidence,
                         0u);

    adapter.status.feedback_rx_timestamp_ns = 1000500ull;
    adapter.status.up_tx_sequence = 10u;
    adapter.status.down_rx_sequence = 11u;
    adapter.status.feedback_reference_sequence = 10u;
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

    /* Training is a core0 -> core1 command slot. Submission must not invoke
     * the physical adapter inline, and START must restore the normal adapter
     * persona before cyclic service resumes. */
    {
        tdma_ring_runtime_t train_runtime;
        tdma_ring_runtime_snapshot_t train_snapshot;
        fake_ring_adapter_t train_adapter = {0};
        failed += expect_bool("train runtime init",
                              tdma_ring_runtime_init(&train_runtime),
                              true);
        failed += expect_bool("train runtime configure",
                              tdma_ring_runtime_configure(&train_runtime,
                                                          &valid),
                              true);
        failed += expect_bool("train runtime bind",
                              tdma_ring_runtime_bind_adapter(
                                  &train_runtime,
                                  &s_fake_ring_ops,
                                  &train_adapter),
                              true);
        tdma_ring_runtime_service(&train_runtime);
        failed += expect_bool("train submit accepted",
                              tdma_ring_runtime_train_clock(&train_runtime,
                                                            100u),
                              true);
        failed += expect_u32("train not inline", train_adapter.train_count, 0u);
        failed += expect_bool("second pending train rejected",
                              tdma_ring_runtime_train_clock(&train_runtime,
                                                            1000u),
                              false);
        failed += expect_bool("start rejected while train pending",
                              tdma_ring_runtime_set_data_enabled(&train_runtime,
                                                                 true),
                              false);
        tdma_ring_runtime_service(&train_runtime);
        (void)tdma_ring_runtime_get_snapshot(&train_runtime,
                                             &train_snapshot);
        failed += expect_u32("train starts on owner", train_adapter.train_count, 1u);
        failed += expect_u32("train cycles delivered",
                             train_adapter.train_cycles,
                             100u);
        failed += expect_u32("train service on owner",
                             train_adapter.train_service_count,
                             1u);
        failed += expect_u32("train request published",
                             train_snapshot.train_request_seq,
                             1u);
        failed += expect_u32("train accepted published",
                             train_snapshot.train_accepted_seq,
                             1u);
        failed += expect_u32("training persona dirty",
                             train_snapshot.training_dirty,
                             1u);
        failed += expect_bool("start accepted after train consume",
                              tdma_ring_runtime_set_data_enabled(&train_runtime,
                                                                 true),
                              true);
        tdma_ring_runtime_service(&train_runtime);
        (void)tdma_ring_runtime_get_snapshot(&train_runtime,
                                             &train_snapshot);
        failed += expect_u32("training adapter stopped before data",
                             train_adapter.stop_count,
                             1u);
        failed += expect_u32("adapter stopped for restore",
                             train_snapshot.adapter_started,
                             0u);
        failed += expect_u32("training dirty cleared",
                             train_snapshot.training_dirty,
                             0u);
        tdma_ring_runtime_service(&train_runtime);
        (void)tdma_ring_runtime_get_snapshot(&train_runtime,
                                             &train_snapshot);
        failed += expect_u32("normal adapter restarted",
                             train_adapter.start_count,
                             2u);
        failed += expect_u32("normal data service resumed",
                             train_snapshot.adapter_service_count,
                             1u);
    }

    runtime.config_guard = 1u;
    failed += expect_bool("odd config guard is bounded",
                          tdma_ring_runtime_get_snapshot(&runtime, &snapshot),
                          false);
    runtime.config_guard = 0u;
    runtime.result_guard = 1u;
    failed += expect_bool("odd result guard is bounded",
                          tdma_ring_runtime_get_snapshot(&runtime, &snapshot),
                          false);

    if (failed != 0) {
        return 1;
    }
    puts("tdma_ring_runtime tests passed");
    return 0;
}
