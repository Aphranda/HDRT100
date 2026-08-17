#include "tdma_traffic_scheduler.h"

#include <string.h>

static bool tdma_traffic_scheduler_try_lock(tdma_traffic_scheduler_t *scheduler)
{
    uint32_t expected = 0u;
    return __atomic_compare_exchange_n(&scheduler->lock,
                                       &expected,
                                       1u,
                                       false,
                                       __ATOMIC_ACQUIRE,
                                       __ATOMIC_RELAXED);
}

static void tdma_traffic_scheduler_unlock(tdma_traffic_scheduler_t *scheduler)
{
    __atomic_store_n(&scheduler->lock, 0u, __ATOMIC_RELEASE);
}

static tdma_traffic_scheduler_result_t tdma_traffic_scheduler_note_result(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t traffic_class,
    tdma_traffic_scheduler_result_t result)
{
    scheduler->last_result = (uint32_t)result;
    scheduler->last_traffic_class = traffic_class;
    return result;
}

static bool tdma_traffic_scheduler_payload_class(
    const tdma_traffic_scheduler_t *scheduler,
    uint32_t payload_class,
    uint32_t *traffic_class)
{
    if (payload_class >= 32u || traffic_class == NULL) {
        return false;
    }
    const uint32_t payload_bit = TDMA_PAYLOAD_BIT(payload_class);
    for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
        if ((scheduler->profile[i].payload_mask & payload_bit) != 0u) {
            *traffic_class = i;
            return true;
        }
    }
    return false;
}

static bool tdma_traffic_scheduler_frame_class_allowed(
    uint32_t traffic_class,
    uint32_t frame_class)
{
    if (traffic_class <= TDMA_TRAFFIC_REFMEM_REALTIME) {
        return frame_class == TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_SHORT;
    }
    if (traffic_class == TDMA_TRAFFIC_CONFIG_CONTROL) {
        return frame_class == TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_SHORT ||
               frame_class == TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG;
    }
    return frame_class == TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG;
}

static tdma_traffic_scheduler_slot_t *tdma_traffic_scheduler_queue_head(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t traffic_class)
{
    tdma_traffic_scheduler_queue_t *queue = &scheduler->queue[traffic_class];
    if (queue->count == 0u || queue->depth == 0u) {
        return NULL;
    }
    return &scheduler->slot[queue->base + queue->read_index];
}

static void tdma_traffic_scheduler_pop_head(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t traffic_class)
{
    tdma_traffic_scheduler_queue_t *queue = &scheduler->queue[traffic_class];
    if (queue->count == 0u || queue->depth == 0u) {
        return;
    }
    queue->read_index = (queue->read_index + 1u) % queue->depth;
    queue->count--;
    scheduler->quality[traffic_class].current_depth = queue->count;
}

static void tdma_traffic_scheduler_refresh_cycle(
    tdma_traffic_scheduler_t *scheduler,
    uint64_t now_ns)
{
    if (scheduler->cycle_period_ns == 0u) {
        return;
    }
    const uint64_t cycle_number = now_ns / scheduler->cycle_period_ns;
    if (cycle_number == scheduler->cycle_number) {
        return;
    }
    scheduler->cycle_number = cycle_number;
    scheduler->cycle_seq++;
    scheduler->cycle_bytes = 0u;
    for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
        scheduler->quality[i].cycle_bytes = 0u;
        scheduler->quality[i].cycle_frames = 0u;
    }
}

static bool tdma_traffic_scheduler_deadline_expired(
    const tdma_traffic_scheduler_t *scheduler,
    const tdma_traffic_scheduler_slot_t *slot,
    uint64_t now_ns)
{
    if (slot->scheduled_window_valid != 0u) {
        return false;
    }
    const uint32_t deadline_ns =
        scheduler->profile[slot->traffic_class].deadline_ns;
    return deadline_ns != 0u && now_ns > slot->enqueue_time_ns &&
           now_ns - slot->enqueue_time_ns > deadline_ns;
}

static void tdma_traffic_scheduler_drop_expired(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t traffic_class,
    uint64_t now_ns)
{
    while (true) {
        tdma_traffic_scheduler_slot_t *slot =
            tdma_traffic_scheduler_queue_head(scheduler, traffic_class);
        if (slot == NULL) {
            return;
        }
        const bool window_missed =
            slot->scheduled_window_valid != 0u &&
            now_ns > slot->scheduled_window_end_ns;
        if (!window_missed &&
            !tdma_traffic_scheduler_deadline_expired(scheduler, slot, now_ns)) {
            return;
        }
        tdma_traffic_scheduler_pop_head(scheduler, traffic_class);
        scheduler->quality[traffic_class].deadline_miss_count++;
        scheduler->quality[traffic_class].drop_count++;
        if (scheduler->profile[traffic_class].overflow_policy ==
            TDMA_OVERFLOW_FAULT) {
            scheduler->fault_latched = 1u;
        }
        (void)tdma_traffic_scheduler_note_result(
            scheduler,
            traffic_class,
            TDMA_TRAFFIC_SCHEDULER_DEADLINE_MISSED);
    }
}

static bool tdma_traffic_scheduler_budget_available(
    tdma_traffic_scheduler_t *scheduler,
    const tdma_traffic_scheduler_slot_t *slot)
{
    const uint32_t traffic_class = slot->traffic_class;
    const tdma_traffic_class_profile_t *profile =
        &scheduler->profile[traffic_class];
    const tdma_traffic_class_quality_t *quality =
        &scheduler->quality[traffic_class];
    if (quality->cycle_frames >= profile->max_frames_per_cycle) {
        return false;
    }
    if (traffic_class <= TDMA_TRAFFIC_CONFIG_CONTROL &&
        quality->cycle_bytes + slot->frame_size >
            profile->reserved_bytes_per_cycle) {
        return false;
    }
    return scheduler->cycle_bytes + slot->frame_size <=
           scheduler->usable_cycle_bytes;
}

static void tdma_traffic_scheduler_note_budget_overrun(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t traffic_class)
{
    if (scheduler->budget_reported_cycle[traffic_class] !=
        scheduler->cycle_number) {
        scheduler->budget_reported_cycle[traffic_class] =
            scheduler->cycle_number;
        scheduler->quality[traffic_class].budget_overrun_count++;
    }
}

static bool tdma_traffic_scheduler_before_higher_priority_guard(
    tdma_traffic_scheduler_t *scheduler,
    const tdma_traffic_scheduler_slot_t *candidate,
    uint64_t now_ns,
    uint32_t higher_priority_class_count)
{
    uint64_t earliest_guard_ns = UINT64_MAX;
    for (uint32_t i = 0u; i < higher_priority_class_count; i++) {
        const tdma_traffic_scheduler_slot_t *slot =
            tdma_traffic_scheduler_queue_head(scheduler, i);
        if (slot != NULL && slot->scheduled_window_valid != 0u &&
            slot->scheduled_guard_start_ns > now_ns &&
            slot->scheduled_guard_start_ns < earliest_guard_ns) {
            earliest_guard_ns = slot->scheduled_guard_start_ns;
        }
    }
    if (earliest_guard_ns == UINT64_MAX) {
        return true;
    }
    return candidate->estimated_duration_ns <= earliest_guard_ns - now_ns;
}

static bool tdma_traffic_scheduler_dispatch_head(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t traffic_class,
    tdma_traffic_dispatch_t *dispatch)
{
    tdma_traffic_scheduler_slot_t *slot =
        tdma_traffic_scheduler_queue_head(scheduler, traffic_class);
    if (slot == NULL || dispatch == NULL) {
        return false;
    }
    memset(dispatch, 0, sizeof(*dispatch));
    dispatch->sequence = slot->sequence;
    dispatch->traffic_class = traffic_class;
    dispatch->request.intent_type = slot->intent_type;
    dispatch->request.role = slot->role;
    dispatch->request.baud_hz = slot->baud_hz;
    dispatch->request.rx_pin = slot->rx_pin;
    dispatch->request.csn_pin = slot->csn_pin;
    dispatch->request.sck_pin = slot->sck_pin;
    dispatch->request.tx_pin = slot->tx_pin;
    dispatch->request.deadline_1e3ns = slot->deadline_1e3ns;
    dispatch->request.frame_class = slot->frame_class;
    dispatch->request.payload_class = slot->payload_class;
    dispatch->request.window_epoch = slot->window_epoch;
    dispatch->request.window_index = slot->window_index;
    dispatch->request.scheduled_window_valid = slot->scheduled_window_valid;
    dispatch->request.scheduled_window_class = slot->scheduled_window_class;
    dispatch->request.schedule_crc32 = slot->schedule_crc32;
    dispatch->request.scheduled_window_start_ns =
        slot->scheduled_window_start_ns;
    dispatch->request.scheduled_window_end_ns = slot->scheduled_window_end_ns;
    dispatch->request.scheduled_guard_start_ns =
        slot->scheduled_guard_start_ns;
    dispatch->request.scheduled_guard_end_ns = slot->scheduled_guard_end_ns;
    dispatch->request.enqueue_time_ns = slot->enqueue_time_ns;
    dispatch->request.estimated_duration_ns = slot->estimated_duration_ns;
    dispatch->request.frame_size = slot->frame_size;
    if (slot->frame_size != 0u) {
        memcpy(dispatch->frame, slot->frame, slot->frame_size);
    }
    dispatch->request.frame = dispatch->frame;

    tdma_traffic_scheduler_pop_head(scheduler, traffic_class);
    scheduler->dispatch_seq++;
    scheduler->cycle_bytes += slot->frame_size;
    scheduler->quality[traffic_class].dispatched_count++;
    scheduler->quality[traffic_class].last_dispatched_sequence =
        slot->sequence;
    scheduler->quality[traffic_class].cycle_bytes += slot->frame_size;
    scheduler->quality[traffic_class].cycle_frames++;
    return true;
}

bool tdma_traffic_scheduler_init(
    tdma_traffic_scheduler_t *scheduler,
    tdma_traffic_scheduler_slot_t *slot_storage,
    uint32_t slot_capacity)
{
    if (scheduler == NULL || slot_storage == NULL || slot_capacity == 0u ||
        slot_capacity > TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT) {
        return false;
    }
    memset(scheduler, 0, sizeof(*scheduler));
    memset(slot_storage,
           0,
           sizeof(tdma_traffic_scheduler_slot_t) * slot_capacity);
    scheduler->slot = slot_storage;
    scheduler->slot_capacity = slot_capacity;
    scheduler->last_traffic_class = UINT32_MAX;
    return true;
}

bool tdma_traffic_scheduler_configure(
    tdma_traffic_scheduler_t *scheduler,
    const tdma_foundation_profile_t *profile)
{
    tdma_profile_result_t profile_result = TDMA_PROFILE_BAD_ARGUMENT;
    if (scheduler == NULL || profile == NULL ||
        !tdma_foundation_profile_validate(profile, &profile_result) ||
        profile->resource.long_frame_capacity >
            TDMA_TRAFFIC_SCHEDULER_FRAME_MAX ||
        !tdma_traffic_scheduler_try_lock(scheduler)) {
        return false;
    }

    uint32_t slot_base = 0u;
    for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
        if (profile->resource.traffic[i].queue_depth >
            scheduler->slot_capacity - slot_base) {
            tdma_traffic_scheduler_unlock(scheduler);
            return false;
        }
        scheduler->profile[i] = profile->resource.traffic[i];
        scheduler->queue[i].base = slot_base;
        scheduler->queue[i].depth = profile->resource.traffic[i].queue_depth;
        scheduler->queue[i].read_index = 0u;
        scheduler->queue[i].write_index = 0u;
        scheduler->queue[i].count = 0u;
        memset(&scheduler->quality[i], 0, sizeof(scheduler->quality[i]));
        scheduler->budget_reported_cycle[i] = UINT64_MAX;
        slot_base += profile->resource.traffic[i].queue_depth;
    }
    scheduler->configured = 1u;
    scheduler->config_seq++;
    scheduler->enqueue_seq = 0u;
    scheduler->dispatch_seq = 0u;
    scheduler->cycle_seq = 0u;
    scheduler->cycle_number = UINT64_MAX;
    scheduler->cycle_period_ns = profile->resource.cycle_period_ns;
    scheduler->cycle_capacity_bytes = profile->resource.cycle_capacity_bytes;
    scheduler->guard_band_bytes = profile->resource.guard_band_bytes;
    scheduler->usable_cycle_bytes =
        profile->resource.cycle_capacity_bytes -
        profile->resource.guard_band_bytes;
    scheduler->short_frame_capacity =
        profile->resource.short_frame_capacity;
    scheduler->long_frame_capacity =
        profile->resource.long_frame_capacity;
    scheduler->cycle_bytes = 0u;
    scheduler->fault_latched = 0u;
    scheduler->last_result = TDMA_TRAFFIC_SCHEDULER_OK;
    scheduler->last_traffic_class = UINT32_MAX;
    tdma_traffic_scheduler_unlock(scheduler);
    return true;
}

tdma_traffic_scheduler_result_t tdma_traffic_scheduler_enqueue(
    tdma_traffic_scheduler_t *scheduler,
    const tdma_traffic_request_t *request)
{
    if (scheduler == NULL || request == NULL ||
        request->frame_size > TDMA_TRAFFIC_SCHEDULER_FRAME_MAX ||
        (request->frame_size != 0u && request->frame == NULL)) {
        return TDMA_TRAFFIC_SCHEDULER_BAD_ARGUMENT;
    }
    if (!tdma_traffic_scheduler_try_lock(scheduler)) {
        return TDMA_TRAFFIC_SCHEDULER_BUSY;
    }
    if (scheduler->configured == 0u) {
        tdma_traffic_scheduler_unlock(scheduler);
        return TDMA_TRAFFIC_SCHEDULER_NOT_CONFIGURED;
    }

    uint32_t traffic_class = UINT32_MAX;
    if (!tdma_traffic_scheduler_payload_class(scheduler,
                                              request->payload_class,
                                              &traffic_class)) {
        (void)tdma_traffic_scheduler_note_result(
            scheduler,
            UINT32_MAX,
            TDMA_TRAFFIC_SCHEDULER_CLASS_REJECTED);
        tdma_traffic_scheduler_unlock(scheduler);
        return TDMA_TRAFFIC_SCHEDULER_CLASS_REJECTED;
    }
    if (!tdma_traffic_scheduler_frame_class_allowed(traffic_class,
                                                     request->frame_class)) {
        (void)tdma_traffic_scheduler_note_result(
            scheduler,
            traffic_class,
            TDMA_TRAFFIC_SCHEDULER_CLASS_REJECTED);
        tdma_traffic_scheduler_unlock(scheduler);
        return TDMA_TRAFFIC_SCHEDULER_CLASS_REJECTED;
    }
    const bool frame_capacity_valid =
        (request->frame_class == TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_SHORT &&
         request->frame_size <= scheduler->short_frame_capacity) ||
        (request->frame_class == TDMA_TRAFFIC_SCHEDULER_FRAME_CLASS_LONG &&
         request->frame_size <= scheduler->long_frame_capacity);
    if (!frame_capacity_valid) {
        (void)tdma_traffic_scheduler_note_result(
            scheduler,
            traffic_class,
            TDMA_TRAFFIC_SCHEDULER_BAD_ARGUMENT);
        tdma_traffic_scheduler_unlock(scheduler);
        return TDMA_TRAFFIC_SCHEDULER_BAD_ARGUMENT;
    }

    tdma_traffic_scheduler_queue_t *queue = &scheduler->queue[traffic_class];
    tdma_traffic_scheduler_result_t enqueue_result =
        TDMA_TRAFFIC_SCHEDULER_OK;
    if (queue->count >= queue->depth) {
        switch (scheduler->profile[traffic_class].overflow_policy) {
        case TDMA_OVERFLOW_FAULT:
            scheduler->fault_latched = 1u;
            scheduler->quality[traffic_class].drop_count++;
            enqueue_result = TDMA_TRAFFIC_SCHEDULER_FAULT;
            break;
        case TDMA_OVERFLOW_BACKPRESSURE:
            scheduler->quality[traffic_class].backpressure_count++;
            enqueue_result = TDMA_TRAFFIC_SCHEDULER_BACKPRESSURE;
            break;
        case TDMA_OVERFLOW_DROP_OLDEST:
            tdma_traffic_scheduler_pop_head(scheduler, traffic_class);
            scheduler->quality[traffic_class].drop_count++;
            enqueue_result = TDMA_TRAFFIC_SCHEDULER_DROPPED_OLDEST;
            break;
        case TDMA_OVERFLOW_DROP_NEWEST:
        default:
            scheduler->quality[traffic_class].drop_count++;
            enqueue_result = TDMA_TRAFFIC_SCHEDULER_DROPPED_NEWEST;
            break;
        }
        if (enqueue_result != TDMA_TRAFFIC_SCHEDULER_DROPPED_OLDEST) {
            (void)tdma_traffic_scheduler_note_result(scheduler,
                                                     traffic_class,
                                                     enqueue_result);
            tdma_traffic_scheduler_unlock(scheduler);
            return enqueue_result;
        }
    }

    tdma_traffic_scheduler_slot_t *slot =
        &scheduler->slot[queue->base + queue->write_index];
    memset(slot, 0, sizeof(*slot));
    scheduler->enqueue_seq++;
    slot->sequence = scheduler->enqueue_seq;
    slot->traffic_class = traffic_class;
    slot->intent_type = request->intent_type;
    slot->role = request->role;
    slot->baud_hz = request->baud_hz;
    slot->rx_pin = request->rx_pin;
    slot->csn_pin = request->csn_pin;
    slot->sck_pin = request->sck_pin;
    slot->tx_pin = request->tx_pin;
    slot->deadline_1e3ns = request->deadline_1e3ns;
    slot->frame_class = request->frame_class;
    slot->payload_class = request->payload_class;
    slot->window_epoch = request->window_epoch;
    slot->window_index = request->window_index;
    slot->scheduled_window_valid = request->scheduled_window_valid;
    slot->scheduled_window_class = request->scheduled_window_class;
    slot->schedule_crc32 = request->schedule_crc32;
    slot->scheduled_window_start_ns = request->scheduled_window_start_ns;
    slot->scheduled_window_end_ns = request->scheduled_window_end_ns;
    slot->scheduled_guard_start_ns = request->scheduled_guard_start_ns;
    slot->scheduled_guard_end_ns = request->scheduled_guard_end_ns;
    slot->enqueue_time_ns = request->enqueue_time_ns;
    slot->estimated_duration_ns = request->estimated_duration_ns;
    slot->frame_size = (uint32_t)request->frame_size;
    if (request->frame_size != 0u) {
        memcpy(slot->frame, request->frame, request->frame_size);
    }
    queue->write_index = (queue->write_index + 1u) % queue->depth;
    queue->count++;
    scheduler->quality[traffic_class].queued_count++;
    scheduler->quality[traffic_class].current_depth = queue->count;
    if (queue->count > scheduler->quality[traffic_class].queue_high_watermark) {
        scheduler->quality[traffic_class].queue_high_watermark = queue->count;
    }
    (void)tdma_traffic_scheduler_note_result(scheduler,
                                             traffic_class,
                                             enqueue_result);
    tdma_traffic_scheduler_unlock(scheduler);
    return enqueue_result;
}

tdma_traffic_scheduler_result_t tdma_traffic_scheduler_select(
    tdma_traffic_scheduler_t *scheduler,
    uint64_t now_ns,
    bool maintenance_gate_open,
    tdma_traffic_dispatch_t *dispatch)
{
    if (scheduler == NULL || dispatch == NULL) {
        return TDMA_TRAFFIC_SCHEDULER_BAD_ARGUMENT;
    }
    if (!tdma_traffic_scheduler_try_lock(scheduler)) {
        return TDMA_TRAFFIC_SCHEDULER_BUSY;
    }
    if (scheduler->configured == 0u) {
        tdma_traffic_scheduler_unlock(scheduler);
        return TDMA_TRAFFIC_SCHEDULER_NOT_CONFIGURED;
    }

    tdma_traffic_scheduler_refresh_cycle(scheduler, now_ns);
    for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
        tdma_traffic_scheduler_drop_expired(scheduler, i, now_ns);
    }

    for (uint32_t i = TDMA_TRAFFIC_VDC_REALTIME;
         i <= TDMA_TRAFFIC_REFMEM_REALTIME;
         i++) {
        tdma_traffic_scheduler_slot_t *slot =
            tdma_traffic_scheduler_queue_head(scheduler, i);
        if (slot == NULL) {
            continue;
        }
        const bool gate_open =
            slot->scheduled_window_valid == 0u ||
            now_ns >= slot->scheduled_guard_start_ns;
        if (!gate_open) {
            continue;
        }
        if (i == TDMA_TRAFFIC_REFMEM_REALTIME &&
            !tdma_traffic_scheduler_before_higher_priority_guard(
                scheduler,
                slot,
                now_ns,
                TDMA_TRAFFIC_REFMEM_REALTIME)) {
            continue;
        }
        if (!tdma_traffic_scheduler_budget_available(scheduler, slot)) {
            tdma_traffic_scheduler_note_budget_overrun(scheduler, i);
            (void)tdma_traffic_scheduler_note_result(
                scheduler, i, TDMA_TRAFFIC_SCHEDULER_BUDGET_EXHAUSTED);
            tdma_traffic_scheduler_unlock(scheduler);
            return TDMA_TRAFFIC_SCHEDULER_BUDGET_EXHAUSTED;
        }
        (void)tdma_traffic_scheduler_dispatch_head(scheduler, i, dispatch);
        (void)tdma_traffic_scheduler_note_result(
            scheduler, i, TDMA_TRAFFIC_SCHEDULER_OK);
        tdma_traffic_scheduler_unlock(scheduler);
        return TDMA_TRAFFIC_SCHEDULER_OK;
    }

    if (!maintenance_gate_open) {
        (void)tdma_traffic_scheduler_note_result(
            scheduler, UINT32_MAX, TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED);
        tdma_traffic_scheduler_unlock(scheduler);
        return TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED;
    }

    for (uint32_t i = TDMA_TRAFFIC_CONFIG_CONTROL;
         i < TDMA_TRAFFIC_CLASS_COUNT;
         i++) {
        tdma_traffic_scheduler_slot_t *slot =
            tdma_traffic_scheduler_queue_head(scheduler, i);
        if (slot == NULL ||
            !tdma_traffic_scheduler_before_higher_priority_guard(
                scheduler,
                slot,
                now_ns,
                TDMA_TRAFFIC_CONFIG_CONTROL)) {
            continue;
        }
        if (!tdma_traffic_scheduler_budget_available(scheduler, slot)) {
            tdma_traffic_scheduler_note_budget_overrun(scheduler, i);
            continue;
        }
        (void)tdma_traffic_scheduler_dispatch_head(scheduler, i, dispatch);
        (void)tdma_traffic_scheduler_note_result(
            scheduler, i, TDMA_TRAFFIC_SCHEDULER_OK);
        tdma_traffic_scheduler_unlock(scheduler);
        return TDMA_TRAFFIC_SCHEDULER_OK;
    }

    (void)tdma_traffic_scheduler_note_result(
        scheduler, UINT32_MAX, TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED);
    tdma_traffic_scheduler_unlock(scheduler);
    return TDMA_TRAFFIC_SCHEDULER_GATE_CLOSED;
}

bool tdma_traffic_scheduler_complete(
    tdma_traffic_scheduler_t *scheduler,
    uint32_t traffic_class,
    tdma_traffic_completion_t completion)
{
    if (scheduler == NULL || traffic_class >= TDMA_TRAFFIC_CLASS_COUNT ||
        completion > TDMA_TRAFFIC_COMPLETION_ADAPTER_ERROR ||
        !tdma_traffic_scheduler_try_lock(scheduler)) {
        return false;
    }
    tdma_traffic_class_quality_t *quality = &scheduler->quality[traffic_class];
    quality->last_completed_sequence = quality->last_dispatched_sequence;
    switch (completion) {
    case TDMA_TRAFFIC_COMPLETION_SENT:
        quality->sent_count++;
        break;
    case TDMA_TRAFFIC_COMPLETION_LATE:
        quality->sent_count++;
        quality->late_count++;
        break;
    case TDMA_TRAFFIC_COMPLETION_RETRY:
        quality->retry_count++;
        break;
    case TDMA_TRAFFIC_COMPLETION_DROP:
    case TDMA_TRAFFIC_COMPLETION_WINDOW_MISSED:
        quality->drop_count++;
        if (completion == TDMA_TRAFFIC_COMPLETION_WINDOW_MISSED) {
            quality->deadline_miss_count++;
        }
        break;
    case TDMA_TRAFFIC_COMPLETION_ADAPTER_ERROR:
        quality->adapter_error_count++;
        break;
    default:
        tdma_traffic_scheduler_unlock(scheduler);
        return false;
    }
    tdma_traffic_scheduler_unlock(scheduler);
    return true;
}

bool tdma_traffic_scheduler_clear_fault(tdma_traffic_scheduler_t *scheduler)
{
    if (scheduler == NULL || !tdma_traffic_scheduler_try_lock(scheduler)) {
        return false;
    }
    scheduler->fault_latched = 0u;
    tdma_traffic_scheduler_unlock(scheduler);
    return true;
}

bool tdma_traffic_scheduler_get_snapshot(
    tdma_traffic_scheduler_t *scheduler,
    tdma_traffic_scheduler_snapshot_t *snapshot)
{
    if (scheduler == NULL || snapshot == NULL ||
        !tdma_traffic_scheduler_try_lock(scheduler)) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = TDMA_TRAFFIC_SCHEDULER_VERSION;
    snapshot->configured = scheduler->configured;
    snapshot->config_seq = scheduler->config_seq;
    snapshot->enqueue_seq = scheduler->enqueue_seq;
    snapshot->dispatch_seq = scheduler->dispatch_seq;
    snapshot->cycle_seq = scheduler->cycle_seq;
    snapshot->cycle_period_ns = scheduler->cycle_period_ns;
    snapshot->cycle_capacity_bytes = scheduler->cycle_capacity_bytes;
    snapshot->guard_band_bytes = scheduler->guard_band_bytes;
    snapshot->usable_cycle_bytes = scheduler->usable_cycle_bytes;
    snapshot->short_frame_capacity = scheduler->short_frame_capacity;
    snapshot->long_frame_capacity = scheduler->long_frame_capacity;
    snapshot->cycle_bytes = scheduler->cycle_bytes;
    snapshot->fault_latched = scheduler->fault_latched;
    snapshot->last_result = scheduler->last_result;
    snapshot->last_traffic_class = scheduler->last_traffic_class;
    memcpy(snapshot->traffic,
           scheduler->quality,
           sizeof(snapshot->traffic));
    tdma_traffic_scheduler_unlock(scheduler);
    return true;
}
