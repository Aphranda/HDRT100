#include "tdma_transport_frame.h"

#include <string.h>

enum {
    TDMA_TRANSPORT_OFFSET_MAGIC = 0u,
    TDMA_TRANSPORT_OFFSET_VERSION = 2u,
    TDMA_TRANSPORT_OFFSET_FRAME_CLASS = 3u,
    TDMA_TRANSPORT_OFFSET_PACKET_SIZE = 4u,
    TDMA_TRANSPORT_OFFSET_HEADER_SIZE = 6u,
    TDMA_TRANSPORT_OFFSET_ORIGIN_SLOT = 7u,
    TDMA_TRANSPORT_OFFSET_SEQUENCE = 8u,
    TDMA_TRANSPORT_OFFSET_PAYLOAD_CLASS = 12u,
    TDMA_TRANSPORT_OFFSET_FLAGS = 13u,
    TDMA_TRANSPORT_OFFSET_HOP_COUNT = 14u,
    TDMA_TRANSPORT_OFFSET_HOP_LIMIT = 15u,
    TDMA_TRANSPORT_OFFSET_SCHEDULE_CRC = 16u,
    TDMA_TRANSPORT_OFFSET_RING_PROFILE_CRC = 20u,
    TDMA_TRANSPORT_OFFSET_IDENTITY_CRC = 24u,
    TDMA_TRANSPORT_OFFSET_TRANSPORT_CRC = 28u,
};

static void tdma_transport_set_result(tdma_transport_result_t *result,
                                      tdma_transport_result_t value)
{
    if (result != NULL) {
        *result = value;
    }
}

static void tdma_transport_write_le16(uint8_t *data,
                                      size_t offset,
                                      uint16_t value)
{
    data[offset] = (uint8_t)(value & 0xFFu);
    data[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
}

static uint16_t tdma_transport_read_le16(const uint8_t *data, size_t offset)
{
    return (uint16_t)((uint16_t)data[offset] |
                      ((uint16_t)data[offset + 1u] << 8u));
}

static void tdma_transport_write_le32(uint8_t *data,
                                      size_t offset,
                                      uint32_t value)
{
    data[offset] = (uint8_t)(value & 0xFFu);
    data[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
    data[offset + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
    data[offset + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint32_t tdma_transport_read_le32(const uint8_t *data, size_t offset)
{
    return (uint32_t)data[offset] |
           ((uint32_t)data[offset + 1u] << 8u) |
           ((uint32_t)data[offset + 2u] << 16u) |
           ((uint32_t)data[offset + 3u] << 24u);
}

static uint32_t tdma_transport_crc32_update(uint32_t crc,
                                            const uint8_t *data,
                                            size_t size)
{
    for (size_t i = 0u; i < size; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t tdma_transport_crc32_update_u8(uint32_t crc, uint32_t value)
{
    const uint8_t encoded = (uint8_t)value;
    return tdma_transport_crc32_update(crc, &encoded, sizeof(encoded));
}

static uint32_t tdma_transport_crc32_update_le16(uint32_t crc, uint32_t value)
{
    uint8_t encoded[2];
    tdma_transport_write_le16(encoded, 0u, (uint16_t)value);
    return tdma_transport_crc32_update(crc, encoded, sizeof(encoded));
}

static uint32_t tdma_transport_crc32_update_le32(uint32_t crc, uint32_t value)
{
    uint8_t encoded[4];
    tdma_transport_write_le32(encoded, 0u, value);
    return tdma_transport_crc32_update(crc, encoded, sizeof(encoded));
}

static bool tdma_transport_frame_class_valid(uint32_t frame_class)
{
    return frame_class == TDMA_TRANSPORT_FRAME_CLASS_SHORT ||
           frame_class == TDMA_TRANSPORT_FRAME_CLASS_LONG;
}

static size_t tdma_transport_packet_capacity(uint32_t frame_class)
{
    if (frame_class == TDMA_TRANSPORT_FRAME_CLASS_SHORT) {
        return TDMA_TRANSPORT_SHORT_PACKET_MAX;
    }
    if (frame_class == TDMA_TRANSPORT_FRAME_CLASS_LONG) {
        return TDMA_TRANSPORT_LONG_PACKET_MAX;
    }
    return 0u;
}

static uint32_t tdma_transport_identity_crc32(
    const tdma_transport_frame_view_t *view)
{
    uint32_t crc = UINT32_MAX;
    crc = tdma_transport_crc32_update_le16(crc,
                                           TDMA_TRANSPORT_FRAME_MAGIC);
    crc = tdma_transport_crc32_update_u8(crc,
                                         TDMA_TRANSPORT_FRAME_VERSION);
    crc = tdma_transport_crc32_update_u8(crc, view->frame_class);
    crc = tdma_transport_crc32_update_le16(crc, view->packet_size);
    crc = tdma_transport_crc32_update_u8(
        crc,
        TDMA_TRANSPORT_FRAME_HEADER_SIZE);
    crc = tdma_transport_crc32_update_u8(crc, view->origin_slot_id);
    crc = tdma_transport_crc32_update_le32(crc, view->transport_sequence);
    crc = tdma_transport_crc32_update_u8(crc, view->payload_class);
    crc = tdma_transport_crc32_update_u8(crc, view->flags);
    crc = tdma_transport_crc32_update_u8(crc, view->hop_limit);
    crc = tdma_transport_crc32_update_le32(crc, view->schedule_crc32);
    crc = tdma_transport_crc32_update_le32(crc, view->ring_profile_crc32);
    return ~crc;
}

static uint32_t tdma_transport_packet_crc32(const uint8_t *packet,
                                            size_t packet_size)
{
    static const uint8_t zero_crc[4] = {0u, 0u, 0u, 0u};
    uint32_t crc = UINT32_MAX;
    crc = tdma_transport_crc32_update(
        crc,
        packet,
        TDMA_TRANSPORT_OFFSET_TRANSPORT_CRC);
    crc = tdma_transport_crc32_update(crc, zero_crc, sizeof(zero_crc));
    if ((packet[TDMA_TRANSPORT_OFFSET_FLAGS] &
         TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) != 0u) {
        /* Mutable process-image bytes are replaced by several Nodes while
         * the frame is physically in flight. Their mailbox-level CRC owns
         * payload integrity; the transport CRC protects the stable routing
         * header and therefore remains valid between overlay points. */
        return ~crc;
    }
    crc = tdma_transport_crc32_update(
        crc,
        packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
        packet_size - TDMA_TRANSPORT_FRAME_HEADER_SIZE);
    return ~crc;
}

static bool tdma_transport_build_valid(
    const tdma_transport_frame_build_t *build)
{
    return build != NULL &&
           tdma_transport_frame_class_valid(build->frame_class) &&
           build->origin_slot_id < TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT &&
           build->payload_class != 0u && build->payload_class <= UINT8_MAX &&
           build->flags <= UINT8_MAX && build->hop_limit != 0u &&
           build->hop_limit <= TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT &&
           ((build->flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) == 0u ||
            build->frame_class == TDMA_TRANSPORT_FRAME_CLASS_SHORT) &&
           (build->payload_size == 0u || build->payload != NULL);
}

size_t tdma_transport_frame_packet_size(uint32_t frame_class,
                                        size_t payload_size)
{
    const size_t capacity = tdma_transport_packet_capacity(frame_class);
    if (capacity == 0u ||
        payload_size > capacity - TDMA_TRANSPORT_FRAME_HEADER_SIZE) {
        return 0u;
    }
    return TDMA_TRANSPORT_FRAME_HEADER_SIZE + payload_size;
}

bool tdma_transport_frame_encode(const tdma_transport_frame_build_t *build,
                                 uint8_t *packet,
                                 size_t packet_capacity,
                                 size_t *packet_size,
                                 tdma_transport_result_t *result)
{
    if (!tdma_transport_build_valid(build) || packet == NULL ||
        packet_size == NULL) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_ARGUMENT);
        return false;
    }

    const size_t required_size = tdma_transport_frame_packet_size(
        build->frame_class,
        build->payload_size);
    if (required_size == 0u || packet_capacity < required_size) {
        tdma_transport_set_result(result,
                                  TDMA_TRANSPORT_CAPACITY_REJECTED);
        return false;
    }

    memset(packet, 0, required_size);
    tdma_transport_write_le16(packet,
                              TDMA_TRANSPORT_OFFSET_MAGIC,
                              TDMA_TRANSPORT_FRAME_MAGIC);
    packet[TDMA_TRANSPORT_OFFSET_VERSION] = TDMA_TRANSPORT_FRAME_VERSION;
    packet[TDMA_TRANSPORT_OFFSET_FRAME_CLASS] = (uint8_t)build->frame_class;
    tdma_transport_write_le16(packet,
                              TDMA_TRANSPORT_OFFSET_PACKET_SIZE,
                              (uint16_t)required_size);
    packet[TDMA_TRANSPORT_OFFSET_HEADER_SIZE] =
        TDMA_TRANSPORT_FRAME_HEADER_SIZE;
    packet[TDMA_TRANSPORT_OFFSET_ORIGIN_SLOT] =
        (uint8_t)build->origin_slot_id;
    tdma_transport_write_le32(packet,
                              TDMA_TRANSPORT_OFFSET_SEQUENCE,
                              build->transport_sequence);
    packet[TDMA_TRANSPORT_OFFSET_PAYLOAD_CLASS] =
        (uint8_t)build->payload_class;
    packet[TDMA_TRANSPORT_OFFSET_FLAGS] = (uint8_t)build->flags;
    packet[TDMA_TRANSPORT_OFFSET_HOP_LIMIT] = (uint8_t)build->hop_limit;
    tdma_transport_write_le32(packet,
                              TDMA_TRANSPORT_OFFSET_SCHEDULE_CRC,
                              build->schedule_crc32);
    tdma_transport_write_le32(packet,
                              TDMA_TRANSPORT_OFFSET_RING_PROFILE_CRC,
                              build->ring_profile_crc32);
    if (build->payload_size != 0u) {
        memcpy(packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
               build->payload,
               build->payload_size);
    }

    const tdma_transport_frame_view_t view = {
        .frame_class = build->frame_class,
        .packet_size = (uint32_t)required_size,
        .origin_slot_id = build->origin_slot_id,
        .transport_sequence = build->transport_sequence,
        .payload_class = build->payload_class,
        .flags = build->flags,
        .schedule_crc32 = build->schedule_crc32,
        .ring_profile_crc32 = build->ring_profile_crc32,
        .hop_limit = build->hop_limit,
        .payload_size = (uint32_t)build->payload_size,
        .payload = packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
    };
    tdma_transport_write_le32(packet,
                              TDMA_TRANSPORT_OFFSET_IDENTITY_CRC,
                              tdma_transport_identity_crc32(&view));
    tdma_transport_write_le32(
        packet,
        TDMA_TRANSPORT_OFFSET_TRANSPORT_CRC,
        tdma_transport_packet_crc32(packet, required_size));

    *packet_size = required_size;
    tdma_transport_set_result(result, TDMA_TRANSPORT_OK);
    return true;
}

bool tdma_transport_frame_decode(const uint8_t *packet,
                                 size_t packet_size,
                                 tdma_transport_frame_view_t *view,
                                 tdma_transport_result_t *result)
{
    if (packet == NULL || view == NULL) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_ARGUMENT);
        return false;
    }
    memset(view, 0, sizeof(*view));
    if (packet_size < TDMA_TRANSPORT_FRAME_HEADER_SIZE) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_HEADER);
        return false;
    }
    if (tdma_transport_read_le16(packet, TDMA_TRANSPORT_OFFSET_MAGIC) !=
        TDMA_TRANSPORT_FRAME_MAGIC) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_MAGIC);
        return false;
    }
    if (packet[TDMA_TRANSPORT_OFFSET_VERSION] !=
        TDMA_TRANSPORT_FRAME_VERSION) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_VERSION);
        return false;
    }
    if (packet[TDMA_TRANSPORT_OFFSET_HEADER_SIZE] !=
        TDMA_TRANSPORT_FRAME_HEADER_SIZE) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_HEADER);
        return false;
    }

    view->frame_class = packet[TDMA_TRANSPORT_OFFSET_FRAME_CLASS];
    if (!tdma_transport_frame_class_valid(view->frame_class)) {
        tdma_transport_set_result(result,
                                  TDMA_TRANSPORT_FRAME_CLASS_REJECTED);
        return false;
    }
    view->packet_size =
        tdma_transport_read_le16(packet, TDMA_TRANSPORT_OFFSET_PACKET_SIZE);
    if (view->packet_size != packet_size ||
        packet_size > tdma_transport_packet_capacity(view->frame_class)) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_PACKET_SIZE);
        return false;
    }

    view->origin_slot_id = packet[TDMA_TRANSPORT_OFFSET_ORIGIN_SLOT];
    view->transport_sequence =
        tdma_transport_read_le32(packet, TDMA_TRANSPORT_OFFSET_SEQUENCE);
    view->payload_class = packet[TDMA_TRANSPORT_OFFSET_PAYLOAD_CLASS];
    view->flags = packet[TDMA_TRANSPORT_OFFSET_FLAGS];
    view->hop_count = packet[TDMA_TRANSPORT_OFFSET_HOP_COUNT];
    view->hop_limit = packet[TDMA_TRANSPORT_OFFSET_HOP_LIMIT];
    view->schedule_crc32 =
        tdma_transport_read_le32(packet, TDMA_TRANSPORT_OFFSET_SCHEDULE_CRC);
    view->ring_profile_crc32 = tdma_transport_read_le32(
        packet,
        TDMA_TRANSPORT_OFFSET_RING_PROFILE_CRC);
    view->identity_crc32 =
        tdma_transport_read_le32(packet, TDMA_TRANSPORT_OFFSET_IDENTITY_CRC);
    view->transport_crc32 =
        tdma_transport_read_le32(packet, TDMA_TRANSPORT_OFFSET_TRANSPORT_CRC);
    view->payload_size =
        (uint32_t)(packet_size - TDMA_TRANSPORT_FRAME_HEADER_SIZE);
    view->payload = packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE;

    if (view->origin_slot_id >= TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT ||
        view->payload_class == 0u || view->hop_limit == 0u ||
        view->hop_limit > TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT ||
        view->hop_count > view->hop_limit ||
        ((view->flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) != 0u &&
         view->frame_class != TDMA_TRANSPORT_FRAME_CLASS_SHORT)) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_ROUTE);
        return false;
    }
    if (tdma_transport_packet_crc32(packet, packet_size) !=
        view->transport_crc32) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_CRC_MISMATCH);
        return false;
    }
    if (tdma_transport_identity_crc32(view) != view->identity_crc32) {
        tdma_transport_set_result(result,
                                  TDMA_TRANSPORT_IDENTITY_CRC_MISMATCH);
        return false;
    }

    tdma_transport_set_result(result, TDMA_TRANSPORT_OK);
    return true;
}

bool tdma_transport_frame_patch_flight_payload(
    uint8_t *packet,
    size_t packet_size,
    size_t payload_offset,
    const uint8_t *data,
    size_t data_size,
    tdma_transport_result_t *result)
{
    tdma_transport_frame_view_t view;
    if (packet == NULL || (data_size != 0u && data == NULL)) {
        tdma_transport_set_result(result, TDMA_TRANSPORT_BAD_ARGUMENT);
        return false;
    }
    if (!tdma_transport_frame_decode(packet, packet_size, &view, result)) {
        return false;
    }
    if (view.frame_class != TDMA_TRANSPORT_FRAME_CLASS_SHORT ||
        (view.flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) == 0u) {
        tdma_transport_set_result(result,
                                  TDMA_TRANSPORT_FRAME_CLASS_REJECTED);
        return false;
    }
    if (payload_offset > view.payload_size ||
        data_size > view.payload_size - payload_offset) {
        tdma_transport_set_result(result,
                                  TDMA_TRANSPORT_CAPACITY_REJECTED);
        return false;
    }

    if (data_size != 0u) {
        memcpy(packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE + payload_offset,
               data,
               data_size);
    }
    tdma_transport_write_le32(packet,
                              TDMA_TRANSPORT_OFFSET_TRANSPORT_CRC,
                              0u);
    tdma_transport_write_le32(
        packet,
        TDMA_TRANSPORT_OFFSET_TRANSPORT_CRC,
        tdma_transport_packet_crc32(packet, packet_size));
    tdma_transport_set_result(result, TDMA_TRANSPORT_OK);
    return true;
}

tdma_transport_route_t tdma_transport_frame_route(
    const tdma_transport_frame_view_t *view,
    uint32_t local_slot_id)
{
    if (view == NULL ||
        local_slot_id >= TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT ||
        view->hop_count > view->hop_limit) {
        return TDMA_TRANSPORT_ROUTE_DROP;
    }
    if (view->origin_slot_id == local_slot_id) {
        return view->hop_count == 0u ? TDMA_TRANSPORT_ROUTE_LOCAL_TX
                                    : TDMA_TRANSPORT_ROUTE_FEEDBACK;
    }
    if (view->hop_count >= view->hop_limit) {
        return TDMA_TRANSPORT_ROUTE_DROP;
    }
    return TDMA_TRANSPORT_ROUTE_FORWARD;
}

bool tdma_transport_frame_advance_hop(uint8_t *packet,
                                      size_t packet_size,
                                      tdma_transport_result_t *result)
{
    tdma_transport_frame_view_t view;
    if (!tdma_transport_frame_decode(packet, packet_size, &view, result)) {
        return false;
    }
    if (view.hop_count >= view.hop_limit) {
        tdma_transport_set_result(result,
                                  TDMA_TRANSPORT_HOP_LIMIT_REACHED);
        return false;
    }

    packet[TDMA_TRANSPORT_OFFSET_HOP_COUNT] = (uint8_t)(view.hop_count + 1u);
    tdma_transport_write_le32(packet,
                              TDMA_TRANSPORT_OFFSET_TRANSPORT_CRC,
                              0u);
    tdma_transport_write_le32(
        packet,
        TDMA_TRANSPORT_OFFSET_TRANSPORT_CRC,
        tdma_transport_packet_crc32(packet, packet_size));
    tdma_transport_set_result(result, TDMA_TRANSPORT_OK);
    return true;
}
