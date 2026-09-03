#include "tdma_flight_overlay.h"

#include <string.h>

static uint8_t tdma_flight_overlay_aligned_byte(
    const uint8_t *packet,
    size_t packet_size,
    uint32_t outer_header_size,
    uint32_t aligned_index)
{
    if (aligned_index < outer_header_size) {
        return 0u;
    }
    const uint32_t packet_index = aligned_index - outer_header_size;
    return packet_index < packet_size ? packet[packet_index] : 0u;
}

static uint8_t tdma_flight_overlay_physical_byte(
    const uint8_t *packet,
    size_t packet_size,
    uint32_t outer_header_size,
    uint32_t aligned_index,
    uint32_t bit_shift)
{
    const uint8_t current = tdma_flight_overlay_aligned_byte(
        packet, packet_size, outer_header_size, aligned_index);
    if (bit_shift == 0u) {
        return current;
    }
    const uint8_t previous = aligned_index == 0u
        ? 0u
        : tdma_flight_overlay_aligned_byte(
              packet, packet_size, outer_header_size, aligned_index - 1u);
    const uint32_t low_mask = (1u << bit_shift) - 1u;
    return (uint8_t)(((uint32_t)(previous & low_mask)
                      << (8u - bit_shift)) |
                     ((uint32_t)current >> bit_shift));
}

uint8_t tdma_flight_overlay_script_byte(uint32_t word)
{
    return (uint8_t)((word >> 22u) & 0xFFu);
}

static bool tdma_flight_overlay_force_replace(
    uint32_t packet_index,
    uint32_t force_replace_packet_offset,
    const uint32_t *force_replace_bitmap,
    size_t force_replace_bitmap_words)
{
    if (force_replace_bitmap == NULL ||
        packet_index < force_replace_packet_offset) {
        return false;
    }
    const uint32_t force_index =
        packet_index - force_replace_packet_offset;
    const size_t word_index = force_index / 32u;
    return word_index < force_replace_bitmap_words &&
           (force_replace_bitmap[word_index] &
            (1u << (force_index % 32u))) != 0u;
}

bool tdma_flight_overlay_build(const uint8_t *incoming_packet,
                               const uint8_t *processed_packet,
                               size_t packet_size,
                               uint32_t outer_header_size,
                               uint32_t alignment_byte_shift,
                               uint32_t alignment_bit_shift,
                               uint32_t physical_byte_count,
                               uint32_t force_replace_packet_offset,
                               const uint32_t *force_replace_bitmap,
                               size_t force_replace_bitmap_words,
                               uint32_t *script,
                               size_t script_capacity,
                               tdma_flight_overlay_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (incoming_packet == NULL || processed_packet == NULL ||
        packet_size == 0u || alignment_bit_shift >= 8u ||
        physical_byte_count == 0u || script == NULL ||
        script_capacity < (size_t)physical_byte_count + 1u ||
        alignment_byte_shift >= physical_byte_count ||
        ((force_replace_bitmap == NULL) !=
         (force_replace_bitmap_words == 0u)) ||
        (force_replace_bitmap != NULL &&
         force_replace_packet_offset > packet_size)) {
        return false;
    }

    memset(script,
           0,
           ((size_t)physical_byte_count + 1u) * sizeof(script[0]));
    script[physical_byte_count] = TDMA_FLIGHT_OVERLAY_SCRIPT_END;

    uint32_t replacement_count = 0u;
    for (uint32_t packet_index = 0u;
         packet_index < (uint32_t)packet_size;
         packet_index++) {
        const bool force_replace = tdma_flight_overlay_force_replace(
            packet_index,
            force_replace_packet_offset,
            force_replace_bitmap,
            force_replace_bitmap_words);
        if (!force_replace &&
            incoming_packet[packet_index] == processed_packet[packet_index]) {
            continue;
        }
        const uint32_t aligned_index = outer_header_size + packet_index;
        const uint32_t affected_count = alignment_bit_shift == 0u ? 1u : 2u;
        for (uint32_t affected = 0u; affected < affected_count; affected++) {
            const uint32_t physical_aligned_index = aligned_index + affected;
            const uint32_t script_index = alignment_byte_shift + 1u +
                                          physical_aligned_index;
            if (script_index >= physical_byte_count) {
                return false;
            }
            const uint8_t value = tdma_flight_overlay_physical_byte(
                processed_packet,
                packet_size,
                outer_header_size,
                physical_aligned_index,
                alignment_bit_shift);
            const uint32_t word = TDMA_FLIGHT_OVERLAY_SCRIPT_REPLACE |
                                  ((uint32_t)value << 22u);
            if (script[script_index] == TDMA_FLIGHT_OVERLAY_SCRIPT_PASS) {
                replacement_count++;
            }
            script[script_index] = word;
        }
    }

    if (result != NULL) {
        result->physical_byte_count = physical_byte_count;
        result->replacement_byte_count = replacement_count;
        result->alignment_byte_shift = alignment_byte_shift;
        result->alignment_bit_shift = alignment_bit_shift;
    }
    return true;
}
