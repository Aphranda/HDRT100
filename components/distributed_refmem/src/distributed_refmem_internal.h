#ifndef DISTRIBUTED_REFMEM_INTERNAL_H
#define DISTRIBUTED_REFMEM_INTERNAL_H

#include "distributed_refmem.h"
#include "refmem_cmd_reply.h"
#include "refmem_sync.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "trigger_position_descriptor.h"
#include "trigger_position_mailbox.h"

#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE TDMA_FLIGHT_SHORT_PAYLOAD_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE TDMA_FLIGHT_SHORT_SLOT_SIZE

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
/* N5.2b: control coordinator slot derived from the active SlotClaimMap (lowest
 * claimed slot).  Read-only; false means "no authority" (fail closed). */
bool distributed_refmem_control_owner_slot(uint32_t *owner_slot);
bool distributed_refmem_node_load_auto_enqueue(uint32_t instance_id);
refmem_vector_table_t *distributed_refmem_vector_table(void);

/* Ordinary-mailbox VDC admission binding. The type moved here when the VDC
 * feedback command unit became its own translation unit; the storage stays
 * private to distributed_refmem.c so the Core0 RefMem service remains the only
 * writer and the command units only take read-only views. */
typedef struct {
    uint32_t active, source_slot, local_slot, control_generation;
    uint32_t ring_config_seq, schedule_crc32, clock_epoch_id, clock_run_id;
    uint32_t rx_admission_epoch;
} distributed_refmem_vdc_flight_rx_binding_t;

const distributed_refmem_vdc_flight_rx_binding_t *
distributed_refmem_vdc_flight_rx_binding(void);
bool distributed_refmem_vdc_flight_rx_admission_ready(void);
bool distributed_refmem_is_initialized(void);

bool distributed_refmem_vdc_flight_rx_read_binding(
    distributed_refmem_vdc_flight_rx_binding_t *out);
bool distributed_refmem_vdc_flight_rx_same_binding(
    const distributed_refmem_vdc_flight_rx_binding_t *a,
    const distributed_refmem_vdc_flight_rx_binding_t *b);

/* Core0 VDC vector region accessor; the priority-health unit samples the
 * asynchronous diagnostic mirror through it (RAM-resident in the definition). */
refmem_vdc_vector_region_t *distributed_refmem_vdc_vector_region(void);

/* Typed VDC feedback binding. The type moved out of the feedback unit so the
 * Core0 service path can read the rate mode through a read-only accessor; the
 * feedback unit remains the only writer. */
typedef struct {
    uint32_t mode, local, target, node_count, role_generation;
    uint32_t ring_config_seq, schedule_crc32, clock_epoch_id, clock_run_id, session, rate_mode;
} distributed_refmem_feedback_binding_t;

const distributed_refmem_feedback_binding_t *distributed_refmem_feedback_binding(void);
bool distributed_refmem_feedback_tx_active(void);

/* Published cadence for the typed VDC reference transport (was a macro inside
 * the feedback command fragment; the Core0 service path clamps to it). */
#define DISTRIBUTED_REFMEM_REFERENCE_PUBLISH_INTERVAL_MS 6u

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

#ifndef DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH
#define DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH 4u
#endif

typedef struct {
    uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE];
    uint8_t target_mask;
    uint8_t stage;
    uint8_t position_id;
    uint8_t pending;
} distributed_refmem_position_pending_t;

typedef struct {
    trigger_position_slice_t slice;
    uint32_t target_mask;
    uint32_t attempts;
} distributed_refmem_position_hold_entry_t;

typedef struct {
    distributed_refmem_position_pending_t pending[DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS];
    uint32_t drain_ms;
    uint32_t last_drain_ms;
    uint32_t coalesced_count;
    uint8_t drain_cursor;
    uint8_t wait_counted;
    trigger_position_mailbox_t rx_mailboxes[DISTRIBUTED_REFMEM_NODE_COUNT];
    trigger_position_freeze_latch_t freeze_latches[DISTRIBUTED_REFMEM_NODE_COUNT];
    volatile uint32_t cancel_requested;
    /* N8.5 running-safe reset handshake: the refmem service (same context as
     * the position TX prepare) performs the clear when reset_request is set,
     * then publishes reset_done.  No cross-task clear is performed. */
    volatile uint32_t reset_request;
    volatile uint32_t reset_done;
    uint32_t local_frozen;
    uint32_t freeze_ack[DISTRIBUTED_REFMEM_NODE_COUNT];
    uint32_t priority_owned;
    uint8_t priority_last[DISTRIBUTED_REFMEM_NODE_COUNT][TRIGGER_POSITION_SLICE_WIRE_SIZE];
    uint32_t priority_accept_count;
    uint32_t priority_duplicate_count;
    uint32_t priority_reject_count;
    uint32_t priority_decode_reject_count;
    distributed_refmem_position_hold_entry_t priority_hold[DISTRIBUTED_REFMEM_NODE_COUNT]
                                                         [DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH];
    volatile uint32_t priority_hold_head[DISTRIBUTED_REFMEM_NODE_COUNT];
    volatile uint32_t priority_hold_tail[DISTRIBUTED_REFMEM_NODE_COUNT];
    uint32_t priority_hold_count;
    uint32_t priority_hold_dropped_count;
    uint32_t priority_hold_delivered_count;
    uint32_t freeze_latched_count;
    uint32_t freeze_kept_count;
    uint32_t freeze_taken_count;
    uint32_t priority_drain_count;
    uint32_t priority_drain_fail_count;
    uint32_t priority_hold_expired_count;
    uint32_t ack_override_count;
    uint32_t diag_refused_count;
} distributed_refmem_position_state_t;

distributed_refmem_position_state_t *distributed_refmem_position_state(void);
void distributed_refmem_position_note_freeze_taken(void);
bool distributed_refmem_position_priority_owned(void);
bool distributed_refmem_position_priority_drain(uint32_t source_slot);
void distributed_refmem_position_cancel_service(void);

#define s_position_pending (distributed_refmem_position_state()->pending)
#define s_position_drain_ms (distributed_refmem_position_state()->drain_ms)
#define s_position_last_drain_ms (distributed_refmem_position_state()->last_drain_ms)
#define s_position_coalesced_count (distributed_refmem_position_state()->coalesced_count)
#define s_position_drain_cursor (distributed_refmem_position_state()->drain_cursor)
#define s_position_wait_counted (distributed_refmem_position_state()->wait_counted)
#define s_position_rx_mailboxes (distributed_refmem_position_state()->rx_mailboxes)
#define s_position_freeze_latches (distributed_refmem_position_state()->freeze_latches)
#define s_position_cancel_requested (distributed_refmem_position_state()->cancel_requested)
#define s_position_reset_request (distributed_refmem_position_state()->reset_request)
#define s_position_reset_done (distributed_refmem_position_state()->reset_done)
#define s_position_local_frozen (distributed_refmem_position_state()->local_frozen)
#define s_position_freeze_ack (distributed_refmem_position_state()->freeze_ack)
#define s_position_priority_owned (distributed_refmem_position_state()->priority_owned)
#define s_position_priority_last (distributed_refmem_position_state()->priority_last)
#define s_position_priority_accept_count (distributed_refmem_position_state()->priority_accept_count)
#define s_position_priority_duplicate_count (distributed_refmem_position_state()->priority_duplicate_count)
#define s_position_priority_reject_count (distributed_refmem_position_state()->priority_reject_count)
#define s_position_priority_decode_reject_count (distributed_refmem_position_state()->priority_decode_reject_count)
#define s_position_priority_hold (distributed_refmem_position_state()->priority_hold)
#define s_position_priority_hold_head (distributed_refmem_position_state()->priority_hold_head)
#define s_position_priority_hold_tail (distributed_refmem_position_state()->priority_hold_tail)
#define s_position_priority_hold_count (distributed_refmem_position_state()->priority_hold_count)
#define s_position_priority_hold_dropped_count (distributed_refmem_position_state()->priority_hold_dropped_count)
#define s_position_priority_hold_delivered_count (distributed_refmem_position_state()->priority_hold_delivered_count)
#define s_position_freeze_latched_count (distributed_refmem_position_state()->freeze_latched_count)
#define s_position_freeze_kept_count (distributed_refmem_position_state()->freeze_kept_count)
#define s_position_freeze_taken_count (distributed_refmem_position_state()->freeze_taken_count)
#define s_position_priority_drain_count (distributed_refmem_position_state()->priority_drain_count)
#define s_position_priority_drain_fail_count (distributed_refmem_position_state()->priority_drain_fail_count)
#define s_position_priority_hold_expired_count (distributed_refmem_position_state()->priority_hold_expired_count)
#define s_position_ack_override_count (distributed_refmem_position_state()->ack_override_count)
#define s_position_diag_refused_count (distributed_refmem_position_state()->diag_refused_count)

/* Helpers the Core0 service path still calls directly. They were file
 * static inside the .inc fragments; the split promotes them to module
 * scope so the parent translation unit keeps its call sites. */
void action_stop_requested(void);
void action_observe(const tdma_ring_runtime_snapshot_t *ring);
void action_patch_mailbox(uint8_t *mailbox, bool control_allowed);
void action_mailbox_published(const uint8_t *mailbox);
bool action_receive_mailbox(uint32_t source,const uint8_t *mailbox);
bool distributed_refmem_position_ack_control_available(uint8_t mailbox_class);
void distributed_refmem_position_ack_patch(uint8_t *mailbox);
void distributed_refmem_position_ack_receive(uint32_t slot, const uint8_t *mailbox);
void distributed_refmem_position_ack_reset(void);
bool distributed_refmem_feedback_refresh(tdma_service_service_t *owner);
int distributed_refmem_feedback_prepare(tdma_service_service_t *owner);
void distributed_refmem_feedback_fragment(uint8_t *mailbox);
void distributed_refmem_feedback_published(bool feedback, uint32_t sequence);
void distributed_refmem_feedback_receive(tdma_service_service_t *owner, uint32_t source, const uint8_t *mailbox, const tdma_flight_rx_view_t *view);
void distributed_refmem_publish_priority_health_core0(void);

#endif
