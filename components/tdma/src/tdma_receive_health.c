#include "tdma_receive_health.h"

#include "tdma_profile.h"

#include <string.h>

static void tdma_receive_write_begin(tdma_receive_health_t *health)
{
    (void)__atomic_add_fetch(&health->guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_receive_write_end(tdma_receive_health_t *health)
{
    (void)__atomic_add_fetch(&health->guard, 1u, __ATOMIC_RELEASE);
}

static uint32_t tdma_receive_popcount(uint32_t value)
{
    uint32_t count = 0u;
    while (value != 0u) {
        count += value & 1u;
        value >>= 1u;
    }
    return count;
}

static bool tdma_receive_config_valid(
    const tdma_receive_health_config_t *config)
{
    return config != NULL && config->schedule_crc32 != 0u &&
           config->ring_profile_crc32 != 0u &&
           config->map_generation != 0u &&
           config->expected_payload_size != 0u &&
           config->expected_payload_size <= TDMA_TRANSPORT_SHORT_PAYLOAD_MAX &&
           config->expected_segment_mask != 0u &&
           config->stale_timeout_ns != 0ull;
}

bool tdma_receive_health_init(tdma_receive_health_t *health)
{
    if (health == NULL) {
        return false;
    }
    memset(health, 0, sizeof(*health));
    return true;
}

bool tdma_receive_health_configure_stopped(
    tdma_receive_health_t *health,
    const tdma_receive_health_config_t *config)
{
    if (health == NULL || !tdma_receive_config_valid(config)) {
        return false;
    }
    tdma_receive_write_begin(health);
    health->config = *config;
    health->configured = 1u;
    health->state = TDMA_RECEIVE_STATE_EMPTY;
    health->last_reason = TDMA_RECEIVE_REASON_NONE;
    health->last_transport_result = TDMA_TRANSPORT_OK;
    health->quality_flags = 0u;
    health->accepted_count = 0u;
    health->rejected_count = 0u;
    health->missing_count = 0u;
    health->consecutive_failure_count = 0u;
    health->image_generation = 0u;
    health->accepted_sequence = 0u;
    health->accepted_identity_crc32 = 0u;
    health->accepted_schedule_crc32 = 0u;
    health->accepted_profile_crc32 = 0u;
    health->accepted_map_generation = 0u;
    health->accepted_segment_mask = 0u;
    health->accepted_wkc = 0u;
    health->accepted_payload_size = 0u;
    health->last_accept_timestamp_ns = 0ull;
    health->last_observation_timestamp_ns = 0ull;
    memset(health->accepted_payload, 0, sizeof(health->accepted_payload));
    tdma_receive_write_end(health);
    return true;
}

void tdma_receive_health_reset_stopped(tdma_receive_health_t *health)
{
    if (health == NULL) {
        return;
    }
    tdma_receive_write_begin(health);
    memset(&health->config, 0, sizeof(health->config));
    health->configured = 0u;
    health->state = TDMA_RECEIVE_STATE_EMPTY;
    health->last_reason = TDMA_RECEIVE_REASON_NONE;
    health->last_transport_result = TDMA_TRANSPORT_OK;
    health->quality_flags = 0u;
    health->accepted_count = 0u;
    health->rejected_count = 0u;
    health->missing_count = 0u;
    health->consecutive_failure_count = 0u;
    health->image_generation = 0u;
    health->accepted_sequence = 0u;
    health->accepted_identity_crc32 = 0u;
    health->accepted_schedule_crc32 = 0u;
    health->accepted_profile_crc32 = 0u;
    health->accepted_map_generation = 0u;
    health->accepted_segment_mask = 0u;
    health->accepted_wkc = 0u;
    health->accepted_payload_size = 0u;
    health->last_accept_timestamp_ns = 0ull;
    health->last_observation_timestamp_ns = 0ull;
    memset(health->accepted_payload, 0, sizeof(health->accepted_payload));
    tdma_receive_write_end(health);
}

static void tdma_receive_reject_locked(tdma_receive_health_t *health,
                                       tdma_receive_reason_t reason,
                                       tdma_transport_result_t transport_result,
                                       uint64_t observation_timestamp_ns)
{
    health->last_reason = (uint32_t)reason;
    health->last_transport_result = (uint32_t)transport_result;
    health->last_observation_timestamp_ns = observation_timestamp_ns;
    health->rejected_count++;
    health->consecutive_failure_count++;
    if (health->image_generation != 0u) {
        health->state = TDMA_RECEIVE_STATE_STALE;
    }
}

static bool tdma_receive_sequence_newer(uint32_t observed, uint32_t accepted)
{
    return (int32_t)(observed - accepted) > 0;
}

bool tdma_receive_health_evaluate(
    tdma_receive_health_t *health,
    const tdma_transport_frame_view_t *view,
    tdma_transport_result_t transport_result,
    uint32_t observed_segment_mask,
    uint64_t observation_timestamp_ns,
    tdma_receive_reason_t *reason)
{
    if (reason != NULL) {
        *reason = TDMA_RECEIVE_REASON_NONE;
    }
    if (health == NULL) {
        if (reason != NULL) {
            *reason = TDMA_RECEIVE_REASON_MAP_UNAVAILABLE;
        }
        return false;
    }

    tdma_receive_reason_t rejected = TDMA_RECEIVE_REASON_NONE;
    uint32_t quality = TDMA_RECEIVE_QUALITY_DIAGNOSTIC_ONLY;
    if (health->configured == 0u) {
        rejected = TDMA_RECEIVE_REASON_MAP_UNAVAILABLE;
    } else if (view == NULL || transport_result != TDMA_TRANSPORT_OK) {
        rejected = TDMA_RECEIVE_REASON_TRANSPORT;
    } else {
        quality |= TDMA_RECEIVE_QUALITY_TRANSPORT_VALID;
        if (view->schedule_crc32 != health->config.schedule_crc32) {
            rejected = TDMA_RECEIVE_REASON_SCHEDULE_CRC;
        } else {
            quality |= TDMA_RECEIVE_QUALITY_SCHEDULE_VALID;
        }
        if (rejected == TDMA_RECEIVE_REASON_NONE &&
            view->ring_profile_crc32 != health->config.ring_profile_crc32) {
            rejected = TDMA_RECEIVE_REASON_PROFILE_CRC;
        } else if (rejected == TDMA_RECEIVE_REASON_NONE) {
            quality |= TDMA_RECEIVE_QUALITY_PROFILE_VALID;
        }
        if (rejected == TDMA_RECEIVE_REASON_NONE &&
            view->payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE) {
            rejected = TDMA_RECEIVE_REASON_PAYLOAD_CLASS;
        }
        if (rejected == TDMA_RECEIVE_REASON_NONE &&
            (view->flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) == 0u) {
            rejected = TDMA_RECEIVE_REASON_FLAGS;
        }
        if (rejected == TDMA_RECEIVE_REASON_NONE &&
            (health->config.map_generation == 0u ||
             health->config.expected_segment_mask == 0u)) {
            rejected = TDMA_RECEIVE_REASON_MAP_UNAVAILABLE;
        } else if (rejected == TDMA_RECEIVE_REASON_NONE) {
            quality |= TDMA_RECEIVE_QUALITY_MAP_VALID;
        }
        if (rejected == TDMA_RECEIVE_REASON_NONE &&
            view->payload_size != health->config.expected_payload_size) {
            rejected = TDMA_RECEIVE_REASON_PAYLOAD_SIZE;
        }
        if (rejected == TDMA_RECEIVE_REASON_NONE &&
            observed_segment_mask != health->config.expected_segment_mask) {
            rejected = TDMA_RECEIVE_REASON_SEGMENT_BITMAP;
        } else if (rejected == TDMA_RECEIVE_REASON_NONE) {
            quality |= TDMA_RECEIVE_QUALITY_BITMAP_COMPLETE;
        }
        if (rejected == TDMA_RECEIVE_REASON_NONE &&
            health->image_generation != 0u) {
            if (view->transport_sequence == health->accepted_sequence) {
                rejected = TDMA_RECEIVE_REASON_SEQUENCE_DUPLICATE;
            } else if (!tdma_receive_sequence_newer(
                           view->transport_sequence,
                           health->accepted_sequence)) {
                rejected = TDMA_RECEIVE_REASON_SEQUENCE_STALE;
            }
        }
        if (rejected == TDMA_RECEIVE_REASON_NONE) {
            quality |= TDMA_RECEIVE_QUALITY_SEQUENCE_VALID;
        }
    }

    tdma_receive_write_begin(health);
    health->quality_flags = quality;
    if (rejected != TDMA_RECEIVE_REASON_NONE) {
        tdma_receive_reject_locked(health,
                                   rejected,
                                   transport_result,
                                   observation_timestamp_ns);
        tdma_receive_write_end(health);
        if (reason != NULL) {
            *reason = rejected;
        }
        return false;
    }

    memcpy(health->accepted_payload, view->payload, view->payload_size);
    health->state = TDMA_RECEIVE_STATE_VALID;
    health->last_reason = TDMA_RECEIVE_REASON_NONE;
    health->last_transport_result = TDMA_TRANSPORT_OK;
    health->quality_flags = quality;
    health->accepted_count++;
    health->consecutive_failure_count = 0u;
    health->image_generation++;
    if (health->image_generation == 0u) {
        health->image_generation = 1u;
    }
    health->accepted_sequence = view->transport_sequence;
    health->accepted_identity_crc32 = view->identity_crc32;
    health->accepted_schedule_crc32 = view->schedule_crc32;
    health->accepted_profile_crc32 = view->ring_profile_crc32;
    health->accepted_map_generation = health->config.map_generation;
    health->accepted_segment_mask = observed_segment_mask;
    health->accepted_wkc = tdma_receive_popcount(observed_segment_mask);
    health->accepted_payload_size = (uint32_t)view->payload_size;
    health->last_accept_timestamp_ns = observation_timestamp_ns;
    health->last_observation_timestamp_ns = observation_timestamp_ns;
    tdma_receive_write_end(health);
    return true;
}

void tdma_receive_health_observe_missing(tdma_receive_health_t *health,
                                         uint64_t now_ns)
{
    if (health == NULL || health->configured == 0u ||
        health->image_generation == 0u ||
        health->last_accept_timestamp_ns == 0ull ||
        now_ns < health->last_accept_timestamp_ns ||
        now_ns - health->last_accept_timestamp_ns <=
            health->config.stale_timeout_ns ||
        (health->state == TDMA_RECEIVE_STATE_STALE &&
         health->last_reason == TDMA_RECEIVE_REASON_MISSING)) {
        return;
    }
    tdma_receive_write_begin(health);
    health->state = TDMA_RECEIVE_STATE_STALE;
    health->last_reason = TDMA_RECEIVE_REASON_MISSING;
    health->last_transport_result = TDMA_TRANSPORT_OK;
    health->last_observation_timestamp_ns = now_ns;
    health->quality_flags = TDMA_RECEIVE_QUALITY_DIAGNOSTIC_ONLY;
    health->missing_count++;
    health->consecutive_failure_count++;
    tdma_receive_write_end(health);
}

static void tdma_receive_copy_snapshot(
    const tdma_receive_health_t *health,
    uint64_t now_ns,
    tdma_receive_health_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = TDMA_RECEIVE_HEALTH_VERSION;
    snapshot->configured = health->configured;
    snapshot->state = health->state;
    snapshot->last_reason = health->last_reason;
    snapshot->last_transport_result = health->last_transport_result;
    snapshot->quality_flags = health->quality_flags;
    snapshot->accepted_count = health->accepted_count;
    snapshot->rejected_count = health->rejected_count;
    snapshot->missing_count = health->missing_count;
    snapshot->consecutive_failure_count = health->consecutive_failure_count;
    snapshot->image_generation = health->image_generation;
    snapshot->accepted_sequence = health->accepted_sequence;
    snapshot->accepted_identity_crc32 = health->accepted_identity_crc32;
    snapshot->accepted_schedule_crc32 = health->accepted_schedule_crc32;
    snapshot->accepted_profile_crc32 = health->accepted_profile_crc32;
    snapshot->accepted_map_generation = health->accepted_map_generation;
    snapshot->accepted_segment_mask = health->accepted_segment_mask;
    snapshot->expected_segment_mask = health->config.expected_segment_mask;
    snapshot->accepted_wkc = health->accepted_wkc;
    snapshot->expected_wkc =
        tdma_receive_popcount(health->config.expected_segment_mask);
    snapshot->accepted_payload_size = health->accepted_payload_size;
    snapshot->last_accept_timestamp_ns = health->last_accept_timestamp_ns;
    snapshot->last_observation_timestamp_ns =
        health->last_observation_timestamp_ns;
    if (health->last_accept_timestamp_ns != 0ull &&
        now_ns >= health->last_accept_timestamp_ns) {
        snapshot->stale_age_ns = now_ns - health->last_accept_timestamp_ns;
    }
}

bool tdma_receive_health_get_snapshot(
    const tdma_receive_health_t *health,
    uint64_t now_ns,
    tdma_receive_health_snapshot_t *snapshot)
{
    if (health == NULL || snapshot == NULL) {
        return false;
    }
    for (uint32_t retry = 0u;
         retry < TDMA_RECEIVE_HEALTH_SNAPSHOT_RETRY_MAX;
         retry++) {
        const uint32_t begin =
            __atomic_load_n(&health->guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        tdma_receive_copy_snapshot(health, now_ns, snapshot);
        const uint32_t end =
            __atomic_load_n(&health->guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return true;
        }
    }
    return false;
}

bool tdma_receive_health_read_image(const tdma_receive_health_t *health,
                                    uint64_t now_ns,
                                    tdma_receive_image_t *image)
{
    if (health == NULL || image == NULL) {
        return false;
    }
    for (uint32_t retry = 0u;
         retry < TDMA_RECEIVE_HEALTH_SNAPSHOT_RETRY_MAX;
         retry++) {
        const uint32_t begin =
            __atomic_load_n(&health->guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        tdma_receive_copy_snapshot(health, now_ns, &image->health);
        if (image->health.accepted_payload_size != 0u) {
            memcpy(image->data,
                   health->accepted_payload,
                   image->health.accepted_payload_size);
        }
        const uint32_t end =
            __atomic_load_n(&health->guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return image->health.image_generation != 0u;
        }
    }
    return false;
}
