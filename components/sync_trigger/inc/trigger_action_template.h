#ifndef TRIGGER_ACTION_TEMPLATE_H
#define TRIGGER_ACTION_TEMPLATE_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_position_instance.h"

typedef enum {
    TRIGGER_ACTION_TEMPLATE_SWITCH = 1u,
    TRIGGER_ACTION_TEMPLATE_GATEWAY = 2u,
} trigger_action_template_kind_t;

typedef struct {
    uint32_t template_id, generation, role, kind, argument, span_slices;
    int64_t delta_q;
} trigger_action_template_t;

typedef struct {
    uint32_t template_id, role, kind, argument, span_slices;
    trigger_position_instance_t position;
} trigger_action_template_instance_t;

static inline bool trigger_action_template_valid(
    const trigger_action_template_t *t)
{
    return t && t->template_id && t->generation && t->span_slices &&
        (t->kind == TRIGGER_ACTION_TEMPLATE_SWITCH ||
         t->kind == TRIGGER_ACTION_TEMPLATE_GATEWAY);
}

static inline bool trigger_action_template_expand(
    const trigger_action_template_t *t,
    const trigger_position_descriptor_t *descriptor, uint32_t subdivisions,
    trigger_action_template_instance_t *out)
{
    if (!out || !trigger_action_template_valid(t) ||
        !trigger_position_instance_expand(descriptor, subdivisions, t->delta_q,
            &out->position)) return false;
    *out = (trigger_action_template_instance_t){
        .template_id=t->template_id, .role=t->role, .kind=t->kind,
        .argument=t->argument, .span_slices=t->span_slices,
        .position=out->position};
    return true;
}

#endif
