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
#include "sync_io_persona_manager.h"

#define SYNC_IO_MODEL_PULSE_MAX_ENTRIES 4096u
#define SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY 2u
#define SYNC_IO_MODEL_PULSE_US_TICK_HZ 1000000u
#define SYNC_IO_MODEL_PULSE_DEFAULT_TICK_PERIOD_NS 100u
#define SYNC_IO_MODEL_PULSE_SECTION_OVERHEAD_TICKS 3u

typedef struct {
    bool running;
    bool active_high;
    bool persona_managed;
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
    uint64_t total_duration_ns64;
    uint64_t completed_elapsed_ns;
    PIO pio;
    /* The maintenance schedule shares the capture DMA workspace.  Keep only
     * a pointer in the persona state so the 32 KiB workspace is not duplicated
     * in .bss.  The arm path assigns it after confirming capture is idle. */
    uint32_t *words;
} sync_io_model_pulse_t;

static sync_io_model_pulse_t s_model_pulse;
static sync_io_persona_manager_t s_wave_output_manager;
static sync_io_persona_manager_handle_t s_wave_output_handle;
static bool s_wave_output_manager_initialized;
static bool s_wave_output_manager_active;
static bool s_wave_output_sm_claimed;
static bool s_wave_output_dma_claimed;
static bool s_wave_output_program_loaded;

static float sync_io_model_clkdiv_for_tick_rate(uint32_t tick_hz);
static uint32_t sync_io_model_tick_hz_from_period_ns(uint32_t tick_period_ns);
static void sync_io_model_release_pin(void);

static const pio_program_t *sync_io_pio0_output_program(void)
{
    return s_model_pulse.active_high
        ? &sync_model_sched_pulse_high_program
        : &sync_model_sched_pulse_low_program;
}

static bool sync_io_wave_output_load(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)context;
    const uint expected_sm = descriptor != NULL &&
        descriptor->id == SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER
        ? BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM
        : BOARD_SYNC_PIO0_WAVE_OUTPUT_SM;
    if (descriptor == NULL ||
        (descriptor->id != SYNC_IO_PERSONA_ID_WAVE_OUTPUT &&
         descriptor->id != SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER) ||
        dma_channel_mask != (1u << SYNC_IO_MODEL_PULSE_DMA_CH) ||
        s_model_pulse.pio != BOARD_SYNC_PIO_FAST ||
        s_model_pulse.sm != expected_sm ||
        s_model_pulse.dma_ch != SYNC_IO_MODEL_PULSE_DMA_CH ||
        s_model_pulse.output_pin >= 32u ||
        (descriptor->gpio_write_mask & (1u << s_model_pulse.output_pin)) == 0u ||
        s_wave_output_sm_claimed ||
        s_wave_output_dma_claimed ||
        dma_channel_is_claimed(SYNC_IO_MODEL_PULSE_DMA_CH) ||
        pio_sm_is_claimed(BOARD_SYNC_PIO_FAST,
                          expected_sm) ||
        !pio_can_add_program(BOARD_SYNC_PIO_FAST,
                             sync_io_pio0_output_program())) {
        return false;
    }

    pio_sm_claim(BOARD_SYNC_PIO_FAST, expected_sm);
    s_wave_output_sm_claimed = true;
    dma_channel_claim(SYNC_IO_MODEL_PULSE_DMA_CH);
    s_wave_output_dma_claimed = true;
    s_model_pulse.offset = (uint)pio_add_program(
        BOARD_SYNC_PIO_FAST,
        sync_io_pio0_output_program());
    s_wave_output_program_loaded = true;
    return true;
}

static bool sync_io_wave_output_arm(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)context;
    if (descriptor == NULL ||
        (descriptor->id != SYNC_IO_PERSONA_ID_WAVE_OUTPUT &&
         descriptor->id != SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER) ||
        dma_channel_mask != (1u << s_model_pulse.dma_ch) ||
        !s_wave_output_program_loaded ||
        s_model_pulse.words == NULL ||
        s_model_pulse.total_pulses == 0u) {
        return false;
    }

    pio_sm_set_enabled(s_model_pulse.pio, s_model_pulse.sm, false);
    pio_sm_clear_fifos(s_model_pulse.pio, s_model_pulse.sm);
    pio_sm_restart(s_model_pulse.pio, s_model_pulse.sm);
    sync_model_sched_pulse_program_init(
        s_model_pulse.pio,
        s_model_pulse.sm,
        s_model_pulse.offset,
        s_model_pulse.output_pin,
        s_model_pulse.active_high,
        sync_io_model_clkdiv_for_tick_rate(
            sync_io_model_tick_hz_from_period_ns(
                s_model_pulse.tick_period_ns)));

    dma_channel_abort(s_model_pulse.dma_ch);
    dma_channel_set_irq0_enabled(s_model_pulse.dma_ch, false);
    dma_channel_config dma_cfg =
        dma_channel_get_default_config(s_model_pulse.dma_ch);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(
        &dma_cfg,
        pio_get_dreq(s_model_pulse.pio, s_model_pulse.sm, true));
    dma_channel_configure(
        s_model_pulse.dma_ch,
        &dma_cfg,
        &s_model_pulse.pio->txf[s_model_pulse.sm],
        s_model_pulse.words,
        s_model_pulse.total_pulses * SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY,
        false);
    return true;
}

static bool sync_io_wave_output_start(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)context;
    (void)descriptor;
    (void)dma_channel_mask;
    s_model_pulse.start_us = time_us_64();
    s_model_pulse.running = true;
    dma_start_channel_mask(1u << s_model_pulse.dma_ch);
    pio_sm_set_enabled(s_model_pulse.pio, s_model_pulse.sm, true);
    return true;
}

static void sync_io_wave_output_stop(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)context;
    (void)descriptor;
    (void)dma_channel_mask;
    const bool hardware_owned = s_wave_output_sm_claimed ||
                                s_wave_output_dma_claimed ||
                                s_wave_output_program_loaded;
    if (!hardware_owned) {
        s_model_pulse.running = false;
        return;
    }
    if (s_model_pulse.pio != NULL) {
        pio_sm_set_enabled(s_model_pulse.pio, s_model_pulse.sm, false);
        pio_sm_set_pins(s_model_pulse.pio, s_model_pulse.sm, 0u);
    }
    if (s_wave_output_dma_claimed) {
        dma_channel_abort(s_model_pulse.dma_ch);
    }
    s_model_pulse.running = false;
}

static void sync_io_wave_output_cleanup(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)context;
    (void)descriptor;
    (void)dma_channel_mask;
    const bool hardware_owned = s_wave_output_sm_claimed ||
                                s_wave_output_dma_claimed ||
                                s_wave_output_program_loaded;
    sync_io_wave_output_stop(context, descriptor, dma_channel_mask);
    if (hardware_owned && s_model_pulse.pio != NULL) {
        pio_sm_clear_fifos(s_model_pulse.pio, s_model_pulse.sm);
        pio_sm_restart(s_model_pulse.pio, s_model_pulse.sm);
    }
    if (s_wave_output_program_loaded) {
        pio_remove_program(s_model_pulse.pio,
                           sync_io_pio0_output_program(),
                           s_model_pulse.offset);
        s_wave_output_program_loaded = false;
    }
    if (s_wave_output_dma_claimed) {
        dma_channel_unclaim(SYNC_IO_MODEL_PULSE_DMA_CH);
        s_wave_output_dma_claimed = false;
    }
    if (s_wave_output_sm_claimed) {
        pio_sm_unclaim(BOARD_SYNC_PIO_FAST, s_model_pulse.sm);
        s_wave_output_sm_claimed = false;
    }
    if (hardware_owned) {
        sync_io_model_release_pin();
    }
}

static void sync_io_wave_output_manager_init(void)
{
    if (s_wave_output_manager_initialized) {
        return;
    }
    const sync_io_persona_manager_hooks_t hooks = {
        .load = sync_io_wave_output_load,
        .arm = sync_io_wave_output_arm,
        .start = sync_io_wave_output_start,
        .stop = sync_io_wave_output_stop,
        .cleanup = sync_io_wave_output_cleanup,
    };
    sync_io_persona_manager_init(&s_wave_output_manager, &hooks, NULL);
    s_wave_output_manager_initialized = true;
}

static bool sync_io_wave_output_manager_start(
    sync_io_persona_id_t persona_id)
{
    sync_io_wave_output_manager_init();
    if (!sync_io_persona_manager_claim(
            &s_wave_output_manager,
            persona_id,
            &s_wave_output_handle,
            NULL) ||
        !sync_io_persona_manager_load(&s_wave_output_manager,
                                      &s_wave_output_handle) ||
        !sync_io_persona_manager_arm(&s_wave_output_manager,
                                     &s_wave_output_handle) ||
        !sync_io_persona_manager_start(&s_wave_output_manager,
                                       &s_wave_output_handle)) {
        s_wave_output_manager_active = false;
        return false;
    }
    s_wave_output_manager_active = true;
    return true;
}

static void sync_io_wave_output_manager_release(void)
{
    if (!s_wave_output_manager_active) {
        return;
    }
    (void)sync_io_persona_manager_release(
        &s_wave_output_manager,
        &s_wave_output_handle);
    s_wave_output_manager_active = false;
}

bool sync_io_core_wave_output_persona_active(void)
{
    return s_wave_output_manager_active;
}

void sync_io_model_pulse_schedule_quiesce_tdma_pio(void)
{
    if (s_model_pulse.total_pulses != 0u &&
        s_model_pulse.pio == BOARD_SYNC_PIO_WAVE) {
        sync_io_model_pulse_schedule_disarm();
    }
}

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

static uint32_t sync_io_model_delay_word_to_ticks(uint32_t word)
{
    return word == 0u ? 0u : word + 1u;
}

static uint32_t sync_io_model_high_word_to_ticks(uint32_t word)
{
    return word + 1u;
}

static uint64_t sync_io_model_word_ticks_to_ns(uint32_t ticks,
                                               uint32_t tick_period_ns)
{
    if (ticks == 0u || tick_period_ns == 0u) {
        return 0u;
    }
    return ((uint64_t)ticks + SYNC_IO_MODEL_PULSE_SECTION_OVERHEAD_TICKS) *
           (uint64_t)tick_period_ns;
}

static uint64_t sync_io_model_pulse_duration_ns(uint32_t pulse_index)
{
    if (pulse_index >= s_model_pulse.total_pulses) {
        return 0u;
    }

    const uint32_t delay_word =
        s_model_pulse.words[(pulse_index * SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY) + 0u];
    const uint32_t high_word =
        s_model_pulse.words[(pulse_index * SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY) + 1u];
    return sync_io_model_word_ticks_to_ns(
               sync_io_model_delay_word_to_ticks(delay_word),
               s_model_pulse.tick_period_ns) +
           sync_io_model_word_ticks_to_ns(
               sync_io_model_high_word_to_ticks(high_word),
               s_model_pulse.tick_period_ns);
}

static uint32_t sync_io_model_saturate_u64_to_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static bool sync_io_model_output_index_valid(uint32_t output_index)
{
    return BOARD_DEBUG_MODEL_GPIO_ENABLED != 0 &&
           output_index < BOARD_DEBUG_MODEL_GPIO_PIN_COUNT;
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
    if (!s_model_pulse.running ||
        (sync_io_core_tdma_flight_suspended() &&
         s_model_pulse.pio == BOARD_SYNC_PIO_WAVE)) {
        return;
    }

    const uint64_t elapsed_ns =
        (time_us_64() - s_model_pulse.start_us) * 1000ull;

    uint64_t completed_elapsed_ns = s_model_pulse.completed_elapsed_ns;
    while (s_model_pulse.completed_pulses < s_model_pulse.total_pulses) {
        const uint64_t next_elapsed_ns =
            completed_elapsed_ns +
            sync_io_model_pulse_duration_ns(s_model_pulse.completed_pulses);
        if (elapsed_ns < next_elapsed_ns) {
            break;
        }
        s_model_pulse.completed_pulses++;
        completed_elapsed_ns = next_elapsed_ns;
    }
    s_model_pulse.completed_elapsed_ns = completed_elapsed_ns;

    if (s_model_pulse.completed_pulses >= s_model_pulse.total_pulses &&
        elapsed_ns >= s_model_pulse.total_duration_ns64 &&
        !dma_channel_is_busy(s_model_pulse.dma_ch) &&
        pio_sm_is_tx_fifo_empty(s_model_pulse.pio, s_model_pulse.sm)) {
        s_model_pulse.completed_pulses = s_model_pulse.total_pulses;
        s_model_pulse.completed_elapsed_ns = s_model_pulse.total_duration_ns64;
        pio_sm_set_enabled(s_model_pulse.pio, s_model_pulse.sm, false);
        s_model_pulse.running = false;
        if (s_model_pulse.persona_managed) {
            sync_io_wave_output_manager_release();
        }
    }
}

static bool sync_io_pulse_schedule_arm_on_pin_common(
    PIO pulse_pio,
    uint pulse_sm,
    uint pulse_dreq,
    uint32_t output_pin,
    uint32_t trace_output_index,
    const sync_io_model_pulse_entry_t *entries_us,
    const sync_io_model_pulse_entry_ns_t *entries_ns,
    uint32_t periodic_first_delay_ns,
    uint32_t periodic_period_ns,
    uint32_t periodic_high_ns,
    uint32_t entry_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    const uint32_t sanitized_tick_period_ns =
        tick_period_ns != 0u
            ? tick_period_ns
            : SYNC_IO_MODEL_PULSE_DEFAULT_TICK_PERIOD_NS;
    const bool use_ns_entries = entries_ns != NULL;
    const bool use_periodic_entries =
        entries_us == NULL && entries_ns == NULL &&
        periodic_period_ns != 0u && periodic_high_ns != 0u;
    if (!sync_io_core_initialized() ||
        sync_io_core_capture_is_running() ||
        (!use_periodic_entries && entries_us == NULL && entries_ns == NULL) ||
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

    /* The schedule shares the capture DMA workspace.  Both APIs reject an
     * active peer, so assigning the workspace here cannot race a DMA owner. */
    s_model_pulse.words = sync_io_shared_workspace;

    uint64_t cumulative_ns = 0u;
    for (uint32_t i = 0u; i < entry_count; i++) {
        uint32_t delay_ns = 0u;
        uint32_t high_ns = 0u;
        if (use_periodic_entries) {
            if (periodic_high_ns >= periodic_period_ns) {
                sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                                   SYNC_IO_TRACE_ERROR,
                                   i,
                                   3u);
                return false;
            }
            delay_ns = i == 0u
                ? periodic_first_delay_ns
                : periodic_period_ns - periodic_high_ns;
            high_ns = periodic_high_ns;
        } else if (use_ns_entries) {
            delay_ns = entries_ns[i].delay_ns;
            high_ns = entries_ns[i].high_ns;
        } else {
            delay_ns = entries_us[i].delay_us * 1000u;
            high_ns = entries_us[i].high_us * 1000u;
        }
        const uint32_t delay_ticks =
            sync_io_model_delay_ticks_for_duration(
                delay_ns,
                sanitized_tick_period_ns);
        const uint32_t high_ticks =
            sync_io_model_high_ticks_for_duration(
                high_ns,
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
        cumulative_ns += delay_ns;
        cumulative_ns += high_ns;
    }

    const pio_program_t *program = rising_edge
        ? &sync_model_sched_pulse_high_program
        : &sync_model_sched_pulse_low_program;

    const bool use_pio0_output_persona =
        pulse_pio == BOARD_SYNC_PIO_FAST &&
        (pulse_sm == BOARD_SYNC_PIO0_WAVE_OUTPUT_SM ||
         pulse_sm == BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM);

    s_model_pulse.pio = pulse_pio;
    s_model_pulse.sm = pulse_sm;
    s_model_pulse.dma_ch = SYNC_IO_MODEL_PULSE_DMA_CH;
    s_model_pulse.output_pin = output_pin;
    s_model_pulse.active_high = rising_edge;
    s_model_pulse.persona_managed = use_pio0_output_persona;
    s_model_pulse.total_pulses = entry_count;
    s_model_pulse.completed_pulses = 0u;
    s_model_pulse.completed_elapsed_ns = 0u;
    s_model_pulse.total_duration_ns =
        sync_io_model_saturate_u64_to_u32(cumulative_ns);
    s_model_pulse.total_duration_ns64 = cumulative_ns;
    s_model_pulse.total_duration_us =
        sync_io_model_saturate_u64_to_u32((cumulative_ns + 999ull) / 1000ull);
    s_model_pulse.tick_period_ns = sanitized_tick_period_ns;
    s_model_pulse.fault_code = 0u;

    if (use_pio0_output_persona) {
        const sync_io_persona_id_t persona_id =
            pulse_sm == BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM
                ? SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER
                : SYNC_IO_PERSONA_ID_WAVE_OUTPUT;
        if (!sync_io_wave_output_manager_start(persona_id)) {
            sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                               SYNC_IO_TRACE_ERROR,
                               entry_count,
                               trace_output_index);
            memset(&s_model_pulse, 0, sizeof(s_model_pulse));
            return false;
        }
        LOG_INFO("sync_io", "wave output schedule armed: count=%lu pin=%lu",
                 (unsigned long)entry_count,
                 (unsigned long)s_model_pulse.output_pin);
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_ARM,
                           SYNC_IO_TRACE_INFO,
                           entry_count,
                           ((trace_output_index & 0xFFu) << 8) |
                               (rising_edge ? 1u : 0u));
        return true;
    }

    if (!pio_can_add_program(pulse_pio, program)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           2u);
        return false;
    }

    s_model_pulse.offset = (uint)pio_add_program(pulse_pio, program);

    pio_sm_set_enabled(pulse_pio, s_model_pulse.sm, false);
    pio_sm_clear_fifos(pulse_pio, s_model_pulse.sm);
    pio_sm_restart(pulse_pio, s_model_pulse.sm);

    sync_model_sched_pulse_program_init(pulse_pio,
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
    channel_config_set_dreq(&dma_cfg, pulse_dreq);
    dma_channel_configure(s_model_pulse.dma_ch,
                          &dma_cfg,
                          &pulse_pio->txf[s_model_pulse.sm],
                          s_model_pulse.words,
                          entry_count * SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY,
                          true);

    s_model_pulse.start_us = time_us_64();
    s_model_pulse.running = true;
    pio_sm_set_enabled(pulse_pio, s_model_pulse.sm, true);

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

static bool sync_io_pulse_schedule_arm_on_pin(
    PIO pulse_pio,
    uint pulse_sm,
    uint pulse_dreq,
    uint32_t output_pin,
    uint32_t trace_output_index,
    const sync_io_model_pulse_entry_ns_t *entries,
    uint32_t entry_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    return sync_io_pulse_schedule_arm_on_pin_common(pulse_pio,
                                                    pulse_sm,
                                                    pulse_dreq,
                                                    output_pin,
                                                    trace_output_index,
                                                    NULL,
                                                    entries,
                                                    0u,
                                                    0u,
                                                    0u,
                                                    entry_count,
                                                    rising_edge,
                                                    tick_period_ns);
}

bool sync_io_model_pulse_schedule_arm(uint32_t output_index,
                                      const sync_io_model_pulse_entry_t *entries,
                                      uint32_t entry_count,
                                      bool rising_edge)
{
    if (sync_io_core_tdma_flight_suspended() ||
        !sync_io_model_output_index_valid(output_index)) {
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
    return sync_io_pulse_schedule_arm_on_pin_common(
        BOARD_SYNC_PIO_WAVE,
        BOARD_SYNC_MODEL_SCHED_SM,
        DREQ_PIO1_TX0 + BOARD_SYNC_MODEL_SCHED_SM,
        BOARD_DEBUG_MODEL_GPIO_BASE_PIN + output_index,
        output_index,
        entries,
        NULL,
        0u,
        0u,
        0u,
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
    if (sync_io_core_tdma_flight_suspended() ||
        !sync_io_model_output_index_valid(output_index)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           output_index);
        return false;
    }

    return sync_io_pulse_schedule_arm_on_pin(
        BOARD_SYNC_PIO_WAVE,
        BOARD_SYNC_MODEL_SCHED_SM,
        DREQ_PIO1_TX0 + BOARD_SYNC_MODEL_SCHED_SM,
        BOARD_DEBUG_MODEL_GPIO_BASE_PIN + output_index,
        output_index,
        entries,
        entry_count,
        rising_edge,
        tick_period_ns);
}

bool sync_io_model_pulse_schedule_arm_periodic_ns(
    uint32_t output_index,
    uint32_t first_delay_ns,
    uint32_t pulse_period_ns,
    uint32_t pulse_high_ns,
    uint32_t pulse_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    if (sync_io_core_tdma_flight_suspended() ||
        !sync_io_model_output_index_valid(output_index)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           pulse_count,
                           output_index);
        return false;
    }

    return sync_io_pulse_schedule_arm_on_pin_common(
        BOARD_SYNC_PIO_WAVE,
        BOARD_SYNC_MODEL_SCHED_SM,
        DREQ_PIO1_TX0 + BOARD_SYNC_MODEL_SCHED_SM,
        BOARD_DEBUG_MODEL_GPIO_BASE_PIN + output_index,
        output_index,
        NULL,
        NULL,
        first_delay_ns,
        pulse_period_ns,
        pulse_high_ns,
        pulse_count,
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
    return sync_io_pulse_schedule_arm_on_pin_common(
        BOARD_SYNC_PIO_FAST,
        BOARD_SYNC_PIO0_WAVE_OUTPUT_SM,
        DREQ_PIO0_TX0 + BOARD_SYNC_PIO0_WAVE_OUTPUT_SM,
        BOARD_SYNC_OUTPUT_BASE_PIN + output_index,
        output_index,
        entries,
        NULL,
        0u,
        0u,
        0u,
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
        BOARD_SYNC_PIO_FAST,
        BOARD_SYNC_PIO0_WAVE_OUTPUT_SM,
        DREQ_PIO0_TX0 + BOARD_SYNC_PIO0_WAVE_OUTPUT_SM,
        BOARD_SYNC_OUTPUT_BASE_PIN + output_index,
        output_index,
        entries,
        entry_count,
        rising_edge,
        tick_period_ns);
}

bool sync_io_sma_observer_pulse_schedule_arm_periodic_ns(
    uint32_t output_index,
    uint32_t first_delay_ns,
    uint32_t pulse_period_ns,
    uint32_t pulse_high_ns,
    uint32_t pulse_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    if (!sync_io_main_output_index_valid(output_index) ||
        output_index != 0u || pulse_count == 0u) {
        return false;
    }

    return sync_io_pulse_schedule_arm_on_pin_common(
        BOARD_SYNC_PIO_FAST,
        BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM,
        DREQ_PIO0_TX0 + BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM,
        BOARD_SYNC_OUTPUT_BASE_PIN + output_index,
        output_index,
        NULL,
        NULL,
        first_delay_ns,
        pulse_period_ns,
        pulse_high_ns,
        pulse_count,
        rising_edge,
        tick_period_ns);
}

void sync_io_model_pulse_schedule_disarm(void)
{
    if (s_model_pulse.persona_managed) {
        const uint32_t completed = s_model_pulse.completed_pulses;
        const uint32_t total = s_model_pulse.total_pulses;
        if (s_wave_output_manager_active) {
            sync_io_wave_output_manager_release();
        }
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_DISARM,
                           SYNC_IO_TRACE_INFO,
                           completed,
                           total);
        memset(&s_model_pulse, 0, sizeof(s_model_pulse));
        return;
    }

    if (s_model_pulse.running) {
        pio_sm_set_enabled(s_model_pulse.pio, s_model_pulse.sm, false);
    }

    if (s_model_pulse.offset != 0u || s_model_pulse.total_pulses != 0u) {
        dma_channel_abort(s_model_pulse.dma_ch == 0u
                              ? SYNC_IO_MODEL_PULSE_DMA_CH
                              : s_model_pulse.dma_ch);
        pio_sm_clear_fifos(s_model_pulse.pio,
                           s_model_pulse.sm == 0u
                               ? BOARD_SYNC_MODEL_SCHED_SM
                               : s_model_pulse.sm);
        pio_sm_set_pins(s_model_pulse.pio,
                        s_model_pulse.sm == 0u
                            ? BOARD_SYNC_MODEL_SCHED_SM
                            : s_model_pulse.sm,
                        0u);
        const pio_program_t *program = s_model_pulse.active_high
            ? &sync_model_sched_pulse_high_program
            : &sync_model_sched_pulse_low_program;
        pio_remove_program(s_model_pulse.pio, program, s_model_pulse.offset);
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
        sync_io_core_sm_is_enabled(s_model_pulse.pio, s_model_pulse.sm);
    runtime->dma_busy = dma_channel_is_busy(s_model_pulse.dma_ch);
    runtime->tx_fifo_empty =
        pio_sm_is_tx_fifo_empty(s_model_pulse.pio, s_model_pulse.sm);
    runtime->tx_fifo_full =
        pio_sm_is_tx_fifo_full(s_model_pulse.pio, s_model_pulse.sm);
    runtime->transfer_count = dma_hw->ch[s_model_pulse.dma_ch].transfer_count;
}
