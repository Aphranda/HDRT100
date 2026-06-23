#include "pota_types.h"

uint32_t pota_crc32_update(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0u; i < size; i++) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint32_t pota_crc32_compute(const void *data, size_t size)
{
    return pota_crc32_update(0u, data, size);
}
