#ifndef TRIGGER_ACTION_RUNTIME_H
#define TRIGGER_ACTION_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_action_template_table.h"

#define TRIGGER_ACTION_RUNTIME_CAPACITY TRIGGER_ACTION_TEMPLATE_CAPACITY

typedef enum {
    TRIGGER_ACTION_RUNTIME_OK=0u,
    TRIGGER_ACTION_RUNTIME_ARGUMENT=1u,
    TRIGGER_ACTION_RUNTIME_BUSY=2u,
    TRIGGER_ACTION_RUNTIME_CONFLICT=3u,
    TRIGGER_ACTION_RUNTIME_BACKWARD=4u,
    TRIGGER_ACTION_RUNTIME_OVERFLOW=5u,
} trigger_action_runtime_result_t;

typedef struct {
    uint32_t position_id, template_id, role, kind, argument;
    uint64_t action_q;
} trigger_action_runtime_event_t;

typedef struct {
    trigger_action_template_table_t templates;
    trigger_position_descriptor_t descriptor;
    trigger_action_template_instance_t instance[TRIGGER_ACTION_RUNTIME_CAPACITY];
    uint32_t active, instance_count, emitted_mask;
    uint64_t last_cursor;
} trigger_action_runtime_t;

static inline trigger_action_runtime_result_t trigger_action_runtime_configure(
    trigger_action_runtime_t *r, const trigger_action_template_table_t *t,
    const trigger_deployment_t *deployment, uint32_t crc)
{
    if(!r || trigger_action_template_table_validate(t,deployment,crc))
        return TRIGGER_ACTION_RUNTIME_ARGUMENT;
    if(r->active) return TRIGGER_ACTION_RUNTIME_BUSY;
    r->templates=*t;r->active=0u;r->instance_count=0u;r->emitted_mask=0u;
    return TRIGGER_ACTION_RUNTIME_OK;
}

static inline trigger_action_runtime_result_t trigger_action_runtime_stage(
    trigger_action_runtime_t *r, const trigger_position_descriptor_t *d,
    uint32_t subdivisions)
{
    if(!r || !d || !r->templates.count || r->active)
        return TRIGGER_ACTION_RUNTIME_ARGUMENT;
    trigger_action_runtime_t next=*r; next.descriptor=*d;next.instance_count=0u;
    next.emitted_mask=0u;next.last_cursor=0u;
    for(uint32_t i=0u;i<r->templates.count;++i){
        if(!trigger_action_template_expand(&r->templates.row[i],d,subdivisions,
                &next.instance[i])) return TRIGGER_ACTION_RUNTIME_ARGUMENT;
        const uint64_t end=next.instance[i].position.action_q+
            next.instance[i].span_slices;
        if(end<next.instance[i].position.action_q)
            return TRIGGER_ACTION_RUNTIME_OVERFLOW;
        for(uint32_t j=0u;j<i;++j){
            const uint64_t a=next.instance[j].position.action_q;
            const uint64_t b=a+next.instance[j].span_slices;
            if(next.instance[i].position.action_q<b && a<end)
                return TRIGGER_ACTION_RUNTIME_CONFLICT;
        }
        next.instance_count++;
    }
    next.active=1u;*r=next;return TRIGGER_ACTION_RUNTIME_OK;
}

static inline trigger_action_runtime_result_t trigger_action_runtime_advance(
    trigger_action_runtime_t *r, uint64_t cursor,
    trigger_action_runtime_event_t *events, uint32_t capacity,
    uint32_t *count)
{
    if(count)*count=0u;
    if(!r || !r->active || (!events && capacity)) return TRIGGER_ACTION_RUNTIME_ARGUMENT;
    if(cursor<r->last_cursor) return TRIGGER_ACTION_RUNTIME_BACKWARD;
    uint32_t emitted=0u;
    for(uint32_t i=0u;i<r->instance_count;++i){
        if(r->emitted_mask&(1u<<i) || r->instance[i].position.action_q>cursor) continue;
        if(emitted>=capacity) break;
        events[emitted++]=(trigger_action_runtime_event_t){
            .position_id=r->instance[i].position.position_id,
            .template_id=r->instance[i].template_id,.role=r->instance[i].role,
            .kind=r->instance[i].kind,.argument=r->instance[i].argument,
            .action_q=r->instance[i].position.action_q};
        r->emitted_mask|=1u<<i;
    }
    r->last_cursor=cursor;if(count)*count=emitted;
    return TRIGGER_ACTION_RUNTIME_OK;
}

#endif
