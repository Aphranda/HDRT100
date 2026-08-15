#include "refmem_command.h"

#include <string.h>

static uint32_t refmem_command_load(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void refmem_command_store_guard(volatile uint32_t *guard)
{
    (void)__atomic_add_fetch(guard, 1u, __ATOMIC_RELEASE);
}

static void refmem_command_begin_write(refmem_command_slot_t *slot)
{
    refmem_command_store_guard(&slot->guard);
}

static void refmem_command_end_write(refmem_command_slot_t *slot)
{
    refmem_command_store_guard(&slot->guard);
}

static uint32_t refmem_command_node_bit(uint32_t node)
{
    return node < REFMEM_COMMAND_NODE_COUNT ? (1u << node) : 0u;
}

static uint32_t refmem_command_all_terminal_flags(const refmem_command_slot_t *slot)
{
    return slot->ack_flags | slot->nack_flags | slot->timeout_flags;
}

static bool refmem_command_is_active(const refmem_command_slot_t *slot)
{
    return refmem_command_load(&slot->command_seq) != 0u;
}

static uint32_t refmem_command_derive_state(uint32_t command_seq,
                                            uint32_t required_mask,
                                            uint32_t taken_flags,
                                            uint32_t ack_flags,
                                            uint32_t nack_flags,
                                            uint32_t busy_flags,
                                            uint32_t timeout_flags)
{
    if (command_seq == 0u) {
        return REFMEM_COMMAND_STATE_IDLE;
    }

    if ((timeout_flags & required_mask) != 0u) {
        return REFMEM_COMMAND_STATE_TIMED_OUT;
    }
    if ((nack_flags & required_mask) != 0u) {
        return REFMEM_COMMAND_STATE_NACKED;
    }
    if (required_mask != 0u && (ack_flags & required_mask) == required_mask) {
        return REFMEM_COMMAND_STATE_ACKED;
    }
    if (busy_flags != 0u) {
        return REFMEM_COMMAND_STATE_BUSY;
    }
    if (taken_flags != 0u) {
        return REFMEM_COMMAND_STATE_TAKEN;
    }
    return REFMEM_COMMAND_STATE_POSTED;
}

static bool refmem_command_request_is_valid(const refmem_command_request_t *request)
{
    const uint32_t valid_mask = (1u << REFMEM_COMMAND_NODE_COUNT) - 1u;
    if (request == NULL ||
        request->command_seq == 0u ||
        request->source_node >= REFMEM_COMMAND_NODE_COUNT ||
        request->target_mask == 0u ||
        (request->target_mask & ~valid_mask) != 0u ||
        (request->required_mask & ~request->target_mask) != 0u ||
        request->command_type == REFMEM_COMMAND_TYPE_NONE) {
        return false;
    }
    return true;
}

static bool refmem_command_is_complete_snapshot(const refmem_command_snapshot_t *snapshot)
{
    if (snapshot == NULL || snapshot->command_seq == 0u || snapshot->required_mask == 0u) {
        return false;
    }
    if ((snapshot->nack_flags & snapshot->required_mask) != 0u ||
        (snapshot->timeout_flags & snapshot->required_mask) != 0u) {
        return true;
    }
    return (snapshot->ack_flags & snapshot->required_mask) == snapshot->required_mask;
}

bool refmem_command_init(refmem_command_slot_t *slot, uint32_t reason_table_crc32)
{
    if (slot == NULL) {
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    slot->reason_table_crc32 = reason_table_crc32;
    slot->last_reason_slot = REFMEM_COMMAND_INVALID_NODE;
    return true;
}

bool refmem_command_set_reason_table_crc32(refmem_command_slot_t *slot,
                                           uint32_t reason_table_crc32)
{
    if (slot == NULL) {
        return false;
    }

    refmem_command_begin_write(slot);
    slot->reason_table_crc32 = reason_table_crc32;
    refmem_command_end_write(slot);
    return true;
}

bool refmem_command_try_post(refmem_command_slot_t *slot,
                             const refmem_command_request_t *request,
                             uint32_t issue_tick32)
{
    if (slot == NULL || !refmem_command_request_is_valid(request)) {
        return false;
    }
    if (refmem_command_is_active(slot)) {
        return false;
    }

    refmem_command_begin_write(slot);
    slot->command_seq = request->command_seq;
    slot->source_node = request->source_node;
    slot->source_instance = request->source_instance;
    slot->target_mask = request->target_mask;
    slot->required_mask = request->required_mask;
    slot->command_type = request->command_type;
    slot->command_class = request->command_class;
    slot->payload_kind = request->payload_kind;
    slot->payload_ref = request->payload_ref;
    slot->payload_size = request->payload_size;
    slot->payload_crc32 = request->payload_crc32;
    slot->issue_epoch = request->issue_epoch;
    slot->run_id = request->run_id;
    slot->issue_tick32 = issue_tick32;
    slot->timeout_1e3ns = request->timeout_1e3ns;
    slot->taken_flags = 0u;
    slot->ack_flags = 0u;
    slot->nack_flags = 0u;
    slot->busy_flags = 0u;
    slot->timeout_flags = 0u;
    slot->last_reason = REFMEM_COMMAND_REASON_NONE;
    slot->last_reason_slot = REFMEM_COMMAND_INVALID_NODE;
    slot->evidence_index = 0u;
    slot->clear_seq = 0u;
    refmem_command_end_write(slot);
    return true;
}

static void refmem_command_set_nack(refmem_command_slot_t *slot,
                                    uint32_t bit,
                                    refmem_command_reason_t reason,
                                    uint32_t target_node,
                                    uint32_t evidence_index)
{
    slot->taken_flags |= bit;
    slot->busy_flags &= ~bit;
    slot->ack_flags &= ~bit;
    slot->timeout_flags &= ~bit;
    slot->nack_flags |= bit;
    slot->last_reason = (uint32_t)reason;
    slot->last_reason_slot = target_node;
    slot->evidence_index = evidence_index;
}

refmem_command_take_result_t refmem_command_try_take(refmem_command_slot_t *slot,
                                                     uint32_t target_node,
                                                     uint32_t active_epoch,
                                                     uint32_t active_run_id,
                                                     uint32_t observed_payload_crc32,
                                                     uint32_t evidence_index)
{
    if (slot == NULL || !refmem_command_is_active(slot)) {
        return REFMEM_COMMAND_TAKE_NO_COMMAND;
    }

    const uint32_t bit = refmem_command_node_bit(target_node);
    if (bit == 0u || (slot->target_mask & bit) == 0u) {
        return REFMEM_COMMAND_TAKE_NOT_TARGET;
    }
    if ((refmem_command_all_terminal_flags(slot) & bit) != 0u) {
        return REFMEM_COMMAND_TAKE_ALREADY_COMPLETE;
    }

    refmem_command_begin_write(slot);
    if (slot->issue_epoch != active_epoch || slot->run_id != active_run_id) {
        refmem_command_set_nack(slot,
                                bit,
                                REFMEM_COMMAND_REASON_EPOCH_MISMATCH,
                                target_node,
                                evidence_index);
        refmem_command_end_write(slot);
        return REFMEM_COMMAND_TAKE_EPOCH_MISMATCH;
    }
    if (slot->payload_crc32 != observed_payload_crc32) {
        refmem_command_set_nack(slot,
                                bit,
                                REFMEM_COMMAND_REASON_PAYLOAD_CRC_MISMATCH,
                                target_node,
                                evidence_index);
        refmem_command_end_write(slot);
        return REFMEM_COMMAND_TAKE_PAYLOAD_CRC_MISMATCH;
    }

    slot->taken_flags |= bit;
    slot->busy_flags |= bit;
    refmem_command_end_write(slot);
    return REFMEM_COMMAND_TAKE_TAKEN;
}

bool refmem_command_ack(refmem_command_slot_t *slot,
                        uint32_t target_node,
                        uint32_t evidence_index)
{
    if (slot == NULL || !refmem_command_is_active(slot)) {
        return false;
    }

    const uint32_t bit = refmem_command_node_bit(target_node);
    if (bit == 0u || (slot->target_mask & bit) == 0u ||
        (refmem_command_all_terminal_flags(slot) & bit) != 0u) {
        return false;
    }

    refmem_command_begin_write(slot);
    slot->taken_flags |= bit;
    slot->busy_flags &= ~bit;
    slot->nack_flags &= ~bit;
    slot->timeout_flags &= ~bit;
    slot->ack_flags |= bit;
    slot->last_reason = REFMEM_COMMAND_REASON_NONE;
    slot->last_reason_slot = target_node;
    slot->evidence_index = evidence_index;
    refmem_command_end_write(slot);
    return true;
}

bool refmem_command_nack(refmem_command_slot_t *slot,
                         uint32_t target_node,
                         refmem_command_reason_t reason,
                         uint32_t evidence_index)
{
    if (slot == NULL || !refmem_command_is_active(slot) ||
        reason == REFMEM_COMMAND_REASON_NONE) {
        return false;
    }

    const uint32_t bit = refmem_command_node_bit(target_node);
    if (bit == 0u || (slot->target_mask & bit) == 0u ||
        (refmem_command_all_terminal_flags(slot) & bit) != 0u) {
        return false;
    }

    refmem_command_begin_write(slot);
    refmem_command_set_nack(slot, bit, reason, target_node, evidence_index);
    refmem_command_end_write(slot);
    return true;
}

bool refmem_command_mark_timeout(refmem_command_slot_t *slot,
                                 uint32_t now_tick32,
                                 uint32_t evidence_index)
{
    if (slot == NULL || !refmem_command_is_active(slot) || slot->timeout_1e3ns == 0u) {
        return false;
    }

    const uint32_t elapsed = now_tick32 - slot->issue_tick32;
    if (elapsed < slot->timeout_1e3ns) {
        return false;
    }

    const uint32_t pending =
        slot->required_mask & ~(slot->ack_flags | slot->nack_flags | slot->timeout_flags);
    if (pending == 0u) {
        return false;
    }

    refmem_command_begin_write(slot);
    slot->busy_flags &= ~pending;
    slot->timeout_flags |= pending;
    slot->last_reason = REFMEM_COMMAND_REASON_TIMEOUT;
    slot->last_reason_slot = REFMEM_COMMAND_INVALID_NODE;
    slot->evidence_index = evidence_index;
    refmem_command_end_write(slot);
    return true;
}

bool refmem_command_clear(refmem_command_slot_t *slot, uint32_t clear_seq)
{
    refmem_command_snapshot_t snapshot;
    const uint32_t reason_table_crc32 =
        slot != NULL ? refmem_command_load(&slot->reason_table_crc32) : 0u;
    const uint32_t last_completed_seq =
        slot != NULL ? refmem_command_load(&slot->last_completed_seq) : 0u;

    if (!refmem_command_get_snapshot(slot, &snapshot) ||
        snapshot.command_seq == 0u ||
        clear_seq != snapshot.command_seq ||
        !refmem_command_is_complete_snapshot(&snapshot)) {
        return false;
    }

    refmem_command_begin_write(slot);
    slot->command_seq = 0u;
    slot->source_node = 0u;
    slot->source_instance = 0u;
    slot->target_mask = 0u;
    slot->required_mask = 0u;
    slot->command_type = 0u;
    slot->command_class = 0u;
    slot->payload_kind = 0u;
    slot->payload_ref = 0u;
    slot->payload_size = 0u;
    slot->payload_crc32 = 0u;
    slot->issue_epoch = 0u;
    slot->run_id = 0u;
    slot->issue_tick32 = 0u;
    slot->timeout_1e3ns = 0u;
    slot->taken_flags = 0u;
    slot->ack_flags = 0u;
    slot->nack_flags = 0u;
    slot->busy_flags = 0u;
    slot->timeout_flags = 0u;
    slot->last_reason = REFMEM_COMMAND_REASON_NONE;
    slot->evidence_index = 0u;
    slot->reason_table_crc32 = reason_table_crc32;
    slot->clear_seq = clear_seq;
    slot->last_completed_seq = snapshot.command_seq > last_completed_seq
        ? snapshot.command_seq
        : last_completed_seq;
    slot->last_reason_slot = REFMEM_COMMAND_INVALID_NODE;
    refmem_command_end_write(slot);
    return true;
}

bool refmem_command_get_snapshot(const refmem_command_slot_t *slot,
                                 refmem_command_snapshot_t *snapshot)
{
    if (slot == NULL || snapshot == NULL) {
        return false;
    }

    while (true) {
        const uint32_t seq_begin = refmem_command_load(&slot->guard);
        if ((seq_begin & 1u) != 0u) {
            continue;
        }
        snapshot->command_seq = slot->command_seq;
        snapshot->source_node = slot->source_node;
        snapshot->source_instance = slot->source_instance;
        snapshot->target_mask = slot->target_mask;
        snapshot->required_mask = slot->required_mask;
        snapshot->command_type = slot->command_type;
        snapshot->command_class = slot->command_class;
        snapshot->payload_kind = slot->payload_kind;
        snapshot->payload_ref = slot->payload_ref;
        snapshot->payload_size = slot->payload_size;
        snapshot->payload_crc32 = slot->payload_crc32;
        snapshot->issue_epoch = slot->issue_epoch;
        snapshot->run_id = slot->run_id;
        snapshot->issue_tick32 = slot->issue_tick32;
        snapshot->timeout_1e3ns = slot->timeout_1e3ns;
        snapshot->taken_flags = slot->taken_flags;
        snapshot->ack_flags = slot->ack_flags;
        snapshot->nack_flags = slot->nack_flags;
        snapshot->busy_flags = slot->busy_flags;
        snapshot->timeout_flags = slot->timeout_flags;
        snapshot->last_reason = slot->last_reason;
        snapshot->last_reason_slot = slot->last_reason_slot;
        snapshot->reason_table_crc32 = slot->reason_table_crc32;
        snapshot->evidence_index = slot->evidence_index;
        snapshot->clear_seq = slot->clear_seq;
        snapshot->last_completed_seq = slot->last_completed_seq;
        const uint32_t seq_end = refmem_command_load(&slot->guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            break;
        }
    }

    snapshot->state = refmem_command_derive_state(snapshot->command_seq,
                                                  snapshot->required_mask,
                                                  snapshot->taken_flags,
                                                  snapshot->ack_flags,
                                                  snapshot->nack_flags,
                                                  snapshot->busy_flags,
                                                  snapshot->timeout_flags);
    return true;
}

bool refmem_command_to_sync_command_payload(const refmem_command_snapshot_t *snapshot,
                                            refmem_sync_command_payload_t *payload)
{
    if (snapshot == NULL || payload == NULL || snapshot->command_seq == 0u) {
        return false;
    }

    payload->command_seq = snapshot->command_seq;
    payload->command_type = snapshot->command_type;
    payload->command_class = snapshot->command_class;
    payload->source_instance = snapshot->source_instance;
    payload->target_mask = snapshot->target_mask;
    payload->required_mask = snapshot->required_mask;
    payload->payload_kind = snapshot->payload_kind;
    payload->payload_ref = snapshot->payload_ref;
    payload->payload_size = snapshot->payload_size;
    payload->payload_crc32 = snapshot->payload_crc32;
    payload->timeout_1e3ns = snapshot->timeout_1e3ns;
    return true;
}

bool refmem_command_to_sync_ack_payload(const refmem_command_snapshot_t *snapshot,
                                        refmem_sync_ack_nack_payload_t *payload)
{
    if (snapshot == NULL || payload == NULL || snapshot->command_seq == 0u) {
        return false;
    }

    payload->command_seq = snapshot->command_seq;
    payload->delta_seq32 = 0u;
    payload->taken_flags = snapshot->taken_flags;
    payload->ack_flags = snapshot->ack_flags;
    payload->nack_flags = snapshot->nack_flags;
    payload->busy_flags = snapshot->busy_flags;
    payload->timeout_flags = snapshot->timeout_flags;
    payload->last_reason = snapshot->last_reason;
    payload->last_reason_slot = snapshot->last_reason_slot;
    payload->evidence_index = snapshot->evidence_index;
    return true;
}
