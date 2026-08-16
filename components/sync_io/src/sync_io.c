#include "sync_io.h"

#include <assert.h>

#include "board_config.h"
#include "diagnostics.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "osal.h"
#include "resource_arbiter.h"
#include "biss_tap_rx.pio.h"
#include "sync_io_hw_profile.h"
#include "sync_io_core_internal.h"
#include "storage_manager.h"
#include "sync_io.pio.h"

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
#define SYNC_IO_CAPTURE_LATCH_RING_SIZE 32u
#define SYNC_IO_CAPTURE_DMA_RING_WORDS 2048u
#define SYNC_IO_CAPTURE_DMA_RING_BITS 13u
#define SYNC_IO_CAPTURE_DMA_RING_BYTES \
    (SYNC_IO_CAPTURE_DMA_RING_WORDS * sizeof(uint32_t))
#define SYNC_IO_CAPTURE_LATCH_SERVICE_MAX_WORDS 32u
#define SYNC_IO_CAPTURE_TIMESTAMP_SOURCE_HARDWARE_TICK 2u
#define SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY 0x00000001u
#define SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DPLL_ELIGIBLE   0x00000002u
#define SYNC_IO_CAPTURE_LATCH_TIMER timer1_hw

typedef struct {
    bool initialized;
    bool capture_running;
    bool clock_running;
    uint capture_offset;
    uint pulse_offset;
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
    uint32_t capture_dma_read_seq;
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

static sync_io_context_t s_sync_io;
static uint32_t s_sync_io_capture_dma_ring[SYNC_IO_CAPTURE_DMA_RING_WORDS]
    __attribute__((aligned(SYNC_IO_CAPTURE_DMA_RING_BYTES)));

void sync_io_core_trace(sync_io_trace_event_t event_id,
                        uint8_t severity,
                        uint32_t arg0,
                        uint32_t arg1)
{
    storage_manager_trace_event(SYNC_IO_TRACE_DOMAIN,
                                (uint16_t)event_id,
                                severity,
                                arg0,
                                arg1);
}

static void sync_io_trace(sync_io_trace_event_t event_id,
                          uint8_t severity,
                          uint32_t arg0,
                          uint32_t arg1)
{
    sync_io_core_trace(event_id, severity, arg0, arg1);
}

static uint32_t sync_io_pack_runtime_flags(bool running,
                                           bool pio_enabled,
                                           bool dma_busy,
                                           bool dma_irq_enabled,
                                           bool tx_fifo_empty,
                                           bool tx_fifo_full)
{
    return (running ? (1u << 0) : 0u) |
           (pio_enabled ? (1u << 1) : 0u) |
           (dma_busy ? (1u << 2) : 0u) |
           (dma_irq_enabled ? (1u << 3) : 0u) |
           (tx_fifo_empty ? (1u << 4) : 0u) |
           (tx_fifo_full ? (1u << 5) : 0u);
}

uint32_t sync_io_core_pack_runtime_flags(bool running,
                                         bool pio_enabled,
                                         bool dma_busy,
                                         bool dma_irq_enabled,
                                         bool tx_fifo_empty,
                                         bool tx_fifo_full)
{
    return sync_io_pack_runtime_flags(running,
                                      pio_enabled,
                                      dma_busy,
                                      dma_irq_enabled,
                                      tx_fifo_empty,
                                      tx_fifo_full);
}

static bool sync_io_sm_is_enabled(PIO pio, uint sm)
{
    return (pio->ctrl & (1u << sm)) != 0u;
}

bool sync_io_core_sm_is_enabled(PIO pio, uint sm)
{
    return sync_io_sm_is_enabled(pio, sm);
}

static uint32_t sync_io_pack_pio_state(uint sm,
                                       uint32_t offset,
                                       bool pio_enabled,
                                       bool tx_fifo_empty,
                                       bool tx_fifo_full)
{
    return (sm & 0xFFu) |
           ((offset & 0xFFu) << 8) |
           (pio_enabled ? (1u << 16) : 0u) |
           (tx_fifo_empty ? (1u << 17) : 0u) |
           (tx_fifo_full ? (1u << 18) : 0u);
}

uint32_t sync_io_core_pack_pio_state(uint sm,
                                     uint32_t offset,
                                     bool pio_enabled,
                                     bool tx_fifo_empty,
                                     bool tx_fifo_full)
{
    return sync_io_pack_pio_state(sm,
                                  offset,
                                  pio_enabled,
                                  tx_fifo_empty,
                                  tx_fifo_full);
}

static const uint s_aux_pins[SYNC_IO_AUX_COUNT] = {
    BOARD_SYNC_AUX0_PIN,
    BOARD_SYNC_AUX1_PIN,
    BOARD_SYNC_AUX2_PIN,
    BOARD_SYNC_AUX3_PIN,
};

static const uint s_aux_sms[SYNC_IO_AUX_COUNT] = {
    BOARD_SYNC_AUX0_SM,
    BOARD_SYNC_AUX1_SM,
    BOARD_SYNC_AUX2_SM,
    BOARD_SYNC_AUX3_SM,
};

static uint32_t s_aux_last_snapshot;
static uint32_t s_ready_last_snapshot;
static uint32_t s_aux_wait_start_ms;
static uint32_t s_aux_timeout_latched_mask;
static uint32_t s_expected_ready_mask;
static bool s_aux_trace_sample_valid;

static float sync_io_clkdiv_for_instruction_rate(uint32_t instruction_hz)
{
    if (instruction_hz < SYNC_IO_MIN_HZ) {
        instruction_hz = SYNC_IO_MIN_HZ;
    }

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)sys_hz / (float)instruction_hz;

    if (clkdiv < 1.0f) {
        clkdiv = 1.0f;
    }

    return clkdiv;
}

static uint32_t sync_io_capture_sample_period_ns(uint32_t sample_hz)
{
    if (sample_hz == 0u) {
        return 0u;
    }
    uint64_t period_ns = 1000000000ull / (uint64_t)sample_hz;
    if (period_ns == 0u) {
        period_ns = 1u;
    }
    return period_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)period_ns;
}

static uint32_t sync_io_capture_dma_produced_words(void)
{
    if (!s_sync_io.initialized) {
        return 0u;
    }

    const uint32_t remaining =
        dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].transfer_count;
    return UINT32_MAX - remaining;
}

static bool sync_io_capture_dma_configure(void)
{
    if (!dma_channel_is_claimed(SYNC_IO_CAPTURE_DMA_CH)) {
        return false;
    }

    dma_channel_abort(SYNC_IO_CAPTURE_DMA_CH);
    dma_channel_set_irq0_enabled(SYNC_IO_CAPTURE_DMA_CH, false);

    dma_channel_config dma_cfg =
        dma_channel_get_default_config(SYNC_IO_CAPTURE_DMA_CH);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_ring(&dma_cfg, true, SYNC_IO_CAPTURE_DMA_RING_BITS);
    channel_config_set_dreq(
        &dma_cfg,
        pio_get_dreq(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false));

    dma_channel_configure(
        SYNC_IO_CAPTURE_DMA_CH,
        &dma_cfg,
        s_sync_io_capture_dma_ring,
        &BOARD_SYNC_PIO_FAST->rxf[BOARD_SYNC_CAPTURE_SM],
        UINT32_MAX,
        false);
    return true;
}

static uint64_t sync_io_capture_latch_read_ticks64(void)
{
    uint32_t hi = SYNC_IO_CAPTURE_LATCH_TIMER->timerawh;
    uint32_t lo;
    do {
        lo = SYNC_IO_CAPTURE_LATCH_TIMER->timerawl;
        const uint32_t next_hi = SYNC_IO_CAPTURE_LATCH_TIMER->timerawh;
        if (hi == next_hi) {
            break;
        }
        hi = next_hi;
    } while (true);
    return ((uint64_t)hi << 32u) | lo;
}

static uint64_t sync_io_capture_latch_ticks_to_ns(uint64_t ticks)
{
    const uint32_t hz = s_sync_io.capture_latch_tick_hz != 0u
                            ? s_sync_io.capture_latch_tick_hz
                            : clock_get_hz(clk_sys);
    const uint64_t seconds = ticks / (uint64_t)hz;
    const uint64_t remainder = ticks % (uint64_t)hz;
    return seconds * 1000000000ull +
           (remainder * 1000000000ull) / (uint64_t)hz;
}

static uint32_t sync_io_capture_latch_resolution_ns(uint32_t tick_hz)
{
    if (tick_hz == 0u) {
        return 0u;
    }
    const uint64_t resolution =
        (1000000000ull + (uint64_t)tick_hz - 1ull) / (uint64_t)tick_hz;
    return resolution > UINT32_MAX ? UINT32_MAX : (uint32_t)resolution;
}

static void sync_io_capture_latch_timer_init(void)
{
    SYNC_IO_CAPTURE_LATCH_TIMER->pause = 1u;
    SYNC_IO_CAPTURE_LATCH_TIMER->source = TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS;
    SYNC_IO_CAPTURE_LATCH_TIMER->timelw = 0u;
    SYNC_IO_CAPTURE_LATCH_TIMER->timehw = 0u;
    SYNC_IO_CAPTURE_LATCH_TIMER->pause = 0u;

    s_sync_io.capture_latch_tick_hz = clock_get_hz(clk_sys);
    s_sync_io.capture_latch_resolution_ns =
        sync_io_capture_latch_resolution_ns(s_sync_io.capture_latch_tick_hz);
}

static void sync_io_capture_latch_reset_locked(void)
{
    s_sync_io.capture_latch_write = 0u;
    s_sync_io.capture_latch_read = 0u;
    s_sync_io.capture_latch_seq = 0u;
    s_sync_io.latched_capture_words = 0u;
    s_sync_io.dropped_latched_capture_words = 0u;
}

static bool sync_io_capture_latch_word_in_window(uint64_t word_start_ns,
                                                 uint64_t word_end_ns,
                                                 uint64_t *matched_window_start_ns)
{
    uint64_t window_start_ns = s_sync_io.capture_timestamp_window_start_ns;
    uint64_t window_end_ns = s_sync_io.capture_timestamp_window_end_ns;

    if (s_sync_io.capture_timestamp_window_periodic) {
        const uint32_t period_ns = s_sync_io.capture_timestamp_window_period_ns;
        const uint32_t width_ns =
            (uint32_t)(s_sync_io.capture_timestamp_window_end_ns -
                       s_sync_io.capture_timestamp_window_start_ns);
        if (period_ns == 0u || width_ns == 0u) {
            return false;
        }

        uint64_t cycle = 0u;
        if (word_start_ns > s_sync_io.capture_timestamp_window_start_ns) {
            cycle =
                (word_start_ns - s_sync_io.capture_timestamp_window_start_ns) /
                (uint64_t)period_ns;
        }
        window_start_ns =
            s_sync_io.capture_timestamp_window_start_ns +
            cycle * (uint64_t)period_ns;
        window_end_ns = window_start_ns + (uint64_t)width_ns;
        if (word_start_ns > window_end_ns) {
            window_start_ns += period_ns;
            window_end_ns += period_ns;
        }
    }

    if (word_start_ns < window_start_ns || word_end_ns > window_end_ns) {
        return false;
    }

    if (matched_window_start_ns != NULL) {
        *matched_window_start_ns = window_start_ns;
    }
    return true;
}

static bool sync_io_capture_latch_next_window_start(uint64_t word_start_ns,
                                                    uint64_t word_end_ns,
                                                    uint64_t *window_start_ns,
                                                    uint64_t *window_end_ns)
{
    uint64_t candidate_start = s_sync_io.capture_timestamp_window_start_ns;
    uint64_t candidate_end = s_sync_io.capture_timestamp_window_end_ns;

    if (!s_sync_io.capture_timestamp_window_armed) {
        return false;
    }

    if (s_sync_io.capture_timestamp_window_periodic) {
        const uint32_t period_ns =
            s_sync_io.capture_timestamp_window_period_ns;
        const uint64_t width_ns =
            s_sync_io.capture_timestamp_window_end_ns -
            s_sync_io.capture_timestamp_window_start_ns;
        if (period_ns == 0u || width_ns == 0u) {
            return false;
        }

        if (word_start_ns > candidate_start) {
            candidate_start +=
                ((word_start_ns - candidate_start) / (uint64_t)period_ns) *
                (uint64_t)period_ns;
            candidate_end = candidate_start + width_ns;
            if (word_start_ns > candidate_end ||
                word_end_ns > candidate_end) {
                candidate_start += (uint64_t)period_ns;
                candidate_end += (uint64_t)period_ns;
            }
        }
    }

    if (word_start_ns < candidate_start) {
        /* The current word precedes the first eligible window. */
    } else if (word_start_ns >= candidate_start &&
               word_end_ns <= candidate_end) {
        return false;
    } else if (!s_sync_io.capture_timestamp_window_periodic) {
        return false;
    } else {
        candidate_start +=
            (uint64_t)s_sync_io.capture_timestamp_window_period_ns;
        candidate_end +=
            (uint64_t)s_sync_io.capture_timestamp_window_period_ns;
    }

    if (window_start_ns != NULL) {
        *window_start_ns = candidate_start;
    }
    if (window_end_ns != NULL) {
        *window_end_ns = candidate_end;
    }
    return true;
}

static uint32_t sync_io_capture_latch_flags_for_word(uint64_t word_start_ns,
                                                     uint64_t word_end_ns,
                                                     uint32_t sample_period_ns,
                                                     uint64_t *matched_window_start_ns)
{
    uint32_t flags = SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    const uint64_t word_span_ns =
        word_end_ns > word_start_ns ? word_end_ns - word_start_ns : 0ull;

    if (matched_window_start_ns != NULL) {
        *matched_window_start_ns = 0u;
    }

    if (!s_sync_io.capture_timebase_valid ||
        !s_sync_io.capture_timestamp_window_armed ||
        sample_period_ns == 0u ||
        word_span_ns == 0u ||
        !sync_io_capture_latch_word_in_window(word_start_ns,
                                              word_end_ns,
                                              matched_window_start_ns) ||
        sample_period_ns != s_sync_io.capture_timestamp_window_sample_period_ns ||
        s_sync_io.capture_timestamp_window_observed_mask == 0u) {
        return flags;
    }

    flags &= ~SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    flags |= SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    if (!s_sync_io.capture_timestamp_window_periodic) {
        s_sync_io.capture_timestamp_window_armed = false;
    }
    return flags;
}

static void sync_io_capture_latch_discard_outside_window(
    uint32_t produced,
    uint32_t word_span_ns)
{
    if (!s_sync_io.capture_timestamp_window_armed ||
        !s_sync_io.capture_timebase_valid ||
        word_span_ns == 0u) {
        return;
    }

    osal_critical_enter();
    uint32_t read_seq = s_sync_io.capture_dma_read_seq;
    uint32_t available = produced - read_seq;
    if (available > SYNC_IO_CAPTURE_DMA_RING_WORDS) {
        s_sync_io.dropped_capture_words +=
            available - SYNC_IO_CAPTURE_DMA_RING_WORDS;
        s_sync_io.capture_timebase_valid = false;
        read_seq = produced - SYNC_IO_CAPTURE_DMA_RING_WORDS;
        s_sync_io.capture_dma_read_seq = read_seq;
        available = SYNC_IO_CAPTURE_DMA_RING_WORDS;
    }

    if (available != 0u) {
        const uint64_t capture_start_ns = s_sync_io.capture_timebase_start_ns;
        const uint64_t word_start_ns =
            capture_start_ns + (uint64_t)read_seq * word_span_ns;
        const uint64_t word_end_ns = word_start_ns + word_span_ns;
        uint64_t next_window_start_ns = 0u;
        uint64_t next_window_end_ns = 0u;

        if (sync_io_capture_latch_word_in_window(word_start_ns,
                                                 word_end_ns,
                                                 NULL)) {
            osal_critical_exit();
            return;
        }

        if (sync_io_capture_latch_next_window_start(
                word_start_ns,
                word_end_ns,
                &next_window_start_ns,
                &next_window_end_ns)) {
            (void)next_window_end_ns;
            const uint64_t delta_ns =
                next_window_start_ns > capture_start_ns
                    ? next_window_start_ns - capture_start_ns
                    : 0u;
            uint64_t target_seq =
                (delta_ns + word_span_ns - 1u) / word_span_ns;
            if (target_seq < read_seq) {
                target_seq = read_seq;
            }
            if (target_seq > produced) {
                target_seq = produced;
            }
            s_sync_io.capture_dma_read_seq = (uint32_t)target_seq;
        } else {
            /* A one-shot window has expired; no diagnostic backlog is useful. */
            s_sync_io.capture_dma_read_seq = produced;
        }
    }
    osal_critical_exit();
}

static bool sync_io_claim_sm(PIO pio, uint sm, const char *name)
{
    if (pio_sm_is_claimed(pio, sm)) {
        LOG_ERROR("sync_io", "%s state machine already claimed", name);
        return false;
    }

    pio_sm_claim(pio, sm);
    return true;
}

static void sync_io_configure_static_inputs(void)
{
    gpio_pull_down(BOARD_SYNC_TRIG_IN_PIN);
    gpio_pull_down(BOARD_SYNC_ARM_IN_PIN);
    gpio_pull_down(BOARD_SYNC_EXT_CLK_IN_PIN);
    gpio_pull_down(BOARD_SYNC_GATE_IN_PIN);
}

static bool sync_io_valid_aux_channel(sync_io_aux_channel_t channel)
{
    return sync_io_hw_aux_channel_valid((uint32_t)channel);
}

static bool sync_io_aux_mode_allowed(sync_io_aux_channel_t channel,
                                     sync_io_aux_mode_t mode)
{
    const uint32_t index = (uint32_t)channel;
    if (mode == SYNC_IO_AUX_MODE_INPUT) {
        return sync_io_hw_aux_supports_input(index);
    }
    if (mode == SYNC_IO_AUX_MODE_PIO_OUTPUT) {
        return sync_io_hw_aux_supports_output(index);
    }
    return false;
}

static bool sync_io_aux_resource_busy(void)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    return (snapshot.active_resources & RESOURCE_ARBITER_RESOURCE_AUX) != 0u;
}

bool sync_io_core_initialized(void)
{
    return s_sync_io.initialized;
}

uint sync_io_core_biss_tap_offset(void)
{
    return s_sync_io.biss_tap_offset;
}

uint sync_io_core_aux_passthrough_offset(void)
{
    return s_sync_io.aux_passthrough_offset;
}

void sync_io_core_set_aux_mode(sync_io_aux_channel_t channel,
                               sync_io_aux_mode_t mode)
{
    if (sync_io_valid_aux_channel(channel)) {
        s_sync_io.aux_modes[(uint)channel] = mode;
    }
}

void sync_io_core_restore_aux_channel_input(uint pin)
{
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);

    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        if (s_aux_pins[channel] == pin) {
            s_sync_io.aux_modes[channel] = SYNC_IO_AUX_MODE_INPUT;
        }
    }
}

void sync_io_core_mark_aux_channel_input(uint pin)
{
    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        if (s_aux_pins[channel] == pin) {
            s_sync_io.aux_modes[channel] = SYNC_IO_AUX_MODE_INPUT;
        }
    }
}

static uint32_t sync_io_read_aux_level_mask(void)
{
    uint32_t mask = 0u;
    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        if (gpio_get(s_aux_pins[channel])) {
            mask |= (1u << channel);
        }
    }
    return mask;
}

static uint32_t sync_io_read_aux_mode_mask(void)
{
    uint32_t mask = 0u;
    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        if (s_sync_io.aux_modes[channel] == SYNC_IO_AUX_MODE_PIO_OUTPUT) {
            mask |= (1u << channel);
        }
    }
    return mask;
}

static uint32_t sync_io_read_ready_level_mask(void)
{
    return (gpio_get(BOARD_SYNC_TRIG_IN_PIN) ? (1u << 0) : 0u) |
           (gpio_get(BOARD_SYNC_ARM_IN_PIN) ? (1u << 1) : 0u) |
           (gpio_get(BOARD_SYNC_EXT_CLK_IN_PIN) ? (1u << 2) : 0u) |
           (gpio_get(BOARD_SYNC_GATE_IN_PIN) ? (1u << 3) : 0u) |
           (gpio_get(BOARD_SYNC_AUX_ARM_IN_PIN) ? (1u << 4) : 0u) |
           (gpio_get(BOARD_SYNC_AUX_EXT_CLK_IN_PIN) ? (1u << 5) : 0u) |
           (gpio_get(BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN) ? (1u << 6) : 0u) |
           (gpio_get(BOARD_SYNC_AUX3_OUT_PIN) ? (1u << 7) : 0u);
}

bool sync_io_init(const sync_io_config_t *config)
{
    if (s_sync_io.initialized) {
        return true;
    }

    const uint32_t capture_hz = (config != NULL && config->capture_sample_hz != 0u)
                                    ? config->capture_sample_hz
                                    : SYNC_IO_DEFAULT_CAPTURE_HZ;
    const uint32_t clock_hz = (config != NULL && config->sync_clock_hz != 0u)
                                  ? config->sync_clock_hz
                                  : SYNC_IO_DEFAULT_CLOCK_HZ;

    if (!pio_can_add_program(BOARD_SYNC_PIO_FAST, &sync_capture_4bit_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_WAVE, &sync_pulse_program) ||
        !pio_can_add_program(SYNC_IO_CLOCK_PIO, &sync_clock_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_AUX, &sync_passthrough_1bit_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_AUX, &biss_tap_rx_program)) {
        LOG_ERROR("sync_io", "not enough PIO instruction memory");
        sync_io_trace(SYNC_IO_TRACE_INIT_FAIL, SYNC_IO_TRACE_ERROR, 1u, 0u);
        return false;
    }

    if (!sync_io_claim_sm(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, "capture") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, "output") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_MODEL_SCHED_SM, "model_sched") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_GATE_SM, "pulse") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_RJ45_TRIGGER_SM, "rj45_trigger") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX0_SM, "aux0") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX1_SM, "aux1") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM, "aux2") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM, "aux3")) {
        sync_io_trace(SYNC_IO_TRACE_INIT_FAIL, SYNC_IO_TRACE_ERROR, 2u, 0u);
        return false;
    }

    if (dma_channel_is_claimed(SYNC_IO_CAPTURE_DMA_CH)) {
        LOG_ERROR("sync_io", "capture DMA channel already claimed");
        sync_io_trace(SYNC_IO_TRACE_INIT_FAIL,
                      SYNC_IO_TRACE_ERROR,
                      3u,
                      SYNC_IO_CAPTURE_DMA_CH);
        return false;
    }
    dma_channel_claim(SYNC_IO_CAPTURE_DMA_CH);

    s_sync_io.capture_offset = (uint)pio_add_program(BOARD_SYNC_PIO_FAST, &sync_capture_4bit_program);
    s_sync_io.pulse_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, &sync_pulse_program);
    s_sync_io.clock_offset = (uint)pio_add_program(SYNC_IO_CLOCK_PIO, &sync_clock_program);
    s_sync_io.aux_offset = (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program);
    s_sync_io.aux_passthrough_offset =
        (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &sync_passthrough_1bit_program);
    s_sync_io.biss_tap_offset = (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &biss_tap_rx_program);

    sync_io_configure_static_inputs();
    sync_io_capture_latch_timer_init();

    sync_capture_4bit_program_init(BOARD_SYNC_PIO_FAST,
                                   BOARD_SYNC_CAPTURE_SM,
                                   s_sync_io.capture_offset,
                                   BOARD_SYNC_INPUT_BASE_PIN,
                                   BOARD_SYNC_INPUT_PIN_COUNT,
                                   sync_io_clkdiv_for_instruction_rate(capture_hz));

    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_OUTPUT_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_TRIG_OUT_PIN);

    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_GATE_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_PULSE_OUT_PIN);

    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_RJ45_TRIGGER_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_RJ45_TRIG_OUT_PIN);

    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        sync_aux_output_program_init(BOARD_SYNC_PIO_AUX,
                                     s_aux_sms[channel],
                                     s_sync_io.aux_offset,
                                     s_aux_pins[channel]);
        pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, s_aux_sms[channel], false);
        gpio_set_function(s_aux_pins[channel], GPIO_FUNC_SIO);
        gpio_set_dir(s_aux_pins[channel], GPIO_IN);
        gpio_pull_down(s_aux_pins[channel]);
        s_sync_io.aux_modes[channel] = SYNC_IO_AUX_MODE_INPUT;
    }

    sync_clock_program_init(SYNC_IO_CLOCK_PIO,
                            SYNC_IO_CLOCK_SM,
                            s_sync_io.clock_offset,
                            SYNC_IO_CLOCK_PIN,
                            sync_io_clkdiv_for_instruction_rate(clock_hz * 2u));
    sync_io_core_restore_aux_channel_input(SYNC_IO_CLOCK_PIN);

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_GATE_SM, true);
    pio_sm_set_enabled(SYNC_IO_CLOCK_PIO, SYNC_IO_CLOCK_SM, false);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_RJ45_TRIGGER_SM, true);

    s_sync_io.capture_sample_hz = capture_hz;
    s_sync_io.sync_clock_hz = clock_hz;
    s_sync_io.initialized = true;

    LOG_INFO("sync_io", "initialized capture=%luHz clock=%luHz",
             (unsigned long)capture_hz,
             (unsigned long)clock_hz);
    sync_io_trace(SYNC_IO_TRACE_INIT_OK,
                  SYNC_IO_TRACE_INFO,
                  capture_hz,
                  clock_hz);

    return true;
}

bool sync_io_start_capture(uint32_t sample_hz)
{
    if (!s_sync_io.initialized) {
        sync_io_trace(SYNC_IO_TRACE_CAPTURE_FAIL, SYNC_IO_TRACE_ERROR, sample_hz, 1u);
        return false;
    }

    if (sample_hz == 0u) {
        sample_hz = s_sync_io.capture_sample_hz;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    pio_sm_set_clkdiv(BOARD_SYNC_PIO_FAST,
                      BOARD_SYNC_CAPTURE_SM,
                      sync_io_clkdiv_for_instruction_rate(sample_hz));
    pio_sm_restart(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    if (!sync_io_capture_dma_configure()) {
        sync_io_trace(SYNC_IO_TRACE_CAPTURE_FAIL,
                      SYNC_IO_TRACE_ERROR,
                      sample_hz,
                      2u);
        return false;
    }

    const uint64_t capture_start_ns =
        sync_io_capture_latch_ticks_to_ns(sync_io_capture_latch_read_ticks64());
    osal_critical_enter();
    sync_io_capture_latch_reset_locked();
    s_sync_io.capture_dma_read_seq = 0u;
    s_sync_io.capture_timebase_start_ns = capture_start_ns;
    s_sync_io.capture_timebase_valid = true;
    osal_critical_exit();

    s_sync_io.capture_sample_hz = sample_hz;
    s_sync_io.capture_running = true;
    dma_start_channel_mask(1u << SYNC_IO_CAPTURE_DMA_CH);
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, true);

    sync_io_trace(SYNC_IO_TRACE_CAPTURE_START, SYNC_IO_TRACE_INFO, sample_hz, 0u);
    return true;
}

void sync_io_stop_capture(void)
{
    if (!s_sync_io.initialized) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    dma_channel_abort(SYNC_IO_CAPTURE_DMA_CH);
    osal_critical_enter();
    s_sync_io.capture_running = false;
    s_sync_io.capture_timebase_valid = false;
    osal_critical_exit();
    sync_io_trace(SYNC_IO_TRACE_CAPTURE_STOP,
                  SYNC_IO_TRACE_INFO,
                  s_sync_io.capture_sample_hz,
                  s_sync_io.dropped_capture_words);
}

static bool sync_io_capture_dma_pop(uint32_t *raw_word,
                                    uint32_t *capture_word_seq)
{
    if (raw_word == NULL || capture_word_seq == NULL ||
        !s_sync_io.capture_running) {
        return false;
    }

    const uint32_t produced = sync_io_capture_dma_produced_words();
    bool overflowed = false;
    uint32_t overflow_count = 0u;

    osal_critical_enter();
    uint32_t read_seq = s_sync_io.capture_dma_read_seq;
    uint32_t available = produced - read_seq;
    if (available > SYNC_IO_CAPTURE_DMA_RING_WORDS) {
        overflow_count = available - SYNC_IO_CAPTURE_DMA_RING_WORDS;
        s_sync_io.dropped_capture_words += overflow_count;
        s_sync_io.capture_timebase_valid = false;
        read_seq = produced - SYNC_IO_CAPTURE_DMA_RING_WORDS;
        s_sync_io.capture_dma_read_seq = read_seq;
        available = SYNC_IO_CAPTURE_DMA_RING_WORDS;
        overflowed = true;
    }
    if (available == 0u) {
        osal_critical_exit();
        return false;
    }

    *capture_word_seq = read_seq;
    *raw_word =
        s_sync_io_capture_dma_ring[read_seq & (SYNC_IO_CAPTURE_DMA_RING_WORDS - 1u)];
    s_sync_io.capture_dma_read_seq = read_seq + 1u;
    osal_critical_exit();

    if (overflowed) {
        sync_io_trace(SYNC_IO_TRACE_CAPTURE_DROP,
                      SYNC_IO_TRACE_WARN,
                      s_sync_io.dropped_capture_words,
                      overflow_count);
    }
    return true;
}

size_t sync_io_read_capture_words(uint32_t *buffer, size_t max_words)
{
    if (!s_sync_io.initialized || buffer == NULL || max_words == 0u) {
        return 0u;
    }

    size_t count = 0u;
    while (count < max_words) {
        uint32_t capture_word_seq = 0u;
        if (!sync_io_capture_dma_pop(&buffer[count], &capture_word_seq)) {
            break;
        }
        (void)capture_word_seq;
        count++;
    }

    return count;
}

void sync_io_capture_latch_service_core1(void)
{
    if (!s_sync_io.initialized || !s_sync_io.capture_running) {
        return;
    }

    const uint32_t sample_period_ns =
        sync_io_capture_sample_period_ns(s_sync_io.capture_sample_hz);
    const uint32_t word_span_ns = sample_period_ns * 8u;
    const uint32_t produced = sync_io_capture_dma_produced_words();

    sync_io_capture_latch_discard_outside_window(produced, word_span_ns);

    for (uint32_t processed = 0u;
         processed < SYNC_IO_CAPTURE_LATCH_SERVICE_MAX_WORDS;
         processed++) {
        uint32_t raw_word = 0u;
        uint32_t capture_word_seq = 0u;
        if (!sync_io_capture_dma_pop(&raw_word, &capture_word_seq)) {
            break;
        }

        osal_critical_enter();
        const uint64_t capture_start_ns = s_sync_io.capture_timebase_start_ns;
        osal_critical_exit();
        const uint64_t word_start_ns =
            capture_start_ns + (uint64_t)capture_word_seq * word_span_ns;
        const uint64_t word_end_ns = word_start_ns + word_span_ns;
        const uint32_t base_time_l32_ns =
            (uint32_t)word_start_ns;
        uint64_t matched_window_start_ns = 0u;

        osal_critical_enter();
        const uint32_t next_write =
            (s_sync_io.capture_latch_write + 1u) % SYNC_IO_CAPTURE_LATCH_RING_SIZE;
        if (next_write == s_sync_io.capture_latch_read) {
            s_sync_io.dropped_latched_capture_words++;
            osal_critical_exit();
            continue;
        }

        sync_io_capture_latched_word_t *slot =
            &s_sync_io.capture_latch_ring[s_sync_io.capture_latch_write];
        slot->raw_word = raw_word;
        slot->sample_seq = ++s_sync_io.capture_latch_seq;
        slot->base_time_l32_ns = base_time_l32_ns;
        slot->sample_period_ns = sample_period_ns;
        slot->timestamp_source = SYNC_IO_CAPTURE_TIMESTAMP_SOURCE_HARDWARE_TICK;
        slot->timestamp_resolution_ns =
            sample_period_ns > s_sync_io.capture_latch_resolution_ns
                ? sample_period_ns
                : s_sync_io.capture_latch_resolution_ns;
        slot->timestamp_flags =
            sync_io_capture_latch_flags_for_word(word_start_ns,
                                                 word_end_ns,
                                                 sample_period_ns,
                                                 &matched_window_start_ns);
        slot->matched_window_start_ns = matched_window_start_ns;
        slot->dropped_before = s_sync_io.dropped_latched_capture_words;
        s_sync_io.capture_latch_write = next_write;
        s_sync_io.latched_capture_words++;
        osal_critical_exit();
    }
}

size_t sync_io_read_capture_latched(sync_io_capture_latched_word_t *buffer,
                                    size_t max_words)
{
    if (!s_sync_io.initialized || buffer == NULL || max_words == 0u) {
        return 0u;
    }

    size_t count = 0u;
    osal_critical_enter();
    while (count < max_words &&
           s_sync_io.capture_latch_read != s_sync_io.capture_latch_write) {
        buffer[count] =
            s_sync_io.capture_latch_ring[s_sync_io.capture_latch_read];
        s_sync_io.capture_latch_read =
            (s_sync_io.capture_latch_read + 1u) % SYNC_IO_CAPTURE_LATCH_RING_SIZE;
        count++;
    }
    osal_critical_exit();
    return count;
}

bool sync_io_capture_time_now_ns(uint64_t *now_ns)
{
    if (!s_sync_io.initialized || now_ns == NULL) {
        return false;
    }

    *now_ns =
        sync_io_capture_latch_ticks_to_ns(sync_io_capture_latch_read_ticks64());
    return true;
}

bool sync_io_capture_arm_timestamp_window(uint64_t window_start_ns,
                                          uint32_t window_width_ns,
                                          uint32_t sample_period_ns,
                                          uint32_t observed_mask,
                                          uint32_t initial_sample_mask)
{
    return sync_io_capture_arm_periodic_timestamp_window(window_start_ns,
                                                        window_width_ns,
                                                        0u,
                                                        sample_period_ns,
                                                        observed_mask,
                                                        initial_sample_mask);
}

bool sync_io_capture_arm_periodic_timestamp_window(uint64_t window_start_ns,
                                                   uint32_t window_width_ns,
                                                   uint32_t period_ns,
                                                   uint32_t sample_period_ns,
                                                   uint32_t observed_mask,
                                                   uint32_t initial_sample_mask)
{
    if (!s_sync_io.initialized ||
        window_width_ns == 0u ||
        sample_period_ns == 0u ||
        observed_mask == 0u ||
        (period_ns != 0u && period_ns <= window_width_ns) ||
        (observed_mask & ~0x0Fu) != 0u ||
        (initial_sample_mask & ~0x0Fu) != 0u) {
        return false;
    }

    osal_critical_enter();
    s_sync_io.capture_timestamp_window_armed = true;
    s_sync_io.capture_timestamp_window_periodic = period_ns != 0u;
    s_sync_io.capture_timestamp_window_start_ns = window_start_ns;
    s_sync_io.capture_timestamp_window_end_ns =
        window_start_ns + (uint64_t)window_width_ns;
    s_sync_io.capture_timestamp_window_period_ns = period_ns;
    s_sync_io.capture_timestamp_window_sample_period_ns = sample_period_ns;
    s_sync_io.capture_timestamp_window_observed_mask = observed_mask & 0x0Fu;
    s_sync_io.capture_timestamp_window_initial_sample_mask =
        initial_sample_mask & observed_mask & 0x0Fu;
    osal_critical_exit();
    return true;
}

void sync_io_capture_disarm_timestamp_window(void)
{
    osal_critical_enter();
    s_sync_io.capture_timestamp_window_armed = false;
    s_sync_io.capture_timestamp_window_periodic = false;
    s_sync_io.capture_timestamp_window_start_ns = 0u;
    s_sync_io.capture_timestamp_window_end_ns = 0u;
    s_sync_io.capture_timestamp_window_period_ns = 0u;
    s_sync_io.capture_timestamp_window_sample_period_ns = 0u;
    s_sync_io.capture_timestamp_window_observed_mask = 0u;
    s_sync_io.capture_timestamp_window_initial_sample_mask = 0u;
    osal_critical_exit();
}

static bool sync_io_fire_pulse_on_sm(uint sm, uint32_t high_cycles)
{
    if (!s_sync_io.initialized || high_cycles == 0u) {
        sync_io_trace(SYNC_IO_TRACE_PULSE_INVALID,
                      SYNC_IO_TRACE_WARN,
                      sm,
                      high_cycles);
        return false;
    }

    if (pio_sm_is_tx_fifo_full(BOARD_SYNC_PIO_WAVE, sm)) {
        sync_io_trace(SYNC_IO_TRACE_PULSE_FIFO_FULL,
                      SYNC_IO_TRACE_WARN,
                      sm,
                      high_cycles);
        return false;
    }

    pio_sm_put(BOARD_SYNC_PIO_WAVE, sm, high_cycles - 1u);
    return true;
}

static bool sync_io_fire_pulse_us_on_sm(uint sm, uint32_t high_us)
{
    if (high_us == 0u) {
        return false;
    }

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    uint64_t cycles = ((uint64_t)sys_hz * (uint64_t)high_us) / 1000000ull;
    if (cycles == 0u) {
        cycles = 1u;
    }
    if (cycles > UINT32_MAX) {
        cycles = UINT32_MAX;
    }

    return sync_io_fire_pulse_on_sm(sm, (uint32_t)cycles);
}

bool sync_io_fire_pulse_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_on_sm(BOARD_SYNC_OUTPUT_SM, high_cycles);
}

bool sync_io_fire_pulse_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_sm(BOARD_SYNC_OUTPUT_SM, high_us);
}

bool sync_io_fire_pulse_out_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_on_sm(BOARD_SYNC_GATE_SM, high_cycles);
}

bool sync_io_fire_pulse_out_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_sm(BOARD_SYNC_GATE_SM, high_us);
}

bool sync_io_start_clock(uint32_t frequency_hz)
{
    if (!s_sync_io.initialized || frequency_hz == 0u) {
        sync_io_trace(SYNC_IO_TRACE_CLOCK_FAIL,
                      SYNC_IO_TRACE_WARN,
                      frequency_hz,
                      s_sync_io.initialized ? 0u : 1u);
        return false;
    }

    if (!s_sync_io.clock_running &&
        !resource_arbiter_acquire_owned(SYNC_IO_CLOCK_RESOURCES, SYNC_IO_CLOCK_OWNER)) {
        sync_io_trace(SYNC_IO_TRACE_CLOCK_FAIL,
                      SYNC_IO_TRACE_WARN,
                      frequency_hz,
                      SYNC_IO_HW_SYNC_CLK_OUT_PIN);
        return false;
    }

    pio_sm_set_enabled(SYNC_IO_CLOCK_PIO, SYNC_IO_CLOCK_SM, false);
    sync_clock_program_init(SYNC_IO_CLOCK_PIO,
                            SYNC_IO_CLOCK_SM,
                            s_sync_io.clock_offset,
                            SYNC_IO_CLOCK_PIN,
                            sync_io_clkdiv_for_instruction_rate(frequency_hz * 2u));
    pio_sm_set_clkdiv(SYNC_IO_CLOCK_PIO,
                      SYNC_IO_CLOCK_SM,
                      sync_io_clkdiv_for_instruction_rate(frequency_hz * 2u));
    pio_sm_restart(SYNC_IO_CLOCK_PIO, SYNC_IO_CLOCK_SM);
    pio_sm_set_enabled(SYNC_IO_CLOCK_PIO, SYNC_IO_CLOCK_SM, true);

    s_sync_io.sync_clock_hz = frequency_hz;
    s_sync_io.clock_running = true;
    s_sync_io.aux_modes[(uint)SYNC_IO_AUX2] = SYNC_IO_AUX_MODE_PIO_OUTPUT;
    sync_io_trace(SYNC_IO_TRACE_CLOCK_START,
                  SYNC_IO_TRACE_INFO,
                  frequency_hz,
                  SYNC_IO_HW_SYNC_CLK_OUT_PIN);
    return true;
}

void sync_io_stop_clock(void)
{
    if (!s_sync_io.initialized) {
        return;
    }
    if (!s_sync_io.clock_running) {
        return;
    }

    pio_sm_set_enabled(SYNC_IO_CLOCK_PIO, SYNC_IO_CLOCK_SM, false);
    pio_sm_set_pins(SYNC_IO_CLOCK_PIO, SYNC_IO_CLOCK_SM, 0);
    sync_io_core_restore_aux_channel_input(SYNC_IO_CLOCK_PIN);
    s_sync_io.clock_running = false;
    resource_arbiter_release_owned(SYNC_IO_CLOCK_RESOURCES, SYNC_IO_CLOCK_OWNER);
    sync_io_trace(SYNC_IO_TRACE_CLOCK_STOP,
                  SYNC_IO_TRACE_INFO,
                  s_sync_io.sync_clock_hz,
                  SYNC_IO_HW_SYNC_CLK_OUT_PIN);
}

bool sync_io_fire_marker_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_on_sm(BOARD_SYNC_RJ45_TRIGGER_SM, high_cycles);
}

bool sync_io_fire_marker_us(uint32_t high_us)
{
    return sync_io_fire_rj45_trigger_us(high_us);
}

bool sync_io_fire_rj45_trigger_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_sm(BOARD_SYNC_RJ45_TRIGGER_SM, high_us);
}

bool sync_io_debug_set_output_mask(uint32_t mask)
{
    if (!s_sync_io.initialized) {
        return false;
    }

    const uint32_t valid_mask = (1u << BOARD_SYNC_OUTPUT_PIN_COUNT) - 1u;
    mask &= valid_mask;

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, false);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_GATE_SM, false);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_RJ45_TRIGGER_SM, false);

    for (uint pin = 0u; pin < BOARD_SYNC_OUTPUT_PIN_COUNT; pin++) {
        const uint gpio = BOARD_SYNC_OUTPUT_BASE_PIN + pin;
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, (mask & (1u << pin)) != 0u);
    }

    return true;
}

void sync_io_debug_release_output_mask(void)
{
    if (!s_sync_io.initialized) {
        return;
    }

    for (uint pin = 0u; pin < BOARD_SYNC_OUTPUT_PIN_COUNT; pin++) {
        const uint gpio = BOARD_SYNC_OUTPUT_BASE_PIN + pin;
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, false);
    }

    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_OUTPUT_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_TRIG_OUT_PIN);
    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_GATE_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_PULSE_OUT_PIN);
    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_RJ45_TRIGGER_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_RJ45_TRIG_OUT_PIN);

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_GATE_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_RJ45_TRIGGER_SM, true);
}

uint32_t sync_io_debug_read_input_mask(void)
{
    uint32_t mask = 0u;
    for (uint pin = 0u; pin < BOARD_SYNC_INPUT_PIN_COUNT; pin++) {
        if (gpio_get(BOARD_SYNC_INPUT_BASE_PIN + pin)) {
            mask |= (1u << pin);
        }
    }
    return mask;
}

bool sync_io_debug_model_set_output_mask(uint32_t enable_mask, uint32_t value_mask)
{
    if (!s_sync_io.initialized) {
        return false;
    }

    const uint32_t valid_mask = (1u << BOARD_DEBUG_MODEL_GPIO_PIN_COUNT) - 1u;
    if ((enable_mask & ~valid_mask) != 0u || (value_mask & ~valid_mask) != 0u) {
        return false;
    }

    for (uint pin = 0u; pin < BOARD_DEBUG_MODEL_GPIO_PIN_COUNT; pin++) {
        const uint gpio = BOARD_DEBUG_MODEL_GPIO_BASE_PIN + pin;
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        if ((enable_mask & (1u << pin)) != 0u) {
            gpio_set_dir(gpio, GPIO_OUT);
            gpio_put(gpio, (value_mask & (1u << pin)) != 0u);
        } else {
            gpio_set_dir(gpio, GPIO_IN);
            gpio_pull_down(gpio);
        }
    }

    s_sync_io.debug_model_output_enable_mask = enable_mask;
    s_sync_io.debug_model_output_value_mask = value_mask & enable_mask;
    return true;
}

bool sync_io_debug_model_write_pin(uint32_t pin_index, bool enable, bool value)
{
    if (!s_sync_io.initialized || pin_index >= BOARD_DEBUG_MODEL_GPIO_PIN_COUNT) {
        return false;
    }

    uint32_t enable_mask = s_sync_io.debug_model_output_enable_mask;
    uint32_t value_mask = s_sync_io.debug_model_output_value_mask;
    const uint32_t bit = 1u << pin_index;
    if (enable) {
        enable_mask |= bit;
        if (value) {
            value_mask |= bit;
        } else {
            value_mask &= ~bit;
        }
    } else {
        enable_mask &= ~bit;
        value_mask &= ~bit;
    }

    return sync_io_debug_model_set_output_mask(enable_mask, value_mask);
}

void sync_io_debug_model_release(void)
{
    if (!s_sync_io.initialized) {
        return;
    }

    for (uint pin = 0u; pin < BOARD_DEBUG_MODEL_GPIO_PIN_COUNT; pin++) {
        const uint gpio = BOARD_DEBUG_MODEL_GPIO_BASE_PIN + pin;
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_put(gpio, false);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_pull_down(gpio);
    }

    s_sync_io.debug_model_output_enable_mask = 0u;
    s_sync_io.debug_model_output_value_mask = 0u;
}

uint32_t sync_io_debug_model_read_input_mask(void)
{
    uint32_t mask = 0u;
    for (uint pin = 0u; pin < BOARD_DEBUG_MODEL_GPIO_PIN_COUNT; pin++) {
        if (gpio_get(BOARD_DEBUG_MODEL_GPIO_BASE_PIN + pin)) {
            mask |= (1u << pin);
        }
    }
    return mask;
}

uint32_t sync_io_debug_model_get_output_enable_mask(void)
{
    return s_sync_io.debug_model_output_enable_mask;
}

uint32_t sync_io_debug_model_get_output_value_mask(void)
{
    return s_sync_io.debug_model_output_value_mask;
}

bool sync_io_aux_set_mode(sync_io_aux_channel_t channel, sync_io_aux_mode_t mode)
{
    if (!s_sync_io.initialized || !sync_io_valid_aux_channel(channel)) {
        return false;
    }

    const uint index = (uint)channel;
    if (!sync_io_aux_mode_allowed(channel, mode)) {
        sync_io_trace(SYNC_IO_TRACE_AUX_DIRECTION,
                      SYNC_IO_TRACE_WARN,
                      index,
                      (uint32_t)mode);
        return false;
    }

    if (sync_io_aux_resource_busy()) {
        sync_io_trace(SYNC_IO_TRACE_AUX_BUSY,
                      SYNC_IO_TRACE_WARN,
                      index,
                      (uint32_t)mode);
        return false;
    }

    const uint sm = s_aux_sms[index];
    const uint pin = s_aux_pins[index];

    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, sm);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, sm);

    if (mode == SYNC_IO_AUX_MODE_INPUT) {
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);
        s_sync_io.aux_modes[index] = mode;
        return true;
    }

    if (mode == SYNC_IO_AUX_MODE_PIO_OUTPUT) {
        pio_gpio_init(BOARD_SYNC_PIO_AUX, pin);
        pio_sm_set_consecutive_pindirs(BOARD_SYNC_PIO_AUX, sm, pin, 1, true);
        pio_sm_put(BOARD_SYNC_PIO_AUX, sm, 0u);
        pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, sm, true);
        s_sync_io.aux_modes[index] = mode;
        return true;
    }

    return false;
}

bool sync_io_aux_write(sync_io_aux_channel_t channel, bool level)
{
    if (!s_sync_io.initialized || !sync_io_valid_aux_channel(channel)) {
        return false;
    }

    const uint index = (uint)channel;
    if (sync_io_aux_resource_busy()) {
        sync_io_trace(SYNC_IO_TRACE_AUX_BUSY,
                      SYNC_IO_TRACE_WARN,
                      index,
                      level ? 1u : 0u);
        return false;
    }

    if (!sync_io_hw_aux_supports_output(index) ||
        s_sync_io.aux_modes[index] != SYNC_IO_AUX_MODE_PIO_OUTPUT) {
        sync_io_trace(SYNC_IO_TRACE_AUX_DIRECTION,
                      SYNC_IO_TRACE_WARN,
                      index,
                      1u);
        return false;
    }

    const uint sm = s_aux_sms[index];
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, sm);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, sm);
    pio_sm_put(BOARD_SYNC_PIO_AUX, sm, level ? 1u : 0u);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, sm, true);
    return true;
}

bool sync_io_aux_read(sync_io_aux_channel_t channel, bool *level)
{
    if (!s_sync_io.initialized || !sync_io_valid_aux_channel(channel) || level == NULL) {
        return false;
    }

    if (!sync_io_hw_aux_supports_input((uint32_t)channel)) {
        sync_io_trace(SYNC_IO_TRACE_AUX_DIRECTION,
                      SYNC_IO_TRACE_WARN,
                      (uint32_t)channel,
                      0u);
        return false;
    }

    *level = gpio_get(s_aux_pins[(uint)channel]) != 0;
    return true;
}

void sync_io_get_status(sync_io_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->initialized = s_sync_io.initialized;
    status->capture_running = s_sync_io.capture_running;
    status->sync_clock_running = s_sync_io.clock_running;
    status->capture_sample_hz = s_sync_io.capture_sample_hz;
    status->sync_clock_hz = s_sync_io.sync_clock_hz;
    status->dropped_capture_words = s_sync_io.dropped_capture_words;
    status->latched_capture_words = s_sync_io.latched_capture_words;
    status->dropped_latched_capture_words = s_sync_io.dropped_latched_capture_words;
    status->capture_latch_source = SYNC_IO_CAPTURE_TIMESTAMP_SOURCE_HARDWARE_TICK;
    status->capture_latch_resolution_ns = s_sync_io.capture_latch_resolution_ns;
    status->capture_latch_flags = SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
}

void sync_io_get_capture_debug(sync_io_capture_debug_t *debug)
{
    if (debug == NULL) {
        return;
    }

    debug->initialized = s_sync_io.initialized;
    debug->capture_running = s_sync_io.capture_running;
    debug->pio_enabled =
        sync_io_core_sm_is_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    debug->dma_busy = dma_channel_is_busy(SYNC_IO_CAPTURE_DMA_CH);
    debug->rx_fifo_empty =
        pio_sm_is_rx_fifo_empty(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    debug->rx_fifo_full =
        pio_sm_is_rx_fifo_full(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    debug->dma_transfer_count =
        dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].transfer_count;
    debug->dma_write_addr_lsb =
        (uint32_t)((uintptr_t)dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].write_addr &
                   0xFFFFFFFFu);
    debug->dma_ring_addr_lsb =
        (uint32_t)((uintptr_t)s_sync_io_capture_dma_ring & 0xFFFFFFFFu);
    debug->dma_ring_align_mask =
        debug->dma_ring_addr_lsb & (SYNC_IO_CAPTURE_DMA_RING_BYTES - 1u);
    debug->capture_dma_read_seq = s_sync_io.capture_dma_read_seq;
    debug->produced_words = sync_io_capture_dma_produced_words();
}

void sync_io_set_expected_ready_mask(uint32_t mask)
{
    const uint32_t sanitized = mask & 0xFFu;
    if (s_expected_ready_mask != sanitized) {
        s_aux_wait_start_ms = osal_uptime_ms();
        s_aux_timeout_latched_mask = 0u;
        s_aux_trace_sample_valid = false;
    }
    s_expected_ready_mask = sanitized;
}

uint32_t sync_io_get_expected_ready_mask(void)
{
    return s_expected_ready_mask;
}

void sync_io_trace_aux_status_sample(bool force)
{
    const uint32_t now_ms = osal_uptime_ms();
    if (force) {
        s_aux_wait_start_ms = now_ms;
        s_aux_timeout_latched_mask = 0u;
    }

    const uint32_t aux_levels = sync_io_read_aux_level_mask();
    const uint32_t aux_modes = sync_io_read_aux_mode_mask();
    const uint32_t aux_snapshot = aux_levels | (aux_modes << 8);
    const uint32_t ready_snapshot = sync_io_read_ready_level_mask();
    const uint32_t expected_ready_mask = sync_io_get_expected_ready_mask();
    const uint32_t missing_ready_mask = expected_ready_mask & ~ready_snapshot;
    const bool aux_changed = !s_aux_trace_sample_valid ||
                             aux_snapshot != s_aux_last_snapshot;
    const bool ready_changed = !s_aux_trace_sample_valid ||
                               ready_snapshot != s_ready_last_snapshot;

    if (!s_aux_trace_sample_valid || missing_ready_mask == 0u) {
        s_aux_wait_start_ms = now_ms;
    }

    const uint32_t wait_ms = now_ms - s_aux_wait_start_ms;
    const bool timeout_now = missing_ready_mask != 0u &&
                             wait_ms >= SYNC_IO_AUX_READY_TIMEOUT_MS;
    const uint32_t new_timeout_mask = timeout_now
        ? (missing_ready_mask & ~s_aux_timeout_latched_mask)
        : 0u;

    if (force || aux_changed) {
        sync_io_trace(SYNC_IO_TRACE_AUX_SNAPSHOT,
                      SYNC_IO_TRACE_INFO,
                      aux_snapshot,
                      BOARD_SYNC_AUX0_PIN |
                          (BOARD_SYNC_AUX1_PIN << 8) |
                          (BOARD_SYNC_AUX2_PIN << 16) |
                          (BOARD_SYNC_AUX3_PIN << 24));
    }

    if (force || ready_changed || new_timeout_mask != 0u) {
        sync_io_trace(SYNC_IO_TRACE_READY_REDY,
                      new_timeout_mask != 0u ? SYNC_IO_TRACE_WARN : SYNC_IO_TRACE_INFO,
                      ready_snapshot,
                      (expected_ready_mask & 0xFFu) |
                          ((missing_ready_mask & 0xFFu) << 8));
    }

    if (force || new_timeout_mask != 0u) {
        if (new_timeout_mask != 0u) {
            s_aux_timeout_latched_mask |= new_timeout_mask;
        }
        sync_io_trace(SYNC_IO_TRACE_AUX_TIMEOUT,
                      new_timeout_mask != 0u ? SYNC_IO_TRACE_WARN : SYNC_IO_TRACE_INFO,
                      s_aux_timeout_latched_mask,
                      wait_ms);
    }

    s_aux_last_snapshot = aux_snapshot;
    s_ready_last_snapshot = ready_snapshot;
    s_aux_trace_sample_valid = true;
}

void sync_io_core_dma_irq_handler(void)
{
    assert(!sync_io_seq_step_is_running() || !sync_io_enc_count_is_running());

    const uint32_t ints = dma_hw->ints0;
    dma_hw->ints0 = ints;   /* 清除本次触发的中断位 */

    (void)sync_io_seq_step_dma_irq_service(ints);
    (void)sync_io_enc_count_dma_irq_service(ints);
}
