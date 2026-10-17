#ifndef TRIGGER_POSITION_SLICE_H
#define TRIGGER_POSITION_SLICE_H

#include <stdbool.h>
#include <stdint.h>
#include "trigger_position_window.h"   /* instance domain owner */

/* Compact runtime contract: one position record fits one TDMA mailbox payload, so a
 * PREVIEW or FREEZE is never split across frames. Prediction inputs and run identity stay
 * local; followers bind them from their own start-up configuration. Wire, little-endian:
 *   [0] tag = (0xE0 PREVIEW | 0xF0 FREEZE) | position_id
 *   [1..2] prediction_version   [3..6] target_ms_index   [7..9] fine slice
 *   [10] next_position_id (0 = none)
 *   [11..14] next_target_ms_index (LE32)
 *   [15] reserved, zero
 * The NEXT position rides in the SAME slice instead of a second block (owner ruling
 * 2026-10-17: "extend the previous 10 B", "reuse the first position's machinery, saves
 * space and time"). It reuses this encoder/decoder and the existing PREVIEW/FREEZE path;
 * the record no longer carries the VDC observation, which freed exactly the space needed
 * (VDC area 8..13 + RefMem area 14..23 = 16 contiguous bytes). */
#define TRIGGER_POSITION_SLICE_WIRE_SIZE 16u
#define TRIGGER_POSITION_SLICE_TAG_PREVIEW 0xE0u
#define TRIGGER_POSITION_SLICE_TAG_FREEZE 0xF0u
#define TRIGGER_POSITION_SLICE_FINE_MAX 0xFFFFFFu

typedef enum {
    TRIGGER_POSITION_SLICE_PREVIEW = 0u,
    TRIGGER_POSITION_SLICE_FREEZE = 1u,
} trigger_position_slice_stage_t;

typedef struct {
    uint32_t target_ms_index;
    uint32_t target_fine_slice_index;
    uint16_t prediction_version;
    uint8_t position_id;
    uint8_t stage;
    /* Optional cache of the NEXT position (0 = none). Advisory only: the follower may use
     * it to pre-warm, but execution still follows the existing FREEZE path. */
    uint32_t next_position_id;
    uint32_t next_target_ms_index;
} trigger_position_slice_t;

static inline bool trigger_position_slice_tagged(uint8_t tag)
{
    return (tag & 0xE0u) == TRIGGER_POSITION_SLICE_TAG_PREVIEW;
}

static inline bool trigger_position_slice_valid(const trigger_position_slice_t *slice)
{
    return slice != NULL && slice->position_id >= 1u &&
        slice->position_id <= TRIGGER_POSITION_INSTANCE_MAX &&
        slice->stage <= TRIGGER_POSITION_SLICE_FREEZE &&
        slice->prediction_version != 0u &&
        slice->target_fine_slice_index <= TRIGGER_POSITION_SLICE_FINE_MAX;
}

static inline bool trigger_position_slice_encode(
    const trigger_position_slice_t *slice, uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE])
{
    if (!wire || !trigger_position_slice_valid(slice)) return false;
    wire[0] = (uint8_t)((slice->stage == TRIGGER_POSITION_SLICE_FREEZE ?
        TRIGGER_POSITION_SLICE_TAG_FREEZE : TRIGGER_POSITION_SLICE_TAG_PREVIEW) |
        slice->position_id);
    wire[1] = (uint8_t)slice->prediction_version;
    wire[2] = (uint8_t)(slice->prediction_version >> 8u);
    for (uint32_t i = 0u; i < 4u; ++i)
        wire[3u + i] = (uint8_t)(slice->target_ms_index >> (8u * i));
    for (uint32_t i = 0u; i < 3u; ++i)
        wire[7u + i] = (uint8_t)(slice->target_fine_slice_index >> (8u * i));
    /* NEXT position cache: same slice, no second block. Zero = none. */
    wire[10] = (uint8_t)(slice->next_position_id & 0xFFu);
    for (uint32_t i = 0u; i < 4u; ++i)
        wire[11u + i] = (uint8_t)(slice->next_target_ms_index >> (8u * i));
    wire[15] = 0u;
    return true;
}

static inline bool trigger_position_slice_decode(
    const uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE], trigger_position_slice_t *slice)
{
    if (!wire || !slice || !trigger_position_slice_tagged(wire[0])) return false;
    *slice = (trigger_position_slice_t){
        .position_id = (uint8_t)(wire[0] & 0x0Fu),
        .stage = (wire[0] & 0xF0u) == TRIGGER_POSITION_SLICE_TAG_FREEZE ?
            TRIGGER_POSITION_SLICE_FREEZE : TRIGGER_POSITION_SLICE_PREVIEW,
        .prediction_version = (uint16_t)(wire[1] | (uint16_t)wire[2] << 8u),
    };
    for (uint32_t i = 0u; i < 4u; ++i)
        slice->target_ms_index |= (uint32_t)wire[3u + i] << (8u * i);
    for (uint32_t i = 0u; i < 3u; ++i)
        slice->target_fine_slice_index |= (uint32_t)wire[7u + i] << (8u * i);
    /* NEXT position cache from the same slice (0 = none). */
    slice->next_position_id = wire[10];
    for (uint32_t i = 0u; i < 4u; ++i)
        slice->next_target_ms_index |= (uint32_t)wire[11u + i] << (8u * i);
    return trigger_position_slice_valid(slice);
}

/* Last received NEXT-position cache (T2-PROVISIONAL-001). The record already carries the
 * following position in the same slice, so no extra state machine is needed: the receive
 * path records what it decoded and the runtime reads it when it decides whether a position
 * whose FREEZE is missing may still execute on the cached target. Header-inline storage
 * keeps it visible to the host harness (which rebuilds module translation units from a
 * source slice and cannot see module file-scope state). */
typedef struct {
    uint32_t position_id;
    uint32_t target_ms_index;
} trigger_position_next_cache_t;

static inline trigger_position_next_cache_t *trigger_position_next_cache(void)
{
    static trigger_position_next_cache_t s_cache;
    return &s_cache;
}

#endif
