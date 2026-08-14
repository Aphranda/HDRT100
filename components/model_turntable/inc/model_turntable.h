#ifndef MODEL_TURNTABLE_H
#define MODEL_TURNTABLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MODEL_TURNTABLE_PHASE_IDLE = 0u,
    MODEL_TURNTABLE_PHASE_ACCEL = 1u,
    MODEL_TURNTABLE_PHASE_CRUISE = 2u,
    MODEL_TURNTABLE_PHASE_DECEL = 3u,
    MODEL_TURNTABLE_PHASE_DONE = 4u,
    MODEL_TURNTABLE_PHASE_FAULT = 5u,
} model_turntable_phase_t;

typedef struct {
    uint32_t dimension;
    double start;
    double stop;
    double step;
    uint32_t pulse_width_us;
    bool rising_edge;
    int32_t timeout_ms;
} model_turntable_trigger_config_t;

typedef struct {
    double velocity_units_per_s;
    double acceleration_units_per_s2;
} model_turntable_motion_config_t;

typedef struct {
    bool initialized;
    bool loaded;
    bool configured;
    bool running;
    uint32_t phase;
    uint32_t slot_id;
    uint32_t output_index;
    uint32_t dimension;
    uint32_t total_pulses;
    uint32_t emitted_pulses;
    uint32_t accel_pulses;
    uint32_t cruise_pulses;
    uint32_t decel_pulses;
    uint32_t min_interval_us;
    uint32_t max_interval_us;
    uint32_t pulse_width_us;
    uint32_t last_interval_us;
    uint32_t fault_code;
    double current_position;
} model_turntable_status_t;

bool model_turntable_init(void);
bool model_turntable_load(uint32_t slot_id, uint32_t output_index);
bool model_turntable_configure_trigger(const model_turntable_trigger_config_t *config);
bool model_turntable_configure_motion(const model_turntable_motion_config_t *config);
bool model_turntable_start(void);
void model_turntable_stop(void);
void model_turntable_service(void);
void model_turntable_get_status(model_turntable_status_t *status);
void model_turntable_get_trigger_config(model_turntable_trigger_config_t *config);
void model_turntable_get_motion_config(model_turntable_motion_config_t *config);

#endif
