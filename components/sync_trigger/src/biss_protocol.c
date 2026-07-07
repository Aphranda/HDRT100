#include "biss_protocol.h"

#include <stddef.h>

static bool range_fits(uint32_t offset, uint32_t bits, uint32_t frame_bits)
{
    if (bits == 0u || offset >= frame_bits) {
        return false;
    }
    return bits <= (frame_bits - offset);
}

static bool optional_bit_valid(uint32_t offset, uint32_t frame_bits)
{
    return offset == BISS_PROFILE_DISABLED_OFFSET || offset < frame_bits;
}

biss_profile_status_t biss_profile_validate(const biss_profile_t *profile)
{
    if (profile == NULL) {
        return BISS_PROFILE_ERR_NULL;
    }

    if (profile->frame_bits == 0u ||
        profile->frame_bits > BISS_PROFILE_MAX_FRAME_BITS) {
        return BISS_PROFILE_ERR_FRAME_BITS;
    }

    if (profile->position_bits == 0u ||
        profile->position_bits > BISS_PROFILE_MAX_POSITION_BITS) {
        return BISS_PROFILE_ERR_POSITION_BITS;
    }

    if (!range_fits(profile->position_offset,
                    profile->position_bits,
                    profile->frame_bits)) {
        return BISS_PROFILE_ERR_POSITION_RANGE;
    }

    if (profile->modulo == 0u) {
        return BISS_PROFILE_ERR_MODULO;
    }

    if (profile->anchor_bits > 0u) {
        if (profile->anchor_bits > 64u ||
            !range_fits(profile->anchor_offset,
                        profile->anchor_bits,
                        profile->frame_bits)) {
            return BISS_PROFILE_ERR_ANCHOR_RANGE;
        }
    }

    if (!optional_bit_valid(profile->error_bit_offset, profile->frame_bits) ||
        !optional_bit_valid(profile->warning_bit_offset, profile->frame_bits)) {
        return BISS_PROFILE_ERR_STATUS_RANGE;
    }

    if (profile->crc_bits > BISS_PROFILE_MAX_CRC_BITS) {
        return BISS_PROFILE_ERR_CRC_RANGE;
    }

    if (profile->crc_bits == 0u && profile->crc_cover_bits > 0u) {
        return BISS_PROFILE_ERR_CRC_RANGE;
    }

    if (profile->crc_bits > 0u &&
        !range_fits(profile->crc_offset,
                    profile->crc_bits,
                    profile->frame_bits)) {
        return BISS_PROFILE_ERR_CRC_RANGE;
    }

    if (profile->crc_cover_bits > 0u &&
        !range_fits(profile->crc_cover_offset,
                    profile->crc_cover_bits,
                    profile->frame_bits)) {
        return BISS_PROFILE_ERR_CRC_COVER_RANGE;
    }

    if (profile->sample_edge != BISS_SAMPLE_EDGE_RISING &&
        profile->sample_edge != BISS_SAMPLE_EDGE_FALLING) {
        return BISS_PROFILE_ERR_SAMPLE_EDGE;
    }

    if (profile->pio_cycles_per_bit == 0u ||
        profile->sample_delay_cycles >= profile->pio_cycles_per_bit) {
        return BISS_PROFILE_ERR_SAMPLE_DELAY;
    }

    if (profile->timeout_us == 0u) {
        return BISS_PROFILE_ERR_TIMEOUT;
    }

    if (profile->status_gate_policy > BISS_STATUS_GATE_BLOCK_TRIGGER ||
        profile->crc_gate_policy > BISS_CRC_GATE_BLOCK_TRIGGER) {
        return BISS_PROFILE_ERR_POLICY;
    }

    return BISS_PROFILE_OK;
}

uint64_t biss_extract_bits_msb(uint64_t frame,
                               uint32_t frame_bits,
                               uint32_t offset,
                               uint32_t bits)
{
    if (frame_bits == 0u || frame_bits > 64u || bits == 0u ||
        bits > 64u || offset >= frame_bits || bits > (frame_bits - offset)) {
        return 0u;
    }

    const uint32_t shift = frame_bits - offset - bits;
    if (bits == 64u) {
        return frame;
    }

    return (frame >> shift) & ((1ull << bits) - 1ull);
}

bool biss_anchor_matches(uint64_t frame, const biss_profile_t *profile)
{
    if (profile == NULL || profile->anchor_bits == 0u) {
        return true;
    }

    const uint64_t actual = biss_extract_bits_msb(frame,
                                                  profile->frame_bits,
                                                  profile->anchor_offset,
                                                  profile->anchor_bits);
    return (actual & profile->anchor_mask) ==
           (profile->anchor_value & profile->anchor_mask);
}

uint32_t biss_extract_position(uint64_t frame, const biss_profile_t *profile)
{
    if (profile == NULL) {
        return 0u;
    }

    return (uint32_t)biss_extract_bits_msb(frame,
                                           profile->frame_bits,
                                           profile->position_offset,
                                           profile->position_bits);
}

biss_status_bits_t biss_extract_status(uint64_t frame,
                                       const biss_profile_t *profile)
{
    biss_status_bits_t status = {0};

    if (profile == NULL) {
        return status;
    }

    if (profile->error_bit_offset != BISS_PROFILE_DISABLED_OFFSET) {
        status.error_active =
            biss_extract_bits_msb(frame,
                                  profile->frame_bits,
                                  profile->error_bit_offset,
                                  1u) == 0u;
    }

    if (profile->warning_bit_offset != BISS_PROFILE_DISABLED_OFFSET) {
        status.warning_active =
            biss_extract_bits_msb(frame,
                                  profile->frame_bits,
                                  profile->warning_bit_offset,
                                  1u) == 0u;
    }

    return status;
}

bool biss_status_gate_allows(biss_status_bits_t status,
                             biss_status_gate_policy_t policy)
{
    if (policy != BISS_STATUS_GATE_BLOCK_TRIGGER) {
        return true;
    }

    return !status.error_active && !status.warning_active;
}

uint8_t biss_crc6_update(uint8_t crc, bool bit)
{
    const bool inv = bit ^ ((crc & 0x20u) != 0u);
    uint8_t next = (uint8_t)((crc << 1) & 0x3eu);
    if (inv) {
        next ^= 0x03u;
    }
    return (uint8_t)(next & 0x3fu);
}

uint8_t biss_crc6_compute_bits(uint64_t value, uint32_t bits, bool invert)
{
    uint8_t crc = 0u;

    for (uint32_t i = 0u; i < bits; i++) {
        const uint32_t shift = bits - 1u - i;
        const bool bit = ((value >> shift) & 1ull) != 0u;
        crc = biss_crc6_update(crc, bit);
    }

    if (invert) {
        crc = (uint8_t)(~crc & 0x3fu);
    }

    return crc;
}

uint32_t biss_crc_compute_bits(uint64_t value,
                               uint32_t bits,
                               uint32_t crc_bits,
                               uint32_t polynomial,
                               uint32_t init,
                               uint32_t xor_value,
                               bool invert)
{
    if (crc_bits == 0u || crc_bits > 32u || bits > 64u) {
        return 0u;
    }

    const uint32_t mask = crc_bits == 32u ? UINT32_MAX : ((1u << crc_bits) - 1u);
    const uint32_t top_bit = 1u << (crc_bits - 1u);
    uint32_t crc = init & mask;
    const uint32_t poly = polynomial & mask;

    for (uint32_t i = 0u; i < bits; i++) {
        const uint32_t shift = bits - 1u - i;
        const bool bit = ((value >> shift) & 1ull) != 0u;
        const bool feedback = bit ^ ((crc & top_bit) != 0u);

        crc = (crc << 1u) & mask;
        if (feedback) {
            crc ^= poly;
        }
    }

    crc ^= (xor_value & mask);
    if (invert) {
        crc = ~crc & mask;
    }

    return crc & mask;
}

uint32_t biss_crc_compute_frame(uint64_t frame, const biss_profile_t *profile)
{
    if (profile == NULL || profile->crc_bits == 0u ||
        profile->crc_cover_bits == 0u) {
        return 0u;
    }

    const uint64_t covered = biss_extract_bits_msb(frame,
                                                   profile->frame_bits,
                                                   profile->crc_cover_offset,
                                                   profile->crc_cover_bits);
    return biss_crc_compute_bits(covered,
                                 profile->crc_cover_bits,
                                 profile->crc_bits,
                                 profile->crc_polynomial,
                                 profile->crc_init,
                                 profile->crc_xor,
                                 profile->crc_invert);
}

bool biss_crc_matches(uint64_t frame, const biss_profile_t *profile)
{
    if (profile == NULL || profile->crc_bits == 0u) {
        return true;
    }

    const uint32_t expected = biss_crc_compute_frame(frame, profile);
    const uint32_t actual = (uint32_t)biss_extract_bits_msb(frame,
                                                            profile->frame_bits,
                                                            profile->crc_offset,
                                                            profile->crc_bits);
    return expected == actual;
}

bool biss_crossed_position(uint32_t last,
                           uint32_t current,
                           uint32_t target,
                           uint32_t modulo)
{
    if (modulo == 0u || target >= modulo ||
        last >= modulo || current >= modulo ||
        last == current) {
        return false;
    }

    if (last < current) {
        return last < target && target <= current;
    }

    return target > last || target <= current;
}
