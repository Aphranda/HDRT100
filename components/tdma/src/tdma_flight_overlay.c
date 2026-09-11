#include "tdma_flight_overlay.h"

#include <string.h>

static bool tdma_flight_overlay_add_run(tdma_flight_overlay_plan_t *plan,
                                      uint32_t count, uint32_t source)
{
    if (count == 0u) return true;
    if (plan->run_count >= TDMA_FLIGHT_OVERLAY_RUN_MAX) return false;
    plan->run[plan->run_count++] = (tdma_flight_overlay_dma_run_t){
        .control = source != TDMA_FLIGHT_OVERLAY_SOURCE_PASS,
        .transfer_count = count,
        .read_address = source,
    };
    return true;
}

static bool tdma_flight_overlay_valid_terminal(uint32_t pc)
{
    /* The installed 28-instruction catalog can start at 0..4. Its terminal
     * branch is instruction 16. The adapter checks these against pioasm. */
    return pc >= 16u && pc <= 20u;
}

bool tdma_flight_overlay_build_pass_plan(uint32_t physical_byte_count,
                                        uint32_t final_bit_pc,
                                        tdma_flight_overlay_plan_t *plan)
{
    if (plan == NULL) return false;
    memset(plan, 0, sizeof(*plan));
    if (physical_byte_count == 0u ||
        physical_byte_count > 0x0FFFFFFFu / TDMA_FLIGHT_OVERLAY_COMMAND_WORDS_PER_BYTE ||
        !tdma_flight_overlay_valid_terminal(final_bit_pc)) return false;
    plan->command_word_count = physical_byte_count * TDMA_FLIGHT_OVERLAY_COMMAND_WORDS_PER_BYTE;
    plan->token_word_count = 1u;
    plan->token[0] = (TDMA_FLIGHT_OVERLAY_TOKEN_LIVE << 16u) | final_bit_pc;
    return tdma_flight_overlay_add_run(plan, plan->command_word_count - 1u,
                                      TDMA_FLIGHT_OVERLAY_SOURCE_PASS) &&
           tdma_flight_overlay_add_run(plan, 1u, 0u);
}

bool tdma_flight_overlay_build_plan(
    const uint8_t *incoming_packet, const uint8_t *processed_packet,
    size_t packet_size, const uint32_t *force_payload_bitmap,
    size_t force_payload_bitmap_words, const tdma_flight_overlay_config_t *config,
    tdma_flight_overlay_plan_t *plan)
{
    if (plan == NULL) return false;
    memset(plan, 0, sizeof(*plan));
    if (config == NULL || incoming_packet == NULL || processed_packet == NULL ||
        packet_size != TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        config->local_slot_id >= TDMA_FLIGHT_SHORT_SLOT_COUNT ||
        (config->header_write_mask &
         ~tdma_transport_frame_resident_overlay_header_mask()) != 0u ||
        config->alignment_bit_shift >= 8u ||
        config->physical_byte_count == 0u ||
        config->physical_byte_count > UINT32_MAX / 8u ||
        !tdma_flight_overlay_valid_terminal(config->final_bit_pc) ||
        ((force_payload_bitmap == NULL) != (force_payload_bitmap_words == 0u)) ||
        force_payload_bitmap_words > TDMA_FLIGHT_OUTPUT_BITMAP_WORDS) return false;
    const uint64_t base64 = ((uint64_t)config->alignment_byte_shift +
                            config->outer_header_size + 1u) * 8u +
                           config->alignment_bit_shift;
    const uint32_t physical_bits = config->physical_byte_count * 8u;
    /* The final branch must never consume a payload bit, including a trailer
     * bit owned by the reference. The complete packet needs an in-flight tail. */
    if (base64 + packet_size * 8u >= physical_bits) return false;
    const uint32_t base = (uint32_t)base64;
    const uint32_t local_start = TDMA_TRANSPORT_FRAME_HEADER_SIZE +
        config->local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE;
    const uint32_t local_end = local_start + TDMA_FLIGHT_SHORT_SLOT_SIZE;
    uint32_t replace[1u + TDMA_FLIGHT_OUTPUT_BITMAP_WORDS] = {0};
    for (uint32_t i = 0u; i < packet_size; ++i) {
        const bool header = i < TDMA_TRANSPORT_FRAME_HEADER_SIZE;
        const uint32_t payload_index = header ? 0u : i - TDMA_TRANSPORT_FRAME_HEADER_SIZE;
        const bool forced = !header && force_payload_bitmap != NULL &&
            payload_index / 32u < force_payload_bitmap_words &&
            (force_payload_bitmap[payload_index / 32u] & (1u << (payload_index % 32u))) != 0u;
        if (!forced && incoming_packet[i] == processed_packet[i]) continue;
        const bool authorized = header
            ? (config->header_write_mask & (1u << i)) != 0u
            : i >= local_start && i < local_end;
        if (!authorized) return false;
        replace[i / 32u] |= 1u << (i % 32u);
    }
    /* Unused bitmap bits are not a second mutation channel. */
    if (force_payload_bitmap_words == TDMA_FLIGHT_OUTPUT_BITMAP_WORDS &&
        (force_payload_bitmap[force_payload_bitmap_words - 1u] &
         ~((1u << (TDMA_FLIGHT_SHORT_PAYLOAD_SIZE % 32u)) - 1u)) != 0u) return false;

    struct window { uint32_t first, end, token; } window[2u] = {
        {base / 2u, (base + TDMA_TRANSPORT_FRAME_HEADER_SIZE * 8u + 1u) / 2u, 0u},
        {(base + local_start * 8u) / 2u, (base + local_end * 8u + 1u) / 2u, 0u},
    };
    uint32_t window_count = 2u;
    if (window[1].first <= window[0].end) {
        window[0].end = window[1].end;
        window_count = 1u;
    }
    plan->command_word_count = physical_bits / 2u;
    uint32_t cursor = 0u;
    for (uint32_t i = 0u; i < window_count; ++i) {
        const uint32_t count = window[i].end - window[i].first;
        if (window[i].end >= plan->command_word_count ||
            count + plan->token_word_count + 1u > TDMA_FLIGHT_OVERLAY_TOKEN_WORD_MAX ||
            !tdma_flight_overlay_add_run(plan, window[i].first - cursor,
                                         TDMA_FLIGHT_OVERLAY_SOURCE_PASS)) return false;
        window[i].token = plan->token_word_count;
        if (!tdma_flight_overlay_add_run(plan, count, window[i].token)) return false;
        for (uint32_t j = 0u; j < count; ++j)
            plan->token[plan->token_word_count++] = TDMA_FLIGHT_OVERLAY_LIVE_WORD;
        cursor = window[i].end;
    }
    uint32_t previous_physical_byte = UINT32_MAX;
    for (uint32_t i = 0u; i < packet_size; ++i) {
        if ((replace[i / 32u] & (1u << (i % 32u))) == 0u) continue;
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            const uint32_t position = base + i * 8u + bit;
            const uint32_t word = position / 2u;
            const uint32_t which = window_count == 1u || word < window[0].end ? 0u : 1u;
            const uint32_t index = window[which].token + word - window[which].first;
            const uint32_t mask = 0x80u >> bit;
            /* CRC is affine over a fixed-length header. Applying the hop
             * delta and its CRC delta to LIVE bits preserves every sequence
             * and any existing header error syndrome. Forcing a predicted
             * CRC would corrupt later cycles or repair corrupted wire bits. */
            const uint32_t token = i < TDMA_TRANSPORT_FRAME_HEADER_SIZE
                ? (((incoming_packet[i] ^ processed_packet[i]) & mask) != 0u
                       ? TDMA_FLIGHT_OVERLAY_TOKEN_INVERT
                       : TDMA_FLIGHT_OVERLAY_TOKEN_LIVE)
                : ((processed_packet[i] & mask) != 0u
                       ? TDMA_FLIGHT_OVERLAY_TOKEN_ONE
                       : TDMA_FLIGHT_OVERLAY_TOKEN_ZERO);
            const uint32_t shift = (position & 1u) == 0u ? 16u : 0u;
            plan->token[index] = (plan->token[index] & ~(0xFFFFu << shift)) | (token << shift);
            if (position / 8u != previous_physical_byte) {
                plan->replacement_byte_count++;
                previous_physical_byte = position / 8u;
            }
        }
    }
    if (!tdma_flight_overlay_add_run(plan, plan->command_word_count - cursor - 1u,
                                    TDMA_FLIGHT_OVERLAY_SOURCE_PASS) ||
        !tdma_flight_overlay_add_run(plan, 1u, plan->token_word_count)) return false;
    plan->token[plan->token_word_count++] =
        (TDMA_FLIGHT_OVERLAY_TOKEN_LIVE << 16u) | config->final_bit_pc;
    return tdma_flight_overlay_plan_valid(plan, config->final_bit_pc);
}

bool tdma_flight_overlay_plan_valid(const tdma_flight_overlay_plan_t *plan,
                                   uint32_t final_bit_pc)
{
    if (plan == NULL || plan->generation != 0u ||
        !tdma_flight_overlay_valid_terminal(final_bit_pc) ||
        plan->run_count == 0u || plan->run_count > TDMA_FLIGHT_OVERLAY_RUN_MAX ||
        plan->token_word_count == 0u || plan->token_word_count > TDMA_FLIGHT_OVERLAY_TOKEN_WORD_MAX ||
        plan->command_word_count == 0u || plan->command_word_count > 0x0FFFFFFFu ||
        plan->command_word_count % TDMA_FLIGHT_OVERLAY_COMMAND_WORDS_PER_BYTE != 0u ||
        plan->token[plan->token_word_count - 1u] !=
            ((TDMA_FLIGHT_OVERLAY_TOKEN_LIVE << 16u) | final_bit_pc)) return false;
    uint64_t count = 0u;
    uint32_t token_cursor = 0u;
    for (uint32_t i = 0u; i < plan->run_count; ++i) {
        const tdma_flight_overlay_dma_run_t *run = &plan->run[i];
        if (run->write_address != 0u || run->transfer_count == 0u) return false;
        if (run->control == 0u) {
            if (run->read_address != TDMA_FLIGHT_OVERLAY_SOURCE_PASS ||
                i + 1u == plan->run_count) return false;
        } else if (run->control == 1u) {
            if (run->read_address != token_cursor ||
                run->transfer_count > plan->token_word_count - token_cursor) return false;
            token_cursor += run->transfer_count;
        } else return false;
        count += run->transfer_count;
    }
    if (count != plan->command_word_count || token_cursor != plan->token_word_count) return false;
    for (uint32_t i = 0u; i < token_cursor; ++i) {
        for (uint32_t half = 0u; half < 2u; ++half) {
            const uint32_t token = (plan->token[i] >> (half == 0u ? 16u : 0u)) & 0xFFFFu;
            if (i + 1u == token_cursor && half == 1u) {
                if (token != final_bit_pc) return false;
            } else if (token != TDMA_FLIGHT_OVERLAY_TOKEN_LIVE &&
                       token != TDMA_FLIGHT_OVERLAY_TOKEN_INVERT &&
                       token != TDMA_FLIGHT_OVERLAY_TOKEN_ZERO &&
                       token != TDMA_FLIGHT_OVERLAY_TOKEN_ONE) return false;
        }
    }
    return true;
}

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
