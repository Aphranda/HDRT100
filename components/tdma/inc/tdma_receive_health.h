#ifndef TDMA_RECEIVE_HEALTH_H
#define TDMA_RECEIVE_HEALTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_transport_frame.h"

#define TDMA_RECEIVE_HEALTH_VERSION 2u
#define TDMA_RECEIVE_HEALTH_SNAPSHOT_RETRY_MAX 64u

typedef enum {
    TDMA_RECEIVE_STATE_EMPTY = 0u,
    TDMA_RECEIVE_STATE_VALID = 1u,
    TDMA_RECEIVE_STATE_STALE = 2u,
} tdma_receive_state_t;

typedef enum {
    TDMA_RECEIVE_REASON_NONE = 0u,
    TDMA_RECEIVE_REASON_TRANSPORT = 1u,
    TDMA_RECEIVE_REASON_SCHEDULE_CRC = 2u,
    TDMA_RECEIVE_REASON_PROFILE_CRC = 3u,
    TDMA_RECEIVE_REASON_PAYLOAD_CLASS = 4u,
    TDMA_RECEIVE_REASON_FLAGS = 5u,
    TDMA_RECEIVE_REASON_MAP_UNAVAILABLE = 6u,
    TDMA_RECEIVE_REASON_PAYLOAD_SIZE = 7u,
    TDMA_RECEIVE_REASON_SEGMENT_BITMAP = 8u,
    TDMA_RECEIVE_REASON_SEQUENCE_DUPLICATE = 9u,
    TDMA_RECEIVE_REASON_SEQUENCE_STALE = 10u,
    TDMA_RECEIVE_REASON_MISSING = 11u,
} tdma_receive_reason_t;

#define TDMA_RECEIVE_QUALITY_TRANSPORT_VALID (1u << 0u)
#define TDMA_RECEIVE_QUALITY_SCHEDULE_VALID (1u << 1u)
#define TDMA_RECEIVE_QUALITY_PROFILE_VALID (1u << 2u)
#define TDMA_RECEIVE_QUALITY_SEQUENCE_VALID (1u << 3u)
#define TDMA_RECEIVE_QUALITY_MAP_VALID (1u << 4u)
#define TDMA_RECEIVE_QUALITY_BITMAP_COMPLETE (1u << 5u)
/* V1 has no frozen segment CRC/WKC trailer.  A complete mailbox bitmap is
 * useful bring-up evidence but cannot be promoted into product acceptance. */
#define TDMA_RECEIVE_QUALITY_DIAGNOSTIC_ONLY (1u << 31u)

typedef struct {
    uint32_t schedule_crc32;
    uint32_t ring_profile_crc32;
    uint32_t map_generation;
    uint32_t expected_payload_size;
    uint32_t expected_segment_mask;
    uint64_t stale_timeout_ns;
} tdma_receive_health_config_t;

typedef struct {
    uint32_t version;
    uint32_t configured;
    uint32_t state;
    uint32_t last_reason;
    uint32_t last_transport_result;
    uint32_t quality_flags;
    uint32_t accepted_count;
    uint32_t rejected_count;
    uint32_t missing_count;
    uint32_t consecutive_failure_count;
    uint32_t image_generation;
    uint32_t accepted_sequence;
    uint32_t accepted_identity_crc32;
    uint32_t accepted_schedule_crc32;
    uint32_t accepted_profile_crc32;
    uint32_t accepted_map_generation;
    uint32_t accepted_segment_mask;
    uint32_t expected_segment_mask;
    uint32_t accepted_wkc;
    uint32_t expected_wkc;
    uint32_t accepted_payload_size;
    uint64_t last_accept_timestamp_ns;
    uint64_t last_observation_timestamp_ns;
    uint64_t stale_age_ns;
    uint32_t last_rejected_reason;
    uint32_t last_rejected_transport_result;
    uint32_t last_rejected_sequence;
    uint32_t last_rejected_observed_segment_mask;
    uint32_t last_rejected_expected_segment_mask;
    uint32_t last_rejected_quality_flags;
    uint64_t last_rejected_timestamp_ns;
} tdma_receive_health_snapshot_t;

typedef struct {
    tdma_receive_health_snapshot_t health;
    uint8_t data[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
} tdma_receive_image_t;

typedef struct {
    volatile uint32_t guard;
    tdma_receive_health_config_t config;
    uint32_t configured;
    uint32_t state;
    uint32_t last_reason;
    uint32_t last_transport_result;
    uint32_t quality_flags;
    uint32_t accepted_count;
    uint32_t rejected_count;
    uint32_t missing_count;
    uint32_t consecutive_failure_count;
    uint32_t image_generation;
    uint32_t accepted_sequence;
    uint32_t accepted_identity_crc32;
    uint32_t accepted_schedule_crc32;
    uint32_t accepted_profile_crc32;
    uint32_t accepted_map_generation;
    uint32_t accepted_segment_mask;
    uint32_t accepted_wkc;
    uint32_t accepted_payload_size;
    uint64_t last_accept_timestamp_ns;
    uint64_t last_observation_timestamp_ns;
    uint32_t last_rejected_reason;
    uint32_t last_rejected_transport_result;
    uint32_t last_rejected_sequence;
    uint32_t last_rejected_observed_segment_mask;
    uint32_t last_rejected_expected_segment_mask;
    uint32_t last_rejected_quality_flags;
    uint64_t last_rejected_timestamp_ns;
    uint8_t accepted_payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
} tdma_receive_health_t;

bool tdma_receive_health_init(tdma_receive_health_t *health);
bool tdma_receive_health_configure_stopped(
    tdma_receive_health_t *health,
    const tdma_receive_health_config_t *config);
void tdma_receive_health_reset_stopped(tdma_receive_health_t *health);
bool tdma_receive_health_evaluate(
    tdma_receive_health_t *health,
    const tdma_transport_frame_view_t *view,
    tdma_transport_result_t transport_result,
    uint32_t observed_segment_mask,
    uint64_t observation_timestamp_ns,
    tdma_receive_reason_t *reason);
void tdma_receive_health_observe_missing(tdma_receive_health_t *health,
                                         uint64_t now_ns);
bool tdma_receive_health_get_snapshot(
    const tdma_receive_health_t *health,
    uint64_t now_ns,
    tdma_receive_health_snapshot_t *snapshot);
bool tdma_receive_health_read_image(const tdma_receive_health_t *health,
                                    uint64_t now_ns,
                                    tdma_receive_image_t *image);

#endif
