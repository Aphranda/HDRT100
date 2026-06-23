#include "portable_ota_port.h"

#include "pota_types.h"

uint32_t portable_ota_port_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    return pota_crc32_update(crc, data, length);
}

uint32_t portable_ota_port_crc32_compute(const uint8_t *data, size_t length)
{
    return pota_crc32_compute(data, length);
}
