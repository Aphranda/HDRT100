#ifndef TRIGGER_ACTION_TABLE_H
#define TRIGGER_ACTION_TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include "trigger_deployment.h"
#include "trigger_time_grid.h"

#define TRIGGER_ACTION_TABLE_CAPACITY 8u
typedef enum {
    TRIGGER_ACTION_SWITCH_CODE = 1u,
    TRIGGER_ACTION_GATEWAY_PULSE = 2u
} trigger_action_kind_t;
typedef enum {
    TRIGGER_ACTION_TABLE_OK = 0u,
    TRIGGER_ACTION_TABLE_ARGUMENT = 1u,
    TRIGGER_ACTION_TABLE_DEPLOYMENT = 2u,
    TRIGGER_ACTION_TABLE_GRID = 3u,
    TRIGGER_ACTION_TABLE_ROLE = 4u,
    TRIGGER_ACTION_TABLE_CODE = 5u,
    TRIGGER_ACTION_TABLE_ORDER = 6u,
    TRIGGER_ACTION_TABLE_CONFLICT = 7u,
    TRIGGER_ACTION_TABLE_RESERVED = 8u,
    TRIGGER_ACTION_TABLE_OVERFLOW = 9u,
    TRIGGER_ACTION_TABLE_CRC = 10u,
    TRIGGER_ACTION_TABLE_ID = 11u
} trigger_action_table_result_t;

typedef struct {
    uint32_t action_id,role,kind,argument;
    uint64_t ordinal;
    uint32_t span_slices;
} trigger_action_row_t;
typedef struct {
    uint32_t generation,dep_generation,dep_crc;
    trigger_time_grid_t grid;
    uint32_t count;
    trigger_action_row_t rows[TRIGGER_ACTION_TABLE_CAPACITY];
} trigger_action_table_t;

/* Canonical reflected IEEE CRC32, explicit LE words (u64 low word first):
 * generation, dep_generation, dep_crc, grid.epoch, grid.generation,
 * grid.anchor_vdc_ns, grid.period_ns, grid.subdivisions, count; then count
 * rows: action_id, role, kind, argument, ordinal, span_slices. Unused rows and
 * native structure padding are excluded from BOTH CRC and equality. CRC
 * fingerprints content, not validation/execution authority. Invalid count
 * never reads beyond rows; null/count>capacity returns zero. */
static inline uint32_t trigger_action_table_crc(const trigger_action_table_t *table)
{
    if(!table || table->count>TRIGGER_ACTION_TABLE_CAPACITY) return 0u;
    const uint32_t header[]={table->generation,table->dep_generation,table->dep_crc,
        table->grid.epoch,table->grid.generation,(uint32_t)table->grid.anchor_vdc_ns,
        (uint32_t)(table->grid.anchor_vdc_ns>>32u),table->grid.period_ns,
        table->grid.subdivisions,table->count};
    uint32_t crc=UINT32_MAX;
    for(uint32_t i=0u;i<sizeof(header)/sizeof(header[0]);++i)
        crc=trigger_deployment_crc_word(crc,header[i]);
    for(uint32_t i=0u;i<table->count;++i) {
        const trigger_action_row_t *row=&table->rows[i];
        const uint32_t words[]={row->action_id,row->role,row->kind,row->argument,
            (uint32_t)row->ordinal,(uint32_t)(row->ordinal>>32u),row->span_slices};
        for(uint32_t j=0u;j<sizeof(words)/sizeof(words[0]);++j)
            crc=trigger_deployment_crc_word(crc,words[j]);
    }
    return crc^UINT32_MAX;
}

static inline bool trigger_action_table_equal(const trigger_action_table_t *a,
    const trigger_action_table_t *b)
{
    if(!a || !b || a->count>TRIGGER_ACTION_TABLE_CAPACITY ||
        a->generation!=b->generation || a->dep_generation!=b->dep_generation ||
        a->dep_crc!=b->dep_crc || a->grid.epoch!=b->grid.epoch ||
        a->grid.generation!=b->grid.generation || a->grid.anchor_vdc_ns!=b->grid.anchor_vdc_ns ||
        a->grid.period_ns!=b->grid.period_ns || a->grid.subdivisions!=b->grid.subdivisions ||
        a->count!=b->count) return false;
    for(uint32_t i=0u;i<a->count;++i) {
        const trigger_action_row_t *x=&a->rows[i],*y=&b->rows[i];
        if(x->action_id!=y->action_id || x->role!=y->role || x->kind!=y->kind ||
            x->argument!=y->argument || x->ordinal!=y->ordinal || x->span_slices!=y->span_slices)
            return false;
    }
    return true;
}

/* Arithmetic target only. A later executor must revalidate model, lease,
 * deployment/clock identity and lead; this result never fires an output.
 * On every rejection *out is unchanged. */
static inline trigger_action_table_result_t trigger_action_table_target(
    const trigger_action_table_t *table,uint32_t row_index,uint64_t *out)
{
    if(!table || !out || table->count>TRIGGER_ACTION_TABLE_CAPACITY || row_index>=table->count)
        return TRIGGER_ACTION_TABLE_ARGUMENT;
    if(!trigger_time_grid_valid(&table->grid)) return TRIGGER_ACTION_TABLE_GRID;
    uint64_t value;
    const trigger_time_grid_result_t result=trigger_time_grid_target(
        &table->grid,table->rows[row_index].ordinal,&value);
    if(result==TRIGGER_TIME_GRID_RESERVED) return TRIGGER_ACTION_TABLE_RESERVED;
    if(result==TRIGGER_TIME_GRID_OVERFLOW) return TRIGGER_ACTION_TABLE_OVERFLOW;
    if(result!=TRIGGER_TIME_GRID_OK) return TRIGGER_ACTION_TABLE_GRID;
    *out=value;return TRIGGER_ACTION_TABLE_OK;
}

/* Installation-only kernel: spans are planned half-open occupancy intervals,
 * not external READY/settle/completion evidence. All rows are globally ordered
 * and nonoverlapping, even when different boards execute them. A span may
 * cross a reserved boundary; only the action's start is forbidden there.
 * Deployment shape/identity and its actual CRC are checked. Physical TDMA
 * slot capacity/online binding belongs to the caller (this pure layer does
 * not assume a board/node capacity). No counter action in this first slice.
 * tick_hz is an explicit execution-profile parameter, never a host clock. */
static inline trigger_action_table_result_t trigger_action_table_validate(
    const trigger_action_table_t *table,const trigger_deployment_t *deployment,uint32_t tick_hz)
{
    if(!table || !deployment || !table->generation || !table->count ||
        table->count>TRIGGER_ACTION_TABLE_CAPACITY || !tick_hz || tick_hz>500000000u)
        return TRIGGER_ACTION_TABLE_ARGUMENT;
    if(trigger_deployment_validate(deployment,UINT32_MAX)!=TRIGGER_DEPLOYMENT_OK ||
        table->dep_generation!=deployment->generation) return TRIGGER_ACTION_TABLE_DEPLOYMENT;
    if(table->dep_crc!=trigger_deployment_crc(deployment)) return TRIGGER_ACTION_TABLE_CRC;
    if(!trigger_time_grid_valid(&table->grid)) return TRIGGER_ACTION_TABLE_GRID;
    uint64_t previous_end=0u;
    for(uint32_t i=0u;i<table->count;++i) {
        const trigger_action_row_t *row=&table->rows[i];
        if(!row->action_id) return TRIGGER_ACTION_TABLE_ID;
        for(uint32_t j=0u;j<i;++j)
            if(row->action_id==table->rows[j].action_id) return TRIGGER_ACTION_TABLE_ID;
        if(!row->span_slices) return TRIGGER_ACTION_TABLE_ARGUMENT;
        if(i && row->ordinal<=table->rows[i-1u].ordinal) return TRIGGER_ACTION_TABLE_ORDER;
        if(row->ordinal>UINT64_MAX-row->span_slices) return TRIGGER_ACTION_TABLE_OVERFLOW;
        if(i && row->ordinal<previous_end) return TRIGGER_ACTION_TABLE_CONFLICT;
        uint64_t start,end;
        const trigger_action_table_result_t target=trigger_action_table_target(table,i,&start);
        if(target!=TRIGGER_ACTION_TABLE_OK) return target;
        previous_end=row->ordinal+row->span_slices;
        if(trigger_time_grid_boundary(&table->grid,previous_end,&end)!=TRIGGER_TIME_GRID_OK || end<start)
            return TRIGGER_ACTION_TABLE_OVERFLOW;
        if(row->role>=TRIGGER_DEPLOYMENT_ROLE_COUNT) return TRIGGER_ACTION_TABLE_ROLE;
        if(row->kind==TRIGGER_ACTION_SWITCH_CODE) {
            if(row->role!=TRIGGER_DEPLOYMENT_SLOT_SWITCH_A && row->role!=TRIGGER_DEPLOYMENT_SLOT_SWITCH_B)
                return TRIGGER_ACTION_TABLE_ROLE;
            if(row->argument&~deployment->rows[row->role].output_mask) return TRIGGER_ACTION_TABLE_CODE;
        }else if(row->kind==TRIGGER_ACTION_GATEWAY_PULSE) {
            if(row->role!=TRIGGER_DEPLOYMENT_SLOT_GATEWAY) return TRIGGER_ACTION_TABLE_ROLE;
            const uint64_t ticks=(uint64_t)row->argument*tick_hz;
            if(!row->argument || ticks%UINT64_C(1000000000) ||
                ticks/UINT64_C(1000000000)<2u || ticks/UINT64_C(1000000000)>UINT32_MAX ||
                end-start<row->argument) return TRIGGER_ACTION_TABLE_CODE;
        }else return TRIGGER_ACTION_TABLE_ROLE;
    }
    return TRIGGER_ACTION_TABLE_OK;
}
#endif
