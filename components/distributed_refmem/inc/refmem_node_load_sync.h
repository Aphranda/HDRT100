#ifndef REFMEM_NODE_LOAD_SYNC_H
#define REFMEM_NODE_LOAD_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_application_model.h"
#include "refmem_sync_frame.h"

#define REFMEM_NODE_LOAD_SYNC_PAYLOAD_KIND_ENTRY 0x20u
#define REFMEM_NODE_LOAD_SYNC_FIELD_ENTRY        0x0003u
#define REFMEM_NODE_LOAD_SYNC_DIRTY_ENTRY_ALL    0x000007FFu
#define REFMEM_NODE_LOAD_SYNC_ENTRY_U32_COUNT    11u

bool refmem_node_load_sync_build_delta_payload(
    const refmem_node_load_entry_t *entry,
    uint32_t slot_seq,
    uint16_t delta_id,
    uint8_t *payload,
    size_t payload_capacity,
    uint16_t *payload_size);

bool refmem_node_load_sync_decode_delta_payload(
    const uint8_t *payload,
    uint16_t payload_size,
    refmem_sync_delta_header_t *delta,
    refmem_node_load_entry_t *entry);

bool refmem_node_load_sync_build_delta_frame(
    const refmem_node_load_entry_t *entry,
    uint8_t source_slot,
    uint8_t target_mask,
    uint32_t epoch_id,
    uint32_t run_id,
    uint32_t seq32,
    uint32_t slot_seq,
    uint32_t compact_time,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size);

#endif
