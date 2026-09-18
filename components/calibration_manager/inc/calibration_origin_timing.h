#ifndef CALIBRATION_ORIGIN_TIMING_H
#define CALIBRATION_ORIGIN_TIMING_H

#include <stdbool.h>
#include <stdint.h>
#include "tdma_ring_runtime.h"
#include "refmem_realtime_contract.h"

#define CALIBRATION_ORIGIN_TIMING_VERSION 3u
#define CALIBRATION_ORIGIN_DIAGNOSTIC_SKIP_RECORDS 1u
#define CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT 2u
#define CALIBRATION_ORIGIN_DIAGNOSTIC_BUILD_CANCEL 4u
#define CALIBRATION_ORIGIN_DIAGNOSTIC_DEFER_RELEASE 8u

/* Volatile diagnostic experiment, never a measured product timing grant.
 * The owner validates this exact epoch before preparation and installation.
 * Raw requested bounds are clk_sys cycles, not the CPU phase's time budget. */
typedef struct {
    uint32_t version;
    uint32_t epoch;
    uint32_t enabled;
    uint32_t trial_id;
    uint32_t config_seq;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t calibration_crc32;
    uint32_t rearm_budget_ticks;
    uint32_t abort_poll_count;
    /* Zero explicitly requests continuous diagnostic operation until revoked.
     * All epoch/configuration/resource checks still apply. */
    uint64_t expires_ticks;
    tdma_ring_runtime_config_t config;
    refmem_realtime_origin_admission_t admission;
    uint32_t diagnostic_flags;
} calibration_origin_timing_t;

/* Last serialized Core0 control attempt only. Never consumed by Core1 as a
 * grant, and never reconstructed from a later runtime snapshot. */
typedef enum {
    CALIBRATION_ORIGIN_ATTEMPT_NONE = 0,
    CALIBRATION_ORIGIN_ATTEMPT_PENDING,
    CALIBRATION_ORIGIN_ATTEMPT_ACCEPTED,
    CALIBRATION_ORIGIN_ATTEMPT_ARGUMENT,
    CALIBRATION_ORIGIN_ATTEMPT_RING_UNAVAILABLE,
    CALIBRATION_ORIGIN_ATTEMPT_RING_DISABLED,
    CALIBRATION_ORIGIN_ATTEMPT_RING_NOT_STARTED,
    CALIBRATION_ORIGIN_ATTEMPT_CONFIG_PENDING,
    CALIBRATION_ORIGIN_ATTEMPT_CONFIG_UNAVAILABLE,
    CALIBRATION_ORIGIN_ATTEMPT_DIAGNOSTIC_REQUIRED,
    CALIBRATION_ORIGIN_ATTEMPT_ROLE,
    CALIBRATION_ORIGIN_ATTEMPT_STAGE_UNAVAILABLE,
    CALIBRATION_ORIGIN_ATTEMPT_STAGE_INCOMPLETE,
    CALIBRATION_ORIGIN_ATTEMPT_CAPABILITY_UNAVAILABLE,
    CALIBRATION_ORIGIN_ATTEMPT_OWNER_UNAVAILABLE,
    CALIBRATION_ORIGIN_ATTEMPT_STAGE_NODES,
    CALIBRATION_ORIGIN_ATTEMPT_STAGE_PROFILE,
    CALIBRATION_ORIGIN_ATTEMPT_STAGE_SCHEDULE,
    CALIBRATION_ORIGIN_ATTEMPT_CADENCE,
    CALIBRATION_ORIGIN_ATTEMPT_REARM_MARGIN,
    CALIBRATION_ORIGIN_ATTEMPT_EXPIRY,
    CALIBRATION_ORIGIN_ATTEMPT_MODEL_REJECTED,
    CALIBRATION_ORIGIN_ATTEMPT_RECHECK_UNAVAILABLE,
    CALIBRATION_ORIGIN_ATTEMPT_RECHECK_DISABLED,
    CALIBRATION_ORIGIN_ATTEMPT_RECHECK_NOT_STARTED,
    CALIBRATION_ORIGIN_ATTEMPT_RECHECK_CONFIG,
    CALIBRATION_ORIGIN_ATTEMPT_RECHECK_APPLIED,
    CALIBRATION_ORIGIN_ATTEMPT_MODEL_CHANGED,
    CALIBRATION_ORIGIN_ATTEMPT_EPOCH_EXHAUSTED
} calibration_origin_attempt_reason_t;

enum {
    CALIBRATION_ORIGIN_OBS_RING = 1u << 0,
    CALIBRATION_ORIGIN_OBS_CONFIG = 1u << 1,
    CALIBRATION_ORIGIN_OBS_STAGE = 1u << 2,
    CALIBRATION_ORIGIN_OBS_CAPABILITY = 1u << 3,
    CALIBRATION_ORIGIN_OBS_OWNER = 1u << 4,
    CALIBRATION_ORIGIN_OBS_CADENCE = 1u << 5,
    CALIBRATION_ORIGIN_OBS_CLOCK = 1u << 6,
    CALIBRATION_ORIGIN_OBS_ADMISSION = 1u << 7,
    CALIBRATION_ORIGIN_OBS_RECHECK = 1u << 8,
    CALIBRATION_ORIGIN_OBS_MODEL_EPOCH = 1u << 9
};

typedef struct {
    uint32_t attempt; /* Saturating diagnostic count, not a unique lease. */
    uint32_t trial_id;
    uint32_t trial_epoch;
    uint32_t reason;
    uint32_t config_seq;
    uint32_t applied_config_seq;
    uint32_t recheck_config_seq;
    uint32_t recheck_applied_config_seq;
    uint32_t admitted_model_epoch;
    uint32_t observed_model_epoch;
    /* Reason-specific mismatch only; zero on ACCEPTED or unavailable helper
     * does not claim a measured zero. observed_mask qualifies generation
     * fields and distinguishes an unexecuted stage from a successful read.
     * REARM_MARGIN requires observed > expected; EXPIRY requires <=;
     * DIAGNOSTIC_REQUIRED requires the expected bit; RING_* requires nonzero
     * (expected=1 is descriptive, not a new equality admission rule). */
    uint64_t observed;
    uint64_t expected;
    uint32_t observed_mask; /* Unobserved fields are zero, not measured zero. */
    uint32_t requested_rearm_ticks;
    uint32_t requested_abort_polls;
    uint32_t requested_flags;
    uint64_t requested_duration_ticks;
} calibration_origin_attempt_t;

/* Core0 serialized control/SCPI only; STOP/revoke leaves this diagnostic
 * attempt intact. ACCEPTED describes a past attempt, never current authority. */
bool calibration_manager_origin_get_attempt(calibration_origin_attempt_t *attempt);

bool calibration_manager_origin_trial(uint32_t trial_id, uint32_t rearm_budget_ticks,
    uint32_t abort_poll_count, uint64_t duration_ticks);
bool calibration_manager_origin_trial_configured(uint32_t trial_id, uint32_t rearm_budget_ticks,
    uint32_t abort_poll_count, uint64_t duration_ticks, uint32_t diagnostic_flags);
void calibration_manager_origin_revoke(void);
uint32_t calibration_manager_origin_epoch(void);
/* One bounded attempt; false is an unavailable snapshot, never permission. */
bool calibration_manager_origin_get_timing(calibration_origin_timing_t *timing);

#endif
