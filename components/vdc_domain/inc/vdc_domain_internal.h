#ifndef VDC_DOMAIN_INTERNAL_H
#define VDC_DOMAIN_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "vdc_domain.h"

/* N5.0.4e: cross-unit surface.  Every helper keeps its single definition
 * in vdc_domain.c; the evidence/profile units call it through here. */

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define VDC_DOMAIN_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_DOMAIN_TIME_CRITICAL(name) name
#endif

#define VDC_DOMAIN_CRC_OFFSET 2166136261u
#define VDC_DOMAIN_CRC_PRIME 16777619u
#define VDC_DOMAIN_INITIAL_LOCK_SAMPLES 1u
#define VDC_DOMAIN_FREQ_LOCK_SAMPLES 2u
#define VDC_DOMAIN_PHASE_LOCK_SAMPLES 3u
#define VDC_DOMAIN_DEFAULT_FRESHNESS_LIMIT_US 1000000u
#define VDC_DOMAIN_DEFAULT_HOLDOVER_DRIFT_BOUND_NS_S 1000u
#define VDC_DOMAIN_ACQUISITION_GUARD_NS 0u
#define VDC_DOMAIN_RATE_MIN_OBSERVATION_CYCLES 8u
#define VDC_DOMAIN_RATE_SLEW_DIVISOR 8u
#define VDC_DOMAIN_RATE_MIN_SLEW_LIMIT_PPB 1000u
uint32_t vdc_domain_hash_u32(uint32_t hash, uint32_t value);
uint32_t vdc_domain_abs_i32(int32_t value);
uint32_t vdc_domain_increment_nonzero(uint32_t value);
void vdc_domain_increment_saturating(uint32_t *value);
bool vdc_domain_is_follower(const vdc_domain_context_t *context);
int32_t VDC_DOMAIN_TIME_CRITICAL( vdc_domain_scaled_ratio_clamped_i32)(int64_t value, uint32_t scale, uint64_t divisor, uint32_t limit);
int32_t VDC_DOMAIN_TIME_CRITICAL( vdc_domain_ppb_elapsed_correction_ns)(int32_t ppb, uint32_t elapsed_ns);
void vdc_domain_gate_fail(vdc_gate_result_t *gate, vdc_domain_gate_code_t code, uint32_t slot, uint32_t evidence);
vdc_domain_gate_code_t vdc_domain_timestamp_admission_to_gate( vdc_timestamp_admission_code_t code);
bool vdc_domain_range_in_period(uint32_t offset_ns, uint32_t width_ns, uint32_t period_ns);
bool vdc_domain_ranges_overlap(uint32_t offset_a_ns, uint32_t width_a_ns, uint32_t offset_b_ns, uint32_t width_b_ns);
uint32_t vdc_domain_ring_forward_distance(uint32_t from_slot_id, uint32_t to_slot_id, uint32_t node_count);
bool vdc_domain_guarded_observation_overlaps(uint32_t obs_offset_ns, uint32_t obs_width_ns, uint32_t guard_before_ns, uint32_t guard_after_ns, uint32_t other_offset_ns, uint32_t other_width_ns);
bool vdc_domain_window_contract( const vdc_tdma_schedule_profile_t *profile, uint32_t window_class, uint32_t *offset_ns, uint32_t *width_ns);
bool vdc_domain_payload_allowed_for_window(uint32_t window_class, uint32_t payload_class);
bool vdc_domain_payload_requires_reference_sync(uint32_t payload_class);
uint32_t vdc_domain_saturate_u64_to_u32(uint64_t value);
uint64_t vdc_domain_tracking_window_start_ns( const vdc_domain_context_t *context, uint64_t expected_window_start_ns, uint32_t half_width_ns);
uint32_t vdc_domain_tracking_window_width_ns(uint32_t half_width_ns);
uint32_t vdc_domain_avg_u32(uint32_t previous, uint32_t sample);
int32_t vdc_domain_phase_error_ns( const vdc_tdma_schedule_profile_t *profile, uint64_t expected_window_start_ns, uint32_t path_delay_ns, uint64_t observed_time_ns);
uint32_t vdc_domain_acquisition_window_width_ns( const vdc_tdma_schedule_profile_t *profile);
bool vdc_domain_context_uses_acquisition_window( const vdc_domain_context_t *context);
int32_t vdc_domain_corrected_phase_error_ns( const vdc_domain_context_t *context, const vdc_tdma_timestamp_evidence_t *evidence);
uint32_t vdc_domain_effective_lock_acceptance_threshold_ns( const vdc_domain_context_t *context);
uint32_t vdc_domain_effective_phase_diagnostic_threshold_ns( const vdc_domain_context_t *context);
int32_t vdc_domain_clamp_ppb(int64_t value, uint32_t limit_ppb);
int32_t vdc_domain_slew_i32(int32_t current, int32_t target, uint32_t max_step);
uint64_t vdc_domain_evidence_time_ns( const vdc_tdma_timestamp_evidence_t *evidence);
void VDC_DOMAIN_TIME_CRITICAL(vdc_domain_refresh_quality_state)( vdc_domain_context_t *context);
void vdc_domain_sync_dco_lock_state(vdc_domain_context_t *context);
void vdc_domain_refresh_quality_age(vdc_domain_context_t *context, uint64_t now_ns);
void vdc_domain_init_quality(vdc_domain_context_t *context);
bool vdc_domain_reject_requires_reacquire(uint32_t reject_code);
void vdc_domain_record_rejected_sample( vdc_domain_context_t *context, const vdc_tdma_timestamp_evidence_t *evidence, const vdc_gate_result_t *gate);
bool vdc_domain_debug_gate_recoverable(uint32_t gate_code);
bool vdc_domain_validate_evidence_generations( const vdc_domain_context_t *context, const vdc_tdma_timestamp_evidence_t *evidence, vdc_gate_result_t *gate);
bool vdc_domain_validate_evidence_phase_domain( const vdc_domain_context_t *context, const vdc_tdma_timestamp_evidence_t *evidence, vdc_gate_result_t *gate);
void vdc_domain_record_debug_continue( vdc_domain_context_t *context, const vdc_gate_result_t *gate);
void vdc_domain_reset_lock_acquisition(vdc_domain_context_t *context);
void vdc_domain_record_observation_metadata( vdc_domain_context_t *context, const vdc_tdma_timestamp_evidence_t *evidence);
uint32_t vdc_domain_lock_state_after_reject( const vdc_domain_context_t *context, bool reacquire);
void vdc_domain_record_accepted_sample( vdc_domain_context_t *context, const vdc_tdma_timestamp_evidence_t *evidence, uint64_t local_apply_time_ns);
void vdc_domain_update_clock_from_evidence( vdc_domain_context_t *context, const vdc_tdma_timestamp_evidence_t *evidence, int32_t input_residual_ns, uint64_t local_apply_time_ns);
bool vdc_domain_active_schedule_validate( const vdc_tdma_schedule_profile_t *profile);
void vdc_domain_request_oscillator_discipline( vdc_domain_context_t *context, int32_t rate_source_ppb, uint32_t source_slot_id, uint32_t source_command_seq, uint64_t now_ns);

#endif /* VDC_DOMAIN_INTERNAL_H */

