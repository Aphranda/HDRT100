#include "vdc_ring_observer.h"

#include <limits.h>
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define VDC_RING_OBSERVER_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_RING_OBSERVER_TIME_CRITICAL(name) name
#endif

static int32_t vdc_ring_observer_residual(uint32_t reference_tx_phase_ns,
                                          uint32_t link_delay_ns,
                                          uint32_t local_rx_phase_ns,
                                          uint32_t period_ns)
{
    if (period_ns == 0u || reference_tx_phase_ns >= period_ns ||
        local_rx_phase_ns >= period_ns) {
        return INT32_MAX;
    }
    /* Both phases are captured by their respective local counters.  Only
     * their position inside a frozen TDMA period is comparable across
     * boards; subtracting the raw 64-bit latches would inject boot-epoch
     * offsets as apparent DPLL phase error. */
    const int64_t period = (int64_t)period_ns;
    const int64_t expected_rx_phase_ns =
        ((int64_t)reference_tx_phase_ns + (int64_t)link_delay_ns) % period;
    int64_t residual = (int64_t)local_rx_phase_ns - expected_rx_phase_ns;
    const int64_t half_period = period / 2ll;
    while (residual > half_period) {
        residual -= period;
    }
    while (residual < -half_period) {
        residual += period;
    }
    if (residual > INT32_MAX) {
        return INT32_MAX;
    }
    if (residual < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)residual;
}

static bool vdc_ring_observer_active_schedule_validate(
    const vdc_tdma_schedule_profile_t *schedule)
{
    if (schedule == NULL || schedule->enabled == 0u ||
        schedule->period_ns == 0u || schedule->schedule_crc32 == 0u ||
        schedule->observation_window_width_ns == 0u ||
        schedule->observation_window_offset_ns >= schedule->period_ns ||
        schedule->observation_window_width_ns >
            schedule->period_ns - schedule->observation_window_offset_ns ||
        schedule->ring_binding.node_count < 2u ||
        schedule->ring_binding.node_count > VDC_DOMAIN_NODE_COUNT ||
        schedule->local_slot_id >= schedule->ring_binding.node_count ||
        schedule->reference_slot_id >= schedule->ring_binding.node_count ||
        schedule->local_slot_id != schedule->ring_binding.local_index ||
        schedule->reference_slot_id != schedule->ring_binding.reference_index) {
        return false;
    }
    return true;
}

static bool vdc_ring_observer_expand_checked(
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_ring_observation_t *observation,
    vdc_tdma_timestamp_evidence_t *evidence,
    bool validate_static_schedule)
{
    if (schedule == NULL || observation == NULL || evidence == NULL ||
        !(validate_static_schedule
              ? vdc_domain_schedule_validate(schedule)
              : vdc_ring_observer_active_schedule_validate(schedule)) ||
        observation->node_count < 2u ||
        observation->node_count > VDC_DOMAIN_NODE_COUNT ||
        observation->node_count != schedule->ring_binding.node_count ||
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
        (observation->correlation_flags &
         (TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
          TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME)) !=
            (TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
             TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME) ||
        observation->reference_tx_phase_ns >= schedule->period_ns ||
        observation->local_rx_phase_ns >= schedule->period_ns ||
        observation->common_effective_time_ns == 0ull ||
        observation->local_rx_timestamp_ns == 0ull) {
        return false;
    }
    if (schedule->period_ns == 0u) {
        return false;
    }

    if (observation->common_effective_time_ns % schedule->period_ns !=
        observation->reference_tx_phase_ns ||
        observation->common_effective_time_ns <
            (uint64_t)observation->reference_tx_phase_ns) {
        return false;
    }
    const uint64_t cycle_start_ns = observation->common_effective_time_ns -
        (uint64_t)observation->reference_tx_phase_ns;
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
    /* All event times are in the logical TDMA schedule domain.  The local
     * hardware RX latch remains provenance only and is never used here. */
    evidence->start_time_ns = observation->common_effective_time_ns;
    evidence->observed_time_ns = cycle_start_ns +
        (uint64_t)observation->local_rx_phase_ns;
    evidence->done_time_ns = evidence->observed_time_ns;
    evidence->apply_time_ns = evidence->observed_time_ns;
    evidence->delay_ns = observation->link_delay_ns;
    evidence->phase_error_ns = vdc_ring_observer_residual(
        observation->reference_tx_phase_ns,
        observation->link_delay_ns,
        observation->local_rx_phase_ns,
        schedule->period_ns);
    evidence->timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    evidence->timestamp_resolution_ns =
        observation->timestamp_resolution_ns;
    evidence->timestamp_flags = observation->timestamp_flags;
    evidence->schedule_crc32 = observation->schedule_crc32;
    evidence->frame_crc32 = observation->frame_crc32;
    evidence->sample_crc32 = observation->frame_crc32;
    evidence->delay_generation = observation->delay_generation;
    evidence->bias_generation = observation->bias_generation;
    evidence->correlation_flags = observation->correlation_flags;
    evidence->reference_tx_phase_ns = observation->reference_tx_phase_ns;
    evidence->local_rx_phase_ns = observation->local_rx_phase_ns;
    evidence->common_effective_time_ns = observation->common_effective_time_ns;
    evidence->reference_tx_timestamp_ns = observation->reference_tx_timestamp_ns;
    evidence->local_rx_timestamp_ns = observation->local_rx_timestamp_ns;
    return true;
}

bool vdc_ring_observer_expand(
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_ring_observation_t *observation,
    vdc_tdma_timestamp_evidence_t *evidence)
{
    return vdc_ring_observer_expand_checked(
        schedule, observation, evidence, true);
}

bool VDC_RING_OBSERVER_TIME_CRITICAL(vdc_ring_observer_expand_active)(
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_ring_observation_t *observation,
    vdc_tdma_timestamp_evidence_t *evidence)
{
    return vdc_ring_observer_expand_checked(
        schedule, observation, evidence, false);
}
