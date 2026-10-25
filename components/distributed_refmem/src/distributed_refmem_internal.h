#ifndef DISTRIBUTED_REFMEM_INTERNAL_H
#define DISTRIBUTED_REFMEM_INTERNAL_H

#include "distributed_refmem.h"
#include "refmem_cmd_reply.h"
#include "refmem_sync.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "trigger_position_descriptor.h"

#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE TDMA_FLIGHT_SHORT_PAYLOAD_SIZE

#include <stdbool.h>
#include <stdint.h>

/* Internal command-reply helpers (N5.0.2b).  The implementation lives in
 * refmem_cmd_reply.c; only this private header is shared inside the domain. */
void distributed_refmem_cmd_reply_arm(uint32_t command_seq,
                                      uint32_t origin_slot,
                                      uint32_t state,
                                      uint32_t reason);
bool distributed_refmem_cmd_reply_pending(void);
void distributed_refmem_cmd_reply_store(uint32_t source_slot,
                                        uint16_t seq16,
                                        uint8_t flags,
                                        uint8_t reason);
void distributed_refmem_cmd_reply_service(void);
bool distributed_refmem_cmd_reply_tx_get(refmem_cmd_reply_t *reply);
uint32_t distributed_refmem_cmd_reply_target_mask(void);
void distributed_refmem_cmd_reply_note_published(void);

/* Shared node region accessor used by the node-control implementation. */
refmem_vector_node_region_t *distributed_refmem_node_region(uint32_t node_id);

void distributed_refmem_node_control_service(void);
bool distributed_refmem_post_command_replacing_complete(
    refmem_command_request_t *request, uint32_t issue_tick32);
bool distributed_refmem_node_load_auto_enqueue(uint32_t instance_id);
refmem_vector_table_t *distributed_refmem_vector_table(void);

typedef struct {
    uint32_t enabled;
    uint32_t local_slot;
    uint32_t node_count;
    uint32_t active_mask;
    uint32_t reference_slot;
    uint32_t remote_slot;
    uint32_t publish_interval_ms;
    uint32_t last_publish_ms;
    uint32_t next_seq32;
    uint32_t tx_publish_count;
    uint32_t tx_reject_count;
    uint32_t rx_acquire_count;
    uint32_t rx_empty_count;
    uint32_t rx_accept_count;
    uint32_t rx_reject_count;
    uint32_t rx_duplicate_skip_count;
    uint32_t rx_bad_mailbox_count;
    uint32_t rx_seen_mask;
    uint32_t rx_last_seq_by_source[REFMEM_SYNC_NODE_COUNT];
    uint32_t last_rx_result;
    uint32_t last_frame_type;
    uint32_t last_source_slot;
    uint32_t last_seq32;
    uint32_t last_value_u32;
    int32_t last_vdc_phase_offset_ns;
    int32_t last_vdc_rate_adjust_ppb;
    uint32_t last_vdc_lock_state;
    uint32_t last_vdc_quality;
    uint32_t last_ack_seq16;
    uint32_t last_ack_flags;
    uint32_t last_control_opcode;
    uint32_t last_control_seq8;
    uint32_t last_optional_diagnostic;
    uint32_t last_mailbox_crc16;
    uint32_t vdc_command_fragment_rx_count;
    uint32_t vdc_command_fragment_complete_count;
    uint32_t vdc_command_fragment_reject_count;
    uint32_t vdc_command_accept_count;
    uint32_t vdc_command_last_seq;
    uint32_t vdc_command_prepare_attempt_count;
    uint32_t vdc_command_prepare_reject_count;
    uint32_t vdc_command_prepare_last_reason;
    uint32_t vdc_command_prepare_last_gate_code;
    uint32_t vdc_command_prepare_last_gate_slot;
    uint32_t vdc_command_prepare_last_gate_evidence;
    uint64_t vdc_command_last_common_time_ns;
    uint32_t vdc_command_record_active;
    uint8_t vdc_command_record_fragment_index;
    uint32_t vdc_command_record_source_update_seq;
    uint32_t position_tx_active, position_tx_target_mask;
    uint32_t position_tx_fragment_index, position_tx_sequence;
    uint32_t position_retiring;
    uint32_t position_tx_publish_count, position_tx_reject_count,
        position_tx_position_id;
    uint32_t position_tx_phase, position_tx_wait_reason,
        position_tx_wait_count, position_tx_submitted_sequence;
    uint32_t position_rx_phase, position_rx_last_failure;
    uint8_t position_tx_wire[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE];
    trigger_position_fragment_context_t position_rx_fragments[DISTRIBUTED_REFMEM_NODE_COUNT];
    uint32_t position_rx_valid_mask, position_rx_complete_count,
        position_rx_reject_count, position_rx_source_slot,
        position_rx_position_id;
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
    uint32_t next_command_seq;
    uint32_t vdc_command_frame_seq;
    uint32_t vdc_command_transport_valid;
    uint32_t vdc_command_transport_mode;
    uint32_t vdc_command_transport_source_slot;
    uint32_t vdc_command_transport_generation;
    uint32_t vdc_command_transport_schedule_crc32;
    uint32_t vdc_command_transport_epoch_id;
    uint32_t vdc_command_transport_run_id;
    uint32_t vdc_command_rx_admission_epoch;
    uint8_t vdc_command_record[sizeof(refmem_sync_vdc_command_payload_t)];
    refmem_sync_vdc_fragment_context_t vdc_command_fragments;
#endif
    uint32_t last_error;
    refmem_sync_delta_context_t context;
    uint8_t tx_image[DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE];
} distributed_refmem_tdma_flight_sync_t;

distributed_refmem_tdma_flight_sync_t *distributed_refmem_tdma_flight_sync(void);

#endif
