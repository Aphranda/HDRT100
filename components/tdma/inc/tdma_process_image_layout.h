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

/* Global process-image trailer.  It is owned by the TDMA reference Node,
 * not by any per-Node mailbox.  Frame N carries the reference TX latch for
 * frame N-1, so the current transport sequence supplies correlation without
 * repeating a sequence or identity field in the payload.
 *
 * bit 31    : valid
 * bits 30:0 : reference TX phase within the frozen TDMA cycle, 4 ns per tick
 *
 * A cycle phase is independent of each Node's asynchronous boot epoch.  The
 * receiver maps it into its local cycle before applying the VDC path matrix.
 */
#define TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET \
    TDMA_FLIGHT_NODE_IMAGE_SIZE
#define TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SIZE \
    TDMA_FLIGHT_DPLL_OBSERVATION_SIZE
#define TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_VALID_MASK 0x80000000u
#define TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_MASK 0x7FFFFFFFu
#define TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS 4u
#define TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SEQUENCE_LAG 1u

_Static_assert(TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET +
                       TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SIZE ==
                   TDMA_FLIGHT_SHORT_PAYLOAD_SIZE,
               "DPLL observation trailer must close the SHORT payload");

static inline uint32_t tdma_process_image_dpll_observation_encode_phase(
    uint64_t reference_tx_timestamp_ns,
    uint32_t cycle_period_ns)
{
    if (cycle_period_ns <
        TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS) {
        return 0u;
    }
    const uint64_t phase_ns = reference_tx_timestamp_ns % cycle_period_ns;
    const uint64_t tick = phase_ns /
        TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS;
    if (tick > TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_MASK) {
        return 0u;
    }
    return TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_VALID_MASK |
           (uint32_t)tick;
}

static inline bool tdma_process_image_dpll_observation_map_phase(
    uint32_t encoded,
    uint32_t cycle_period_ns,
    uint64_t local_rx_timestamp_ns,
    uint64_t *reference_tx_timestamp_ns)
{
    if ((encoded & TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_VALID_MASK) == 0u ||
        cycle_period_ns <
            TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS ||
        reference_tx_timestamp_ns == NULL) {
        return false;
    }
    const uint64_t phase_ns =
        (uint64_t)(encoded &
                   TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_MASK) *
        TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS;
    if (phase_ns >= cycle_period_ns) {
        return false;
    }
    const uint64_t local_cycle_start_ns = local_rx_timestamp_ns -
        local_rx_timestamp_ns % cycle_period_ns;
    if (UINT64_MAX - local_cycle_start_ns < phase_ns) {
        return false;
    }
    *reference_tx_timestamp_ns = local_cycle_start_ns + phase_ns;
    return true;
}

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
