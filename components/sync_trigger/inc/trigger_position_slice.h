#ifndef TRIGGER_POSITION_SLICE_H
#define TRIGGER_POSITION_SLICE_H

#include <stdbool.h>
#include <stdint.h>

/* Compact runtime contract. Full prediction descriptors remain local. */
#define TRIGGER_POSITION_SLICE_SCHEMA 1u
#define TRIGGER_POSITION_SLICE_WIRE_SIZE 10u

typedef enum {
    TRIGGER_POSITION_SLICE_PREVIEW = 0u,
    TRIGGER_POSITION_SLICE_FREEZE = 1u,
} trigger_position_slice_stage_t;

typedef struct {
    uint64_t position_q;
    uint16_t prediction_version;
    uint8_t position_id;
    uint8_t stage;
} trigger_position_slice_t;

static inline bool trigger_position_slice_valid(
    const trigger_position_slice_t *slice, uint32_t subdivisions)
{
    return slice != NULL && slice->position_id >= 1u &&
        slice->position_id <= 3u &&
        slice->stage <= TRIGGER_POSITION_SLICE_FREEZE &&
        slice->prediction_version != 0u &&
        (slice->stage == TRIGGER_POSITION_SLICE_FREEZE ||
         (subdivisions >= 2u && slice->position_q != UINT64_MAX));
}

static inline bool trigger_position_slice_encode(
    const trigger_position_slice_t *slice, uint32_t subdivisions,
    uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE])
{
    if (!wire || !trigger_position_slice_valid(slice, subdivisions)) return false;
    for (uint32_t i = 0u; i < sizeof(slice->position_q); ++i)
        wire[i] = (uint8_t)(slice->position_q >> (8u * i));
    wire[8] = (uint8_t)slice->prediction_version;
    wire[9] = (uint8_t)(slice->prediction_version >> 8u);
    return true;
}

static inline bool trigger_position_slice_decode(
    const uint8_t wire[TRIGGER_POSITION_SLICE_WIRE_SIZE],
    uint8_t position_id, uint8_t stage, uint32_t subdivisions,
    trigger_position_slice_t *slice)
{
    if (!wire || !slice || position_id < 1u || position_id > 3u ||
        stage > TRIGGER_POSITION_SLICE_FREEZE) return false;
    slice->position_q = 0u;
    for (uint32_t i = 0u; i < sizeof(slice->position_q); ++i)
        slice->position_q |= (uint64_t)wire[i] << (8u * i);
    slice->prediction_version = (uint16_t)wire[8] |
        (uint16_t)wire[9] << 8u;
    slice->position_id = position_id;
    slice->stage = stage;
    return trigger_position_slice_valid(slice, subdivisions);
}

#endif
