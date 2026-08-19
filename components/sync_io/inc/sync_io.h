#ifndef SYNC_IO_H
#define SYNC_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
} sync_io_config_t;

typedef struct {
    bool initialized;
    bool capture_running;
    bool sync_clock_running;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t dropped_capture_words;
    uint32_t latched_capture_words;
    uint32_t dropped_latched_capture_words;
    uint32_t capture_latch_source;
    uint32_t capture_latch_resolution_ns;
    uint32_t capture_latch_flags;
    uint32_t capture_timestamp_window_armed;
    uint32_t capture_timestamp_window_periodic;
    uint32_t capture_timestamp_window_start_lo;
    uint32_t capture_timestamp_window_start_hi;
    uint32_t capture_timestamp_window_end_lo;
    uint32_t capture_timestamp_window_end_hi;
    uint32_t capture_timestamp_window_period_ns;
    uint32_t capture_timestamp_window_sample_period_ns;
    uint32_t capture_timestamp_window_observed_mask;
    uint32_t capture_timestamp_window_initial_sample_mask;
    uint32_t capture_timebase_valid;
    uint32_t capture_timebase_start_lo;
    uint32_t capture_timebase_start_hi;
} sync_io_status_t;

typedef struct {
    bool initialized;
    bool capture_running;
    bool pio_enabled;
    bool dma_busy;
    bool rx_fifo_empty;
    bool rx_fifo_full;
    uint32_t dma_transfer_count;
    uint32_t dma_write_addr_lsb;
    uint32_t dma_ring_addr_lsb;
    uint32_t dma_ring_align_mask;
    uint32_t capture_dma_read_seq;
    uint32_t produced_words;
} sync_io_capture_debug_t;

typedef struct {
    uint32_t raw_word;
    uint32_t sample_seq;
    uint32_t previous_sample_mask;
    uint32_t base_time_l32_ns;
    uint64_t matched_window_start_ns;
    uint32_t sample_period_ns;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t dropped_before;
} sync_io_capture_latched_word_t;

typedef struct {
    bool running;
    bool pio_enabled;
    bool dma_busy;
    bool dma_irq_enabled;
    bool tx_fifo_empty;
    bool tx_fifo_full;
    uint32_t transfer_count;
    uint32_t rollover_count_low32;
} sync_io_seq_step_runtime_t;

typedef struct {
    bool running;
    bool pio_enabled;
    bool dma_busy;
    bool dma_irq_enabled;
    bool tx_fifo_empty;
    bool tx_fifo_full;
    uint32_t transfer_count;
    uint32_t dma_restart_count;
} sync_io_enc_count_runtime_t;

typedef struct {
    uint32_t delay_us;
    uint32_t high_us;
} sync_io_model_pulse_entry_t;

typedef struct {
    uint32_t delay_ns;
    uint32_t high_ns;
} sync_io_model_pulse_entry_ns_t;

typedef struct {
    bool running;
    bool pio_enabled;
    bool dma_busy;
    bool tx_fifo_empty;
    bool tx_fifo_full;
    uint32_t total_pulses;
    uint32_t completed_pulses;
    uint32_t transfer_count;
    uint32_t elapsed_us;
    uint32_t fault_code;
} sync_io_model_pulse_runtime_t;

typedef struct {
    bool running;
    uint32_t output_channel;
    uint32_t output_pin;
    uint32_t requested_hz;
    uint32_t actual_hz;
    uint32_t system_clock_hz;
    uint32_t pwm_top;
    uint32_t pwm_div16;
} sync_io_sma_frequency_tx_status_t;

typedef struct {
    uint32_t input_channel;
    uint32_t input_pin;
    uint32_t gate_us;
    uint32_t elapsed_us;
    uint32_t edge_count;
    uint32_t frequency_hz;
} sync_io_sma_frequency_rx_result_t;

typedef enum {
    SYNC_IO_AUX0 = 0,
    SYNC_IO_AUX1,
    SYNC_IO_AUX2,
    SYNC_IO_AUX3,
    SYNC_IO_AUX_COUNT,
} sync_io_aux_channel_t;

typedef enum {
    SYNC_IO_AUX_MODE_INPUT = 0,
    SYNC_IO_AUX_MODE_PIO_OUTPUT,
} sync_io_aux_mode_t;

typedef struct {
    uint32_t clk_pin;
    uint32_t data_pin;
    uint32_t frame_bits;
    uint32_t sample_delay_cycles;
    uint32_t sample_edge;
} sync_io_biss_tap_config_t;

bool sync_io_init(const sync_io_config_t *config);
bool sync_io_start_capture(uint32_t sample_hz);
void sync_io_stop_capture(void);
size_t sync_io_read_capture_words(uint32_t *buffer, size_t max_words);
void sync_io_capture_latch_service_core1(void);
size_t sync_io_read_capture_latched(sync_io_capture_latched_word_t *buffer,
                                    size_t max_words);
bool sync_io_capture_time_now_ns(uint64_t *now_ns);
bool sync_io_capture_arm_timestamp_window(uint64_t window_start_ns,
                                          uint32_t window_width_ns,
                                          uint32_t sample_period_ns,
                                          uint32_t observed_mask,
                                          uint32_t initial_sample_mask);
bool sync_io_capture_arm_periodic_timestamp_window(uint64_t window_start_ns,
                                                   uint32_t window_width_ns,
                                                   uint32_t period_ns,
                                                   uint32_t sample_period_ns,
                                                   uint32_t observed_mask,
                                                   uint32_t initial_sample_mask);
void sync_io_capture_disarm_timestamp_window(void);
bool sync_io_fire_pulse_cycles(uint32_t high_cycles);
bool sync_io_fire_pulse_us(uint32_t high_us);
bool sync_io_fire_pulse_out_cycles(uint32_t high_cycles);
bool sync_io_fire_pulse_out_us(uint32_t high_us);
bool sync_io_start_clock(uint32_t frequency_hz);
void sync_io_stop_clock(void);
/* Deprecated compatibility aliases: legacy marker commands pulse
 * RJ45_TRIG_OUT; marker is not a separate product hardware signal. */
bool sync_io_fire_marker_cycles(uint32_t high_cycles);
bool sync_io_fire_marker_us(uint32_t high_us);
bool sync_io_fire_rj45_trigger_us(uint32_t high_us);
bool sync_io_debug_set_output_mask(uint32_t mask);
void sync_io_debug_release_output_mask(void);
uint32_t sync_io_debug_read_input_mask(void);
bool sync_io_debug_model_set_output_mask(uint32_t enable_mask, uint32_t value_mask);
bool sync_io_debug_model_write_pin(uint32_t pin_index, bool enable, bool value);
void sync_io_debug_model_release(void);
uint32_t sync_io_debug_model_read_input_mask(void);
uint32_t sync_io_debug_model_get_output_enable_mask(void);
uint32_t sync_io_debug_model_get_output_value_mask(void);
bool sync_io_sma_frequency_tx_start(
    uint32_t output_channel,
    uint32_t frequency_hz,
    sync_io_sma_frequency_tx_status_t *status);
void sync_io_sma_frequency_tx_stop(void);
void sync_io_sma_frequency_tx_get_status(
    sync_io_sma_frequency_tx_status_t *status);
bool sync_io_sma_frequency_rx_measure(
    uint32_t input_channel,
    uint32_t gate_us,
    sync_io_sma_frequency_rx_result_t *result);
bool sync_io_model_pulse_schedule_arm(uint32_t output_index,
                                      const sync_io_model_pulse_entry_t *entries,
                                      uint32_t entry_count,
                                      bool rising_edge);
bool sync_io_model_pulse_schedule_arm_ns(
    uint32_t output_index,
    const sync_io_model_pulse_entry_ns_t *entries,
    uint32_t entry_count,
    bool rising_edge,
    uint32_t tick_period_ns);
bool sync_io_output_pulse_schedule_arm(uint32_t output_index,
                                       const sync_io_model_pulse_entry_t *entries,
                                       uint32_t entry_count,
                                       bool rising_edge);
bool sync_io_output_pulse_schedule_arm_ns(
    uint32_t output_index,
    const sync_io_model_pulse_entry_ns_t *entries,
    uint32_t entry_count,
    bool rising_edge,
    uint32_t tick_period_ns);
void sync_io_model_pulse_schedule_disarm(void);
bool sync_io_model_pulse_schedule_is_running(void);
void sync_io_model_pulse_schedule_get_runtime(sync_io_model_pulse_runtime_t *runtime);
bool sync_io_aux_set_mode(sync_io_aux_channel_t channel, sync_io_aux_mode_t mode);
bool sync_io_aux_write(sync_io_aux_channel_t channel, bool level);
bool sync_io_aux_read(sync_io_aux_channel_t channel, bool *level);
bool sync_io_biss_tap_arm(const sync_io_biss_tap_config_t *config);
void sync_io_biss_tap_disarm(void);
bool sync_io_biss_tap_is_running(void);
bool sync_io_biss_tap_read_frame_word(uint32_t *word);
void sync_io_get_status(sync_io_status_t *status);
void sync_io_get_capture_debug(sync_io_capture_debug_t *debug);

/* ── SEQ_STEP 编码序列步进 ── */

typedef enum {
    SYNC_IO_EDGE_RISING  = 0,
    SYNC_IO_EDGE_FALLING = 1,
} sync_io_edge_t;

bool sync_io_seq_step_arm(const uint32_t *seq_table,
                          uint32_t seq_length,
                          uint32_t seq_width,
                          uint32_t trigger_pin,
                          sync_io_edge_t edge,
                          bool gate_enabled);
void sync_io_seq_step_disarm(void);
uint32_t sync_io_seq_step_get_index(void);
uint64_t sync_io_seq_step_get_rollover_count(void);
bool sync_io_seq_step_is_running(void);
void sync_io_seq_step_get_runtime(sync_io_seq_step_runtime_t *runtime);
void sync_io_seq_step_trace_runtime_sample(bool force);
void sync_io_set_expected_ready_mask(uint32_t mask);
uint32_t sync_io_get_expected_ready_mask(void);
void sync_io_trace_aux_status_sample(bool force);

/* ── ENC_COUNT 编码器计数触发 ── */

bool sync_io_enc_count_arm(uint32_t target,
                           uint32_t in_pin_base,
                           uint32_t output_pin);
void sync_io_enc_count_disarm(void);
uint32_t sync_io_enc_count_get_count(void);
bool sync_io_enc_count_is_running(void);
void sync_io_enc_count_get_runtime(sync_io_enc_count_runtime_t *runtime);
void sync_io_enc_count_trace_runtime_sample(bool force);

#endif
