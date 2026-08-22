#include "pota_stream_wire.h"

#include <string.h>

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

bool pota_stream_open_decode_le(const uint8_t *data, uint32_t size,
                                pota_stream_open_t *open)
{
    if (data == NULL || open == NULL || size != POTA_STREAM_OPEN_WIRE_SIZE ||
        data[36] > 1u || data[37] != 0u || data[38] != 0u || data[39] != 0u) {
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
