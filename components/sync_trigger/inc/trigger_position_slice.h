#ifndef TRIGGER_POSITION_SLICE_H
#define TRIGGER_POSITION_SLICE_H

#include <stdbool.h>
#include <stdint.h>
#include "trigger_position_window.h"   /* instance domain owner */

/* Compact runtime contract: one position record fits one TDMA mailbox RefMem
 * region, so a PREVIEW or FREEZE is never split across frames. Prediction
 * inputs and run identity stay local; followers bind them from their own
 * start-up configuration. Wire, little-endian:
 *   [0] tag = (0xE0 PREVIEW | 0xF0 FREEZE) | position_id
 *   [1..2] prediction_version   [3..6] target_ms_index   [7..9] fine slice */
#define TRIGGER_POSITION_SLICE_WIRE_SIZE 10u
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
    return trigger_position_slice_valid(slice);
}

#endif
