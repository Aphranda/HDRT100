#include "vdc_ring_observer.h"

#include <limits.h>
#include <string.h>

static int32_t vdc_ring_observer_residual(uint64_t reference_tx_timestamp_ns,
                                          uint32_t link_delay_ns,
                                          uint64_t local_rx_timestamp_ns)
{
    if (UINT64_MAX - reference_tx_timestamp_ns < (uint64_t)link_delay_ns) {
        return INT32_MAX;
    }
    const uint64_t expected_rx_timestamp_ns =
        reference_tx_timestamp_ns + (uint64_t)link_delay_ns;
    const int64_t residual = local_rx_timestamp_ns >= expected_rx_timestamp_ns
        ? (int64_t)(local_rx_timestamp_ns - expected_rx_timestamp_ns)
        : -(int64_t)(expected_rx_timestamp_ns - local_rx_timestamp_ns);
    if (residual > INT32_MAX) {
        return INT32_MAX;
    }
    if (residual < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)residual;
}

bool vdc_ring_observer_expand(
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_ring_observation_t *observation,
    vdc_tdma_timestamp_evidence_t *evidence)
{
    if (schedule == NULL || observation == NULL || evidence == NULL ||
        !vdc_domain_schedule_validate(schedule) ||
        observation->node_count < 2u ||
        observation->node_count > VDC_DOMAIN_NODE_COUNT ||
        observation->node_count != schedule->ring_binding.node_count ||
        observation->source_node == observation->reference_node ||
        observation->source_node != schedule->local_slot_id ||
        observation->reference_node != schedule->reference_slot_id ||
        observation->correlated_sequence == 0u ||
        observation->frame_crc32 == 0u ||
        observation->schedule_crc32 != schedule->schedule_crc32 ||
        observation->correlated_frame_evidence == 0u ||
        observation->timestamp_resolution_ns == 0u ||
        observation->timestamp_resolution_ns >
            VDC_DOMAIN_DPLL_ADMISSION_TIMESTAMP_RESOLUTION_LIMIT_NS ||
        (observation->timestamp_flags &
         VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) == 0u ||
        (observation->timestamp_flags &
         VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u ||
        observation->reference_tx_timestamp_ns == 0ull ||
        observation->local_rx_timestamp_ns == 0ull) {
        return false;
    }
    if (schedule->period_ns == 0u) {
        return false;
    }

    const uint64_t cycle_start_ns =
        observation->reference_tx_timestamp_ns -
        observation->reference_tx_timestamp_ns % schedule->period_ns;
    if (UINT64_MAX - cycle_start_ns <
        (uint64_t)schedule->observation_window_offset_ns) {
        return false;
    }

    memset(evidence, 0, sizeof(*evidence));
    evidence->sample_seq = observation->correlated_sequence;
    evidence->schedule_epoch = schedule->schedule_epoch;
    evidence->slot_index = 0u;
    evidence->source_slot_id = observation->source_node;
    evidence->reference_slot_id = observation->reference_node;
    evidence->payload_class = VDC_DOMAIN_PAYLOAD_IDLE_BEACON;
    evidence->expected_window_start_ns =
        cycle_start_ns + schedule->observation_window_offset_ns;
    evidence->start_time_ns = observation->reference_tx_timestamp_ns;
    evidence->observed_time_ns = observation->local_rx_timestamp_ns;
    evidence->done_time_ns = observation->local_rx_timestamp_ns;
    evidence->apply_time_ns = observation->local_rx_timestamp_ns;
    evidence->delay_ns = observation->link_delay_ns;
    evidence->phase_error_ns = vdc_ring_observer_residual(
        observation->reference_tx_timestamp_ns,
        observation->link_delay_ns,
        observation->local_rx_timestamp_ns);
    evidence->timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    evidence->timestamp_resolution_ns =
        observation->timestamp_resolution_ns;
    evidence->timestamp_flags = observation->timestamp_flags;
    evidence->schedule_crc32 = observation->schedule_crc32;
    evidence->frame_crc32 = observation->frame_crc32;
    evidence->sample_crc32 = observation->frame_crc32;
    return true;
}
