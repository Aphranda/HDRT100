#include "tdma_boot_profile.h"

#include <string.h>

#include "algo_checksum.h"
#include "algo_wire.h"

uint32_t tdma_boot_profile_crc32(const uint8_t *data, size_t size)
{
    if (data == NULL) {
        return 0u;
    }
    return algo_crc32_iso_hdlc_compute(data, size);
}

static void put_u32(uint8_t *cursor, uint32_t value)
{
    algo_le32_store(cursor, value);
}

static uint32_t get_u32(const uint8_t *cursor)
{
    return algo_le32_load(cursor);
}

bool tdma_boot_profile_valid(const tdma_boot_profile_t *profile)
{
    if (profile == NULL || profile->version != TDMA_BOOT_PROFILE_VERSION ||
        profile->profile_level == 0u || profile->generation == 0u ||
        profile->topology_crc32 == 0u ||
        profile->node_count < TDMA_BOOT_PROFILE_NODE_MIN ||
        profile->node_count > TDMA_BOOT_PROFILE_NODE_MAX ||
        profile->local_slot >= profile->node_count ||
        (profile->flags & ~TDMA_BOOT_PROFILE_FLAG_MASK) != 0u) {
        return false;
    }
    return true;
}

bool tdma_boot_profile_encode(const tdma_boot_profile_t *profile,
                              uint8_t *payload,
                              size_t capacity)
{
    if (!tdma_boot_profile_valid(profile) || payload == NULL ||
        capacity < TDMA_BOOT_PROFILE_PAYLOAD_SIZE) {
        return false;
    }
    put_u32(payload + 0u, profile->version);
    put_u32(payload + 4u, profile->profile_level);
    put_u32(payload + 8u, profile->node_count);
    put_u32(payload + 12u, profile->local_slot);
    put_u32(payload + 16u, profile->flags);
    put_u32(payload + 20u, profile->topology_crc32);
    put_u32(payload + 24u, profile->generation);
    put_u32(payload + TDMA_BOOT_PROFILE_CRC_OFFSET,
            tdma_boot_profile_crc32(payload, TDMA_BOOT_PROFILE_CRC_OFFSET));
    return true;
}

bool tdma_boot_profile_decode(const uint8_t *payload,
                              size_t size,
                              tdma_boot_profile_t *out)
{
    if (payload == NULL || out == NULL ||
        size != TDMA_BOOT_PROFILE_PAYLOAD_SIZE) {
        return false;
    }
    if (get_u32(payload + TDMA_BOOT_PROFILE_CRC_OFFSET) !=
        tdma_boot_profile_crc32(payload, TDMA_BOOT_PROFILE_CRC_OFFSET)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->version = get_u32(payload + 0u);
    out->profile_level = get_u32(payload + 4u);
    out->node_count = get_u32(payload + 8u);
    out->local_slot = get_u32(payload + 12u);
    out->flags = get_u32(payload + 16u);
    out->topology_crc32 = get_u32(payload + 20u);
    out->generation = get_u32(payload + 24u);
    return tdma_boot_profile_valid(out);
}
