#include "model_turntable.h"

#include <math.h>
#include <string.h>

#include "board_config.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "project_config.h"
#include "sync_io.h"

#define MODEL_TURNTABLE_DEFAULT_VELOCITY   10.0
#define MODEL_TURNTABLE_DEFAULT_ACCEL      20.0
#define MODEL_TURNTABLE_MIN_INTERVAL_US    1000u
#define MODEL_TURNTABLE_MAX_INTERVAL_US    10000000u
#define MODEL_TURNTABLE_DEFAULT_TIMEOUT_MS (-1)
#define MODEL_TURNTABLE_MAX_SCHEDULE_PULSES 256u

typedef struct {
    bool initialized;
    bool loaded;
    bool configured;
    bool running;
    uint32_t phase;
    uint32_t total_pulses;
    uint32_t emitted_pulses;
    uint32_t accel_pulses;
    uint32_t cruise_pulses;
    uint32_t decel_pulses;
    uint32_t min_interval_us;
    uint32_t max_interval_us;
    uint32_t last_interval_us;
    uint32_t fault_code;
    uint64_t start_us;
    double current_position;
    double direction;
    double abs_step;
    uint32_t slot_id;
    uint32_t output_index;
    sync_io_model_pulse_entry_t schedule[MODEL_TURNTABLE_MAX_SCHEDULE_PULSES];
    model_turntable_trigger_config_t trigger;
    model_turntable_motion_config_t motion;
} model_turntable_context_t;

static model_turntable_context_t s_turntable;

static double model_turntable_abs_double(double value)
{
    return value < 0.0 ? -value : value;
}

static uint32_t model_turntable_clamp_interval(double interval_us)
{
    if (interval_us < (double)MODEL_TURNTABLE_MIN_INTERVAL_US) {
        return MODEL_TURNTABLE_MIN_INTERVAL_US;
    }
    if (interval_us > (double)MODEL_TURNTABLE_MAX_INTERVAL_US) {
        return MODEL_TURNTABLE_MAX_INTERVAL_US;
    }
    return (uint32_t)(interval_us + 0.5);
}

static uint32_t model_turntable_interval_for_next_pulse(uint32_t next_index)
{
    if (s_turntable.total_pulses <= 1u) {
        return s_turntable.min_interval_us;
    }

    if (next_index < s_turntable.accel_pulses && s_turntable.accel_pulses > 1u) {
        const uint32_t remaining = s_turntable.accel_pulses - next_index;
        const uint32_t span = s_turntable.max_interval_us - s_turntable.min_interval_us;
        return s_turntable.min_interval_us +
               (uint32_t)(((uint64_t)span * remaining) / s_turntable.accel_pulses);
    }

    if (next_index >= (s_turntable.accel_pulses + s_turntable.cruise_pulses) &&
        s_turntable.decel_pulses > 1u) {
        const uint32_t decel_index =
            next_index - s_turntable.accel_pulses - s_turntable.cruise_pulses;
        const uint32_t span = s_turntable.max_interval_us - s_turntable.min_interval_us;
        return s_turntable.min_interval_us +
               (uint32_t)(((uint64_t)span * (decel_index + 1u)) / s_turntable.decel_pulses);
    }

    return s_turntable.min_interval_us;
}

static void model_turntable_recompute_plan(void)
{
    const double total_distance =
        model_turntable_abs_double(s_turntable.trigger.stop - s_turntable.trigger.start);
    s_turntable.abs_step = model_turntable_abs_double(s_turntable.trigger.step);
    s_turntable.direction = s_turntable.trigger.stop >= s_turntable.trigger.start ? 1.0 : -1.0;

    if (s_turntable.abs_step <= 0.0 || total_distance <= 0.0) {
        s_turntable.total_pulses = 1u;
    } else {
        s_turntable.total_pulses = (uint32_t)(total_distance / s_turntable.abs_step) + 1u;
    }

    const double velocity = s_turntable.motion.velocity_units_per_s;
    const double acceleration = s_turntable.motion.acceleration_units_per_s2;
    s_turntable.min_interval_us =
        model_turntable_clamp_interval((s_turntable.abs_step / velocity) * 1000000.0);

    const double accel_distance = (velocity * velocity) / (2.0 * acceleration);
    uint32_t accel_pulses = (uint32_t)(accel_distance / s_turntable.abs_step) + 1u;
    if (accel_pulses < 1u) {
        accel_pulses = 1u;
    }

    const uint32_t segment_pulses =
        s_turntable.total_pulses > 1u ? s_turntable.total_pulses - 1u : 1u;
    if ((accel_pulses * 2u) > segment_pulses) {
        accel_pulses = segment_pulses / 2u;
    }
    s_turntable.accel_pulses = accel_pulses;
    s_turntable.decel_pulses = accel_pulses;
    s_turntable.cruise_pulses = segment_pulses -
                                s_turntable.accel_pulses -
                                s_turntable.decel_pulses;

    const uint32_t ramp_factor = s_turntable.accel_pulses > 0u ? s_turntable.accel_pulses + 1u : 2u;
    double max_interval = (double)s_turntable.min_interval_us * (double)ramp_factor;
    if (max_interval < (double)s_turntable.min_interval_us * 2.0) {
        max_interval = (double)s_turntable.min_interval_us * 2.0;
    }
    s_turntable.max_interval_us = model_turntable_clamp_interval(max_interval);
}

static bool model_turntable_valid_trigger_config(const model_turntable_trigger_config_t *config)
{
    return config != NULL &&
           config->dimension < 16u &&
           config->step != 0.0 &&
           config->pulse_width_us > 0u &&
           config->pulse_width_us <= 1000000u;
}

static bool model_turntable_valid_motion_config(const model_turntable_motion_config_t *config)
{
    return config != NULL &&
           config->velocity_units_per_s > 0.0 &&
           config->acceleration_units_per_s2 > 0.0;
}

static bool model_turntable_build_schedule(void)
{
    if (s_turntable.total_pulses == 0u ||
        s_turntable.total_pulses > MODEL_TURNTABLE_MAX_SCHEDULE_PULSES) {
        s_turntable.fault_code = 2u;
        return false;
    }

    for (uint32_t i = 0u; i < s_turntable.total_pulses; i++) {
        uint32_t delay_us = 0u;
        if (i > 0u) {
            const uint32_t start_interval_us = model_turntable_interval_for_next_pulse(i);
            delay_us = start_interval_us > s_turntable.trigger.pulse_width_us
                ? start_interval_us - s_turntable.trigger.pulse_width_us
                : 0u;
            s_turntable.last_interval_us = start_interval_us;
        }
        s_turntable.schedule[i].delay_us = delay_us;
        s_turntable.schedule[i].high_us = s_turntable.trigger.pulse_width_us;
    }

    return true;
}

static void model_turntable_update_phase_from_pulses(uint32_t completed_pulses)
{
    if (completed_pulses >= s_turntable.total_pulses) {
        s_turntable.phase = MODEL_TURNTABLE_PHASE_DONE;
    } else if (completed_pulses < s_turntable.accel_pulses) {
        s_turntable.phase = MODEL_TURNTABLE_PHASE_ACCEL;
    } else if (completed_pulses < (s_turntable.accel_pulses + s_turntable.cruise_pulses)) {
        s_turntable.phase = MODEL_TURNTABLE_PHASE_CRUISE;
    } else {
        s_turntable.phase = MODEL_TURNTABLE_PHASE_DECEL;
    }
}

bool model_turntable_init(void)
{
    memset(&s_turntable, 0, sizeof(s_turntable));
    s_turntable.initialized = true;
    s_turntable.loaded = false;
    s_turntable.phase = MODEL_TURNTABLE_PHASE_IDLE;
    s_turntable.slot_id = UINT32_MAX;
    s_turntable.output_index = 0u;
    s_turntable.trigger.dimension = 0u;
    s_turntable.trigger.start = 0.0;
    s_turntable.trigger.stop = 10.0;
    s_turntable.trigger.step = 1.0;
    s_turntable.trigger.pulse_width_us = 2000u;
    s_turntable.trigger.rising_edge = true;
    s_turntable.trigger.timeout_ms = MODEL_TURNTABLE_DEFAULT_TIMEOUT_MS;
    s_turntable.motion.velocity_units_per_s = MODEL_TURNTABLE_DEFAULT_VELOCITY;
    s_turntable.motion.acceleration_units_per_s2 = MODEL_TURNTABLE_DEFAULT_ACCEL;
    s_turntable.configured = true;
    model_turntable_recompute_plan();
    return true;
}

bool model_turntable_load(uint32_t slot_id, uint32_t output_index)
{
    if (!s_turntable.initialized ||
        s_turntable.running ||
        slot_id >= 8u ||
        output_index >= BOARD_DEBUG_MODEL_GPIO_PIN_COUNT) {
        return false;
    }

    s_turntable.slot_id = slot_id;
    s_turntable.output_index = output_index;
    s_turntable.loaded = true;
    s_turntable.phase = MODEL_TURNTABLE_PHASE_IDLE;
    return true;
}

bool model_turntable_configure_trigger(const model_turntable_trigger_config_t *config)
{
    if (!s_turntable.initialized || s_turntable.running ||
        !model_turntable_valid_trigger_config(config)) {
        return false;
    }

    s_turntable.trigger = *config;
    s_turntable.configured = true;
    s_turntable.phase = MODEL_TURNTABLE_PHASE_IDLE;
    s_turntable.fault_code = 0u;
    model_turntable_recompute_plan();
    return true;
}

bool model_turntable_configure_motion(const model_turntable_motion_config_t *config)
{
    if (!s_turntable.initialized || s_turntable.running ||
        !model_turntable_valid_motion_config(config)) {
        return false;
    }

    s_turntable.motion = *config;
    s_turntable.configured = true;
    s_turntable.phase = MODEL_TURNTABLE_PHASE_IDLE;
    s_turntable.fault_code = 0u;
    model_turntable_recompute_plan();
    return true;
}

bool model_turntable_start(void)
{
    if (!s_turntable.initialized || !s_turntable.loaded ||
        !s_turntable.configured || PROJECT_ENABLE_UART_STDIO) {
        return false;
    }

    model_turntable_recompute_plan();
    if (!model_turntable_build_schedule()) {
        return false;
    }

    if (!sync_io_model_pulse_schedule_arm(s_turntable.output_index,
                                          s_turntable.schedule,
                                          s_turntable.total_pulses,
                                          s_turntable.trigger.rising_edge)) {
        s_turntable.fault_code = 3u;
        s_turntable.phase = MODEL_TURNTABLE_PHASE_FAULT;
        return false;
    }

    s_turntable.running = true;
    s_turntable.phase = MODEL_TURNTABLE_PHASE_ACCEL;
    s_turntable.emitted_pulses = 0u;
    s_turntable.last_interval_us = 0u;
    s_turntable.current_position = s_turntable.trigger.start;
    s_turntable.start_us = time_us_64();
    s_turntable.fault_code = 0u;
    return true;
}

void model_turntable_stop(void)
{
    if (!s_turntable.initialized) {
        return;
    }

    s_turntable.running = false;
    s_turntable.phase = MODEL_TURNTABLE_PHASE_IDLE;
    sync_io_model_pulse_schedule_disarm();
}

void model_turntable_service(void)
{
    if (!s_turntable.running) {
        return;
    }

    const uint64_t now = time_us_64();
    if (s_turntable.trigger.timeout_ms >= 0 &&
        (now - s_turntable.start_us) >
            ((uint64_t)(uint32_t)s_turntable.trigger.timeout_ms * 1000u)) {
        s_turntable.running = false;
        s_turntable.phase = MODEL_TURNTABLE_PHASE_FAULT;
        s_turntable.fault_code = 1u;
        sync_io_model_pulse_schedule_disarm();
        return;
    }

    sync_io_model_pulse_runtime_t runtime;
    sync_io_model_pulse_schedule_get_runtime(&runtime);
    if (runtime.fault_code != 0u) {
        s_turntable.running = false;
        s_turntable.phase = MODEL_TURNTABLE_PHASE_FAULT;
        s_turntable.fault_code = runtime.fault_code;
        return;
    }

    s_turntable.emitted_pulses = runtime.completed_pulses;
    if (s_turntable.emitted_pulses > 0u) {
        s_turntable.current_position =
            s_turntable.trigger.start +
            (s_turntable.direction *
             s_turntable.abs_step *
             (double)(s_turntable.emitted_pulses - 1u));
    }

    model_turntable_update_phase_from_pulses(s_turntable.emitted_pulses);
    if (s_turntable.phase == MODEL_TURNTABLE_PHASE_DONE && !runtime.running) {
        s_turntable.running = false;
    }
}

void model_turntable_get_status(model_turntable_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->initialized = s_turntable.initialized;
    status->loaded = s_turntable.loaded;
    status->configured = s_turntable.configured;
    status->running = s_turntable.running;
    status->phase = s_turntable.phase;
    status->slot_id = s_turntable.slot_id;
    status->output_index = s_turntable.output_index;
    status->dimension = s_turntable.trigger.dimension;
    status->total_pulses = s_turntable.total_pulses;
    status->emitted_pulses = s_turntable.emitted_pulses;
    status->accel_pulses = s_turntable.accel_pulses;
    status->cruise_pulses = s_turntable.cruise_pulses;
    status->decel_pulses = s_turntable.decel_pulses;
    status->min_interval_us = s_turntable.min_interval_us;
    status->max_interval_us = s_turntable.max_interval_us;
    status->pulse_width_us = s_turntable.trigger.pulse_width_us;
    status->last_interval_us = s_turntable.last_interval_us;
    status->fault_code = s_turntable.fault_code;
    status->current_position = s_turntable.current_position;
}

void model_turntable_get_trigger_config(model_turntable_trigger_config_t *config)
{
    if (config != NULL) {
        *config = s_turntable.trigger;
    }
}

void model_turntable_get_motion_config(model_turntable_motion_config_t *config)
{
    if (config != NULL) {
        *config = s_turntable.motion;
    }
}
