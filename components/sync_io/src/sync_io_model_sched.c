#include "sync_io.h"

#include <string.h>

#include "board_config.h"
#include "diagnostics.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "osal.h"
#include "pico/time.h"
#include "pico/platform.h"
#include "sync_io.pio.h"
#include "sync_io_core_internal.h"
#include "sync_io_persona_manager.h"

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
    uint64_t first_deadline_ns;
    uint32_t periodic_period_ns;
    bool fixed_rate;
    uint32_t fixed_system_clock_hz;
    uint32_t fixed_pio_divider256;
    bool (*validate_before_start)(void *context);
    void *validation_context;
    PIO pio;
    /* The maintenance schedule holds the shared arena lease before writing
     * any words. Only the one-entry phase observer uses private storage. */
    uint32_t *words;
} sync_io_model_pulse_t;

static sync_io_model_pulse_t s_model_pulse;
static sync_io_fixed_rate_runtime_t s_fixed_rate_runtime;
typedef enum {
    SYNC_IO_SCHEDULE_IDLE = 0u,
    SYNC_IO_SCHEDULE_LEGACY_OPERATION,
    SYNC_IO_SCHEDULE_FIXED_PREPARING,
    SYNC_IO_SCHEDULE_FIXED_ACTIVE,
    SYNC_IO_SCHEDULE_FIXED_SERVICE,
    SYNC_IO_SCHEDULE_RUN_RESERVED,
} sync_io_schedule_phase_t;
static uint32_t s_schedule_phase;
static uintptr_t s_run_output_token;
static bool s_fixed_rate_cancel_requested;

static bool sync_io_schedule_reserve(uint32_t expected, uint32_t desired)
{
    return __atomic_compare_exchange_n(&s_schedule_phase, &expected, desired,
                                        false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE);
}

static void sync_io_schedule_publish_phase(uint32_t phase)
{
    __atomic_store_n(&s_schedule_phase, phase, __ATOMIC_RELEASE);
}

bool sync_io_core_legacy_try_enter(void)
{
    return sync_io_schedule_reserve(SYNC_IO_SCHEDULE_IDLE,
                                     SYNC_IO_SCHEDULE_LEGACY_OPERATION);
}

void sync_io_core_legacy_leave(void)
{
    (void)sync_io_schedule_reserve(SYNC_IO_SCHEDULE_LEGACY_OPERATION,
                                   SYNC_IO_SCHEDULE_IDLE);
}

static void sync_io_model_update_completion(void);

bool sync_io_core_model_output_active(void)
{
    if (__atomic_load_n(&s_schedule_phase, __ATOMIC_ACQUIRE) !=
        SYNC_IO_SCHEDULE_LEGACY_OPERATION) return true;
    /* Preserve legacy is_running's completed-schedule retirement while the
     * caller already owns the gate, avoiding a nested public getter. */
    sync_io_model_update_completion();
    return s_model_pulse.running;
}
_Static_assert(SYNC_IO_RATE_SCHEDULE_MAX_PULSES <=
                   SYNC_IO_MODEL_PULSE_MAX_ENTRIES,
               "fixed rate schedule must fit the existing shared workspace");
static sync_io_persona_manager_t s_wave_output_manager;
static sync_io_persona_manager_handle_t s_wave_output_handle;
static bool s_wave_output_manager_initialized;
static bool s_wave_output_manager_active;
static bool s_wave_output_sm_claimed;
static bool s_wave_output_dma_claimed;
static bool s_wave_output_program_loaded;
/* A phase-only observer pulse may coexist with the normal input capture.
 * Keep its one-entry schedule out of the capture ring; larger schedules still
 * use the shared workspace and remain mutually exclusive with capture. */
static uint32_t s_phase_observer_words[SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY];

static float sync_io_model_clkdiv_for_tick_rate(uint32_t tick_hz);
static uint32_t sync_io_model_tick_hz_from_period_ns(uint32_t tick_period_ns);
static uint32_t sync_io_model_delay_ticks_for_duration(
    uint32_t ns, uint32_t tick_period_ns);
static uint32_t sync_io_model_delay_word(uint32_t delay_ticks);
static uint32_t sync_io_model_delay_word_to_ticks(uint32_t word);
static uint64_t sync_io_model_word_ticks_to_ns(
    uint32_t ticks, uint32_t tick_period_ns);
static uint32_t sync_io_model_saturate_u64_to_u32(uint64_t value);
static void sync_io_model_release_pin(void);
static void sync_io_model_pulse_schedule_disarm_owned(void);
static void sync_io_model_pulse_schedule_get_runtime_owned(
    sync_io_model_pulse_runtime_t *runtime);

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
    if (s_model_pulse.fixed_rate) {
        if (s_model_pulse.validate_before_start == NULL ||
            !s_model_pulse.validate_before_start(
                s_model_pulse.validation_context)) {
            return false;
        }
        /* No generator, callback, abort or cleanup runs with IRQs masked.
         * Only the final cancellation/clock check and hardware handoff do. */
        osal_critical_enter();
        if (s_fixed_rate_cancel_requested ||
            clock_get_hz(clk_sys) != s_model_pulse.fixed_system_clock_hz ||
            s_model_pulse.pio->sm[s_model_pulse.sm].clkdiv !=
                (s_model_pulse.fixed_pio_divider256 << 8u)) {
            osal_critical_exit();
            return false;
        }
        s_model_pulse.start_us = time_us_64();
        s_model_pulse.running = true;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        dma_start_channel_mask(1u << s_model_pulse.dma_ch);
        pio_sm_set_enabled(s_model_pulse.pio, s_model_pulse.sm, true);
        osal_critical_exit();
        return true;
    }
    if (s_model_pulse.first_deadline_ns != 0u) {
        const uint64_t now_ns = time_us_64() * 1000ull;
        const uint64_t start_guard_ns = s_model_pulse.periodic_period_ns;
        if (s_model_pulse.periodic_period_ns == 0u ||
            now_ns > UINT64_MAX - start_guard_ns) {
            return false;
        }
        const uint64_t minimum_deadline_ns = now_ns + start_guard_ns;
        if (s_model_pulse.first_deadline_ns <= minimum_deadline_ns) {
            const uint64_t periods =
                (minimum_deadline_ns - s_model_pulse.first_deadline_ns) /
                    s_model_pulse.periodic_period_ns +
                1u;
            if (periods >
                (UINT64_MAX - s_model_pulse.first_deadline_ns) /
                    s_model_pulse.periodic_period_ns) {
                return false;
            }
            s_model_pulse.first_deadline_ns +=
                periods * s_model_pulse.periodic_period_ns;
        }
        if (s_model_pulse.first_deadline_ns - now_ns > UINT32_MAX) {
            return false;
        }
        const uint64_t previous_delay_ns = sync_io_model_word_ticks_to_ns(
            sync_io_model_delay_word_to_ticks(s_model_pulse.words[0]),
            s_model_pulse.tick_period_ns);
        const uint32_t remaining_ns =
            (uint32_t)(s_model_pulse.first_deadline_ns - now_ns);
        const uint32_t delay_ticks = sync_io_model_delay_ticks_for_duration(
            remaining_ns, s_model_pulse.tick_period_ns);
        s_model_pulse.words[0] = sync_io_model_delay_word(delay_ticks);
        const uint64_t rebased_delay_ns = sync_io_model_word_ticks_to_ns(
            sync_io_model_delay_word_to_ticks(s_model_pulse.words[0]),
            s_model_pulse.tick_period_ns);
        s_model_pulse.total_duration_ns64 =
            s_model_pulse.total_duration_ns64 - previous_delay_ns +
            rebased_delay_ns;
        s_model_pulse.total_duration_ns = sync_io_model_saturate_u64_to_u32(
            s_model_pulse.total_duration_ns64);
        s_model_pulse.total_duration_us = sync_io_model_saturate_u64_to_u32(
            (s_model_pulse.total_duration_ns64 + 999ull) / 1000ull);
    }
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
    return __atomic_load_n(&s_schedule_phase, __ATOMIC_ACQUIRE) !=
               SYNC_IO_SCHEDULE_IDLE || s_wave_output_manager_active;
}

bool sync_io_core_wave_output_persona_active_owned(void)
{
    const uint32_t phase = __atomic_load_n(&s_schedule_phase, __ATOMIC_ACQUIRE);
    /* Legacy mutators call this only after acquiring their short gate. */
    return (phase != SYNC_IO_SCHEDULE_IDLE &&
            phase != SYNC_IO_SCHEDULE_LEGACY_OPERATION) ||
           s_wave_output_manager_active;
}

bool sync_io_core_run_output_reserve(const void *token)
{
    if (get_core_num() != 0u || token == NULL ||
        !sync_io_schedule_reserve(SYNC_IO_SCHEDULE_IDLE,
                                  SYNC_IO_SCHEDULE_RUN_RESERVED)) return false;
    /* A legacy schedule may leave IDLE while its hardware remains active.
     * Never call the public runtime getter here: it takes this same gate. */
    if (!sync_io_core_initialized() || s_model_pulse.running ||
        s_wave_output_manager_active || s_wave_output_sm_claimed ||
        s_wave_output_dma_claimed || s_wave_output_program_loaded ||
        sync_io_core_capture_is_running() || sync_io_seq_step_is_running() ||
        sync_io_enc_count_is_running() || sync_io_core_sma_frequency_output_active()) {
        sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
        return false;
    }
    __atomic_store_n(&s_run_output_token, (uintptr_t)token, __ATOMIC_RELEASE);
    return true;
}

/* Shared lease observation is also used by the Core1 cached-refill path. */
__attribute__((noinline)) bool __not_in_flash_func(sync_io_core_run_output_held)(const void *token);
bool sync_io_core_run_output_held(const void *token)
{
    return token != NULL &&
        __atomic_load_n(&s_schedule_phase, __ATOMIC_ACQUIRE) ==
            SYNC_IO_SCHEDULE_RUN_RESERVED &&
        __atomic_load_n(&s_run_output_token, __ATOMIC_ACQUIRE) == (uintptr_t)token;
}

bool sync_io_core_run_output_release(const void *token)
{
    if (get_core_num() != 0u || !sync_io_core_run_output_held(token)) return false;
    /* Runtime owns generation/retirement: caller may release only after ACK. */
    __atomic_store_n(&s_run_output_token, 0u, __ATOMIC_RELEASE);
    sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
    return true;
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

    const bool transport_empty =
        !dma_channel_is_busy(s_model_pulse.dma_ch) &&
        pio_sm_is_tx_fifo_empty(s_model_pulse.pio, s_model_pulse.sm);
    /* The last high word may already be in X while DMA and FIFO are empty.
     * Only the subsequent low PULL proves that the final falling edge ran. */
    const bool fixed_terminal = s_model_pulse.fixed_rate && transport_empty &&
        dma_hw->ch[s_model_pulse.dma_ch].transfer_count == 0u &&
        pio_sm_get_pc(s_model_pulse.pio, s_model_pulse.sm) ==
            s_model_pulse.offset;
    if (s_model_pulse.fixed_rate && !fixed_terminal &&
        s_model_pulse.completed_pulses == s_model_pulse.total_pulses) {
        --s_model_pulse.completed_pulses;
        s_model_pulse.completed_elapsed_ns -= sync_io_model_pulse_duration_ns(
            s_model_pulse.completed_pulses);
    }
    if (s_model_pulse.fixed_rate ? fixed_terminal :
        (s_model_pulse.completed_pulses >= s_model_pulse.total_pulses &&
         elapsed_ns >= s_model_pulse.total_duration_ns64 && transport_empty)) {
        s_model_pulse.completed_pulses = s_model_pulse.total_pulses;
        s_model_pulse.completed_elapsed_ns = s_model_pulse.total_duration_ns64;
        pio_sm_set_enabled(s_model_pulse.pio, s_model_pulse.sm, false);
        s_model_pulse.running = false;
        if (s_model_pulse.persona_managed) {
            sync_io_wave_output_manager_release();
        }
        (void)sync_io_workspace_release(&s_model_pulse);
    }
}

static bool sync_io_pulse_schedule_arm_on_pin_common_owned(
    PIO pulse_pio,
    uint pulse_sm,
    uint pulse_dreq,
    uint32_t output_pin,
    uint32_t trace_output_index,
    const sync_io_model_pulse_entry_t *entries_us,
    const sync_io_model_pulse_entry_ns_t *entries_ns,
    uint32_t periodic_first_delay_ns,
    uint64_t periodic_first_deadline_ns,
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
    const bool observer_persona =
        pulse_pio == BOARD_SYNC_PIO_FAST &&
        pulse_sm == BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM;
    const bool observer_capture_overlap =
        observer_persona && use_periodic_entries && entry_count == 1u &&
        sync_io_core_capture_is_running();
    if (!sync_io_core_initialized() ||
        (s_model_pulse.fixed_rate && s_model_pulse.running) ||
        (sync_io_core_capture_is_running() && !observer_capture_overlap) ||
        (!use_periodic_entries && entries_us == NULL && entries_ns == NULL) ||
        entry_count == 0u ||
        entry_count > SYNC_IO_MODEL_PULSE_MAX_ENTRIES ||
        sanitized_tick_period_ns == 0u ||
        (periodic_first_delay_ns != 0u &&
         periodic_first_deadline_ns != 0u)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           entry_count,
                           trace_output_index);
        return false;
    }

    sync_io_model_pulse_schedule_disarm_owned();

    if (!observer_capture_overlap &&
        !sync_io_workspace_claim(&s_model_pulse)) {
        sync_io_core_trace(SYNC_IO_TRACE_MODEL_FAIL, SYNC_IO_TRACE_ERROR,
                           entry_count, SYNC_IO_PERSONA_CONFLICT_WORKSPACE);
        return false;
    }

    /* Batch schedules share the capture DMA workspace.  A phase-only observer
     * uses its bounded one-entry buffer when capture is active, so the two DMA
     * clients cannot overwrite one another. */
    s_model_pulse.words = sync_io_shared_workspace;
    if (observer_capture_overlap) {
        s_model_pulse.words = s_phase_observer_words;
    }

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
                (void)sync_io_workspace_release(&s_model_pulse);
                return false;
            }
            if (i == 0u && periodic_first_deadline_ns != 0u) {
                const uint64_t now_ns = time_us_64() * 1000ull;
                if (periodic_first_deadline_ns > now_ns &&
                    periodic_first_deadline_ns - now_ns > UINT32_MAX) {
                    (void)sync_io_workspace_release(&s_model_pulse);
                    return false;
                }
                delay_ns = periodic_first_deadline_ns > now_ns
                    ? (uint32_t)(periodic_first_deadline_ns - now_ns)
                    : 0u;
            } else {
                delay_ns = i == 0u
                    ? periodic_first_delay_ns
                    : periodic_period_ns - periodic_high_ns;
            }
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
            (void)sync_io_workspace_release(&s_model_pulse);
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
    s_model_pulse.first_deadline_ns = periodic_first_deadline_ns;
    s_model_pulse.periodic_period_ns = periodic_period_ns;
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
            (void)sync_io_workspace_release(&s_model_pulse);
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
        (void)sync_io_workspace_release(&s_model_pulse);
        memset(&s_model_pulse, 0, sizeof(s_model_pulse));
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

static bool sync_io_pulse_schedule_arm_on_pin_common(
    PIO pulse_pio,
    uint pulse_sm,
    uint pulse_dreq,
    uint32_t output_pin,
    uint32_t trace_output_index,
    const sync_io_model_pulse_entry_t *entries_us,
    const sync_io_model_pulse_entry_ns_t *entries_ns,
    uint32_t periodic_first_delay_ns,
    uint64_t periodic_first_deadline_ns,
    uint32_t periodic_period_ns,
    uint32_t periodic_high_ns,
    uint32_t entry_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    if (!sync_io_schedule_reserve(SYNC_IO_SCHEDULE_IDLE,
                                  SYNC_IO_SCHEDULE_LEGACY_OPERATION)) {
        return false;
    }
    const bool armed = sync_io_pulse_schedule_arm_on_pin_common_owned(
        pulse_pio, pulse_sm, pulse_dreq, output_pin, trace_output_index,
        entries_us, entries_ns, periodic_first_delay_ns,
        periodic_first_deadline_ns, periodic_period_ns, periodic_high_ns,
        entry_count, rising_edge, tick_period_ns);
    sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
    return armed;
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
    return sync_io_output_pulse_schedule_arm(output_index,
                                             entries,
                                             entry_count,
                                             rising_edge);
}

bool sync_io_model_pulse_schedule_arm_ns(
    uint32_t output_index,
    const sync_io_model_pulse_entry_ns_t *entries,
    uint32_t entry_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    return sync_io_output_pulse_schedule_arm_ns(output_index,
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
    return sync_io_sma_observer_pulse_schedule_arm_periodic_ns(
        output_index,
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
        0u,
        pulse_period_ns,
        pulse_high_ns,
        pulse_count,
        rising_edge,
        tick_period_ns);
}

bool sync_io_sma_observer_pulse_schedule_arm_periodic_at_ns(
    uint32_t output_index,
    uint64_t first_deadline_ns,
    uint32_t pulse_period_ns,
    uint32_t pulse_high_ns,
    uint32_t pulse_count,
    bool rising_edge,
    uint32_t tick_period_ns)
{
    if (!sync_io_main_output_index_valid(output_index) ||
        output_index != 0u || first_deadline_ns == 0u || pulse_count == 0u) {
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
        0u,
        first_deadline_ns,
        pulse_period_ns,
        pulse_high_ns,
        pulse_count,
        rising_edge,
        tick_period_ns);
}

static bool sync_io_sma_observer_fixed_rate_arm_owned(
    const sync_io_rate_schedule_request_t *request,
    sync_io_fixed_rate_runtime_t *runtime,
    bool (*validate_before_start)(void *context),
    void *context)
{
    if (!sync_io_core_initialized() || validate_before_start == NULL ||
        !sync_io_rate_schedule_request_valid(request) ||
        sync_io_core_capture_is_running() ||
        s_model_pulse.running || s_wave_output_manager_active ||
        s_wave_output_sm_claimed || s_wave_output_dma_claimed ||
        s_wave_output_program_loaded ||
        sync_io_seq_step_is_running() || sync_io_enc_count_is_running()) {
        return false;
    }
    sync_io_sma_frequency_tx_status_t pwm_status;
    sync_io_sma_frequency_tx_get_status(&pwm_status);
    if (pwm_status.running) {
        return false;
    }
    const uint32_t system_clock_hz = clock_get_hz(clk_sys);
    const uint32_t tick_hz = 1000000000u / request->tick_period_ns;
    const uint32_t divider = system_clock_hz / tick_hz;
    if (system_clock_hz % tick_hz != 0u ||
        divider == 0u || divider > UINT16_MAX ||
        !sync_io_workspace_claim(&s_model_pulse)) {
        return false;
    }

    /* No other owner has been stopped. The lease precedes every buffer write.
     * Only small metadata lives off-arena; no second entry array is created. */
    sync_io_rate_schedule_encoding_t encoding;
    if (!sync_io_rate_schedule_generate(
            request, sync_io_shared_workspace, SYNC_IO_SHARED_WORKSPACE_WORDS,
            &encoding)) {
        (void)sync_io_workspace_release(&s_model_pulse);
        return false;
    }
    memset(&s_model_pulse, 0, sizeof(s_model_pulse));
    s_model_pulse.words = sync_io_shared_workspace;
    s_model_pulse.pio = BOARD_SYNC_PIO_FAST;
    s_model_pulse.sm = BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM;
    s_model_pulse.dma_ch = SYNC_IO_MODEL_PULSE_DMA_CH;
    s_model_pulse.output_pin = BOARD_SYNC_OUTPUT_BASE_PIN;
    s_model_pulse.active_high = true;
    s_model_pulse.persona_managed = true;
    s_model_pulse.total_pulses = request->pulse_count;
    s_model_pulse.tick_period_ns = request->tick_period_ns;
    s_model_pulse.total_duration_ns64 =
        encoding.total_ticks * request->tick_period_ns;
    s_model_pulse.total_duration_ns = sync_io_model_saturate_u64_to_u32(
        s_model_pulse.total_duration_ns64);
    s_model_pulse.total_duration_us = sync_io_model_saturate_u64_to_u32(
        (s_model_pulse.total_duration_ns64 + 999u) / 1000u);
    s_model_pulse.fixed_rate = true;
    s_model_pulse.fixed_system_clock_hz = system_clock_hz;
    s_model_pulse.fixed_pio_divider256 = divider * 256u;
    s_model_pulse.validate_before_start = validate_before_start;
    s_model_pulse.validation_context = context;
    if (!sync_io_wave_output_manager_start(
            SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER)) {
        (void)sync_io_workspace_release(&s_model_pulse);
        memset(&s_model_pulse, 0, sizeof(s_model_pulse));
        return false;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->configured = true;
    runtime->request = *request;
    runtime->encoding = encoding;
    runtime->system_clock_hz = system_clock_hz;
    runtime->pio_divider256 = divider * 256u;
    sync_io_model_pulse_schedule_get_runtime_owned(&runtime->pulse);
    sync_io_core_trace(SYNC_IO_TRACE_MODEL_ARM, SYNC_IO_TRACE_INFO,
                       request->pulse_count, request->request_id);
    return true;
}

bool sync_io_sma_observer_fixed_rate_arm(
    const sync_io_rate_schedule_request_t *request,
    sync_io_fixed_rate_runtime_t *runtime,
    bool (*validate_before_start)(void *context),
    void *context)
{
    osal_critical_enter();
    const bool reserved = sync_io_schedule_reserve(
        SYNC_IO_SCHEDULE_IDLE, SYNC_IO_SCHEDULE_FIXED_PREPARING);
    if (reserved) {
        s_fixed_rate_cancel_requested = false;
    }
    osal_critical_exit();
    if (!reserved) {
        return false;
    }
    sync_io_fixed_rate_runtime_t prepared;
    const bool armed = sync_io_sma_observer_fixed_rate_arm_owned(
        request, &prepared, validate_before_start, context);
    if (!armed) {
        sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
        return false;
    }
    osal_critical_enter();
    const bool cancelled = s_fixed_rate_cancel_requested;
    if (!cancelled) {
        s_fixed_rate_runtime = prepared;
        sync_io_schedule_publish_phase(s_model_pulse.running
            ? SYNC_IO_SCHEDULE_FIXED_ACTIVE : SYNC_IO_SCHEDULE_IDLE);
    }
    osal_critical_exit();
    if (cancelled) {
        /* A STOP can arrive after enable but before publication. It never
         * releases an arena that the preparing task is still using. */
        sync_io_wave_output_manager_release();
        (void)sync_io_workspace_release(&s_model_pulse);
        memset(&s_model_pulse, 0, sizeof(s_model_pulse));
        sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
        return false;
    }
    if (runtime != NULL) {
        *runtime = prepared;
    }
    return true;
}

void sync_io_sma_observer_fixed_rate_disarm(void)
{
    osal_critical_enter();
    const uint32_t phase = __atomic_load_n(
        &s_schedule_phase, __ATOMIC_ACQUIRE);
    if (phase == SYNC_IO_SCHEDULE_FIXED_PREPARING ||
        phase == SYNC_IO_SCHEDULE_FIXED_SERVICE) {
        s_fixed_rate_cancel_requested = true;
        osal_critical_exit();
        return;
    }
    const bool reserved = sync_io_schedule_reserve(
        SYNC_IO_SCHEDULE_FIXED_ACTIVE, SYNC_IO_SCHEDULE_FIXED_SERVICE);
    osal_critical_exit();
    if (reserved) {
        sync_io_model_pulse_schedule_disarm_owned();
        sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
    }
}

void sync_io_sma_observer_fixed_rate_get_runtime(
    sync_io_fixed_rate_runtime_t *runtime)
{
    if (sync_io_schedule_reserve(SYNC_IO_SCHEDULE_FIXED_ACTIVE,
                                 SYNC_IO_SCHEDULE_FIXED_SERVICE)) {
        sync_io_model_pulse_runtime_t pulse;
        sync_io_model_pulse_schedule_get_runtime_owned(&pulse);
        osal_critical_enter();
        const bool cancelled = s_fixed_rate_cancel_requested;
        if (!cancelled) {
            s_fixed_rate_runtime.pulse = pulse;
            sync_io_schedule_publish_phase(s_model_pulse.running
                ? SYNC_IO_SCHEDULE_FIXED_ACTIVE : SYNC_IO_SCHEDULE_IDLE);
        }
        osal_critical_exit();
        if (cancelled) {
            sync_io_model_pulse_schedule_disarm_owned();
            sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
        }
    }
    if (runtime != NULL) {
        osal_critical_enter();
        *runtime = s_fixed_rate_runtime;
        osal_critical_exit();
    }
}

static void sync_io_model_pulse_schedule_disarm_owned(void)
{
    if (s_model_pulse.fixed_rate &&
        __atomic_load_n(&s_schedule_phase, __ATOMIC_ACQUIRE) ==
            SYNC_IO_SCHEDULE_FIXED_SERVICE) {
        sync_io_model_pulse_runtime_t stopped;
        sync_io_model_pulse_schedule_get_runtime_owned(&stopped);
        stopped.running = false;
        stopped.pio_enabled = false;
        stopped.dma_busy = false;
        stopped.tx_fifo_empty = true;
        stopped.tx_fifo_full = false;
        osal_critical_enter();
        s_fixed_rate_runtime.pulse = stopped;
        osal_critical_exit();
    }
    if (s_model_pulse.persona_managed) {
        const uint32_t completed = s_model_pulse.completed_pulses;
        const uint32_t total = s_model_pulse.total_pulses;
        if (s_wave_output_manager_active) {
            sync_io_wave_output_manager_release();
        }
        (void)sync_io_workspace_release(&s_model_pulse);
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

    (void)sync_io_workspace_release(&s_model_pulse);
    memset(&s_model_pulse, 0, sizeof(s_model_pulse));
}

void sync_io_model_pulse_schedule_disarm(void)
{
    if (sync_io_schedule_reserve(SYNC_IO_SCHEDULE_IDLE,
                                 SYNC_IO_SCHEDULE_LEGACY_OPERATION)) {
        sync_io_model_pulse_schedule_disarm_owned();
        sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
    }
}

bool sync_io_model_pulse_schedule_is_running(void)
{
    sync_io_model_pulse_runtime_t runtime;
    sync_io_model_pulse_schedule_get_runtime(&runtime);
    return runtime.running;
}

static void sync_io_model_pulse_schedule_get_runtime_owned(
    sync_io_model_pulse_runtime_t *runtime)
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

    /* A completed fixed batch has released the SM/DMA lease. Never inspect
     * registers that may now belong to a different output owner. */
    if (s_model_pulse.fixed_rate && !s_model_pulse.running) {
        runtime->elapsed_us = s_model_pulse.total_duration_us;
        runtime->tx_fifo_empty = true;
        return;
    }

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

void sync_io_model_pulse_schedule_get_runtime(sync_io_model_pulse_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (!sync_io_schedule_reserve(SYNC_IO_SCHEDULE_IDLE,
                                  SYNC_IO_SCHEDULE_LEGACY_OPERATION)) {
        /* Fixed output is serviced only by its Core0 maintenance owner.
         * Ordinary callers (including Core1) get busy without reading the
         * Core0 cache, unpublished model state, or this owner's hardware. */
        memset(runtime, 0, sizeof(*runtime));
        runtime->running = true;
        return;
    }
    sync_io_model_pulse_schedule_get_runtime_owned(runtime);
    sync_io_schedule_publish_phase(SYNC_IO_SCHEDULE_IDLE);
}
