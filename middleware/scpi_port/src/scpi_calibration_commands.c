#include "scpi_calibration_commands.h"
#include "scpi_calibration_commands_internal.h"

#include "calibration_manager.h"
#include "board_config.h"
#include "distributed_config.h"
#include "sma_cable_delay.h"
#include "sma_cable_delay_pio.h"
#include "sync_io.h"
#include "tdma_runtime_owner.h"

#define SCPI_SMA_CABLE_CAPTURE_MAX_WORDS 512u
#define SCPI_SMA_CABLE_CAPTURE_DEFAULT_WORDS 256u
#define SCPI_SMA_CABLE_CAPTURE_TIMEOUT_US 100000u
#define SCPI_SMA_CABLE_RTT_RESPONDER_TIMEOUT_US 2000000u

static uint32_t s_scpi_sma_cable_capture[SCPI_SMA_CABLE_CAPTURE_MAX_WORDS];

static void scpi_calibration_origin_record_fields(scpi_t *context,
    const tdma_origin_record_frozen_t *snapshot)
{
    SCPI_ResultUInt32(context, snapshot->epoch);
    SCPI_ResultUInt32(context, snapshot->published_version);
    SCPI_ResultUInt32(context, snapshot->fault);
    const tdma_origin_record_t *r = &snapshot->record;
    SCPI_ResultUInt32(context, r->observation.sequence);
    SCPI_ResultUInt32(context, r->observation.identity);
    SCPI_ResultUInt32(context, r->observation.local_generation);
    SCPI_ResultUInt32(context, r->observation.output_remaining);
    SCPI_ResultUInt32(context, r->observation.rtt_remaining);
    SCPI_ResultUInt32(context, r->observation.rtt_present);
    SCPI_ResultUInt32(context, r->capture_remaining);
    SCPI_ResultUInt32(context, r->returned_trailer);
    SCPI_ResultUInt32(context, r->epoch);
    SCPI_ResultUInt32(context, r->flags);
    SCPI_ResultUInt32(context, r->format);
    SCPI_ResultUInt32(context, r->sequence_end);
    for (uint32_t i = 0u; i < 3u; ++i) SCPI_ResultUInt32(context, r->raw_time.arm_before[i]);
    for (uint32_t i = 0u; i < 3u; ++i) SCPI_ResultUInt32(context, r->raw_time.arm_after[i]);
    SCPI_ResultUInt32(context, r->raw_time.latch_remaining);
    SCPI_ResultUInt32(context, r->raw_time.latch_fstat);
    SCPI_ResultUInt32(context, r->raw_time.arm_padout);
    SCPI_ResultUInt32(context, r->raw_time.tick_hz);
}

scpi_result_t scpi_calibration_origin_record_q(scpi_t *context)
{
    uint32_t age;
    tdma_origin_record_frozen_t snapshot;
    if (!SCPI_ParamUInt32(context, &age, TRUE)) return SCPI_RES_ERR;
    if (!tdma_runtime_owner_get_origin_frozen_record(age, &snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINRECORD");
    SCPI_ResultUInt32(context, age);
    scpi_calibration_origin_record_fields(context, &snapshot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_live_q(scpi_t *context)
{
    tdma_origin_live_snapshot_t snapshot;
    if (!tdma_runtime_owner_get_origin_live_snapshot(&snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINLIVE");
    SCPI_ResultUInt32(context, snapshot.retained);
    SCPI_ResultUInt32(context, snapshot.active);
    SCPI_ResultUInt32(context, snapshot.copy_count);
    SCPI_ResultUInt32(context, snapshot.reject_count);
    scpi_calibration_origin_record_fields(context, &snapshot.sample);
    return SCPI_RES_OK;
}

/* Why the origin lifetime path last stopped the ring (TDMA_ORIGIN_STOP_* cause,
 * TDMA_ORIGIN_AUTH_* failed terms, TDMA_ORIGIN_FAULT_* first fault site,
 * then the first BEGIN refusal code with its observed/expected values). */
scpi_result_t scpi_calibration_origin_stop_q(scpi_t *context)
{
    tdma_origin_stop_cause_t s;
    if (!tdma_runtime_owner_get_origin_stop_cause(&s)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINSTOP");
    SCPI_ResultUInt32(context, s.stop_count);
    SCPI_ResultUInt32(context, s.trial_epoch);
    SCPI_ResultUInt32(context, s.origin_epoch);
    SCPI_ResultUInt32(context, s.cause);
    SCPI_ResultUInt32(context, s.auth_mask);
    SCPI_ResultUInt32(context, s.fault_site);
    SCPI_ResultUInt32(context, s.comm_state);
    SCPI_ResultUInt32(context, s.comm_last_error);
    SCPI_ResultUInt32(context, s.trial_config_seq);
    SCPI_ResultUInt32(context, s.ring_config_seq);
    SCPI_ResultUInt32(context, s.release_state);
    SCPI_ResultUInt64(context, s.stop_ticks);
    SCPI_ResultUInt32(context, s.begin_reject);
    SCPI_ResultUInt32(context, s.begin_observed);
    SCPI_ResultUInt32(context, s.begin_expected);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_runtime_q(scpi_t *context)
{
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    if (!tdma_pio_spi_ring_adapter_try_get_snapshot(tdma_runtime_owner_get_ring_adapter(), &snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINRUNTIME");
    SCPI_ResultUInt32(context, snapshot.origin.active);
    SCPI_ResultUInt32(context, snapshot.comm_fsm_state);
    SCPI_ResultUInt32(context, snapshot.comm_fsm_last_error);
    SCPI_ResultUInt32(context, snapshot.origin.observed_return_count);
    SCPI_ResultUInt32(context, snapshot.origin.unobserved_cycle_count);
    SCPI_ResultUInt32(context, snapshot.origin.rejected_observation_count);
    SCPI_ResultUInt32(context, snapshot.origin.published_generation);
    SCPI_ResultUInt32(context, snapshot.origin.published_owner_generation);
    SCPI_ResultUInt32(context, snapshot.origin.matched_owner_generation);
    SCPI_ResultUInt32(context, snapshot.origin.returned.sequence);
    SCPI_ResultUInt32(context, snapshot.origin.returned.identity);
    SCPI_ResultUInt32(context, snapshot.origin.returned.local_generation);
    SCPI_ResultUInt32(context, snapshot.origin.boundary.sequence);
    SCPI_ResultUInt32(context, snapshot.origin.boundary.identity);
    SCPI_ResultUInt32(context, snapshot.origin.boundary.local_generation);
    SCPI_ResultUInt32(context, snapshot.origin.boundary.output_remaining);
    tdma_origin_schedule_evidence_t schedule_evidence;
    tdma_pio_spi_ring_adapter_schedule_evidence(&schedule_evidence);
    SCPI_ResultUInt32(context, schedule_evidence.allow_total);
    SCPI_ResultUInt32(context, schedule_evidence.block_total);
    SCPI_ResultUInt32(context, snapshot.timestamp_flags);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_calibration_origin_trial_flags(scpi_t *context, uint32_t flags)
{
    uint32_t trial, rearm, polls;
    uint64_t duration;
    if (!SCPI_ParamUInt32(context, &trial, TRUE) ||
        !SCPI_ParamUInt32(context, &rearm, TRUE) ||
        !SCPI_ParamUInt32(context, &polls, TRUE) ||
        !SCPI_ParamUInt64(context, &duration, TRUE)) return SCPI_RES_ERR;
    if (!calibration_manager_origin_trial_configured(trial, rearm, polls, duration, flags)) {
        scpi_port_push_exec_error(context, "CAL_ORIGIN_TRIAL_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, calibration_manager_origin_epoch());
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_trial(scpi_t *context)
{
    return scpi_calibration_origin_trial_flags(context, 0u);
}

scpi_result_t scpi_calibration_origin_trial_no_record(scpi_t *context)
{
    return scpi_calibration_origin_trial_flags(context, CALIBRATION_ORIGIN_DIAGNOSTIC_SKIP_RECORDS);
}

scpi_result_t scpi_calibration_origin_trial_blackout(scpi_t *context)
{
    return scpi_calibration_origin_trial_flags(context, CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT);
}

scpi_result_t scpi_calibration_origin_trial_build_cancel(scpi_t *context)
{
    return scpi_calibration_origin_trial_flags(context, CALIBRATION_ORIGIN_DIAGNOSTIC_BUILD_CANCEL);
}

scpi_result_t scpi_calibration_origin_trial_ready(scpi_t *context)
{
    return scpi_calibration_origin_trial_flags(context, CALIBRATION_ORIGIN_DIAGNOSTIC_DEFER_RELEASE);
}

scpi_result_t scpi_calibration_origin_release(scpi_t *context)
{
    uint32_t trial_epoch, config_seq, request_seq;
    if (!SCPI_ParamUInt32(context, &trial_epoch, TRUE) ||
        !SCPI_ParamUInt32(context, &config_seq, TRUE)) return SCPI_RES_ERR;
    if (!tdma_runtime_owner_request_origin_release(trial_epoch, config_seq, &request_seq)) {
        scpi_port_push_exec_error(context, "CAL_ORIGIN_RELEASE_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, request_seq); /* Accepted intent, not emitted. */
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_release_q(scpi_t *context)
{
    tdma_origin_release_snapshot_t s;
    if (!tdma_runtime_owner_get_origin_release(&s)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINRELEASE");
    SCPI_ResultUInt32(context, 2u);
    SCPI_ResultUInt32(context, s.result.state);
    SCPI_ResultUInt32(context, s.result.trial_epoch);
    SCPI_ResultUInt32(context, s.result.config_seq);
    SCPI_ResultUInt32(context, s.result.seed_sequence);
    SCPI_ResultUInt32(context, s.result.seed_identity);
    SCPI_ResultUInt32(context, s.result.record_epoch);
    SCPI_ResultUInt32(context, s.result.ready_checks);
    SCPI_ResultUInt32(context, s.result.request_seq);
    SCPI_ResultUInt64(context, s.result.ready_ticks);
    SCPI_ResultUInt64(context, s.result.last_ready_ticks);
    SCPI_ResultUInt64(context, s.result.release_ticks);
    SCPI_ResultUInt32(context, s.attempt.attempts);
    SCPI_ResultUInt32(context, s.attempt.rejected);
    SCPI_ResultUInt32(context, s.attempt.reason);
    SCPI_ResultUInt32(context, s.attempt.trial_epoch);
    SCPI_ResultUInt32(context, s.attempt.config_seq);
    SCPI_ResultUInt32(context, s.attempt.request_seq);
    SCPI_ResultUInt32(context, s.result.physical_reject);
    SCPI_ResultUInt32(context, s.result.physical_observed);
    SCPI_ResultUInt32(context, s.result.physical_expected);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_build_cancel_q(scpi_t *context)
{
    tdma_origin_build_probe_t s;
    if (!tdma_runtime_owner_get_origin_build_probe(&s)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINBUILDCANCEL");
    SCPI_ResultUInt32(context, 1u); /* Diagnostic schema, not a timing grant. */
    SCPI_ResultUInt32(context, s.state);
    SCPI_ResultUInt32(context, s.trial_epoch);
    SCPI_ResultUInt32(context, s.config_seq);
    SCPI_ResultUInt32(context, s.emitted_runs);
    SCPI_ResultUInt32(context, s.deferred_cancels);
    SCPI_ResultUInt32(context, s.entries_cleared);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_handoff_q(scpi_t *context)
{
    tdma_origin_handoff_snapshot_t s;
    if (!tdma_runtime_owner_get_origin_handoff(&s)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINHANDOFF");
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.trial_epoch);
    SCPI_ResultUInt32(context, s.config_seq);
    SCPI_ResultUInt32(context, s.clock_hz);
    SCPI_ResultUInt32(context, s.result);
    SCPI_ResultUInt32(context, s.invalid_count);
    SCPI_ResultUInt32(context, s.elapsed_ticks);
    SCPI_ResultUInt64(context, s.begin_ticks);
    SCPI_ResultUInt32(context, TDMA_ORIGIN_HANDOFF_STAGES);
    for (uint32_t i = 0u; i < TDMA_ORIGIN_HANDOFF_STAGES; ++i) {
        SCPI_ResultUInt32(context, s.first_ticks[i]);
        SCPI_ResultUInt32(context, s.work_ticks[i]);
        SCPI_ResultUInt32(context, s.calls[i]);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_blackout_q(scpi_t *context)
{
    tdma_origin_blackout_snapshot_t s;
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled || ring.adapter_started ||
        ring.config_seq != ring.applied_config_seq || !tdma_runtime_owner_get_origin_blackout(&s)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINBLACKOUT");
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.state);
    SCPI_ResultUInt32(context, s.trial_epoch);
    SCPI_ResultUInt32(context, s.config_seq);
    SCPI_ResultUInt32(context, s.record_epoch);
    SCPI_ResultUInt32(context, s.clock_hz);
    SCPI_ResultUInt32(context, s.wait_calls);
    SCPI_ResultUInt32(context, s.skipped_calls);
    SCPI_ResultUInt32(context, s.before_version);
    SCPI_ResultUInt32(context, s.after_version);
    SCPI_ResultUInt64(context, s.begin_ticks);
    SCPI_ResultUInt64(context, s.end_ticks);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_revoke(scpi_t *context)
{
    calibration_manager_origin_revoke();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_origin_diagnostic_q(scpi_t *context)
{
    calibration_origin_attempt_t attempt;
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled || ring.adapter_started ||
        ring.config_seq != ring.applied_config_seq ||
        !calibration_manager_origin_get_attempt(&attempt)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "ORIGINADMISSION");
    SCPI_ResultUInt32(context, 1u); /* Diagnostic schema; not grant authority. */
    SCPI_ResultUInt32(context, attempt.attempt);
    SCPI_ResultUInt32(context, attempt.trial_id);
    SCPI_ResultUInt32(context, attempt.trial_epoch);
    SCPI_ResultUInt32(context, attempt.reason);
    SCPI_ResultUInt32(context, attempt.config_seq);
    SCPI_ResultUInt32(context, attempt.applied_config_seq);
    SCPI_ResultUInt32(context, attempt.recheck_config_seq);
    SCPI_ResultUInt32(context, attempt.recheck_applied_config_seq);
    SCPI_ResultUInt32(context, attempt.admitted_model_epoch);
    SCPI_ResultUInt32(context, attempt.observed_model_epoch);
    SCPI_ResultUInt64(context, attempt.observed);
    SCPI_ResultUInt64(context, attempt.expected);
    SCPI_ResultUInt32(context, attempt.observed_mask);
    SCPI_ResultUInt32(context, attempt.requested_rearm_ticks);
    SCPI_ResultUInt32(context, attempt.requested_abort_polls);
    SCPI_ResultUInt32(context, attempt.requested_flags);
    SCPI_ResultUInt64(context, attempt.requested_duration_ticks);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_origin_q(scpi_t *context)
{
    calibration_origin_timing_t trial;
    if (!calibration_manager_origin_get_timing(&trial)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    /* Publication facts only. Runtime/physical queries remain independent
     * observations and are never represented as one atomic grant snapshot. */
    SCPI_ResultText(context, "ORIGINTRIAL");
    SCPI_ResultUInt32(context, trial.version);
    SCPI_ResultUInt32(context, trial.epoch);
    SCPI_ResultUInt32(context, trial.enabled);
    SCPI_ResultUInt32(context, trial.trial_id);
    SCPI_ResultUInt32(context, trial.config_seq);
    SCPI_ResultUInt32(context, trial.calibration_generation);
    SCPI_ResultUInt32(context, trial.topology_generation);
    SCPI_ResultUInt32(context, trial.topology_crc32);
    SCPI_ResultUInt32(context, trial.calibration_crc32);
    SCPI_ResultUInt32(context, trial.rearm_budget_ticks);
    SCPI_ResultUInt32(context, trial.abort_poll_count);
    SCPI_ResultUInt64(context, trial.expires_ticks);
    SCPI_ResultUInt32(context, trial.admission.model_epoch);
    SCPI_ResultUInt32(context, trial.admission.foundation_crc32);
    SCPI_ResultUInt32(context, trial.admission.deployment_crc32);
    SCPI_ResultUInt32(context, trial.admission.owner_instance);
    SCPI_ResultUInt32(context, trial.admission.node_id);
    SCPI_ResultUInt32(context, trial.admission.board_id);
    SCPI_ResultUInt32(context, trial.admission.gate_mask);
    SCPI_ResultUInt32(context, trial.admission.diagnostic_valid);
    SCPI_ResultUInt32(context, trial.admission.product_valid);
    SCPI_ResultUInt32(context, trial.admission.product_reject_mask);
    SCPI_ResultUInt32(context, trial.admission.capability.persona);
    SCPI_ResultUInt32(context, trial.admission.capability.clk_sys_hz);
    SCPI_ResultUInt32(context, trial.admission.capability.physical_bytes);
    SCPI_ResultUInt32(context, trial.admission.capability.resource_mask);
    SCPI_ResultUInt32(context, trial.admission.capability.dma_mask);
    SCPI_ResultUInt32(context, trial.diagnostic_flags);
    return SCPI_RES_OK;
}

static bool scpi_sma_cable_input_pin(uint32_t input_channel,
                                     uint32_t *input_pin)
{
    if (input_pin == NULL || input_channel == 0u ||
        input_channel > BOARD_SYNC_INPUT_PIN_COUNT) {
        return false;
    }
#if BOARD_SYNC_INPUT_BITS_REVERSED
    *input_pin = BOARD_SYNC_INPUT_BASE_PIN +
                 BOARD_SYNC_INPUT_PIN_COUNT - input_channel;
#else
    *input_pin = BOARD_SYNC_INPUT_BASE_PIN + input_channel - 1u;
#endif
    return true;
}

void scpi_calibration_result_u64(scpi_t *context, uint64_t value)
{
    SCPI_ResultUInt32(context, (uint32_t)value);
    SCPI_ResultUInt32(context, (uint32_t)(value >> 32u));
}

scpi_result_t scpi_calibration_loopback_start(scpi_t *context)
{
    uint32_t words = 128u;
    (void)SCPI_ParamUInt32(context, &words, FALSE);
    if (!calibration_manager_start_loopback(words)) {
        scpi_port_push_exec_error(context, "CAL_LOOPBACK_START_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, words);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_loopback_stop(scpi_t *context)
{
    calibration_manager_stop_loopback();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_loopback_q(scpi_t *context)
{
    calibration_manager_loopback_snapshot_t snapshot;
    if (!calibration_manager_get_loopback_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.raw.armed);
    SCPI_ResultUInt32(context, snapshot.raw.complete);
    SCPI_ResultUInt32(context, snapshot.raw.sample_hz);
    SCPI_ResultUInt32(context, snapshot.raw.sample_period_ns);
    SCPI_ResultUInt32(context, snapshot.raw.produced_words);
    SCPI_ResultUInt32(context, snapshot.raw.edge_mask);
    SCPI_ResultUInt32(context, snapshot.raw.flags);
    SCPI_ResultUInt32(context, snapshot.result.reject_reason);
    SCPI_ResultUInt32(context, snapshot.raw.epoch);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.raw.t1_clk_tx);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.raw.t2_clk_rx);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.raw.t3_data_tx);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.raw.t4_data_rx);
    SCPI_ResultUInt32(context, snapshot.result_valid);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.result.residence_ns);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.result.raw_path_sum_ns);
    SCPI_ResultInt32(context, (int32_t)snapshot.result.delay_estimate_ns);
    SCPI_ResultBool(context, snapshot.result.active_eligible ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_clk_coded_start(scpi_t *context)
{
    uint32_t codebook_id = 0u;
    uint32_t min_lag_sample = 0u;
    uint32_t max_lag_sample = 0u;
    uint32_t max_best_distance = 0u;
    uint32_t min_margin = 0u;
    if (SCPI_ParamUInt32(context, &codebook_id, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &min_lag_sample, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &max_lag_sample, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &max_best_distance, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &min_margin, TRUE) != TRUE ||
        !calibration_manager_request_clk_coded(
            codebook_id, min_lag_sample, max_lag_sample,
            max_best_distance, min_margin)) {
        scpi_port_push_exec_error(context, "CAL_CODED_START_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, codebook_id);
    SCPI_ResultUInt32(context, min_lag_sample);
    SCPI_ResultUInt32(context, max_lag_sample);
    SCPI_ResultUInt32(context, max_best_distance);
    SCPI_ResultUInt32(context, min_margin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sma_cable_phase_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t frequency_hz = 0u;
    uint32_t output_channel = 0u;
    uint32_t capture_word_count = SCPI_SMA_CABLE_CAPTURE_DEFAULT_WORDS;
    if (SCPI_ParamUInt32(context, &frequency_hz, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &output_channel, TRUE) != TRUE) {
        scpi_port_push_exec_error(context, "SMA_CABLE_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &capture_word_count, FALSE);
    if (output_channel == 0u ||
        output_channel > BOARD_SYNC_OUTPUT_PIN_COUNT ||
        capture_word_count < SMA_CABLE_DELAY_PIO_MIN_CAPTURE_WORDS ||
        capture_word_count > SCPI_SMA_CABLE_CAPTURE_MAX_WORDS) {
        scpi_port_push_exec_error(context, "SMA_CABLE_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }

    sync_io_sma_frequency_tx_status_t frequency_tx;
    sync_io_sma_frequency_tx_get_status(&frequency_tx);
    if (frequency_tx.running) {
        scpi_port_push_exec_error(context, "SMA_FREQUENCY_TX_ACTIVE");
        return SCPI_RES_ERR;
    }

    const sma_cable_delay_pio_config_t config = {
        .role = SMA_CABLE_DELAY_PIO_ROLE_SELF_LOOP,
        .timing = SMA_CABLE_DELAY_PIO_TIMING_FREE_RUNNING,
        .output_index = output_channel - 1u,
        .input_base_pin = BOARD_SYNC_INPUT_BASE_PIN,
        .appointment_marker_pin = BOARD_SYNC_INPUT_BASE_PIN,
        .reverse_input_bits = BOARD_SYNC_INPUT_BITS_REVERSED != 0,
    };
    sma_cable_delay_pio_status_t status = sma_cable_delay_pio_open(&config);
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    sma_cable_delay_pio_capture_t capture;
    status = sma_cable_delay_pio_capture_frequency(
        frequency_hz,
        s_scpi_sma_cable_capture,
        capture_word_count,
        SCPI_SMA_CABLE_CAPTURE_TIMEOUT_US,
        &capture);
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        sma_cable_delay_pio_close();
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    sma_cable_delay_phase_extract_t phases[SMA_CABLE_DELAY_CHANNEL_COUNT];
    bool any_phase_valid = false;
    for (uint32_t channel = 0u;
         channel < SMA_CABLE_DELAY_CHANNEL_COUNT;
         ++channel) {
        if (sma_cable_delay_extract_phase_from_capture(
                s_scpi_sma_cable_capture,
                capture.captured_word_count,
                channel,
                capture.period_samples,
                capture.sample_rate_hz,
                capture.reverse_input_bits,
                &phases[channel])) {
            any_phase_valid = true;
        }
    }
    sma_cable_delay_pio_close();
    if (!any_phase_valid) {
        scpi_port_push_exec_error(context, "SMA_CABLE_NO_EDGE");
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, capture.requested_frequency_hz);
    SCPI_ResultUInt32(context, capture.actual_frequency_hz);
    SCPI_ResultUInt32(context, capture.sample_rate_hz);
    SCPI_ResultUInt32(context, capture.period_samples);
    SCPI_ResultUInt32(context, capture.captured_word_count);
    for (uint32_t channel = 0u;
         channel < SMA_CABLE_DELAY_CHANNEL_COUNT;
         ++channel) {
        SCPI_ResultBool(context, phases[channel].valid ? TRUE : FALSE);
        SCPI_ResultInt32(context, phases[channel].phase_mdeg);
        SCPI_ResultUInt32(context, phases[channel].rising_edge_count);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sma_cable_source_start(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t frequency_hz = 0u;
    uint32_t output_channel = 0u;
    uint32_t appointment_mode = 0u;
    if (SCPI_ParamUInt32(context, &frequency_hz, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &output_channel, TRUE) != TRUE ||
        frequency_hz == 0u || output_channel == 0u ||
        output_channel > BOARD_SYNC_OUTPUT_PIN_COUNT) {
        scpi_port_push_exec_error(context, "SMA_CABLE_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &appointment_mode, FALSE);
    if (appointment_mode > 1u) {
        scpi_port_push_exec_error(context, "SMA_CABLE_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }

    sync_io_sma_frequency_tx_status_t frequency_tx;
    sync_io_sma_frequency_tx_get_status(&frequency_tx);
    if (frequency_tx.running) {
        scpi_port_push_exec_error(context, "SMA_FREQUENCY_TX_ACTIVE");
        return SCPI_RES_ERR;
    }

    const sma_cable_delay_pio_config_t config = {
        .role = SMA_CABLE_DELAY_PIO_ROLE_SOURCE,
        .timing = appointment_mode != 0u
            ? SMA_CABLE_DELAY_PIO_TIMING_MARK_APPOINTMENT
            : SMA_CABLE_DELAY_PIO_TIMING_FREE_RUNNING,
        .output_index = output_channel - 1u,
        .input_base_pin = BOARD_SYNC_INPUT_BASE_PIN,
        .appointment_marker_pin = BOARD_SYNC_INPUT_BASE_PIN,
        .reverse_input_bits = BOARD_SYNC_INPUT_BITS_REVERSED != 0,
    };
    sma_cable_delay_pio_status_t status = sma_cable_delay_pio_open(&config);
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    uint32_t actual_frequency_hz = 0u;
    status = sma_cable_delay_pio_source_start(frequency_hz,
                                               &actual_frequency_hz);
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        sma_cable_delay_pio_close();
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, frequency_hz);
    SCPI_ResultUInt32(context, actual_frequency_hz);
    SCPI_ResultUInt32(context, output_channel);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sma_cable_source_stop(scpi_t *context)
{
    sma_cable_delay_pio_source_stop();
    sma_cable_delay_pio_close();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_sma_cable_validator_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t frequency_hz = 0u;
    uint32_t capture_word_count = SCPI_SMA_CABLE_CAPTURE_DEFAULT_WORDS;
    uint32_t appointment_mode = 0u;
    if (SCPI_ParamUInt32(context, &frequency_hz, TRUE) != TRUE) {
        scpi_port_push_exec_error(context, "SMA_CABLE_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &capture_word_count, FALSE);
    (void)SCPI_ParamUInt32(context, &appointment_mode, FALSE);
    if (frequency_hz == 0u ||
        capture_word_count < SMA_CABLE_DELAY_PIO_MIN_CAPTURE_WORDS ||
        capture_word_count > SCPI_SMA_CABLE_CAPTURE_MAX_WORDS ||
        appointment_mode > 1u) {
        scpi_port_push_exec_error(context, "SMA_CABLE_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }

    const sma_cable_delay_pio_config_t config = {
        .role = SMA_CABLE_DELAY_PIO_ROLE_VALIDATOR,
        .timing = appointment_mode != 0u
            ? SMA_CABLE_DELAY_PIO_TIMING_MARK_APPOINTMENT
            : SMA_CABLE_DELAY_PIO_TIMING_FREE_RUNNING,
        .output_index = 0u,
        .input_base_pin = BOARD_SYNC_INPUT_BASE_PIN,
        .appointment_marker_pin =
            BOARD_SYNC_INPUT_BASE_PIN +
            (BOARD_SYNC_INPUT_BITS_REVERSED != 0
                ? SMA_CABLE_DELAY_CHANNEL_COUNT - 1u
                : 0u),
        .reverse_input_bits = BOARD_SYNC_INPUT_BITS_REVERSED != 0,
    };
    sma_cable_delay_pio_status_t status = sma_cable_delay_pio_open(&config);
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    sma_cable_delay_pio_capture_t capture;
    status = sma_cable_delay_pio_capture_frequency(
        frequency_hz,
        s_scpi_sma_cable_capture,
        capture_word_count,
        SCPI_SMA_CABLE_CAPTURE_TIMEOUT_US,
        &capture);
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        sma_cable_delay_pio_close();
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    sma_cable_delay_phase_extract_t phases[SMA_CABLE_DELAY_CHANNEL_COUNT];
    bool any_phase_valid = false;
    for (uint32_t channel = 0u;
         channel < SMA_CABLE_DELAY_CHANNEL_COUNT;
         ++channel) {
        if (sma_cable_delay_extract_phase_from_capture(
                s_scpi_sma_cable_capture,
                capture.captured_word_count,
                channel,
                capture.period_samples,
                capture.sample_rate_hz,
                capture.reverse_input_bits,
                &phases[channel])) {
            any_phase_valid = true;
        }
    }
    sma_cable_delay_pio_close();
    if (!any_phase_valid) {
        scpi_port_push_exec_error(context, "SMA_CABLE_NO_EDGE");
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, capture.requested_frequency_hz);
    SCPI_ResultUInt32(context, capture.actual_frequency_hz);
    SCPI_ResultUInt32(context, capture.sample_rate_hz);
    SCPI_ResultUInt32(context, capture.period_samples);
    SCPI_ResultUInt32(context, capture.captured_word_count);
    for (uint32_t channel = 0u;
         channel < SMA_CABLE_DELAY_CHANNEL_COUNT;
         ++channel) {
        SCPI_ResultBool(context, phases[channel].valid ? TRUE : FALSE);
        SCPI_ResultInt32(context, phases[channel].phase_mdeg);
        SCPI_ResultUInt32(context, phases[channel].rising_edge_count);
        SCPI_ResultUInt32(context, phases[channel].falling_edge_count);
        SCPI_ResultUInt32(context, phases[channel].observed_frequency_hz);
        SCPI_ResultUInt32(context, phases[channel].duty_cycle_ppm);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sma_cable_coarse_q(scpi_t *context)
{
    const int64_t relative_delay_ps[SMA_CABLE_DELAY_CHANNEL_COUNT] = {
        0, 0, 0, 0,
    };
    sma_cable_delay_coarse_result_t coarse;
    if (!sma_cable_delay_coarse_equal_cables(
            0u,
            false,
            0u,
            relative_delay_ps,
            &coarse)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultBool(context,
                    coarse.common_cable_delay_valid ? TRUE : FALSE);
    SCPI_ResultUInt32(context, coarse.common_cable_delay_ps);
    SCPI_ResultUInt32(context, coarse.velocity_factor_ppm);
    for (uint32_t channel = 0u;
         channel < SMA_CABLE_DELAY_CHANNEL_COUNT;
         ++channel) {
        SCPI_ResultInt32(
            context,
            (int32_t)coarse.channels[channel].relative_delay_ps);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sma_cable_rtt_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t output_channel = 0u;
    uint32_t input_channel = 0u;
    uint32_t capture_word_count = SCPI_SMA_CABLE_CAPTURE_DEFAULT_WORDS;
    uint32_t input_pin = 0u;
    if (SCPI_ParamUInt32(context, &output_channel, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &input_channel, TRUE) != TRUE) {
        scpi_port_push_exec_error(context, "SMA_CABLE_RTT_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &capture_word_count, FALSE);
    if (output_channel == 0u ||
        output_channel > BOARD_SYNC_OUTPUT_PIN_COUNT ||
        !scpi_sma_cable_input_pin(input_channel, &input_pin) ||
        capture_word_count < SMA_CABLE_DELAY_PIO_MIN_CAPTURE_WORDS ||
        capture_word_count > SCPI_SMA_CABLE_CAPTURE_MAX_WORDS) {
        scpi_port_push_exec_error(context, "SMA_CABLE_RTT_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }

    const sma_cable_delay_pio_config_t config = {
        .role = SMA_CABLE_DELAY_PIO_ROLE_RTT_INITIATOR,
        .timing = SMA_CABLE_DELAY_PIO_TIMING_FREE_RUNNING,
        .output_index = output_channel - 1u,
        .input_base_pin = input_pin,
        .appointment_marker_pin = input_pin,
        .reverse_input_bits = false,
    };
    sma_cable_delay_pio_status_t status = sma_cable_delay_pio_open(&config);
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    sma_cable_delay_pio_rtt_capture_t capture;
    status = sma_cable_delay_pio_rtt_initiate(
        s_scpi_sma_cable_capture,
        capture_word_count,
        SCPI_SMA_CABLE_CAPTURE_TIMEOUT_US,
        &capture);
    sma_cable_delay_pio_close();
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    sma_cable_delay_symmetric_rtt_t result;
    if (!sma_cable_delay_resolve_symmetric_rtt(
            capture.raw_round_trip_cycles,
            capture.responder_turnaround_cycles,
            capture.sample_period_ps,
            &result)) {
        scpi_port_push_exec_error(context, "SMA_CABLE_RTT_INVALID_RESULT");
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, output_channel);
    SCPI_ResultUInt32(context, input_channel);
    SCPI_ResultUInt32(context, capture.sample_rate_hz);
    SCPI_ResultUInt32(context, capture.sample_period_ps);
    SCPI_ResultUInt32(context, capture.response_sample_index);
    SCPI_ResultUInt32(context, capture.raw_round_trip_cycles);
    SCPI_ResultUInt32(context, capture.responder_turnaround_cycles);
    scpi_calibration_result_u64(context, result.path_sum_ps);
    scpi_calibration_result_u64(context, result.mean_leg_delay_ps);
    SCPI_ResultBool(context, result.valid ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sma_cable_rtt_responder_q(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t input_channel = 0u;
    uint32_t output_channel = 0u;
    uint32_t input_pin = 0u;
    if (SCPI_ParamUInt32(context, &input_channel, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &output_channel, TRUE) != TRUE ||
        output_channel == 0u ||
        output_channel > BOARD_SYNC_OUTPUT_PIN_COUNT ||
        !scpi_sma_cable_input_pin(input_channel, &input_pin)) {
        scpi_port_push_exec_error(context, "SMA_CABLE_RTT_BAD_ARGUMENT");
        return SCPI_RES_ERR;
    }

    const sma_cable_delay_pio_config_t config = {
        .role = SMA_CABLE_DELAY_PIO_ROLE_RTT_RESPONDER,
        .timing = SMA_CABLE_DELAY_PIO_TIMING_FREE_RUNNING,
        .output_index = output_channel - 1u,
        .input_base_pin = input_pin,
        .appointment_marker_pin = input_pin,
        .reverse_input_bits = false,
    };
    sma_cable_delay_pio_status_t status = sma_cable_delay_pio_open(&config);
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    uint32_t turnaround_cycles = 0u;
    status = sma_cable_delay_pio_rtt_respond(
        SCPI_SMA_CABLE_RTT_RESPONDER_TIMEOUT_US,
        &turnaround_cycles);
    sma_cable_delay_pio_close();
    if (status != SMA_CABLE_DELAY_PIO_OK) {
        scpi_port_push_exec_error(context,
                                  sma_cable_delay_pio_status_string(status));
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, input_channel);
    SCPI_ResultUInt32(context, output_channel);
    SCPI_ResultUInt32(context, turnaround_cycles);
    SCPI_ResultUInt32(context, SMA_CABLE_DELAY_PIO_MARK_WIDTH_NS);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_marker_arm(scpi_t *context)
{
    uint32_t codebook_id = 0u;
    uint32_t train_epoch = 0u;
    uint32_t train_sequence = 0u;
    uint32_t marker_id = 0u;
    uint32_t calibration_generation = 0u;
    uint32_t link_base_delay_ns = 0u;
    uint32_t origin_node = UINT32_MAX;
    uint32_t diagnostic_fault_flags = 0u;
    int32_t offset_sample_count = 0;
    if (SCPI_ParamUInt32(context, &codebook_id, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &train_epoch, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &train_sequence, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &marker_id, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &calibration_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link_base_delay_ns, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &offset_sample_count, TRUE) != TRUE) {
        scpi_port_push_exec_error(context, "CAL_MARKER_ARM_REJECTED");
        return SCPI_RES_ERR;
    }
    const bool origin_node_provided =
        SCPI_ParamUInt32(context, &origin_node, FALSE) == TRUE;
    const bool diagnostic_fault_provided =
        SCPI_ParamUInt32(context, &diagnostic_fault_flags, FALSE) == TRUE;
    if (
        (diagnostic_fault_provided && !origin_node_provided) ||
        !calibration_manager_request_marker_training(
            codebook_id, train_epoch, train_sequence, marker_id,
            calibration_generation, link_base_delay_ns,
            offset_sample_count, origin_node, diagnostic_fault_flags)) {
        scpi_port_push_exec_error(context, "CAL_MARKER_ARM_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, codebook_id);
    SCPI_ResultUInt32(context, train_epoch);
    SCPI_ResultUInt32(context, train_sequence);
    SCPI_ResultUInt32(context, marker_id);
    SCPI_ResultUInt32(context, calibration_generation);
    SCPI_ResultUInt32(context, link_base_delay_ns);
    SCPI_ResultInt32(context, offset_sample_count);
    if (origin_node_provided) {
        SCPI_ResultUInt32(context, origin_node);
    }
    if (diagnostic_fault_provided) {
        SCPI_ResultUInt32(context, diagnostic_fault_flags);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_marker_inject(scpi_t *context)
{
    if (!calibration_manager_inject_marker_training()) {
        scpi_port_push_exec_error(context, "CAL_MARKER_INJECT_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_marker_stop(scpi_t *context)
{
    calibration_manager_stop_marker_training();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_marker_q(scpi_t *context)
{
    calibration_training_marker_snapshot_t snapshot;
    if (!calibration_manager_get_marker_training_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "MARKERTRN");
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.reject_reason);
    SCPI_ResultUInt32(context, snapshot.flags);
    SCPI_ResultUInt32(context, snapshot.role);
    scpi_calibration_result_u64(context, snapshot.board_unique_id);
    scpi_calibration_result_u64(context, snapshot.build_id);
    SCPI_ResultUInt32(context, snapshot.local_node);
    SCPI_ResultUInt32(context, snapshot.reference_node);
    SCPI_ResultUInt32(context, snapshot.predecessor_node);
    SCPI_ResultUInt32(context, snapshot.successor_node);
    SCPI_ResultUInt32(context, snapshot.train_epoch);
    SCPI_ResultUInt32(context, snapshot.train_sequence);
    SCPI_ResultUInt32(context, snapshot.marker_id);
    SCPI_ResultUInt32(context, snapshot.marker_codebook_id);
    SCPI_ResultUInt32(context, snapshot.marker_crc32);
    SCPI_ResultUInt32(context, snapshot.observed_crc32);
    SCPI_ResultUInt32(context, snapshot.polarity);
    SCPI_ResultUInt32(context, snapshot.marker_flags);
    SCPI_ResultUInt32(context, snapshot.correlation_reject_reason);
    SCPI_ResultUInt32(context, snapshot.best_lag_sample);
    SCPI_ResultUInt32(context, snapshot.best_distance);
    SCPI_ResultUInt32(context, snapshot.calibration_generation);
    SCPI_ResultUInt32(context, snapshot.topology_generation);
    SCPI_ResultUInt32(context, snapshot.topology_crc32);
    SCPI_ResultUInt32(context, snapshot.profile_crc32);
    SCPI_ResultUInt32(context, snapshot.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.tick_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.link_base_delay_ns);
    SCPI_ResultInt32(context, snapshot.offset_sample_count);
    SCPI_ResultUInt32(context, snapshot.diagnostic_fault_flags);
    scpi_calibration_result_u64(context, snapshot.marker_capture_tick);
    scpi_calibration_result_u64(context, snapshot.marker_forward_tick);
    scpi_calibration_result_u64(context, snapshot.marker_return_tick);
    scpi_calibration_result_u64(context, snapshot.forward_residence_ticks);
    scpi_calibration_result_u64(context, snapshot.loop_rtt_ticks);
    SCPI_ResultUInt32(context, snapshot.dma_capture_count);
    SCPI_ResultUInt32(context, snapshot.dma_overrun_count);
    SCPI_ResultUInt32(context, snapshot.pio_stall_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_marker_capture_save(scpi_t *context)
{
    uint32_t job_id = 0u;
    char path[96];
    if (!calibration_manager_save_marker_capture(
            &job_id, path, sizeof(path))) {
        scpi_port_push_exec_error(context, "CAL_MARKER_CAPTURE_SAVE_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, job_id);
    SCPI_ResultText(context, path);
    return SCPI_RES_OK;
}

