#ifndef TDMA_TRANSPORT_FRAME_H
#define TDMA_TRANSPORT_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TDMA_TRANSPORT_FRAME_MAGIC 0x4454u
#define TDMA_TRANSPORT_FRAME_VERSION 1u
#define TDMA_TRANSPORT_FRAME_HEADER_SIZE 32u
#define TDMA_TRANSPORT_SHORT_PACKET_MAX 292u
#define TDMA_TRANSPORT_LONG_PACKET_MAX 1024u
#define TDMA_TRANSPORT_SHORT_PAYLOAD_MAX \
    (TDMA_TRANSPORT_SHORT_PACKET_MAX - TDMA_TRANSPORT_FRAME_HEADER_SIZE)
#define TDMA_TRANSPORT_LONG_PAYLOAD_MAX \
    (TDMA_TRANSPORT_LONG_PACKET_MAX - TDMA_TRANSPORT_FRAME_HEADER_SIZE)
#define TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT 8u

#define TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK 0x01u
#define TDMA_TRANSPORT_FLAG_IDLE_BEACON 0x02u
#define TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE 0x04u

typedef enum {
    TDMA_TRANSPORT_FRAME_CLASS_SHORT = 1u,
    TDMA_TRANSPORT_FRAME_CLASS_LONG = 2u,
} tdma_transport_frame_class_t;

typedef enum {
    TDMA_TRANSPORT_OK = 0u,
    TDMA_TRANSPORT_BAD_ARGUMENT = 1u,
    TDMA_TRANSPORT_CAPACITY_REJECTED = 2u,
    TDMA_TRANSPORT_BAD_MAGIC = 3u,
    TDMA_TRANSPORT_BAD_VERSION = 4u,
    TDMA_TRANSPORT_BAD_HEADER = 5u,
    TDMA_TRANSPORT_BAD_PACKET_SIZE = 6u,
    TDMA_TRANSPORT_BAD_ROUTE = 7u,
    TDMA_TRANSPORT_IDENTITY_CRC_MISMATCH = 8u,
    TDMA_TRANSPORT_CRC_MISMATCH = 9u,
    TDMA_TRANSPORT_HOP_LIMIT_REACHED = 10u,
    TDMA_TRANSPORT_FRAME_CLASS_REJECTED = 11u,
} tdma_transport_result_t;

typedef enum {
    TDMA_TRANSPORT_ROUTE_LOCAL_TX = 0u,
    TDMA_TRANSPORT_ROUTE_FORWARD = 1u,
    TDMA_TRANSPORT_ROUTE_FEEDBACK = 2u,
    TDMA_TRANSPORT_ROUTE_DROP = 3u,
} tdma_transport_route_t;

typedef struct {
    uint32_t frame_class;
    uint32_t origin_slot_id;
    uint32_t transport_sequence;
    uint32_t payload_class;
    uint32_t flags;
    uint32_t schedule_crc32;
    uint32_t ring_profile_crc32;
    uint32_t hop_limit;
    const uint8_t *payload;
    size_t payload_size;
} tdma_transport_frame_build_t;

typedef struct {
    uint32_t frame_class;
    uint32_t packet_size;
    uint32_t origin_slot_id;
    uint32_t transport_sequence;
    uint32_t payload_class;
    uint32_t flags;
    uint32_t schedule_crc32;
    uint32_t ring_profile_crc32;
    uint32_t identity_crc32;
    uint32_t hop_count;
    uint32_t hop_limit;
    uint32_t transport_crc32;
    uint32_t payload_size;
    const uint8_t *payload;
} tdma_transport_frame_view_t;

size_t tdma_transport_frame_packet_size(uint32_t frame_class,
                                        size_t payload_size);
bool tdma_transport_frame_encode(const tdma_transport_frame_build_t *build,
                                 uint8_t *packet,
                                 size_t packet_capacity,
                                 size_t *packet_size,
                                 tdma_transport_result_t *result);
bool tdma_transport_frame_decode(const uint8_t *packet,
                                 size_t packet_size,
                                 tdma_transport_frame_view_t *view,
                                 tdma_transport_result_t *result);
tdma_transport_route_t tdma_transport_frame_route(
    const tdma_transport_frame_view_t *view,
    uint32_t local_slot_id);
bool tdma_transport_frame_advance_hop(uint8_t *packet,
                                      size_t packet_size,
                                      tdma_transport_result_t *result);
bool tdma_transport_frame_patch_flight_payload(
    uint8_t *packet,
    size_t packet_size,
    size_t payload_offset,
    const uint8_t *data,
    size_t data_size,
    tdma_transport_result_t *result);

#endif
