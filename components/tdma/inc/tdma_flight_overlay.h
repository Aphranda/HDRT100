#ifndef TDMA_FLIGHT_OVERLAY_H
#define TDMA_FLIGHT_OVERLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TDMA_FLIGHT_OVERLAY_SCRIPT_PASS 0x00000000u
#define TDMA_FLIGHT_OVERLAY_SCRIPT_REPLACE 0x40000000u
#define TDMA_FLIGHT_OVERLAY_SCRIPT_END 0x80000000u

typedef struct {
    uint32_t physical_byte_count;
    uint32_t replacement_byte_count;
    uint32_t alignment_byte_shift;
    uint32_t alignment_bit_shift;
} tdma_flight_overlay_result_t;

/* Build one follower-frame PIO script. The PIO has a one-byte elastic stage,
 * hence logical byte zero is emitted at alignment_byte_shift + 1. A non-zero
 * bit shift may make one logical replacement span two physical bytes. */
bool tdma_flight_overlay_build(const uint8_t *incoming_packet,
                               const uint8_t *processed_packet,
                               size_t packet_size,
                               uint32_t outer_header_size,
                               uint32_t alignment_byte_shift,
                               uint32_t alignment_bit_shift,
                               uint32_t physical_byte_count,
                               uint32_t *script,
                               size_t script_capacity,
                               tdma_flight_overlay_result_t *result);

uint8_t tdma_flight_overlay_script_byte(uint32_t word);

#endif
