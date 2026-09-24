#ifndef TRIGGER_READY_OVERLAP_H
#define TRIGGER_READY_OVERLAP_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TRIGGER_READY_OVERLAP_OK=0u,
    TRIGGER_READY_OVERLAP_MISSING=1u,
    TRIGGER_READY_OVERLAP_EARLY=2u,
    TRIGGER_READY_OVERLAP_LATE=3u,
    TRIGGER_READY_OVERLAP_ACTION_CONFLICT=4u,
    TRIGGER_READY_OVERLAP_OVERFLOW=5u,
} trigger_ready_overlap_result_t;

typedef struct {
    uint32_t position_id;
    uint64_t expected_start_q, expected_end_q;
    uint64_t observed_q;
    trigger_ready_overlap_result_t result;
} trigger_ready_overlap_report_t;

static inline trigger_ready_overlap_result_t trigger_ready_overlap_check(
    uint32_t position_id, uint64_t previous_action_q, uint32_t previous_span,
    int64_t ready_offset_q, uint32_t ready_window_q, bool ready_observed,
    uint64_t observed_q,
    uint64_t next_action_q, uint32_t next_action_span,
    trigger_ready_overlap_report_t *out)
{
    if (!out || !previous_span || !ready_window_q || !next_action_span)
        return TRIGGER_READY_OVERLAP_OVERFLOW;
    if (ready_offset_q < 0 && (uint64_t)(-(ready_offset_q + 1)) + 1u > previous_action_q)
        return TRIGGER_READY_OVERLAP_OVERFLOW;
    const uint64_t start = ready_offset_q < 0
        ? previous_action_q - ((uint64_t)(-(ready_offset_q + 1)) + 1u)
        : previous_action_q + (uint64_t)ready_offset_q;
    if (start < previous_action_q && ready_offset_q >= 0) return TRIGGER_READY_OVERLAP_OVERFLOW;
    if (start > UINT64_MAX - ready_window_q) return TRIGGER_READY_OVERLAP_OVERFLOW;
    const uint64_t end = start + ready_window_q;
    if (next_action_q > UINT64_MAX - next_action_span)
        return TRIGGER_READY_OVERLAP_OVERFLOW;
    const uint64_t next_end = next_action_q + next_action_span;
    out->position_id=position_id;out->expected_start_q=start;out->expected_end_q=end;
    out->observed_q=observed_q;
    if (!ready_observed){out->result=TRIGGER_READY_OVERLAP_MISSING;return out->result;}
    const uint64_t observed=observed_q;
    if (observed < start){out->result=TRIGGER_READY_OVERLAP_EARLY;return out->result;}
    if (observed >= end){out->result=TRIGGER_READY_OVERLAP_LATE;return out->result;}
    if (observed >= next_action_q && observed < next_end){
        out->result=TRIGGER_READY_OVERLAP_ACTION_CONFLICT;return out->result;
    }
    out->result=TRIGGER_READY_OVERLAP_OK;return out->result;
}

#endif
