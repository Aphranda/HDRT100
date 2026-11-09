#include "distributed_refmem_internal.h"

#include "refmem_node_control.h"

bool distributed_refmem_node_control_write_with_params(
    uint32_t node_id,
    const refmem_command_snapshot_t *snapshot,
    const uint32_t *params,
    uint32_t seq)
{
    refmem_vector_node_region_t *node = distributed_refmem_node_region(node_id);
    return node != NULL &&
           refmem_node_control_write(&node->command, node->params,
                                     &node->control_seq,
                                     &node->control_crc32, snapshot, params,
                                     seq);
}

bool distributed_refmem_node_control_read_params(
    uint32_t node_id,
    refmem_command_snapshot_t *snapshot,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32)
{
    const refmem_vector_node_region_t *node =
        distributed_refmem_node_region(node_id);
    if (node == NULL) {
        return false;
    }
    if (!refmem_node_control_read(&node->command, node->params,
                                  node->control_seq, node->control_crc32,
                                  snapshot, params)) {
        return false;
    }
    if (seq != NULL) {
        *seq = node->control_seq;
    }
    if (crc32 != NULL) {
        *crc32 = node->control_crc32;
    }
    return true;
}

bool distributed_refmem_node_control_read(uint32_t node_id,
                                          refmem_command_snapshot_t *snapshot,
                                          uint32_t *seq,
                                          uint32_t *crc32)
{
    const refmem_vector_node_region_t *node =
        distributed_refmem_node_region(node_id);
    if (node == NULL) {
        return false;
    }
    if (!refmem_node_control_read(&node->command, node->params,
                                  node->control_seq, node->control_crc32,
                                  snapshot, NULL)) {
        return false;
    }
    if (seq != NULL) {
        *seq = node->control_seq;
    }
    if (crc32 != NULL) {
        *crc32 = node->control_crc32;
    }
    return true;
}

bool distributed_refmem_node_control_ack(uint32_t node_id,
                                         uint32_t target_slot,
                                         uint32_t evidence_index)
{
    refmem_vector_node_region_t *node = distributed_refmem_node_region(node_id);
    return node != NULL &&
           refmem_node_control_ack(&node->command, node->params,
                                   &node->control_seq,
                                   &node->control_crc32, target_slot,
                                   evidence_index);
}

bool distributed_refmem_node_control_update_status(
    uint32_t node_id,
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
    refmem_vector_node_region_t *node = distributed_refmem_node_region(node_id);
    return node != NULL &&
           refmem_node_control_update_status(
               &node->command, node->params, &node->control_seq,
               &node->control_crc32, state, ack_flags, nack_flags, busy_flags,
               timeout_flags, last_reason, last_reason_slot, evidence_index,
               last_completed_seq);
}

bool distributed_refmem_node_control_clear(uint32_t node_id)
{
    refmem_vector_node_region_t *node = distributed_refmem_node_region(node_id);
    return node != NULL &&
           refmem_node_control_clear(&node->command, node->params,
                                     &node->control_seq,
                                     &node->control_crc32);
}

uint32_t distributed_refmem_node_control_sequence(uint32_t node_id)
{
    const refmem_vector_node_region_t *node =
        distributed_refmem_node_region(node_id);
    return node != NULL ? node->control_seq : 0u;
}
