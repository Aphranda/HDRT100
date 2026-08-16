#include "vdc_tdma_payload.h"

#include <string.h>

#define VDC_TDMA_PAYLOAD_CRC_OFFSET 2166136261u
#define VDC_TDMA_PAYLOAD_CRC_PRIME 16777619u

static uint32_t vdc_tdma_payload_crc32(const uint8_t *data, size_t size)
{
    uint32_t hash = VDC_TDMA_PAYLOAD_CRC_OFFSET;
    if (data == NULL && size != 0u) {
        return 0u;
    }
    for (size_t i = 0u; i < size; i++) {
        hash ^= data[i];
        hash *= VDC_TDMA_PAYLOAD_CRC_PRIME;
    }
    return hash == 0u ? 1u : hash;
}

static void vdc_tdma_payload_set_status(vdc_tdma_payload_status_t *status,
                                        vdc_tdma_payload_result_t result,
                                        uint32_t payload_class,
                                        uint32_t frame_crc32,
                                        uint32_t payload_crc32,
                                        const vdc_gate_result_t *gate)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->result = result;
    status->payload_class = payload_class;
    status->frame_crc32 = frame_crc32;
    status->payload_crc32 = payload_crc32;
    if (gate != NULL) {
        status->gate = *gate;
    }
}

static void vdc_tdma_payload_gate_reject(vdc_gate_result_t *gate,
                                         vdc_domain_gate_code_t code,
                                         uint32_t slot,
                                         uint32_t evidence)
{
    if (gate == NULL) {
        return;
    }
    memset(gate, 0, sizeof(*gate));
    gate->reject_code = (uint32_t)code;
    gate->reject_slot = slot;
    gate->reject_evidence = evidence;
}

static bool vdc_tdma_payload_allowed(uint32_t window_class,
                                     uint32_t payload_class)
{
    switch ((vdc_domain_tdma_window_class_t)window_class) {
    case VDC_DOMAIN_WINDOW_VDC_OBSERVATION:
        return payload_class == VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE ||
               payload_class == VDC_DOMAIN_PAYLOAD_IDLE_BEACON;
    case VDC_DOMAIN_WINDOW_IDLE_BEACON:
        return payload_class == VDC_DOMAIN_PAYLOAD_IDLE_BEACON;
    case VDC_DOMAIN_WINDOW_REFMEM_DATA:
    default:
        return false;
    }
}

static void put_u32(uint8_t *frame, size_t offset, uint32_t value)
{
    frame[offset + 0u] = (uint8_t)(value & 0xFFu);
    frame[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
    frame[offset + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
    frame[offset + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void put_i32(uint8_t *frame, size_t offset, int32_t value)
{
    put_u32(frame, offset, (uint32_t)value);
}

static void put_u64(uint8_t *frame, size_t offset, uint64_t value)
{
    put_u32(frame, offset, (uint32_t)(value & 0xFFFFFFFFull));
    put_u32(frame, offset + 4u, (uint32_t)(value >> 32u));
}

static uint32_t get_u32(const uint8_t *frame, size_t offset)
{
    return (uint32_t)frame[offset + 0u] |
           ((uint32_t)frame[offset + 1u] << 8u) |
           ((uint32_t)frame[offset + 2u] << 16u) |
           ((uint32_t)frame[offset + 3u] << 24u);
}

static int32_t get_i32(const uint8_t *frame, size_t offset)
{
    return (int32_t)get_u32(frame, offset);
}

static uint64_t get_u64(const uint8_t *frame, size_t offset)
{
    return (uint64_t)get_u32(frame, offset) |
           ((uint64_t)get_u32(frame, offset + 4u) << 32u);
}

static uint32_t vdc_tdma_payload_map_to_tdma(uint32_t payload_class)
{
    switch ((vdc_domain_payload_class_t)payload_class) {
    case VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE:
        return TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE;
    case VDC_DOMAIN_PAYLOAD_IDLE_BEACON:
        return TDMA_SERVICE_PAYLOAD_CLASS_IDLE_BEACON;
    case VDC_DOMAIN_PAYLOAD_REFMEM_DELTA:
    case VDC_DOMAIN_PAYLOAD_ACK_NACK_FENCE_QUALITY:
    default:
        return TDMA_SERVICE_PAYLOAD_CLASS_NONE;
    }
}

static uint32_t vdc_tdma_payload_timestamp_source(uint32_t source)
{
    switch ((tdma_service_timestamp_source_t)source) {
    case tdma_service_TIMESTAMP_SOURCE_SOFTWARE_US:
        return VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US;
    case tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK:
        return VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    case tdma_service_TIMESTAMP_SOURCE_NONE:
    default:
        return VDC_DOMAIN_TIMESTAMP_SOURCE_NONE;
    }
}

static uint32_t vdc_tdma_payload_timestamp_flags(uint32_t flags)
{
    uint32_t mapped = 0u;
    if ((flags & tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u) {
        mapped |= VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    }
    if ((flags & tdma_service_TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0u) {
        mapped |= VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    }
    return mapped;
}

static uint64_t join_u64(uint32_t lo, uint32_t hi)
{
    return ((uint64_t)hi << 32u) | (uint64_t)lo;
}

static uint32_t elapsed_u32(uint64_t start_ns, uint64_t done_ns)
{
    if (done_ns <= start_ns) {
        return 0u;
    }
    const uint64_t elapsed = done_ns - start_ns;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

bool vdc_tdma_payload_register(tdma_service_service_t *service)
{
    if (service == NULL) {
        return false;
    }

    const tdma_service_payload_binding_t sync_sample = {
        .used = 1u,
        .producer_id = 2u,
        .consumer_id = 2u,
        .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .max_payload_size = VDC_TDMA_PAYLOAD_FRAME_SIZE,
        .flags = 0u,
    };
    const tdma_service_payload_binding_t idle_beacon = {
        .used = 1u,
        .producer_id = 2u,
        .consumer_id = 2u,
        .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_IDLE_BEACON,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .max_payload_size = VDC_TDMA_PAYLOAD_FRAME_SIZE,
        .flags = 0u,
    };
    return tdma_service_register_payload(service, &sync_sample) &&
           tdma_service_register_payload(service, &idle_beacon);
}

bool vdc_tdma_payload_build_frame(
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_tdma_window_plan_t *plan,
    uint32_t payload_class,
    uint32_t frame_seq,
    const vdc_tdma_timestamp_evidence_t *timestamp,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size,
    vdc_tdma_frame_envelope_t *envelope,
    vdc_tdma_payload_status_t *status)
{
    vdc_tdma_timestamp_evidence_t local_timestamp;
    vdc_gate_result_t gate;

    if (frame_size != NULL) {
        *frame_size = 0u;
    }
    if (envelope != NULL) {
        memset(envelope, 0, sizeof(*envelope));
    }
    if (schedule == NULL || plan == NULL || frame == NULL ||
        frame_size == NULL || envelope == NULL ||
        frame_capacity < VDC_TDMA_PAYLOAD_FRAME_SIZE ||
        plan->valid == 0u || frame_seq == 0u) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_BAD_ARGUMENT,
                                    payload_class,
                                    0u,
                                    0u,
                                    NULL);
        return false;
    }
    if (!vdc_domain_schedule_validate(schedule) ||
        plan->schedule_crc32 != schedule->schedule_crc32 ||
        plan->schedule_epoch != schedule->schedule_epoch) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_BAD_WINDOW,
                                    payload_class,
                                    0u,
                                    0u,
                                    NULL);
        return false;
    }
    if (!vdc_tdma_payload_allowed(plan->window_class, payload_class)) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_BAD_PAYLOAD,
                                    payload_class,
                                    0u,
                                    0u,
                                    NULL);
        return false;
    }

    if (timestamp != NULL) {
        local_timestamp = *timestamp;
    } else {
        memset(&local_timestamp, 0, sizeof(local_timestamp));
        local_timestamp.sample_seq = frame_seq;
        local_timestamp.schedule_epoch = schedule->schedule_epoch;
        local_timestamp.slot_index = plan->slot_index;
        local_timestamp.source_slot_id = plan->source_slot_id;
        local_timestamp.reference_slot_id = plan->reference_slot_id;
        local_timestamp.payload_class = payload_class;
        local_timestamp.expected_window_start_ns = plan->window_start_ns;
        local_timestamp.arm_time_ns = plan->guard_start_ns;
        local_timestamp.start_time_ns = plan->window_start_ns;
        local_timestamp.observed_time_ns = plan->window_start_ns + plan->late_ns;
        local_timestamp.done_time_ns = local_timestamp.observed_time_ns;
        local_timestamp.apply_time_ns = local_timestamp.done_time_ns;
        local_timestamp.late_ns = plan->late_ns;
        local_timestamp.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US;
        local_timestamp.timestamp_resolution_ns = 1000u;
        local_timestamp.timestamp_flags =
            VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
        local_timestamp.schedule_crc32 = schedule->schedule_crc32;
        local_timestamp.sample_crc32 = 1u;
    }

    envelope->frame_version = VDC_DOMAIN_TDMA_FRAME_VERSION;
    envelope->frame_seq = frame_seq;
    envelope->schedule_epoch = schedule->schedule_epoch;
    envelope->slot_index = plan->slot_index;
    envelope->source_slot_id = plan->source_slot_id;
    envelope->reference_slot_id = plan->reference_slot_id;
    envelope->window_class = plan->window_class;
    envelope->payload_class = payload_class;
    envelope->window_start_ns = plan->window_start_ns;
    envelope->schedule_crc32 = schedule->schedule_crc32;
    envelope->quality_flags = local_timestamp.quality_flags;
    envelope->timestamp = local_timestamp;
    envelope->timestamp.sample_seq = frame_seq;
    envelope->timestamp.schedule_epoch = schedule->schedule_epoch;
    envelope->timestamp.slot_index = plan->slot_index;
    envelope->timestamp.source_slot_id = plan->source_slot_id;
    envelope->timestamp.reference_slot_id = plan->reference_slot_id;
    envelope->timestamp.payload_class = payload_class;
    envelope->timestamp.expected_window_start_ns = plan->window_start_ns;
    envelope->timestamp.schedule_crc32 = schedule->schedule_crc32;

    memset(frame, 0, VDC_TDMA_PAYLOAD_FRAME_SIZE);
    put_u32(frame, 0u, VDC_TDMA_PAYLOAD_MAGIC);
    put_u32(frame, 4u, VDC_TDMA_PAYLOAD_VERSION);
    put_u32(frame, 8u, VDC_TDMA_PAYLOAD_FRAME_SIZE);
    put_u32(frame, 12u, 0u);
    put_u32(frame, 16u, envelope->frame_version);
    put_u32(frame, 20u, envelope->frame_seq);
    put_u32(frame, 24u, envelope->schedule_epoch);
    put_u32(frame, 28u, envelope->slot_index);
    put_u32(frame, 32u, envelope->source_slot_id);
    put_u32(frame, 36u, envelope->reference_slot_id);
    put_u32(frame, 40u, envelope->window_class);
    put_u32(frame, 44u, envelope->payload_class);
    put_u64(frame, 48u, envelope->window_start_ns);
    put_u32(frame, 56u, envelope->schedule_crc32);
    put_u32(frame, 60u, 0u);
    put_u32(frame, 64u, 0u);
    put_u32(frame, 68u, envelope->quality_flags);
    put_u32(frame, 72u, envelope->timestamp.sample_seq);
    put_u32(frame, 76u, envelope->timestamp.schedule_epoch);
    put_u32(frame, 80u, envelope->timestamp.slot_index);
    put_u32(frame, 84u, envelope->timestamp.source_slot_id);
    put_u32(frame, 88u, envelope->timestamp.reference_slot_id);
    put_u32(frame, 92u, envelope->timestamp.payload_class);
    put_u64(frame, 96u, envelope->timestamp.expected_window_start_ns);
    put_u64(frame, 104u, envelope->timestamp.arm_time_ns);
    put_u64(frame, 112u, envelope->timestamp.start_time_ns);
    put_u64(frame, 120u, envelope->timestamp.observed_time_ns);
    put_u64(frame, 128u, envelope->timestamp.done_time_ns);
    put_u64(frame, 136u, envelope->timestamp.apply_time_ns);
    put_u32(frame, 144u, envelope->timestamp.late_ns);
    put_u32(frame, 148u, envelope->timestamp.jitter_ns);
    put_u32(frame, 152u, envelope->timestamp.delay_ns);
    put_i32(frame, 156u, envelope->timestamp.phase_error_ns);
    put_u32(frame, 160u, envelope->timestamp.timestamp_source);
    put_u32(frame, 164u, envelope->timestamp.timestamp_resolution_ns);
    put_u32(frame, 168u, envelope->timestamp.timestamp_flags);
    put_u32(frame, 172u, envelope->timestamp.quality_flags);

    const uint32_t payload_crc =
        vdc_tdma_payload_crc32(&frame[72u], VDC_TDMA_PAYLOAD_FRAME_SIZE - 72u);
    put_u32(frame, 64u, payload_crc);
    envelope->payload_crc32 = payload_crc;
    envelope->timestamp.sample_crc32 = payload_crc;

    const uint32_t frame_crc =
        vdc_tdma_payload_crc32(&frame[16u], VDC_TDMA_PAYLOAD_FRAME_SIZE - 16u);
    put_u32(frame, 12u, frame_crc);
    put_u32(frame, 60u, frame_crc);
    envelope->frame_crc32 = frame_crc;
    envelope->timestamp.frame_crc32 = frame_crc;

    if (!vdc_domain_validate_tdma_frame_envelope(schedule,
                                                 envelope,
                                                 false,
                                                 &gate)) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_GATE_REJECTED,
                                    payload_class,
                                    frame_crc,
                                    payload_crc,
                                    &gate);
        return false;
    }

    *frame_size = VDC_TDMA_PAYLOAD_FRAME_SIZE;
    vdc_tdma_payload_set_status(status,
                                VDC_TDMA_PAYLOAD_OK,
                                payload_class,
                                frame_crc,
                                payload_crc,
                                &gate);
    return true;
}

bool vdc_tdma_payload_parse_frame(
    const vdc_tdma_schedule_profile_t *schedule,
    const tdma_service_snapshot_t *tdma,
    const uint8_t *frame,
    size_t frame_size,
    bool require_dpll_eligible,
    vdc_tdma_frame_envelope_t *envelope,
    vdc_tdma_payload_status_t *status)
{
    vdc_gate_result_t gate;

    if (envelope != NULL) {
        memset(envelope, 0, sizeof(*envelope));
    }
    if (schedule == NULL || tdma == NULL || frame == NULL ||
        envelope == NULL || frame_size != VDC_TDMA_PAYLOAD_FRAME_SIZE) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_BAD_ARGUMENT,
                                    0u,
                                    0u,
                                    0u,
                                    NULL);
        return false;
    }
    if (get_u32(frame, 0u) != VDC_TDMA_PAYLOAD_MAGIC ||
        get_u32(frame, 4u) != VDC_TDMA_PAYLOAD_VERSION ||
        get_u32(frame, 8u) != VDC_TDMA_PAYLOAD_FRAME_SIZE) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_BAD_FRAME,
                                    0u,
                                    0u,
                                    0u,
                                    NULL);
        return false;
    }

    const uint32_t stored_frame_crc = get_u32(frame, 12u);
    uint8_t crc_frame[VDC_TDMA_PAYLOAD_FRAME_SIZE];
    memcpy(crc_frame, frame, VDC_TDMA_PAYLOAD_FRAME_SIZE);
    put_u32(crc_frame, 12u, 0u);
    put_u32(crc_frame, 60u, 0u);
    if (stored_frame_crc !=
        vdc_tdma_payload_crc32(&crc_frame[16u],
                               VDC_TDMA_PAYLOAD_FRAME_SIZE - 16u)) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_CRC_MISMATCH,
                                    get_u32(frame, 44u),
                                    stored_frame_crc,
                                    get_u32(frame, 64u),
                                    NULL);
        return false;
    }
    if (get_u32(frame, 64u) !=
        vdc_tdma_payload_crc32(&frame[72u],
                               VDC_TDMA_PAYLOAD_FRAME_SIZE - 72u)) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_CRC_MISMATCH,
                                    get_u32(frame, 44u),
                                    stored_frame_crc,
                                    get_u32(frame, 64u),
                                    NULL);
        return false;
    }

    envelope->frame_version = get_u32(frame, 16u);
    envelope->frame_seq = get_u32(frame, 20u);
    envelope->schedule_epoch = get_u32(frame, 24u);
    envelope->slot_index = get_u32(frame, 28u);
    envelope->source_slot_id = get_u32(frame, 32u);
    envelope->reference_slot_id = get_u32(frame, 36u);
    envelope->window_class = get_u32(frame, 40u);
    envelope->payload_class = get_u32(frame, 44u);
    envelope->window_start_ns = get_u64(frame, 48u);
    envelope->schedule_crc32 = get_u32(frame, 56u);
    envelope->frame_crc32 = get_u32(frame, 60u);
    envelope->payload_crc32 = get_u32(frame, 64u);
    envelope->quality_flags = get_u32(frame, 68u);
    envelope->timestamp.sample_seq = get_u32(frame, 72u);
    envelope->timestamp.schedule_epoch = get_u32(frame, 76u);
    envelope->timestamp.slot_index = get_u32(frame, 80u);
    envelope->timestamp.source_slot_id = get_u32(frame, 84u);
    envelope->timestamp.reference_slot_id = get_u32(frame, 88u);
    envelope->timestamp.payload_class = get_u32(frame, 92u);
    envelope->timestamp.expected_window_start_ns = get_u64(frame, 96u);
    envelope->timestamp.arm_time_ns = get_u64(frame, 104u);
    envelope->timestamp.start_time_ns = get_u64(frame, 112u);
    envelope->timestamp.observed_time_ns = get_u64(frame, 120u);
    envelope->timestamp.done_time_ns = get_u64(frame, 128u);
    envelope->timestamp.apply_time_ns = get_u64(frame, 136u);
    envelope->timestamp.late_ns = get_u32(frame, 144u);
    envelope->timestamp.jitter_ns = get_u32(frame, 148u);
    envelope->timestamp.delay_ns = get_u32(frame, 152u);
    envelope->timestamp.phase_error_ns = get_i32(frame, 156u);
    envelope->timestamp.timestamp_source = get_u32(frame, 160u);
    envelope->timestamp.timestamp_resolution_ns = get_u32(frame, 164u);
    envelope->timestamp.timestamp_flags = get_u32(frame, 168u);
    envelope->timestamp.quality_flags = get_u32(frame, 172u);
    envelope->timestamp.schedule_crc32 = envelope->schedule_crc32;
    envelope->timestamp.frame_crc32 = envelope->frame_crc32;
    envelope->timestamp.sample_crc32 = envelope->payload_crc32;

    if (tdma->completed_seq == 0u ||
        tdma->completed_seq != tdma->intent_seq ||
        tdma->last_result != tdma_service_RESULT_FRAME_READY ||
        tdma->payload_class !=
            vdc_tdma_payload_map_to_tdma(envelope->payload_class)) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_TDMA_MISMATCH,
                                    envelope->payload_class,
                                    envelope->frame_crc32,
                                    envelope->payload_crc32,
                                    NULL);
        return false;
    }

    const uint64_t arm_ns =
        join_u64(tdma->core1_arm_time_ns_lo, tdma->core1_arm_time_ns_hi);
    const uint64_t start_ns =
        join_u64(tdma->core1_start_time_ns_lo, tdma->core1_start_time_ns_hi);
    const uint64_t done_ns =
        join_u64(tdma->core1_done_time_ns_lo, tdma->core1_done_time_ns_hi);
    if (done_ns != 0u) {
        envelope->timestamp.arm_time_ns = arm_ns;
        envelope->timestamp.start_time_ns = start_ns;
        envelope->timestamp.observed_time_ns = start_ns;
        envelope->timestamp.done_time_ns = done_ns;
        envelope->timestamp.apply_time_ns = done_ns;
        envelope->timestamp.delay_ns = elapsed_u32(start_ns, done_ns);
        envelope->timestamp.late_ns =
            start_ns > envelope->window_start_ns
                ? elapsed_u32(envelope->window_start_ns, start_ns)
                : 0u;
        envelope->timestamp.timestamp_source =
            vdc_tdma_payload_timestamp_source(tdma->timestamp_source);
        envelope->timestamp.timestamp_resolution_ns =
            tdma->timestamp_resolution_ns;
        envelope->timestamp.timestamp_flags =
            vdc_tdma_payload_timestamp_flags(tdma->timestamp_flags);
    } else if (require_dpll_eligible) {
        vdc_tdma_payload_gate_reject(&gate,
                                     VDC_DOMAIN_GATE_TIMESTAMP_NOT_ELIGIBLE,
                                     envelope->source_slot_id,
                                     envelope->frame_seq);
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_GATE_REJECTED,
                                    envelope->payload_class,
                                    envelope->frame_crc32,
                                    envelope->payload_crc32,
                                    &gate);
        return false;
    }

    if (!vdc_domain_validate_tdma_frame_envelope(schedule,
                                                 envelope,
                                                 require_dpll_eligible,
                                                 &gate)) {
        vdc_tdma_payload_set_status(status,
                                    VDC_TDMA_PAYLOAD_GATE_REJECTED,
                                    envelope->payload_class,
                                    envelope->frame_crc32,
                                    envelope->payload_crc32,
                                    &gate);
        return false;
    }

    vdc_tdma_payload_set_status(status,
                                VDC_TDMA_PAYLOAD_OK,
                                envelope->payload_class,
                                envelope->frame_crc32,
                                envelope->payload_crc32,
                                &gate);
    return true;
}
