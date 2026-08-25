#include "calibration_training_data.h"

#include <limits.h>
#include <string.h>

static bool calibration_training_data_request_valid(
    const calibration_training_data_request_t *request)
{
    if (request == NULL || request->board_unique_id == 0u ||
        request->build_id == 0u ||
        request->source_node >= CALIBRATION_TRAINING_DATA_MAX_NODES ||
        request->destination_node >= CALIBRATION_TRAINING_DATA_MAX_NODES ||
        request->source_node == request->destination_node ||
        request->train_epoch == 0u || request->train_sequence == 0u ||
        request->data_codebook_id > 3u || request->data_crc32 == 0u ||
        request->calibration_generation == 0u ||
        request->topology_generation == 0u || request->topology_crc32 == 0u ||
        request->profile_crc32 == 0u || request->schedule_crc32 == 0u ||
        request->sample_period_ns == 0u ||
        request->marker_to_data_samples == 0u ||
        request->link_base_delay_ns == 0u ||
        request->marker_offset_sample_count <
            CALIBRATION_TRAINING_DATA_MIN_OFFSET_SAMPLES ||
        request->marker_offset_sample_count >
            CALIBRATION_TRAINING_DATA_MAX_OFFSET_SAMPLES ||
        request->configured_data_offset_sample_count <
            CALIBRATION_TRAINING_DATA_MIN_OFFSET_SAMPLES ||
        request->configured_data_offset_sample_count >
            CALIBRATION_TRAINING_DATA_MAX_OFFSET_SAMPLES ||
        request->search_start_offset_sample <
            CALIBRATION_TRAINING_DATA_MIN_OFFSET_SAMPLES ||
        request->search_end_offset_sample >
            CALIBRATION_TRAINING_DATA_MAX_OFFSET_SAMPLES ||
        request->search_start_offset_sample >
            request->search_end_offset_sample ||
        request->guard_sample_count >
            CALIBRATION_TRAINING_DATA_MAX_GUARD_SAMPLES ||
        request->expected_polarity > 1u) {
        return false;
    }
    const int64_t earliest_ns = (int64_t)request->link_base_delay_ns +
        ((int64_t)request->configured_data_offset_sample_count +
         (int64_t)request->search_start_offset_sample -
         (int64_t)request->guard_sample_count) * request->sample_period_ns;
    return earliest_ns >= 0 && earliest_ns <= INT32_MAX;
}

static void calibration_training_data_snapshot_from_request(
    calibration_training_data_snapshot_t *snapshot,
    const calibration_training_data_request_t *request)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = CALIBRATION_TRAINING_DATA_SNAPSHOT_VERSION;
    snapshot->flags = CALIBRATION_TRAINING_DATA_FLAG_DIAGNOSTIC_ONLY;
    snapshot->board_unique_id = request->board_unique_id;
    snapshot->build_id = request->build_id;
    snapshot->source_node = request->source_node;
    snapshot->destination_node = request->destination_node;
    snapshot->train_epoch = request->train_epoch;
    snapshot->train_sequence = request->train_sequence;
    snapshot->data_codebook_id = request->data_codebook_id;
    snapshot->data_crc32 = request->data_crc32;
    snapshot->calibration_generation = request->calibration_generation;
    snapshot->topology_generation = request->topology_generation;
    snapshot->topology_crc32 = request->topology_crc32;
    snapshot->profile_crc32 = request->profile_crc32;
    snapshot->schedule_crc32 = request->schedule_crc32;
    snapshot->sample_period_ns = request->sample_period_ns;
    snapshot->marker_to_data_samples = request->marker_to_data_samples;
    snapshot->link_base_delay_ns = request->link_base_delay_ns;
    snapshot->marker_offset_sample_count =
        request->marker_offset_sample_count;
    snapshot->configured_data_offset_sample_count =
        request->configured_data_offset_sample_count;
    snapshot->search_start_offset_sample =
        request->search_start_offset_sample;
    snapshot->search_end_offset_sample = request->search_end_offset_sample;
    snapshot->guard_sample_count = request->guard_sample_count;
}

void calibration_training_data_store_init(
    calibration_training_data_store_t *store)
{
    if (store == NULL) return;
    memset(store, 0, sizeof(*store));
    store->snapshot.version = CALIBRATION_TRAINING_DATA_SNAPSHOT_VERSION;
    store->snapshot.state = CALIBRATION_TRAINING_DATA_IDLE;
}

bool calibration_training_data_publish_core1(
    calibration_training_data_store_t *store,
    const calibration_training_data_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL ||
        snapshot->version != CALIBRATION_TRAINING_DATA_SNAPSHOT_VERSION) {
        return false;
    }
    (void)__atomic_add_fetch(&store->guard, 1u, __ATOMIC_RELEASE);
    store->snapshot = *snapshot;
    (void)__atomic_add_fetch(&store->guard, 1u, __ATOMIC_RELEASE);
    return true;
}

bool calibration_training_data_get_snapshot(
    const calibration_training_data_store_t *store,
    calibration_training_data_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u;
         attempt < CALIBRATION_TRAINING_DATA_SNAPSHOT_READ_ATTEMPTS;
         attempt++) {
        const uint32_t begin =
            __atomic_load_n(&store->guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = store->snapshot;
        const uint32_t end =
            __atomic_load_n(&store->guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool calibration_training_data_prepare_core1(
    calibration_training_data_store_t *store,
    const calibration_training_data_request_t *request)
{
    if (store == NULL || !calibration_training_data_request_valid(request)) {
        return false;
    }
    calibration_training_data_snapshot_t next;
    calibration_training_data_snapshot_from_request(&next, request);
    next.state = CALIBRATION_TRAINING_DATA_PREPARED;
    return calibration_training_data_publish_core1(store, &next);
}

static uint32_t calibration_training_data_reject_reason(
    const calibration_training_data_request_t *request,
    const calibration_training_data_evidence_t *evidence)
{
    if (request->calibration_generation != evidence->calibration_generation ||
        request->topology_generation != evidence->topology_generation ||
        request->topology_crc32 != evidence->topology_crc32 ||
        request->profile_crc32 != evidence->profile_crc32 ||
        request->schedule_crc32 != evidence->schedule_crc32) {
        return CALIBRATION_TRAINING_DATA_REJECT_GENERATION;
    }
    if (request->train_epoch != evidence->train_epoch) {
        return CALIBRATION_TRAINING_DATA_REJECT_EPOCH;
    }
    if (request->train_sequence != evidence->train_sequence) {
        return CALIBRATION_TRAINING_DATA_REJECT_SEQUENCE;
    }
    if (request->data_crc32 != evidence->observed_crc32 ||
        (evidence->flags & CALIBRATION_TRAINING_DATA_FLAG_CRC_VALID) == 0u) {
        return CALIBRATION_TRAINING_DATA_REJECT_CRC;
    }
    if ((evidence->flags & CALIBRATION_TRAINING_DATA_REQUIRED_FLAGS) !=
        CALIBRATION_TRAINING_DATA_REQUIRED_FLAGS) {
        return CALIBRATION_TRAINING_DATA_REJECT_EVIDENCE_FLAGS;
    }
    if (evidence->correlation_reject_reason != 0u ||
        evidence->second_distance < evidence->best_distance ||
        evidence->margin !=
            evidence->second_distance - evidence->best_distance) {
        return CALIBRATION_TRAINING_DATA_REJECT_CORRELATION;
    }
    if (evidence->polarity != request->expected_polarity) {
        return CALIBRATION_TRAINING_DATA_REJECT_POLARITY;
    }
    if (evidence->expected_sample_count == 0u ||
        evidence->captured_sample_count < evidence->expected_sample_count) {
        return CALIBRATION_TRAINING_DATA_REJECT_CAPTURE_TRUNCATED;
    }
    if (evidence->dma_overrun_count != 0u) {
        return CALIBRATION_TRAINING_DATA_REJECT_DMA;
    }
    if (evidence->pio_stall_count != 0u) {
        return CALIBRATION_TRAINING_DATA_REJECT_PIO_STALL;
    }
    if (evidence->timeout_count != 0u) {
        return CALIBRATION_TRAINING_DATA_REJECT_TIMEOUT;
    }
    if (evidence->best_distance > request->max_best_distance) {
        return CALIBRATION_TRAINING_DATA_REJECT_DISTANCE;
    }
    if (evidence->margin < request->min_margin) {
        return CALIBRATION_TRAINING_DATA_REJECT_MARGIN;
    }
    const uint32_t search_samples = (uint32_t)(
        request->search_end_offset_sample -
        request->search_start_offset_sample + 1);
    if (evidence->best_lag_sample >= search_samples ||
        evidence->second_lag_sample >= search_samples) {
        return CALIBRATION_TRAINING_DATA_REJECT_SEARCH_RANGE;
    }
    if (evidence->marker_capture_tick == 0u ||
        evidence->data_capture_tick < evidence->marker_capture_tick) {
        return CALIBRATION_TRAINING_DATA_REJECT_EDGE_ORDER;
    }
    return CALIBRATION_TRAINING_DATA_REJECT_NONE;
}

bool calibration_training_data_evaluate_core1(
    calibration_training_data_store_t *store,
    const calibration_training_data_request_t *request,
    const calibration_training_data_evidence_t *evidence)
{
    if (store == NULL || evidence == NULL ||
        !calibration_training_data_request_valid(request)) {
        return false;
    }
    calibration_training_data_snapshot_t next;
    calibration_training_data_snapshot_from_request(&next, request);
    next.flags |= evidence->flags;
    next.observed_crc32 = evidence->observed_crc32;
    next.polarity = evidence->polarity;
    next.correlation_reject_reason = evidence->correlation_reject_reason;
    next.best_lag_sample = evidence->best_lag_sample;
    next.best_distance = evidence->best_distance;
    next.second_lag_sample = evidence->second_lag_sample;
    next.second_distance = evidence->second_distance;
    next.margin = evidence->margin;
    next.captured_sample_count = evidence->captured_sample_count;
    next.expected_sample_count = evidence->expected_sample_count;
    next.dma_overrun_count = evidence->dma_overrun_count;
    next.pio_stall_count = evidence->pio_stall_count;
    next.timeout_count = evidence->timeout_count;
    next.marker_capture_tick = evidence->marker_capture_tick;
    next.data_capture_tick = evidence->data_capture_tick;
    next.reject_reason =
        calibration_training_data_reject_reason(request, evidence);

    if (next.reject_reason == CALIBRATION_TRAINING_DATA_REJECT_NONE) {
        next.resolved_offset_sample_count =
            request->search_start_offset_sample +
            (int32_t)evidence->best_lag_sample;
        next.resolved_offset_ns = next.resolved_offset_sample_count *
                                  (int32_t)request->sample_period_ns;
        next.training_window_start_ns =
            (int32_t)request->link_base_delay_ns +
            (request->configured_data_offset_sample_count +
             next.resolved_offset_sample_count -
             (int32_t)request->guard_sample_count) *
                (int32_t)request->sample_period_ns;
        next.training_window_end_ns =
            (int32_t)request->link_base_delay_ns +
            (request->configured_data_offset_sample_count +
             next.resolved_offset_sample_count +
             (int32_t)request->guard_sample_count) *
                (int32_t)request->sample_period_ns;
        const int64_t link_base_delay_samples =
            ((int64_t)request->link_base_delay_ns +
             request->sample_period_ns / 2u) /
            request->sample_period_ns;
        const int64_t expected_delta_samples =
            (int64_t)request->marker_to_data_samples +
            4 * link_base_delay_samples +
            request->configured_data_offset_sample_count +
            next.resolved_offset_sample_count;
        const int64_t observed_delta_samples =
            (int64_t)(evidence->data_capture_tick -
                      evidence->marker_capture_tick);
        const int64_t skew_ns =
            (observed_delta_samples - expected_delta_samples) *
            request->sample_period_ns;
        if (skew_ns < INT32_MIN || skew_ns > INT32_MAX) {
            next.reject_reason =
                CALIBRATION_TRAINING_DATA_REJECT_EDGE_ORDER;
        } else {
            next.marker_data_skew_ns = (int32_t)skew_ns;
        }
    }

    next.state = next.reject_reason == CALIBRATION_TRAINING_DATA_REJECT_NONE
                     ? CALIBRATION_TRAINING_DATA_ACCEPTED
                     : CALIBRATION_TRAINING_DATA_REJECTED;
    if (!calibration_training_data_publish_core1(store, &next)) return false;
    return next.state == CALIBRATION_TRAINING_DATA_ACCEPTED;
}
