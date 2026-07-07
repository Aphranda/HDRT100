#ifndef BISS_PROTOCOL_H
#define BISS_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define BISS_PROFILE_DISABLED_OFFSET UINT32_MAX
#define BISS_PROFILE_MAX_FRAME_BITS  64u
#define BISS_PROFILE_MAX_POSITION_BITS 32u
#define BISS_PROFILE_MAX_CRC_BITS    16u

typedef enum {
    BISS_SAMPLE_EDGE_RISING = 0,
    BISS_SAMPLE_EDGE_FALLING = 1,
} biss_sample_edge_t;

typedef enum {
    BISS_STATUS_GATE_IGNORE = 0,
    BISS_STATUS_GATE_COUNT_ONLY = 1,
    BISS_STATUS_GATE_BLOCK_TRIGGER = 2,
} biss_status_gate_policy_t;

typedef enum {
    BISS_CRC_GATE_LATE_COUNT = 0,
    BISS_CRC_GATE_BLOCK_TRIGGER = 1,
} biss_crc_gate_policy_t;

typedef enum {
    BISS_PROFILE_OK = 0,
    BISS_PROFILE_ERR_NULL,
    BISS_PROFILE_ERR_FRAME_BITS,
    BISS_PROFILE_ERR_POSITION_BITS,
    BISS_PROFILE_ERR_POSITION_RANGE,
    BISS_PROFILE_ERR_MODULO,
    BISS_PROFILE_ERR_ANCHOR_RANGE,
    BISS_PROFILE_ERR_STATUS_RANGE,
    BISS_PROFILE_ERR_CRC_RANGE,
    BISS_PROFILE_ERR_CRC_COVER_RANGE,
    BISS_PROFILE_ERR_SAMPLE_EDGE,
    BISS_PROFILE_ERR_SAMPLE_DELAY,
    BISS_PROFILE_ERR_TIMEOUT,
    BISS_PROFILE_ERR_POLICY,
} biss_profile_status_t;

typedef struct {
    uint32_t frame_bits;
    uint32_t position_offset;
    uint32_t position_bits;
    uint32_t modulo;

    uint32_t anchor_offset;
    uint32_t anchor_bits;
    uint64_t anchor_mask;
    uint64_t anchor_value;

    uint32_t error_bit_offset;
    uint32_t warning_bit_offset;
    biss_status_gate_policy_t status_gate_policy;

    uint32_t crc_offset;
    uint32_t crc_bits;
    uint32_t crc_cover_offset;
    uint32_t crc_cover_bits;
    uint8_t crc_polynomial;
    uint8_t crc_init;
    uint8_t crc_xor;
    bool crc_invert;
    biss_crc_gate_policy_t crc_gate_policy;

    biss_sample_edge_t sample_edge;
    uint32_t sample_delay_cycles;
    uint32_t pio_cycles_per_bit;
    uint32_t timeout_us;
} biss_profile_t;

typedef struct {
    bool error_active;
    bool warning_active;
} biss_status_bits_t;

biss_profile_status_t biss_profile_validate(const biss_profile_t *profile);

uint64_t biss_extract_bits_msb(uint64_t frame,
                               uint32_t frame_bits,
                               uint32_t offset,
                               uint32_t bits);

bool biss_anchor_matches(uint64_t frame, const biss_profile_t *profile);

uint32_t biss_extract_position(uint64_t frame, const biss_profile_t *profile);

biss_status_bits_t biss_extract_status(uint64_t frame,
                                       const biss_profile_t *profile);

bool biss_status_gate_allows(biss_status_bits_t status,
                             biss_status_gate_policy_t policy);

uint8_t biss_crc6_update(uint8_t crc, bool bit);
uint8_t biss_crc6_compute_bits(uint64_t value, uint32_t bits, bool invert);
uint32_t biss_crc_compute_bits(uint64_t value,
                               uint32_t bits,
                               uint32_t crc_bits,
                               uint32_t polynomial,
                               uint32_t init,
                               uint32_t xor_value,
                               bool invert);
uint32_t biss_crc_compute_frame(uint64_t frame, const biss_profile_t *profile);
bool biss_crc_matches(uint64_t frame, const biss_profile_t *profile);

bool biss_crossed_position(uint32_t last,
                           uint32_t current,
                           uint32_t target,
                           uint32_t modulo);

#endif
