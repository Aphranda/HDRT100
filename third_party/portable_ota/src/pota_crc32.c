#include "pota_types.h"

/* Reflected CRC-32 (0xEDB88320) nibble table.  Kept in .rodata so the
 * 4 KiB fast path spends no additional RAM on a 256-entry table. */
static const uint32_t s_crc32_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};

uint32_t pota_crc32_update(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0u; i < size; i++) {
        crc ^= bytes[i];
        crc = (crc >> 4u) ^ s_crc32_nibble[crc & 0x0Fu];
        crc = (crc >> 4u) ^ s_crc32_nibble[crc & 0x0Fu];
    }
    return ~crc;
}

uint32_t pota_crc32_compute(const void *data, size_t size)
{
    return pota_crc32_update(0u, data, size);
}
