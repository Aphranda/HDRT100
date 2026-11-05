#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tdma_boot_profile.h"

static tdma_boot_profile_t sample_profile(void)
{
    tdma_boot_profile_t profile = {0};
    profile.version = TDMA_BOOT_PROFILE_VERSION;
    profile.profile_level = 23u; /* 10 kHz guide level */
    profile.node_count = 4u;
    profile.local_slot = 2u;
    profile.flags = TDMA_BOOT_PROFILE_FLAG_BRINGUP_NO_CAL;
    profile.topology_crc32 = 0x1234ABCDu;
    profile.generation = 7u;
    return profile;
}

int main(void)
{
    const tdma_boot_profile_t profile = sample_profile();
    uint8_t payload[TDMA_BOOT_PROFILE_PAYLOAD_SIZE] = {0};

    assert(tdma_boot_profile_valid(&profile));
    assert(tdma_boot_profile_encode(&profile, payload, sizeof(payload)));
    /* The CRC must cover the first 28 bytes and be present at offset 28. */
    assert((uint32_t)(payload[28] | (payload[29] << 8) |
                      (payload[30] << 16) | (payload[31] << 24)) ==
           tdma_boot_profile_crc32(payload, TDMA_BOOT_PROFILE_CRC_OFFSET));

    tdma_boot_profile_t decoded = {0};
    assert(tdma_boot_profile_decode(payload, sizeof(payload), &decoded));
    assert(decoded.version == profile.version);
    assert(decoded.profile_level == profile.profile_level);
    assert(decoded.node_count == profile.node_count);
    assert(decoded.local_slot == profile.local_slot);
    assert(decoded.flags == profile.flags);
    assert(decoded.topology_crc32 == profile.topology_crc32);
    assert(decoded.generation == profile.generation);

    /* A single flipped byte anywhere in the body must fail the CRC. */
    uint8_t corrupt[TDMA_BOOT_PROFILE_PAYLOAD_SIZE];
    memcpy(corrupt, payload, sizeof(corrupt));
    corrupt[8] ^= 0x01u;
    assert(!tdma_boot_profile_decode(corrupt, sizeof(corrupt), &decoded));

    /* Field validation. */
    tdma_boot_profile_t bad = sample_profile();
    bad.version = 0u;
    assert(!tdma_boot_profile_valid(&bad));
    bad = sample_profile();
    bad.profile_level = 0u;
    assert(!tdma_boot_profile_valid(&bad));
    bad = sample_profile();
    bad.local_slot = 4u;
    assert(!tdma_boot_profile_valid(&bad));
    bad = sample_profile();
    bad.node_count = 1u;
    assert(!tdma_boot_profile_valid(&bad));
    bad = sample_profile();
    bad.node_count = 9u;
    assert(!tdma_boot_profile_valid(&bad));
    bad = sample_profile();
    bad.flags = 0x80000000u;
    assert(!tdma_boot_profile_valid(&bad));
    bad = sample_profile();
    bad.generation = 0u;
    assert(!tdma_boot_profile_valid(&bad));
    bad = sample_profile();
    bad.topology_crc32 = 0u;
    assert(!tdma_boot_profile_valid(&bad));

    /* Argument and size checks. */
    assert(!tdma_boot_profile_encode(&profile, payload,
                                     TDMA_BOOT_PROFILE_PAYLOAD_SIZE - 1u));
    assert(!tdma_boot_profile_decode(payload,
                                     TDMA_BOOT_PROFILE_PAYLOAD_SIZE - 1u,
                                     &decoded));
    assert(!tdma_boot_profile_encode(NULL, payload, sizeof(payload)));
    assert(!tdma_boot_profile_decode(payload, sizeof(payload), NULL));

    printf("tdma boot profile host tests passed\n");
    return 0;
}
