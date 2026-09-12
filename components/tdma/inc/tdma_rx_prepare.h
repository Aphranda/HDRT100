#ifndef TDMA_RX_PREPARE_H
#define TDMA_RX_PREPARE_H

#include "tdma_origin_plan.h"
#include "tdma_ring_runtime.h"

typedef enum {
    TDMA_RX_PREPARE_IDLE = 0u,
    TDMA_RX_PREPARE_REQUESTED,
    TDMA_RX_PREPARE_BUILDING,
    TDMA_RX_PREPARE_READY,
    TDMA_RX_PREPARE_CANCELLED,
} tdma_rx_prepare_state_t;

typedef struct {
    uint32_t header_diff_count, header_first_diff_offset;
    uint32_t header_expected_byte, header_observed_byte;
    uint32_t packet_diff_count, packet_first_diff_offset;
    uint32_t packet_expected_byte, packet_observed_byte;
    uint32_t clock_evidence;
    uint32_t expected_transport_crc32, observed_transport_crc32;
    uint32_t recomputed_transport_crc32;
    uint32_t expected_payload_crc32, observed_payload_crc32;
} tdma_rx_diagnostic_t;

/* One immutable station slot. Core0 accesses only this job; no adapter,
 * engine, FIFO, DMA bank or register pointer crosses the boundary. Epoch
 * and state are Core1-owned except the worker's claim/completion/ACK. */
typedef struct {
    volatile uint32_t state;
    uint32_t epoch, request_epoch;
    uint32_t schedule_crc32, profile_crc32, map_generation, node_count;
    uint64_t capture_service_ns, rx_timestamp_ns;
    uint32_t round_trip_ns, resolution_ns, flags;
    uint32_t capture_timestamp_resolution_ns, capture_timestamp_flags;
    bool round_trip_valid, local_tx_captured;
    tdma_ring_local_tx_edge_evidence_t local_tx;
    bool origin_active, origin_paired, origin_owner_match;
    uint32_t origin_owner_generation, origin_owner_sequence;
    tdma_origin_observation_t origin;
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t packet_size;
    uint8_t expected[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t expected_size;
    bool expected_clock;
    /* Worker output; view.payload borrows this job's packet until release. */
    bool decoded, origin_mailboxes_valid;
    tdma_transport_result_t result;
    tdma_transport_frame_view_t view;
    tdma_rx_diagnostic_t diagnostic;
} tdma_rx_prepare_t;

uint32_t tdma_rx_prepare_state(const tdma_rx_prepare_t *job);
bool tdma_rx_prepare_request(tdma_rx_prepare_t *job);
bool tdma_rx_prepare_core0_claim(tdma_rx_prepare_t *job);
void tdma_rx_prepare_core0_build_claimed(tdma_rx_prepare_t *job);
void tdma_rx_prepare_core0_service(tdma_rx_prepare_t *job);
/* False retains only the static job lease. Hardware can stop immediately. */
bool tdma_rx_prepare_cancel(tdma_rx_prepare_t *job);
void tdma_rx_prepare_release(tdma_rx_prepare_t *job);
void tdma_rx_diagnose(const uint8_t *packet, size_t packet_size,
    const tdma_transport_frame_view_t *view, const uint8_t *expected,
    size_t expected_size, bool clock_evidence, tdma_rx_diagnostic_t *diagnostic);

#endif
