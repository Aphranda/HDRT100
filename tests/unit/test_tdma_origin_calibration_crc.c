#include <assert.h>
#include <stdio.h>
#include "tdma_origin_calibration_crc.h"
#include "tdma_ring_runtime.h"
#include "pota_types.h"

int main(void)
{
    const uint8_t known[] = "123456789";
    assert(tdma_origin_calibration_crc32(known, 9) == 0xcbf43926u);
    assert(tdma_origin_calibration_crc32(NULL, 0) == pota_crc32_compute(NULL, 0));
    uint8_t data[sizeof(tdma_ring_calibration_stage_t) + 3];
    uint32_t random = 0x617fb32du;
    for (unsigned pattern = 0; pattern < 4; ++pattern) {
        for (size_t i = 0; i < sizeof(data); ++i) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            data[i] = pattern == 0 ? 0 : pattern == 1 ? 0xff : pattern == 2 ? (uint8_t)i : (uint8_t)random;
        }
        for (unsigned offset = 0; offset < 4; ++offset) {
            for (size_t length = 0; length <= sizeof(tdma_ring_calibration_stage_t); ++length) {
                assert(tdma_origin_calibration_crc32(data + offset, length) ==
                       pota_crc32_compute(data + offset, length));
            }
        }
    }
    /* Changes anywhere in the stage, including unused link storage, must
     * still change its admitted identity. It is not a header-only hash. */
    const size_t length = sizeof(tdma_ring_calibration_stage_t);
    const uint32_t initial = tdma_origin_calibration_crc32(data, length);
    for (size_t i = 0; i < length; ++i) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            data[i] ^= 1u << bit;
            const uint32_t changed = tdma_origin_calibration_crc32(data, length);
            assert(changed != initial && changed == pota_crc32_compute(data, length));
            data[i] ^= 1u << bit;
        }
    }
    printf("CRC compatibility passed, stage bytes=%zu\n", length);
    return 0;
}
