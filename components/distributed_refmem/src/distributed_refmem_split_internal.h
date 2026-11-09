#ifndef DISTRIBUTED_REFMEM_SPLIT_INTERNAL_H
#define DISTRIBUTED_REFMEM_SPLIT_INTERNAL_H

#include "distributed_refmem_internal.h"
#include "refmem_le.h"

#define DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT 16u
#define DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO 0u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_QUEUE_COUNT 8u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH 1u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN 1u
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT \
    TDMA_FLIGHT_SHORT_SLOT_COUNT
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_INTERVAL_MS 1u
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC \
    TDMA_FLIGHT_MAILBOX_MAGIC
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION \
    TDMA_FLIGHT_MAILBOX_VERSION

/* N5.0.4p: state types hoisted out of distributed_refmem.c. */
typedef enum {
    DISTRIBUTED_REFMEM_AUTO_INTENT_NONE = 0u,
    DISTRIBUTED_REFMEM_AUTO_INTENT_TX_NODE_LOAD = 1u,
    DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW = 2u,
} distributed_refmem_auto_intent_t;

typedef struct {
    uint32_t instance_id;
    distributed_refmem_node_load_owner_t owner;
    void *context;
} distributed_refmem_node_load_owner_entry_t;

typedef struct {
    uint32_t enabled;
    uint8_t local_slot;
    uint8_t target_mask;
    uint32_t epoch_id;
    uint32_t run_id;
    uint32_t baud_hz;
    uint32_t deadline_us;
    uint32_t uplink_duplex_mode;
    uint32_t downlink_duplex_mode;
    refmem_spi_physical_pin_config_t uplink_adapter_pins;
    refmem_spi_physical_pin_config_t downlink_adapter_pins;
    uint32_t pending_instance[DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_QUEUE_COUNT];
    uint32_t pending_count;
    uint32_t active_intent;
    uint32_t active_instance_id;
    uint32_t active_intent_seq;
    uint32_t last_processed_completed_seq;
    uint32_t next_seq32;
    uint32_t submitted_tx_count;
    uint32_t submitted_rx_count;
    uint32_t applied_rx_count;
    uint32_t failed_apply_count;
    uint32_t dropped_pending_count;
    uint32_t last_rx_result;
    uint32_t last_frame_type;
    uint32_t last_source_slot;
    uint32_t last_error;
} distributed_refmem_node_load_auto_sync_t;

typedef struct {
    uint32_t active;
    uint32_t active_intent_seq;
    uint32_t next_window_seq;
    uint32_t last_processed_completed_seq;
    uint32_t submitted_count;
    uint32_t frame_ready_count;
    uint32_t accepted_count;
    uint32_t invalid_count;
    uint32_t timeout_count;
    uint32_t window_miss_count;
    uint32_t submit_reject_count;
    uint32_t last_result;
    uint32_t last_rx_result;
    uint32_t last_error;
    uint32_t last_source_slot;
    uint32_t last_command_seq;
} distributed_refmem_vdc_follower_rx_t;

typedef struct {
    uint32_t active;
    uint32_t window_planned;
    uint32_t source_slot;
    uint32_t node_count;
    uint32_t next_target_index;
    uint32_t command_seq;
    uint32_t control_generation;
    uint32_t schedule_crc32;
    uint32_t window_epoch;
    uint64_t effective_vdc_time_ns;
    uint64_t window_end_ns;
    uint64_t guard_start_ns;
    uint64_t guard_end_ns;
    int32_t period_adjust_ppb;
    int32_t phase_offset_ns;
    uint32_t lock_state;
    uint32_t quality;
    uint32_t epoch_id;
    uint32_t run_id;
    uint32_t source_update_seq;
} distributed_refmem_vdc_command_batch_t;

/* cross-unit state and helpers */
extern refmem_vector_table_t s_distributed_refmem_table;
extern refmem_command_slot_t s_refmem_command_slot;
extern refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
extern refmem_spi_physical_adapter_t s_refmem_realtime_spi;
extern distributed_refmem_status_t s_status;
extern refmem_sync_context_t s_refmem_sync_context;
extern refmem_sync_vdc_context_t s_vdc_command_context;
extern distributed_refmem_node_load_auto_sync_t s_node_load_auto_sync;
extern distributed_refmem_vdc_follower_rx_t s_vdc_follower_rx;
extern uint32_t s_service_count;
extern bool s_refmem_core_initialized;
extern uint32_t s_tdma_ring_log_last_ms;
extern bool s_tdma_ring_log_enabled;
extern uint32_t s_vdc_vector_publish_sequence;
extern uint32_t s_dpll_vector_publish_sequence;
extern uint32_t s_vdc_vector_source_update_seq;
extern uint32_t s_dpll_vector_source_update_seq;
extern uint32_t s_next_runtime_vector;
extern distributed_refmem_vdc_command_batch_t s_vdc_command_batch;
extern uint32_t s_vdc_command_sequence;
extern uint32_t s_vdc_command_frame_sequence;
extern uint32_t s_vdc_command_published_update_seq;
extern uint32_t s_vdc_command_published_generation;
extern uint32_t s_vdc_command_published_source_slot;
extern uint32_t s_vdc_command_published_schedule_crc32;
extern uint8_t s_vdc_command_context_local_slot;
extern uint32_t s_vdc_command_context_epoch_id;
extern uint32_t s_vdc_command_context_run_id;
extern uint32_t s_vdc_command_context_schedule_epoch;
extern bool s_vdc_command_context_initialized;
extern bool s_vdc_command_context_ready;
extern distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;
extern volatile uint32_t s_vdc_flight_rx_guard;
extern distributed_refmem_vdc_flight_rx_binding_t s_vdc_flight_rx_binding;
extern distributed_refmem_vdc_flight_rx_snapshot_t s_vdc_flight_rx;
extern bool s_vdc_flight_rx_admission_ready;
extern volatile uint32_t s_tdma_ring_arm_last_result;
extern uint32_t s_node_control_seq;
extern distributed_refmem_node_load_owner_entry_t s_node_load_owners[DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT];

uint32_t distributed_refmem_next_nonzero_sequence(uint32_t *sequence);
uint32_t distributed_refmem_flight_input_offset_for_slot(uint32_t slot);
uint32_t distributed_refmem_flight_publish_mask_for_slot(uint32_t slot);
void distributed_refmem_tdma_reset_resident_command_state(void);
void distributed_refmem_tdma_sync_resident_command_identity( const vdc_dpll_manager_refmem_snapshot_t *snapshot);
tdma_process_image_map_t distributed_refmem_default_flight_map(uint32_t node_count);
void distributed_refmem_tdma_flight_sync_init(void);
void distributed_refmem_tdma_flight_sync_update_ring( const tdma_ring_runtime_snapshot_t *ring);
bool distributed_refmem_vdc_flight_rx_refresh(tdma_service_service_t *owner);
void distributed_refmem_vdc_flight_rx_accept(uint32_t slot, const uint8_t *mailbox, const tdma_flight_rx_view_t *view);
bool distributed_refmem_tdma_prepare_vdc_command_record( const tdma_ring_runtime_snapshot_t *ring);
bool distributed_refmem_tdma_flight_build_compact_mailbox( uint8_t source_slot, uint8_t target_mask, uint32_t seq32, uint32_t value, uint16_t field_id, uint8_t message_class, uint8_t *mailbox, size_t mailbox_size);
void distributed_refmem_tdma_flight_sync_store_mailbox( uint8_t source_slot, const uint8_t *mailbox, size_t mailbox_size);
bool distributed_refmem_tdma_flight_expand_compact_delta( const uint8_t *mailbox, size_t mailbox_size, uint8_t *frame, size_t frame_capacity, size_t *frame_size);
void distributed_refmem_tdma_flight_parse_vdc_fragment( const uint8_t *mailbox, size_t mailbox_size);
void distributed_refmem_tdma_parse_position_fragment( uint32_t source_slot, const uint8_t *mailbox);
bool distributed_refmem_position_tx_prepare(tdma_service_service_t *owner);
void distributed_refmem_tdma_flight_parse_mailbox( const uint8_t *mailbox, size_t mailbox_size);
void distributed_refmem_tdma_flight_sync_publish( tdma_service_service_t *owner, const tdma_ring_runtime_snapshot_t *ring);
void distributed_refmem_tdma_flight_sync_receive( tdma_service_service_t *owner, const tdma_ring_runtime_snapshot_t *ring);
void distributed_refmem_tdma_flight_sync_service(void);
void distributed_refmem_node_load_auto_init(void);
bool distributed_refmem_fill_vdc_data_window_plan( refmem_realtime_tdma_intent_config_t *config);
void distributed_refmem_node_load_auto_pop_front(void);
bool distributed_refmem_tdma_busy(const refmem_realtime_tdma_snapshot_t *snapshot);
uint32_t distributed_refmem_u32_payload_crc32(const uint32_t *fields, uint32_t field_count);
uint32_t distributed_refmem_model_payload_crc32(uint32_t slot_id, uint32_t output_index);
bool distributed_refmem_command_state_is_complete(uint32_t state);
uint32_t distributed_refmem_next_command_seq( const refmem_command_snapshot_t *snapshot);
bool distributed_refmem_execute_node_load_owner(uint32_t instance_id, uint32_t slot_id, uint32_t payload_ref);
bool distributed_refmem_slot_claim_gate_ready(void);
bool distributed_refmem_flash_activation_safe(void);
bool distributed_refmem_tdma_profile_activation_ready( const tdma_foundation_profile_t *profile, uint32_t *schedule_crc32);
bool distributed_refmem_apply_tdma_foundation_profile( const tdma_foundation_profile_t *profile, uint32_t schedule_crc32);
refmem_command_reason_t distributed_refmem_activation_nack_reason(uint32_t result);
refmem_vector_header_region_t *distributed_refmem_header(void);
void distributed_refmem_publish_status_locked(void);
uint32_t distributed_refmem_header_crc_locked(void);
refmem_dpll_vector_region_t *DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_dpll_vector_region)(void);
void DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_copy_to_volatile)(volatile uint8_t *destination, const void *source, size_t size);
void distributed_refmem_copy_from_volatile(void *destination, const volatile uint8_t *source, size_t size);
uint32_t DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_next_publish_sequence)(uint32_t *sequence);
uint32_t DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_begin_vector_write)(volatile uint32_t *seqlock, uint32_t *stable_sequence);
void DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_publish_vdc_vector_payload)( refmem_vdc_vector_region_t *region, refmem_vdc_vector_payload_t *payload);
void DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_publish_dpll_vector_payload)( refmem_dpll_vector_region_t *region, refmem_dpll_vector_payload_t *payload);
bool DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_vector_hardware_evidence_valid)( const vdc_dpll_manager_vector_snapshot_t *snapshot);
uint32_t __attribute__((noinline)) DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_vector_flags)( const vdc_dpll_manager_vector_snapshot_t *snapshot);
void DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_fill_vdc_vector_payload)( refmem_vdc_vector_payload_t *payload, const vdc_dpll_manager_vector_snapshot_t *snapshot, uint32_t publish_sequence, bool snapshot_valid);
void DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_fill_dpll_vector_payload)( refmem_dpll_vector_payload_t *payload, const vdc_dpll_manager_vector_snapshot_t *snapshot, uint32_t publish_sequence, bool snapshot_valid);
bool distributed_refmem_read_vdc_vector_snapshot( distributed_refmem_vdc_vector_snapshot_t *snapshot);
bool distributed_refmem_read_dpll_vector_snapshot( distributed_refmem_dpll_vector_snapshot_t *snapshot);
bool distributed_refmem_mark_init_failed(distributed_refmem_init_stage_t stage);
uint32_t distributed_refmem_deadline_us_to_ms(uint32_t deadline_us);
bool distributed_refmem_tdma_transmit(void *context, const uint8_t *frame, size_t frame_size, refmem_spi_physical_role_t role, uint32_t baud_hz, const refmem_spi_physical_pin_config_t *pins, uint32_t deadline_us, refmem_realtime_tdma_exec_status_t *status);
bool distributed_refmem_tdma_receive(void *context, uint8_t *frame, size_t frame_capacity, refmem_spi_physical_role_t role, uint32_t baud_hz, const refmem_spi_physical_pin_config_t *pins, uint32_t deadline_us, refmem_realtime_tdma_exec_status_t *status);
bool distributed_refmem_apply_node_load_sync_payload_internal( const uint8_t *payload, uint16_t payload_size);
void distributed_refmem_node_load_auto_process_completed( const refmem_realtime_tdma_snapshot_t *snapshot);
bool distributed_refmem_node_load_auto_submit_tx(void);
bool distributed_refmem_node_load_auto_submit_rx(void);
void distributed_refmem_node_load_auto_service(void);
void distributed_refmem_vdc_follower_rx_process_completed( const refmem_realtime_tdma_snapshot_t *tdma, uint32_t expected_source_slot);
void distributed_refmem_vdc_follower_rx_service(void);
void distributed_refmem_refresh_directory_flags_locked(void);
void distributed_refmem_publish_runtime_locked(void);
void DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_publish_runtime_vector_if_pending)( uint32_t source_update_seq);
void distributed_refmem_log_tdma_ring_service(void);
void distributed_refmem_mirror_local_control(void);
bool distributed_refmem_sync_command_handler( void *handler_context, const refmem_sync_command_payload_t *payload, uint8_t source_slot, uint32_t epoch_id, uint32_t run_id);
bool distributed_refmem_refresh_vdc_command_context(void);
bool distributed_refmem_refresh_vdc_command_context_from_snapshot( const vdc_dpll_manager_refmem_snapshot_t *snapshot);
void DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_reset_vdc_command_batch)(void);
bool DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_begin_vdc_command_batch)( const vdc_dpll_manager_refmem_snapshot_t *snapshot);
bool DISTRIBUTED_REFMEM_TIME_CRITICAL( distributed_refmem_capture_vdc_command_batch)( const vdc_dpll_manager_refmem_snapshot_t *snapshot);
bool distributed_refmem_publish_vdc_command_once( const vdc_dpll_manager_refmem_snapshot_t *snapshot);

#endif /* DISTRIBUTED_REFMEM_SPLIT_INTERNAL_H */
