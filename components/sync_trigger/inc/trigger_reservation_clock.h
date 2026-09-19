#ifndef TRIGGER_RESERVATION_CLOCK_H
#define TRIGGER_RESERVATION_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "vdc_dpll_manager.h"

typedef enum {
    TRIGGER_RESERVATION_CLOCK_OK = 0u,
    TRIGGER_RESERVATION_CLOCK_ARGUMENT,
    TRIGGER_RESERVATION_CLOCK_IDENTITY,
    TRIGGER_RESERVATION_CLOCK_NOT_READY,
    TRIGGER_RESERVATION_CLOCK_MODEL,
    TRIGGER_RESERVATION_CLOCK_QUALITY,
    TRIGGER_RESERVATION_CLOCK_CALIBRATION,
    TRIGGER_RESERVATION_CLOCK_MAPPING,
    TRIGGER_RESERVATION_CLOCK_LATE,
    TRIGGER_RESERVATION_CLOCK_CHANGED,
} trigger_reservation_clock_result_t;

typedef struct {
    uint32_t run_id;
    uint32_t deployment_generation;
    uint32_t plan_generation;
} trigger_reservation_identity_t;

typedef struct {
    trigger_reservation_identity_t identity;
    uint32_t publication_revision;
    uint32_t schedule_crc32;
    uint32_t clock_model_seq;
    uint32_t clock_epoch_id;
    uint32_t clock_run_id;
    uint32_t dco_update_seq;
    uint32_t calibration_generation;
    uint32_t calibration_crc32;
    uint32_t quality_update_seq;
    uint32_t quality_age_us;
    uint32_t maximum_age_us;
    uint64_t last_accepted_local_ns;
    uint64_t valid_from_local_ns;
    vdc_dco_control_t dco;
} trigger_reservation_clock_snapshot_t;

typedef struct {
    trigger_reservation_clock_snapshot_t clock;
    uint64_t target_vdc_ns;
    uint64_t model_local_ns;
    uint64_t timer1_deadline_ticks;
    uint64_t prepared_at_ticks;
    uint64_t minimum_lead_ticks;
} trigger_reservation_clock_prepared_t;

trigger_reservation_clock_result_t trigger_reservation_clock_snapshot_from_vector(
    const vdc_dpll_manager_vector_snapshot_t *vector,
    const trigger_reservation_identity_t *identity,
    uint32_t maximum_age_us,
    trigger_reservation_clock_snapshot_t *snapshot);
trigger_reservation_clock_result_t trigger_reservation_clock_capture(
    const trigger_reservation_identity_t *identity,
    uint32_t maximum_age_us,
    trigger_reservation_clock_snapshot_t *snapshot);
trigger_reservation_clock_result_t trigger_reservation_clock_prepare(
    const trigger_reservation_clock_snapshot_t *snapshot,
    uint64_t target_vdc_ns, uint32_t timer1_hz,
    uint64_t now_ticks, uint64_t minimum_lead_ticks,
    trigger_reservation_clock_prepared_t *prepared);
bool trigger_reservation_clock_identity_matches(
    const trigger_reservation_clock_prepared_t *prepared,
    const trigger_reservation_clock_snapshot_t *current);
const char *trigger_reservation_clock_result_name(
    trigger_reservation_clock_result_t result);

#endif
