#include "refmem_node_control.h"

#include <string.h>

#include "algo_checksum.h"

#define REFMEM_NODE_CONTROL_CRC_SEED 2166136261u
#define REFMEM_NODE_CONTROL_PARAM_BYTES \
    (REFMEM_NODE_CONTROL_PARAM_WORDS * sizeof(uint32_t))

static uint32_t refmem_node_control_crc(
    const refmem_command_snapshot_t *snapshot,
    const uint32_t *params,
    uint32_t seq)
{
    uint32_t crc = algo_fnv1a32_update(REFMEM_NODE_CONTROL_CRC_SEED, snapshot,
                                      sizeof(*snapshot));
    crc = algo_fnv1a32_update(crc, params, REFMEM_NODE_CONTROL_PARAM_BYTES);
    return algo_fnv1a32_u32_le(crc, seq);
}

bool refmem_node_control_write(
    refmem_command_snapshot_t *command,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32,
    const refmem_command_snapshot_t *snapshot,
    const uint32_t *new_params,
    uint32_t new_seq)
{
    if (command == NULL || params == NULL || seq == NULL || crc32 == NULL ||
        snapshot == NULL || new_seq == 0u) {
        return false;
    }
    memcpy(command, snapshot, sizeof(*command));
    if (new_params != NULL) {
        memcpy(params, new_params, REFMEM_NODE_CONTROL_PARAM_BYTES);
    } else {
        memset(params, 0, REFMEM_NODE_CONTROL_PARAM_BYTES);
    }
    *seq = new_seq;
    *crc32 = refmem_node_control_crc(command, params, new_seq);
    return true;
}

bool refmem_node_control_read(
    const refmem_command_snapshot_t *command,
    const uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t seq,
    uint32_t crc32,
    refmem_command_snapshot_t *snapshot,
    uint32_t out_params[REFMEM_NODE_CONTROL_PARAM_WORDS])
{
    if (command == NULL || params == NULL || snapshot == NULL || seq == 0u) {
        return false;
    }
    if (crc32 != refmem_node_control_crc(command, params, seq)) {
        return false;
    }
    memcpy(snapshot, command, sizeof(*snapshot));
    if (out_params != NULL) {
        memcpy(out_params, params, REFMEM_NODE_CONTROL_PARAM_BYTES);
    }
    return true;
}

bool refmem_node_control_ack(
    refmem_command_snapshot_t *command,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32,
    uint32_t target_slot,
    uint32_t evidence_index)
{
    if (command == NULL || params == NULL || seq == NULL || crc32 == NULL ||
        target_slot >= 32u) {
        return false;
    }
    refmem_command_snapshot_t snapshot;
    if (!refmem_node_control_read(command, params, *seq, *crc32, &snapshot,
                                  NULL)) {
        return false;
    }
    snapshot.ack_flags |= (1u << target_slot);
    snapshot.evidence_index = evidence_index;
    const uint32_t required = snapshot.required_mask != 0u
                                  ? snapshot.required_mask
                                  : snapshot.target_mask;
    if ((snapshot.ack_flags & required) == required) {
        snapshot.state = REFMEM_COMMAND_STATE_ACKED;
        snapshot.last_completed_seq = snapshot.command_seq;
    }
    return refmem_node_control_write(command, params, seq, crc32, &snapshot,
                                     params, *seq + 1u);
}

bool refmem_node_control_clear(
    refmem_command_snapshot_t *command,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32)
{
    if (command == NULL || params == NULL || seq == NULL || crc32 == NULL) {
        return false;
    }
    memset(command, 0, sizeof(*command));
    memset(params, 0, REFMEM_NODE_CONTROL_PARAM_BYTES);
    *seq = 0u;
    *crc32 = 0u;
    return true;
}

bool refmem_node_control_update_status(
    refmem_command_snapshot_t *command,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32,
    uint32_t state,
    uint32_t ack_flags,
    uint32_t nack_flags,
    uint32_t busy_flags,
    uint32_t timeout_flags,
    uint32_t last_reason,
    uint32_t last_reason_slot,
    uint32_t evidence_index,
    uint32_t last_completed_seq)
{
    if (command == NULL || params == NULL || seq == NULL || crc32 == NULL) {
        return false;
    }
    refmem_command_snapshot_t snapshot;
    if (!refmem_node_control_read(command, params, *seq, *crc32, &snapshot,
                                  NULL)) {
        return false;
    }
    snapshot.state = state;
    snapshot.ack_flags = ack_flags;
    snapshot.nack_flags = nack_flags;
    snapshot.busy_flags = busy_flags;
    snapshot.timeout_flags = timeout_flags;
    snapshot.last_reason = last_reason;
    snapshot.last_reason_slot = last_reason_slot;
    snapshot.evidence_index = evidence_index;
    snapshot.last_completed_seq = last_completed_seq;
    return refmem_node_control_write(command, params, seq, crc32, &snapshot,
                                     params, *seq + 1u);
}

bool refmem_node_control_apply_request(
    refmem_command_snapshot_t *command,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32,
    const refmem_command_snapshot_t *request,
    const uint32_t *request_params,
    uint32_t request_seq)
{
    if (command == NULL || params == NULL || seq == NULL || crc32 == NULL ||
        request == NULL || request_seq == 0u) {
        return false;
    }
    refmem_command_snapshot_t current;
    if (*seq != 0u &&
        refmem_node_control_read(command, params, *seq, *crc32, &current,
                                 NULL) &&
        request->command_seq <= current.command_seq &&
        request->source_node == current.source_node) {
        return false; /* stale or duplicate request from the same issuer */
    }
    refmem_command_snapshot_t next = *request;
    next.state = REFMEM_COMMAND_STATE_POSTED;
    next.ack_flags = 0u;
    next.nack_flags = 0u;
    next.busy_flags = 0u;
    next.timeout_flags = 0u;
    next.last_reason = 0u;
    next.last_reason_slot = 0u;
    next.evidence_index = 0u;
    next.last_completed_seq = 0u;
    return refmem_node_control_write(command, params, seq, crc32, &next,
                                     request_params, request_seq);
}
