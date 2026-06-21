#include "sync_io.h"

#include "board_config.h"
#include "diagnostics.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "sync_io.pio.h"

#define SYNC_IO_DEFAULT_CAPTURE_HZ 1000000u
#define SYNC_IO_DEFAULT_CLOCK_HZ   1000000u
#define SYNC_IO_MIN_HZ             1u

typedef struct {
    bool initialized;
    bool capture_running;
    bool clock_running;
    uint capture_offset;
    uint pulse_offset;
    uint clock_offset;
    uint aux_offset;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t dropped_capture_words;
    sync_io_aux_mode_t aux_modes[SYNC_IO_AUX_COUNT];
} sync_io_context_t;

static sync_io_context_t s_sync_io;

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
        !pio_can_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program)) {
        LOG_ERROR("sync_io", "not enough PIO instruction memory");
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
        return false;
    }

    s_sync_io.capture_offset = (uint)pio_add_program(BOARD_SYNC_PIO_FAST, &sync_capture_4bit_program);
    s_sync_io.pulse_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, &sync_pulse_program);
    s_sync_io.clock_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, &sync_clock_program);
    s_sync_io.aux_offset = (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program);

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

    return true;
}

bool sync_io_start_capture(uint32_t sample_hz)
{
    if (!s_sync_io.initialized) {
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
    return true;
}

void sync_io_stop_capture(void)
{
    if (!s_sync_io.initialized) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    s_sync_io.capture_running = false;
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
    }

    return count;
}

static bool sync_io_fire_pulse_on_sm(uint sm, uint32_t high_cycles)
{
    if (!s_sync_io.initialized || high_cycles == 0u) {
        return false;
    }

    if (pio_sm_is_tx_fifo_full(BOARD_SYNC_PIO_WAVE, sm)) {
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
