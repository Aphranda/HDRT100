#include "tdma_origin_calibration_crc.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define ORIGIN_CRC_RAM __not_in_flash("tdma_origin_calibration_crc")
#define ORIGIN_CRC_TABLE_RAM __scratch_y("tdma_origin_calibration_crc_table")
#else
#define ORIGIN_CRC_RAM
#define ORIGIN_CRC_TABLE_RAM
#endif

/* Four-bit reflected polynomial residues. Both code and table reside in SRAM.
 * This table uses the linked data region below the Core0 stack in SCRATCH_Y;
 * placing it in .data would push the aligned main-RAM DMA workspace into the
 * next page. Link both capacities with their full stack reservations. */
static const uint32_t ORIGIN_CRC_TABLE_RAM residues[16] = {
    0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
    0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
    0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
    0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
};

uint32_t ORIGIN_CRC_RAM tdma_origin_calibration_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size; ++i) {
        crc ^= data[i];
        crc = (crc >> 4u) ^ residues[crc & 15u];
        crc = (crc >> 4u) ^ residues[crc & 15u];
    }
    return ~crc;
}
