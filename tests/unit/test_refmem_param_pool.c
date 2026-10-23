#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "refmem_param_pool.h"

int main(void)
{
    static uint32_t region[REFMEM_PARAM_POOL_SLOT_COUNT *
                           (REFMEM_PARAM_POOL_SLOT_SIZE / sizeof(uint32_t))];
    const size_t region_size = sizeof(region);
    refmem_param_pool_init((uint8_t *)region, region_size);

    assert(refmem_param_pool_alloc((uint8_t *)region, region_size) == 0);
    assert(refmem_param_pool_alloc((uint8_t *)region, region_size) == 1);
    assert(refmem_param_pool_alloc((uint8_t *)region, region_size) == 2);
    assert(refmem_param_pool_alloc((uint8_t *)region, region_size) == -1);

    uint8_t payload[64];
    for (uint32_t i = 0u; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 3u + 1u);
    }
    assert(refmem_param_pool_write((uint8_t *)region, region_size, 0u, 7u, 1u,
                                   payload, sizeof(payload)));

    uint8_t out[64];
    uint32_t out_size = 0u;
    uint32_t out_crc = 0u;
    assert(refmem_param_pool_read((const uint8_t *)region, region_size, 0u, 7u,
                                  out, sizeof(out), &out_size, &out_crc));
    assert(out_size == sizeof(payload));
    assert(memcmp(out, payload, sizeof(payload)) == 0);
    assert(out_crc == refmem_param_pool_crc(payload, sizeof(payload)));

    /* Wrong command seq must be rejected. */
    assert(!refmem_param_pool_read((const uint8_t *)region, region_size, 0u, 8u,
                                   out, sizeof(out), NULL, NULL));

    /* Corrupted payload must be rejected by the CRC. */
    ((uint8_t *)region)[REFMEM_PARAM_POOL_HEADER_SIZE + 3u] ^= 0x5Au;
    assert(!refmem_param_pool_read((const uint8_t *)region, region_size, 0u, 7u,
                                   out, sizeof(out), NULL, NULL));
    ((uint8_t *)region)[REFMEM_PARAM_POOL_HEADER_SIZE + 3u] ^= 0x5Au;

    /* Oversized and empty payloads are rejected. */
    uint8_t big[REFMEM_PARAM_POOL_PAYLOAD_MAX + 1u];
    memset(big, 0, sizeof(big));
    assert(!refmem_param_pool_write((uint8_t *)region, region_size, 1u, 9u, 1u,
                                    big, sizeof(big)));
    assert(!refmem_param_pool_write((uint8_t *)region, region_size, 1u, 9u, 1u,
                                    big, 0u));

    assert(refmem_param_pool_release((uint8_t *)region, region_size, 0u));
    const int32_t reused = refmem_param_pool_alloc((uint8_t *)region,
                                                   region_size);
    assert(reused >= 0);

    printf("refmem_param_pool: alloc/write/read/crc/release OK\n");
    return 0;
}
