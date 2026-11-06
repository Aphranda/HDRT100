#ifndef APP_REFMEM_DOMAIN_CONFIG_H
#define APP_REFMEM_DOMAIN_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "guide_action_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the N6 domain-config handlers that call the owning TDMA/VDC/T2
 * APIs.  Called once from app_init after the domain owners are initialized
 * and before SCPI commands are reachable.  RefMem itself stays independent of
 * those components; it only dispatches through this registry. */
bool app_refmem_domain_config_init(void);

/* Consumes a guide trigger delivered by CAL_GUIDE_TRIGGER (T segment). Returns
 * false when no trigger is pending; the plan is copied on success. */
bool app_cal_guide_take_trigger(guide_action_plan_t *plan, uint32_t *epoch);

/* Post a guide trigger for the plan named by `action`.  Used by the compact
 * ring message (targets and the origin alike) and by the single-port SCPI
 * handler, so every board runs the same admission rule. */
bool app_cal_guide_post_trigger(uint32_t action, uint32_t epoch);
/* R segment: record progress while the ring is down (refmem parameter pool,
 * no link needed). Q reads it back after re-form; `epoch` is the trigger epoch
 * and doubles as the pool slot sequence, so a stale record never matches. */
bool app_cal_guide_publish_progress(const guide_action_progress_t *progress,
                                    uint32_t epoch);
bool app_cal_guide_read_progress(guide_action_progress_t *progress, uint32_t epoch);

/* Read-only view of the staged plan and trigger, for the single-port readback. */
typedef struct {
    bool plan_valid;
    bool trigger_pending;
    uint32_t action_id;
    uint32_t plan_crc32;
    uint32_t generation;
    uint32_t profile_level;
    uint32_t window_ms;
    uint32_t trial_count;
    uint32_t trigger_epoch;
    /* Q readback: the progress record the R window left in the local pool and
     * the reconcile decision; zero-initialised until the first Q completes. */
    uint32_t progress_valid;
    uint32_t progress_status;
    uint32_t trials_done;
    uint32_t deepest_trial;
    uint32_t progress_reason;
    /* Per-trial verdicts: bit N is trial N.  P1 reports COMPLETE only when
     * failed_mask is clear, so the status word alone is no longer the whole
     * story and the caller reads both. */
    uint32_t passed_mask;
    uint32_t failed_mask;
    /* Executor state at the last abort (0 = none): distinguishes the coarse
     * TIMEOUT/NOT_ONLINE_SAFE reasons by the phase that actually failed. */
    uint32_t abort_site;
    uint32_t reconcile_decision;
    uint32_t reconcile_resume_trial;
    /* S segment: the commit gate decision, whether this action has a staged
     * flash payload, and the commit result (1 = written, 0 = none/failed). */
    uint32_t commit_decision;
    uint32_t commit_applicable;
    uint32_t commit_ok;
    /* Ladder: the action index the next round must use (0 = P1) and the last
     * guide_action_ladder_advance() step (0 same, 1 next, 2 done, 3 stopped). */
    uint32_t ladder_index;
    uint32_t ladder_step;
} app_cal_guide_snapshot_t;
bool app_cal_guide_snapshot(app_cal_guide_snapshot_t *snapshot);
/* Executor state at the last abort (0 = none), see snapshot.abort_site. */
uint32_t app_cal_guide_abort_site(void);

/* Q/S result store: the executor records the readback, reconcile decision, the
 * S commit gate and the ladder step so the single-port readback can expose
 * them. */
typedef struct {
    bool have_record;
    bool have_result;
    guide_action_progress_t progress;
    guide_action_reconcile_result_t reconcile;
    uint32_t commit_decision;
    uint32_t commit_applicable;
    uint32_t commit_ok;
    uint32_t ladder_index;
    uint32_t ladder_step;
} app_cal_guide_q_t;
void app_cal_guide_set_q_result(const guide_action_progress_t *progress,
                                bool have_record,
                                const guide_action_reconcile_result_t *result);
void app_cal_guide_set_commit_result(uint32_t decision, uint32_t applicable,
                                     uint32_t ok);
void app_cal_guide_set_ladder_result(uint32_t action_index, uint32_t step);
bool app_cal_guide_get_q_result(app_cal_guide_q_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_REFMEM_DOMAIN_CONFIG_H */
