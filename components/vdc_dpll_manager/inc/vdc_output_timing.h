#ifndef VDC_OUTPUT_TIMING_H
#define VDC_OUTPUT_TIMING_H

#include <stdbool.h>
#include <stdint.h>

/* Requested durations in us, separate from edge timestamp precision.
 * PREPARE additionally validates output period, capacity and table cadence. */
typedef struct {
    /* Remaining committed time at which incremental preparation is allowed. */
    uint32_t plan_ahead_us;
    /* Maximum new tail ahead of fresh raw time during RUN admission. */
    uint32_t commit_ahead_us;
    /* Urgent refill threshold; hardware DMA readiness is still mandatory. */
    uint32_t refill_low_us;
} vdc_output_timing_profile_t;

#define VDC_OUTPUT_TIMING_MIN_US 1000u
#define VDC_OUTPUT_TIMING_MAX_US 1000000u
/* Candidates for the current table/output cadence; PREPARE checks the pair. */
#define VDC_OUTPUT_TIMING_PLAN_DEFAULT_US 12000u
#define VDC_OUTPUT_TIMING_COMMIT_DEFAULT_US 16000u
#define VDC_OUTPUT_TIMING_REFILL_DEFAULT_US 6000u

static inline bool vdc_output_timing_profile_valid(const vdc_output_timing_profile_t *profile)
{
    return profile && profile->refill_low_us >= VDC_OUTPUT_TIMING_MIN_US &&
        profile->refill_low_us < profile->plan_ahead_us &&
        profile->plan_ahead_us <= profile->commit_ahead_us &&
        profile->commit_ahead_us <= VDC_OUTPUT_TIMING_MAX_US;
}

bool vdc_dpll_manager_get_output_timing_profile(vdc_output_timing_profile_t *profile);
bool vdc_dpll_manager_set_output_timing_profile(const vdc_output_timing_profile_t *profile);
bool vdc_dpll_manager_default_output_timing(void);
bool vdc_dpll_manager_recall_output_timing(void);
bool vdc_dpll_manager_store_output_timing(void);

#endif
