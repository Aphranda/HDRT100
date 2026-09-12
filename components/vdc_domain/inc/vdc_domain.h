#ifndef VDC_DOMAIN_H
#define VDC_DOMAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_profile.h"
#include "vdc_timestamp.h"

#define VDC_DOMAIN_NODE_COUNT PROJECT_NODE_CAPACITY
#define VDC_DOMAIN_DEFAULT_ACTIVE_NODE_COUNT \
    TDMA_PROFILE_DEFAULT_ACTIVE_NODE_COUNT
#define VDC_DOMAIN_DEFAULT_PERIOD_NS 1000000u
#define VDC_DOMAIN_DEFAULT_OBSERVATION_WIDTH_NS 10000u
#define VDC_DOMAIN_DEFAULT_GUARD_NS 1000u
#define VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS 100u
#define VDC_DOMAIN_DPLL_ADMISSION_TIMESTAMP_RESOLUTION_LIMIT_NS 1000u
#define VDC_DOMAIN_LOCK_TIER_FINE_NS 100u
#define VDC_DOMAIN_LOCK_TIER_DEBUG_NS 1000u
#define VDC_DOMAIN_LOCK_TIER_COARSE_NS 10000u
#define VDC_DOMAIN_DEFAULT_SERVO_PROFILE_CRC32 0x2D0585E8u
#define VDC_DOMAIN_DEFAULT_SERVO_KP_Q16 16384
#define VDC_DOMAIN_DEFAULT_SERVO_KI_Q16 256
#define VDC_DOMAIN_DEFAULT_SANITY_FREQ_LIMIT_PPB 10000u
#define VDC_DOMAIN_TDMA_FRAME_VERSION 1u
#define VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_OFFSET_NS 20000u
#define VDC_DOMAIN_DEFAULT_REFMEM_WINDOW_WIDTH_NS 800000u
#define VDC_DOMAIN_DEFAULT_IDLE_WINDOW_OFFSET_NS 900000u
#define VDC_DOMAIN_DEFAULT_IDLE_WINDOW_WIDTH_NS 50000u
#define VDC_DOMAIN_PATH_DELAY_TABLE_VERSION 1u
#define VDC_DPLL_CONTROL_PROFILE_VERSION 1u
#define VDC_OSCILLATOR_DISCIPLINE_PROFILE_VERSION 1u
#define VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT VDC_DOMAIN_NODE_COUNT
#define VDC_DOMAIN_OBSERVATION_PATH_MATRIX_ENTRY_COUNT \
    (VDC_DOMAIN_NODE_COUNT * VDC_DOMAIN_NODE_COUNT)
#define VDC_DOMAIN_OBSERVATION_PATH_MATRIX_BITMAP_WORD_COUNT \
    ((VDC_DOMAIN_OBSERVATION_PATH_MATRIX_ENTRY_COUNT + 31u) / 32u)

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

/* Every node retains the same PI implementation. The active control role
 * decides whether SyncDpllFB feeds it from local evidence or a selected peer
 * command at the Core1 service boundary. */
typedef enum {
    VDC_DPLL_CONTROL_MODE_MASTER = 0u,
    VDC_DPLL_CONTROL_MODE_FOLLOWER = 1u,
} vdc_dpll_control_mode_t;

typedef enum {
    VDC_OSCILLATOR_DISCIPLINE_FREEZE_NONE = 0u,
    VDC_OSCILLATOR_DISCIPLINE_FREEZE_DISABLED = 1u,
    VDC_OSCILLATOR_DISCIPLINE_FREEZE_UNAVAILABLE = 2u,
    VDC_OSCILLATOR_DISCIPLINE_FREEZE_FAULT = 3u,
    VDC_OSCILLATOR_DISCIPLINE_FREEZE_STALE_SOURCE = 4u,
    VDC_OSCILLATOR_DISCIPLINE_FREEZE_LIMIT = 5u,
} vdc_oscillator_discipline_freeze_reason_t;

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
    VDC_DOMAIN_GATE_DELAY_GENERATION = 15u,
    VDC_DOMAIN_GATE_BIAS_GENERATION = 16u,
    VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED = 17u,
} vdc_domain_gate_code_t;

#define VDC_DOMAIN_QUALITY_FLAG_PHASE_OUT_OF_LOCK (1u << 0u)
#define VDC_DOMAIN_QUALITY_FLAG_RATE_LIMITED      (1u << 1u)
#define VDC_DOMAIN_QUALITY_FLAG_PHASE_LARGE       (1u << 2u)

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
    tdma_ring_profile_t ring_binding;
    /* Zero identifies the base VDC schedule. A nonzero value binds the
     * schedule to one validated TDMA operating profile and makes
     * schedule_crc32 the effective wire-schedule identity. */
    uint32_t operating_profile_crc32;
    uint32_t schedule_crc32;
} vdc_tdma_schedule_profile_t;

typedef struct {
    uint32_t node_count;
    uint32_t local_slot_id;
    uint32_t reference_slot_id;
    uint32_t ring_profile_crc32;
    uint32_t operating_profile_crc32;
    uint32_t cycle_period_ns;
    uint32_t effective_schedule_crc32;
} vdc_tdma_runtime_binding_t;

typedef struct {
    uint32_t enabled;
    uint32_t servo_type;
    int32_t kp_q16;
    int32_t ki_q16;
    uint32_t update_period_us;
    uint32_t first_step_threshold_ns;
    uint32_t step_threshold_ns;
    uint32_t sanity_freq_limit_ppb;
    uint32_t offset_lock_threshold_ns;
    uint32_t debug_lock_threshold_ns;
    uint32_t coarse_lock_threshold_ns;
    uint32_t lock_acceptance_threshold_ns;
    uint32_t lock_sample_count;
    uint32_t phase_diagnostic_threshold_ns;
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

/* This profile is configured by the control plane but applied only by the
 * Core1 DPLL owner. FOLLOWER never falls back to local PI when peer commands
 * are missing or invalid. */
typedef struct {
    uint32_t valid;
    uint32_t version;
    uint32_t mode;
    uint32_t follow_master_slot_id;
    uint32_t generation;
} vdc_dpll_control_profile_t;

/* The caller must have already held this peer command until its absolute
 * effective time. This Domain API applies it at that deterministic service
 * boundary; it does not reinterpret a remote local-tick anchor. */
typedef struct {
    uint32_t valid;
    uint32_t source_slot_id;
    uint32_t control_generation;
    uint32_t command_seq;
    uint32_t schedule_crc32;
    uint64_t effective_vdc_time_ns;
    int32_t period_adjust_ppb;
    int32_t phase_offset_ns;
    uint32_t lock_state;
    uint32_t quality;
} vdc_dpll_follower_command_t;

typedef struct {
    vdc_dpll_control_profile_t profile;
    uint32_t follower_apply_count;
    uint32_t follower_no_command_count;
    uint32_t follower_wrong_source_count;
    uint32_t follower_stale_command_count;
    uint32_t follower_invalid_command_count;
    uint32_t follower_local_evidence_bypass_count;
    uint32_t last_follower_source_slot_id;
    uint32_t last_follower_control_generation;
    uint32_t last_follower_command_seq;
    uint32_t last_follower_quality;
    uint64_t last_follower_effective_vdc_time_ns;
} vdc_dpll_control_status_t;

/* The oscillator actuator is outside VDC. SyncDpllFB may create a bounded
 * request, while a board-specific hardware owner reports availability and
 * applied state. Neither direction changes the signal DCO phase owner. */
typedef struct {
    uint32_t valid;
    uint32_t version;
    uint32_t enabled;
    uint32_t minimum_update_interval_us;
    uint32_t max_abs_trim_ppb;
    uint32_t max_step_ppb;
    int32_t actuator_polarity;
    uint32_t generation;
} vdc_oscillator_discipline_profile_t;

typedef struct {
    uint32_t valid;
    uint32_t available;
    uint32_t healthy;
    uint32_t applied_generation;
    int32_t applied_trim_ppb;
} vdc_oscillator_discipline_actuator_report_t;

typedef struct {
    vdc_oscillator_discipline_profile_t profile;
    uint32_t actuator_available;
    uint32_t actuator_healthy;
    uint32_t request_generation;
    uint32_t applied_generation;
    int32_t requested_trim_ppb;
    int32_t applied_trim_ppb;
    uint64_t last_request_time_ns;
    uint32_t last_source_slot_id;
    uint32_t last_source_command_seq;
    uint32_t freeze_reason;
    uint32_t freeze_count;
    uint32_t unavailable_count;
    uint32_t fault_count;
    uint32_t stale_count;
    uint32_t limit_count;
} vdc_oscillator_discipline_status_t;

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
    /* These generations identify the immutable directed path metadata used
     * to derive delay_ns/phase_error_ns. They are diagnostic provenance and
     * are intentionally not serialized into the legacy short TDMA frame. */
    uint32_t delay_generation;
    uint32_t bias_generation;
    /* TDMA correlation provenance used to replay the same residual formula
     * for MASTER and FOLLOWER observations. */
    uint32_t correlation_flags;
    uint32_t reference_tx_phase_ns;
    uint32_t local_rx_phase_ns;
    uint64_t common_effective_time_ns;
    uint64_t reference_tx_timestamp_ns;
    uint64_t local_rx_timestamp_ns;
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
    uint32_t cycle_period_ns;
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
    uint32_t schedule_crc32;
    uint32_t dpll_update_seq;
    uint32_t accepted;
    /* A debug continuation records a failed admission without feeding that
     * sample into the servo. It is processed successfully by the realtime
     * pipeline, but remains ineligible for any formal lock claim. */
    uint32_t continued;
    /* A follower retains the gate result for diagnostics but the complete
     * local PI/lock/quality pipeline is bypassed. */
    uint32_t follower_bypassed;
    uint32_t servo_applied;
    uint32_t post_servo_dpll_update_seq;
    uint32_t applied;
    uint32_t post_apply_dpll_update_seq;
    int32_t input_residual_ns;
    vdc_gate_result_t gate;
} vdc_tdma_evidence_preparation_t;

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
    uint32_t last_sample_age_us;
    uint32_t freshness_limit_us;
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

#define VDC_PATH_DELAY_FLAG_ACCEPTED (1u << 0u)
#define VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED (1u << 1u)
#define VDC_PATH_DELAY_FLAG_BIAS_VALID (1u << 2u)
#define VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH (1u << 3u)
#define VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY (1u << 4u)
#define VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID (1u << 5u)

/* VDC samples the circulating TDMA process image.  TDMA carries clock
 * markers in the forward direction and the process image in the reverse
 * direction, so installed DPLL paths must identify the latter explicitly. */
#define VDC_PATH_DELAY_DIRECTION_MARKER_FORWARD 0u
#define VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE 1u

typedef struct {
    uint32_t valid;
    uint32_t source_slot_id;
    uint32_t reference_slot_id;
    uint32_t direction;
    uint32_t delay_ns;
    uint32_t jitter_ns;
    uint32_t stddev_ns;
    uint32_t cal_crc32;
    uint32_t freshness_us;
    uint32_t writer;
    uint32_t update_seq;
} vdc_path_delay_entry_t;

/* Calibration loads the complete directed observation path matrix once.  The
 * row-major index is source_node * VDC_DOMAIN_NODE_COUNT + reference_node;
 * runtime DPLL admission only indexes this immutable matrix and never walks
 * the physical ring.  A diagonal entry (source==reference) is the complete
 * loop return path used by the reference Node. */
typedef struct {
    uint32_t valid;
    uint32_t node_count;
    uint32_t entry_count;
    uint32_t valid_bitmap[VDC_DOMAIN_OBSERVATION_PATH_MATRIX_BITMAP_WORD_COUNT];
    uint32_t delay_ns[VDC_DOMAIN_OBSERVATION_PATH_MATRIX_ENTRY_COUNT];
} vdc_observation_path_matrix_t;

typedef struct {
    uint32_t valid;
    uint32_t version;
    uint32_t update_seq;
    uint32_t entry_count;
    uint32_t schedule_crc32;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t bias_generation;
    uint32_t freshness_us;
    uint32_t flags;
    uint32_t table_crc32;
    vdc_path_delay_entry_t entries[VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT];
    vdc_observation_path_matrix_t observation_matrix;
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
    /* The Type-II loop filter's accumulated phase-to-rate correction.  The
     * FLL estimate remains in last_frequency_error_ppb; keeping both makes
     * the integral state observable without conflating the two loops. */
    int32_t loop_filter_integrator_ppb;
    int32_t last_offset_ns;
    uint32_t rms_offset_ns;
    uint32_t max_abs_offset_ns;
    uint32_t jitter_pk_ns;
    uint32_t holdover_age_us;
    uint32_t schedule_crc32;
    uint32_t servo_profile_crc32;
    int32_t last_raw_phase_error_ns;
    uint64_t last_expected_window_start_ns;
    uint64_t last_observed_time_ns;
    /* Identity and transport measurements of the most recent timestamp
     * evidence.  These fields are diagnostic metadata only; they do not
     * participate in the PI/DCO state transition. */
    uint32_t last_observed_source_slot_id;
    uint32_t last_observed_reference_slot_id;
    uint32_t last_observed_payload_class;
    uint32_t last_observed_delay_ns;
    uint32_t last_observed_jitter_ns;
    uint32_t last_observed_frame_crc32;
    uint32_t last_observed_sample_crc32;
    uint32_t last_observed_timestamp_source;
    uint32_t last_observed_timestamp_resolution_ns;
    uint32_t last_observed_timestamp_flags;
    int32_t last_observed_raw_phase_error_ns;
    uint32_t last_observed_delay_generation;
    uint32_t last_observed_bias_generation;
    uint32_t last_observed_correlation_flags;
    uint32_t last_observed_reference_tx_phase_ns;
    uint32_t last_observed_local_rx_phase_ns;
    uint64_t last_observed_common_effective_time_ns;
    uint64_t last_observed_reference_tx_timestamp_ns;
    uint64_t last_observed_local_rx_timestamp_ns;
    /* Debug continuation deliberately separates a recorded bad observation
     * from a product admission rejection. The raw gate remains inspectable
     * while last_reject_code stays PASS for the active debug session. */
    uint32_t debug_continue_enabled;
    uint32_t debug_continue_generation;
    uint32_t debug_continue_count;
    uint32_t last_debug_gate_code;
    uint32_t last_debug_gate_slot;
    uint32_t last_debug_gate_evidence;
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
    vdc_dpll_control_status_t control;
    vdc_oscillator_discipline_status_t oscillator_discipline;
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
    vdc_dpll_control_status_t control;
    vdc_oscillator_discipline_status_t oscillator_discipline;
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
bool vdc_domain_default_schedule_for_topology(
    vdc_tdma_schedule_profile_t *profile,
    uint32_t local_slot_id,
    uint32_t reference_slot_id,
    uint32_t node_count);
bool vdc_domain_set_schedule_ring_topology(vdc_domain_context_t *context,
                                           uint32_t local_slot_id,
                                           uint32_t reference_slot_id,
                                           uint32_t node_count);
bool vdc_domain_build_tdma_runtime_schedule(
    const vdc_tdma_schedule_profile_t *base,
    const vdc_tdma_runtime_binding_t *binding,
    vdc_tdma_schedule_profile_t *runtime_schedule);
/* Re-derive the ring binding for a new local slot and refresh the schedule
 * CRC. Used by the TDMA ring role maintenance command (SYSTem:TDMA:RING:
 * LOCAL) so the same firmware can run as reference or forward node. */
void vdc_domain_set_schedule_local_slot(vdc_domain_context_t *context,
                                        uint32_t local_slot_id);
void vdc_domain_default_servo(vdc_servo_profile_t *profile);
uint32_t vdc_domain_servo_profile_crc32(const vdc_servo_profile_t *profile);
/* Debug tuning intentionally accepts every representable coefficient tuple.
 * The caller owns transport syntax; this operation only rebinds the profile
 * at a VDC service boundary and restarts acquisition. */
bool vdc_domain_apply_debug_servo_profile(
    vdc_domain_context_t *context,
    const vdc_servo_profile_t *profile);
/* Select whether recoverable VDC evidence-gate failures are recorded as
 * debug continuations. Continued evidence never updates the PI/DCO and never
 * confers formal lock; disabling restores strict product admission. */
bool vdc_domain_set_debug_continue(
    vdc_domain_context_t *context,
    bool enabled);
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
void vdc_domain_default_dpll_control_profile(
    vdc_dpll_control_profile_t *profile);
/* Call only from the Core1 DPLL service boundary. Role changes retain the
 * current output but clear local PI/lock acquisition state. */
bool vdc_domain_set_dpll_control_profile(
    vdc_domain_context_t *context,
    const vdc_dpll_control_profile_t *profile);
/* Call only after RefMem/manager identity, sequence and absolute execution
 * time checks have selected this command for the active Core1 boundary. */
bool vdc_domain_apply_follower_command(
    vdc_domain_context_t *context,
    const vdc_dpll_follower_command_t *command);
void vdc_domain_note_follower_command_missing(vdc_domain_context_t *context);
void vdc_domain_default_oscillator_discipline_profile(
    vdc_oscillator_discipline_profile_t *profile);
bool vdc_domain_set_oscillator_discipline_profile(
    vdc_domain_context_t *context,
    const vdc_oscillator_discipline_profile_t *profile);
/* This only records a board-driver report. It never writes the DCO, clock
 * model, lock state or quality table. */
bool vdc_domain_report_oscillator_discipline_actuator(
    vdc_domain_context_t *context,
    const vdc_oscillator_discipline_actuator_report_t *report);
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
/* Provisional P4-LIVE tables are diagnostic inputs derived from the frozen
 * TRN-03 matrix. They may drive the servo, but remain ineligible for formal
 * calibration/LOCKED publication because endpoint bias is not accepted. */
bool vdc_domain_path_delay_table_validate_provisional(
    const vdc_path_delay_table_t *table);
/* Build the complete source/reference observation matrix from the loaded
 * directed link entries. This is a calibration-load operation; runtime
 * consumers must use vdc_domain_observation_path_delay_lookup(). */
bool vdc_domain_load_observation_path_matrix(
    vdc_path_delay_table_t *table,
    uint32_t node_count);
bool vdc_domain_path_delay_lookup(const vdc_path_delay_table_t *table,
                                  uint32_t source_slot_id,
                                  uint32_t reference_slot_id,
                                  vdc_path_delay_entry_t *entry);
bool vdc_domain_observation_path_delay_lookup(
    const vdc_path_delay_table_t *table,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    vdc_path_delay_entry_t *entry);
/* Realtime lookup for the private table already accepted by
 * vdc_domain_activate_tdma_*().  It preserves shape/bitmap bounds but does
 * not recompute the frozen table CRC for every observation. */
bool vdc_domain_active_observation_path_delay_lookup(
    const vdc_path_delay_table_t *table,
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
void vdc_domain_default_timestamp_dictionary(
    vdc_timestamp_dictionary_t *dictionary,
    const vdc_tdma_schedule_profile_t *schedule);
bool vdc_domain_activate_tdma_configuration(
    vdc_domain_context_t *context,
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay);
bool vdc_domain_activate_tdma_provisional_configuration(
    vdc_domain_context_t *context,
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay);
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
/* Realtime submit path for the immutable schedule/dictionary/path set already
 * accepted by vdc_domain_activate_tdma_*(). */
bool vdc_domain_submit_active_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence);
/* The active realtime path is split into deterministic prepare, servo, state
 * and finalize operations. Every mutating step rejects stale work using the
 * frozen schedule and preceding DPLL update identity. */
bool vdc_domain_prepare_active_tdma_evidence(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation);
bool vdc_domain_apply_prepared_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted);
bool vdc_domain_apply_prepared_tdma_evidence_core(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted);
bool vdc_domain_apply_prepared_tdma_evidence_servo(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation);
bool vdc_domain_apply_prepared_tdma_evidence_state(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted);
bool vdc_domain_finalize_prepared_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    const vdc_tdma_evidence_preparation_t *preparation);
bool vdc_domain_submit_compact_observation(
    vdc_domain_context_t *context,
    const vdc_compact_observation_sample_t *compact);
bool vdc_domain_get_snapshot(const vdc_domain_context_t *context,
                             vdc_domain_snapshot_t *snapshot);

#endif
