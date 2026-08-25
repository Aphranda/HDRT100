#include "calibration_training_marker.h"

#include <string.h>

bool calibration_training_marker_capture_delay_cycles(
    uint32_t half_chip_samples,
    int32_t offset_sample_count,
    uint32_t *capture_delay_cycles)
{
    if (capture_delay_cycles == NULL) {
        return false;
    }
    const int64_t delay = (int64_t)half_chip_samples + offset_sample_count;
    if (delay < 0 ||
        delay > CALIBRATION_TRAINING_MARKER_MAX_CAPTURE_DELAY_CYCLES) {
        return false;
    }
    *capture_delay_cycles = (uint32_t)delay;
    return true;
}

static bool calibration_training_marker_request_valid(
    const calibration_training_marker_request_t *request)
{
    return request != NULL && request->board_unique_id != 0u &&
           request->build_id != 0u &&
           (request->role == CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR ||
            request->role == CALIBRATION_TRAINING_MARKER_ROLE_FOLLOWER) &&
           request->local_node < CALIBRATION_TRAINING_MARKER_MAX_NODES &&
           request->reference_node < CALIBRATION_TRAINING_MARKER_MAX_NODES &&
           request->predecessor_node < CALIBRATION_TRAINING_MARKER_MAX_NODES &&
           request->successor_node < CALIBRATION_TRAINING_MARKER_MAX_NODES &&
           ((request->role == CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR &&
             request->local_node == request->reference_node) ||
            (request->role == CALIBRATION_TRAINING_MARKER_ROLE_FOLLOWER &&
             request->local_node != request->reference_node)) &&
           request->predecessor_node != request->local_node &&
           request->successor_node != request->local_node &&
           request->train_epoch != 0u && request->train_sequence != 0u &&
           request->marker_id != 0u &&
           request->offset_sample_count >=
               CALIBRATION_TRAINING_MARKER_MIN_OFFSET_SAMPLES &&
           request->offset_sample_count <=
               CALIBRATION_TRAINING_MARKER_MAX_OFFSET_SAMPLES &&
           request->calibration_generation != 0u &&
           request->topology_generation != 0u &&
           request->topology_crc32 != 0u && request->profile_crc32 != 0u &&
           request->schedule_crc32 != 0u && request->tick_resolution_ns != 0u;
}

static void calibration_training_marker_snapshot_from_request(
    calibration_training_marker_snapshot_t *snapshot,
    const calibration_training_marker_request_t *request)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = CALIBRATION_TRAINING_MARKER_SNAPSHOT_VERSION;
    snapshot->flags = CALIBRATION_TRAINING_MARKER_FLAG_DIAGNOSTIC_ONLY;
    snapshot->board_unique_id = request->board_unique_id;
    snapshot->build_id = request->build_id;
    snapshot->role = request->role;
    snapshot->local_node = request->local_node;
    snapshot->reference_node = request->reference_node;
    snapshot->predecessor_node = request->predecessor_node;
    snapshot->successor_node = request->successor_node;
    snapshot->train_epoch = request->train_epoch;
    snapshot->train_sequence = request->train_sequence;
    snapshot->marker_id = request->marker_id;
    snapshot->marker_codebook_id = request->marker_codebook_id;
    snapshot->marker_crc32 = request->marker_crc32;
    snapshot->calibration_generation = request->calibration_generation;
    snapshot->topology_generation = request->topology_generation;
    snapshot->topology_crc32 = request->topology_crc32;
    snapshot->profile_crc32 = request->profile_crc32;
    snapshot->schedule_crc32 = request->schedule_crc32;
    snapshot->tick_resolution_ns = request->tick_resolution_ns;
    snapshot->offset_sample_count = request->offset_sample_count;
}

void calibration_training_marker_store_init(
    calibration_training_marker_store_t *store)
{
    if (store == NULL) return;
    memset(store, 0, sizeof(*store));
    store->snapshot.version = CALIBRATION_TRAINING_MARKER_SNAPSHOT_VERSION;
    store->snapshot.state = CALIBRATION_TRAINING_MARKER_IDLE;
}

bool calibration_training_marker_publish_core1(
    calibration_training_marker_store_t *store,
    const calibration_training_marker_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL ||
        snapshot->version != CALIBRATION_TRAINING_MARKER_SNAPSHOT_VERSION) {
        return false;
    }
    (void)__atomic_add_fetch(&store->guard, 1u, __ATOMIC_RELEASE);
    store->snapshot = *snapshot;
    (void)__atomic_add_fetch(&store->guard, 1u, __ATOMIC_RELEASE);
    return true;
}

bool calibration_training_marker_get_snapshot(
    const calibration_training_marker_store_t *store,
    calibration_training_marker_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u;
         attempt < CALIBRATION_TRAINING_MARKER_SNAPSHOT_READ_ATTEMPTS;
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

bool calibration_training_marker_prepare_core1(
    calibration_training_marker_store_t *store,
    const calibration_training_marker_request_t *request)
{
    if (store == NULL || !calibration_training_marker_request_valid(request)) {
        return false;
    }
    calibration_training_marker_snapshot_t next;
    calibration_training_marker_snapshot_from_request(&next, request);
    next.state = CALIBRATION_TRAINING_MARKER_PREPARED;
    return calibration_training_marker_publish_core1(store, &next);
}

static uint32_t calibration_training_marker_reject_reason(
    const calibration_training_marker_request_t *request,
    const calibration_training_marker_evidence_t *evidence)
{
    if (request->calibration_generation != evidence->calibration_generation ||
        request->topology_generation != evidence->topology_generation ||
        request->topology_crc32 != evidence->topology_crc32 ||
        request->profile_crc32 != evidence->profile_crc32 ||
        request->schedule_crc32 != evidence->schedule_crc32) {
        return CALIBRATION_TRAINING_MARKER_REJECT_GENERATION;
    }
    if (request->train_epoch != evidence->train_epoch) {
        return CALIBRATION_TRAINING_MARKER_REJECT_EPOCH;
    }
    if (request->train_sequence != evidence->train_sequence) {
        return CALIBRATION_TRAINING_MARKER_REJECT_SEQUENCE;
    }
    if (request->marker_id != evidence->marker_id) {
        return CALIBRATION_TRAINING_MARKER_REJECT_MARKER_ID;
    }
    if (request->marker_crc32 != evidence->observed_crc32 ||
        (evidence->flags & CALIBRATION_TRAINING_MARKER_FLAG_CRC_VALID) == 0u) {
        return CALIBRATION_TRAINING_MARKER_REJECT_CRC;
    }
    if ((evidence->flags & CALIBRATION_TRAINING_MARKER_REQUIRED_FLAGS) !=
        CALIBRATION_TRAINING_MARKER_REQUIRED_FLAGS) {
        return CALIBRATION_TRAINING_MARKER_REJECT_EVIDENCE_FLAGS;
    }
    if (evidence->dma_overrun_count != 0u) {
        return CALIBRATION_TRAINING_MARKER_REJECT_DMA;
    }
    if (evidence->pio_stall_count != 0u) {
        return CALIBRATION_TRAINING_MARKER_REJECT_PIO_STALL;
    }
    if (evidence->timeout_count != 0u) {
        return CALIBRATION_TRAINING_MARKER_REJECT_TIMEOUT;
    }
    if (request->role == CALIBRATION_TRAINING_MARKER_ROLE_FOLLOWER &&
        (evidence->marker_capture_tick == 0u ||
         evidence->marker_forward_tick < evidence->marker_capture_tick)) {
        return CALIBRATION_TRAINING_MARKER_REJECT_EDGE_ORDER;
    }
    if (request->role == CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR &&
        (evidence->marker_forward_tick == 0u ||
         evidence->marker_return_tick < evidence->marker_forward_tick ||
         evidence->marker_capture_tick != evidence->marker_return_tick)) {
        return CALIBRATION_TRAINING_MARKER_REJECT_EDGE_ORDER;
    }
    return CALIBRATION_TRAINING_MARKER_REJECT_NONE;
}

bool calibration_training_marker_evaluate_core1(
    calibration_training_marker_store_t *store,
    const calibration_training_marker_request_t *request,
    const calibration_training_marker_evidence_t *evidence)
{
    if (store == NULL || evidence == NULL ||
        !calibration_training_marker_request_valid(request)) {
        return false;
    }
    calibration_training_marker_snapshot_t next;
    calibration_training_marker_snapshot_from_request(&next, request);
    next.flags |= evidence->flags;
    next.observed_crc32 = evidence->observed_crc32;
    next.polarity = evidence->polarity;
    next.marker_flags = evidence->marker_flags;
    next.correlation_reject_reason = evidence->correlation_reject_reason;
    next.best_lag_sample = evidence->best_lag_sample;
    next.best_distance = evidence->best_distance;
    next.marker_capture_tick = evidence->marker_capture_tick;
    next.marker_forward_tick = evidence->marker_forward_tick;
    next.marker_return_tick = evidence->marker_return_tick;
    next.dma_capture_count = evidence->dma_capture_count;
    next.dma_overrun_count = evidence->dma_overrun_count;
    next.pio_stall_count = evidence->pio_stall_count;
    next.timeout_count = evidence->timeout_count;
    if (request->role == CALIBRATION_TRAINING_MARKER_ROLE_FOLLOWER &&
        evidence->marker_forward_tick >= evidence->marker_capture_tick) {
        next.forward_residence_ticks =
            evidence->marker_forward_tick - evidence->marker_capture_tick;
    } else if (request->role ==
                   CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR &&
               evidence->marker_return_tick >= evidence->marker_forward_tick) {
        next.loop_rtt_ticks =
            evidence->marker_return_tick - evidence->marker_forward_tick;
    }
    next.reject_reason =
        calibration_training_marker_reject_reason(request, evidence);
    next.state = next.reject_reason == CALIBRATION_TRAINING_MARKER_REJECT_NONE
                     ? CALIBRATION_TRAINING_MARKER_ACCEPTED
                     : CALIBRATION_TRAINING_MARKER_REJECTED;
    if (!calibration_training_marker_publish_core1(store, &next)) return false;
    return next.state == CALIBRATION_TRAINING_MARKER_ACCEPTED;
}
