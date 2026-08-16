#include "sync_io.h"

#include <string.h>

#include "board_config.h"
#include "diagnostics.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "sync_io.pio.h"
#include "sync_io_core_internal.h"

#define SYNC_IO_MODEL_PULSE_MAX_ENTRIES 4096u
#define SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY 2u
#define SYNC_IO_MODEL_PULSE_US_TICK_HZ 1000000u
#define SYNC_IO_MODEL_PULSE_DEFAULT_TICK_PERIOD_NS 100u
#define SYNC_IO_MODEL_PULSE_SECTION_OVERHEAD_TICKS 3u

typedef struct {
    bool running;
    bool active_high;
    uint sm;
    uint offset;
    uint dma_ch;
    uint output_pin;
    uint32_t total_pulses;
    uint32_t completed_pulses;
    uint32_t total_duration_us;
    uint32_t total_duration_ns;
    uint32_t tick_period_ns;
    uint32_t fault_code;
    uint64_t start_us;
    uint32_t words[SYNC_IO_MODEL_PULSE_MAX_ENTRIES *
                   SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY];
    uint64_t completion_ns[SYNC_IO_MODEL_PULSE_MAX_ENTRIES];
} sync_io_model_pulse_t;

static sync_io_model_pulse_t s_model_pulse;
static sync_io_model_pulse_entry_ns_t
    s_model_pulse_compat_entries[SYNC_IO_MODEL_PULSE_MAX_ENTRIES];

static float sync_io_model_clkdiv_for_tick_rate(uint32_t tick_hz)
{
    if (tick_hz == 0u) {
        tick_hz = SYNC_IO_MODEL_PULSE_US_TICK_HZ;
    }

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)sys_hz / (float)tick_hz;
    if (clkdiv < 1.0f) {
        clkdiv = 1.0f;
    }
    return clkdiv;
}

static uint32_t sync_io_model_tick_hz_from_period_ns(uint32_t tick_period_ns)
{
    if (tick_period_ns == 0u) {
        tick_period_ns = SYNC_IO_MODEL_PULSE_DEFAULT_TICK_PERIOD_NS;
    }
    const uint64_t hz =
        (1000000000ull + (uint64_t)tick_period_ns - 1ull) /
        (uint64_t)tick_period_ns;
    return hz > UINT32_MAX ? UINT32_MAX : (uint32_t)hz;
}

static uint32_t sync_io_model_ns_to_ticks(uint32_t ns, uint32_t tick_period_ns)
{
    if (ns == 0u || tick_period_ns == 0u) {
        return 0u;
    }
    return (ns + tick_period_ns - 1u) / tick_period_ns;
}

static uint32_t sync_io_model_delay_ticks_for_duration(uint32_t ns,
                                                       uint32_t tick_period_ns)
{
    const uint32_t requested_ticks =
        sync_io_model_ns_to_ticks(ns, tick_period_ns);
    if (requested_ticks <= SYNC_IO_MODEL_PULSE_SECTION_OVERHEAD_TICKS) {
        return 0u;
    }
    return requested_ticks - SYNC_IO_MODEL_PULSE_SECTION_OVERHEAD_TICKS;
}

static uint32_t sync_io_model_high_ticks_for_duration(uint32_t ns,
                                                      uint32_t tick_period_ns)
{
    const uint32_t requested_ticks =
        sync_io_model_ns_to_ticks(ns, tick_period_ns);
    if (requested_ticks <= SYNC_IO_MODEL_PULSE_SECTION_OVERHEAD_TICKS) {
        return 1u;
    }
    return requested_ticks - SYNC_IO_MODEL_PULSE_SECTION_OVERHEAD_TICKS;
}

static uint32_t sync_io_model_delay_word(uint32_t delay_ticks)
{
    return delay_ticks == 0u ? 0u : delay_ticks - 1u;
}

static uint32_t sync_io_model_high_word(uint32_t high_ticks)
{
    return high_ticks <= 1u ? 0u : high_ticks - 1u;
}

static uint32_t sync_io_model_saturate_u64_to_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static bool sync_io_model_output_index_valid(uint32_t output_index)
{
    return output_index < BOARD_DEBUG_MODEL_GPIO_PIN_COUNT;
}

static bool sync_io_main_output_index_valid(uint32_t output_index)
{
    return output_index < BOARD_SYNC_OUTPUT_PIN_COUNT;
}

static void sync_io_model_release_pin(void)
{
    gpio_set_function(s_model_pulse.output_pin, GPIO_FUNC_SIO);
    gpio_put(s_model_pulse.output_pin, false);
    gpio_set_dir(s_model_pulse.output_pin, GPIO_IN);
    gpio_pull_down(s_model_pulse.output_pin);
}

static void sync_io_model_update_completion(void)
{
    if (!s_model_pulse.running) {
        return;
    }

    const uint64_t elapsed_ns =
        (time_us_64() - s_model_pulse.start_us) * 1000ull;
    while (s_model_pulse.completed_pulses < s_model_pulse.total_pulses &&
           elapsed_ns >=
               s_model_pulse.completion_ns[s_model_pulse.completed_pulses]) {
        s_model_pulse.completed_pulses++;
    }

    if (s_model_pulse.completed_pulses >= s_model_pulse.total_pulses &&
        !dma_channel_is_busy(s_model_pulse.dma_ch) &&
        pio_sm_is_tx_fifo_empty(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm)) {
        pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm, false);
        s_model_pulse.running = false;
    }
}

static bool sync_io_pulse_schedule_arm_on_pin(uint32_t output_pin,
                                              uint32_t trace_output_index,
                                              const sync_io_model_pulse_entry_ns_t *entries,
                                              uint32_t entry_count,
                                              bool rising_edge,
                                              uint32_t tick_period_ns)
{
    const uint32_t sanitized_tick_period_ns =
        tick_period_ns != 0u
            ? tick_period_ns
            : SYNC_IO_MODEL_PULSE_DEFAULT_TICK_PERIOD_NS;
    if (!sync_io_core_initialized() ||
        entries == NULL ||
        entry_count == 0u ||
        entry_count > SYNC_IO_MODEL_PULSE_MAX_ENTRIES ||
        sanitized_tick_period_ns == 0u) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           trace_output_index);
        return false;
    }

    sync_io_model_pulse_schedule_disarm();

    uint64_t cumulative_ns = 0u;
    for (uint32_t i = 0u; i < entry_count; i++) {
        const uint32_t delay_ticks =
            sync_io_model_delay_ticks_for_duration(
                entries[i].delay_ns,
                sanitized_tick_period_ns);
        const uint32_t high_ticks =
            sync_io_model_high_ticks_for_duration(
                entries[i].high_ns,
                sanitized_tick_period_ns);
        if (high_ticks == 0u) {
            sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                               SYNC_IO_TRACE_ERROR,
                               i,
                               1u);
            return false;
        }
        s_model_pulse.words[(i * 2u) + 0u] =
            sync_io_model_delay_word(delay_ticks);
        s_model_pulse.words[(i * 2u) + 1u] =
            sync_io_model_high_word(high_ticks);
        cumulative_ns += entries[i].delay_ns;
        cumulative_ns += entries[i].high_ns;
        s_model_pulse.completion_ns[i] = cumulative_ns;
    }

    const pio_program_t *program = rising_edge
        ? &sync_model_sched_pulse_high_program
        : &sync_model_sched_pulse_low_program;

    if (!pio_can_add_program(BOARD_SYNC_PIO_WAVE, program)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           2u);
        return false;
    }

    s_model_pulse.offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, program);
    s_model_pulse.sm = BOARD_SYNC_MODEL_SCHED_SM;
    s_model_pulse.dma_ch = SYNC_IO_MODEL_PULSE_DMA_CH;
    s_model_pulse.output_pin = output_pin;
    s_model_pulse.active_high = rising_edge;
    s_model_pulse.total_pulses = entry_count;
    s_model_pulse.completed_pulses = 0u;
    s_model_pulse.total_duration_ns =
        sync_io_model_saturate_u64_to_u32(cumulative_ns);
    s_model_pulse.total_duration_us =
        sync_io_model_saturate_u64_to_u32((cumulative_ns + 999ull) / 1000ull);
    s_model_pulse.tick_period_ns = sanitized_tick_period_ns;
    s_model_pulse.fault_code = 0u;

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm);
    pio_sm_restart(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm);

    sync_model_sched_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                                        s_model_pulse.sm,
                                        s_model_pulse.offset,
                                        s_model_pulse.output_pin,
                                        rising_edge,
                                        sync_io_model_clkdiv_for_tick_rate(
                                            sync_io_model_tick_hz_from_period_ns(
                                                sanitized_tick_period_ns)));

    dma_channel_abort(s_model_pulse.dma_ch);
    dma_channel_set_irq0_enabled(s_model_pulse.dma_ch, false);

    dma_channel_config dma_cfg = dma_channel_get_default_config(s_model_pulse.dma_ch);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, DREQ_PIO1_TX0 + s_model_pulse.sm);
    dma_channel_configure(s_model_pulse.dma_ch,
                          &dma_cfg,
                          &BOARD_SYNC_PIO_WAVE->txf[s_model_pulse.sm],
                          s_model_pulse.words,
                          entry_count * SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY,
                          true);

    s_model_pulse.start_us = time_us_64();
    s_model_pulse.running = true;
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm, true);

    LOG_INFO("sync_io", "model pulse schedule armed: count=%lu pin=%lu",
             (unsigned long)entry_count,
             (unsigned long)s_model_pulse.output_pin);
    sync_io_core_trace(SYNC_IO_TRACE_MODEL_ARM,
                       SYNC_IO_TRACE_INFO,
                       entry_count,
                       ((trace_output_index & 0xFFu) << 8) |
                           (rising_edge ? 1u : 0u));
    return true;
}

bool sync_io_model_pulse_schedule_arm(uint32_t output_index,
                                      const sync_io_model_pulse_entry_t *entries,
                                      uint32_t entry_count,
                                      bool rising_edge)
{
    if (!sync_io_model_output_index_valid(output_index)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           output_index);
        return false;
    }
    if (entries == NULL ||
        entry_count == 0u ||
        entry_count > SYNC_IO_MODEL_PULSE_MAX_ENTRIES) {
        return false;
    }
    for (uint32_t i = 0u; i < entry_count; i++) {
        s_model_pulse_compat_entries[i].delay_ns = entries[i].delay_us * 1000u;
        s_model_pulse_compat_entries[i].high_ns = entries[i].high_us * 1000u;
    }

    return sync_io_pulse_schedule_arm_on_pin(
        BOARD_DEBUG_MODEL_GPIO_BASE_PIN + output_index,
        output_index,
        s_model_pulse_compat_entries,
        entry_count,
        rising_edge,
        1000u);
}

bool sync_io_model_pulse_schedule_arm_ns(
    uint32_t output_index,
    const sync_io_model_pulse_entry_ns_t *entries,
    uint32_t entry_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    if (!sync_io_model_output_index_valid(output_index)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           output_index);
        return false;
    }

    return sync_io_pulse_schedule_arm_on_pin(
        BOARD_DEBUG_MODEL_GPIO_BASE_PIN + output_index,
        output_index,
        entries,
        entry_count,
        rising_edge,
        tick_period_ns);
}

bool sync_io_output_pulse_schedule_arm(uint32_t output_index,
                                       const sync_io_model_pulse_entry_t *entries,
                                       uint32_t entry_count,
                                       bool rising_edge)
{
    if (!sync_io_main_output_index_valid(output_index)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           output_index);
        return false;
    }
    if (entries == NULL ||
        entry_count == 0u ||
        entry_count > SYNC_IO_MODEL_PULSE_MAX_ENTRIES) {
        return false;
    }
    for (uint32_t i = 0u; i < entry_count; i++) {
        s_model_pulse_compat_entries[i].delay_ns = entries[i].delay_us * 1000u;
        s_model_pulse_compat_entries[i].high_ns = entries[i].high_us * 1000u;
    }

    return sync_io_pulse_schedule_arm_on_pin(
        BOARD_SYNC_OUTPUT_BASE_PIN + output_index,
        output_index,
        s_model_pulse_compat_entries,
        entry_count,
        rising_edge,
        1000u);
}

bool sync_io_output_pulse_schedule_arm_ns(
    uint32_t output_index,
    const sync_io_model_pulse_entry_ns_t *entries,
    uint32_t entry_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    if (!sync_io_main_output_index_valid(output_index)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           output_index);
        return false;
    }

    return sync_io_pulse_schedule_arm_on_pin(
        BOARD_SYNC_OUTPUT_BASE_PIN + output_index,
        output_index,
        entries,
        entry_count,
        rising_edge,
        tick_period_ns);
}

void sync_io_model_pulse_schedule_disarm(void)
{
    if (s_model_pulse.running) {
        pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm, false);
    }

    if (s_model_pulse.offset != 0u || s_model_pulse.total_pulses != 0u) {
        dma_channel_abort(s_model_pulse.dma_ch == 0u
                              ? SYNC_IO_MODEL_PULSE_DMA_CH
                              : s_model_pulse.dma_ch);
        pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE,
                           s_model_pulse.sm == 0u
                               ? BOARD_SYNC_MODEL_SCHED_SM
                               : s_model_pulse.sm);
        pio_sm_set_pins(BOARD_SYNC_PIO_WAVE,
                        s_model_pulse.sm == 0u
                            ? BOARD_SYNC_MODEL_SCHED_SM
                            : s_model_pulse.sm,
                        0u);
        const pio_program_t *program = s_model_pulse.active_high
            ? &sync_model_sched_pulse_high_program
            : &sync_model_sched_pulse_low_program;
        pio_remove_program(BOARD_SYNC_PIO_WAVE, program, s_model_pulse.offset);
        sync_io_model_release_pin();
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_DISARM,
                           SYNC_IO_TRACE_INFO,
                           s_model_pulse.completed_pulses,
                           s_model_pulse.total_pulses);
    }

    memset(&s_model_pulse, 0, sizeof(s_model_pulse));
}

bool sync_io_model_pulse_schedule_is_running(void)
{
    sync_io_model_update_completion();
    return s_model_pulse.running;
}

void sync_io_model_pulse_schedule_get_runtime(sync_io_model_pulse_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    sync_io_model_update_completion();

    memset(runtime, 0, sizeof(*runtime));
    runtime->running = s_model_pulse.running;
    runtime->total_pulses = s_model_pulse.total_pulses;
    runtime->completed_pulses = s_model_pulse.completed_pulses;
    runtime->fault_code = s_model_pulse.fault_code;

    if (s_model_pulse.total_pulses == 0u) {
        runtime->tx_fifo_empty = true;
        return;
    }

    const uint64_t elapsed64 = time_us_64() - s_model_pulse.start_us;
    runtime->elapsed_us = elapsed64 > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed64;
    runtime->pio_enabled =
        sync_io_core_sm_is_enabled(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm);
    runtime->dma_busy = dma_channel_is_busy(s_model_pulse.dma_ch);
    runtime->tx_fifo_empty =
        pio_sm_is_tx_fifo_empty(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm);
    runtime->tx_fifo_full =
        pio_sm_is_tx_fifo_full(BOARD_SYNC_PIO_WAVE, s_model_pulse.sm);
    runtime->transfer_count = dma_hw->ch[s_model_pulse.dma_ch].transfer_count;
}
