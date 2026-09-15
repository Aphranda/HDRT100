#include "vdc_time_mapping.h"

#include <limits.h>

bool vdc_time_mapping_map_local_to_common_time(
    const tdma_ring_clock_snapshot_t *ring,
    uint32_t expected_schedule_crc32,
    uint64_t local_time_ns,
    uint64_t *common_time_ns)
{
    if (ring == NULL || common_time_ns == NULL ||
        expected_schedule_crc32 == 0u ||
        ring->enabled == 0u || ring->adapter_started == 0u ||
        ring->config_seq == 0u || ring->config_seq != ring->applied_config_seq ||
        ring->cycle_period_ns == 0u || ring->feedback_timeout_ns == 0u ||
        ring->schedule_crc32 == 0u ||
        ring->schedule_crc32 != expected_schedule_crc32) {
        return false;
    }

    const tdma_ring_clock_observation_t *observation =
        &ring->clock_observation;
    const uint32_t required_flags =
        TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
        TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME;
    if (observation->valid == 0u ||
        observation->correlated_frame_evidence == 0u ||
        (observation->correlation_flags & required_flags) != required_flags ||
        (observation->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) == 0u ||
        observation->local_rx_timestamp_ns == 0u ||
        observation->common_effective_time_ns == 0u ||
        local_time_ns < observation->local_rx_timestamp_ns) {
        return false;
    }

    const uint64_t age_ns = local_time_ns -
        observation->local_rx_timestamp_ns;
    if (age_ns > (uint64_t)ring->feedback_timeout_ns ||
        UINT64_MAX - observation->common_effective_time_ns < age_ns) {
        return false;
    }

    *common_time_ns = observation->common_effective_time_ns + age_ns;
    return true;
}

vdc_time_mapping_effective_time_result_t
vdc_time_mapping_classify_effective_time(uint64_t common_now_ns,
                                         uint64_t effective_time_ns,
                                         uint64_t max_lateness_ns,
                                         uint64_t *late_ns)
{
    if (late_ns == NULL || effective_time_ns == 0u ||
        max_lateness_ns == 0u) {
        return VDC_TIME_MAPPING_EFFECTIVE_TIME_INVALID;
    }
    *late_ns = 0u;
    if (common_now_ns < effective_time_ns) {
        return VDC_TIME_MAPPING_EFFECTIVE_TIME_NOT_DUE;
    }
    *late_ns = common_now_ns - effective_time_ns;
    return *late_ns <= max_lateness_ns
        ? VDC_TIME_MAPPING_EFFECTIVE_TIME_DUE
        : VDC_TIME_MAPPING_EFFECTIVE_TIME_TOO_LATE;
}

bool vdc_time_mapping_sequence_is_newer(uint32_t candidate,
                                        uint32_t previous)
{
    return candidate != 0u &&
           (previous == 0u || (int32_t)(candidate - previous) > 0);
}
