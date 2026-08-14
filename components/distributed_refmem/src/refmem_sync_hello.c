#include "refmem_sync_hello.h"

#include <string.h>

bool refmem_sync_hello_payload_from_board(
    const refmem_sync_hello_config_t *config,
    const refmem_board_capability_entry_t *board,
    const refmem_transport_caps_t *adapter_caps,
    refmem_sync_hello_payload_t *payload)
{
    if (config == NULL || board == NULL || adapter_caps == NULL || payload == NULL ||
        adapter_caps->max_payload_size == 0u ||
        adapter_caps->preferred_mtu < REFMEM_SYNC_FRAME_HEADER_SIZE ||
        adapter_caps->preferred_mtu >
            (uint16_t)(REFMEM_SYNC_FRAME_HEADER_SIZE + adapter_caps->max_payload_size)) {
        return false;
    }

    memset(payload, 0, sizeof(*payload));
    payload->build_id_crc32 = config->build_id_crc32;
    payload->layout_version = config->layout_version;
    payload->application_crc32 = config->application_crc32;
    payload->config_crc32 = config->config_crc32;
    payload->capability_mask = board->capability_mask;
    payload->io_constraint_mask = board->io_constraint_mask;
    payload->ip_core_mask = board->ip_core_mask;
    payload->adapter_id = adapter_caps->adapter_id;
    payload->adapter_caps = adapter_caps->capability_mask;
    payload->max_payload_size = adapter_caps->max_payload_size;
    payload->preferred_mtu = adapter_caps->preferred_mtu;
    return true;
}

bool refmem_sync_hello_encode_frame(const refmem_sync_hello_config_t *config,
                                    const refmem_sync_hello_payload_t *payload,
                                    uint8_t *frame,
                                    size_t frame_capacity,
                                    size_t *frame_size)
{
    refmem_sync_frame_header_t header;

    if (config == NULL || payload == NULL || frame == NULL || frame_size == NULL) {
        return false;
    }

    if (!refmem_sync_frame_header_init(&header,
                                       REFMEM_SYNC_FRAME_HELLO,
                                       0u,
                                       config->source_slot,
                                       config->target_mask,
                                       config->epoch_id,
                                       config->run_id,
                                       config->seq32,
                                       0u,
                                       config->compact_time,
                                       payload,
                                       sizeof(*payload))) {
        return false;
    }

    return refmem_sync_frame_encode(&header,
                                    payload,
                                    sizeof(*payload),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}
