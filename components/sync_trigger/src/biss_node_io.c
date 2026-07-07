#include "biss_node_io.h"

#include <string.h>

#include "hardware/pio.h"
#include "board_config.h"
#include "osal.h"
#include "sync_io.h"

typedef struct {
    bool running;
    bool rx_irq_pending;
    biss_profile_t profile;
    uint64_t frame_accum;
    uint32_t frame_words_seen;
    uint32_t frame_words_expected;
    uint32_t final_word_bits;
    uint32_t last_frame_ms;
    uint32_t timeout_ms;
    bool timeout_latched;
    sync_io_biss_tap_config_t tap_config;
} biss_node_io_context_t;

static biss_node_io_context_t s_biss_node_io;

bool biss_node_io_make_profile(const trigger_vector_t *vector,
                               biss_profile_t *profile)
{
    if (vector == NULL || profile == NULL) {
        return false;
    }

    memset(profile, 0, sizeof(*profile));
    profile->frame_bits = vector->biss_frame_bits;
    profile->position_offset = vector->biss_position_offset;
    profile->position_bits = vector->biss_position_bits;
    profile->modulo = vector->biss_position_modulo;
    profile->anchor_offset = vector->biss_anchor_offset;
    profile->anchor_bits = vector->biss_anchor_bits;
    profile->anchor_mask = vector->biss_anchor_mask;
    profile->anchor_value = vector->biss_anchor_value;
    profile->error_bit_offset = vector->biss_error_bit_offset;
    profile->warning_bit_offset = vector->biss_warning_bit_offset;
    profile->status_gate_policy =
        (biss_status_gate_policy_t)vector->biss_status_gate_policy;
    profile->crc_offset = vector->biss_crc_offset;
    profile->crc_bits = vector->biss_crc_bits;
    profile->crc_cover_offset = vector->biss_crc_cover_offset;
    profile->crc_cover_bits = vector->biss_crc_cover_bits;
    profile->crc_polynomial = (uint8_t)vector->biss_crc_polynomial;
    profile->crc_init = (uint8_t)vector->biss_crc_init;
    profile->crc_xor = (uint8_t)vector->biss_crc_xor;
    profile->crc_invert = (vector->biss_crc_invert != 0u);
    profile->crc_gate_policy =
        (biss_crc_gate_policy_t)vector->biss_crc_gate_policy;
    profile->sample_edge = (biss_sample_edge_t)vector->biss_sample_edge;
    profile->sample_delay_cycles = vector->biss_sample_delay_cycles;
    profile->pio_cycles_per_bit = vector->biss_clock_hz == 0u
                                      ? 0u
                                      : (250000000u / vector->biss_clock_hz);
    profile->timeout_us = vector->biss_timeout_us;
    return true;
}

bool biss_node_io_arm(const trigger_vector_t *vector)
{
    biss_profile_t profile;
    if (!biss_node_io_make_profile(vector, &profile) ||
        biss_profile_validate(&profile) != BISS_PROFILE_OK) {
        return false;
    }

    const sync_io_biss_tap_config_t tap_config = {
        .clk_pin = vector->biss_clk_in_pin,
        .data_pin = vector->biss_data_in_pin,
        .frame_bits = vector->biss_frame_bits,
        .sample_delay_cycles = vector->biss_active_sample_delay_cycles,
        .sample_edge = vector->biss_active_sample_edge,
    };
    if (!sync_io_biss_tap_arm(&tap_config)) {
        return false;
    }

    memset(&s_biss_node_io, 0, sizeof(s_biss_node_io));
    s_biss_node_io.profile = profile;
    s_biss_node_io.profile.sample_delay_cycles =
        vector->biss_active_sample_delay_cycles;
    s_biss_node_io.profile.sample_edge =
        (biss_sample_edge_t)vector->biss_active_sample_edge;
    s_biss_node_io.tap_config = tap_config;
    s_biss_node_io.frame_words_expected = (profile.frame_bits + 31u) / 32u;
    s_biss_node_io.final_word_bits = profile.frame_bits % 32u;
    if (s_biss_node_io.final_word_bits == 0u) {
        s_biss_node_io.final_word_bits = 32u;
    }
    s_biss_node_io.last_frame_ms = osal_uptime_ms();
    s_biss_node_io.timeout_ms = (profile.timeout_us + 999u) / 1000u;
    if (s_biss_node_io.timeout_ms == 0u) {
        s_biss_node_io.timeout_ms = 1u;
    }
    s_biss_node_io.running = true;
    return true;
}

void biss_node_io_disarm(void)
{
    sync_io_biss_tap_disarm();
    memset(&s_biss_node_io, 0, sizeof(s_biss_node_io));
}

bool biss_node_io_is_running(void)
{
    return s_biss_node_io.running;
}

void biss_node_io_rx_irq_callback(void)
{
    s_biss_node_io.rx_irq_pending = true;
}

static void biss_node_io_commit_position(trigger_vector_t *vector,
                                         uint32_t position,
                                         bool count_rx_frame)
{
    const uint32_t last_position = vector->biss_last_position;

    if (count_rx_frame) {
        vector->biss_rx_frame_count++;
    }
    vector->trigger_count = vector->biss_rx_frame_count;

    if (vector->biss_target != 0u &&
        biss_crossed_position(last_position,
                              position,
                              vector->biss_target,
                              vector->biss_position_modulo)) {
        (void)sync_io_fire_rj45_trigger_us(vector->trigger_width_us);
        vector->biss_pulse_out_count++;
        vector->biss_trigger_count++;
        vector->output_count = vector->biss_pulse_out_count;
    }

    vector->biss_last_position = position;
}

bool biss_node_io_process_position(trigger_vector_t *vector, uint32_t position)
{
    if (vector == NULL || !s_biss_node_io.running) {
        return false;
    }

    biss_node_io_commit_position(vector, position, true);
    return true;
}

bool biss_node_io_process_frame(trigger_vector_t *vector, uint64_t frame)
{
    if (vector == NULL || !s_biss_node_io.running) {
        return false;
    }

    const biss_profile_t *profile = &s_biss_node_io.profile;
    bool frame_ok = true;

    vector->biss_rx_frame_count++;
    vector->trigger_count = vector->biss_rx_frame_count;

    if (!biss_anchor_matches(frame, profile)) {
        vector->biss_frame_error_count++;
        frame_ok = false;
    }

    const bool crc_ok = biss_crc_matches(frame, profile);
    if (!crc_ok) {
        vector->biss_crc_error_count++;
        if (profile->crc_gate_policy == BISS_CRC_GATE_BLOCK_TRIGGER) {
            frame_ok = false;
        }
    }

    const biss_status_bits_t status = biss_extract_status(frame, profile);
    const bool status_active = status.error_active || status.warning_active;
    const bool status_gate_ok =
        biss_status_gate_allows(status, profile->status_gate_policy);
    if (status_active &&
        profile->status_gate_policy != BISS_STATUS_GATE_IGNORE) {
        vector->biss_status_block_count++;
    }

    if (!frame_ok) {
        return true;
    }

    const uint32_t position = biss_extract_position(frame, profile);
    if (status_gate_ok) {
        biss_node_io_commit_position(vector, position, false);
    } else {
        vector->biss_last_position = position;
    }
    return true;
}

static void biss_node_io_reset_frame_assembler(void)
{
    s_biss_node_io.frame_accum = 0u;
    s_biss_node_io.frame_words_seen = 0u;
}

static bool biss_node_io_ingest_word(trigger_vector_t *vector, uint32_t word)
{
    if (vector == NULL ||
        s_biss_node_io.frame_words_expected == 0u ||
        s_biss_node_io.frame_words_expected > 2u) {
        biss_node_io_reset_frame_assembler();
        if (vector != NULL) {
            vector->biss_frame_error_count++;
        }
        return false;
    }

    const uint32_t word_index = s_biss_node_io.frame_words_seen;
    const uint32_t chunk_bits =
        (word_index + 1u == s_biss_node_io.frame_words_expected)
            ? s_biss_node_io.final_word_bits
            : 32u;
    const uint64_t chunk_mask = chunk_bits == 32u
                                    ? UINT32_MAX
                                    : ((1ull << chunk_bits) - 1ull);

    s_biss_node_io.frame_accum =
        (s_biss_node_io.frame_accum << chunk_bits) |
        ((uint64_t)word & chunk_mask);
    s_biss_node_io.frame_words_seen++;

    if (s_biss_node_io.frame_words_seen < s_biss_node_io.frame_words_expected) {
        return true;
    }

    const uint64_t frame = s_biss_node_io.frame_accum;
    biss_node_io_reset_frame_assembler();
    s_biss_node_io.last_frame_ms = osal_uptime_ms();
    s_biss_node_io.timeout_latched = false;
    return biss_node_io_process_frame(vector, frame);
}

static void biss_node_io_check_timeout(trigger_vector_t *vector)
{
    if (vector == NULL || s_biss_node_io.timeout_latched) {
        return;
    }

    const uint32_t now_ms = osal_uptime_ms();
    if ((uint32_t)(now_ms - s_biss_node_io.last_frame_ms) >=
        s_biss_node_io.timeout_ms) {
        vector->biss_timeout_count++;
        s_biss_node_io.timeout_latched = true;
        biss_node_io_reset_frame_assembler();
        s_biss_node_io.last_frame_ms = now_ms;

        if (vector->biss_sample_scan_enabled != 0u) {
            const uint32_t start = vector->biss_sample_scan_start_cycles;
            const uint32_t end = vector->biss_sample_scan_end_cycles;
            const uint32_t step =
                vector->biss_sample_scan_step_cycles != 0u
                    ? vector->biss_sample_scan_step_cycles
                    : 1u;
            uint32_t next = vector->biss_active_sample_delay_cycles + step;

            if (start <= end && (next > end || next < start)) {
                next = start;
                vector->biss_sample_scan_wrap_count++;
            }

            vector->biss_active_sample_delay_cycles = next;
            vector->biss_sample_scan_index++;
            s_biss_node_io.profile.sample_delay_cycles = next;
            s_biss_node_io.tap_config.sample_delay_cycles = next;
            (void)sync_io_biss_tap_arm(&s_biss_node_io.tap_config);
        }
    }
}

bool biss_node_io_poll(trigger_vector_t *vector)
{
    if (vector == NULL || !s_biss_node_io.running) {
        return false;
    }

    if (!sync_io_biss_tap_is_running()) {
        return false;
    }

    if (pio_sm_is_rx_fifo_full(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX0_SM)) {
        vector->biss_fifo_overflow_count++;
        biss_node_io_reset_frame_assembler();
    }

    uint32_t word;
    while (sync_io_biss_tap_read_frame_word(&word)) {
        (void)biss_node_io_ingest_word(vector, word);
    }
    biss_node_io_check_timeout(vector);

    if (s_biss_node_io.rx_irq_pending) {
        s_biss_node_io.rx_irq_pending = false;
    }

    return true;
}
