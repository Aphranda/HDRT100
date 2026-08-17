#ifndef VDC_DOMAIN_H
#define VDC_DOMAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "vdc_timestamp.h"

#define VDC_DOMAIN_NODE_COUNT 8u
#define VDC_DOMAIN_DEFAULT_PERIOD_NS 1000000u
#define VDC_DOMAIN_DEFAULT_OBSERVATION_WIDTH_NS 10000u
#define VDC_DOMAIN_DEFAULT_GUARD_NS 1000u
#define VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS 100u
#define VDC_DOMAIN_DPLL_ADMISSION_TIMESTAMP_RESOLUTION_LIMIT_NS 1000u
#define VDC_DOMAIN_LOCK_TIER_FINE_NS 100u
#define VDC_DOMAIN_LOCK_TIER_DEBUG_NS 1000u
#define VDC_DOMAIN_LOCK_TIER_COARSE_NS 10000u
#define VDC_DOMAIN_DEFAULT_SERVO_PROFILE_CRC32 0x56444301u
#define VDC_DOMAIN_TDMA_FRAME_VERSION 1u
#define VDC_DOMAIN_TDMA_RING_PROFILE_VERSION 1u
#define VDC_DOMAIN_TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN 0x00000001u
#define VDC_DOMAIN_TDMA_RING_UP_GROUP_ID 1u
#define VDC_DOMAIN_TDMA_RING_DOWN_GROUP_ID 2u
#define VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_OFFSET_NS 20000u
#define VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_WIDTH_NS 800000u
#define VDC_DOMAIN_DEFAULT_IDLE_WINDOW_OFFSET_NS 900000u
#define VDC_DOMAIN_DEFAULT_IDLE_WINDOW_WIDTH_NS 50000u
#define VDC_DOMAIN_PATH_DELAY_TABLE_VERSION 1u
#define VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT VDC_DOMAIN_NODE_COUNT

#define VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY VDC_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY
#define VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE   VDC_TIMESTAMP_FLAG_DPLL_ELIGIBLE

typedef enum {
    VDC_DOMAIN_LOCK_OFF = 0u,
    VDC_DOMAIN_LOCK_CHECKING = 1u,
    VDC_DOMAIN_LOCK_INITIAL_SYNC = 2u,
    VDC_DOMAIN_LOCK_FREQ_LOCK = 3u,
    VDC_DOMAIN_LOCK_PHASE_LOCK = 4u,
    VDC_DOMAIN_LOCK_LOCKED = 5u,
    VDC_DOMAIN_LOCK_HOLDOVER = 6u,
    VDC_DOMAIN_LOCK_RELOCKING = 7u,
    VDC_DOMAIN_LOCK_FAULT = 8u,
} vdc_domain_lock_state_t;

typedef enum {
    VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE = 1u,
    VDC_DOMAIN_PAYLOAD_REFMEM_DELTA = 2u,
    VDC_DOMAIN_PAYLOAD_ACK_NACK_FENCE_QUALITY = 3u,
    VDC_DOMAIN_PAYLOAD_IDLE_BEACON = 4u,
} vdc_domain_payload_class_t;

typedef enum {
    VDC_DOMAIN_WINDOW_VDC_OBSERVATION = 1u,
    VDC_DOMAIN_WINDOW_REFMEM_DATA = 2u,
    VDC_DOMAIN_WINDOW_IDLE_BEACON = 3u,
} vdc_domain_tdma_window_class_t;

typedef enum {
    VDC_DOMAIN_TIMESTAMP_SOURCE_NONE = VDC_TIMESTAMP_SOURCE_NONE,
    VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US = VDC_TIMESTAMP_SOURCE_SOFTWARE_US,
    VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK = VDC_TIMESTAMP_SOURCE_HARDWARE_TICK,
} vdc_domain_timestamp_source_t;

typedef enum {
    VDC_DOMAIN_HEALTH_UNKNOWN = 0u,
    VDC_DOMAIN_HEALTH_CHECKING = 1u,
    VDC_DOMAIN_HEALTH_DEGRADED = 2u,
    VDC_DOMAIN_HEALTH_LOCK_CANDIDATE = 3u,
    VDC_DOMAIN_HEALTH_HEALTHY = 4u,
    VDC_DOMAIN_HEALTH_FAULT = 5u,
} vdc_domain_health_state_t;

typedef enum {
    VDC_DOMAIN_LOCK_QUALITY_NONE = 0u,
    VDC_DOMAIN_LOCK_QUALITY_COARSE_10US = 1u,
    VDC_DOMAIN_LOCK_QUALITY_DEBUG_1US = 2u,
    VDC_DOMAIN_LOCK_QUALITY_FINE_100NS = 3u,
} vdc_domain_lock_quality_t;

typedef enum {
    VDC_DOMAIN_GATE_PASS = 0u,
    VDC_DOMAIN_GATE_DISABLED = 1u,
    VDC_DOMAIN_GATE_BAD_ARGUMENT = 2u,
    VDC_DOMAIN_GATE_BAD_SCHEDULE = 3u,
    VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH = 4u,
    VDC_DOMAIN_GATE_EPOCH_MISMATCH = 5u,
    VDC_DOMAIN_GATE_REFERENCE_MISMATCH = 6u,
    VDC_DOMAIN_GATE_SOURCE_OUT_OF_RANGE = 7u,
    VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE = 8u,
    VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE = 9u,
    VDC_DOMAIN_GATE_TIMESTAMP_RESOLUTION = 10u,
    VDC_DOMAIN_GATE_WINDOW_BOUND = 11u,
    VDC_DOMAIN_GATE_BAD_FRAME = 12u,
    VDC_DOMAIN_GATE_BAD_WINDOW_CLASS = 13u,
    VDC_DOMAIN_GATE_PAYLOAD_WINDOW_FORBIDDEN = 14u,
    VDC_DOMAIN_GATE_SERVO_OUTLIER = 15u,
} vdc_domain_gate_code_t;

typedef struct {
    uint32_t enabled;
    uint32_t schedule_version;
    uint32_t schedule_epoch;
    uint32_t period_ns;
    uint32_t observation_window_offset_ns;
    uint32_t observation_window_width_ns;
    uint32_t refmem_data_window_offset_ns;
    uint32_t refmem_data_window_width_ns;
    uint32_t idle_beacon_window_offset_ns;
    uint32_t idle_beacon_window_width_ns;
    uint32_t guard_before_ns;
    uint32_t guard_after_ns;
    uint32_t reference_slot_id;
    uint32_t local_slot_id;
    uint32_t ring_profile_version;
    uint32_t ring_flags;
    uint32_t ring_node_count;
    uint32_t ring_local_index;
    uint32_t ring_reference_index;
    uint32_t up_leg_group_id;
    uint32_t down_leg_group_id;
    uint32_t upstream_slot_id;
    uint32_t downstream_slot_id;
    uint32_t feedback_slot_id;
    uint32_t ring_profile_crc32;
    uint32_t schedule_crc32;
} vdc_tdma_schedule_profile_t;

typedef struct {
    uint32_t enabled;
    uint32_t servo_type;
    int32_t kp_q16;
    int32_t ki_q16;
    uint32_t update_period_1e3ns;
    uint32_t first_step_threshold_ns;
    uint32_t step_threshold_ns;
    uint32_t sanity_freq_limit_ppb;
    uint32_t offset_lock_threshold_ns;
    uint32_t debug_lock_threshold_ns;
    uint32_t coarse_lock_threshold_ns;
    uint32_t lock_acceptance_threshold_ns;
    uint32_t lock_sample_count;
    uint32_t outlier_threshold_ns;
    uint32_t reset_policy;
    uint32_t servo_profile_crc32;
} vdc_servo_profile_t;

typedef struct {
    uint32_t valid;
    uint32_t model_seq;
    uint32_t epoch_id;
    uint32_t run_id;
    uint64_t base_local_tick64;
    uint64_t base_vdc_time64_ns;
    uint32_t nominal_period_ns;
    int32_t period_adjust_ppb;
    int32_t phase_offset_ns;
    uint32_t slew_limit_ppb;
    uint32_t tdma_schedule_crc32;
    uint32_t servo_profile_crc32;
} vdc_clock_model_t;

typedef struct {
    uint32_t valid;
    uint32_t dco_update_seq;
    uint32_t source_model_seq;
    uint32_t epoch_id;
    uint32_t run_id;
    uint64_t base_local_tick64;
    uint64_t base_vdc_time64_ns;
    uint32_t nominal_period_ns;
    int32_t period_adjust_ppb;
    int32_t phase_offset_ns;
    uint32_t slew_limit_ppb;
    uint32_t lock_state;
    uint32_t tdma_schedule_crc32;
    uint32_t servo_profile_crc32;
} vdc_dco_control_t;

typedef struct {
    uint32_t sample_seq;
    uint32_t schedule_epoch;
    uint32_t slot_index;
    uint32_t source_slot_id;
    uint32_t reference_slot_id;
    uint32_t payload_class;
    uint64_t expected_window_start_ns;
    uint64_t arm_time_ns;
    uint64_t start_time_ns;
    uint64_t observed_time_ns;
    uint64_t done_time_ns;
    uint64_t apply_time_ns;
    uint32_t late_ns;
    uint32_t jitter_ns;
    uint32_t delay_ns;
    int32_t phase_error_ns;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t schedule_crc32;
    uint32_t frame_crc32;
    uint32_t sample_crc32;
    uint32_t quality_flags;
} vdc_tdma_timestamp_evidence_t;

typedef struct {
    uint32_t valid;
    uint32_t sample_seq;
    uint32_t event_id;
    uint32_t tick_l32;
    uint32_t max_backward_ticks;
    uint64_t expected_window_start_ns;
    uint32_t frame_crc32;
    uint32_t sample_crc32;
    uint32_t jitter_ns;
    uint32_t delay_ns;
    uint32_t quality_flags;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
} vdc_compact_observation_sample_t;

typedef struct {
    uint32_t frame_version;
    uint32_t frame_seq;
    uint32_t schedule_epoch;
    uint32_t slot_index;
    uint32_t source_slot_id;
    uint32_t reference_slot_id;
    uint32_t window_class;
    uint32_t payload_class;
    uint64_t window_start_ns;
    uint32_t schedule_crc32;
    uint32_t frame_crc32;
    uint32_t payload_crc32;
    uint32_t quality_flags;
    uint32_t reference_sync_valid;
    uint32_t reference_seq_id;
    uint32_t reference_frame_id;
    uint32_t reference_sync_slot_id;
    uint64_t reference_time_ns;
    uint64_t next_frame_start_ns;
    uint32_t reference_schedule_crc32;
    uint32_t reference_flags;
    vdc_tdma_timestamp_evidence_t timestamp;
} vdc_tdma_frame_envelope_t;

typedef struct {
    uint32_t valid;
    uint32_t window_class;
    uint32_t schedule_epoch;
    uint32_t slot_index;
    uint32_t source_slot_id;
    uint32_t reference_slot_id;
    uint64_t now_ns;
    uint64_t window_start_ns;
    uint64_t window_end_ns;
    uint64_t guard_start_ns;
    uint64_t guard_end_ns;
    uint32_t wait_ns;
    uint32_t late_ns;
    uint32_t in_guarded_window;
    uint32_t inside_payload_window;
    uint32_t missed_current_window;
    uint32_t schedule_crc32;
} vdc_tdma_window_plan_t;

typedef struct {
    uint32_t valid;
    uint32_t ring_node_count;
    uint32_t local_slot_id;
    uint32_t reference_slot_id;
    uint32_t upstream_slot_id;
    uint32_t downstream_slot_id;
    uint32_t feedback_slot_id;
    uint32_t from_reference_hops;
    uint32_t to_feedback_hops;
    uint32_t is_reference_slot;
    uint32_t ring_flags;
    uint32_t ring_profile_crc32;
    uint32_t schedule_crc32;
} vdc_tdma_ring_plan_t;

typedef struct {
    uint32_t passed;
    uint32_t reject_code;
    uint32_t reject_slot;
    uint32_t reject_evidence;
    uint32_t last_pass_seq;
} vdc_gate_result_t;

typedef struct {
    uint32_t valid;
    uint32_t update_seq;
    uint32_t health_state;
    uint32_t lock_state;
    uint32_t lock_quality_tier;
    uint32_t quality_flags;
    uint32_t accepted_sample_count;
    uint32_t rejected_sample_count;
    uint32_t consecutive_good_samples;
    uint32_t consecutive_bad_samples;
    uint32_t consecutive_coarse_samples;
    uint32_t consecutive_debug_samples;
    uint32_t consecutive_fine_samples;
    uint32_t last_sample_seq;
    uint32_t last_reject_code;
    uint32_t last_timestamp_source;
    uint32_t last_timestamp_resolution_ns;
    uint32_t last_timestamp_flags;
    uint64_t last_sample_time_ns;
    uint32_t last_sample_age_1e3ns;
    uint32_t freshness_limit_1e3ns;
    uint32_t lock_acceptance_threshold_ns;
    uint32_t fine_lock_threshold_ns;
    uint32_t debug_lock_threshold_ns;
    uint32_t coarse_lock_threshold_ns;
    int32_t last_offset_ns;
    uint32_t rms_offset_ns;
    uint32_t max_abs_offset_ns;
    uint32_t last_jitter_ns;
    uint32_t jitter_rms_ns;
    uint32_t jitter_pk_ns;
    uint32_t gate_reject_code;
    uint32_t gate_reject_slot;
    uint32_t gate_reject_evidence;
} vdc_quality_table_t;

typedef struct {
    uint32_t valid;
    uint32_t update_seq;
    int32_t last_offset_ns;
    uint32_t rms_offset_ns;
    uint32_t max_abs_offset_ns;
    int32_t freq_offset_ppb;
    uint32_t freq_skew_ppb;
    uint32_t path_delay_ns;
    uint32_t delay_stddev_ns;
    uint32_t dispersion_ns;
    uint32_t root_distance_ns;
    uint32_t holdover_drift_bound_ns_s;
} vdc_error_budget_t;

typedef struct {
    uint32_t valid;
    uint32_t source_slot_id;
    uint32_t reference_slot_id;
    uint32_t direction;
    uint32_t delay_ns;
    uint32_t jitter_ns;
    uint32_t stddev_ns;
    uint32_t cal_crc32;
    uint32_t freshness_1e3ns;
    uint32_t writer;
    uint32_t update_seq;
} vdc_path_delay_entry_t;

typedef struct {
    uint32_t valid;
    uint32_t version;
    uint32_t update_seq;
    uint32_t entry_count;
    uint32_t schedule_crc32;
    uint32_t table_crc32;
    vdc_path_delay_entry_t entries[VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT];
} vdc_path_delay_table_t;

typedef struct {
    uint32_t state;
    uint32_t update_seq;
    uint32_t accepted_sample_count;
    uint32_t rejected_sample_count;
    uint32_t last_reject_code;
    uint32_t last_sample_seq;
    int32_t last_phase_error_ns;
    int32_t last_frequency_error_ppb;
    int32_t last_offset_ns;
    uint32_t rms_offset_ns;
    uint32_t max_abs_offset_ns;
    uint32_t jitter_pk_ns;
    uint32_t holdover_age_1e3ns;
    uint32_t schedule_crc32;
    uint32_t servo_profile_crc32;
    uint64_t last_expected_window_start_ns;
    uint64_t last_observed_time_ns;
} vdc_dpll_state_t;

typedef struct {
    uint32_t ready;
    uint32_t service_count;
    uint64_t first_service_time_ns;
    uint64_t last_service_time_ns;
    vdc_tdma_schedule_profile_t schedule;
    vdc_servo_profile_t servo;
    vdc_clock_model_t clock;
    vdc_dco_control_t dco;
    vdc_dpll_state_t dpll;
    vdc_quality_table_t quality;
    vdc_error_budget_t error_budget;
    vdc_path_delay_table_t path_delay;
    vdc_gate_result_t gate;
} vdc_domain_snapshot_t;

typedef struct {
    uint32_t ready;
    uint32_t service_count;
    uint64_t first_service_time_ns;
    uint64_t last_service_time_ns;
    vdc_tdma_schedule_profile_t schedule;
    vdc_servo_profile_t servo;
    vdc_clock_model_t clock;
    vdc_dco_control_t dco;
    vdc_dpll_state_t dpll;
    vdc_quality_table_t quality;
    vdc_error_budget_t error_budget;
    vdc_path_delay_table_t path_delay;
    vdc_gate_result_t gate;
    vdc_timestamp_dictionary_t timestamp_dictionary;
    vdc_wrap_tracker_t wrap_tracker;
} vdc_domain_context_t;

void vdc_domain_default_schedule(vdc_tdma_schedule_profile_t *profile,
                                 uint32_t local_slot_id,
                                 uint32_t reference_slot_id);
void vdc_domain_default_servo(vdc_servo_profile_t *profile);
uint32_t vdc_domain_ring_profile_crc32(const vdc_tdma_schedule_profile_t *profile);
uint32_t vdc_domain_schedule_crc32(const vdc_tdma_schedule_profile_t *profile);
bool vdc_domain_schedule_validate(const vdc_tdma_schedule_profile_t *profile);
void vdc_domain_default_clock_model(vdc_clock_model_t *model,
                                    uint32_t epoch_id,
                                    uint32_t run_id,
                                    uint64_t base_local_tick64,
                                    uint64_t base_vdc_time64_ns,
                                    uint32_t schedule_crc32);
void vdc_domain_default_dco_control(vdc_dco_control_t *dco,
                                    const vdc_clock_model_t *model,
                                    uint32_t lock_state);
bool vdc_domain_clock_model_local_to_vdc_ns(const vdc_clock_model_t *model,
                                            uint64_t local_tick64,
                                            uint64_t *vdc_time64_ns);
bool vdc_domain_dco_control_validate(const vdc_tdma_schedule_profile_t *schedule,
                                     const vdc_servo_profile_t *servo,
                                     const vdc_dco_control_t *dco);
void vdc_domain_default_path_delay_table(
    vdc_path_delay_table_t *table,
    const vdc_tdma_schedule_profile_t *schedule);
uint32_t vdc_domain_path_delay_table_crc32(
    const vdc_path_delay_table_t *table);
bool vdc_domain_path_delay_table_validate(
    const vdc_path_delay_table_t *table);
bool vdc_domain_path_delay_lookup(const vdc_path_delay_table_t *table,
                                  uint32_t source_slot_id,
                                  uint32_t reference_slot_id,
                                  vdc_path_delay_entry_t *entry);
bool vdc_domain_validate_tdma_timestamp_evidence(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool require_dpll_eligible,
    vdc_gate_result_t *gate);
bool vdc_domain_validate_tdma_frame_envelope(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_frame_envelope_t *frame,
    bool require_dpll_eligible,
    vdc_gate_result_t *gate);
bool vdc_domain_expand_compact_observation(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_timestamp_dictionary_t *dictionary,
    vdc_wrap_tracker_t *wrap_tracker,
    const vdc_compact_observation_sample_t *compact,
    vdc_tdma_timestamp_evidence_t *evidence,
    vdc_gate_result_t *gate);
bool vdc_domain_plan_tdma_window(const vdc_tdma_schedule_profile_t *profile,
                                 uint32_t window_class,
                                 uint64_t now_ns,
                                 vdc_tdma_window_plan_t *plan,
                                 vdc_gate_result_t *gate);
bool vdc_domain_plan_tdma_ring(const vdc_tdma_schedule_profile_t *profile,
                               vdc_tdma_ring_plan_t *plan);
bool vdc_domain_init(vdc_domain_context_t *context);
void vdc_domain_set_ready(vdc_domain_context_t *context, bool ready);
void vdc_domain_service(vdc_domain_context_t *context, uint64_t now_ns);
bool vdc_domain_publish_clock_model(vdc_domain_context_t *context,
                                    const vdc_clock_model_t *model);
bool vdc_domain_publish_dco_control(vdc_domain_context_t *context,
                                    const vdc_dco_control_t *dco);
bool vdc_domain_publish_timestamp_dictionary(
    vdc_domain_context_t *context,
    const vdc_timestamp_dictionary_t *dictionary,
    uint32_t initial_tick_l32);
bool vdc_domain_publish_path_delay_table(
    vdc_domain_context_t *context,
    const vdc_path_delay_table_t *table);
bool vdc_domain_submit_tdma_evidence(vdc_domain_context_t *context,
                                     const vdc_tdma_timestamp_evidence_t *evidence);
bool vdc_domain_submit_compact_observation(
    vdc_domain_context_t *context,
    const vdc_compact_observation_sample_t *compact);
bool vdc_domain_get_snapshot(const vdc_domain_context_t *context,
                             vdc_domain_snapshot_t *snapshot);

#endif
