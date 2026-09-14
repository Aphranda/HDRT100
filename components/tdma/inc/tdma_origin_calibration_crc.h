#ifndef TDMA_ORIGIN_CALIBRATION_CRC_H
#define TDMA_ORIGIN_CALIBRATION_CRC_H

#include <stddef.h>
#include <stdint.h>

/* Same CRC-32/ISO-HDLC bytes as Calibration's OTA CRC. The admitted owner
 * still checks the complete immutable stage; no cached identity or DMA use.
 * As with the publisher's CRC, nonempty input must point to readable bytes. */
uint32_t tdma_origin_calibration_crc32(const uint8_t *data, size_t size);

#endif
