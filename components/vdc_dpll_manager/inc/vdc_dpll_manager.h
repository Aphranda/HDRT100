#ifndef VDC_DPLL_MANAGER_H
#define VDC_DPLL_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_service.h"
#include "vdc_domain.h"

#define VDC_DPLL_MANAGER_PLAN_NOW_NS UINT64_MAX
#define VDC_DPLL_MANAGER_SYNC_IO_MAX_BATCH_WORDS 32u
#define VDC_DPLL_MANAGER_OBSERVER_QUALITY_TDMA_WINDOW_BASE 0x80000000u
#define VDC_DPLL_MANAGER_SELF_TEST_MAX_PULSES 4096u

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
} vdc_dpll_manager_sync_io_observer_status_t;

bool vdc_dpll_manager_init(void);
void vdc_dpll_manager_set_vdc_ready(bool ready);
void vdc_dpll_manager_set_dpll_ready(bool ready);
void vdc_sync_ao_service(void);
void sync_dpll_fb_service(void);
void tdma_component_core1_service(void);
void vdc_dpll_manager_vdc_service(void);
void vdc_dpll_manager_dpll_service(void);
void vdc_dpll_manager_tdma_core1_service(void);
void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *status);
void vdc_dpll_manager_get_dpll_status(vdc_dpll_manager_dpll_status_t *status);
void vdc_dpll_manager_get_dco_consumer_status(
    vdc_dpll_manager_dco_consumer_status_t *status);
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
bool vdc_dpll_manager_get_snapshot(vdc_domain_snapshot_t *snapshot);
bool vdc_dpll_manager_get_tdma_snapshot(tdma_service_snapshot_t *snapshot);
bool vdc_dpll_manager_plan_tdma_window(uint32_t window_class,
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
bool vdc_dpll_manager_submit_compact_observation(
    const vdc_compact_observation_sample_t *compact);

#endif
