#ifndef VDC_LOCAL_FOLLOW_H
#define VDC_LOCAL_FOLLOW_H

#include "vdc_feedback_match.h"
#define VDC_LOCAL_FOLLOW_CANDIDATE_TAG UINT32_C(0x4c464f4c)
#define VDC_LOCAL_FOLLOW_MAX_AGE_MS 120u

/* Mode-exclusive view of the existing matcher publication, never a wire
 * command. result.source is LOCAL; reference is the independent NO1 lifetime.
 * pairs store local projected RX over remote projected reference coordinates.
 * The unique application key is (owner_token, serial), not receive_count. */
typedef struct {
    uint32_t tag, serial, owner_token, session;
    uint32_t role_generation, schedule_crc32, ring_config_seq, local_slot;
    uint32_t reference_slot, local_model_token, expected_dco_update_seq;
    uint32_t reference_model_token, reference_receive_count;
    uint32_t prepared_ms, local_event_ms, active;
    vdc_feedback_match_snapshot_t result;
    vdc_feedback_match_lifetime_t reference;
    uint32_t directed_delay_ns, path_table_crc32;
} vdc_local_follow_candidate_t;

/* Core0 guarded view of the published measured observation matrix; outputs
 * untouched on failure. The CRC binds calibration/topology/bias generations. */
bool vdc_dpll_manager_copy_local_follow_path(uint32_t local, uint32_t reference,
    uint32_t schedule_crc32, uint32_t *delay_ns, uint32_t *table_crc32);
/* Stable STOP-request generation: zero means disabled, false means busy.
 * An enabled generation is even/nonzero and is never reused in this boot. */
bool vdc_dpll_manager_try_local_follow_request(uint32_t *generation);

/* One guarded copy, failure preserves out. Revalidates owner token, mode,
 * session, ring and live local/remote lifetimes. Does NOT read committed-model
 * guard: Core1 calls inside that guard. The sole DCO owner MUST additionally
 * check local_model_token, expected_dco_update_seq, local_event_ms age and
 * the complete binding immediately before consuming a fresh unique key. */
bool vdc_dpll_manager_get_local_follow_candidate(vdc_local_follow_candidate_t *out);

#endif
