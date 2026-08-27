#ifndef TDMA_PROCESS_IMAGE_LAYOUT_H
#define TDMA_PROCESS_IMAGE_LAYOUT_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_flight_engine.h"

/* Product SHORT Node mailbox layout.  Allocation is mandatory-first and
 * entirely static: the one-byte diagnostic field is admitted only because
 * all mandatory regions and the mailbox CRC already fit. */
#define TDMA_PROCESS_IMAGE_LAYOUT_VERSION 1u
#define TDMA_PROCESS_IMAGE_MESSAGE_CLASS 0x10u

#define TDMA_PROCESS_IMAGE_VDC_OFFSET 8u
#define TDMA_PROCESS_IMAGE_VDC_SIZE 6u
#define TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET TDMA_PROCESS_IMAGE_VDC_OFFSET
#define TDMA_PROCESS_IMAGE_VDC_RATE_OFFSET 10u
#define TDMA_PROCESS_IMAGE_VDC_LOCK_OFFSET 12u
#define TDMA_PROCESS_IMAGE_VDC_QUALITY_OFFSET 13u
#define TDMA_PROCESS_IMAGE_VDC_PHASE_QUANTUM_NS 4u
#define TDMA_PROCESS_IMAGE_VDC_RATE_QUANTUM_PPB 2u

#define TDMA_PROCESS_IMAGE_REFMEM_OFFSET 14u
#define TDMA_PROCESS_IMAGE_REFMEM_SIZE 10u
#define TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET \
    TDMA_PROCESS_IMAGE_REFMEM_OFFSET
#define TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET 18u
#define TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET 20u
#define TDMA_PROCESS_IMAGE_REFMEM_BASELINE_FIELD_ID 0u

#define TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET 24u
#define TDMA_PROCESS_IMAGE_ACK_QUALITY_SIZE 3u
#define TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET \
    TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET
#define TDMA_PROCESS_IMAGE_ACK_FLAGS_OFFSET 26u
#define TDMA_PROCESS_IMAGE_ACK_FLAG_VALID (1u << 0u)
#define TDMA_PROCESS_IMAGE_ACK_FLAG_NACK (1u << 1u)
#define TDMA_PROCESS_IMAGE_ACK_FLAG_FENCE (1u << 2u)
#define TDMA_PROCESS_IMAGE_ACK_QUALITY_SHIFT 4u
#define TDMA_PROCESS_IMAGE_ACK_QUALITY_MASK 0xF0u

#define TDMA_PROCESS_IMAGE_CONTROL_OFFSET 27u
#define TDMA_PROCESS_IMAGE_CONTROL_SIZE 2u
#define TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET \
    TDMA_PROCESS_IMAGE_CONTROL_OFFSET
#define TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET 28u
#define TDMA_PROCESS_IMAGE_CONTROL_OPCODE_NONE 0u

#define TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET 29u
#define TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_SIZE 1u

#define TDMA_PROCESS_IMAGE_CRC_OFFSET 30u
#define TDMA_PROCESS_IMAGE_CRC_SIZE 2u

#define TDMA_PROCESS_IMAGE_MANDATORY_BODY_SIZE \
    (TDMA_PROCESS_IMAGE_VDC_SIZE + TDMA_PROCESS_IMAGE_REFMEM_SIZE + \
     TDMA_PROCESS_IMAGE_ACK_QUALITY_SIZE + TDMA_PROCESS_IMAGE_CONTROL_SIZE + \
     TDMA_PROCESS_IMAGE_CRC_SIZE)
#define TDMA_PROCESS_IMAGE_OPTIONAL_BODY_CAPACITY \
    (TDMA_FLIGHT_MAILBOX_BODY_SIZE - \
     TDMA_PROCESS_IMAGE_MANDATORY_BODY_SIZE)
#define TDMA_PROCESS_IMAGE_CONFIGURED_BODY_SIZE \
    (TDMA_PROCESS_IMAGE_MANDATORY_BODY_SIZE + \
     TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_SIZE)

#define TDMA_PROCESS_IMAGE_VDC_QUALITY_HEALTH_MASK 0x0Fu
#define TDMA_PROCESS_IMAGE_VDC_QUALITY_TIER_SHIFT 4u
#define TDMA_PROCESS_IMAGE_VDC_QUALITY_TIER_MASK 0x70u
#define TDMA_PROCESS_IMAGE_VDC_QUALITY_VALID (1u << 7u)

static inline int16_t tdma_process_image_quantize_i16(int32_t value,
                                                       uint32_t quantum)
{
    if (quantum == 0u) {
        return 0;
    }
    const int32_t scaled = value / (int32_t)quantum;
    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

static inline int32_t tdma_process_image_expand_i16(int16_t value,
                                                     uint32_t quantum)
{
    return (int32_t)value * (int32_t)quantum;
}

static inline uint16_t tdma_process_image_crc16_ccitt(const uint8_t *data,
                                                       size_t size)
{
    uint16_t crc = 0xFFFFu;
    if (data == NULL) {
        return 0u;
    }
    for (size_t i = 0u; i < size; i++) {
        crc ^= (uint16_t)data[i] << 8u;
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            crc = (crc & 0x8000u) != 0u
                      ? (uint16_t)((crc << 1u) ^ 0x1021u)
                      : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

#endif
