#ifndef TDMA_FLIGHT_ENGINE_H
#define TDMA_FLIGHT_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_flight_fifo.h"
#include "tdma_process_image_map.h"

#define TDMA_FLIGHT_ENGINE_VERSION 4u
#define TDMA_FLIGHT_MAP_SNAPSHOT_RETRY_MAX 64u

#define TDMA_FLIGHT_SHORT_SLOT_COUNT 8u
#define TDMA_FLIGHT_SHORT_SLOT_SIZE 32u
#define TDMA_FLIGHT_NODE_IMAGE_SIZE \
    (TDMA_FLIGHT_SHORT_SLOT_COUNT * TDMA_FLIGHT_SHORT_SLOT_SIZE)
#define TDMA_FLIGHT_DPLL_OBSERVATION_SIZE 4u
#define TDMA_FLIGHT_SHORT_PAYLOAD_SIZE \
    (TDMA_FLIGHT_NODE_IMAGE_SIZE + TDMA_FLIGHT_DPLL_OBSERVATION_SIZE)
#define TDMA_FLIGHT_OUTPUT_BITMAP_WORDS \
    ((TDMA_FLIGHT_SHORT_PAYLOAD_SIZE + 31u) / 32u)
#define TDMA_FLIGHT_MAILBOX_MAGIC 0x4652u
#define TDMA_FLIGHT_MAILBOX_VERSION 2u
#define TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE 8u
#define TDMA_FLIGHT_MAILBOX_BODY_SIZE \
    (TDMA_FLIGHT_SHORT_SLOT_SIZE - TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE)
#define TDMA_FLIGHT_MAILBOX_VERSION_OFFSET 2u
#define TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET 4u
#define TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET 5u
#define TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET 6u
#define TDMA_FLIGHT_ALIGNMENT_LFSR_SEED 0x01u
#define TDMA_FLIGHT_ALIGNMENT_LFSR_MASK 0x8Eu

_Static_assert(TDMA_FLIGHT_SHORT_PAYLOAD_SIZE ==
                   TDMA_TRANSPORT_SHORT_PAYLOAD_MAX,
               "product process image must consume the fixed SHORT payload");

typedef enum {
    TDMA_FLIGHT_ENGINE_OK = 0u,
    TDMA_FLIGHT_ENGINE_BAD_ARGUMENT = 1u,
    TDMA_FLIGHT_ENGINE_LENGTH_REJECTED = 2u,
    TDMA_FLIGHT_ENGINE_TX_UNAVAILABLE = 3u,
    TDMA_FLIGHT_ENGINE_MAP_UNAVAILABLE = 4u,
} tdma_flight_engine_result_t;

typedef struct {
    uint32_t input_segment_mask;
    uint32_t output_segment_mask;
    uint32_t input_bytes;
    uint32_t output_bytes;
    uint32_t output_byte_bitmap[TDMA_FLIGHT_OUTPUT_BITMAP_WORDS];
} tdma_flight_engine_apply_t;

typedef struct {
    uint32_t present_segment_mask;
    uint32_t new_segment_mask;
    uint32_t expected_segment_mask;
} tdma_flight_engine_unload_t;

typedef struct {
    uint32_t version;
    uint32_t configured;
    uint32_t active;
    uint32_t local_slot_id;
    uint32_t map_crc32;
    uint32_t map_generation;
    uint32_t payload_size;
    uint32_t local_segment_count;
    uint32_t map_apply_count;
    uint32_t input_bytes;
    uint32_t output_bytes;
    uint32_t tx_stale_reuse_count;
    uint32_t map_reject_count;
    uint32_t length_reject_count;
    uint32_t tx_unavailable_count;
    uint32_t rx_bitmap_scan_count;
    uint32_t rx_bitmap_hit_count;
    uint32_t rx_bitmap_duplicate_count;
    uint32_t rx_bitmap_present_count;
    uint32_t rx_bitmap_incomplete_count;
} tdma_flight_engine_snapshot_t;

typedef struct {
    volatile uint32_t configured;
    volatile uint32_t active;
    volatile uint32_t map_sequence;
    volatile uint32_t map_generation;
    uint32_t local_slot_id;
    tdma_process_image_map_t map;
    volatile uint32_t map_apply_count;
    volatile uint32_t input_bytes;
    volatile uint32_t output_bytes;
    volatile uint32_t tx_stale_reuse_count;
    volatile uint32_t map_reject_count;
    volatile uint32_t length_reject_count;
    volatile uint32_t tx_unavailable_count;
    uint32_t rx_seen_segment_mask;
    uint16_t rx_last_seq16_by_segment[TDMA_PROCESS_IMAGE_SEGMENT_COUNT];
    volatile uint32_t rx_bitmap_scan_count;
    volatile uint32_t rx_bitmap_hit_count;
    volatile uint32_t rx_bitmap_duplicate_count;
    volatile uint32_t rx_bitmap_present_count;
    volatile uint32_t rx_bitmap_incomplete_count;
} tdma_flight_engine_t;

bool tdma_flight_engine_init(tdma_flight_engine_t *engine);
bool tdma_flight_engine_configure(tdma_flight_engine_t *engine,
                                  const tdma_process_image_map_t *map);
bool tdma_flight_engine_activate(tdma_flight_engine_t *engine,
                                 uint32_t local_slot_id);
void tdma_flight_engine_deactivate(tdma_flight_engine_t *engine);
bool tdma_flight_engine_is_configured(const tdma_flight_engine_t *engine);
bool tdma_flight_engine_is_active(const tdma_flight_engine_t *engine);
void tdma_flight_engine_fill_alignment_symbols(uint8_t *payload,
                                               size_t payload_size);
bool tdma_flight_engine_apply(tdma_flight_engine_t *engine,
                              const uint8_t *incoming,
                              size_t incoming_size,
                              const tdma_flight_tx_view_t *tx_view,
                              uint8_t *output,
                              size_t output_capacity,
                              tdma_flight_engine_apply_t *applied,
                              tdma_flight_engine_result_t *result);
/* Adapter path after inspect_input(): use the physical-ring-qualified new
 * mailbox mask instead of classifying all wire-image mailboxes again. */
bool tdma_flight_engine_apply_preclassified(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    uint32_t input_segment_mask,
    const tdma_flight_tx_view_t *tx_view,
    uint8_t *output,
    size_t output_capacity,
    tdma_flight_engine_apply_t *applied,
    tdma_flight_engine_result_t *result);
/* Directional TX boundary.  It copies the received image and overlays only
 * segments owned by this node; RX novelty is supplied by rx_unload(). */
bool tdma_flight_engine_tx_load(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    const tdma_flight_tx_view_t *tx_view,
    uint8_t *output,
    size_t output_capacity,
    tdma_flight_engine_apply_t *applied,
    tdma_flight_engine_result_t *result);
bool tdma_flight_engine_classify_input(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    uint32_t *input_segment_mask);
/* Inspect every active remote mailbox without treating an unchanged mailbox
 * sequence as absent. present_segment_mask is the WKC/Node-bitmap evidence
 * for this wire frame; new_segment_mask is the subset core0 has not consumed. */
bool tdma_flight_engine_inspect_input(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    uint32_t expected_owner_mask,
    uint32_t *present_segment_mask,
    uint32_t *new_segment_mask,
    uint32_t *expected_segment_mask);
/* Directional RX boundary.  It inspects the wire image without mutating the
 * consumed-sequence state; call rx_commit() after the RX descriptor is
 * published successfully. */
bool tdma_flight_engine_rx_unload(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    uint32_t expected_owner_mask,
    tdma_flight_engine_unload_t *unloaded);
bool tdma_flight_engine_expected_input_mask(
    const tdma_flight_engine_t *engine,
    uint32_t expected_owner_mask,
    uint32_t *expected_segment_mask);
bool tdma_flight_engine_commit_input(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    uint32_t input_segment_mask);
bool tdma_flight_engine_rx_commit(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    uint32_t input_segment_mask);
bool tdma_flight_engine_get_snapshot(
    const tdma_flight_engine_t *engine,
    tdma_flight_engine_snapshot_t *snapshot);

#endif
