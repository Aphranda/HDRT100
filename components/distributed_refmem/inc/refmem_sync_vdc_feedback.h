#ifndef REFMEM_SYNC_VDC_FEEDBACK_H
#define REFMEM_SYNC_VDC_FEEDBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure feedback transport. Neither schema grants controller or GPIO
 * consumption eligibility; model schema identifies a Core1 committed model. */
#define REFMEM_VDC_FEEDBACK_SCHEMA 1u
#define REFMEM_VDC_FEEDBACK_DOMAIN_FLAGS 0x07u
#define REFMEM_VDC_FEEDBACK_MODEL_SCHEMA 2u
#define REFMEM_VDC_FEEDBACK_MODEL_DOMAIN_FLAGS 0x0fu
#define REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA 3u
#define REFMEM_VDC_BOUNDARY_COMMAND_FLAGS 0x01u
/* Total complete groups for one immutable offer, including the first send.
 * Repetition never refreshes its command sequence, measurement basis or TTL. */
#define REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS 3u
#define REFMEM_VDC_FEEDBACK_RECORD_SIZE 64u
#define REFMEM_VDC_FEEDBACK_CRC_OFFSET 60u
#define REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE 4u
#define REFMEM_VDC_FEEDBACK_FRAGMENT_COUNT 16u
#define REFMEM_VDC_FEEDBACK_ASSEMBLY_TIMEOUT_MS 1000u
#define REFMEM_VDC_FEEDBACK_MAX_NODES 8u

/* Decoded fields; this is NOT the wire layout. The encoder stores explicit
 * little-endian fields at the reviewed offsets and derives a checked u32
 * width from before/after for schema 1. Schema 2 carries a model projection
 * interval and provenance. Clock epoch/run are SOURCE-local identifiers. */
typedef struct {
    uint64_t source_arm_epoch;
    union {
        struct {
            uint64_t rx_elapsed_cycles;
            uint64_t tx_elapsed_cycles;
            uint64_t timer1_enable_before;
            uint64_t timer1_enable_after;
        };
        struct {
            uint64_t output_ns_lo;
            uint64_t output_ns_hi;
            uint32_t model_token;
            uint32_t applied_command_seq;
            uint32_t control_session;
            uint32_t reserved; /* Must be zero; never serialized. */
        } model;
    };
    uint32_t source_clock_epoch_id;
    uint32_t source_clock_run_id;
    uint32_t observer_epoch;
    uint32_t measurement_sequence;
    uint32_t tick_hz;
    uint8_t schema_version;
    uint8_t source_slot;
    uint8_t target_slot;
    uint8_t domain_flags;
} refmem_sync_vdc_feedback_record_t;

/* Decoded schema3, NOT a wire struct. Clock/ARM/observer/model identities are
 * target-local. reserved is zero and never serialized. The header location
 * matches feedback only to simplify typed diagnostics; no field is aliased
 * semantically. This record requests internal frequency-only application. */
typedef struct {
    uint64_t target_arm_epoch;
    uint64_t basis_source_output_ns_lo;
    uint32_t control_session;
    uint32_t command_seq;
    uint32_t schedule_crc32;
    uint32_t target_clock_epoch_id;
    uint32_t target_clock_run_id;
    uint32_t target_observer_epoch;
    uint32_t basis_measurement_sequence;
    uint32_t expected_target_model_token;
    uint32_t expected_applied_command_seq;
    int32_t signed_delta_rate_ppb;
    uint32_t reserved;
    uint8_t schema_version, source_slot, target_slot, flags;
} refmem_sync_vdc_boundary_command_t;

enum {
    REFMEM_VDC_FEEDBACK_ASSEMBLY_SEEN = 1u,
    REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE = 2u,
};

/* One independent object per source, owned by the caller. Initialize/reset
 * before first use. Reset clears transport watermarks for local binding
 * cancellation. Expiry/conflict preserve watermarks to reject old starts.
 * first_ms is never renewed by duplicate packets. last_sequence is the last
 * successfully copied fragment's full mailbox sequence, NOT measurement seq.
 * Completed raw bytes stay here until the next group; retained history and
 * its counters/guard are caller-owned, independent of assembly cancellation. */
typedef struct {
    uint8_t payload[REFMEM_VDC_FEEDBACK_RECORD_SIZE];
    uint32_t first_sequence;
    uint32_t first_ms;
    uint32_t last_sequence;
    uint8_t next_fragment;
    uint8_t source_slot;
    uint8_t target_slot;
    uint8_t state;
} refmem_sync_vdc_feedback_assembly_t;

typedef enum {
    REFMEM_VDC_FEEDBACK_PROGRESS = 0,
    REFMEM_VDC_FEEDBACK_COMPLETE,
    REFMEM_VDC_FEEDBACK_DUPLICATE,
    REFMEM_VDC_FEEDBACK_STALE,
    REFMEM_VDC_FEEDBACK_RESTARTED,
    REFMEM_VDC_FEEDBACK_EXPIRED,
    REFMEM_VDC_FEEDBACK_EXPIRED_RESTARTED,
    REFMEM_VDC_FEEDBACK_BAD_ARGUMENT,
    REFMEM_VDC_FEEDBACK_BAD_IDENTITY,
    REFMEM_VDC_FEEDBACK_BAD_SEQUENCE,
    REFMEM_VDC_FEEDBACK_BAD_FRAGMENT,
    REFMEM_VDC_FEEDBACK_CONFLICT,
    REFMEM_VDC_FEEDBACK_BAD_RECORD,
} refmem_sync_vdc_feedback_result_t;

typedef enum {
    REFMEM_VDC_FEEDBACK_ORDER_FIRST = 0,
    REFMEM_VDC_FEEDBACK_ORDER_NEW_NAMESPACE,
    REFMEM_VDC_FEEDBACK_ORDER_NEWER,
    REFMEM_VDC_FEEDBACK_ORDER_DUPLICATE,
    REFMEM_VDC_FEEDBACK_ORDER_STALE,
    REFMEM_VDC_FEEDBACK_ORDER_CONFLICT,
    REFMEM_VDC_FEEDBACK_ORDER_INVALID,
} refmem_sync_vdc_feedback_order_t;

uint32_t refmem_sync_vdc_feedback_crc32(const uint8_t *data, size_t size);
uint32_t refmem_sync_vdc_feedback_next_sequence(uint32_t sequence);

/* False preserves output. No tick-rate assumption beyond nonzero. Source
 * and target must be distinct runtime-admitted slots; epoch/run may be zero,
 * while ARM and observer epochs identify actual acquisitions and are nonzero.
 * A zero measurement sequence is allowed. Schema 2 additionally requires
 * ordered output bounds and nonzero model token/control session; a command
 * sequence of zero denotes no command yet. Reserved must be zero. */
bool refmem_sync_vdc_feedback_encode(
    const refmem_sync_vdc_feedback_record_t *record, uint32_t node_count,
    uint8_t output[REFMEM_VDC_FEEDBACK_RECORD_SIZE]);
bool refmem_sync_vdc_feedback_decode(
    const uint8_t wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE], uint32_t node_count,
    uint32_t expected_source, uint32_t expected_target,
    refmem_sync_vdc_feedback_record_t *record);

/* Explicit little-endian schema3, CRC32 at byte60. False preserves output.
 * Source and target must differ; session, command, ARM, observer and model
 * token are nonzero. command_seq > expected_applied_command_seq without wrap.
 * Signed delta limits, freshness and current owner admission belong to Core1. */
bool refmem_sync_vdc_boundary_command_encode(
    const refmem_sync_vdc_boundary_command_t *command, uint32_t node_count,
    uint8_t output[REFMEM_VDC_FEEDBACK_RECORD_SIZE]);
bool refmem_sync_vdc_boundary_command_decode(
    const uint8_t wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE], uint32_t node_count,
    uint32_t expected_source, uint32_t expected_target,
    refmem_sync_vdc_boundary_command_t *command);

void refmem_sync_vdc_feedback_reset(refmem_sync_vdc_feedback_assembly_t *assembly);
/* True exactly when an active partial group is cancelled at age >= 1000 ms.
 * Call from the Core0 service even when the RX FIFO is empty. */
bool refmem_sync_vdc_feedback_expire(
    refmem_sync_vdc_feedback_assembly_t *assembly, uint32_t now_ms);

/* Input bytes are one validated mailbox's VDC fragment; the caller verifies
 * outer CRC, segment ownership, outer target mask and local FIFO binding.
 * Source/target/node_count are explicit runtime identities. Only COMPLETE
 * writes complete_wire; all other results preserve it. A newer index zero
 * may replace an unfinished group. Old starts/identical duplicates cannot
 * discard a newer group or extend its timeout. Expiry plus a valid newer
 * start returns EXPIRED_RESTARTED; expiry otherwise returns EXPIRED. */
refmem_sync_vdc_feedback_result_t refmem_sync_vdc_feedback_push(
    refmem_sync_vdc_feedback_assembly_t *assembly,
    uint32_t source_slot, uint32_t target_slot, uint32_t node_count,
    uint32_t transport_sequence, uint8_t fragment_index, uint8_t fragment_count,
    const uint8_t data[REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE], uint32_t now_ms,
    uint8_t complete_wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE]);

/* Same bounded assembly mechanics, but accepts ONLY schema3. Calling the
 * wrong typed push for an active group cancels it without clearing sequence
 * watermarks. feedback_push never accepts a command. */
refmem_sync_vdc_feedback_result_t refmem_sync_vdc_boundary_command_push(
    refmem_sync_vdc_feedback_assembly_t *assembly,
    uint32_t source_slot, uint32_t target_slot, uint32_t node_count,
    uint32_t transport_sequence, uint8_t fragment_index, uint8_t fragment_count,
    const uint8_t data[REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE], uint32_t now_ms,
    uint8_t complete_wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE]);

/* Pure ordering against caller-retained complete bytes. NULL previous means
 * no history. Different source clock epoch/run, ARM or observer epoch is a
 * different namespace, as is a schema or model control-session change; none
 * of these changes claims admission of a fresh shared session. Within one
 * namespace measurement_sequence strictly increases WITHOUT wrap; equal seq
 * with unequal bytes is a conflict. Clock rate must stay fixed. Schema 1
 * also requires a fixed raw anchor; schema 2 instead rejects decreasing
 * model token or applied command sequence as STALE, without wrap. Model
 * projection bounds need not stay fixed. No retained state, input/control
 * eligibility or freshness is changed here. */
refmem_sync_vdc_feedback_order_t refmem_sync_vdc_feedback_compare(
    const uint8_t candidate[REFMEM_VDC_FEEDBACK_RECORD_SIZE],
    const uint8_t previous[REFMEM_VDC_FEEDBACK_RECORD_SIZE],
    uint32_t node_count, uint32_t expected_source, uint32_t expected_target);

#endif
