#ifndef SYNC_IO_INTERNAL_H
#define SYNC_IO_INTERNAL_H

#include "sync_io.h"

#include <assert.h>
#include <string.h>

#include "board_config.h"
#include "diagnostics.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "osal.h"
#include "resource_arbiter.h"
#include "biss_tap_rx.pio.h"
#include "sync_io_hw_profile.h"
#include "../src/sync_io_core_internal.h"
#include "storage_manager.h"
#include "sync_io.pio.h"
#include "vdc_timestamp_clock.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/time.h"
#endif

#define SYNC_IO_DEFAULT_CAPTURE_HZ  1000000u
#define SYNC_IO_DEFAULT_CLOCK_HZ    1000000u
#define SYNC_IO_MIN_HZ              1u
#define SYNC_IO_TRACE_DOMAIN        3u
#define SYNC_IO_AUX_READY_TIMEOUT_MS 1000u
#define SYNC_IO_CLOCK_OWNER         "sync.clock"
#define SYNC_IO_CLOCK_RESOURCES     (RESOURCE_ARBITER_RESOURCE_PIO2 | RESOURCE_ARBITER_RESOURCE_AUX)
#define SYNC_IO_CLOCK_PIO           BOARD_SYNC_PIO_AUX
#define SYNC_IO_CLOCK_SM            BOARD_SYNC_AUX2_SM
#define SYNC_IO_CLOCK_PIN           BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN
#define SYNC_IO_CAPTURE_LATCH_RING_SIZE 128u
#define SYNC_IO_CAPTURE_DMA_RING_WORDS SYNC_IO_SHARED_WORKSPACE_WORDS
#define SYNC_IO_CAPTURE_DMA_RING_BITS SYNC_IO_SHARED_WORKSPACE_DMA_RING_BITS
#define SYNC_IO_CAPTURE_DMA_RING_BYTES \
    (SYNC_IO_CAPTURE_DMA_RING_WORDS * sizeof(uint32_t))
#define SYNC_IO_CAPTURE_LATCH_SERVICE_MAX_WORDS 2048u
#define SYNC_IO_CAPTURE_WORD_SAMPLES 8u
#define SYNC_IO_CAPTURE_TIMESTAMP_SOURCE_HARDWARE_TICK 2u
#define SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY 0x00000001u
#define SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DPLL_ELIGIBLE   0x00000002u
#define SYNC_IO_SMA_FREQUENCY_MIN_HZ 1000000u
#define SYNC_IO_SMA_FREQUENCY_MAX_HZ 50000000u
#define SYNC_IO_SMA_FREQUENCY_MIN_GATE_US 100u
#define SYNC_IO_SMA_FREQUENCY_MAX_GATE_US 1000u

typedef struct {
    bool initialized;
    bool capture_running;
    bool clock_running;
    uint capture_offset;
    uint clock_offset;
    uint aux_offset;
    uint aux_passthrough_offset;
    uint biss_tap_offset;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t dropped_capture_words;
    uint32_t latched_capture_words;
    uint32_t dropped_latched_capture_words;
    uint32_t capture_latch_tick_hz;
    uint32_t capture_latch_resolution_ns;
    uint32_t capture_latch_write;
    uint32_t capture_latch_read;
    uint32_t capture_latch_seq;
    uint32_t capture_latch_previous_sample_mask;
    bool capture_latch_edge_state_valid;
    uint32_t capture_dma_read_seq;
    uint32_t capture_dma_produced_seq;
    uint32_t capture_dma_last_write_index;
    bool capture_dma_write_index_valid;
    uint64_t capture_timebase_start_ns;
    bool capture_timebase_valid;
    bool capture_timestamp_window_armed;
    bool capture_timestamp_window_periodic;
    uint64_t capture_timestamp_window_start_ns;
    uint64_t capture_timestamp_window_end_ns;
    uint32_t capture_timestamp_window_period_ns;
    uint32_t capture_timestamp_window_sample_period_ns;
    uint32_t capture_timestamp_window_observed_mask;
    uint32_t capture_timestamp_window_initial_sample_mask;
    uint32_t debug_model_output_enable_mask;
    uint32_t debug_model_output_value_mask;
    sync_io_aux_mode_t aux_modes[SYNC_IO_AUX_COUNT];
    sync_io_capture_latched_word_t capture_latch_ring[SYNC_IO_CAPTURE_LATCH_RING_SIZE];
} sync_io_context_t;

/* N5.0.4i: single definitions live in sync_io.c. */

/* capture DMA arena alias (single global definition in sync_io.c) */
#define s_sync_io_capture_dma_ring sync_io_shared_workspace

extern sync_io_context_t s_sync_io;
extern sync_io_sma_frequency_tx_status_t s_sma_frequency_tx;
extern const uint s_aux_pins[SYNC_IO_AUX_COUNT];
extern const uint s_aux_sms[SYNC_IO_AUX_COUNT];
extern uint32_t s_aux_last_snapshot;
extern uint32_t s_ready_last_snapshot;
extern uint32_t s_aux_wait_start_ms;
extern uint32_t s_aux_timeout_latched_mask;
extern uint32_t s_expected_ready_mask;
extern bool s_aux_trace_sample_valid;

void sync_io_trace(sync_io_trace_event_t event_id, uint8_t severity, uint32_t arg0, uint32_t arg1);
float sync_io_clkdiv_for_instruction_rate(uint32_t instruction_hz);
uint32_t sync_io_capture_dma_produced_words(void);
uint32_t sync_io_map_input_mask(uint32_t physical_mask);
bool sync_io_valid_aux_channel(sync_io_aux_channel_t channel);
bool sync_io_aux_mode_allowed(sync_io_aux_channel_t channel, sync_io_aux_mode_t mode);
bool sync_io_aux_resource_busy(void);
uint32_t sync_io_read_aux_level_mask(void);
uint32_t sync_io_read_aux_mode_mask(void);
uint32_t sync_io_read_ready_level_mask(void);
bool sync_io_debug_set_output_mask_owned(uint32_t mask);
void sync_io_debug_release_output_mask_owned(void);
void sync_io_sma_frequency_tx_stop_owned(void);
bool sync_io_sma_frequency_tx_start_owned( uint32_t output_channel, uint32_t frequency_hz, sync_io_sma_frequency_tx_status_t *status);
bool sync_io_sma_frequency_rx_measure_owned( uint32_t input_channel, uint32_t gate_us, sync_io_sma_frequency_rx_result_t *result);

#endif /* SYNC_IO_INTERNAL_H */
