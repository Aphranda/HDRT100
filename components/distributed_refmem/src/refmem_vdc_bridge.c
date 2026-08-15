#include "refmem_vdc_bridge.h"

#include <string.h>

#include "ota_crc32.h"
#include "refmem_sync_frame.h"

static uint64_t refmem_vdc_bridge_join_u64(uint32_t lo, uint32_t hi)
{
    return ((uint64_t)hi << 32u) | (uint64_t)lo;
}

static uint32_t refmem_vdc_bridge_frame_crc32(const uint8_t *frame,
                                              size_t frame_size)
{
    uint32_t crc = 0xFFFFFFFFu;
    const uint32_t size32 = frame_size > UINT32_MAX ? UINT32_MAX : (uint32_t)frame_size;
    crc = ota_crc32_update(crc, (const uint8_t *)&size32, sizeof(size32));
    if (frame != NULL && frame_size != 0u) {
        crc = ota_crc32_update(crc, frame, frame_size);
    }
    return crc ^ 0xFFFFFFFFu;
}

static void refmem_vdc_bridge_set_status(refmem_vdc_bridge_status_t *status,
                                         refmem_vdc_bridge_result_t result,
                                         const refmem_sync_frame_header_t *header,
                                         uint32_t payload_class,
                                         uint32_t frame_crc32,
                                         const vdc_gate_result_t *gate)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->result = result;
    status->payload_class = payload_class;
    status->frame_crc32 = frame_crc32;
    if (header != NULL) {
        status->frame_type = header->frame_type;
        status->source_slot = header->source_slot;
        status->payload_crc32 = header->payload_crc32;
    }
    if (gate != NULL) {
        status->gate = *gate;
    }
}

static bool refmem_vdc_bridge_payload_class(uint8_t frame_type,
                                            uint32_t *payload_class)
{
    if (payload_class == NULL) {
        return false;
    }

    switch ((refmem_sync_frame_type_t)frame_type) {
    case REFMEM_SYNC_FRAME_DELTA:
        *payload_class = VDC_DOMAIN_PAYLOAD_REFMEM_DELTA;
        return true;
    case REFMEM_SYNC_FRAME_ACK_NACK:
    case REFMEM_SYNC_FRAME_FENCE:
    case REFMEM_SYNC_FRAME_QUALITY:
        *payload_class = VDC_DOMAIN_PAYLOAD_ACK_NACK_FENCE_QUALITY;
        return true;
    case REFMEM_SYNC_FRAME_HELLO:
    case REFMEM_SYNC_FRAME_EPOCH:
    case REFMEM_SYNC_FRAME_COMMAND:
    default:
        return false;
    }
}

static uint64_t refmem_vdc_bridge_window_start(uint64_t observed_ns,
                                               uint32_t period_ns,
                                               uint32_t offset_ns)
{
    if (period_ns == 0u) {
        return 0u;
    }
    if (observed_ns < offset_ns) {
        return offset_ns;
    }
    const uint64_t cycle = (observed_ns - offset_ns) / period_ns;
    return cycle * period_ns + offset_ns;
}

static uint32_t refmem_vdc_bridge_timestamp_source(uint32_t source)
{
    switch ((refmem_realtime_tdma_timestamp_source_t)source) {
    case REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_SOFTWARE_US:
        return VDC_DOMAIN_TIMESTAMP_SOURCE_SOFTWARE_US;
    case REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_HARDWARE_TICK:
        return VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    case REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_NONE:
    default:
        return VDC_DOMAIN_TIMESTAMP_SOURCE_NONE;
    }
}

static uint32_t refmem_vdc_bridge_timestamp_flags(uint32_t flags)
{
    uint32_t mapped = 0u;
    if ((flags & REFMEM_REALTIME_TDMA_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u) {
        mapped |= VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    }
    if ((flags & REFMEM_REALTIME_TDMA_TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0u) {
        mapped |= VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    }
    return mapped;
}

bool refmem_vdc_bridge_build_data_envelope(
    const vdc_tdma_schedule_profile_t *schedule,
    const refmem_realtime_tdma_snapshot_t *tdma,
    const uint8_t *frame,
    size_t frame_size,
    vdc_tdma_frame_envelope_t *envelope,
    refmem_vdc_bridge_status_t *status)
{
    refmem_sync_frame_header_t header;
    const uint8_t *payload = NULL;
    uint16_t payload_size = 0u;
    uint32_t payload_class = 0u;
    vdc_gate_result_t gate;

    if (envelope != NULL) {
        memset(envelope, 0, sizeof(*envelope));
    }
    if (schedule == NULL || tdma == NULL || frame == NULL ||
        frame_size == 0u || envelope == NULL) {
        refmem_vdc_bridge_set_status(status,
                                     REFMEM_VDC_BRIDGE_BAD_ARGUMENT,
                                     NULL,
                                     0u,
                                     0u,
                                     NULL);
        return false;
    }
    if (!vdc_domain_schedule_validate(schedule)) {
        refmem_vdc_bridge_set_status(status,
                                     REFMEM_VDC_BRIDGE_BAD_SCHEDULE,
                                     NULL,
                                     0u,
                                     0u,
                                     NULL);
        return false;
    }
    if (tdma->completed_seq == 0u ||
        tdma->completed_seq != tdma->intent_seq ||
        tdma->last_result != REFMEM_REALTIME_TDMA_RESULT_FRAME_READY) {
        refmem_vdc_bridge_set_status(status,
                                     REFMEM_VDC_BRIDGE_BAD_TDMA_SNAPSHOT,
                                     NULL,
                                     0u,
                                     0u,
                                     NULL);
        return false;
    }
    if (refmem_sync_frame_validate(frame,
                                   frame_size,
                                   &header,
                                   &payload,
                                   &payload_size) != REFMEM_SYNC_FRAME_OK) {
        refmem_vdc_bridge_set_status(status,
                                     REFMEM_VDC_BRIDGE_BAD_REFMEM_FRAME,
                                     NULL,
                                     0u,
                                     0u,
                                     NULL);
        return false;
    }
    (void)payload;
    (void)payload_size;

    if (!refmem_vdc_bridge_payload_class(header.frame_type, &payload_class)) {
        refmem_vdc_bridge_set_status(status,
                                     REFMEM_VDC_BRIDGE_UNSUPPORTED_FRAME_TYPE,
                                     &header,
                                     0u,
                                     0u,
                                     NULL);
        return false;
    }

    const uint64_t arm_ns =
        refmem_vdc_bridge_join_u64(tdma->core1_arm_time_ns_lo,
                                   tdma->core1_arm_time_ns_hi);
    const uint64_t start_ns =
        refmem_vdc_bridge_join_u64(tdma->core1_start_time_ns_lo,
                                   tdma->core1_start_time_ns_hi);
    const uint64_t done_ns =
        refmem_vdc_bridge_join_u64(tdma->core1_done_time_ns_lo,
                                   tdma->core1_done_time_ns_hi);
    if (start_ns == 0u || done_ns == 0u) {
        refmem_vdc_bridge_set_status(status,
                                     REFMEM_VDC_BRIDGE_BAD_TDMA_SNAPSHOT,
                                     &header,
                                     payload_class,
                                     0u,
                                     NULL);
        return false;
    }

    const uint32_t frame_crc32 = refmem_vdc_bridge_frame_crc32(frame, frame_size);
    const uint64_t window_start_ns =
        refmem_vdc_bridge_window_start(start_ns,
                                       schedule->period_ns,
                                       schedule->refmem_data_window_offset_ns);
    const uint64_t elapsed_ns = done_ns > start_ns ? done_ns - start_ns : 0u;
    const uint64_t late_ns = start_ns > window_start_ns ? start_ns - window_start_ns : 0u;

    envelope->frame_version = VDC_DOMAIN_TDMA_FRAME_VERSION;
    envelope->frame_seq = header.seq32;
    envelope->schedule_epoch = schedule->schedule_epoch;
    envelope->slot_index = header.source_slot;
    envelope->source_slot_id = header.source_slot;
    envelope->reference_slot_id = schedule->reference_slot_id;
    envelope->window_class = VDC_DOMAIN_WINDOW_REFMEM_DATA;
    envelope->payload_class = payload_class;
    envelope->window_start_ns = window_start_ns;
    envelope->schedule_crc32 = schedule->schedule_crc32;
    envelope->frame_crc32 = frame_crc32;
    envelope->payload_crc32 = header.payload_crc32;
    envelope->quality_flags = 0u;

    envelope->timestamp.sample_seq = header.seq32;
    envelope->timestamp.schedule_epoch = schedule->schedule_epoch;
    envelope->timestamp.slot_index = header.source_slot;
    envelope->timestamp.source_slot_id = header.source_slot;
    envelope->timestamp.reference_slot_id = schedule->reference_slot_id;
    envelope->timestamp.payload_class = payload_class;
    envelope->timestamp.expected_window_start_ns = window_start_ns;
    envelope->timestamp.arm_time_ns = arm_ns;
    envelope->timestamp.start_time_ns = start_ns;
    envelope->timestamp.observed_time_ns = start_ns;
    envelope->timestamp.done_time_ns = done_ns;
    envelope->timestamp.apply_time_ns = done_ns;
    envelope->timestamp.late_ns =
        late_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)late_ns;
    envelope->timestamp.jitter_ns = 0u;
    envelope->timestamp.delay_ns =
        elapsed_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ns;
    envelope->timestamp.phase_error_ns = 0;
    envelope->timestamp.timestamp_source =
        refmem_vdc_bridge_timestamp_source(tdma->timestamp_source);
    envelope->timestamp.timestamp_resolution_ns = tdma->timestamp_resolution_ns;
    envelope->timestamp.timestamp_flags =
        refmem_vdc_bridge_timestamp_flags(tdma->timestamp_flags);
    envelope->timestamp.schedule_crc32 = schedule->schedule_crc32;
    envelope->timestamp.frame_crc32 = frame_crc32;
    envelope->timestamp.sample_crc32 = header.payload_crc32;
    envelope->timestamp.quality_flags = 0u;

    if (!vdc_domain_validate_tdma_frame_envelope(schedule,
                                                 envelope,
                                                 false,
                                                 &gate)) {
        refmem_vdc_bridge_set_status(status,
                                     REFMEM_VDC_BRIDGE_VDC_GATE_REJECTED,
                                     &header,
                                     payload_class,
                                     frame_crc32,
                                     &gate);
        return false;
    }

    refmem_vdc_bridge_set_status(status,
                                 REFMEM_VDC_BRIDGE_OK,
                                 &header,
                                 payload_class,
                                 frame_crc32,
                                 &gate);
    return true;
}
