#ifndef REFMEM_CMD_FRAME_H
#define REFMEM_CMD_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_node_control.h"
#include "refmem_sync_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* C1 command frame: one REFMEM_CMD intent (snapshot + 64 B params) carried by
 * an existing REFMEM_SYNC_FRAME_COMMAND.  RefMem only transports/validates;
 * the receiver applies the request to its node control record. */
#define REFMEM_CMD_FRAME_MAGIC 0x52434D46u /* "RCMF" */
#define REFMEM_CMD_FRAME_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    refmem_command_snapshot_t snapshot;
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS];
} refmem_cmd_frame_payload_t;

bool refmem_cmd_frame_build(uint8_t source_slot,
                            uint8_t target_mask,
                            uint32_t epoch_id,
                            uint32_t run_id,
                            uint32_t seq32,
                            const refmem_command_snapshot_t *snapshot,
                            const uint32_t *params,
                            uint8_t *frame,
                            size_t frame_capacity,
                            size_t *frame_size);
bool refmem_cmd_frame_apply(const uint8_t *frame,
                            size_t frame_size,
                            refmem_command_snapshot_t *snapshot,
                            uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
                            uint8_t *source_slot,
                            uint8_t *target_mask);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_CMD_FRAME_H */
