#include "sync_io.h"

#include <assert.h>

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
#include "sync_io_hw_profile.h"
#include "sync_io_core_internal.h"
#include "storage_manager.h"
#include "sync_io.pio.h"

#define SYNC_IO_DEFAULT_CAPTURE_HZ  1000000u
#define SYNC_IO_DEFAULT_CLOCK_HZ    1000000u
#define SYNC_IO_MIN_HZ              1u
#define SYNC_IO_TRACE_DOMAIN        3u
#define SYNC_IO_AUX_READY_TIMEOUT_MS 1000u

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
    sync_io_aux_mode_t aux_modes[SYNC_IO_AUX_COUNT];
} sync_io_context_t;

static sync_io_context_t s_sync_io;

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
        !pio_can_add_program(BOARD_SYNC_PIO_AUX, &sync_passthrough_1bit_program) ||
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
    s_sync_io.aux_passthrough_offset =
        (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &sync_passthrough_1bit_program);
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

bool sync_io_fire_rj45_trigger_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_sm(BOARD_SYNC_RJ45_TRIGGER_SM, high_us);
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
