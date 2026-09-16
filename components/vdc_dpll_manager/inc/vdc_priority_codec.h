#ifndef VDC_PRIORITY_CODEC_H
#define VDC_PRIORITY_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_process_image_layout.h"

#define VDC_PRIORITY_CODEC_SCHEMA 1u
#define VDC_PRIORITY_CODEC_BODY_SIZE TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_SIZE
#define VDC_PRIORITY_CODEC_CRC_INIT 0xFFFFu
#define VDC_PRIORITY_CODEC_CRC_POLY 0x1021u
#define VDC_PRIORITY_CODEC_KNOWN_FLAGS 0u

typedef struct {
    uint32_t binding_generation;
    uint32_t event_sequence;
    uint64_t event_time_lower;
    uint32_t uncertainty_width;
    uint16_t flags;
} vdc_priority_codec_record_t;

/* The codec owns only the 22-byte typed body. Source slot and target bitmap
 * remain immutable frame-header identity and are intentionally not decoded. */
bool vdc_priority_codec_encode(const vdc_priority_codec_record_t *record,
    uint8_t body[VDC_PRIORITY_CODEC_BODY_SIZE]);
bool vdc_priority_codec_decode(const uint8_t body[VDC_PRIORITY_CODEC_BODY_SIZE],
    vdc_priority_codec_record_t *record);
uint16_t vdc_priority_codec_crc16(const uint8_t *data, size_t size);
bool vdc_priority_codec_crc_valid(const uint8_t body[VDC_PRIORITY_CODEC_BODY_SIZE],
    uint16_t expected_crc);

/* Admission is deliberately separate from ordinary process-image parsing.
 * It checks only the typed class and exact body bounds; header source/target
 * identity is supplied by the caller and never inferred from this body. */
bool vdc_priority_codec_layout_admit(uint8_t message_class,
    size_t body_size);

#endif
