#include "tdma_origin_calibration_crc.h"

#include "tdma_transport_frame.h"

/* The origin calibration credential uses the same CRC-32/ISO-HDLC bytes as the
 * TDMA transport frames (see the header contract).  Keep one residue table and
 * one SRAM-resident kernel instead of a second copy: the transport kernel is
 * already `__not_in_flash` and numerically identical for every byte range. */
uint32_t tdma_origin_calibration_crc32(const uint8_t *data, size_t size)
{
    return tdma_transport_crc32_compute(data, size);
}
