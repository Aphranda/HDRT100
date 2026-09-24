#ifndef TRIGGER_POSITION_INSTANCE_H
#define TRIGGER_POSITION_INSTANCE_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_position_descriptor.h"

typedef struct {
    uint32_t position_id, prediction_version, count_epoch;
    uint64_t target_q, action_q;
} trigger_position_instance_t;

static inline bool trigger_position_instance_expand(
    const trigger_position_descriptor_t *descriptor, uint32_t subdivisions,
    int64_t delta_q, trigger_position_instance_t *out)
{
    uint64_t target_q;
    if (!out || !trigger_position_descriptor_target_q(descriptor,
            subdivisions, &target_q)) return false;
    if (delta_q < 0) {
        const uint64_t magnitude = (uint64_t)(-(delta_q + 1)) + 1u;
        if (magnitude > target_q) return false;
        out->action_q = target_q - magnitude;
    } else {
        const uint64_t offset = (uint64_t)delta_q;
        if (target_q > UINT64_MAX - offset) return false;
        out->action_q = target_q + offset;
    }
    out->position_id = descriptor->position_id;
    out->prediction_version = descriptor->prediction_version;
    out->count_epoch = descriptor->count_epoch;
    out->target_q = target_q;
    return true;
}

#endif
