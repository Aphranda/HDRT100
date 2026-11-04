#include "refmem_bootstrap_hello.h"

#include <string.h>

#include "refmem_sync_frame.h"
#include "refmem_sync_hello.h"

bool refmem_bootstrap_hello_build(uint8_t source_slot, uint8_t target_slot,
                                  uint32_t seq, uint8_t *frame,
                                  size_t frame_capacity, size_t *frame_size)
{
    if (frame == NULL || frame_size == NULL || target_slot >= 8u) {
        return false;
    }
    refmem_sync_hello_config_t config;
    refmem_sync_hello_payload_t payload;
    memset(&config, 0, sizeof(config));
    memset(&payload, 0, sizeof(payload));
    config.source_slot = source_slot;
    config.target_mask = (uint8_t)(1u << target_slot);
    config.epoch_id = 1u;
    config.run_id = 1u;
    config.seq32 = seq + 1u;
    return refmem_sync_hello_encode_frame(&config, &payload, frame,
                                          frame_capacity, frame_size);
}

bool refmem_bootstrap_hello_parse(const uint8_t *frame, size_t frame_size,
                                  uint8_t *source_slot, bool *is_ack)
{
    if (frame == NULL || source_slot == NULL || is_ack == NULL) {
        return false;
    }
    refmem_sync_frame_header_t header;
    const uint8_t *payload = NULL;
    uint16_t payload_size = 0u;
    if (refmem_sync_frame_validate(frame, frame_size, &header, &payload,
                                   &payload_size) != REFMEM_SYNC_FRAME_OK) {
        return false;
    }
    if (header.frame_type != (uint8_t)REFMEM_SYNC_FRAME_HELLO) {
        return false;
    }
    *source_slot = (uint8_t)header.source_slot;
    *is_ack = true;
    return true;
}
