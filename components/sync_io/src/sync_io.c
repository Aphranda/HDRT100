#include "sync_io.h"

#include "board_config.h"
#include "diagnostics.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "osal.h"
#include "resource_arbiter.h"
#include "biss_tap_rx.pio.h"
#include "seq_step.pio.h"
#include "storage_manager.h"
#include "sync_io.pio.h"

#define SYNC_IO_DEFAULT_CAPTURE_HZ  1000000u
#define SYNC_IO_DEFAULT_CLOCK_HZ    1000000u
#define SYNC_IO_MIN_HZ              1u
#define SYNC_IO_SEQ_STEP_DMA_CH     0u
#define SYNC_IO_ENC_COUNT_DMA_CH    1u
#define SYNC_IO_SEQ_STEP_DMA_IRQ    DMA_IRQ_0
#define SYNC_IO_TRACE_DOMAIN        3u
#define SYNC_IO_TRACE_INFO          1u
#define SYNC_IO_TRACE_WARN          2u
#define SYNC_IO_TRACE_ERROR         3u
#define SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD 1u
#define SYNC_IO_AUX_READY_TIMEOUT_MS 1000u
#define SYNC_IO_BISS_TAP_SM BOARD_SYNC_AUX0_SM

typedef enum {
    SYNC_IO_TRACE_INIT_OK           = 10u,
    SYNC_IO_TRACE_INIT_FAIL         = 11u,
    SYNC_IO_TRACE_CAPTURE_START     = 20u,
    SYNC_IO_TRACE_CAPTURE_STOP      = 21u,
    SYNC_IO_TRACE_CAPTURE_DROP      = 22u,
    SYNC_IO_TRACE_CAPTURE_FAIL      = 23u,
    SYNC_IO_TRACE_PULSE_FIFO_FULL   = 30u,
    SYNC_IO_TRACE_PULSE_INVALID     = 31u,
    SYNC_IO_TRACE_CLOCK_START       = 40u,
    SYNC_IO_TRACE_CLOCK_STOP        = 41u,
    SYNC_IO_TRACE_CLOCK_FAIL        = 42u,
    SYNC_IO_TRACE_SEQ_ARM_FAIL      = 50u,
    SYNC_IO_TRACE_SEQ_ARMED         = 51u,
    SYNC_IO_TRACE_SEQ_DISARM        = 52u,
    SYNC_IO_TRACE_SEQ_GATE_INVALID  = 53u,
    SYNC_IO_TRACE_SEQ_PIO_NO_SPACE  = 54u,
    SYNC_IO_TRACE_SEQ_RUNTIME       = 55u,
    SYNC_IO_TRACE_SEQ_PIO_STATE     = 56u,
    SYNC_IO_TRACE_SEQ_DMA_RESTART   = 57u,
    SYNC_IO_TRACE_SEQ_DMA_OVERFLOW  = 58u,
    SYNC_IO_TRACE_ENC_ARM_FAIL      = 60u,
    SYNC_IO_TRACE_ENC_ARMED         = 61u,
    SYNC_IO_TRACE_ENC_DISARM        = 62u,
    SYNC_IO_TRACE_ENC_PIO_NO_SPACE  = 63u,
    SYNC_IO_TRACE_ENC_RUNTIME       = 64u,
    SYNC_IO_TRACE_ENC_PIO_STATE     = 65u,
    SYNC_IO_TRACE_ENC_DMA_RESTART   = 66u,
    SYNC_IO_TRACE_ENC_DMA_OVERFLOW  = 67u,
    SYNC_IO_TRACE_AUX_SNAPSHOT      = 70u,
    SYNC_IO_TRACE_READY_REDY        = 71u,
    SYNC_IO_TRACE_AUX_TIMEOUT       = 72u,
    SYNC_IO_TRACE_AUX_BUSY          = 73u,
    SYNC_IO_TRACE_BISS_TAP_ARM      = 80u,
    SYNC_IO_TRACE_BISS_TAP_DISARM   = 81u,
    SYNC_IO_TRACE_BISS_TAP_FAIL     = 82u,
} sync_io_trace_event_t;

typedef struct {
    bool initialized;
    bool capture_running;
    bool clock_running;
    uint capture_offset;
    uint pulse_offset;
    uint clock_offset;
    uint aux_offset;
    uint biss_tap_offset;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t dropped_capture_words;
    sync_io_aux_mode_t aux_modes[SYNC_IO_AUX_COUNT];
    bool biss_tap_running;
    uint32_t biss_tap_frame_bits;
    uint32_t biss_tap_clk_pin;
    uint32_t biss_tap_data_pin;
} sync_io_context_t;

static sync_io_context_t s_sync_io;

static void sync_io_trace(sync_io_trace_event_t event_id,
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

static bool sync_io_sm_is_enabled(PIO pio, uint sm)
{
    return (pio->ctrl & (1u << sm)) != 0u;
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

/* ── SEQ_STEP 状态 ── */

typedef struct {
    bool running;
    bool has_gate;
    uint seq_sm;
    uint seq_offset;
    uint dma_ch;
    uint seq_length;
    uint seq_width;
    uintptr_t seq_table_addr;
    volatile uint64_t rollover_count;
    volatile bool dma_done;
} sync_io_seq_step_t;

static sync_io_seq_step_t s_seq_step;
static uint32_t s_seq_last_runtime_flags;
static uint32_t s_seq_last_transfer_count;
static uint32_t s_seq_last_rollover_low32;
static bool s_seq_dma_overflow_latched;
static bool s_seq_trace_sample_valid;

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
    return (uint)channel < (uint)SYNC_IO_AUX_COUNT;
}

static bool sync_io_aux_resource_busy(void)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    return (snapshot.active_resources & RESOURCE_ARBITER_RESOURCE_AUX) != 0u;
}

static void sync_io_restore_aux_channel_input(uint pin)
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

static void sync_io_mark_aux_channel_input(uint pin)
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
           (gpio_get(BOARD_SYNC_AUX_MARKER_OUT_PIN) ? (1u << 7) : 0u);
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
        !pio_can_add_program(BOARD_SYNC_PIO_WAVE, &sync_clock_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_AUX, &biss_tap_rx_program)) {
        LOG_ERROR("sync_io", "not enough PIO instruction memory");
        sync_io_trace(SYNC_IO_TRACE_INIT_FAIL, SYNC_IO_TRACE_ERROR, 1u, 0u);
        return false;
    }

    if (!sync_io_claim_sm(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, "capture") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, "output") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, "clock") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_GATE_SM, "pulse") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_MARKER_SM, "marker") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX0_SM, "aux0") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX1_SM, "aux1") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM, "aux2") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM, "aux3")) {
        sync_io_trace(SYNC_IO_TRACE_INIT_FAIL, SYNC_IO_TRACE_ERROR, 2u, 0u);
        return false;
    }

    s_sync_io.capture_offset = (uint)pio_add_program(BOARD_SYNC_PIO_FAST, &sync_capture_4bit_program);
    s_sync_io.pulse_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, &sync_pulse_program);
    s_sync_io.clock_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, &sync_clock_program);
    s_sync_io.aux_offset = (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program);
    s_sync_io.biss_tap_offset = (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &biss_tap_rx_program);

    sync_io_configure_static_inputs();

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

    sync_clock_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_CLOCK_SM,
                            s_sync_io.clock_offset,
                            BOARD_SYNC_SYNC_CLK_OUT_PIN,
                            sync_io_clkdiv_for_instruction_rate(clock_hz * 2u));

    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_MARKER_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_MARKER_OUT_PIN);

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

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_GATE_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, false);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_MARKER_SM, true);

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
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, true);

    s_sync_io.capture_sample_hz = sample_hz;
    s_sync_io.capture_running = true;
    sync_io_trace(SYNC_IO_TRACE_CAPTURE_START, SYNC_IO_TRACE_INFO, sample_hz, 0u);
    return true;
}

void sync_io_stop_capture(void)
{
    if (!s_sync_io.initialized) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    s_sync_io.capture_running = false;
    sync_io_trace(SYNC_IO_TRACE_CAPTURE_STOP,
                  SYNC_IO_TRACE_INFO,
                  s_sync_io.capture_sample_hz,
                  s_sync_io.dropped_capture_words);
}

size_t sync_io_read_capture_words(uint32_t *buffer, size_t max_words)
{
    if (!s_sync_io.initialized || buffer == NULL || max_words == 0u) {
        return 0u;
    }

    size_t count = 0u;
    while (count < max_words && !pio_sm_is_rx_fifo_empty(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM)) {
        buffer[count] = pio_sm_get(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
        count++;
    }

    if (pio_sm_is_rx_fifo_full(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM)) {
        s_sync_io.dropped_capture_words++;
        sync_io_trace(SYNC_IO_TRACE_CAPTURE_DROP,
                      SYNC_IO_TRACE_WARN,
                      s_sync_io.dropped_capture_words,
                      (uint32_t)count);
    }

    return count;
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

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, false);
    pio_sm_set_clkdiv(BOARD_SYNC_PIO_WAVE,
                      BOARD_SYNC_CLOCK_SM,
                      sync_io_clkdiv_for_instruction_rate(frequency_hz * 2u));
    pio_sm_restart(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, true);

    s_sync_io.sync_clock_hz = frequency_hz;
    s_sync_io.clock_running = true;
    sync_io_trace(SYNC_IO_TRACE_CLOCK_START, SYNC_IO_TRACE_INFO, frequency_hz, 0u);
    return true;
}

void sync_io_stop_clock(void)
{
    if (!s_sync_io.initialized) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, false);
    pio_sm_set_pins(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, 0);
    s_sync_io.clock_running = false;
    sync_io_trace(SYNC_IO_TRACE_CLOCK_STOP,
                  SYNC_IO_TRACE_INFO,
                  s_sync_io.sync_clock_hz,
                  0u);
}

bool sync_io_fire_marker_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_on_sm(BOARD_SYNC_MARKER_SM, high_cycles);
}

bool sync_io_fire_marker_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_sm(BOARD_SYNC_MARKER_SM, high_us);
}

bool sync_io_aux_set_mode(sync_io_aux_channel_t channel, sync_io_aux_mode_t mode)
{
    if (!s_sync_io.initialized || !sync_io_valid_aux_channel(channel)) {
        return false;
    }

    const uint index = (uint)channel;
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

    if (s_sync_io.aux_modes[index] != SYNC_IO_AUX_MODE_PIO_OUTPUT) {
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

    *level = gpio_get(s_aux_pins[(uint)channel]) != 0;
    return true;
}

bool sync_io_biss_tap_arm(const sync_io_biss_tap_config_t *config)
{
    if (!s_sync_io.initialized || config == NULL ||
        config->frame_bits == 0u ||
        config->frame_bits > 64u ||
        config->clk_pin == config->data_pin ||
        config->sample_edge > 1u) {
        sync_io_trace(SYNC_IO_TRACE_BISS_TAP_FAIL,
                      SYNC_IO_TRACE_ERROR,
                      config != NULL ? config->frame_bits : 0u,
                      config != NULL ? config->sample_edge : 0u);
        return false;
    }

    if (s_sync_io.biss_tap_running) {
        sync_io_biss_tap_disarm();
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);

    biss_tap_rx_program_init(BOARD_SYNC_PIO_AUX,
                             SYNC_IO_BISS_TAP_SM,
                             s_sync_io.biss_tap_offset,
                             config->clk_pin,
                             config->data_pin,
                             config->frame_bits,
                             config->sample_delay_cycles,
                             (int)config->sample_edge,
                             1.0f);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM, true);

    s_sync_io.biss_tap_running = true;
    s_sync_io.biss_tap_frame_bits = config->frame_bits;
    s_sync_io.biss_tap_clk_pin = config->clk_pin;
    s_sync_io.biss_tap_data_pin = config->data_pin;
    sync_io_mark_aux_channel_input(config->clk_pin);
    sync_io_mark_aux_channel_input(config->data_pin);

    sync_io_trace(SYNC_IO_TRACE_BISS_TAP_ARM,
                  SYNC_IO_TRACE_INFO,
                  config->frame_bits,
                  (config->sample_edge & 0xFFu) |
                      ((config->sample_delay_cycles & 0xFFu) << 8));
    return true;
}

void sync_io_biss_tap_disarm(void)
{
    if (!s_sync_io.initialized || !s_sync_io.biss_tap_running) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
    sync_io_restore_aux_channel_input(s_sync_io.biss_tap_clk_pin);
    sync_io_restore_aux_channel_input(s_sync_io.biss_tap_data_pin);

    sync_io_trace(SYNC_IO_TRACE_BISS_TAP_DISARM,
                  SYNC_IO_TRACE_INFO,
                  s_sync_io.biss_tap_frame_bits,
                  0u);
    s_sync_io.biss_tap_running = false;
    s_sync_io.biss_tap_frame_bits = 0u;
    s_sync_io.biss_tap_clk_pin = 0u;
    s_sync_io.biss_tap_data_pin = 0u;
}

bool sync_io_biss_tap_is_running(void)
{
    return s_sync_io.initialized && s_sync_io.biss_tap_running;
}

bool sync_io_biss_tap_read_frame_word(uint32_t *word)
{
    if (!s_sync_io.initialized ||
        !s_sync_io.biss_tap_running ||
        word == NULL ||
        pio_sm_is_rx_fifo_empty(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM)) {
        return false;
    }

    *word = pio_sm_get(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
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

/* ── 前向声明（ENC_COUNT 结构体定义在 SEQ_STEP 之后）── */
typedef struct {
    bool running;
    uint sm;
    uint offset;
    uint target;
    uint in_pin_base;
    uint dma_ch;
    volatile uint32_t fire_count;
    volatile uint32_t dma_restart_count;
} sync_io_enc_count_t;
static sync_io_enc_count_t s_enc;
static uint32_t s_enc_last_runtime_flags;
static uint32_t s_enc_last_transfer_count;
static uint32_t s_enc_last_dma_restart_count;
static bool s_enc_dma_overflow_latched;
static bool s_enc_trace_sample_valid;

/* ── SEQ_STEP 编码序列步进 ── */

static void sync_io_seq_step_dma_handler(void)
{
    const uint32_t ints = dma_hw->ints0;
    dma_hw->ints0 = ints;   /* 清除本次触发的中断位 */

    /* SEQ_STEP: 手动重置读地址 + 重启传输 */
    if ((ints & (1u << s_seq_step.dma_ch)) && s_seq_step.running) {
        s_seq_step.rollover_count++;
        /* 重置读指针到序列表开头 (不用 ring buffer) */
        dma_hw->ch[s_seq_step.dma_ch].read_addr = s_seq_step.seq_table_addr;
        dma_hw->ch[s_seq_step.dma_ch].al1_transfer_count_trig =
            s_seq_step.seq_length;
    }

    /* ENC_COUNT: 每次触发后补填 TX FIFO */
    if ((ints & (1u << s_enc.dma_ch)) && s_enc.running) {
        s_enc.fire_count++;
        s_enc.dma_restart_count++;
        dma_hw->ch[s_enc.dma_ch].al1_transfer_count_trig = 1u;
    }
}

bool sync_io_seq_step_arm(const uint32_t *seq_table,
                          uint32_t seq_length,
                          uint32_t seq_width,
                          uint32_t trigger_pin,
                          sync_io_edge_t edge,
                          bool gate_enabled)
{
    if (!s_sync_io.initialized ||
        seq_table == NULL ||
        seq_length == 0u ||
        seq_length > 256u ||
        seq_width == 0u ||
        seq_width > 8u) {
        sync_io_trace(SYNC_IO_TRACE_SEQ_ARM_FAIL,
                      SYNC_IO_TRACE_ERROR,
                      seq_length,
                      seq_width);
        return false;
    }

    /* ── 强制重置: 确保 DMA/PIO 处于干净状态 ── */
    if (s_seq_step.running) {
        sync_io_seq_step_disarm();
    } else {
        /* 即使 running==false，也清理可能的残留状态 */
        dma_channel_abort(SYNC_IO_SEQ_STEP_DMA_CH);
        pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, false);
        pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM);
    }

    /* 清除 DMA IRQ0 所有残留中断 */
    dma_hw->ints0 = dma_hw->ints0;
    irq_set_enabled(SYNC_IO_SEQ_STEP_DMA_IRQ, false);
    dma_channel_set_irq0_enabled(SYNC_IO_SEQ_STEP_DMA_CH, false);

    /* GATE 模式要求触发源在 GPIO16-19 范围内
     * (GATE_IN 固定 GPIO19 = in_pin_base 16 的 offset 3) */
    if (gate_enabled && (trigger_pin < 16u || trigger_pin > 19u)) {
        LOG_ERROR("sync_io",
                  "seq_step: gate mode requires trigger_pin in GPIO16..GPIO19, "
                  "got %lu", (unsigned long)trigger_pin);
        sync_io_trace(SYNC_IO_TRACE_SEQ_GATE_INVALID,
                      SYNC_IO_TRACE_ERROR,
                      trigger_pin,
                      0u);
        return false;
    }

    /* ── PIO 程序选择 ── */

    const pio_program_t *prog = gate_enabled
        ? &seq_step_gated_program
        : &seq_step_program;

    if (!pio_can_add_program(BOARD_SYNC_PIO_WAVE, prog)) {
        LOG_ERROR("sync_io", "seq_step: not enough PIO instruction space");
        sync_io_trace(SYNC_IO_TRACE_SEQ_PIO_NO_SPACE,
                      SYNC_IO_TRACE_ERROR,
                      gate_enabled ? 1u : 0u,
                      seq_length);
        return false;
    }

    s_seq_step.seq_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, prog);
    s_seq_step.seq_sm = BOARD_SYNC_OUTPUT_SM;
    s_seq_step.has_gate = gate_enabled;

    /* 停旧 SM，清 FIFO */
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    pio_sm_restart(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);

    /* ── PIO 初始化（HAOFV 实时面配置, ARM 时一次性完成）── */

    seq_step_program_init_ex(BOARD_SYNC_PIO_WAVE,
                              s_seq_step.seq_sm,
                              s_seq_step.seq_offset,
                              trigger_pin,
                              BOARD_SYNC_OUTPUT_BASE_PIN,
                              seq_width,
                              1.0f,
                              gate_enabled,
                              (int)edge);

    /* ── DMA 配置: SRAM seq_table → PIO TX FIFO, ring-buffer ── */

    s_seq_step.dma_ch = SYNC_IO_SEQ_STEP_DMA_CH;
    dma_channel_abort(s_seq_step.dma_ch);

    s_seq_step.seq_length = seq_length;
    s_seq_step.seq_width = seq_width;
    s_seq_step.seq_table_addr = (uintptr_t)seq_table;

    /* ── 先安装 ISR，再启动 DMA（避免回绕时中断丢失）── */
    s_seq_step.rollover_count = 0u;
    s_seq_step.dma_done = false;
    s_seq_dma_overflow_latched = false;
    s_seq_trace_sample_valid = false;

    irq_set_exclusive_handler(SYNC_IO_SEQ_STEP_DMA_IRQ,
                              sync_io_seq_step_dma_handler);
    dma_channel_set_irq0_enabled(s_seq_step.dma_ch, true);
    irq_set_enabled(SYNC_IO_SEQ_STEP_DMA_IRQ, true);

    dma_channel_config dma_cfg = dma_channel_get_default_config(s_seq_step.dma_ch);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg,
                            DREQ_PIO1_TX0 + s_seq_step.seq_sm);
    /* 不用 DMA ring buffer — ISR 中手动重置 read_addr */
    dma_channel_configure(s_seq_step.dma_ch,
                          &dma_cfg,
                          &BOARD_SYNC_PIO_WAVE->txf[s_seq_step.seq_sm],
                          seq_table,
                          seq_length,
                          true);   /* 立即启动, 自链持续循环 */

    /* ── 启动 PIO ── */

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, true);

    s_seq_step.running = true;

    LOG_INFO("sync_io", "seq_step armed: length=%lu width=%lu",
             (unsigned long)seq_length, (unsigned long)seq_width);
    sync_io_trace(SYNC_IO_TRACE_SEQ_ARMED,
                  SYNC_IO_TRACE_INFO,
                  seq_length,
                  ((seq_width & 0xFFu) << 24) |
                      ((trigger_pin & 0xFFu) << 8) |
                      ((uint32_t)edge & 0xFFu) |
                      (gate_enabled ? (1u << 16) : 0u));
    sync_io_seq_step_runtime_t runtime;
    sync_io_seq_step_get_runtime(&runtime);
    sync_io_trace(SYNC_IO_TRACE_SEQ_RUNTIME,
                  SYNC_IO_TRACE_INFO,
                  sync_io_pack_runtime_flags(runtime.running,
                                             runtime.pio_enabled,
                                             runtime.dma_busy,
                                             runtime.dma_irq_enabled,
                                             runtime.tx_fifo_empty,
                                             runtime.tx_fifo_full),
                  runtime.transfer_count & 0xFFFFu);
    sync_io_seq_step_trace_runtime_sample(true);

    return true;
}

void sync_io_seq_step_disarm(void)
{
    if (!s_seq_step.running) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, false);
    irq_set_enabled(SYNC_IO_SEQ_STEP_DMA_IRQ, false);
    dma_channel_set_irq0_enabled(s_seq_step.dma_ch, false);
    dma_channel_abort(s_seq_step.dma_ch);

    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    pio_sm_set_pins(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, 0);

    /* 恢复 GPIO 到默认 SIO 状态 */
    for (uint pin = 0u; pin < s_seq_step.seq_width; pin++) {
        gpio_set_function(BOARD_SYNC_OUTPUT_BASE_PIN + pin, GPIO_FUNC_SIO);
        gpio_set_dir(BOARD_SYNC_OUTPUT_BASE_PIN + pin, GPIO_OUT);
        gpio_put(BOARD_SYNC_OUTPUT_BASE_PIN + pin, false);
    }
    /* 恢复输入引脚到 SIO (避免下次 ARM 时残留 PIO 状态) */
    gpio_set_function(BOARD_SYNC_TRIG_IN_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(BOARD_SYNC_TRIG_IN_PIN, GPIO_IN);
    gpio_pull_down(BOARD_SYNC_TRIG_IN_PIN);

    const pio_program_t *prog_to_remove = s_seq_step.has_gate
        ? &seq_step_gated_program
        : &seq_step_program;
    pio_remove_program(BOARD_SYNC_PIO_WAVE, prog_to_remove,
                       s_seq_step.seq_offset);

    s_seq_step.running = false;
    LOG_INFO("sync_io", "seq_step disarmed: rollover_count=%lu",
             (unsigned long)s_seq_step.rollover_count);
    sync_io_trace(SYNC_IO_TRACE_SEQ_DISARM,
                  SYNC_IO_TRACE_INFO,
                  (uint32_t)s_seq_step.rollover_count,
                  s_seq_step.seq_length);
}

uint32_t sync_io_seq_step_get_index(void)
{
    if (!s_seq_step.running) {
        return 0u;
    }

    const uint32_t remaining = dma_hw->ch[s_seq_step.dma_ch].transfer_count
                               & 0xFFFFu;
    const uint32_t idx = s_seq_step.seq_length - remaining;

    return (idx >= s_seq_step.seq_length) ? 0u : idx;
}

bool sync_io_seq_step_is_running(void)
{
    return s_seq_step.running;
}

uint64_t sync_io_seq_step_get_rollover_count(void)
{
    return s_seq_step.rollover_count;
}

void sync_io_seq_step_get_runtime(sync_io_seq_step_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    runtime->running = s_seq_step.running;
    runtime->pio_enabled = false;
    runtime->dma_busy = false;
    runtime->dma_irq_enabled = false;
    runtime->tx_fifo_empty = true;
    runtime->tx_fifo_full = false;
    runtime->transfer_count = 0u;
    runtime->rollover_count_low32 = (uint32_t)s_seq_step.rollover_count;

    if (!s_seq_step.running) {
        return;
    }

    runtime->pio_enabled = sync_io_sm_is_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    runtime->dma_busy = dma_channel_is_busy(s_seq_step.dma_ch);
    runtime->dma_irq_enabled = (dma_hw->inte0 & (1u << s_seq_step.dma_ch)) != 0u;
    runtime->tx_fifo_empty = pio_sm_is_tx_fifo_empty(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    runtime->tx_fifo_full = pio_sm_is_tx_fifo_full(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    runtime->transfer_count = dma_hw->ch[s_seq_step.dma_ch].transfer_count;
}

void sync_io_seq_step_trace_runtime_sample(bool force)
{
    sync_io_seq_step_runtime_t runtime;
    sync_io_seq_step_get_runtime(&runtime);

    const uint32_t flags = sync_io_pack_runtime_flags(runtime.running,
                                                      runtime.pio_enabled,
                                                      runtime.dma_busy,
                                                      runtime.dma_irq_enabled,
                                                      runtime.tx_fifo_empty,
                                                      runtime.tx_fifo_full);
    const uint32_t transfer_count = runtime.transfer_count & 0xFFFFu;
    const uint32_t rollover_low32 = runtime.rollover_count_low32;
    const bool runtime_changed = !s_seq_trace_sample_valid ||
                                 flags != s_seq_last_runtime_flags ||
                                 transfer_count != s_seq_last_transfer_count;
    const bool rollover_changed = !s_seq_trace_sample_valid ||
                                  rollover_low32 != s_seq_last_rollover_low32;
    const uint32_t rollover_delta = s_seq_trace_sample_valid
        ? (uint32_t)(rollover_low32 - s_seq_last_rollover_low32)
        : 0u;
    const bool overflow_detected =
        rollover_delta > SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD;

    if (force || runtime_changed) {
        sync_io_trace(SYNC_IO_TRACE_SEQ_PIO_STATE,
                      runtime.running ? SYNC_IO_TRACE_INFO : SYNC_IO_TRACE_WARN,
                      sync_io_pack_pio_state(s_seq_step.seq_sm,
                                             s_seq_step.seq_offset,
                                             runtime.pio_enabled,
                                             runtime.tx_fifo_empty,
                                             runtime.tx_fifo_full),
                      transfer_count);
    }

    if (force || rollover_changed) {
        sync_io_trace(SYNC_IO_TRACE_SEQ_DMA_RESTART,
                      SYNC_IO_TRACE_INFO,
                      rollover_low32,
                      transfer_count);
    }

    if (force || overflow_detected || s_seq_dma_overflow_latched) {
        if (overflow_detected) {
            s_seq_dma_overflow_latched = true;
        }
        sync_io_trace(SYNC_IO_TRACE_SEQ_DMA_OVERFLOW,
                      overflow_detected ? SYNC_IO_TRACE_WARN : SYNC_IO_TRACE_INFO,
                      rollover_low32,
                      ((rollover_delta & 0xFFFFu) << 16) |
                          (SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD & 0xFFFFu));
    }

    s_seq_last_runtime_flags = flags;
    s_seq_last_transfer_count = transfer_count;
    s_seq_last_rollover_low32 = rollover_low32;
    s_seq_trace_sample_valid = true;
}

/* ── ENC_COUNT 编码器计数触发 ── */

#include "enc_count.pio.h"

bool sync_io_enc_count_arm(uint32_t target,
                           uint32_t in_pin_base,
                           uint32_t output_pin)
{
    if (!s_sync_io.initialized || target == 0u) {
        sync_io_trace(SYNC_IO_TRACE_ENC_ARM_FAIL,
                      SYNC_IO_TRACE_ERROR,
                      target,
                      s_sync_io.initialized ? 0u : 1u);
        return false;
    }

    if (s_enc.running) {
        sync_io_enc_count_disarm();
    }

    if (!pio_can_add_program(BOARD_SYNC_PIO_WAVE, &enc_count_program)) {
        LOG_ERROR("sync_io", "enc_count: not enough PIO instruction space");
        sync_io_trace(SYNC_IO_TRACE_ENC_PIO_NO_SPACE,
                      SYNC_IO_TRACE_ERROR,
                      target,
                      in_pin_base);
        return false;
    }

    s_enc.offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE,
                                          &enc_count_program);
    s_enc.sm = BOARD_SYNC_OUTPUT_SM;
    s_enc.target = target;
    s_enc.in_pin_base = in_pin_base;

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_enc.sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    pio_sm_restart(BOARD_SYNC_PIO_WAVE, s_enc.sm);

    enc_count_program_init(BOARD_SYNC_PIO_WAVE,
                           s_enc.sm,
                           s_enc.offset,
                           in_pin_base,
                           output_pin,
                           1.0f);

    /* TX FIFO: 首字 = 目标值, 后续由 DMA 从 &s_enc.target 持续补填 */
    pio_sm_put(BOARD_SYNC_PIO_WAVE, s_enc.sm, target);

    /* ── DMA 配置: &s_enc.target → PIO TX FIFO, DREQ 节拍 ── */
    s_enc.dma_ch = SYNC_IO_ENC_COUNT_DMA_CH;
    dma_channel_abort(s_enc.dma_ch);

    {
        dma_channel_config dma_cfg =
            dma_channel_get_default_config(s_enc.dma_ch);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_write_increment(&dma_cfg, false);
        channel_config_set_dreq(&dma_cfg,
                                DREQ_PIO1_TX0 + s_enc.sm);
        dma_channel_configure(s_enc.dma_ch,
                              &dma_cfg,
                              &BOARD_SYNC_PIO_WAVE->txf[s_enc.sm],
                              &s_enc.target,
                              0xFFFFFFFFu,   /* 极长 transfer_count, 耗尽后 IRQ 重启 */
                              true);
    }

    dma_channel_set_irq0_enabled(s_enc.dma_ch, true);

    /* 确保 DMA IRQ 已安装（可能已由 SEQ_STEP 安装） */
    if (!irq_is_enabled(SYNC_IO_SEQ_STEP_DMA_IRQ)) {
        irq_set_exclusive_handler(SYNC_IO_SEQ_STEP_DMA_IRQ,
                                  sync_io_seq_step_dma_handler);
        irq_set_enabled(SYNC_IO_SEQ_STEP_DMA_IRQ, true);
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_enc.sm, true);

    s_enc.fire_count = 0u;
    s_enc.dma_restart_count = 0u;
    s_enc_dma_overflow_latched = false;
    s_enc_trace_sample_valid = false;
    s_enc.running = true;

    LOG_INFO("sync_io", "enc_count armed: target=%lu pins=A%lu/B%lu/Z%lu",
             (unsigned long)target,
             (unsigned long)(in_pin_base + 0),
             (unsigned long)(in_pin_base + 1),
             (unsigned long)(in_pin_base + 3));
    sync_io_trace(SYNC_IO_TRACE_ENC_ARMED,
                  SYNC_IO_TRACE_INFO,
                  target,
                  ((in_pin_base & 0xFFu) << 8) | (output_pin & 0xFFu));
    sync_io_enc_count_runtime_t runtime;
    sync_io_enc_count_get_runtime(&runtime);
    sync_io_trace(SYNC_IO_TRACE_ENC_RUNTIME,
                  SYNC_IO_TRACE_INFO,
                  sync_io_pack_runtime_flags(runtime.running,
                                             runtime.pio_enabled,
                                             runtime.dma_busy,
                                             runtime.dma_irq_enabled,
                                             runtime.tx_fifo_empty,
                                             runtime.tx_fifo_full),
                  runtime.transfer_count & 0xFFFFu);
    sync_io_enc_count_trace_runtime_sample(true);

    return true;
}

void sync_io_enc_count_disarm(void)
{
    if (!s_enc.running) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_enc.sm, false);

    dma_channel_set_irq0_enabled(s_enc.dma_ch, false);
    dma_channel_abort(s_enc.dma_ch);

    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    pio_sm_set_pins(BOARD_SYNC_PIO_WAVE, s_enc.sm, 0);

    /* 释放 4-pin 组 */
    for (uint i = 0u; i < 4u; i++) {
        gpio_set_function(s_enc.in_pin_base + i, GPIO_FUNC_SIO);
        gpio_set_dir(s_enc.in_pin_base + i, GPIO_IN);
        gpio_pull_down(s_enc.in_pin_base + i);
    }

    pio_remove_program(BOARD_SYNC_PIO_WAVE, &enc_count_program,
                       s_enc.offset);

    s_enc.running = false;
    LOG_INFO("sync_io", "enc_count disarmed: fire_count=%lu dma_restarts=%lu",
             (unsigned long)s_enc.fire_count,
             (unsigned long)s_enc.dma_restart_count);
    sync_io_trace(SYNC_IO_TRACE_ENC_DISARM,
                  SYNC_IO_TRACE_INFO,
                  s_enc.fire_count,
                  s_enc.dma_restart_count);
}

uint32_t sync_io_enc_count_get_count(void)
{
    if (!s_enc.running) {
        return 0u;
    }

    /* 通过 pio_sm_exec 注入指令读取 X 寄存器 */
    pio_sm_exec(BOARD_SYNC_PIO_WAVE, s_enc.sm,
                pio_encode_mov(pio_isr, pio_x));
    pio_sm_exec(BOARD_SYNC_PIO_WAVE, s_enc.sm,
                pio_encode_push(false, false));

    if (pio_sm_is_rx_fifo_empty(BOARD_SYNC_PIO_WAVE, s_enc.sm)) {
        return 0u;   /* 不应发生, 但防御 */
    }

    const uint32_t remaining = pio_sm_get(BOARD_SYNC_PIO_WAVE, s_enc.sm);

    if (remaining > s_enc.target) {
        return 0u;
    }
    return s_enc.target - remaining;
}

bool sync_io_enc_count_is_running(void)
{
    return s_enc.running;
}

void sync_io_enc_count_get_runtime(sync_io_enc_count_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    runtime->running = s_enc.running;
    runtime->pio_enabled = false;
    runtime->dma_busy = false;
    runtime->dma_irq_enabled = false;
    runtime->tx_fifo_empty = true;
    runtime->tx_fifo_full = false;
    runtime->transfer_count = 0u;
    runtime->dma_restart_count = s_enc.dma_restart_count;

    if (!s_enc.running) {
        return;
    }

    runtime->pio_enabled = sync_io_sm_is_enabled(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    runtime->dma_busy = dma_channel_is_busy(s_enc.dma_ch);
    runtime->dma_irq_enabled = (dma_hw->inte0 & (1u << s_enc.dma_ch)) != 0u;
    runtime->tx_fifo_empty = pio_sm_is_tx_fifo_empty(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    runtime->tx_fifo_full = pio_sm_is_tx_fifo_full(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    runtime->transfer_count = dma_hw->ch[s_enc.dma_ch].transfer_count;
}

void sync_io_enc_count_trace_runtime_sample(bool force)
{
    sync_io_enc_count_runtime_t runtime;
    sync_io_enc_count_get_runtime(&runtime);

    const uint32_t flags = sync_io_pack_runtime_flags(runtime.running,
                                                      runtime.pio_enabled,
                                                      runtime.dma_busy,
                                                      runtime.dma_irq_enabled,
                                                      runtime.tx_fifo_empty,
                                                      runtime.tx_fifo_full);
    const uint32_t transfer_count = runtime.transfer_count & 0xFFFFu;
    const uint32_t restart_count = runtime.dma_restart_count;
    const bool runtime_changed = !s_enc_trace_sample_valid ||
                                 flags != s_enc_last_runtime_flags ||
                                 transfer_count != s_enc_last_transfer_count;
    const bool restart_changed = !s_enc_trace_sample_valid ||
                                 restart_count != s_enc_last_dma_restart_count;
    const uint32_t restart_delta = s_enc_trace_sample_valid
        ? (uint32_t)(restart_count - s_enc_last_dma_restart_count)
        : 0u;
    const bool overflow_detected =
        restart_delta > SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD;

    if (force || runtime_changed) {
        sync_io_trace(SYNC_IO_TRACE_ENC_PIO_STATE,
                      runtime.running ? SYNC_IO_TRACE_INFO : SYNC_IO_TRACE_WARN,
                      sync_io_pack_pio_state(s_enc.sm,
                                             s_enc.offset,
                                             runtime.pio_enabled,
                                             runtime.tx_fifo_empty,
                                             runtime.tx_fifo_full),
                      transfer_count);
    }

    if (force || restart_changed) {
        sync_io_trace(SYNC_IO_TRACE_ENC_DMA_RESTART,
                      SYNC_IO_TRACE_INFO,
                      restart_count,
                      transfer_count);
    }

    if (force || overflow_detected || s_enc_dma_overflow_latched) {
        if (overflow_detected) {
            s_enc_dma_overflow_latched = true;
        }
        sync_io_trace(SYNC_IO_TRACE_ENC_DMA_OVERFLOW,
                      overflow_detected ? SYNC_IO_TRACE_WARN : SYNC_IO_TRACE_INFO,
                      restart_count,
                      ((restart_delta & 0xFFFFu) << 16) |
                          (SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD & 0xFFFFu));
    }

    s_enc_last_runtime_flags = flags;
    s_enc_last_transfer_count = transfer_count;
    s_enc_last_dma_restart_count = restart_count;
    s_enc_trace_sample_valid = true;
}
