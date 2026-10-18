#ifndef TRIGGER_ACTION_TEMPLATE_TABLE_H
#define TRIGGER_ACTION_TEMPLATE_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_action_template.h"
#include "trigger_deployment.h"

/* Per-position group sequence (T2-SEQ-001 / TRIGGER_T2_TODO.md:110): a position
 * carries 16 groups, so the installable program must hold at least that many
 * rows. Existing rows keep their indices; only the bound widens. */
/* One action needs three rows (SWITCH_A + SWITCH_B + GATEWAY TRIG), so a position that
 * carries the full 16 actions (8 channels x 2 polarizations) needs 48. The table is shared by
 * all roles, which is why 8 was enough only for two actions. */
#define TRIGGER_ACTION_TEMPLATE_CAPACITY 48u

typedef enum {
    TRIGGER_ACTION_TEMPLATE_TABLE_OK = 0u,
    TRIGGER_ACTION_TEMPLATE_TABLE_ARGUMENT = 1u,
    TRIGGER_ACTION_TEMPLATE_TABLE_ID = 2u,
    TRIGGER_ACTION_TEMPLATE_TABLE_ROLE = 3u,
    TRIGGER_ACTION_TEMPLATE_TABLE_KIND = 4u,
    TRIGGER_ACTION_TEMPLATE_TABLE_CRC = 5u,
} trigger_action_template_table_result_t;

typedef struct {
    uint32_t generation, count;
    trigger_action_template_t row[TRIGGER_ACTION_TEMPLATE_CAPACITY];
} trigger_action_template_table_t;

static inline uint32_t trigger_action_template_table_crc(
    const trigger_action_template_table_t *t)
{
    if (!t || t->count > TRIGGER_ACTION_TEMPLATE_CAPACITY) return 0u;
    uint32_t crc=trigger_deployment_crc_word(UINT32_MAX,t->generation);
    crc=trigger_deployment_crc_word(crc,t->count);
    for(uint32_t i=0u;i<t->count;++i){
        const trigger_action_template_t *r=&t->row[i];
        const uint32_t words[]={r->template_id,r->generation,r->role,r->kind,
            r->argument,r->span_slices,(uint32_t)r->delta_q,
            (uint32_t)((uint64_t)r->delta_q>>32u)};
        for(uint32_t j=0u;j<sizeof(words)/sizeof(words[0]);++j)
            crc=trigger_deployment_crc_word(crc,words[j]);
    }
    return crc^UINT32_MAX;
}

static inline trigger_action_template_table_result_t
trigger_action_template_table_validate(
    const trigger_action_template_table_t *t,
    const trigger_deployment_t *deployment, uint32_t expected_crc)
{
    if(!t || !deployment || !t->generation || !t->count ||
       t->count>TRIGGER_ACTION_TEMPLATE_CAPACITY) return TRIGGER_ACTION_TEMPLATE_TABLE_ARGUMENT;
    if(trigger_action_template_table_crc(t)!=expected_crc) return TRIGGER_ACTION_TEMPLATE_TABLE_CRC;
    for(uint32_t i=0u;i<t->count;++i){
        const trigger_action_template_t *r=&t->row[i];
        if(!trigger_action_template_valid(r)) return TRIGGER_ACTION_TEMPLATE_TABLE_ARGUMENT;
        for(uint32_t j=0u;j<i;++j) if(r->template_id==t->row[j].template_id)
            return TRIGGER_ACTION_TEMPLATE_TABLE_ID;
        if(r->role>=TRIGGER_DEPLOYMENT_ROLE_COUNT) return TRIGGER_ACTION_TEMPLATE_TABLE_ROLE;
        if(r->kind==TRIGGER_ACTION_TEMPLATE_SWITCH &&
           r->role!=TRIGGER_DEPLOYMENT_SLOT_SWITCH_A &&
           r->role!=TRIGGER_DEPLOYMENT_SLOT_SWITCH_B) return TRIGGER_ACTION_TEMPLATE_TABLE_ROLE;
        if(r->kind==TRIGGER_ACTION_TEMPLATE_GATEWAY &&
           r->role!=TRIGGER_DEPLOYMENT_SLOT_GATEWAY) return TRIGGER_ACTION_TEMPLATE_TABLE_ROLE;
        if(r->kind==TRIGGER_ACTION_TEMPLATE_SWITCH &&
           (r->argument & ~deployment->rows[r->role].output_mask))
            return TRIGGER_ACTION_TEMPLATE_TABLE_KIND;
    }
    return TRIGGER_ACTION_TEMPLATE_TABLE_OK;
}

#endif
