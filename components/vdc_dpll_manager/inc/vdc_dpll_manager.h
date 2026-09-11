#ifndef VDC_DPLL_MANAGER_H
#define VDC_DPLL_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_service.h"
#include "calibration_path_snapshot.h"
#include "vdc_domain.h"

#define VDC_DPLL_MANAGER_PLAN_NOW_NS UINT64_MAX
#define VDC_DPLL_MANAGER_SYNC_IO_MAX_BATCH_WORDS 32u
#define VDC_DPLL_MANAGER_OBSERVER_QUALITY_TDMA_WINDOW_BASE 0x80000000u
#define VDC_DPLL_MANAGER_SELF_TEST_DEFAULT_PULSES 4096u
#define VDC_DPLL_MANAGER_SELF_TEST_MAX_PULSES UINT32_MAX
/* The DPLL capture is a maintenance/evidence buffer, not a realtime queue. */
/* Keep the expanded master/follower event record inside the existing 8 KiB
 * maintenance capture budget.  This buffer is never part of TDMA traffic. */
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES 76u
/* Schema v3 removes segment-common and reconstructable fields.  A bounded
 * three-buffer queue absorbs asynchronous SD hand-off latency; realtime
 * TDMA/RefMem objects are untouched and queue exhaustion remains observable
 * through dropped_count. */
/* Schema v4 records carry explicit sequence and quality provenance.  Keep
 * three 400-record buffers: the v4 record grew by two provenance words, and
 * this releases 1,344 bytes versus the previous 416-record geometry so both
 * A/B images fit the RP2350 application RAM.  SD segmentation and
 * dropped-count accounting preserve long-run continuity. */
#define VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS 400u

/* Per-record provenance for the external waveform evidence.  These bits are
 * diagnostic facts; only CORRECTED_ELIGIBLE records may enter corrected phase
 * and jitter statistics after the offline window-completeness check. */
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_TIMESTAMP_ELIGIBLE (1u << 0u)
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_SEQUENCE_CONTINUOUS (1u << 1u)
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_NO_SOURCE_DROP (1u << 2u)
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_MATCHED_WINDOW_VALID (1u << 3u)
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_RAW_DIAGNOSTIC_ONLY (1u << 4u)
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_CORRECTED_ELIGIBLE (1u << 5u)
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_GAP_BEFORE (1u << 6u)
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_AMBIGUOUS (1u << 7u)
#define VDC_DPLL_MANAGER_WAVEFORM_QUALITY_INCOMPLETE_WINDOW (1u << 8u)

typedef enum {
    VDC_DPLL_MANAGER_SELF_TEST_ROLE_NONE = 0u,
    VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX = 1u,
    VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX = 2u,
    VDC_DPLL_MANAGER_SELF_TEST_ROLE_TX_RX = 3u,
} vdc_dpll_manager_self_test_role_t;

typedef struct {
    bool ready;
    uint32_t lock_state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t sync_seq;
} vdc_dpll_manager_vdc_status_t;

typedef struct {
    bool ready;
    uint32_t state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t update_seq;
} vdc_dpll_manager_dpll_status_t;

typedef struct {
    uint32_t mode;
    uint32_t follow_master_slot_id;
    uint32_t requested_generation;
    uint32_t applied_generation;
    bool pending;
} vdc_dpll_manager_dpll_role_status_t;

typedef struct {
    bool valid;
    uint32_t service_count;
    uint32_t accepted_update_count;
    uint32_t unchanged_count;
    uint32_t invalid_count;
    uint32_t last_error;
    uint32_t last_service_ms;
    uint32_t last_dco_update_seq;
    uint32_t source_model_seq;
    uint32_t lock_state;
    int32_t phase_offset_ns;
    int32_t period_adjust_ppb;
    uint64_t base_local_tick64;
    uint64_t base_vdc_time64_ns;
    uint32_t nominal_period_ns;
    uint32_t slew_limit_ppb;
    uint32_t tdma_schedule_crc32;
    uint32_t servo_profile_crc32;
} vdc_dpll_manager_dco_consumer_status_t;

typedef struct {
    bool enabled;
    uint32_t max_words_per_service;
    uint32_t rising_event_id;
    uint32_t falling_event_id;
    uint32_t observed_mask;
    uint32_t initial_sample_mask;
    uint32_t next_base_time_l32_ns;
    uint32_t sample_period_ns;
    uint64_t expected_window_start_ns;
    uint32_t frame_crc32;
    uint32_t max_backward_ticks;
    uint32_t quality_flags;
    bool sample0_lsb;
    bool phase_only;
    uint32_t phase_max_span_ns;
    uint32_t phase_min_stable_rounds;
} vdc_dpll_manager_sync_io_observer_config_t;

typedef struct {
    uint32_t role;
    uint32_t output_index;
    uint32_t observed_mask;
    uint32_t initial_sample_mask;
    uint32_t sample_period_ns;
    uint32_t pulse_period_ns;
    uint32_t pulse_high_ns;
    uint32_t pulse_count;
    uint32_t frame_crc32;
    uint32_t start_delay_ns;
    bool phase_only;
    uint32_t phase_max_span_ns;
    uint32_t phase_min_stable_rounds;
} vdc_dpll_manager_observation_self_test_config_t;

typedef struct {
    bool active;
    uint32_t role;
    uint32_t output_index;
    uint32_t observed_mask;
    uint32_t initial_sample_mask;
    uint32_t sample_period_ns;
    uint32_t pulse_period_ns;
    uint32_t pulse_high_ns;
    uint32_t pulse_count;
    uint32_t frame_crc32;
    uint32_t schedule_crc32;
    uint32_t last_error;
    uint32_t started_ms;
    uint32_t start_delay_ns;
    uint64_t first_window_start_ns;
    bool phase_only;
    uint32_t phase_max_span_ns;
    uint32_t phase_min_stable_rounds;
    uint32_t scheduled_pulse_count;
} vdc_dpll_manager_observation_self_test_status_t;

typedef struct {
    bool enabled;
    uint32_t max_words_per_service;
    uint32_t service_count;
    uint32_t raw_word_count;
    uint32_t no_edge_count;
    uint32_t ambiguous_edge_count;
    uint32_t bad_argument_count;
    uint32_t submitted_count;
    uint32_t accepted_count;
    uint32_t rejected_count;
    uint32_t last_capture_result;
    uint32_t last_raw_word;
    uint32_t last_sample_seq;
    uint32_t last_event_id;
    uint32_t last_tick_l32;
    uint32_t last_gate_reject_code;
    uint32_t previous_sample_mask;
    uint32_t next_base_time_l32_ns;
    uint32_t rising_event_id;
    uint32_t falling_event_id;
    uint32_t observed_mask;
    uint32_t initial_sample_mask;
    uint32_t sample_period_ns;
    uint32_t expected_window_start_lo;
    uint32_t expected_window_start_hi;
    uint32_t frame_crc32;
    uint32_t max_backward_ticks;
    uint32_t quality_flags;
    uint32_t sample0_lsb;
    uint32_t schedule_crc32;
    uint32_t dictionary_crc32;
    uint32_t dictionary_entry_count;
    uint32_t dictionary_profile_crc32;
    uint32_t last_edge_index;
    uint32_t last_timestamp_source;
    uint32_t last_timestamp_resolution_ns;
    uint32_t last_timestamp_flags;
    uint32_t last_source_slot_id;
    uint32_t last_reference_slot_id;
    uint32_t last_payload_class;
    uint32_t phase_round_count;
    uint32_t phase_complete_count;
    uint32_t phase_missing_count;
    uint32_t phase_ambiguous_count;
    uint32_t phase_last_edge_mask;
    uint32_t phase_last_span_ns;
    int32_t phase_last_offset_ns[4];
    uint32_t phase_initial_span_ns;
    int32_t phase_initial_offset_ns[4];
    uint32_t phase_peak_span_ns;
    uint32_t phase_min_span_ns;
    uint32_t phase_stable_round_count;
    uint32_t phase_stable_streak;
    uint32_t phase_max_stable_streak;
    uint32_t phase_stable_jitter_ns;
    uint32_t phase_first_stable_round;
    uint32_t phase_converged;
    uint32_t phase_max_span_ns;
    uint32_t phase_min_stable_rounds;
    uint32_t phase_last_window_start_lo;
    uint32_t phase_last_window_start_hi;
    uint32_t phase_dropped_word_count;
    uint32_t phase_gap_count;
} vdc_dpll_manager_sync_io_observer_status_t;

typedef enum {
    VDC_DPLL_MANAGER_RING_OBSERVER_NONE = 0u,
    VDC_DPLL_MANAGER_RING_OBSERVER_SNAPSHOT_UNAVAILABLE = 1u,
    VDC_DPLL_MANAGER_RING_OBSERVER_INACTIVE = 2u,
    VDC_DPLL_MANAGER_RING_OBSERVER_DUPLICATE = 3u,
    VDC_DPLL_MANAGER_RING_OBSERVER_IDENTITY_REJECTED = 4u,
    VDC_DPLL_MANAGER_RING_OBSERVER_PATH_REJECTED = 5u,
    VDC_DPLL_MANAGER_RING_OBSERVER_EXPAND_REJECTED = 6u,
    VDC_DPLL_MANAGER_RING_OBSERVER_SUBMIT_ACCEPTED = 7u,
    VDC_DPLL_MANAGER_RING_OBSERVER_SUBMIT_REJECTED = 8u,
    VDC_DPLL_MANAGER_RING_OBSERVER_EVIDENCE_PENDING = 9u,
    VDC_DPLL_MANAGER_RING_OBSERVER_DEBUG_CONTINUED = 10u,
} vdc_dpll_manager_ring_observer_result_t;

typedef struct {
    uint32_t service_count;
    uint32_t snapshot_count;
    uint32_t eligible_count;
    uint32_t path_count;
    uint32_t expand_count;
    uint32_t submitted_count;
    uint32_t accepted_count;
    uint32_t rejected_count;
    uint32_t continued_count;
    uint32_t last_sequence;
    uint32_t last_config_seq;
    uint32_t last_result;
} vdc_dpll_manager_ring_observer_status_t;

typedef struct __attribute__((packed)) {
    uint32_t update_seq;
    uint32_t timestamp_ms;
    /* MASTER: local residuals. FOLLOWER: applied peer DCO command. */
    int32_t phase_value_ns;
    int32_t frequency_value_ppb;
    uint32_t state_and_reject;
    /* kind[7:0], source_slot[15:8], lock_state[23:16], quality[31:24]. */
    uint32_t source_kind_lock_quality;
    uint32_t control_generation;
    uint32_t command_seq;
    uint32_t effective_vdc_time_lo;
    uint32_t effective_vdc_time_hi;
    /* Observation metadata is populated for MASTER and FOLLOWER evidence;
     * command records leave these fields zero. */
    int32_t raw_phase_value_ns;
    uint32_t observation_source_reference;
    uint32_t observation_delay_ns;
    uint32_t observation_jitter_ns;
    uint32_t observation_delay_generation;
    uint32_t observation_bias_generation;
    uint32_t observation_correlation_flags;
    uint32_t observation_reference_tx_phase_ns;
    uint32_t observation_local_rx_phase_ns;
    uint64_t observation_common_effective_time_ns;
    uint64_t observation_expected_window_start_ns;
    uint64_t observation_observed_time_ns;
} vdc_dpll_manager_dpll_capture_record_t;

typedef struct {
    bool armed;
    bool complete;
    uint32_t sample_count;
    uint32_t dropped_count;
    uint32_t first_update_seq;
    uint32_t last_update_seq;
    uint32_t start_ms;
    uint32_t end_ms;
} vdc_dpll_manager_dpll_capture_status_t;

typedef struct __attribute__((packed)) {
    uint32_t raw_word;
    uint32_t sample_seq;
    uint32_t previous_sample_mask;
    uint32_t base_time_l32_ns;
    uint64_t matched_window_start_ns;
    uint32_t sample_period_ns;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t dropped_before;
    uint32_t quality_flags;
} vdc_dpll_manager_waveform_record_t;

typedef struct {
    bool armed;
    bool stopping;
    bool complete;
    uint32_t session_id;
    uint32_t record_count;
    uint32_t dropped_count;
    uint32_t source_dropped_count;
    uint32_t segment_count;
    uint32_t pending_record_count;
    uint32_t first_sample_seq;
    uint32_t last_sample_seq;
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t last_error;
    uint32_t last_job_id;
    char last_path[96];
} vdc_dpll_manager_waveform_capture_status_t;

typedef struct {
    vdc_servo_profile_t profile;
    uint32_t requested_generation;
    uint32_t applied_generation;
    bool pending;
} vdc_dpll_manager_debug_servo_tune_status_t;

typedef struct {
    bool enabled;
    uint32_t requested_generation;
    uint32_t applied_generation;
    uint32_t continued_count;
    uint32_t last_gate_code;
    uint32_t last_gate_slot;
    uint32_t last_gate_evidence;
    bool pending;
} vdc_dpll_manager_debug_admission_status_t;

/* RefMem Core1 only needs the schedule identity and the command fields it
 * republishes for a MASTER.  Keeping this publication separate from the
 * full Domain snapshot prevents the RefMem realtime beat from copying path
 * delay and diagnostic tables on every invocation. */
typedef struct {
    vdc_tdma_schedule_profile_t schedule;
    uint32_t clock_epoch_id;
    uint32_t clock_run_id;
    int32_t dco_period_adjust_ppb;
    int32_t dco_phase_offset_ns;
    uint32_t dpll_update_seq;
    uint32_t dpll_state;
    vdc_dpll_control_profile_t control_profile;
    uint32_t quality_health_state;
} vdc_dpll_manager_refmem_snapshot_t;

bool vdc_dpll_manager_init(void);
void vdc_dpll_manager_set_vdc_ready(bool ready);
void vdc_dpll_manager_set_dpll_ready(bool ready);
void vdc_sync_ao_service(void);
void vdc_dpll_manager_core0_service(void);
void vdc_dpll_manager_sync_io_capture_service_core1(void);
void sync_dpll_fb_service(void);
void tdma_component_core1_service(void);
void vdc_dpll_manager_vdc_service(void);
void vdc_dpll_manager_dpll_service(void);
void vdc_dpll_manager_tdma_core1_service(void);
void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *status);
void vdc_dpll_manager_get_dpll_status(vdc_dpll_manager_dpll_status_t *status);
void vdc_dpll_manager_get_dco_consumer_status(
    vdc_dpll_manager_dco_consumer_status_t *status);
/* Debug-only coefficients are accepted without range rejection.  Core0 only
 * stages the request; Core1 applies it at the next DPLL service boundary. */
bool vdc_dpll_manager_request_debug_servo_tune(
    int32_t kp_q16,
    int32_t ki_q16,
    uint32_t update_period_us,
    uint32_t step_threshold_ns,
    uint32_t sanity_freq_limit_ppb,
    uint32_t *generation);
bool vdc_dpll_manager_request_default_debug_servo_tune(uint32_t *generation);
/* Persist the current staged profile. Flash is only written from this
 * explicit command; runtime TUNE remains a volatile mailbox update. */
bool vdc_dpll_manager_store_debug_servo_profile(void);
void vdc_dpll_manager_get_debug_servo_tune_status(
    vdc_dpll_manager_debug_servo_tune_status_t *status);
/* Role changes follow the same Core0 mailbox/Core1 owner boundary as PI
 * tuning.  STORE persists the requested profile; runtime ROLE is volatile. */
bool vdc_dpll_manager_request_dpll_role(uint32_t mode,
                                        uint32_t follow_master_slot_id,
                                        uint32_t *generation);
bool vdc_dpll_manager_store_dpll_role(void);
void vdc_dpll_manager_get_dpll_role_status(
    vdc_dpll_manager_dpll_role_status_t *status);
/* Core0 stages a single debug-admission intent. Core1 applies it before the
 * next evidence pipeline beat; pending intents are never overwritten. */
bool vdc_dpll_manager_request_debug_continue(bool enabled,
                                             uint32_t *generation);
void vdc_dpll_manager_get_debug_admission_status(
    vdc_dpll_manager_debug_admission_status_t *status);
bool vdc_dpll_manager_configure_sync_io_observer(
    const vdc_dpll_manager_sync_io_observer_config_t *config);
bool vdc_dpll_manager_configure_sync_io_observer_tdma(
    bool enabled,
    uint32_t initial_sample_mask,
    uint32_t sample_period_ns,
    uint32_t frame_crc32);
bool vdc_dpll_manager_start_observation_self_test(
    const vdc_dpll_manager_observation_self_test_config_t *config);
void vdc_dpll_manager_get_observation_self_test_status(
    vdc_dpll_manager_observation_self_test_status_t *status);
void vdc_dpll_manager_get_sync_io_observer_status(
    vdc_dpll_manager_sync_io_observer_status_t *status);
void vdc_dpll_manager_get_ring_observer_status(
    vdc_dpll_manager_ring_observer_status_t *status);
bool vdc_dpll_manager_dpll_capture_arm(void);
bool vdc_dpll_manager_dpll_capture_stop(void);
void vdc_dpll_manager_get_dpll_capture_status(
    vdc_dpll_manager_dpll_capture_status_t *status);
bool vdc_dpll_manager_dpll_capture_save(uint32_t *job_id,
                                        char *path,
                                        size_t path_size);
bool vdc_dpll_manager_waveform_capture_arm(void);
bool vdc_dpll_manager_waveform_capture_stop(void);
void vdc_dpll_manager_get_waveform_capture_status(
    vdc_dpll_manager_waveform_capture_status_t *status);
bool vdc_dpll_manager_waveform_capture_manifest(char *path_prefix,
                                                size_t path_prefix_size,
                                                uint32_t *segment_count);
bool vdc_dpll_manager_get_snapshot(vdc_domain_snapshot_t *snapshot);
/* Lock-free, seqlock-consistent publication for the RefMem Core1 path. */
bool vdc_dpll_manager_get_refmem_snapshot(
    vdc_dpll_manager_refmem_snapshot_t *snapshot);
uint32_t vdc_dpll_manager_published_update_seq(void);
bool vdc_dpll_manager_get_tdma_snapshot(tdma_service_snapshot_t *snapshot);
bool vdc_dpll_manager_plan_tdma_window(uint32_t window_class,
                                       uint64_t now_ns,
                                       vdc_tdma_window_plan_t *plan,
                                       vdc_gate_result_t *gate);
/* Plan from one already validated, seqlock-consistent VDC publication.  Core1
 * callers use this instead of taking the manager's control-plane lock again. */
bool vdc_dpll_manager_plan_published_tdma_window(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot,
    uint32_t window_class,
    uint64_t now_ns,
    vdc_tdma_window_plan_t *plan,
    vdc_gate_result_t *gate);
bool vdc_dpll_manager_plan_tdma_ring(vdc_tdma_ring_plan_t *plan);
bool vdc_dpll_manager_set_tdma_ring_local_slot(uint32_t local_slot_id);
bool vdc_dpll_manager_set_tdma_ring_topology(uint32_t local_slot_id,
                                             uint32_t reference_slot_id,
                                             uint32_t node_count);
bool vdc_dpll_manager_publish_timestamp_dictionary(
    const vdc_timestamp_dictionary_t *dictionary,
    uint32_t initial_tick_l32);
bool vdc_dpll_manager_publish_calibration_path_delay(
    const vdc_path_delay_table_t *table);
bool vdc_dpll_manager_publish_calibration_path_snapshot(
    const calibration_path_snapshot_t *snapshot);
/* Atomically binds the staged TDMA runtime schedule, a derived timestamp
 * dictionary and one active Calibration path matrix. */
bool vdc_dpll_manager_activate_tdma_calibration(
    const calibration_path_snapshot_t *snapshot);
/* Explicit P4-LIVE development path. The frozen TRN-03 link-base matrix is
 * converted to a DIAGNOSTIC_ONLY observation matrix while TDMA is STOPPED.
 * It never creates or replaces a CalibrationManager active snapshot. */
bool vdc_dpll_manager_activate_tdma_provisional_training(void);
bool vdc_dpll_manager_submit_compact_observation(
    const vdc_compact_observation_sample_t *compact);

#endif
