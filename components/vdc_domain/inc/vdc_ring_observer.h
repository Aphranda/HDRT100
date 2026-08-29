#ifndef VDC_RING_OBSERVER_H
#define VDC_RING_OBSERVER_H

#include <stdbool.h>
#include <stdint.h>

#include "vdc_domain.h"

typedef struct {
    uint32_t node_count;
    uint32_t source_node;
    uint32_t reference_node;
    uint32_t correlated_sequence;
    uint32_t frame_crc32;
    uint32_t schedule_crc32;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t correlated_frame_evidence;
    uint32_t link_delay_ns;
    uint64_t reference_tx_timestamp_ns;
    uint64_t local_rx_timestamp_ns;
} vdc_ring_observation_t;

/* Convert one correlated TDMA ring feedback sample into the common VDC
 * evidence contract.  This adapter never invents eligibility: only a
 * sample with matching hardware-latched reference-TX/local-RX evidence is
 * accepted.  When source_node == reference_node the sample denotes the
 * complete calibrated loop return path, allowing the reference Node to use
 * the same observer/servo flow as every forward Node. */
bool vdc_ring_observer_expand(
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_ring_observation_t *observation,
    vdc_tdma_timestamp_evidence_t *evidence);
/* Realtime variant for a schedule already accepted by
 * vdc_domain_activate_tdma_*(). It preserves identity, bounds and timestamp
 * admission checks without recomputing the immutable schedule CRC. */
bool vdc_ring_observer_expand_active(
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_ring_observation_t *observation,
    vdc_tdma_timestamp_evidence_t *evidence);

#endif
