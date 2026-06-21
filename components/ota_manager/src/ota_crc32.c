#include <stddef.h>
#include <stdint.h>

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    crc = ~crc;

    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

uint32_t ota_crc32_compute(const uint8_t *data, size_t length)
{
    return ota_crc32_update(0u, data, length);
}
