#ifndef VDC_RUN_OUTPUT_H
#define VDC_RUN_OUTPUT_H
#include "sync_io_run_output.h"

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
} vdc_run_output_status_t;

bool vdc_run_output_prepare(uint32_t period_ns,uint32_t high_ns,
                            uint32_t duration_ms,uint32_t *request);
void vdc_run_output_cancel(void);
bool vdc_run_output_status(vdc_run_output_status_t *out);
void vdc_run_output_service_core1(void);
#endif
