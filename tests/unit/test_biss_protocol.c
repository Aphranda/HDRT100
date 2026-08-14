#include "biss_protocol.h"

#include <stdio.h>

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static uint64_t put_bits_msb(uint64_t frame,
                             uint32_t frame_bits,
                             uint32_t offset,
                             uint32_t bits,
                             uint64_t value)
{
    const uint32_t shift = frame_bits - offset - bits;
    const uint64_t mask = bits == 64u ? UINT64_MAX : ((1ull << bits) - 1ull);
    frame &= ~(mask << shift);
    frame |= (value & mask) << shift;
    return frame;
}

static biss_profile_t make_profile(void)
{
    biss_profile_t profile = {
        .frame_bits = 40u,
        .position_offset = 4u,
        .position_bits = 20u,
        .modulo = 1u << 20,
        .anchor_offset = 0u,
        .anchor_bits = 2u,
        .anchor_mask = 0x3u,
        .anchor_value = 0x2u,
        .error_bit_offset = 24u,
        .warning_bit_offset = 25u,
        .status_gate_policy = BISS_STATUS_GATE_BLOCK_TRIGGER,
        .crc_offset = 26u,
        .crc_bits = 6u,
        .crc_cover_offset = 4u,
        .crc_cover_bits = 22u,
        .crc_polynomial = 0x03u,
        .crc_init = 0u,
        .crc_xor = 0u,
        .crc_invert = true,
        .crc_gate_policy = BISS_CRC_GATE_LATE_COUNT,
        .sample_edge = BISS_SAMPLE_EDGE_FALLING,
        .sample_delay_cycles = 25u,
        .pio_cycles_per_bit = 50u,
        .timeout_us = 20u,
    };

    return profile;
}

static uint64_t make_frame(const biss_profile_t *profile,
                           uint32_t position,
                           bool error_bit,
                           bool warning_bit)
{
    uint64_t frame = 0u;

    frame = put_bits_msb(frame,
                         profile->frame_bits,
                         profile->anchor_offset,
                         profile->anchor_bits,
                         profile->anchor_value);
    frame = put_bits_msb(frame,
                         profile->frame_bits,
                         profile->position_offset,
                         profile->position_bits,
                         position);
    frame = put_bits_msb(frame,
                         profile->frame_bits,
                         profile->error_bit_offset,
                         1u,
                         error_bit ? 1u : 0u);
    frame = put_bits_msb(frame,
                         profile->frame_bits,
                         profile->warning_bit_offset,
                         1u,
                         warning_bit ? 1u : 0u);

    const uint32_t crc = biss_crc_compute_frame(frame, profile);
    frame = put_bits_msb(frame,
                         profile->frame_bits,
                         profile->crc_offset,
                         profile->crc_bits,
                         crc);
    return frame;
}

static int test_profile_validation(void)
{
    int failed = 0;
    biss_profile_t profile = make_profile();

    failed += expect_u32("valid profile",
                         (uint32_t)biss_profile_validate(&profile),
                         (uint32_t)BISS_PROFILE_OK);

    profile.position_bits = 0u;
    failed += expect_u32("invalid position bits",
                         (uint32_t)biss_profile_validate(&profile),
                         (uint32_t)BISS_PROFILE_ERR_POSITION_BITS);

    profile = make_profile();
    profile.position_offset = 32u;
    failed += expect_u32("invalid position range",
                         (uint32_t)biss_profile_validate(&profile),
                         (uint32_t)BISS_PROFILE_ERR_POSITION_RANGE);

    profile = make_profile();
    profile.modulo = 0u;
    failed += expect_u32("invalid modulo",
                         (uint32_t)biss_profile_validate(&profile),
                         (uint32_t)BISS_PROFILE_ERR_MODULO);

    profile = make_profile();
    profile.sample_delay_cycles = profile.pio_cycles_per_bit;
    failed += expect_u32("invalid sample delay",
                         (uint32_t)biss_profile_validate(&profile),
                         (uint32_t)BISS_PROFILE_ERR_SAMPLE_DELAY);

    profile = make_profile();
    profile.crc_bits = 0u;
    failed += expect_u32("invalid crc coverage without crc field",
                         (uint32_t)biss_profile_validate(&profile),
                         (uint32_t)BISS_PROFILE_ERR_CRC_RANGE);

    return failed;
}

static int test_bit_extract_anchor_and_status(void)
{
    int failed = 0;
    const biss_profile_t profile = make_profile();
    const uint64_t frame = make_frame(&profile, 0xABCDEu, true, true);

    failed += expect_u32("extract top nibble",
                         (uint32_t)biss_extract_bits_msb(frame, 40u, 0u, 4u),
                         0x8u);
    failed += expect_u32("extract position",
                         biss_extract_position(frame, &profile),
                         0xABCDEu);
    failed += expect_bool("anchor matches",
                          biss_anchor_matches(frame, &profile),
                          true);

    biss_profile_t bad_anchor = profile;
    bad_anchor.anchor_value = 0x1u;
    failed += expect_bool("anchor mismatch",
                          biss_anchor_matches(frame, &bad_anchor),
                          false);

    const biss_status_bits_t healthy = biss_extract_status(frame, &profile);
    failed += expect_bool("healthy error inactive", healthy.error_active, false);
    failed += expect_bool("healthy warning inactive", healthy.warning_active, false);
    failed += expect_bool("healthy gate allows",
                          biss_status_gate_allows(healthy,
                                                  BISS_STATUS_GATE_BLOCK_TRIGGER),
                          true);

    const uint64_t error_frame = make_frame(&profile, 0xABCDEu, false, true);
    const biss_status_bits_t error = biss_extract_status(error_frame, &profile);
    failed += expect_bool("active low error", error.error_active, true);
    failed += expect_bool("error gate blocks",
                          biss_status_gate_allows(error,
                                                  BISS_STATUS_GATE_BLOCK_TRIGGER),
                          false);
    failed += expect_bool("count-only gate allows",
                          biss_status_gate_allows(error,
                                                  BISS_STATUS_GATE_COUNT_ONLY),
                          true);

    return failed;
}

static int test_crc6(void)
{
    int failed = 0;
    const biss_profile_t profile = make_profile();
    const uint64_t frame = make_frame(&profile, 0xABCDEu, true, true);

    failed += expect_u32("crc6 zero uninverted",
                         biss_crc6_compute_bits(0u, 36u, false),
                         0x00u);
    failed += expect_u32("crc6 zero inverted",
                         biss_crc6_compute_bits(0u, 36u, true),
                         0x3Fu);
    failed += expect_u32("crc6 biss vector 12 34 status00",
                         biss_crc6_compute_bits((0x12ull << 10) |
                                                (0x34ull << 2),
                                                18u,
                                                true),
                         0x1Au);
    failed += expect_u32("generic crc6 biss vector fedcba",
                         biss_crc_compute_bits(0xFEDCBAull,
                                               24u,
                                               6u,
                                               0x03u,
                                               0u,
                                               0u,
                                               true),
                         0x32u);
    failed += expect_bool("frame crc matches",
                          biss_crc_matches(frame, &profile),
                          true);
    failed += expect_bool("frame crc mismatch",
                          biss_crc_matches(frame ^ (1ull << 8), &profile),
                          false);

    return failed;
}

static int test_crossing(void)
{
    int failed = 0;

    failed += expect_bool("normal crossing",
                          biss_crossed_position(10u, 20u, 15u, 100u),
                          true);
    failed += expect_bool("target at last not crossing",
                          biss_crossed_position(10u, 20u, 10u, 100u),
                          false);
    failed += expect_bool("target at current crossing",
                          biss_crossed_position(10u, 20u, 20u, 100u),
                          true);
    failed += expect_bool("wrap crossing high side",
                          biss_crossed_position(95u, 5u, 98u, 100u),
                          true);
    failed += expect_bool("wrap crossing low side",
                          biss_crossed_position(95u, 5u, 3u, 100u),
                          true);
    failed += expect_bool("wrap no crossing",
                          biss_crossed_position(95u, 5u, 50u, 100u),
                          false);
    failed += expect_bool("invalid modulo",
                          biss_crossed_position(95u, 5u, 3u, 0u),
                          false);

    return failed;
}

int main(void)
{
    int failed = 0;

    failed += test_profile_validation();
    failed += test_bit_extract_anchor_and_status();
    failed += test_crc6();
    failed += test_crossing();

    if (failed != 0) {
        (void)printf("biss_protocol tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("biss_protocol tests passed\n");
    return 0;
}
