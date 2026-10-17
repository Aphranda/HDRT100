#ifndef DISTRIBUTED_REFMEM_H
#define DISTRIBUTED_REFMEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "trigger_position_descriptor.h"
#include "trigger_position_slice.h"
#include "tdma_priority_rx.h"

#define DISTRIBUTED_REFMEM_TABLE_SIZE       18432u
#define DISTRIBUTED_REFMEM_LAYOUT_VERSION   2u
#define DISTRIBUTED_REFMEM_NODE_COUNT       8u
#define DISTRIBUTED_REFMEM_LOCAL_NODE_ID    0u

/* Keep the unaccepted resident command prototype outside the ordinary TDMA
 * build, including RX dispatch and private assembly state.  An enabled build
 * is experimental and needs its own command evidence as well as P3. */
#ifndef DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
#define DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED 0u
#endif

#define DISTRIBUTED_REFMEM_NODE_FLAG_VIRTUAL 0x00000001u

#define DISTRIBUTED_REFMEM_OWNER_CORE0       0u
#define DISTRIBUTED_REFMEM_OWNER_CORE1       1u
#define DISTRIBUTED_REFMEM_OWNER_SHARED      2u

#define DISTRIBUTED_REFMEM_IRQ_USB_MASK      0x00000001u
#define DISTRIBUTED_REFMEM_IRQ_STORAGE_MASK  0x00000002u
#define DISTRIBUTED_REFMEM_IRQ_OTA_MASK      0x00000004u
#define DISTRIBUTED_REFMEM_IRQ_UI_MASK       0x00000008u
#define DISTRIBUTED_REFMEM_IRQ_PIO_MASK      0x00000100u
#define DISTRIBUTED_REFMEM_IRQ_DMA_MASK      0x00000200u
#define DISTRIBUTED_REFMEM_IRQ_CAPTURE_MASK  0x00000400u
#define DISTRIBUTED_REFMEM_IRQ_TIMER_MASK    0x00000800u

#define DISTRIBUTED_REFMEM_PROT_RAM_RESIDENT_REQUIRED 0x00000001u
#define DISTRIBUTED_REFMEM_PROT_FLASH_LOCKOUT_READY   0x00000002u
#define DISTRIBUTED_REFMEM_PROT_CORE1_PARKED          0x00000004u
#define DISTRIBUTED_REFMEM_PROT_ENTRY_OWNER_VALID     0x00000008u

#define DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF        1u
#define DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_FULL        2u

#define DISTRIBUTED_REFMEM_FLAG_DIRECTORY_VALID       0x00000001u
#define DISTRIBUTED_REFMEM_FLAG_DIRECTORY_CRC_VALID   0x00000002u
#define DISTRIBUTED_REFMEM_FLAG_APP_MODEL_VALID       0x00000004u

#define DISTRIBUTED_REFMEM_HEADER_SIZE      1024u
#define DISTRIBUTED_REFMEM_SYSTEM_SIZE      1024u
#define DISTRIBUTED_REFMEM_ROLE_SIZE        1024u
#define DISTRIBUTED_REFMEM_VDC_SIZE         1024u
#define DISTRIBUTED_REFMEM_LOOP_SIZE        1024u
#define DISTRIBUTED_REFMEM_DPLL_SIZE        1024u
#define DISTRIBUTED_REFMEM_NODE_SLOT_SIZE   128u
#define DISTRIBUTED_REFMEM_TRIGGER_SIZE     1024u
#define DISTRIBUTED_REFMEM_IO_SIZE          1024u
#define DISTRIBUTED_REFMEM_CAL_SIZE         1024u
#define DISTRIBUTED_REFMEM_STATS_SIZE       1024u
#define DISTRIBUTED_REFMEM_ACK_CMD_SIZE     1024u
#define DISTRIBUTED_REFMEM_FAULT_SIZE       1024u
#define DISTRIBUTED_REFMEM_GATEWAY_SIZE     1024u
#define DISTRIBUTED_REFMEM_SERVICE_SIZE     1024u
#define DISTRIBUTED_REFMEM_TLV_SIZE         3072u

#include "refmem_command.h"
#include "refmem_realtime_tdma.h"
#include "refmem_sync.h"
#include "refmem_sync_vdc_feedback.h"
#include "refmem_vdc_vector.h"

typedef bool (*distributed_refmem_node_load_owner_t)(uint32_t instance_id,
                                                     uint32_t slot_id,
                                                     uint32_t payload_ref,
                                                     void *context);

typedef enum {
    DISTRIBUTED_REFMEM_NODE_MISSING = 0,
    DISTRIBUTED_REFMEM_NODE_OK = 1,
    DISTRIBUTED_REFMEM_NODE_STALE = 2,
    DISTRIBUTED_REFMEM_NODE_INVALID = 3,
    DISTRIBUTED_REFMEM_NODE_FAULT = 4,
} distributed_refmem_node_state_t;

typedef enum {
    DISTRIBUTED_REFMEM_NODE_TYPE_BOARD = 0,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_VNA = 1,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_TURNTABLE = 2,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_DUT = 3,
    DISTRIBUTED_REFMEM_NODE_TYPE_TEST_AGENT = 4,
} distributed_refmem_node_type_t;

typedef enum {
    DISTRIBUTED_REFMEM_TDMA_ARM_NOT_ATTEMPTED = 0u,
    DISTRIBUTED_REFMEM_TDMA_ARM_OK = 1u,
    DISTRIBUTED_REFMEM_TDMA_ARM_OWNER_UNAVAILABLE = 2u,
    DISTRIBUTED_REFMEM_TDMA_ARM_SNAPSHOT_UNAVAILABLE = 3u,
    DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_ACTIVE = 4u,
    DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_REJECTED = 5u,
    DISTRIBUTED_REFMEM_TDMA_ARM_STAGED_CONFIG_MISSING = 6u,
    DISTRIBUTED_REFMEM_TDMA_ARM_CALIBRATION_GATE_REJECTED = 7u,
    DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_CONFIG_REJECTED = 8u,
    DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_BUSY = 9u,
    DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_ENGINE_ACTIVE = 10u,
} distributed_refmem_tdma_arm_result_t;

typedef enum {
    DISTRIBUTED_REFMEM_INIT_STAGE_UNINIT = 0u,
    DISTRIBUTED_REFMEM_INIT_STAGE_APP_MODEL = 1u,
    DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OWNER = 2u,
    DISTRIBUTED_REFMEM_INIT_STAGE_COMMAND_SLOT = 3u,
    DISTRIBUTED_REFMEM_INIT_STAGE_NODE_LOAD_AUTO = 4u,
    DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OPS = 5u,
    DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_PROFILE = 6u,
    DISTRIBUTED_REFMEM_INIT_STAGE_VECTOR_TABLE = 7u,
    DISTRIBUTED_REFMEM_INIT_STAGE_READY = 8u,
} distributed_refmem_init_stage_t;

typedef struct {
    uint32_t table_size;
    uint32_t layout_version;
    uint32_t table_seq;
    uint32_t local_node_id;
    uint32_t node_count;
    uint32_t local_heartbeat;
    uint32_t service_count;
    uint32_t flags;
    uint32_t initialized;
    uint32_t init_stage;
    uint32_t init_error;
} distributed_refmem_status_t;

typedef struct {
    uint32_t node_id;
    uint32_t state;
    uint32_t heartbeat;
    uint32_t slot_version;
    uint32_t last_update_ms;
    uint32_t stale_count;
    uint32_t fault_code;
    uint32_t flags;
    uint32_t node_type;
} distributed_refmem_node_snapshot_t;

typedef struct {
    uint32_t table_seq;
    uint32_t owner;
    uint32_t crc32;
    uint32_t stale;
    uint32_t flags;
} distributed_refmem_slot_guard_t;

typedef struct {
    uint32_t version;
    uint32_t table_seq;
    uint32_t core_count;
    uint32_t core0_vtor_owner;
    uint32_t core1_vtor_owner;
    uint32_t core0_irq_owner_mask;
    uint32_t core1_irq_owner_mask;
    uint32_t entry_table_owner;
    uint32_t flags;
    distributed_refmem_slot_guard_t guard;
} distributed_refmem_core_vector_snapshot_t;

typedef struct {
    uint32_t version;
    uint32_t table_seq;
    uint32_t ram_resident_required;
    uint32_t flash_lockout_supported;
    uint32_t flash_lockout_online;
    uint32_t flash_lockout_requested;
    uint32_t flash_lockout_acknowledged;
    uint32_t park_state;
    uint32_t last_result;
    uint32_t last_elapsed_us;
    uint32_t request_seq;
    uint32_t ack_seq;
    uint32_t release_seq;
    uint32_t timeout_count;
    uint32_t release_timeout_count;
    uint32_t entry_table_owner;
    uint32_t flags;
    distributed_refmem_slot_guard_t guard;
} distributed_refmem_runtime_protection_snapshot_t;

typedef struct {
    uint32_t enabled;
    uint32_t local_slot;
    uint32_t target_mask;
    uint32_t baud_hz;
    uint32_t deadline_us;
    uint32_t uplink_duplex_mode;
    uint32_t uplink_rx_pin;
    uint32_t uplink_sck_pin;
    uint32_t uplink_tx_pin;
    uint32_t downlink_duplex_mode;
    uint32_t downlink_rx_pin;
    uint32_t downlink_sck_pin;
    uint32_t downlink_tx_pin;
    uint32_t pending_count;
    uint32_t active_intent;
    uint32_t active_instance_id;
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
} distributed_refmem_node_load_auto_sync_snapshot_t;

/* Dedicated VDC command RX telemetry.  This channel is independent from
 * node-load maintenance so a follower can receive commands while that
 * maintenance path remains disabled. */
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
} distributed_refmem_vdc_follower_rx_snapshot_t;

typedef enum {
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_NONE = 0u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_BAD_ARGUMENT = 1u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_SNAPSHOT = 2u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_IDENTITY = 3u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_WINDOW = 4u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_COMMON_TIME = 5u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_OVERFLOW = 6u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_SNAPSHOT = 7u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_INACTIVE = 8u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_SCHEDULE = 9u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_INVALID = 10u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_UNCORRELATED = 11u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_FLAGS = 12u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_TIMESTAMP = 13u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_EFFECTIVE = 14u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_ORDER = 15u,
    DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_STALE = 16u,
} distributed_refmem_vdc_command_prepare_reason_t;

typedef struct {
    uint32_t enabled;
    uint32_t local_slot;
    uint32_t node_count;
    uint32_t active_mask;
    uint32_t reference_slot;
    uint32_t remote_slot;
    uint32_t payload_size;
    uint32_t mailbox_size;
    uint32_t publish_interval_ms;
    uint32_t next_seq32;
    uint32_t tx_publish_count;
    uint32_t tx_reject_count;
    uint32_t rx_acquire_count;
    uint32_t rx_empty_count;
    uint32_t rx_accept_count;
    uint32_t rx_reject_count;
    uint32_t rx_duplicate_skip_count;
    uint32_t rx_bad_mailbox_count;
    uint32_t last_rx_result;
    uint32_t last_frame_type;
    uint32_t last_source_slot;
    uint32_t last_seq32;
    uint32_t last_value_u32;
    uint32_t last_error;
    uint32_t wire_layout_version;
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
    uint32_t vdc_command_record_active;
    uint32_t vdc_command_record_fragment_index;
    uint32_t vdc_command_record_source_update_seq;
    uint32_t vdc_command_last_common_time_ns_lo;
    uint32_t vdc_command_last_common_time_ns_hi;
} distributed_refmem_tdma_flight_sync_snapshot_t;

/* Received ordinary 0x10 mailbox telemetry, not a DCO command or clock
 * qualification. retained survives STOP; active additionally requires the
 * current acknowledged ring and FOLLOWER binding. receive_count is a
 * saturating boot-lifetime count. mailbox_seq is the sender's mailbox
 * publication sequence, not its DPLL update sequence. */
typedef struct {
    uint32_t retained;
    uint32_t active;
    uint32_t source_slot;
    uint32_t local_slot;
    uint32_t control_generation;
    uint32_t ring_config_seq;
    uint32_t schedule_crc32;
    uint32_t mailbox_seq;
    uint32_t receive_count;
    int32_t phase_offset_ns;
    int32_t period_adjust_ppb;
    uint32_t lock_state;
    uint32_t quality;
    uint32_t mailbox_crc16;
    uint32_t rx_admission_epoch;
    uint32_t transport_sequence;
} distributed_refmem_vdc_flight_rx_snapshot_t;

/* One bounded guarded copy. Only Core0 RefMem publishes; this slice has no
 * Core1 consumer. A false result leaves the destination unspecified. */
bool distributed_refmem_get_vdc_flight_rx(
    distributed_refmem_vdc_flight_rx_snapshot_t *snapshot);

/* Core1 submits one immutable T2 request; Core0 accepts and encodes it at
 * the RefMem service boundary, then emits one fragment per flight mailbox. */
bool distributed_refmem_stage_position_descriptor(
    const trigger_position_descriptor_t *descriptor, uint32_t target_mask);
/* True while a request or its encoded fragments are pending. */
bool distributed_refmem_position_descriptor_pending(void);
/* STOP intent; Core0 retires queued and active fragments on its next service. */
void distributed_refmem_request_position_cancel(void);
/* Core0 caller MUST hold the TDMA STOP-maintenance owner gate (Core1 parked).
 * First retire shared TX with the existing stopped FLIGHT:FIFO:RESET: this
 * API refuses any resident/queued shared TX and does not revoke wire frames.
 * Drain at most the fixed RX FIFO capacity, parsing ordinary payloads as usual
 * while discarding T2, then reset only private position mailboxes/fragments.
 * No nested owner lock and no shared RX admission-epoch change. */
bool distributed_refmem_reset_position_stopped(void);
/* Core1 consumes one complete Core0-published descriptor. */
bool distributed_refmem_get_position_descriptor(
    uint32_t source_slot, trigger_position_descriptor_t *descriptor);
/* T2 freeze completion (TDMA-SEAT-01 first class). A follower T2 publishes
 * its frozen positions; every image it sends then carries that state. The
 * origin reads which positions every target has reported frozen for run_id. */
void distributed_refmem_note_position_frozen(uint32_t run_id, bool active,
                                             uint32_t frozen_mask);
uint32_t distributed_refmem_position_freeze_acked(uint32_t run_id, uint32_t target_mask);
/* Core1 priority-lane receiver of origin first-class T2 records. */
void distributed_refmem_position_priority_rx(const tdma_priority_rx_record_t *record);

/* Core0 diagnostic snapshot. Counts represent software handoffs, not wire
 * delivery or hardware action completion. The phase fields distinguish a
 * staged descriptor, resource/priority waits, a submitted TX image, and a
 * completed RX descriptor. */
typedef enum {
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_IDLE = 0u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_STAGED = 1u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_VDC = 2u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_RESOURCE = 3u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_FEEDBACK = 4u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_SUBMITTED = 5u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_RECEIVED = 6u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_COMPLETE = 7u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED = 8u,
} distributed_refmem_position_transport_phase_t;

typedef enum {
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE = 0u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_BAD_DESCRIPTOR = 1u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FIFO_SNAPSHOT = 2u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FIFO_READY = 3u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FEEDBACK = 4u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_PUBLISH = 5u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_CRC = 6u,
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FRAGMENT = 7u,
    /* The single-depth first-class seat still held the previous record, so this pass did
     * NOT offer: the prepared record is kept and boards on a later pass. Counted in
     * tx_wait_count, never in stage_refused_count - a wait, not a drop. */
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_SEAT_BUSY = 8u,
    /* The pending value was ready but the fixed T_drain cadence had not elapsed yet: the
     * record WAITS in the latest-value table and is taken on a later pass. */
    DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_DRAIN_PACE = 9u,
} distributed_refmem_position_transport_failure_t;

/* T2 position TX: the producer always writes the newest value for a position into a
 * latest-value table (never refused), and the transport takes AT MOST ONE record per
 * T_drain ms, only while the single-depth first-class seat is free. This is the
 * "buffer updates immediately, transmit on a fixed period" shape the RefMem/HAOFV
 * contracts require (REFMEM_SYNC_ARCHITECTURE "merge to latest before the wire"), and it
 * makes a refused offer impossible by construction. The cadence is a STOP-state install
 * parameter so it can be tuned without a re-flash; 0 means "as fast as the seat allows". */
#define DISTRIBUTED_REFMEM_POSITION_DRAIN_DEFAULT_MS 6u
#define DISTRIBUTED_REFMEM_POSITION_DRAIN_MAX_MS 1000u
/* Depth of the latest-value TX table: one entry per position the T2 runtime keeps in
 * flight (the same number as TRIGGER_T2_RUNTIME_POSITION_COUNT). It is an own symbol so
 * this module needs no trigger-runtime header; a wider window must widen both. */
#define DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS 3u
bool distributed_refmem_position_drain_ms(uint32_t drain_ms);
void distributed_refmem_position_drain_get(uint32_t *drain_ms);
/* Why the VDC feedback path last refused to let a frame out (a refusal stalls the WHOLE
 * flight publish path - no VDC frame and no T2 frame is built). `counts` receives
 * FB_PREPARE_* buckets in order; returns the last reason. Diagnostic only. */
uint32_t distributed_refmem_feedback_prepare_reason(uint32_t *counts, uint32_t count);

typedef struct {
    uint32_t tx_pending, tx_active, tx_fragment_index;
    uint32_t tx_publish_count, tx_reject_count, tx_position_id;
    uint32_t tx_phase, tx_wait_reason, tx_wait_count, tx_submitted_sequence;
    /* Latest-value coalescing on the TX side: how many staged updates replaced (or were
     * outranked by) a value that had not been transmitted yet. Zero means every staged
     * value reached the wire; non-zero proves the merge, not a loss. */
    uint32_t tx_coalesced_count;
    uint32_t rx_complete_count, rx_reject_count, rx_pending_mask;
    uint32_t rx_source_slot, rx_position_id, cancel_requested;
    uint32_t rx_phase, rx_last_failure;
    /* Priority-lane receiver: accepted, repeated wire and unstaged records. */
    uint32_t lane_accept_count, lane_duplicate_count, lane_reject_count, lane_owned;
    /* Bounded receive preservation (TDMA-SEAT-03): records held for the next
     * slice after a refusal, and the drain attempts that staged / failed. */
    uint32_t lane_hold_count, lane_drain_count, lane_drain_fail_count;
    /* Held records retired because the declared retry budget was exhausted, and
     * refused records dropped because the bounded queue was full. */
    uint32_t lane_hold_expired_count, lane_hold_dropped_count;
    /* Delivery-critical control bytes that replaced a diagnostic patch. */
    uint32_t lane_ack_override_count;
    /* Diagnostic control writes refused up front because the first-class fact
     * owned the shared control bytes for that image. */
    uint32_t lane_diag_refused_count;
    /* Frames the lane validated whose first-class slice did not decode: an
     * unrecoverable first-class loss, kept apart from backpressure refusals. */
    uint32_t lane_decode_reject_count;
    /* Queued copies retired because the same record reached the consumer by the
     * wire first: no data loss, but the queue exit that closes
     * lane_hold_count == lane_drain_count + lane_hold_expired_count +
     * lane_hold_delivered_count + still queued. */
    uint32_t lane_hold_delivered_count;
    /* FREEZE delivery evidence (it bypasses the PREVIEW queue): latched by the lane, kept
     * first (a second FREEZE for an id not yet taken is not stored), taken by T2. */
    uint32_t lane_freeze_latched_count, lane_freeze_kept_count, lane_freeze_taken_count;
} distributed_refmem_position_transport_snapshot_t;

/* Bounded receive preservation for the first-class seat (TDMA-SEAT-03). These
 * are the DECLARED budgets that the queue counters above are evidence against,
 * so they belong in the public header: the seat's SCPI page reports them, and a
 * capture has to be readable without the firmware source. The receiver
 * translation unit keeps a guarded fallback for host harnesses that compile it
 * without this header; tests assert both agree. */
#ifndef DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH
#define DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH 4u
#endif
#ifndef DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS
#define DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS 4u
#endif
bool distributed_refmem_get_position_transport(
    distributed_refmem_position_transport_snapshot_t *snapshot);

/* Why a first-class FREEZE has not completed: `stale_mask` holds the target
 * slots whose report does not carry this run id (another run, or a board that
 * never reported), `incomplete_mask` the position bits no reporting board has
 * frozen yet. Both are in target_mask coordinates; a call with both zero means
 * every target has reported and nothing is missing. The origin's resend count
 * alone cannot tell these apart, which is exactly how a resend storm is
 * diagnosed. */
void distributed_refmem_position_ack_pending(
    uint32_t run_id, uint32_t target_mask, uint32_t *stale_mask,
    uint32_t *incomplete_mask);

/* Core1 T2 phase: retry one held first-class record through the source's
 * receive mailbox. Called by distributed_refmem_get_position_descriptor() when
 * nothing is staged, so a refused PREVIEW is delivered on the next slice
 * instead of waiting for an origin wire repeat (TDMA-SEAT-03). Returns true
 * when the record was staged; a false return keeps it queued. The queue depth is
 * DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH and the per-entry retry budget is
 * DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS (both in
 * distributed_refmem_position_ack.inc); a record that exhausts the budget is
 * retired and counted, and a full queue drops the newest arrival explicitly. */
bool distributed_refmem_position_priority_drain(uint32_t source_slot);

/* Typed transport history. Core0 RefMem publishes; readers take bounded
 * copies. record[0] is authoritative; reception is never an apply ACK. */
typedef struct {
    uint32_t schema, active, retained, source_slot;
    uint32_t ring_config_seq, role_generation, schedule_crc32;
    uint32_t clock_epoch_id, clock_run_id;
    uint32_t receive_count, reject_count, duplicate_count, timeout_count;
    uint32_t last_transport_seq, first_rx_ms, last_rx_ms;
    uint8_t record[64];
    /* Decoded by the sole Core0 publisher. Core1 never parses wire bytes. */
    union {
        refmem_sync_vdc_feedback_record_t sample;
        refmem_sync_vdc_boundary_command_t command;
        /* Internal schema 2 is the REFERENCE MASTER arm. The most recent
         * successful full FIFO publication for this follower; record[] is
         * its received receipt ACK. Never interpret this arm as sample. */
        uint8_t reference_proof[64];
    };
} distributed_refmem_vdc_feedback_rx_snapshot_t;

/* Ephemeral compact Core1 input, not another retained buffer. All metadata
 * and decoded command are copied under the source's single publication guard.
 * Caller rechecks current owner admission and cancellation before applying. */
typedef struct {
    refmem_sync_vdc_boundary_command_t command;
    uint32_t active, ring_config_seq, role_generation, schedule_crc32;
    uint32_t clock_epoch_id, clock_run_id, first_rx_ms, last_rx_ms;
} distributed_refmem_vdc_boundary_view_t;

bool distributed_refmem_copy_vdc_boundary_command(
    uint32_t source, distributed_refmem_vdc_boundary_view_t *view);

typedef struct {
    uint32_t schema, active, fragment_index, fragments_published;
    uint32_t groups_published, cancel_count, reject_count, history_count;
    uint32_t ring_config_seq, role_generation, source_slot, target_slot;
    uint32_t schedule_crc32, clock_epoch_id, clock_run_id;
    uint8_t history[2][64]; /* Most recently completed FIFO publications first. */
    uint32_t control_session; /* Frozen with the active fragment group. */
} distributed_refmem_vdc_feedback_tx_snapshot_t;

bool distributed_refmem_get_vdc_feedback_rx(uint32_t source_slot,
    distributed_refmem_vdc_feedback_rx_snapshot_t *snapshot);
/* Same scalar layout as RX, but retained/active describe reference_proof and
 * record[] returns that original MODEL wire. One coherent guarded copy;
 * STOP preserves the proof as inactive history. Counters describe receipts. */
bool distributed_refmem_get_vdc_reference_proof(uint32_t follower_slot,
    distributed_refmem_vdc_feedback_rx_snapshot_t *snapshot);
/* One atomic SRAM copy; caller must validate local current ring/role/clock
 * binding. Intended for the Core1 owner without a full runtime snapshot. */
bool distributed_refmem_copy_vdc_feedback_rx(uint32_t source_slot,
    distributed_refmem_vdc_feedback_rx_snapshot_t *snapshot);
bool distributed_refmem_get_vdc_feedback_tx(
    distributed_refmem_vdc_feedback_tx_snapshot_t *snapshot);

/* Read-only stable copies of the core1-owned VDC/DPLL vector regions.  The
 * aliases intentionally expose the fixed wire payload without exposing the
 * mutable region/seqlock storage itself. */
typedef refmem_vdc_vector_payload_t distributed_refmem_vdc_vector_snapshot_t;
typedef refmem_dpll_vector_payload_t distributed_refmem_dpll_vector_snapshot_t;

bool distributed_refmem_init(void);
void distributed_refmem_service(void);
void distributed_refmem_realtime_run_once(void);
/* Core1 TDMA-owner boundary for a frozen MASTER command batch.  RefMem's
 * own realtime phase may capture a batch, but it must never enqueue work
 * into the shared TDMA scheduler. */
void distributed_refmem_tdma_publish_service(void);
void distributed_refmem_get_status(distributed_refmem_status_t *status);
bool distributed_refmem_get_node(uint32_t node_id, distributed_refmem_node_snapshot_t *snapshot);
void distributed_refmem_get_core_vector(distributed_refmem_core_vector_snapshot_t *snapshot);
void distributed_refmem_get_runtime_protection(distributed_refmem_runtime_protection_snapshot_t *snapshot);
bool distributed_refmem_get_realtime_tdma(refmem_realtime_tdma_snapshot_t *snapshot);
void distributed_refmem_get_vdc_follower_rx(
    distributed_refmem_vdc_follower_rx_snapshot_t *snapshot);
bool distributed_refmem_get_vdc_vector_snapshot(
    distributed_refmem_vdc_vector_snapshot_t *snapshot);
/* One bounded atomic read of the asynchronous Core0 diagnostic mirror.
 * Checksum/schema checked; failure preserves output. FRESH and age describe
 * the last source sample, not current freshness, control authority or lock. */
bool distributed_refmem_get_vdc_priority_snapshot(refmem_vdc_priority_payload_t *out);
bool distributed_refmem_get_dpll_vector_snapshot(
    distributed_refmem_dpll_vector_snapshot_t *snapshot);
bool distributed_refmem_get_realtime_tdma_frame(uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size);
bool distributed_refmem_submit_realtime_tdma_tx(
    const refmem_realtime_tdma_intent_config_t *config);
bool distributed_refmem_submit_realtime_tdma_rx(
    const refmem_realtime_tdma_intent_config_t *config);
void distributed_refmem_abort_realtime_tdma(void);
bool distributed_refmem_quality_gate_ready(void);
bool distributed_refmem_command_set_reason_table_crc32(uint32_t reason_table_crc32);
bool distributed_refmem_command_try_post(const refmem_command_request_t *request,
                                         uint32_t issue_tick32);
bool distributed_refmem_command_ack(uint32_t target_node,
                                    uint32_t evidence_index);
bool distributed_refmem_command_nack(uint32_t target_node,
                                     refmem_command_reason_t reason,
                                     uint32_t evidence_index);
bool distributed_refmem_command_mark_timeout(uint32_t now_tick32,
                                             uint32_t evidence_index);
bool distributed_refmem_command_clear(uint32_t clear_seq);
bool distributed_refmem_get_command_snapshot(refmem_command_snapshot_t *snapshot);
bool distributed_refmem_register_node_load_owner(
    uint32_t instance_id,
    distributed_refmem_node_load_owner_t owner,
    void *context);
bool distributed_refmem_can_accept_node_load_intent(uint32_t realtime_idle);
bool distributed_refmem_stage_node_load(uint32_t node_id,
                                        uint32_t instance_id,
                                        uint32_t role_mask,
                                        uint32_t persona_mask,
                                        uint32_t enabled,
                                        uint32_t required,
                                        uint32_t load_order);
bool distributed_refmem_stage_sd_system_pack(const char *path,
                                             uint32_t path_hash,
                                             uint32_t manifest_status,
                                             uint32_t manifest_schema,
                                             uint32_t manifest_required_count,
                                             uint32_t manifest_missing_count,
                                             const char *manifest_build_id,
                                             uint32_t package_crc32,
                                             uint32_t package_valid,
                                             uint32_t package_error,
                                             const uint8_t *package_data,
                                             size_t package_size,
                                             const uint32_t *table_crc32,
                                             uint32_t table_crc32_count,
                                             uint32_t owner_validated_table_mask,
                                             uint32_t first_bad_table);
bool distributed_refmem_activate_staging(uint32_t realtime_idle);
bool distributed_refmem_stage_board_capability(uint32_t board_id,
                                               uint32_t board_uuid_crc32,
                                               uint32_t capability_mask,
                                               uint32_t io_constraint_mask,
                                               uint32_t ip_core_mask,
                                               uint32_t default_persona_mask,
                                               uint32_t hw_profile_crc32,
                                               uint32_t active_default_slot,
                                               uint32_t online_required);
bool distributed_refmem_stage_model_turntable_load(uint32_t slot_id,
                                                   uint32_t output_index);
bool distributed_refmem_build_node_load_sync_frame(uint32_t instance_id,
                                                   uint8_t source_slot,
                                                   uint8_t target_mask,
                                                   uint32_t epoch_id,
                                                   uint32_t run_id,
                                                   uint32_t seq32,
                                                   uint32_t compact_time,
                                                   uint8_t *frame,
                                                   size_t frame_capacity,
                                                   size_t *frame_size);
bool distributed_refmem_apply_node_load_sync_payload(const uint8_t *payload,
                                                     uint16_t payload_size);
bool distributed_refmem_configure_node_load_auto_sync(
    uint32_t enabled,
    uint32_t local_slot,
    uint32_t target_mask,
    uint32_t baud_hz,
    uint32_t deadline_us,
    uint32_t uplink_duplex_mode,
    const refmem_spi_physical_pin_config_t *uplink_adapter_pins,
    uint32_t downlink_duplex_mode,
    const refmem_spi_physical_pin_config_t *downlink_adapter_pins);
void distributed_refmem_get_node_load_auto_sync(
    distributed_refmem_node_load_auto_sync_snapshot_t *snapshot);
void distributed_refmem_get_tdma_flight_sync(
    distributed_refmem_tdma_flight_sync_snapshot_t *snapshot);
bool distributed_refmem_get_tdma_flight_sync_peer(
    uint32_t source_slot,
    refmem_sync_peer_state_t *snapshot);
bool distributed_refmem_get_tdma_flight_sync_mirror(
    uint32_t source_slot,
    refmem_sync_mirror_snapshot_t *snapshot);
bool distributed_refmem_get_vdc_follower_command(
    uint32_t source_slot,
    uint32_t expected_consumer_generation,
    uint32_t expected_ring_config_seq,
    refmem_sync_vdc_command_snapshot_t *snapshot);
void distributed_refmem_get_tdma_flight_sync_quality(
    refmem_sync_quality_counters_t *snapshot);

/* TDMA ring role maintenance (P0.5-3): re-derive the VDC schedule and the TDMA
 * foundation profile ring for a new local slot, then re-apply the profile so
 * the ring runtime restarts the adapter with the matching REFERENCE/FORWARD
 * role. The same firmware can therefore run as reference (local==reference)
 * or forward node without a separate build. */
bool distributed_refmem_set_tdma_ring_local_slot(uint32_t local_slot_id);
bool distributed_refmem_set_tdma_ring_topology(uint32_t local_slot_id,
                                               uint32_t reference_slot_id,
                                               uint32_t node_count);
bool distributed_refmem_tdma_ring_arm(void);
distributed_refmem_tdma_arm_result_t
distributed_refmem_tdma_ring_arm_last_result(void);
bool distributed_refmem_tdma_ring_train(uint32_t cycles);
bool distributed_refmem_tdma_ring_start(void);
bool distributed_refmem_tdma_ring_stop(void);
bool distributed_refmem_set_tdma_ring_log_enabled(bool enabled);
bool distributed_refmem_get_tdma_ring_log_enabled(void);

#endif
