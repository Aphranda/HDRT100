#include "refmem_cmd_frame.h"

#include <string.h>

bool refmem_cmd_frame_build(uint8_t source_slot,
                            uint8_t target_mask,
                            uint32_t epoch_id,
                            uint32_t run_id,
                            uint32_t seq32,
                            const refmem_command_snapshot_t *snapshot,
                            const uint32_t *params,
                            uint8_t *frame,
                            size_t frame_capacity,
                            size_t *frame_size)
{
    if (snapshot == NULL || frame == NULL || frame_size == NULL) {
        return false;
    }
    refmem_cmd_frame_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.magic = REFMEM_CMD_FRAME_MAGIC;
    payload.version = REFMEM_CMD_FRAME_VERSION;
    memcpy(&payload.snapshot, snapshot, sizeof(payload.snapshot));
    if (params != NULL) {
        memcpy(payload.params, params, sizeof(payload.params));
    }
    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(&header, REFMEM_SYNC_FRAME_COMMAND, 0u,
                                       source_slot, target_mask, epoch_id,
                                       run_id, seq32, 0u, 0u, &payload,
                                       (uint16_t)sizeof(payload))) {
        return false;
    }
    return refmem_sync_frame_encode(&header, &payload,
                                    (uint16_t)sizeof(payload), frame,
                                    frame_capacity, frame_size);
}

bool refmem_cmd_frame_apply(const uint8_t *frame,
                            size_t frame_size,
                            refmem_command_snapshot_t *snapshot,
                            uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
                            uint8_t *source_slot,
                            uint8_t *target_mask)
{
    if (frame == NULL || snapshot == NULL || params == NULL) {
        return false;
    }
    refmem_sync_frame_header_t header;
    const uint8_t *payload = NULL;
    uint16_t payload_size = 0u;
    if (refmem_sync_frame_validate(frame, frame_size, &header, &payload,
                                   &payload_size) != REFMEM_SYNC_FRAME_OK ||
        header.frame_type != (uint8_t)REFMEM_SYNC_FRAME_COMMAND ||
        payload == NULL || payload_size != (uint16_t)sizeof(refmem_cmd_frame_payload_t)) {
        return false;
    }
    refmem_cmd_frame_payload_t decoded;
    memcpy(&decoded, payload, sizeof(decoded));
    if (decoded.magic != REFMEM_CMD_FRAME_MAGIC ||
        decoded.version != REFMEM_CMD_FRAME_VERSION) {
        return false;
    }
    memcpy(snapshot, &decoded.snapshot, sizeof(*snapshot));
    memcpy(params, decoded.params, sizeof(decoded.params));
    if (source_slot != NULL) {
        *source_slot = header.source_slot;
    }
    if (target_mask != NULL) {
        *target_mask = header.target_mask;
    }
    return true;
}
