#include "sma_cable_delay_pio.h"

#include <string.h>

#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "resource_arbiter.h"
#include "sma_cable_delay.pio.h"

#define SMA_CABLE_DELAY_PIO_INSTANCE BOARD_SYNC_PIO_FAST
#define SMA_CABLE_DELAY_PIO_RESOURCE_MASK                                  \
    (RESOURCE_ARBITER_RESOURCE_PIO0 | RESOURCE_ARBITER_RESOURCE_DMA)
#define SMA_CABLE_DELAY_PIO_OWNER "sma_cable_delay"
#define SMA_CABLE_DELAY_PIO_MIN_HALF_PERIOD_CYCLES 3u

typedef struct {
    bool open;
    bool capture_active;
    bool resources_owned;
    bool source_program_added;
    bool capture_program_added;
    bool pins_configured;
    bool pins_snapshotted;
    int source_sm;
    int capture_sm;
    int dma_channel;
    uint source_offset;
    uint capture_offset;
    uint output_pin;
    uint input_base_pin;
    bool reverse_input_bits;
    gpio_function_t output_function;
    bool output_sio_dir_out;
    bool output_sio_level;
    bool output_pull_up;
    bool output_pull_down;
    gpio_function_t input_functions[SMA_CABLE_DELAY_PIO_INPUT_COUNT];
    bool input_sio_dir_out[SMA_CABLE_DELAY_PIO_INPUT_COUNT];
    bool input_sio_level[SMA_CABLE_DELAY_PIO_INPUT_COUNT];
    bool input_pull_up[SMA_CABLE_DELAY_PIO_INPUT_COUNT];
    bool input_pull_down[SMA_CABLE_DELAY_PIO_INPUT_COUNT];
} sma_cable_delay_pio_context_t;

static sma_cable_delay_pio_context_t s_persona = {
    .source_sm = -1,
    .capture_sm = -1,
    .dma_channel = -1,
};

static void sma_cable_delay_pio_restore_pull(uint pin,
                                             bool pull_up,
                                             bool pull_down)
{
    gpio_set_pulls(pin, pull_up, pull_down);
}

static void sma_cable_delay_pio_snapshot_pins(void)
{
    if (s_persona.pins_snapshotted) {
        return;
    }

    s_persona.output_function = gpio_get_function(s_persona.output_pin);
    s_persona.output_sio_dir_out = gpio_is_dir_out(s_persona.output_pin);
    s_persona.output_sio_level = gpio_get_out_level(s_persona.output_pin);
    s_persona.output_pull_up = gpio_is_pulled_up(s_persona.output_pin);
    s_persona.output_pull_down = gpio_is_pulled_down(s_persona.output_pin);
    for (uint index = 0u;
         index < SMA_CABLE_DELAY_PIO_INPUT_COUNT;
         ++index) {
        const uint pin = s_persona.input_base_pin + index;
        s_persona.input_functions[index] = gpio_get_function(pin);
        s_persona.input_sio_dir_out[index] = gpio_is_dir_out(pin);
        s_persona.input_sio_level[index] = gpio_get_out_level(pin);
        s_persona.input_pull_up[index] = gpio_is_pulled_up(pin);
        s_persona.input_pull_down[index] = gpio_is_pulled_down(pin);
    }
    s_persona.pins_snapshotted = true;
}

static void sma_cable_delay_pio_restore_pins(void)
{
    if (!s_persona.pins_configured || !s_persona.pins_snapshotted ||
        s_persona.output_pin < BOARD_SYNC_OUTPUT_BASE_PIN ||
        s_persona.output_pin >=
            BOARD_SYNC_OUTPUT_BASE_PIN + BOARD_SYNC_OUTPUT_PIN_COUNT) {
        return;
    }

    gpio_set_function(s_persona.output_pin, s_persona.output_function);
    sma_cable_delay_pio_restore_pull(s_persona.output_pin,
                                     s_persona.output_pull_up,
                                     s_persona.output_pull_down);
    if (s_persona.output_function == GPIO_FUNC_SIO) {
        gpio_put(s_persona.output_pin, s_persona.output_sio_level);
        gpio_set_dir(s_persona.output_pin, s_persona.output_sio_dir_out);
    }

    for (uint index = 0u;
         index < SMA_CABLE_DELAY_PIO_INPUT_COUNT;
         ++index) {
        const uint pin = s_persona.input_base_pin + index;
        gpio_set_function(pin, s_persona.input_functions[index]);
        sma_cable_delay_pio_restore_pull(pin,
                                         s_persona.input_pull_up[index],
                                         s_persona.input_pull_down[index]);
        if (s_persona.input_functions[index] == GPIO_FUNC_SIO) {
            gpio_put(pin, s_persona.input_sio_level[index]);
            gpio_set_dir(pin, s_persona.input_sio_dir_out[index]);
        }
    }
}

void sma_cable_delay_pio_close(void)
{
    if (s_persona.source_sm >= 0) {
        pio_sm_set_enabled(SMA_CABLE_DELAY_PIO_INSTANCE,
                           (uint)s_persona.source_sm,
                           false);
        pio_sm_clear_fifos(SMA_CABLE_DELAY_PIO_INSTANCE,
                           (uint)s_persona.source_sm);
        pio_sm_set_pins(SMA_CABLE_DELAY_PIO_INSTANCE,
                        (uint)s_persona.source_sm,
                        0u);
    }
    if (s_persona.capture_sm >= 0) {
        pio_sm_set_enabled(SMA_CABLE_DELAY_PIO_INSTANCE,
                           (uint)s_persona.capture_sm,
                           false);
        pio_sm_clear_fifos(SMA_CABLE_DELAY_PIO_INSTANCE,
                           (uint)s_persona.capture_sm);
    }
    if (s_persona.dma_channel >= 0) {
        dma_channel_abort((uint)s_persona.dma_channel);
    }

    sma_cable_delay_pio_restore_pins();

    if (s_persona.capture_program_added) {
        pio_remove_program(SMA_CABLE_DELAY_PIO_INSTANCE,
                           &sma_cable_delay_capture_program,
                           s_persona.capture_offset);
    }
    if (s_persona.source_program_added) {
        pio_remove_program(SMA_CABLE_DELAY_PIO_INSTANCE,
                           &sma_cable_delay_source_program,
                           s_persona.source_offset);
    }
    if (s_persona.dma_channel >= 0) {
        dma_channel_unclaim((uint)s_persona.dma_channel);
    }
    if (s_persona.capture_sm >= 0) {
        pio_sm_unclaim(SMA_CABLE_DELAY_PIO_INSTANCE,
                       (uint)s_persona.capture_sm);
    }
    if (s_persona.source_sm >= 0) {
        pio_sm_unclaim(SMA_CABLE_DELAY_PIO_INSTANCE,
                       (uint)s_persona.source_sm);
    }
    if (s_persona.resources_owned) {
        resource_arbiter_release_owned(SMA_CABLE_DELAY_PIO_RESOURCE_MASK,
                                       SMA_CABLE_DELAY_PIO_OWNER);
    }

    memset(&s_persona, 0, sizeof(s_persona));
    s_persona.source_sm = -1;
    s_persona.capture_sm = -1;
    s_persona.dma_channel = -1;
}

sma_cable_delay_pio_status_t sma_cable_delay_pio_open(
    const sma_cable_delay_pio_config_t *config)
{
    if (config == NULL ||
        config->output_index >= BOARD_SYNC_OUTPUT_PIN_COUNT ||
        config->input_base_pin > 26u) {
        return SMA_CABLE_DELAY_PIO_INVALID_ARGUMENT;
    }
    if (s_persona.open || s_persona.capture_active) {
        return SMA_CABLE_DELAY_PIO_BUSY;
    }

    s_persona.output_pin = BOARD_SYNC_OUTPUT_BASE_PIN + config->output_index;
    s_persona.input_base_pin = config->input_base_pin;
    s_persona.reverse_input_bits = config->reverse_input_bits;

    if (!resource_arbiter_acquire_owned(SMA_CABLE_DELAY_PIO_RESOURCE_MASK,
                                        SMA_CABLE_DELAY_PIO_OWNER)) {
        sma_cable_delay_pio_close();
        return SMA_CABLE_DELAY_PIO_RESOURCE_CONFLICT;
    }
    s_persona.resources_owned = true;

    s_persona.source_sm =
        pio_claim_unused_sm(SMA_CABLE_DELAY_PIO_INSTANCE, false);
    if (s_persona.source_sm < 0) {
        sma_cable_delay_pio_close();
        return SMA_CABLE_DELAY_PIO_NO_STATE_MACHINE;
    }
    s_persona.capture_sm =
        pio_claim_unused_sm(SMA_CABLE_DELAY_PIO_INSTANCE, false);
    if (s_persona.capture_sm < 0) {
        sma_cable_delay_pio_close();
        return SMA_CABLE_DELAY_PIO_NO_STATE_MACHINE;
    }

    s_persona.dma_channel = dma_claim_unused_channel(false);
    if (s_persona.dma_channel < 0) {
        sma_cable_delay_pio_close();
        return SMA_CABLE_DELAY_PIO_NO_DMA_CHANNEL;
    }

    if (!pio_can_add_program(SMA_CABLE_DELAY_PIO_INSTANCE,
                             &sma_cable_delay_source_program)) {
        sma_cable_delay_pio_close();
        return SMA_CABLE_DELAY_PIO_NO_INSTRUCTION_SPACE;
    }
    s_persona.source_offset =
        pio_add_program(SMA_CABLE_DELAY_PIO_INSTANCE,
                        &sma_cable_delay_source_program);
    s_persona.source_program_added = true;

    if (!pio_can_add_program(SMA_CABLE_DELAY_PIO_INSTANCE,
                             &sma_cable_delay_capture_program)) {
        sma_cable_delay_pio_close();
        return SMA_CABLE_DELAY_PIO_NO_INSTRUCTION_SPACE;
    }
    s_persona.capture_offset =
        pio_add_program(SMA_CABLE_DELAY_PIO_INSTANCE,
                        &sma_cable_delay_capture_program);
    s_persona.capture_program_added = true;
    s_persona.open = true;
    return SMA_CABLE_DELAY_PIO_OK;
}

sma_cable_delay_pio_status_t sma_cable_delay_pio_capture_frequency(
    uint32_t frequency_hz,
    uint32_t *capture_words,
    size_t capture_word_count,
    uint32_t timeout_us,
    sma_cable_delay_pio_capture_t *capture)
{
    if (!s_persona.open) {
        return SMA_CABLE_DELAY_PIO_NOT_OPEN;
    }
    if (s_persona.capture_active) {
        return SMA_CABLE_DELAY_PIO_BUSY;
    }
    if (frequency_hz == 0u || capture_words == NULL || capture == NULL ||
        capture_word_count < SMA_CABLE_DELAY_PIO_MIN_CAPTURE_WORDS ||
        capture_word_count > UINT32_MAX || timeout_us == 0u) {
        return SMA_CABLE_DELAY_PIO_INVALID_ARGUMENT;
    }

    const uint32_t system_hz = clock_get_hz(clk_sys);
    const uint64_t divisor = (uint64_t)frequency_hz * 2ull;
    const uint32_t half_period_cycles =
        (uint32_t)(((uint64_t)system_hz + divisor / 2ull) / divisor);
    if (half_period_cycles < SMA_CABLE_DELAY_PIO_MIN_HALF_PERIOD_CYCLES ||
        half_period_cycles > 65535u) {
        return SMA_CABLE_DELAY_PIO_FREQUENCY_OUT_OF_RANGE;
    }

    const uint32_t actual_frequency_hz =
        system_hz / (2u * half_period_cycles);
    memset(capture, 0, sizeof(*capture));
    capture->requested_frequency_hz = frequency_hz;
    capture->actual_frequency_hz = actual_frequency_hz;
    capture->sample_rate_hz = system_hz;
    capture->period_samples = 2u * half_period_cycles;
    capture->captured_word_count = (uint32_t)capture_word_count;
    capture->reverse_input_bits = s_persona.reverse_input_bits;

    const uint source_sm = (uint)s_persona.source_sm;
    const uint capture_sm = (uint)s_persona.capture_sm;
    const uint dma_channel = (uint)s_persona.dma_channel;
    pio_sm_set_enabled(SMA_CABLE_DELAY_PIO_INSTANCE, source_sm, false);
    pio_sm_set_enabled(SMA_CABLE_DELAY_PIO_INSTANCE, capture_sm, false);
    pio_sm_clear_fifos(SMA_CABLE_DELAY_PIO_INSTANCE, source_sm);
    pio_sm_clear_fifos(SMA_CABLE_DELAY_PIO_INSTANCE, capture_sm);
    pio_sm_restart(SMA_CABLE_DELAY_PIO_INSTANCE, source_sm);
    pio_sm_restart(SMA_CABLE_DELAY_PIO_INSTANCE, capture_sm);

    sma_cable_delay_pio_snapshot_pins();
    sma_cable_delay_source_program_init(SMA_CABLE_DELAY_PIO_INSTANCE,
                                        source_sm,
                                        s_persona.source_offset,
                                        s_persona.output_pin,
                                        (float)half_period_cycles);
    sma_cable_delay_capture_program_init(SMA_CABLE_DELAY_PIO_INSTANCE,
                                         capture_sm,
                                         s_persona.capture_offset,
                                         s_persona.input_base_pin);
    s_persona.pins_configured = true;

    dma_channel_abort(dma_channel);
    dma_channel_config dma_config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, true);
    channel_config_set_dreq(
        &dma_config,
        pio_get_dreq(SMA_CABLE_DELAY_PIO_INSTANCE, capture_sm, false));
    dma_channel_configure(
        dma_channel,
        &dma_config,
        capture_words,
        &SMA_CABLE_DELAY_PIO_INSTANCE->rxf[capture_sm],
        (uint32_t)capture_word_count,
        false);

    s_persona.capture_active = true;
    dma_start_channel_mask(1u << dma_channel);
    pio_enable_sm_mask_in_sync(SMA_CABLE_DELAY_PIO_INSTANCE,
                               (1u << source_sm) | (1u << capture_sm));

    const uint64_t deadline_us = time_us_64() + timeout_us;
    while (dma_channel_is_busy(dma_channel) && time_us_64() < deadline_us) {
        tight_loop_contents();
    }

    pio_sm_set_enabled(SMA_CABLE_DELAY_PIO_INSTANCE, source_sm, false);
    pio_sm_set_enabled(SMA_CABLE_DELAY_PIO_INSTANCE, capture_sm, false);
    pio_sm_set_pins(SMA_CABLE_DELAY_PIO_INSTANCE, source_sm, 0u);
    s_persona.capture_active = false;

    if (dma_channel_is_busy(dma_channel)) {
        dma_channel_abort(dma_channel);
        return SMA_CABLE_DELAY_PIO_CAPTURE_TIMEOUT;
    }
    return SMA_CABLE_DELAY_PIO_OK;
}

bool sma_cable_delay_pio_is_open(void)
{
    return s_persona.open;
}

const char *sma_cable_delay_pio_status_string(sma_cable_delay_pio_status_t status)
{
    switch (status) {
    case SMA_CABLE_DELAY_PIO_OK:
        return "ok";
    case SMA_CABLE_DELAY_PIO_INVALID_ARGUMENT:
        return "invalid_argument";
    case SMA_CABLE_DELAY_PIO_RESOURCE_CONFLICT:
        return "resource_conflict";
    case SMA_CABLE_DELAY_PIO_NO_STATE_MACHINE:
        return "no_state_machine";
    case SMA_CABLE_DELAY_PIO_NO_DMA_CHANNEL:
        return "no_dma_channel";
    case SMA_CABLE_DELAY_PIO_NO_INSTRUCTION_SPACE:
        return "no_instruction_space";
    case SMA_CABLE_DELAY_PIO_NOT_OPEN:
        return "not_open";
    case SMA_CABLE_DELAY_PIO_BUSY:
        return "busy";
    case SMA_CABLE_DELAY_PIO_FREQUENCY_OUT_OF_RANGE:
        return "frequency_out_of_range";
    case SMA_CABLE_DELAY_PIO_CAPTURE_TIMEOUT:
        return "capture_timeout";
    default:
        return "unknown";
    }
}
