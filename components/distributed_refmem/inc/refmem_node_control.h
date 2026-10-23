#ifndef REFMEM_NODE_CONTROL_H
#define REFMEM_NODE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_command.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFMEM_NODE_CONTROL_PARAM_WORDS 16u

/* C0: per-board replicated control record.
 *
 * The record is the node slot's control block (command snapshot + 32 B params
 * + seq/CRC).  The write is a REFMEM write, not a second command slot; the AO
 * mirrors the single local command slot into it.  These helpers are
 * storage-only and host testable; the caller supplies the control block fields
 * so the module does not depend on the vector-table layout. */
bool refmem_node_control_write(
    refmem_command_snapshot_t *command,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32,
    const refmem_command_snapshot_t *snapshot,
    const uint32_t *new_params,
    uint32_t new_seq);
bool refmem_node_control_read(
    const refmem_command_snapshot_t *command,
    const uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t seq,
    uint32_t crc32,
    refmem_command_snapshot_t *snapshot,
    uint32_t out_params[REFMEM_NODE_CONTROL_PARAM_WORDS]);
bool refmem_node_control_ack(
    refmem_command_snapshot_t *command,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32,
    uint32_t target_slot,
    uint32_t evidence_index);
bool refmem_node_control_clear(
    refmem_command_snapshot_t *command,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_NODE_CONTROL_H */
