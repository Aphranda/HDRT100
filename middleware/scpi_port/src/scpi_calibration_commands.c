#include "scpi_calibration_commands.h"

#include "calibration_manager.h"
#include "board_config.h"
#include "distributed_config.h"
#include "sma_cable_delay.h"
#include "sma_cable_delay_pio.h"
#include "sync_io.h"

#define SCPI_SMA_CABLE_CAPTURE_MAX_WORDS 512u
#define SCPI_SMA_CABLE_CAPTURE_DEFAULT_WORDS 256u
#define SCPI_SMA_CABLE_CAPTURE_TIMEOUT_US 100000u
#define SCPI_SMA_CABLE_RTT_RESPONDER_TIMEOUT_US 2000000u

static uint32_t s_scpi_sma_cable_capture[SCPI_SMA_CABLE_CAPTURE_MAX_WORDS];

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

static void scpi_calibration_result_u64(scpi_t *context, uint64_t value)
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

scpi_result_t scpi_calibration_data_arm(scpi_t *context)
{
    uint32_t source_node = 0u, destination_node = 0u, codebook_id = 0u;
    uint32_t train_epoch = 0u, train_sequence = 0u;
    uint32_t calibration_generation = 0u, marker_to_data_samples = 0u;
    uint32_t link_base_delay_ns = 0u, guard_sample_count = 0u;
    uint32_t max_best_distance = 0u, min_margin = 0u;
    uint32_t diagnostic_fault_flags = 0u;
    uint32_t diagnostic_wire_epoch = 0u;
    uint32_t diagnostic_header_crc8_xor = 0u;
    int32_t marker_offset_sample_count = 0;
    int32_t configured_data_offset_sample_count = 0;
    int32_t search_start_offset_sample = 0;
    int32_t search_end_offset_sample = 0;
    if (SCPI_ParamUInt32(context, &source_node, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &destination_node, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &codebook_id, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &train_epoch, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &train_sequence, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &calibration_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &marker_to_data_samples, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link_base_delay_ns, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &marker_offset_sample_count, TRUE) != TRUE ||
        SCPI_ParamInt32(
            context, &configured_data_offset_sample_count, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &search_start_offset_sample, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &search_end_offset_sample, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &guard_sample_count, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &max_best_distance, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &min_margin, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &diagnostic_fault_flags, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &diagnostic_wire_epoch, TRUE) != TRUE ||
        SCPI_ParamUInt32(
            context, &diagnostic_header_crc8_xor, TRUE) != TRUE ||
        !calibration_manager_request_data_training(
            source_node, destination_node, codebook_id, train_epoch,
            train_sequence, calibration_generation, marker_to_data_samples,
            link_base_delay_ns, marker_offset_sample_count,
            configured_data_offset_sample_count,
            search_start_offset_sample,
            search_end_offset_sample, guard_sample_count,
            max_best_distance, min_margin, diagnostic_fault_flags,
            diagnostic_wire_epoch, diagnostic_header_crc8_xor)) {
        scpi_port_push_exec_error(context, "CAL_DATA_ARM_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, source_node);
    SCPI_ResultUInt32(context, destination_node);
    SCPI_ResultUInt32(context, train_epoch);
    SCPI_ResultUInt32(context, calibration_generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_data_inject(scpi_t *context)
{
    if (!calibration_manager_inject_data_training()) {
        scpi_port_push_exec_error(context, "CAL_DATA_INJECT_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_data_stop(scpi_t *context)
{
    calibration_manager_stop_data_training();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_data_q(scpi_t *context)
{
    calibration_training_data_snapshot_t snapshot;
    if (!calibration_manager_get_data_training_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "DATATRN");
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.reject_reason);
    SCPI_ResultUInt32(context, snapshot.flags);
    scpi_calibration_result_u64(context, snapshot.board_unique_id);
    scpi_calibration_result_u64(context, snapshot.build_id);
    SCPI_ResultUInt32(context, snapshot.source_node);
    SCPI_ResultUInt32(context, snapshot.destination_node);
    SCPI_ResultUInt32(context, snapshot.train_epoch);
    SCPI_ResultUInt32(context, snapshot.train_sequence);
    SCPI_ResultUInt32(context, snapshot.data_codebook_id);
    SCPI_ResultUInt32(context, snapshot.data_crc32);
    SCPI_ResultUInt32(context, snapshot.observed_crc32);
    SCPI_ResultUInt32(context, snapshot.observed_header_fields_valid);
    SCPI_ResultUInt32(context, snapshot.observed_header);
    SCPI_ResultUInt32(context, snapshot.observed_header_inverse);
    SCPI_ResultUInt32(context, snapshot.observed_header_crc8);
    SCPI_ResultUInt32(context, snapshot.calibration_generation);
    SCPI_ResultUInt32(context, snapshot.topology_generation);
    SCPI_ResultUInt32(context, snapshot.topology_crc32);
    SCPI_ResultUInt32(context, snapshot.profile_crc32);
    SCPI_ResultUInt32(context, snapshot.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.sample_period_ns);
    SCPI_ResultUInt32(context, snapshot.marker_to_data_samples);
    SCPI_ResultUInt32(context, snapshot.link_base_delay_ns);
    SCPI_ResultInt32(context, snapshot.marker_offset_sample_count);
    SCPI_ResultInt32(
        context, snapshot.configured_data_offset_sample_count);
    SCPI_ResultInt32(context, snapshot.search_start_offset_sample);
    SCPI_ResultInt32(context, snapshot.search_end_offset_sample);
    SCPI_ResultUInt32(context, snapshot.guard_sample_count);
    SCPI_ResultUInt32(context, snapshot.max_best_distance);
    SCPI_ResultUInt32(context, snapshot.min_margin);
    SCPI_ResultUInt32(context, snapshot.diagnostic_fault_flags);
    SCPI_ResultUInt32(context, snapshot.diagnostic_wire_epoch);
    SCPI_ResultUInt32(context, snapshot.diagnostic_header_crc8_xor);
    SCPI_ResultUInt32(context, snapshot.polarity);
    SCPI_ResultUInt32(context, snapshot.correlation_reject_reason);
    SCPI_ResultUInt32(context, snapshot.best_lag_sample);
    SCPI_ResultUInt32(context, snapshot.best_distance);
    SCPI_ResultUInt32(context, snapshot.second_lag_sample);
    SCPI_ResultUInt32(context, snapshot.second_distance);
    SCPI_ResultUInt32(context, snapshot.margin);
    SCPI_ResultInt32(context, snapshot.resolved_offset_sample_count);
    SCPI_ResultInt32(context, snapshot.resolved_offset_ns);
    SCPI_ResultInt32(context, snapshot.training_window_start_ns);
    SCPI_ResultInt32(context, snapshot.training_window_end_ns);
    SCPI_ResultInt32(context, snapshot.marker_data_skew_ns);
    SCPI_ResultUInt32(context, snapshot.captured_sample_count);
    SCPI_ResultUInt32(context, snapshot.expected_sample_count);
    SCPI_ResultUInt32(context, snapshot.dma_overrun_count);
    SCPI_ResultUInt32(context, snapshot.pio_stall_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    scpi_calibration_result_u64(context, snapshot.marker_capture_tick);
    scpi_calibration_result_u64(context, snapshot.data_capture_tick);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_data_capture_save(scpi_t *context)
{
    uint32_t job_id = 0u;
    char path[96];
    if (!calibration_manager_save_data_capture(
            &job_id, path, sizeof(path))) {
        scpi_port_push_exec_error(context, "CAL_DATA_CAPTURE_SAVE_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, job_id);
    SCPI_ResultText(context, path);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sck_arm(scpi_t *context)
{
    uint32_t source_node = 0u, destination_node = 0u, codebook_id = 0u;
    uint32_t train_epoch = 0u, train_sequence = 0u;
    uint32_t calibration_generation = 0u;
    uint32_t sck_launch_guard_sample_count = 0u;
    uint32_t link_base_delay_ns = 0u;
    uint32_t guard_sample_count = 0u, max_best_distance = 0u;
    uint32_t min_margin = 0u;
    int32_t configured_sck_offset_sample_count = 0;
    int32_t search_start_offset_sample = 0;
    int32_t search_end_offset_sample = 0;
    if (SCPI_ParamUInt32(context, &source_node, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &destination_node, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &codebook_id, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &train_epoch, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &train_sequence, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &calibration_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(
            context, &sck_launch_guard_sample_count, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link_base_delay_ns, TRUE) != TRUE ||
        SCPI_ParamInt32(
            context, &configured_sck_offset_sample_count, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &search_start_offset_sample, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &search_end_offset_sample, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &guard_sample_count, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &max_best_distance, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &min_margin, TRUE) != TRUE ||
        !calibration_manager_request_sck_training(
            source_node, destination_node, codebook_id, train_epoch,
            train_sequence, calibration_generation,
            sck_launch_guard_sample_count,
            link_base_delay_ns,
            configured_sck_offset_sample_count,
            search_start_offset_sample, search_end_offset_sample,
            guard_sample_count, max_best_distance, min_margin)) {
        scpi_port_push_exec_error(context, "CAL_SCK_ARM_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, source_node);
    SCPI_ResultUInt32(context, destination_node);
    SCPI_ResultUInt32(context, train_epoch);
    SCPI_ResultUInt32(context, calibration_generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sck_inject(scpi_t *context)
{
    if (!calibration_manager_inject_sck_training()) {
        scpi_port_push_exec_error(context, "CAL_SCK_INJECT_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_sck_stop(scpi_t *context)
{
    calibration_manager_stop_sck_training();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_sck_q(scpi_t *context)
{
    calibration_training_sck_snapshot_t snapshot;
    if (!calibration_manager_get_sck_training_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "SCKTRN");
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.reject_reason);
    SCPI_ResultUInt32(context, snapshot.flags);
    scpi_calibration_result_u64(context, snapshot.board_unique_id);
    scpi_calibration_result_u64(context, snapshot.build_id);
    SCPI_ResultUInt32(context, snapshot.source_node);
    SCPI_ResultUInt32(context, snapshot.destination_node);
    SCPI_ResultUInt32(context, snapshot.train_epoch);
    SCPI_ResultUInt32(context, snapshot.train_sequence);
    SCPI_ResultUInt32(context, snapshot.sck_codebook_id);
    SCPI_ResultUInt32(context, snapshot.sck_crc32);
    SCPI_ResultUInt32(context, snapshot.observed_crc32);
    SCPI_ResultUInt32(context, snapshot.calibration_generation);
    SCPI_ResultUInt32(context, snapshot.topology_generation);
    SCPI_ResultUInt32(context, snapshot.topology_crc32);
    SCPI_ResultUInt32(context, snapshot.profile_crc32);
    SCPI_ResultUInt32(context, snapshot.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.sample_period_ns);
    SCPI_ResultUInt32(context, snapshot.sck_launch_guard_sample_count);
    SCPI_ResultUInt32(context, snapshot.link_base_delay_ns);
    SCPI_ResultInt32(context, snapshot.configured_sck_offset_sample_count);
    SCPI_ResultInt32(context, snapshot.search_start_offset_sample);
    SCPI_ResultInt32(context, snapshot.search_end_offset_sample);
    SCPI_ResultUInt32(context, snapshot.guard_sample_count);
    SCPI_ResultUInt32(context, snapshot.polarity);
    SCPI_ResultUInt32(context, snapshot.correlation_reject_reason);
    SCPI_ResultUInt32(context, snapshot.best_lag_sample);
    SCPI_ResultUInt32(context, snapshot.best_distance);
    SCPI_ResultUInt32(context, snapshot.second_lag_sample);
    SCPI_ResultUInt32(context, snapshot.second_distance);
    SCPI_ResultUInt32(context, snapshot.margin);
    SCPI_ResultInt32(context, snapshot.resolved_offset_sample_count);
    SCPI_ResultInt32(context, snapshot.resolved_offset_ns);
    SCPI_ResultInt32(context, snapshot.training_window_start_ns);
    SCPI_ResultInt32(context, snapshot.training_window_end_ns);
    SCPI_ResultUInt32(context, snapshot.captured_sample_count);
    SCPI_ResultUInt32(context, snapshot.expected_sample_count);
    SCPI_ResultUInt32(context, snapshot.dma_overrun_count);
    SCPI_ResultUInt32(context, snapshot.pio_stall_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    scpi_calibration_result_u64(context, snapshot.sck_capture_origin_tick);
    scpi_calibration_result_u64(context, snapshot.sck_code_capture_tick);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_sck_capture_save(scpi_t *context)
{
    uint32_t job_id = 0u;
    char path[96];
    if (!calibration_manager_save_sck_capture(
            &job_id, path, sizeof(path))) {
        scpi_port_push_exec_error(context, "CAL_SCK_CAPTURE_SAVE_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, job_id);
    SCPI_ResultText(context, path);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_ring_capture_save(scpi_t *context)
{
    uint32_t calibration_generation = 0u;
    uint32_t capture_epoch = 0u;
    uint32_t job_id = 0u;
    char path[96];
    if (SCPI_ParamUInt32(
            context, &calibration_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &capture_epoch, TRUE) != TRUE ||
        !calibration_manager_save_ring_capture(
            calibration_generation, capture_epoch,
            &job_id, path, sizeof(path))) {
        scpi_port_push_exec_error(context, "CAL_RING_CAPTURE_SAVE_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, job_id);
    SCPI_ResultText(context, path);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_ring_capture_latch(scpi_t *context)
{
    uint32_t calibration_generation = 0u;
    uint32_t capture_epoch = 0u;
    if (SCPI_ParamUInt32(
            context, &calibration_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &capture_epoch, TRUE) != TRUE ||
        !calibration_manager_request_ring_capture(
            calibration_generation, capture_epoch)) {
        scpi_port_push_exec_error(context, "CAL_RING_CAPTURE_LATCH_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, calibration_generation);
    SCPI_ResultUInt32(context, capture_epoch);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_ring_capture_q(scpi_t *context)
{
    calibration_ring_capture_snapshot_t snapshot;
    calibration_ring_capture_debug_t debug;
    if (!calibration_manager_get_ring_capture_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    calibration_manager_get_ring_capture_debug(&debug);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.sequence);
    SCPI_ResultUInt32(context, snapshot.calibration_generation);
    SCPI_ResultUInt32(context, snapshot.capture_epoch);
    SCPI_ResultUInt32(context, snapshot.node);
    SCPI_ResultUInt32(context, snapshot.node_count);
    SCPI_ResultUInt32(context, snapshot.physical.rx_byte_count);
    SCPI_ResultUInt32(context, snapshot.physical.tx_byte_count);
    SCPI_ResultUInt32(context, snapshot.physical.rx_produced_bytes);
    SCPI_ResultUInt32(context, snapshot.physical.tx_produced_bytes);
    SCPI_ResultUInt32(context, debug.core1_service_count);
    SCPI_ResultUInt32(context, debug.intent_read_fail_count);
    SCPI_ResultUInt32(context, debug.last_seen_sequence);
    SCPI_ResultUInt32(context, debug.copy_attempt_count);
    SCPI_ResultUInt32(context, debug.copy_fail_count);
    SCPI_ResultUInt32(context, debug.consumed_sequence);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_training_stage_begin(scpi_t *context)
{
    uint32_t node_count = 0u;
    uint32_t evidence_flags = 0u;
    uint32_t calibration_generation = 0u;
    uint32_t topology_generation = 0u;
    uint32_t topology_crc32 = 0u;
    uint32_t profile_crc32 = 0u;
    uint32_t schedule_crc32 = 0u;
    if (SCPI_ParamUInt32(context, &node_count, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &evidence_flags, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &calibration_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &topology_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &topology_crc32, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &profile_crc32, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &schedule_crc32, TRUE) != TRUE ||
        !calibration_manager_begin_training_stage(
            node_count, evidence_flags, calibration_generation,
            topology_generation,
            topology_crc32, profile_crc32, schedule_crc32)) {
        scpi_port_push_exec_error(context, "CAL_TRAIN_STAGE_BEGIN_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, node_count);
    SCPI_ResultUInt32(context, evidence_flags);
    SCPI_ResultUInt32(context, calibration_generation);
    SCPI_ResultUInt32(context, topology_generation);
    SCPI_ResultUInt32(context, topology_crc32);
    SCPI_ResultUInt32(context, profile_crc32);
    SCPI_ResultUInt32(context, schedule_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_training_stage_link(scpi_t *context)
{
    uint32_t values[18] = {0u};
    int32_t marker_offset_sample_count = 0;
    int32_t sck_offset_sample_count = 0;
    int32_t data_offset_sample_count = 0;
    uint32_t sample_period_ns = 0u;
    uint32_t link_base_delay_ns = 0u;
    uint32_t marker_phase_delay_cycles = 0u;
    uint32_t sck_phase_delay_cycles = 0u;
    uint32_t data_phase_delay_cycles = 0u;
    for (uint32_t i = 0u; i < 18u; i++) {
        if (SCPI_ParamUInt32(context, &values[i], TRUE) != TRUE) {
            return SCPI_RES_ERR;
        }
    }
    if (SCPI_ParamInt32(context, &marker_offset_sample_count, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &sck_offset_sample_count, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &data_offset_sample_count, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &sample_period_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link_base_delay_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &marker_phase_delay_cycles, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &sck_phase_delay_cycles, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &data_phase_delay_cycles, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }
    if (!calibration_manager_stage_training_link(
            values[0], values[1], values[2], values[3], values[4],
            values[5], values[6], values[7], values[8], values[9],
            values[10], values[11], values[12], values[13], values[14],
            values[15], values[16], values[17],
            marker_offset_sample_count, sck_offset_sample_count,
            data_offset_sample_count, sample_period_ns,
            link_base_delay_ns, marker_phase_delay_cycles,
            sck_phase_delay_cycles, data_phase_delay_cycles)) {
        scpi_port_push_exec_error(context, "CAL_TRAIN_STAGE_LINK_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, values[0]);
    SCPI_ResultUInt32(context, values[12]);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_training_stage_q(scpi_t *context)
{
    tdma_ring_calibration_stage_t stage;
    bool complete = false;
    if (!calibration_manager_get_training_stage(&stage, &complete)) {
        SCPI_ResultText(context, "EMPTY");
        return SCPI_RES_OK;
    }
    uint32_t valid_link_bitmap = 0u;
    for (uint32_t link = 0u; link < stage.node_count; link++) {
        if (stage.links[link].valid != 0u) {
            valid_link_bitmap |= 1u << link;
        }
    }
    SCPI_ResultText(context, "TRN03STG");
    SCPI_ResultUInt32(context, stage.enabled);
    SCPI_ResultUInt32(context, stage.node_count);
    SCPI_ResultUInt32(context, stage.evidence_flags);
    SCPI_ResultBool(context, complete ? TRUE : FALSE);
    SCPI_ResultUInt32(context, stage.calibration_generation);
    SCPI_ResultUInt32(context, stage.topology_generation);
    SCPI_ResultUInt32(context, stage.topology_crc32);
    SCPI_ResultUInt32(context, stage.profile_crc32);
    SCPI_ResultUInt32(context, stage.schedule_crc32);
    SCPI_ResultUInt32(context, valid_link_bitmap);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_training_stage_link_q(scpi_t *context)
{
    uint32_t link_index = 0u;
    tdma_ring_calibration_stage_t stage;
    bool complete = false;
    if (SCPI_ParamUInt32(context, &link_index, TRUE) != TRUE ||
        !calibration_manager_get_training_stage(&stage, &complete) ||
        link_index >= stage.node_count) {
        scpi_port_push_exec_error(context, "CAL_TRAIN_STAGE_LINK_QUERY");
        return SCPI_RES_ERR;
    }
    (void)complete;
    const tdma_ring_calibration_link_t *link = &stage.links[link_index];
    SCPI_ResultText(context, "TRN03LNK");
    SCPI_ResultUInt32(context, link->valid);
    SCPI_ResultUInt32(context, link->link_index);
    SCPI_ResultUInt32(context, link->marker_source_node);
    SCPI_ResultUInt32(context, link->marker_destination_node);
    SCPI_ResultUInt32(context, link->data_source_node);
    SCPI_ResultUInt32(context, link->data_destination_node);
    SCPI_ResultUInt32(context, link->evidence_flags);
    SCPI_ResultUInt32(context, link->calibration_generation);
    SCPI_ResultUInt32(context, link->topology_generation);
    SCPI_ResultUInt32(context, link->topology_crc32);
    SCPI_ResultUInt32(context, link->profile_crc32);
    SCPI_ResultUInt32(context, link->schedule_crc32);
    SCPI_ResultUInt32(context, link->pio_persona);
    SCPI_ResultUInt32(context, link->clkdiv_q16);
    SCPI_ResultUInt32(context, link->clk_sys_hz);
    SCPI_ResultUInt32(context, link->instruction_period_ns);
    SCPI_ResultUInt32(context, link->bit_cycles);
    SCPI_ResultUInt32(context, link->marker_to_data_cycles);
    SCPI_ResultUInt32(context, link->forward_residence_cycles);
    SCPI_ResultUInt32(context, link->rx_arm_lead_cycles);
    SCPI_ResultUInt32(context, link->codeword_cycles);
    SCPI_ResultUInt32(context, link->guard_cycles);
    SCPI_ResultUInt32(context, link->link_budget_cycles);
    SCPI_ResultUInt32(context, link->loop_delay_cycles);
    SCPI_ResultInt32(context, link->marker_offset_sample_count);
    SCPI_ResultInt32(context, link->sck_offset_sample_count);
    SCPI_ResultInt32(context, link->data_offset_sample_count);
    SCPI_ResultUInt32(context, link->sample_period_ns);
    SCPI_ResultUInt32(context, link->link_base_delay_ns);
    SCPI_ResultUInt32(context, link->marker_phase_delay_cycles);
    SCPI_ResultUInt32(context, link->sck_phase_delay_cycles);
    SCPI_ResultUInt32(context, link->data_phase_delay_cycles);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_training_stage_clear(scpi_t *context)
{
    if (!calibration_manager_clear_training_stage()) {
        scpi_port_push_exec_error(context, "CAL_TRAIN_STAGE_CLEAR_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_topology_probe(scpi_t *context)
{
    uint32_t enabled = 0u;
    uint32_t phase_delay_cycles = 0u;
    if (SCPI_ParamUInt32(context, &enabled, TRUE) != TRUE || enabled > 1u) {
        return SCPI_RES_ERR;
    }
    if (enabled != 0u &&
        SCPI_ParamUInt32(context, &phase_delay_cycles, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }
    if (!calibration_manager_set_topology_probe_mode(
            enabled != 0u, phase_delay_cycles)) {
        scpi_port_push_exec_error(context, "CAL_TOPOLOGY_PROBE_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled);
    SCPI_ResultUInt32(context, phase_delay_cycles);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_path_candidate_begin(scpi_t *context)
{
    calibration_path_import_header_t header = {0};
    if (SCPI_ParamUInt32(context, &header.link_count, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.topology_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.topology_crc32, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.bias_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.profile_crc32, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.schedule_crc32, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.calibration_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.freshness_us, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.evidence_age_us, TRUE) != TRUE ||
        SCPI_ParamUInt64(context, &header.ring_round_trip_ns, TRUE) != TRUE ||
        SCPI_ParamUInt64(context,
                        &header.forwarding_residence_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.max_residual_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.max_jitter_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.max_asymmetry_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &header.expected_table_crc32, TRUE) != TRUE ||
        !calibration_manager_begin_path_import(&header)) {
        scpi_port_push_exec_error(context, "CAL_PATH_BEGIN_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, header.link_count);
    SCPI_ResultUInt32(context, header.calibration_generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_path_candidate_link(scpi_t *context)
{
    uint32_t link_index = 0u;
    calibration_path_link_evidence_t link = {0};
    uint32_t reference_accepted = 0u;
    uint32_t active_eligible = 0u;
    if (SCPI_ParamUInt32(context, &link_index, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.source_node, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.destination_node, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.profile_crc32, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.topology_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.bias_generation, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.sample_count, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.accepted_count, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.jitter_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.asymmetry_ns, TRUE) != TRUE ||
        SCPI_ParamUInt64(context, &link.measurement.residence_ns, TRUE) != TRUE ||
        SCPI_ParamUInt64(context, &link.measurement.raw_path_sum_ns, TRUE) != TRUE ||
        SCPI_ParamInt64(context,
                       &link.measurement.corrected_path_sum_ns, TRUE) != TRUE ||
        SCPI_ParamInt64(context,
                       &link.measurement.delay_estimate_ns, TRUE) != TRUE ||
        SCPI_ParamUInt64(context,
                        &link.measurement.clock_rate_error_bound_ns,
                        TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &link.measurement.reject_reason, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &reference_accepted, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &active_eligible, TRUE) != TRUE ||
        reference_accepted > 1u || active_eligible > 1u) {
        scpi_port_push_exec_error(context, "CAL_PATH_LINK_ARGUMENT");
        return SCPI_RES_ERR;
    }
    link.measurement.reference_accepted = reference_accepted != 0u;
    link.measurement.active_eligible = active_eligible != 0u;
    if (!calibration_manager_import_path_link(link_index, &link)) {
        scpi_port_push_exec_error(context, "CAL_PATH_LINK_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, link_index);
    SCPI_ResultUInt32(context, link.source_node);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_path_candidate_finalize(scpi_t *context)
{
    if (!calibration_manager_finalize_path_import()) {
        scpi_port_push_exec_error(context, "CAL_PATH_FINALIZE_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_path_candidate_clear(scpi_t *context)
{
    if (!calibration_manager_clear_path_import() ||
        !calibration_manager_clear_path_candidate()) {
        scpi_port_push_exec_error(context, "CAL_PATH_CLEAR_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_calibration_path_snapshot_result(
    scpi_t *context, const char *tag,
    const calibration_path_snapshot_t *snapshot, bool valid)
{
    SCPI_ResultText(context, tag);
    SCPI_ResultBool(context, valid ? TRUE : FALSE);
    SCPI_ResultUInt32(context, valid ? snapshot->link_count : 0u);
    SCPI_ResultUInt32(context, valid ? snapshot->topology_generation : 0u);
    SCPI_ResultUInt32(context, valid ? snapshot->topology_crc32 : 0u);
    SCPI_ResultUInt32(context, valid ? snapshot->bias_generation : 0u);
    SCPI_ResultUInt32(context, valid ? snapshot->profile_crc32 : 0u);
    SCPI_ResultUInt32(context, valid ? snapshot->schedule_crc32 : 0u);
    SCPI_ResultUInt32(context, valid ? snapshot->calibration_generation : 0u);
    SCPI_ResultUInt32(context, valid ? snapshot->freshness_us : 0u);
    SCPI_ResultUInt64(context, valid ? snapshot->cumulative_delay_ns : 0u);
    SCPI_ResultUInt64(context,
                      valid ? snapshot->forwarding_residence_ns : 0u);
    SCPI_ResultUInt64(context,
                      valid ? snapshot->predicted_ring_round_trip_ns : 0u);
    SCPI_ResultUInt64(context, valid ? snapshot->ring_round_trip_ns : 0u);
    SCPI_ResultUInt64(context, valid ? snapshot->residual_ns : 0u);
    SCPI_ResultUInt32(context, valid ? snapshot->table_crc32 : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_path_candidate_q(scpi_t *context)
{
    calibration_path_import_status_t import_status;
    calibration_path_snapshot_t candidate;
    const bool candidate_valid = calibration_manager_get_path_candidate(&candidate);
    if (!calibration_manager_get_path_import_status(&import_status)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "CALPATHCAND");
    SCPI_ResultUInt32(context, import_status.active);
    SCPI_ResultUInt32(context, import_status.complete);
    SCPI_ResultBool(context, candidate_valid ? TRUE : FALSE);
    SCPI_ResultUInt32(context, import_status.header.link_count);
    SCPI_ResultUInt32(context, import_status.valid_link_bitmap);
    SCPI_ResultUInt32(context, import_status.reject_reason);
    SCPI_ResultUInt32(context, import_status.header.topology_generation);
    SCPI_ResultUInt32(context, import_status.header.topology_crc32);
    SCPI_ResultUInt32(context, import_status.header.bias_generation);
    SCPI_ResultUInt32(context, import_status.header.profile_crc32);
    SCPI_ResultUInt32(context, import_status.header.schedule_crc32);
    SCPI_ResultUInt32(context, import_status.header.calibration_generation);
    SCPI_ResultUInt32(context, import_status.header.freshness_us);
    SCPI_ResultUInt32(context, import_status.header.evidence_age_us);
    SCPI_ResultUInt64(context, import_status.header.ring_round_trip_ns);
    SCPI_ResultUInt64(context,
                      import_status.header.forwarding_residence_ns);
    SCPI_ResultUInt32(context, import_status.header.expected_table_crc32);
    SCPI_ResultUInt32(context, import_status.calculated_table_crc32);
    SCPI_ResultUInt32(context, candidate_valid ? candidate.table_crc32 : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_path_candidate_link_q(scpi_t *context)
{
    uint32_t link_index = 0u;
    calibration_path_link_evidence_t link;
    bool valid = false;
    if (SCPI_ParamUInt32(context, &link_index, TRUE) != TRUE ||
        !calibration_manager_get_path_import_link(link_index, &link, &valid)) {
        scpi_port_push_exec_error(context, "CAL_PATH_LINK_QUERY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "CALPATHLINK");
    SCPI_ResultBool(context, valid ? TRUE : FALSE);
    SCPI_ResultUInt32(context, link_index);
    SCPI_ResultUInt32(context, link.source_node);
    SCPI_ResultUInt32(context, link.destination_node);
    SCPI_ResultUInt32(context, link.profile_crc32);
    SCPI_ResultUInt32(context, link.topology_generation);
    SCPI_ResultUInt32(context, link.bias_generation);
    SCPI_ResultUInt32(context, link.sample_count);
    SCPI_ResultUInt32(context, link.accepted_count);
    SCPI_ResultUInt32(context, link.jitter_ns);
    SCPI_ResultUInt32(context, link.asymmetry_ns);
    SCPI_ResultUInt64(context, link.measurement.residence_ns);
    SCPI_ResultUInt64(context, link.measurement.raw_path_sum_ns);
    SCPI_ResultInt64(context, link.measurement.corrected_path_sum_ns);
    SCPI_ResultInt64(context, link.measurement.delay_estimate_ns);
    SCPI_ResultUInt64(context, link.measurement.clock_rate_error_bound_ns);
    SCPI_ResultUInt32(context, link.measurement.reject_reason);
    SCPI_ResultBool(context, link.measurement.reference_accepted ? TRUE : FALSE);
    SCPI_ResultBool(context, link.measurement.active_eligible ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_path_active_q(scpi_t *context)
{
    calibration_path_snapshot_t snapshot = {0};
    const bool valid = calibration_manager_get_active_path(&snapshot);
    return scpi_calibration_path_snapshot_result(
        context, "CALPATHACTIVE", &snapshot, valid);
}

scpi_result_t scpi_calibration_path_rollback_q(scpi_t *context)
{
    calibration_path_snapshot_t snapshot = {0};
    const bool valid = calibration_manager_get_rollback_path(&snapshot);
    return scpi_calibration_path_snapshot_result(
        context, "CALPATHROLLBACK", &snapshot, valid);
}

scpi_result_t scpi_calibration_bias_start(scpi_t *context)
{
    uint32_t expected_path_ns = 0u;
    uint32_t minimum_samples = 0u;
    uint32_t maximum_samples = 0u;
    uint32_t maximum_spread_ns = 0u;
    uint32_t maximum_clock_error_ns = 0u;
    if (SCPI_ParamUInt32(context, &expected_path_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &minimum_samples, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &maximum_samples, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &maximum_spread_ns, TRUE) != TRUE ||
        SCPI_ParamUInt32(context, &maximum_clock_error_ns, TRUE) != TRUE ||
        !calibration_manager_start_bias(expected_path_ns, minimum_samples,
                                         maximum_samples, maximum_spread_ns,
                                         maximum_clock_error_ns)) {
        scpi_port_push_exec_error(context, "CAL_BIAS_START_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, expected_path_ns);
    SCPI_ResultUInt32(context, minimum_samples);
    SCPI_ResultUInt32(context, maximum_samples);
    SCPI_ResultUInt32(context, maximum_spread_ns);
    SCPI_ResultUInt32(context, maximum_clock_error_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_bias_stop(scpi_t *context)
{
    calibration_manager_stop_bias();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_bias_q(scpi_t *context)
{
    calibration_bias_snapshot_t snapshot;
    if (!calibration_manager_get_bias_snapshot(&snapshot)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, snapshot.valid);
    SCPI_ResultUInt32(context, snapshot.flags);
    SCPI_ResultUInt32(context, snapshot.reject_reason);
    SCPI_ResultUInt32(context, snapshot.generation);
    SCPI_ResultUInt32(context, snapshot.sample_count);
    SCPI_ResultUInt32(context, snapshot.accepted_count);
    SCPI_ResultUInt32(context, snapshot.rejected_count);
    SCPI_ResultUInt32(context, snapshot.persona_generation);
    SCPI_ResultUInt32(context, snapshot.profile_crc32);
    SCPI_ResultUInt32(context, snapshot.topology_generation);
    SCPI_ResultUInt32(context, snapshot.first_epoch);
    SCPI_ResultUInt32(context, snapshot.last_epoch);
    SCPI_ResultInt32(context, (int32_t)snapshot.mean_bias_ns);
    SCPI_ResultUInt32(context, snapshot.spread_ns);
    SCPI_ResultUInt32(context, snapshot.table_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_save(scpi_t *context)
{
    uint32_t job_id = 0u;
    if (!calibration_manager_save_bias_snapshot(&job_id)) {
        scpi_port_push_exec_error(context, "CAL_SAVE_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, job_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_activate(scpi_t *context)
{
    if (!calibration_manager_activate_path_candidate()) {
        scpi_port_push_exec_error(context, "CAL_ACTIVATE_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_rollback(scpi_t *context)
{
    if (!calibration_manager_rollback_path()) {
        scpi_port_push_exec_error(context, "CAL_ROLLBACK_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_clear(scpi_t *context)
{
    if (!calibration_manager_clear_path_candidate()) {
        scpi_port_push_exec_error(context, "CAL_CLEAR_REJECTED");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_clk_coded_stop(scpi_t *context)
{
    calibration_manager_stop_clk_coded();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_clk_coded_q(scpi_t *context)
{
    calibration_clk_coded_snapshot_t snapshot;
    if (!calibration_manager_get_clk_coded_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.reject_reason);
    SCPI_ResultUInt32(context, snapshot.flags);
    scpi_calibration_result_u64(context, snapshot.board_unique_id);
    scpi_calibration_result_u64(context, snapshot.build_id);
    SCPI_ResultUInt32(context, snapshot.local_node);
    SCPI_ResultUInt32(context, snapshot.train_epoch);
    SCPI_ResultUInt32(context, snapshot.train_sequence);
    SCPI_ResultUInt32(context, snapshot.calibration_generation);
    SCPI_ResultUInt32(context, snapshot.topology_generation);
    SCPI_ResultUInt32(context, snapshot.topology_crc32);
    SCPI_ResultUInt32(context, snapshot.profile_crc32);
    SCPI_ResultUInt32(context, snapshot.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.baud_hz);
    SCPI_ResultUInt32(context, snapshot.codebook_id);
    SCPI_ResultUInt32(context, snapshot.sample_period_ns);
    SCPI_ResultUInt32(context, snapshot.coarse_min_sample);
    SCPI_ResultUInt32(context, snapshot.coarse_max_sample);
    scpi_calibration_result_u64(context, snapshot.capture_origin_tick);
    SCPI_ResultUInt32(context, snapshot.capture_sample_count);
    SCPI_ResultUInt32(context, snapshot.timing_field_tx_origin_sample);
    SCPI_ResultUInt32(context, snapshot.best_lag_sample);
    SCPI_ResultUInt32(context, snapshot.best_distance);
    SCPI_ResultUInt32(context, snapshot.second_lag_sample);
    SCPI_ResultUInt32(context, snapshot.second_distance);
    SCPI_ResultUInt32(context, snapshot.margin);
    SCPI_ResultUInt32(context, snapshot.detected_polarity);
    SCPI_ResultUInt32(context, snapshot.marker_flags);
    SCPI_ResultUInt32(context, snapshot.tx_dma_count);
    SCPI_ResultUInt32(context, snapshot.rx_dma_count);
    SCPI_ResultUInt32(context, snapshot.dma_overrun_count);
    SCPI_ResultUInt32(context, snapshot.pio_stall_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_p3_start(scpi_t *context)
{
    uint32_t role = 0u, baud_hz = 0u, pulse_count = 0u;
    uint32_t capture_words = 0u, epoch = 0u;
    uint32_t signal_group = TDMA_PIO_SPI_P3_GROUP_CLK_DATA;
    const bool required_ok =
        SCPI_ParamUInt32(context, &role, TRUE) == TRUE &&
        SCPI_ParamUInt32(context, &baud_hz, TRUE) == TRUE &&
        SCPI_ParamUInt32(context, &pulse_count, TRUE) == TRUE &&
        SCPI_ParamUInt32(context, &capture_words, TRUE) == TRUE &&
        SCPI_ParamUInt32(context, &epoch, TRUE) == TRUE;
    (void)SCPI_ParamUInt32(context, &signal_group, FALSE);
    if (!required_ok ||
        !calibration_manager_request_p3(
            role, baud_hz, pulse_count, capture_words, epoch,
            signal_group)) {
        scpi_port_push_exec_error(context, "CAL_P3_START_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, role);
    SCPI_ResultUInt32(context, baud_hz);
    SCPI_ResultUInt32(context, pulse_count);
    SCPI_ResultUInt32(context, capture_words);
    SCPI_ResultUInt32(context, epoch);
    SCPI_ResultUInt32(context, signal_group);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_p3_stop(scpi_t *context)
{
    calibration_manager_stop_p3();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_p3_q(scpi_t *context)
{
    calibration_manager_p3_snapshot_t snapshot;
    if (!calibration_manager_get_p3_snapshot(&snapshot)) return SCPI_RES_ERR;
    const tdma_pio_spi_p3_snapshot_t *raw = &snapshot.raw;
    SCPI_ResultUInt32(context, raw->state);
    SCPI_ResultUInt32(context, raw->role);
    SCPI_ResultUInt32(context, raw->signal_group);
    SCPI_ResultUInt32(context, raw->flags);
    SCPI_ResultUInt32(context, raw->reject_reason);
    SCPI_ResultUInt32(context, raw->baud_hz);
    SCPI_ResultUInt32(context, raw->epoch);
    SCPI_ResultUInt32(context, raw->sample_period_ns);
    SCPI_ResultUInt32(context, raw->pulse_count);
    SCPI_ResultUInt32(context, raw->requested_words);
    SCPI_ResultUInt32(context, raw->produced_words);
    SCPI_ResultUInt32(context, raw->edge_mask);
    SCPI_ResultUInt32(context, raw->dma_overrun_count);
    SCPI_ResultUInt32(context, raw->pio_stall_count);
    SCPI_ResultUInt32(context, raw->clock_high_ns);
    SCPI_ResultUInt32(context, raw->clock_low_ns);
    SCPI_ResultUInt32(context, raw->data_high_ns);
    scpi_calibration_result_u64(context, raw->t1_clk_tx);
    scpi_calibration_result_u64(context, raw->t2_clk_rx);
    scpi_calibration_result_u64(context, raw->t3_data_tx);
    scpi_calibration_result_u64(context, raw->t4_data_rx);
    SCPI_ResultUInt32(context, snapshot.result_valid);
    SCPI_ResultUInt32(context, raw->data_pulse_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_link_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.command_seq);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.active_crc32);
    SCPI_ResultBool(context, status.ready ? FALSE : TRUE);
    SCPI_ResultUInt32(context, status.ready ? 1u : 0u);
    SCPI_ResultText(context, "SMA");
    SCPI_ResultText(context, "A0");
    SCPI_ResultText(context, "OUT1");
    SCPI_ResultText(context, "A1");
    SCPI_ResultText(context, "IN1");
    SCPI_ResultText(context, "BIDIR");
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_parameter_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.command_seq);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.active_crc32);
    SCPI_ResultBool(context, status.ready ? FALSE : TRUE);
    SCPI_ResultUInt32(context, status.ready ? 1u : 0u);
    SCPI_ResultText(context, "SMA");
    SCPI_ResultText(context, "A0");
    SCPI_ResultText(context, "OUT1");
    SCPI_ResultText(context, "A1");
    SCPI_ResultText(context, "IN1");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_result_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultText(context, status.ready ? "DONE" : "IDLE");
    SCPI_ResultText(context, "SMA");
    SCPI_ResultText(context, "A0:OUT1");
    SCPI_ResultText(context, "A1:IN1");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.last_error);
    SCPI_ResultText(context, status.last_error == 0u ? "NONE" : "ERROR");
    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.state);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.active_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_list_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, 0x10000003u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "ALL");
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_active_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_path_snapshot_t active;
    calibration_path_snapshot_t rollbackable;
    calibration_manager_get_status(&status);
    const bool active_valid = calibration_manager_get_active_path(&active);
    const bool rollback_valid =
        calibration_manager_get_rollback_path(&rollbackable);

    /* Keep the six-field response shape used by existing diagnostics while
     * replacing the old fixed record with the manager-owned path snapshot. */
    SCPI_ResultText(context, active_valid ? "PATH" : "NONE");
    SCPI_ResultText(context, active_valid ? "ACTIVE" : "EMPTY");
    SCPI_ResultUInt32(context, active_valid ? active.table_crc32 : 0u);
    SCPI_ResultBool(context, active_valid ? TRUE : FALSE);
    SCPI_ResultBool(context, rollback_valid ? TRUE : FALSE);
    SCPI_ResultText(context, active_valid ? "ACK" : "NO_ACTIVE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_meta_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultText(context, "OP");
    SCPI_ResultText(context, "FIXTURE");
    SCPI_ResultText(context, "CABLE");
    SCPI_ResultInt32(context, 25);
    SCPI_ResultText(context, "FRAMEWORK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_health_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultText(context, status.ready ? "OK" : "INIT");
    SCPI_ResultUInt32(context, status.link_count);
    SCPI_ResultUInt32(context, status.delay_count);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, status.last_error == 0u ? "NONE" : "ERROR");
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_limit_q(scpi_t *context)
{
    SCPI_ResultText(context, "DEFAULT");
    SCPI_ResultText(context, "SMA");
    SCPI_ResultUInt32(context, 1000u);
    SCPI_ResultUInt32(context, 100u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 86400u);
    SCPI_ResultBool(context, FALSE);
    return SCPI_RES_OK;
}
