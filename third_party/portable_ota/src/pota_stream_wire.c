#include "pota_stream_wire.h"

#include <string.h>

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8u) & 0xFFu);
    data[2] = (uint8_t)((value >> 16u) & 0xFFu);
    data[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

bool pota_stream_open_decode_le(const uint8_t *data, uint32_t size,
                                pota_stream_open_t *open)
{
    const uint32_t known_capabilities =
        POTA_STREAM_CAP_INACTIVE_WRITE | POTA_STREAM_CAP_DURABLE_ACK;
    if (data == NULL || open == NULL || size != POTA_STREAM_OPEN_WIRE_SIZE ||
        data[36] > 1u || data[37] != 0u || data[38] != 0u || data[39] != 0u) {
        return false;
    }
    if ((read_le32(&data[8]) & ~known_capabilities) != 0u) {
        return false;
    }

    memset(open, 0, sizeof(*open));
    open->session_id = read_le32(&data[0]);
    open->generation = read_le32(&data[4]);
    open->capability_mask = read_le32(&data[8]);
    open->map_version = read_le32(&data[12]);
    open->partition_id = read_le32(&data[16]);
    open->destination_slot = read_le32(&data[20]);
    open->object_id = read_le32(&data[24]);
    open->total_size = read_le32(&data[28]);
    open->package_crc32 = read_le32(&data[32]);
    open->package_mode = data[36] != 0u;
    memcpy(open->identity, &data[POTA_STREAM_OPEN_IDENTITY_OFFSET],
           sizeof(open->identity));
    memcpy(open->package_hash, &data[POTA_STREAM_OPEN_PACKAGE_HASH_OFFSET],
           sizeof(open->package_hash));
    return true;
}

uint32_t pota_stream_open_token(const pota_stream_open_t *open)
{
    if (open == NULL) {
        return 0u;
    }
    uint8_t data[POTA_STREAM_OPEN_WIRE_SIZE] = {0};
    write_le32(&data[0], open->session_id);
    write_le32(&data[4], open->generation);
    write_le32(&data[8], open->capability_mask);
    write_le32(&data[12], open->map_version);
    write_le32(&data[16], open->partition_id);
    write_le32(&data[20], open->destination_slot);
    write_le32(&data[24], open->object_id);
    write_le32(&data[28], open->total_size);
    write_le32(&data[32], open->package_crc32);
    data[36] = open->package_mode ? 1u : 0u;
    memcpy(&data[POTA_STREAM_OPEN_IDENTITY_OFFSET], open->identity,
           sizeof(open->identity));
    memcpy(&data[POTA_STREAM_OPEN_PACKAGE_HASH_OFFSET], open->package_hash,
           sizeof(open->package_hash));
    return pota_crc32_compute(data, sizeof(data));
}
