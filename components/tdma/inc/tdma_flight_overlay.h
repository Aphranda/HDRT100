#ifndef TDMA_FLIGHT_OVERLAY_H
#define TDMA_FLIGHT_OVERLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_flight_engine.h"

/* Internal PIO catalog tokens, never accepted from a transport payload. */
#define TDMA_FLIGHT_OVERLAY_TOKEN_LIVE 0xA026u
#define TDMA_FLIGHT_OVERLAY_TOKEN_INVERT 0xA02Eu
#define TDMA_FLIGHT_OVERLAY_TOKEN_ZERO 0xE020u
#define TDMA_FLIGHT_OVERLAY_TOKEN_ONE 0xE021u
#define TDMA_FLIGHT_OVERLAY_LIVE_WORD \
    ((TDMA_FLIGHT_OVERLAY_TOKEN_LIVE << 16u) | TDMA_FLIGHT_OVERLAY_TOKEN_LIVE)
#define TDMA_FLIGHT_OVERLAY_COMMAND_WORDS_PER_BYTE 4u
#define TDMA_FLIGHT_OVERLAY_STORAGE_WINDOWS 2u
#define TDMA_FLIGHT_OVERLAY_RUN_MAX \
    (2u * TDMA_FLIGHT_OVERLAY_STORAGE_WINDOWS + 2u)
#define TDMA_FLIGHT_OVERLAY_TOKEN_WORD_MAX \
    ((TDMA_TRANSPORT_FRAME_HEADER_SIZE + TDMA_FLIGHT_SHORT_SLOT_SIZE) * \
         TDMA_FLIGHT_OVERLAY_COMMAND_WORDS_PER_BYTE + \
     TDMA_FLIGHT_OVERLAY_STORAGE_WINDOWS + 1u)
#define TDMA_FLIGHT_OVERLAY_SOURCE_PASS UINT32_MAX

/* Before DMA binding, control is read_increment, read_address is a token
 * offset (or SOURCE_PASS), and write_address is zero. The adapter binds these
 * four words to the RP2350 AL3 register order only after the plan is complete. */
typedef struct {
    uint32_t control;
    uint32_t write_address;
    uint32_t transfer_count;
    uint32_t read_address;
} tdma_flight_overlay_dma_run_t;

typedef struct {
    tdma_flight_overlay_dma_run_t run[TDMA_FLIGHT_OVERLAY_RUN_MAX];
    uint32_t token[TDMA_FLIGHT_OVERLAY_TOKEN_WORD_MAX];
    uint32_t run_count;
    uint32_t token_word_count;
    uint32_t command_word_count;
    uint32_t replacement_byte_count;
} tdma_flight_overlay_plan_t;

typedef struct {
    uint32_t physical_byte_count;
    uint32_t outer_header_size;
    uint32_t alignment_byte_shift;
    uint32_t alignment_bit_shift;
    uint32_t local_slot_id;
    uint32_t header_write_mask;
    uint32_t final_bit_pc;
} tdma_flight_overlay_config_t;

/* Header changes XOR the actual wire bits; they never replay a predicted
 * sequence's CRC. Local payload changes still select owner-prepared values.
 * The owner supplies two valid FLIGHT_MUTABLE models at the admitted hop. */
bool tdma_flight_overlay_build_plan(
    const uint8_t *incoming_packet, const uint8_t *processed_packet,
    size_t packet_size, const uint32_t *force_payload_bitmap,
    size_t force_payload_bitmap_words, const tdma_flight_overlay_config_t *config,
    tdma_flight_overlay_plan_t *plan);

bool tdma_flight_overlay_build_pass_plan(uint32_t physical_byte_count,
                                        uint32_t final_bit_pc,
                                        tdma_flight_overlay_plan_t *plan);

bool tdma_flight_overlay_plan_valid(const tdma_flight_overlay_plan_t *plan,
                                   uint32_t final_bit_pc);

#define TDMA_FLIGHT_OVERLAY_SCRIPT_PASS 0x00000000u
#define TDMA_FLIGHT_OVERLAY_SCRIPT_REPLACE 0x80000000u
#define TDMA_FLIGHT_OVERLAY_SCRIPT_END 0xC0000000u

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
                               uint32_t force_replace_packet_offset,
                               const uint32_t *force_replace_bitmap,
                               size_t force_replace_bitmap_words,
                               uint32_t *script,
                               size_t script_capacity,
                               tdma_flight_overlay_result_t *result);

uint8_t tdma_flight_overlay_script_byte(uint32_t word);

#endif
