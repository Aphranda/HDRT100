#ifndef REFMEM_SYNC_HELLO_H
#define REFMEM_SYNC_HELLO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_application_model.h"
#include "refmem_sync_frame.h"
#include "refmem_transport_adapter.h"

typedef struct {
    uint32_t build_id_crc32;
    uint32_t layout_version;
    uint32_t application_crc32;
    uint32_t config_crc32;
    uint8_t source_slot;
    uint8_t target_mask;
    uint32_t epoch_id;
    uint32_t run_id;
    uint32_t seq32;
    uint32_t compact_time;
} refmem_sync_hello_config_t;

bool refmem_sync_hello_payload_from_board(
    const refmem_sync_hello_config_t *config,
    const refmem_board_capability_entry_t *board,
    const refmem_transport_caps_t *adapter_caps,
    refmem_sync_hello_payload_t *payload);

bool refmem_sync_hello_encode_frame(const refmem_sync_hello_config_t *config,
                                    const refmem_sync_hello_payload_t *payload,
                                    uint8_t *frame,
                                    size_t frame_capacity,
                                    size_t *frame_size);

#endif
