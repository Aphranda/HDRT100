#ifndef VDC_TIME_MAPPING_H
#define VDC_TIME_MAPPING_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_ring_runtime.h"

/* Map a local monotonic timestamp using one already copied TDMA clock
 * snapshot.  The function is deliberately stateless so the realtime owner can
 * validate a bounded snapshot without taking a control-plane lock, and host
 * tests can exercise every fail-closed boundary. */
bool vdc_time_mapping_map_local_to_common_time(
    const tdma_ring_clock_snapshot_t *ring,
    uint32_t expected_schedule_crc32,
    uint64_t local_time_ns,
    uint64_t *common_time_ns);

typedef enum {
    VDC_TIME_MAPPING_EFFECTIVE_TIME_INVALID = 0u,
    VDC_TIME_MAPPING_EFFECTIVE_TIME_NOT_DUE = 1u,
    VDC_TIME_MAPPING_EFFECTIVE_TIME_DUE = 2u,
    VDC_TIME_MAPPING_EFFECTIVE_TIME_TOO_LATE = 3u,
} vdc_time_mapping_effective_time_result_t;

/* Classify a command's common-time boundary against a receiver-owned lateness
 * bound.  The sender cannot extend this bound through the wire payload. */
vdc_time_mapping_effective_time_result_t
vdc_time_mapping_classify_effective_time(uint64_t common_now_ns,
                                         uint64_t effective_time_ns,
                                         uint64_t max_lateness_ns,
                                         uint64_t *late_ns);

/* Sequence ordering uses the bounded signed-difference rule so a non-zero
 * command sequence can advance across uint32 wrap without accepting an old
 * command. */
bool vdc_time_mapping_sequence_is_newer(uint32_t candidate,
                                        uint32_t previous);

#endif
