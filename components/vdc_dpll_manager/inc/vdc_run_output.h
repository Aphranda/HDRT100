#ifndef VDC_RUN_OUTPUT_H
#define VDC_RUN_OUTPUT_H
#include "sync_io_run_output.h"
#include "vdc_output_timing.h"

#define VDC_RUN_OUTPUT_SCHEMA 9u

enum { VDC_RUN_OUTPUT_PREPARED_PHASE, VDC_RUN_OUTPUT_RUNNING_PHASE,
       VDC_RUN_OUTPUT_PHASE_COUNT };
typedef enum {
    VDC_RUN_OUTPUT_RING_UNAVAILABLE,
    VDC_RUN_OUTPUT_RING_WAIT,
    VDC_RUN_OUTPUT_MODEL_UNAVAILABLE,
    VDC_RUN_OUTPUT_IDENTITY_WAIT,
    VDC_RUN_OUTPUT_DMA_NOT_READY,
    VDC_RUN_OUTPUT_BRIDGE_UNAVAILABLE,
    VDC_RUN_OUTPUT_BACKEND_SNAPSHOT_UNAVAILABLE,
    VDC_RUN_OUTPUT_POSTPLAN_RING_UNAVAILABLE,
    VDC_RUN_OUTPUT_PLAN_REJECTED,
    VDC_RUN_OUTPUT_SUBMIT_REJECTED,
    VDC_RUN_OUTPUT_SUBMITTED,
    VDC_RUN_OUTPUT_BINDING_CANCELLED,
    VDC_RUN_OUTPUT_OUTCOME_COUNT,
    VDC_RUN_OUTPUT_OUTCOME_NONE=VDC_RUN_OUTPUT_OUTCOME_COUNT
} vdc_run_output_outcome_t;

typedef struct {
    sync_io_run_output_snapshot_t hardware;
    uint64_t last_local_ns, last_target_vdc_ns, maximum_bridge_width_ticks;
    uint32_t request, session, period_ns, high_ns, duration_ms;
    uint32_t ring_config, role_generation, clock_epoch, clock_run;
    uint32_t prepared_config, arm_config;
    uint32_t blocks_planned, plan_rejects, binding_rejects;
    int32_t delay_ns;
    /* Saturating per-service outcomes under the client ownership gate.
     * PREPARED includes the successful first block; RUNNING counts refills.
     * CAS contention is deliberately uncounted. Terminal backend states
     * preserve the final active outcome, rather than overwrite its cause. */
    uint32_t outcomes[VDC_RUN_OUTPUT_PHASE_COUNT][VDC_RUN_OUTPUT_OUTCOME_COUNT];
    uint32_t last_phase, last_outcome;
    /* Saturating cache diagnostics: busy-source plans, prior-cache successful
     * admissions, and model/tail invalidations (not STOP/terminal cleanup). */
    uint32_t prefetched_blocks, cache_hits, cache_invalidations;
    /* Cached-only fallback observations, exported after retirement. The
     * body interval excludes ownership-gate entry/exit; scheduler wall
     * accounting remains authoritative. Microsecond quantization rounded up. */
    uint32_t fast_calls, fast_submissions, fast_empty, fast_body_max_us;
    /* Caller-measured function wall intervals, including ownership entry /
     * exit. Within a retained request, missing reports can be compared with
     * fast_calls (which excludes CAS refusal); release/reprepare resets the
     * old statistics. Reporting itself is dispatcher bookkeeping. */
    uint32_t fast_wall_samples, fast_wall_max_cycles, fast_budget_overruns;
    /* Latched STOP configuration and finite timeline observations. Counts
     * are diagnostic, not hardware inventory or precision qualifications. */
    uint32_t plan_ahead_us, commit_ahead_us, refill_low_us;
    uint32_t timeline_bridge_samples, partial_plan_steps;
    /* First accepted RUN bridge, retained after retirement for correlation with
     * the backend start/enable enclosure. These are diagnostic bounds, not a
     * physical edge timestamp or a cross-board phase measurement. */
    uint64_t timeline_raw_before, timeline_local_ns, timeline_raw_after;
    uint32_t plan_waits, refill_waits, commit_waits;
    uint32_t block_edges, schedule_cycles;
    /* STOP-only correlation for the retained outcome and cache invalidation.
     * The service sequence is local to this request; tick is the backend's
     * last valid service observation, not a physical edge timestamp. */
    uint32_t service_sequence, last_outcome_service_sequence;
    uint32_t last_invalidation_service_sequence;
    uint64_t last_outcome_tick, last_invalidation_tick;
    /* SYNC_IO's most recent rejected-submit branch; zero means no rejection
     * has been observed in this request. NOT_READY may retain the private
     * suffix for a later same-generation service retry; other branches clear
     * it, with GUARD treated as an expired hardware runway. */
    uint32_t last_submit_failure;
} vdc_run_output_status_t;

bool vdc_run_output_prepare(uint32_t period_ns,uint32_t high_ns,
                            uint32_t duration_ms,uint32_t *request);
void vdc_run_output_cancel(void);
bool vdc_run_output_status(vdc_run_output_status_t *out);
/* Core0 configuration callback, inside the STOP metadata owner gate. */
bool vdc_run_output_configuration_idle(void);
void vdc_run_output_service_core1(void);
/* No first block, bridge acquisition or model inverse. Only existing private
 * suffixes may be admitted, with the same lifetime and clock validation. */
uint32_t vdc_run_output_service_cached_core1(void);
void vdc_run_output_note_cached_wall_core1(uint32_t request,
    uint32_t cycles,uint32_t budget_cycles);
#endif
