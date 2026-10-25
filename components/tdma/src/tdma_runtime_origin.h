#ifndef TDMA_RUNTIME_ORIGIN_H
#define TDMA_RUNTIME_ORIGIN_H

#include <stdbool.h>
#include <stdint.h>

#include "calibration_origin_timing.h"
#include "tdma_origin_owner_ram.h"
#include "tdma_origin_stop_cause.h"
#include "tdma_runtime_owner.h"

/* The origin trial record stays private to the origin unit; the owner
 * reads it through this accessor. */
const calibration_origin_timing_t *tdma_runtime_origin_trial(void);

bool tdma_runtime_owner_request_origin_release(uint32_t trial_epoch, uint32_t config_seq, uint32_t *request_seq);
bool tdma_runtime_owner_get_origin_capability(refmem_realtime_origin_capability_t *capability);
bool tdma_runtime_owner_origin_stop_cause_copy(tdma_origin_stop_cause_t *out);
tdma_origin_admission_result_t tdma_runtime_owner_origin_admit( void *context, const tdma_ring_runtime_config_t *config, uint32_t *rearm_budget_ticks, uint32_t *abort_poll_count);
bool tdma_runtime_owner_origin_begin(void *context, const tdma_ring_runtime_config_t *config, const uint8_t *packet, size_t size, uint32_t rearm_budget_ticks, uint32_t abort_poll_count);
tdma_origin_build_result_t tdma_runtime_owner_origin_poll(void *context);
bool tdma_runtime_owner_origin_healthy(const void *context);
void tdma_runtime_owner_origin_lifetime_core1(void);
bool tdma_runtime_owner_skip_tdma_service(void);
bool tdma_runtime_owner_get_origin_blackout(tdma_origin_blackout_snapshot_t *out);
bool tdma_runtime_owner_get_origin_build_probe(tdma_origin_build_probe_t *out);
bool tdma_runtime_owner_get_origin_handoff(tdma_origin_handoff_snapshot_t *out);
bool tdma_runtime_owner_get_origin_release(tdma_origin_release_snapshot_t *out);

#endif /* TDMA_RUNTIME_ORIGIN_H */
