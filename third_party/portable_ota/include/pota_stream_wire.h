#ifndef POTA_STREAM_WIRE_H
#define POTA_STREAM_WIRE_H

#include <stdbool.h>
#include <stdint.h>

#include "pota_stream_session.h"

#define POTA_STREAM_OPEN_WIRE_SIZE 88u
#define POTA_STREAM_OPEN_IDENTITY_OFFSET 40u
#define POTA_STREAM_OPEN_PACKAGE_HASH_OFFSET 56u

/* Decode the fixed little-endian local-ingress OPEN descriptor. */
bool pota_stream_open_decode_le(const uint8_t *data, uint32_t size,
                                pota_stream_open_t *open);

#endif
