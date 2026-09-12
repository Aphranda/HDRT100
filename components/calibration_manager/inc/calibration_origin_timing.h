#ifndef CALIBRATION_ORIGIN_TIMING_H
#define CALIBRATION_ORIGIN_TIMING_H

#include <stdbool.h>
#include <stdint.h>
#include "tdma_ring_runtime.h"
#include "refmem_realtime_contract.h"

#define CALIBRATION_ORIGIN_TIMING_VERSION 1u

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
    uint64_t expires_ticks;
    tdma_ring_runtime_config_t config;
    refmem_realtime_origin_admission_t admission;
} calibration_origin_timing_t;

bool calibration_manager_origin_trial(uint32_t trial_id, uint32_t rearm_budget_ticks,
    uint32_t abort_poll_count, uint64_t duration_ticks);
void calibration_manager_origin_revoke(void);
uint32_t calibration_manager_origin_epoch(void);
/* One bounded attempt; false is an unavailable snapshot, never permission. */
bool calibration_manager_origin_get_timing(calibration_origin_timing_t *timing);

#endif
