#include "calibration_manager.h"

#include <string.h>

#include "board.h"
#include "osal.h"
#include "tdma_runtime_owner.h"

#define CALIBRATION_MANAGER_DEFAULT_CRC32 0x10000003u

static calibration_manager_status_t s_status;
static bool s_ready;
static calibration_manager_loopback_snapshot_t s_loopback_snapshot;
static uint32_t s_loopback_processed_epoch;

bool calibration_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_status, 0, sizeof(s_status));
    memset(&s_loopback_snapshot, 0, sizeof(s_loopback_snapshot));
    s_loopback_processed_epoch = 0u;
    s_status.last_service_ms = now_ms;
    s_status.command_seq = 1u;
    s_status.link_count = 1u;
    s_status.delay_count = 1u;
    s_status.active_crc32 = CALIBRATION_MANAGER_DEFAULT_CRC32;
    s_ready = false;
    return calibration_pio_loopback_init();
}

void calibration_manager_set_ready(bool ready)
{
    osal_critical_enter();
    s_ready = ready;
    s_status.ready = ready;
    osal_critical_exit();
}

void calibration_manager_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_status.service_count == 0u) {
        s_status.first_service_ms = now_ms;
    }
    s_status.service_count++;
    s_status.last_service_ms = now_ms;
    s_status.ready = s_ready;
    s_status.state = 0u;
    osal_critical_exit();

    calibration_pio_loopback_snapshot_t raw;
    if (calibration_pio_loopback_get_snapshot(&raw) && raw.complete != 0u &&
        raw.epoch != s_loopback_processed_epoch) {
        calibration_bidirectional_sample_t sample = {
            .t1_clk_tx = raw.t1_clk_tx,
            .t2_clk_rx = raw.t2_clk_rx,
            .t3_data_tx = raw.t3_data_tx,
            .t4_data_rx = raw.t4_data_rx,
            .train_epoch = raw.epoch,
            .train_sequence = raw.epoch,
            .persona_generation = 1u,
            .sample_flags = CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED |
                            CALIBRATION_BIDIRECTIONAL_FLAG_DIAGNOSTIC_ONLY |
                            CALIBRATION_BIDIRECTIONAL_FLAG_DMA_COMPLETE |
                            CALIBRATION_BIDIRECTIONAL_FLAG_REFERENCE_LOOPBACK,
            .edge_mask = raw.edge_mask,
            .clock_rate_error_bound_ns = raw.sample_period_ns,
            .reference_loopback = true,
        };
        if ((raw.flags & (1u << 2u)) != 0u) {
            sample.sample_flags |= CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH;
        }
        const calibration_bidirectional_gate_t gate = {
            .required_sample_flags = CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED |
                                     CALIBRATION_BIDIRECTIONAL_FLAG_DMA_COMPLETE |
                                     CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH,
            .required_edge_mask = CALIBRATION_BIDIRECTIONAL_EDGE_ALL,
            .expected_persona_generation = 1u,
            .max_clock_rate_error_bound_ns = raw.sample_period_ns,
            .allow_reference_loopback = true,
        };
        calibration_manager_loopback_snapshot_t next = { .raw = raw };
        next.result_valid = calibration_bidirectional_evaluate(
            &sample, &gate, &next.result) ? 1u : 0u;
        osal_critical_enter();
        s_loopback_snapshot = next;
        s_loopback_processed_epoch = raw.epoch;
        s_status.state = next.result_valid != 0u ? 2u : 3u;
        s_status.last_error = next.result.reject_reason;
        osal_critical_exit();
    }
}

bool calibration_manager_start_loopback(uint32_t sample_words)
{
    calibration_pio_loopback_config_t config = {
        .sample_hz = 50000000u,
        .sample_words = sample_words == 0u ? 128u : sample_words,
        .epoch = s_status.command_seq + 1u,
    };
    const bool accepted = calibration_pio_loopback_request_start(&config);
    if (accepted) {
        osal_critical_enter();
        s_status.command_seq++;
        s_status.state = 1u;
        s_status.last_error = 0u;
        osal_critical_exit();
    }
    return accepted;
}

void calibration_manager_stop_loopback(void)
{
    calibration_pio_loopback_request_stop();
}

void calibration_manager_service_core1(void)
{
    tdma_ring_runtime_snapshot_t ring;
    const bool stopped = tdma_runtime_owner_get_ring_snapshot(&ring) &&
                         ring.enabled == 0u;
    calibration_pio_loopback_service_core1(stopped);
}

bool calibration_manager_get_loopback_snapshot(
    calibration_manager_loopback_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    calibration_pio_loopback_snapshot_t raw;
    if (!calibration_pio_loopback_get_snapshot(&raw)) return false;
    osal_critical_enter();
    *snapshot = s_loopback_snapshot;
    osal_critical_exit();
    snapshot->raw = raw;
    return true;
}

void calibration_manager_get_status(calibration_manager_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_status;
    status->ready = s_ready;
    osal_critical_exit();
}
