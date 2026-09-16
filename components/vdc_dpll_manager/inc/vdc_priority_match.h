#ifndef VDC_PRIORITY_MATCH_H
#define VDC_PRIORITY_MATCH_H

#include <stdbool.h>
#include <stdint.h>

enum {
    VDC_PRIORITY_MATCH_OK = 0u, VDC_PRIORITY_MATCH_DISABLED = 1u,
    VDC_PRIORITY_MATCH_RETIRED = 2u, VDC_PRIORITY_MATCH_CONFIG = 3u,
    VDC_PRIORITY_MATCH_ROLE = 4u, VDC_PRIORITY_MATCH_BINDING = 5u,
    VDC_PRIORITY_MATCH_MODEL = 6u, VDC_PRIORITY_MATCH_RX = 7u,
    VDC_PRIORITY_MATCH_GENERATION = 8u, VDC_PRIORITY_MATCH_SOURCE = 9u,
    VDC_PRIORITY_MATCH_PENDING = 10u, VDC_PRIORITY_MATCH_EVICTED = 11u,
    VDC_PRIORITY_MATCH_BUSY = 12u, VDC_PRIORITY_MATCH_SEQUENCE = 13u,
    VDC_PRIORITY_MATCH_PROJECTION = 14u, VDC_PRIORITY_MATCH_CHANGED = 15u,
    VDC_PRIORITY_MATCH_OVERFLOW = 16u, VDC_PRIORITY_MATCH_PATH = 17u,
    VDC_PRIORITY_MATCH_STOP = 18u
};
#define VDC_PRIORITY_MATCH_PATH_PROVISIONAL_TRANSPOSE 1u

/* Core1 observational matching only: no DCO command or lock/precision grant.
 * Counter meanings are service attempts except matched (at most once per
 * source event) and superseded (an unresolved candidate replaced by another).
 * Identity/interval fields describe the last successful match and survive
 * retirement. A different fresh request clears them and all counters.
 * The path is a provisional forward-CS scalar obtained by transposing the
 * installed reverse-DATA matrix, not a calibrated CS endpoint interval. */
typedef struct {
    uint32_t schema, generation, active, retired, have_match;
    uint32_t calls, matched, repeated, superseded, busy, pending, history_miss;
    uint32_t rejected, last_reason, history_result;
    uint32_t source_slot, event_sequence, carrier_sequence, rx_epoch;
    uint32_t session, role_generation, clock_epoch, clock_run, model_token, applied_command_seq;
    uint32_t ring_config_seq, ring_applied_seq, local_slot, reference_slot, node_count;
    uint32_t schedule_crc32, profile_crc32, observer_epoch, tick_hz, path_crc32, delay_ns;
    uint32_t path_direction, path_qualification;
    uint64_t arm_epoch, valid_from_raw, raw_lo, raw_hi;
    uint64_t local_lo, local_hi, remote_lo, remote_hi, expected_lo, expected_hi;
    int64_t residual_lo, residual_hi;
} vdc_priority_match_snapshot_t;

/* Core0, stopped metadata gate; nonzero requires an existing feedback session
 * and must exceed every previously accepted nonzero request. The session is
 * latched with that request; any later session change retires it. Zero disables.
 * Before the first successful match, the request may await its first ARM.
 * active describes the last service, not a live grant after STOP. */
bool vdc_dpll_manager_set_priority_match(uint32_t expected_remote_generation);
uint32_t vdc_dpll_manager_priority_match_generation(void);
/* One bounded atomic-word read; false preserves *out. Reader may not span
 * 2^31 producer publications (one complete guard cycle). STOP diagnostics. */
bool vdc_dpll_manager_get_priority_match(vdc_priority_match_snapshot_t *out);
/* Core1 DPLL boundary before opening the committed-model writer guard. */
void vdc_priority_match_core1(void);

#endif
