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
#define SYNC_IO_MODEL_PULSE_TICK_HZ 1000000u

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
    uint32_t fault_code;
    uint64_t start_us;
    uint32_t words[SYNC_IO_MODEL_PULSE_MAX_ENTRIES * SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY];
    uint32_t completion_us[SYNC_IO_MODEL_PULSE_MAX_ENTRIES];
} sync_io_model_pulse_t;

static sync_io_model_pulse_t s_model_pulse;

static float sync_io_model_clkdiv_for_tick_rate(uint32_t tick_hz)
{
    if (tick_hz == 0u) {
        tick_hz = SYNC_IO_MODEL_PULSE_TICK_HZ;
    }

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)sys_hz / (float)tick_hz;
    if (clkdiv < 1.0f) {
        clkdiv = 1.0f;
    }
    return clkdiv;
}

static uint32_t sync_io_model_delay_word(uint32_t delay_us)
{
    return delay_us == 0u ? 0u : delay_us - 1u;
}

static uint32_t sync_io_model_high_word(uint32_t high_us)
{
    return high_us <= 1u ? 0u : high_us - 1u;
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

    const uint64_t elapsed64 = time_us_64() - s_model_pulse.start_us;
    const uint32_t elapsed = elapsed64 > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed64;
    while (s_model_pulse.completed_pulses < s_model_pulse.total_pulses &&
           elapsed >= s_model_pulse.completion_us[s_model_pulse.completed_pulses]) {
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
                                              const sync_io_model_pulse_entry_t *entries,
                                              uint32_t entry_count,
                                              bool rising_edge)
{
    if (!sync_io_core_initialized() ||
        entries == NULL ||
        entry_count == 0u ||
        entry_count > SYNC_IO_MODEL_PULSE_MAX_ENTRIES) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           trace_output_index);
        return false;
    }

    sync_io_model_pulse_schedule_disarm();

    uint32_t cumulative_us = 0u;
    for (uint32_t i = 0u; i < entry_count; i++) {
        if (entries[i].high_us == 0u) {
            sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                               SYNC_IO_TRACE_ERROR,
                               i,
                               1u);
            return false;
        }
        s_model_pulse.words[(i * 2u) + 0u] = sync_io_model_delay_word(entries[i].delay_us);
        s_model_pulse.words[(i * 2u) + 1u] = sync_io_model_high_word(entries[i].high_us);
        cumulative_us += entries[i].delay_us + entries[i].high_us;
        s_model_pulse.completion_us[i] = cumulative_us;
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
    s_model_pulse.total_duration_us = cumulative_us;
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
                                            SYNC_IO_MODEL_PULSE_TICK_HZ));

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

    return sync_io_pulse_schedule_arm_on_pin(
        BOARD_DEBUG_MODEL_GPIO_BASE_PIN + output_index,
        output_index,
        entries,
        entry_count,
        rising_edge);
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

    return sync_io_pulse_schedule_arm_on_pin(
        BOARD_SYNC_OUTPUT_BASE_PIN + output_index,
        output_index,
        entries,
        entry_count,
        rising_edge);
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
