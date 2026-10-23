#include "refmem_node_control.h"

#include <string.h>

#define REFMEM_NODE_CONTROL_CRC_SEED 2166136261u

static uint32_t refmem_node_control_crc(
    const refmem_command_snapshot_t *snapshot,
    uint32_t seq)
{
    uint32_t crc = REFMEM_NODE_CONTROL_CRC_SEED;
    const uint8_t *bytes = (const uint8_t *)snapshot;
    for (size_t i = 0u; i < sizeof(*snapshot); i++) {
        crc ^= bytes[i];
        crc *= 16777619u;
    }
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        crc ^= (uint8_t)(seq >> shift);
        crc *= 16777619u;
    }
    return crc;
}

bool refmem_node_control_write(refmem_command_snapshot_t *command,
                               uint32_t *seq,
                               uint32_t *crc32,
                               const refmem_command_snapshot_t *snapshot,
                               uint32_t new_seq)
{
    if (command == NULL || seq == NULL || crc32 == NULL ||
        snapshot == NULL || new_seq == 0u) {
        return false;
    }
    memcpy(command, snapshot, sizeof(*command));
    *seq = new_seq;
    *crc32 = refmem_node_control_crc(command, new_seq);
    return true;
}

bool refmem_node_control_read(const refmem_command_snapshot_t *command,
                              uint32_t seq,
                              uint32_t crc32,
                              refmem_command_snapshot_t *snapshot)
{
    if (command == NULL || snapshot == NULL || seq == 0u) {
        return false;
    }
    if (crc32 != refmem_node_control_crc(command, seq)) {
        return false;
    }
    memcpy(snapshot, command, sizeof(*snapshot));
    return true;
}

bool refmem_node_control_ack(refmem_command_snapshot_t *command,
                             uint32_t *seq,
                             uint32_t *crc32,
                             uint32_t target_slot,
                             uint32_t evidence_index)
{
    if (command == NULL || seq == NULL || crc32 == NULL ||
        target_slot >= 32u) {
        return false;
    }
    refmem_command_snapshot_t snapshot;
    if (!refmem_node_control_read(command, *seq, *crc32, &snapshot)) {
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
    return refmem_node_control_write(command, seq, crc32, &snapshot,
                                     *seq + 1u);
}

bool refmem_node_control_clear(refmem_command_snapshot_t *command,
                               uint32_t *seq,
                               uint32_t *crc32)
{
    if (command == NULL || seq == NULL || crc32 == NULL) {
        return false;
    }
    memset(command, 0, sizeof(*command));
    *seq = 0u;
    *crc32 = 0u;
    return true;
}
