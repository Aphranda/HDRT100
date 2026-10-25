#include "sync_io_internal.h"

static bool sync_io_fire_pulse_ns(uint32_t output_index,
                                  uint64_t high_ns,
                                  uint32_t tick_period_ns)
{
    if (!s_sync_io.initialized || high_ns == 0u || high_ns > UINT32_MAX ||
        tick_period_ns == 0u) {
        sync_io_trace(SYNC_IO_TRACE_PULSE_INVALID,
                      SYNC_IO_TRACE_WARN,
                      output_index,
                      high_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)high_ns);
        return false;
    }

    const sync_io_model_pulse_entry_ns_t pulse = {
        .delay_ns = 0u,
        .high_ns = (uint32_t)high_ns,
    };
    return sync_io_output_pulse_schedule_arm_ns(output_index,
                                                &pulse,
                                                1u,
                                                true,
                                                tick_period_ns);
}

static bool sync_io_fire_pulse_cycles_on_output(uint32_t output_index,
                                                uint32_t high_cycles)
{
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    if (high_cycles == 0u || sys_hz == 0u) {
        return false;
    }
    const uint64_t high_ns =
        (((uint64_t)high_cycles * 1000000000ull) + sys_hz - 1u) / sys_hz;
    const uint32_t tick_period_ns =
        (uint32_t)((1000000000ull + sys_hz - 1u) / sys_hz);
    return sync_io_fire_pulse_ns(output_index, high_ns, tick_period_ns);
}

static bool sync_io_fire_pulse_us_on_output(uint32_t output_index,
                                            uint32_t high_us)
{
    return sync_io_fire_pulse_ns(output_index,
                                 (uint64_t)high_us * 1000ull,
                                 100u);
}

bool sync_io_fire_pulse_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_cycles_on_output(0u, high_cycles);
}

bool sync_io_fire_pulse_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_output(0u, high_us);
}

bool sync_io_fire_pulse_out_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_cycles_on_output(1u, high_cycles);
}

bool sync_io_fire_pulse_out_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_output(1u, high_us);
}

bool sync_io_start_clock(uint32_t frequency_hz)
{
#if !BOARD_SYNC_AUX_ENABLED
    (void)frequency_hz;
    return false;
#else
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
#endif
}

void sync_io_stop_clock(void)
{
#if !BOARD_SYNC_AUX_ENABLED
    return;
#else
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
#endif
}

bool sync_io_fire_marker_cycles(uint32_t high_cycles)
{
#if !BOARD_SYNC_RJ45_TRIGGER_ENABLED
    (void)high_cycles;
    return false;
#else
    return sync_io_fire_pulse_cycles_on_output(3u, high_cycles);
#endif
}

bool sync_io_fire_marker_us(uint32_t high_us)
{
    return sync_io_fire_rj45_trigger_us(high_us);
}

bool sync_io_fire_rj45_trigger_us(uint32_t high_us)
{
#if !BOARD_SYNC_RJ45_TRIGGER_ENABLED
    (void)high_us;
    return false;
#else
    return sync_io_fire_pulse_us_on_output(3u, high_us);
#endif
}

bool sync_io_debug_set_output_mask_owned(uint32_t mask)
{
    if (!s_sync_io.initialized ||
        sync_io_core_wave_output_persona_active_owned()) {
        return false;
    }

    const uint32_t valid_mask = (1u << BOARD_SYNC_OUTPUT_PIN_COUNT) - 1u;
    mask &= valid_mask;

    for (uint pin = 0u; pin < BOARD_SYNC_OUTPUT_PIN_COUNT; pin++) {
        const uint gpio = BOARD_SYNC_OUTPUT_BASE_PIN + pin;
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, (mask & (1u << pin)) != 0u);
    }

    return true;
}

bool sync_io_debug_set_output_mask(uint32_t mask)
{
    if (!sync_io_core_legacy_try_enter()) return false;
    const bool result = sync_io_debug_set_output_mask_owned(mask);
    sync_io_core_legacy_leave();
    return result;
}

void sync_io_debug_release_output_mask_owned(void)
{
    if (!s_sync_io.initialized ||
        sync_io_core_wave_output_persona_active_owned()) {
        return;
    }

    for (uint pin = 0u; pin < BOARD_SYNC_OUTPUT_PIN_COUNT; pin++) {
        const uint gpio = BOARD_SYNC_OUTPUT_BASE_PIN + pin;
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, false);
    }

}

void sync_io_debug_release_output_mask(void)
{
    if (!sync_io_core_legacy_try_enter()) return;
    sync_io_debug_release_output_mask_owned();
    sync_io_core_legacy_leave();
}

uint32_t sync_io_debug_read_input_mask(void)
{
    uint32_t mask = 0u;
    for (uint pin = 0u; pin < BOARD_SYNC_INPUT_PIN_COUNT; pin++) {
        if (gpio_get(BOARD_SYNC_INPUT_BASE_PIN + pin)) {
            mask |= (1u << pin);
        }
    }
    return sync_io_map_input_mask(mask);
}

static uint32_t sync_io_sma_public_input_pin(uint32_t input_channel)
{
    const uint32_t public_index = input_channel - 1u;
#if BOARD_SYNC_INPUT_BITS_REVERSED
    return BOARD_SYNC_INPUT_BASE_PIN +
           (BOARD_SYNC_INPUT_PIN_COUNT - 1u - public_index);
#else
    return BOARD_SYNC_INPUT_BASE_PIN + public_index;
#endif
}

static bool sync_io_sma_frequency_choose_pwm(uint32_t frequency_hz,
                                             uint32_t system_clock_hz,
                                             uint16_t *top,
                                             uint16_t *div16,
                                             uint32_t *actual_hz)
{
    if (frequency_hz == 0u || system_clock_hz == 0u ||
        top == NULL || div16 == NULL || actual_hz == NULL) {
        return false;
    }

    const uint64_t target_product =
        ((uint64_t)system_clock_hz * 16ull + frequency_hz / 2u) /
        frequency_hz;
    uint64_t best_error = UINT64_MAX;
    uint32_t best_period = 0u;
    uint32_t best_div16 = 0u;

    for (uint32_t period = 2u; period <= 65536u; period++) {
        uint64_t candidate =
            (target_product + period / 2u) / period;
        if (candidate < 16u) {
            candidate = 16u;
        }
        if (candidate > 4095u) {
            continue;
        }
        const uint64_t product = (uint64_t)period * candidate;
        const uint64_t error = product > target_product
            ? product - target_product
            : target_product - product;
        if (error < best_error) {
            best_error = error;
            best_period = period;
            best_div16 = (uint32_t)candidate;
            if (error == 0u) {
                break;
            }
        }
    }

    if (best_period == 0u || best_div16 == 0u) {
        return false;
    }
    *top = (uint16_t)(best_period - 1u);
    *div16 = (uint16_t)best_div16;
    *actual_hz = (uint32_t)(((uint64_t)system_clock_hz * 16ull +
                             ((uint64_t)best_period * best_div16) / 2ull) /
                            ((uint64_t)best_period * best_div16));
    return true;
}

void sync_io_sma_frequency_tx_stop_owned(void)
{
    if (!s_sma_frequency_tx.running) {
        return;
    }

    const uint32_t output_pin = s_sma_frequency_tx.output_pin;
    const uint slice = pwm_gpio_to_slice_num(output_pin);
    pwm_set_enabled(slice, false);
    gpio_set_function(output_pin, GPIO_FUNC_SIO);
    gpio_set_dir(output_pin, GPIO_OUT);
    gpio_put(output_pin, false);
    memset(&s_sma_frequency_tx, 0, sizeof(s_sma_frequency_tx));
    sync_io_debug_release_output_mask_owned();
}

void sync_io_sma_frequency_tx_stop(void)
{
    if (!sync_io_core_legacy_try_enter()) return;
    sync_io_sma_frequency_tx_stop_owned();
    sync_io_core_legacy_leave();
}

bool sync_io_sma_frequency_tx_start_owned(
    uint32_t output_channel,
    uint32_t frequency_hz,
    sync_io_sma_frequency_tx_status_t *status)
{
    if (!s_sync_io.initialized ||
        output_channel == 0u ||
        output_channel > BOARD_SYNC_OUTPUT_PIN_COUNT ||
        frequency_hz < SYNC_IO_SMA_FREQUENCY_MIN_HZ ||
        frequency_hz > SYNC_IO_SMA_FREQUENCY_MAX_HZ ||
        s_sync_io.capture_running ||
        sync_io_seq_step_is_running() ||
        sync_io_enc_count_is_running() ||
        sync_io_core_model_output_active() ||
        sync_io_core_wave_output_persona_active_owned()) {
        return false;
    }

    const uint32_t output_pin =
        BOARD_SYNC_OUTPUT_BASE_PIN + output_channel - 1u;
    const uint32_t system_clock_hz = clock_get_hz(clk_sys);
    uint16_t top = 0u;
    uint16_t div16 = 0u;
    uint32_t actual_hz = 0u;
    if (!sync_io_sma_frequency_choose_pwm(frequency_hz,
                                          system_clock_hz,
                                          &top,
                                          &div16,
                                          &actual_hz)) {
        return false;
    }

    sync_io_sma_frequency_tx_stop_owned();
    if (!sync_io_debug_set_output_mask_owned(0u)) {
        return false;
    }

    const uint slice = pwm_gpio_to_slice_num(output_pin);
    const uint channel = pwm_gpio_to_channel(output_pin);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, top);
    pwm_config_set_clkdiv_int_frac4(&config,
                                    div16 >> 4u,
                                    (uint8_t)(div16 & 0x0Fu));
    pwm_init(slice, &config, false);
    gpio_set_function(output_pin, GPIO_FUNC_PWM);
    pwm_set_chan_level(slice, channel, (uint16_t)(((uint32_t)top + 1u) / 2u));
    pwm_set_enabled(slice, true);

    s_sma_frequency_tx.running = true;
    s_sma_frequency_tx.output_channel = output_channel;
    s_sma_frequency_tx.output_pin = output_pin;
    s_sma_frequency_tx.requested_hz = frequency_hz;
    s_sma_frequency_tx.actual_hz = actual_hz;
    s_sma_frequency_tx.system_clock_hz = system_clock_hz;
    s_sma_frequency_tx.pwm_top = top;
    s_sma_frequency_tx.pwm_div16 = div16;
    if (status != NULL) {
        *status = s_sma_frequency_tx;
    }
    return true;
}

bool sync_io_sma_frequency_tx_start(
    uint32_t output_channel,
    uint32_t frequency_hz,
    sync_io_sma_frequency_tx_status_t *status)
{
    if (!sync_io_core_legacy_try_enter()) return false;
    const bool result = sync_io_sma_frequency_tx_start_owned(output_channel, frequency_hz, status);
    sync_io_core_legacy_leave();
    return result;
}

bool sync_io_core_sma_frequency_output_active(void)
{
    return s_sma_frequency_tx.running;
}

void sync_io_sma_frequency_tx_get_status(
    sync_io_sma_frequency_tx_status_t *status)
{
    if (status != NULL) {
        *status = s_sma_frequency_tx;
    }
}

bool sync_io_sma_frequency_rx_measure_owned(
    uint32_t input_channel,
    uint32_t gate_us,
    sync_io_sma_frequency_rx_result_t *result)
{
    if (!s_sync_io.initialized || result == NULL ||
        input_channel == 0u ||
        input_channel > BOARD_SYNC_INPUT_PIN_COUNT ||
        gate_us < SYNC_IO_SMA_FREQUENCY_MIN_GATE_US ||
        gate_us > SYNC_IO_SMA_FREQUENCY_MAX_GATE_US ||
        s_sync_io.capture_running) {
        return false;
    }

    const uint32_t input_pin = sync_io_sma_public_input_pin(input_channel);
    if (pwm_gpio_to_channel(input_pin) != PWM_CHAN_B) {
        return false;
    }
    const uint slice = pwm_gpio_to_slice_num(input_pin);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&config, PWM_DIV_B_RISING);
    pwm_config_set_clkdiv_int(&config, 1u);
    pwm_config_set_wrap(&config, UINT16_MAX);

    gpio_set_function(input_pin, GPIO_FUNC_PWM);
    pwm_init(slice, &config, false);
    pwm_set_counter(slice, 0u);
    /* Keep the 16-bit edge counter inside one exact, non-preempted gate.
     * At 50 MHz a 100 us gate contains only 5000 edges, well below wrap.
     * Without this local IRQ guard, an RTOS/timer ISR can extend the gate by
     * milliseconds and make the 16-bit counter wrap, which looks like link
     * loss even though the physical input is healthy. */
    const uint32_t interrupt_state = save_and_disable_interrupts();
    const uint64_t started_us = time_us_64();
    pwm_set_enabled(slice, true);
    busy_wait_us_32(gate_us);
    pwm_set_enabled(slice, false);
    const uint64_t finished_us = time_us_64();
    const uint32_t edge_count = pwm_get_counter(slice);
    restore_interrupts(interrupt_state);

    gpio_set_function(input_pin, GPIO_FUNC_PIO0);
    gpio_set_dir(input_pin, GPIO_IN);
    gpio_pull_down(input_pin);

    uint64_t elapsed_us = finished_us - started_us;
    if (elapsed_us == 0u) {
        elapsed_us = gate_us;
    }
    memset(result, 0, sizeof(*result));
    result->input_channel = input_channel;
    result->input_pin = input_pin;
    result->gate_us = gate_us;
    result->elapsed_us = elapsed_us > UINT32_MAX
        ? UINT32_MAX
        : (uint32_t)elapsed_us;
    result->edge_count = edge_count;
    result->frequency_hz = (uint32_t)(
        ((uint64_t)edge_count * 1000000ull + elapsed_us / 2ull) /
        elapsed_us);
    return true;
}

bool sync_io_sma_frequency_rx_measure(
    uint32_t input_channel,
    uint32_t gate_us,
    sync_io_sma_frequency_rx_result_t *result)
{
    if (!sync_io_core_legacy_try_enter()) return false;
    const bool accepted = sync_io_sma_frequency_rx_measure_owned(input_channel, gate_us, result);
    sync_io_core_legacy_leave();
    return accepted;
}

bool sync_io_debug_model_set_output_mask(uint32_t enable_mask, uint32_t value_mask)
{
#if !BOARD_DEBUG_MODEL_GPIO_ENABLED
    (void)enable_mask;
    (void)value_mask;
    return false;
#else
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
#endif
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
#if !BOARD_DEBUG_MODEL_GPIO_ENABLED
    return;
#else
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
#endif
}

uint32_t sync_io_debug_model_read_input_mask(void)
{
#if !BOARD_DEBUG_MODEL_GPIO_ENABLED
    return 0u;
#else
    uint32_t mask = 0u;
    for (uint pin = 0u; pin < BOARD_DEBUG_MODEL_GPIO_PIN_COUNT; pin++) {
        if (gpio_get(BOARD_DEBUG_MODEL_GPIO_BASE_PIN + pin)) {
            mask |= (1u << pin);
        }
    }
    return mask;
#endif
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
    status->capture_timestamp_window_armed =
        s_sync_io.capture_timestamp_window_armed ? 1u : 0u;
    status->capture_timestamp_window_periodic =
        s_sync_io.capture_timestamp_window_periodic ? 1u : 0u;
    status->capture_timestamp_window_start_lo =
        (uint32_t)(s_sync_io.capture_timestamp_window_start_ns & 0xFFFFFFFFull);
    status->capture_timestamp_window_start_hi =
        (uint32_t)(s_sync_io.capture_timestamp_window_start_ns >> 32u);
    status->capture_timestamp_window_end_lo =
        (uint32_t)(s_sync_io.capture_timestamp_window_end_ns & 0xFFFFFFFFull);
    status->capture_timestamp_window_end_hi =
        (uint32_t)(s_sync_io.capture_timestamp_window_end_ns >> 32u);
    status->capture_timestamp_window_period_ns =
        s_sync_io.capture_timestamp_window_period_ns;
    status->capture_timestamp_window_sample_period_ns =
        s_sync_io.capture_timestamp_window_sample_period_ns;
    status->capture_timestamp_window_observed_mask =
        s_sync_io.capture_timestamp_window_observed_mask;
    status->capture_timestamp_window_initial_sample_mask =
        s_sync_io.capture_timestamp_window_initial_sample_mask;
    status->capture_timebase_valid =
        s_sync_io.capture_timebase_valid ? 1u : 0u;
    status->capture_timebase_start_lo =
        (uint32_t)(s_sync_io.capture_timebase_start_ns & 0xFFFFFFFFull);
    status->capture_timebase_start_hi =
        (uint32_t)(s_sync_io.capture_timebase_start_ns >> 32u);
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
