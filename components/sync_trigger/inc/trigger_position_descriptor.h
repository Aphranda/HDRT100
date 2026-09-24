#ifndef TRIGGER_POSITION_DESCRIPTOR_H
#define TRIGGER_POSITION_DESCRIPTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "trigger_deployment.h"

#define TRIGGER_POSITION_DESCRIPTOR_SCHEMA 1u
#define TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_SIZE 4u
#define TRIGGER_POSITION_DESCRIPTOR_WIRE_WORDS 17u
#define TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE \
    (TRIGGER_POSITION_DESCRIPTOR_WIRE_WORDS * TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_SIZE)
#define TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT \
    TRIGGER_POSITION_DESCRIPTOR_WIRE_WORDS
#define TRIGGER_POSITION_DESCRIPTOR_NODE_COUNT 8u

typedef struct {
    uint32_t schema;
    uint32_t run_id, sync_epoch;
    uint32_t position_id, count_epoch;
    uint64_t target_count;
    uint32_t prediction_version;
    uint64_t target_ms_index;
    uint32_t target_fine_slice_index;
    uint32_t grid_generation;
    uint32_t deployment_generation, deployment_crc;
    uint32_t valid_from_sync, expiry_sync;
    uint32_t descriptor_crc32;
} trigger_position_descriptor_t;

static inline bool trigger_position_descriptor_valid(
    const trigger_position_descriptor_t *d, uint32_t subdivisions);

static inline uint32_t trigger_position_descriptor_crc(
    const trigger_position_descriptor_t *d)
{
    if (!d) return 0u;
    const uint32_t words[] = {
        d->schema, d->run_id, d->sync_epoch, d->position_id, d->count_epoch,
        (uint32_t)d->target_count, (uint32_t)(d->target_count >> 32u),
        d->prediction_version, (uint32_t)d->target_ms_index,
        (uint32_t)(d->target_ms_index >> 32u), d->target_fine_slice_index,
        d->grid_generation, d->deployment_generation, d->deployment_crc,
        d->valid_from_sync, d->expiry_sync,
    };
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0u; i < sizeof(words) / sizeof(words[0]); ++i)
        crc = trigger_deployment_crc_word(crc, words[i]);
    return crc ^ UINT32_MAX;
}

static inline void trigger_position_descriptor_put32(uint8_t *p, uint32_t v)
{
    for (uint32_t i=0u;i<4u;++i) p[i]=(uint8_t)(v>>(8u*i));
}

static inline uint32_t trigger_position_descriptor_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8u |
        (uint32_t)p[2]<<16u | (uint32_t)p[3]<<24u;
}

static inline bool trigger_position_descriptor_encode(
    const trigger_position_descriptor_t *d,
    uint8_t wire[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE])
{
    if (!wire || !trigger_position_descriptor_valid(d, UINT32_MAX)) return false;
    trigger_position_descriptor_t next=*d;
    next.descriptor_crc32=trigger_position_descriptor_crc(d);
    const uint32_t words[]={next.schema,next.run_id,next.sync_epoch,next.position_id,
        next.count_epoch,(uint32_t)next.target_count,(uint32_t)(next.target_count>>32u),
        next.prediction_version,(uint32_t)next.target_ms_index,
        (uint32_t)(next.target_ms_index>>32u),next.target_fine_slice_index,
        next.grid_generation,next.deployment_generation,next.deployment_crc,
        next.valid_from_sync,next.expiry_sync,next.descriptor_crc32};
    for(uint32_t i=0u;i<TRIGGER_POSITION_DESCRIPTOR_WIRE_WORDS;++i)
        trigger_position_descriptor_put32(wire+i*4u,words[i]);
    return true;
}

static inline bool trigger_position_descriptor_decode(
    const uint8_t wire[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE],
    trigger_position_descriptor_t *out)
{
    if (!wire || !out) return false;
    trigger_position_descriptor_t d={
        .schema=trigger_position_descriptor_get32(wire),
        .run_id=trigger_position_descriptor_get32(wire+4u),
        .sync_epoch=trigger_position_descriptor_get32(wire+8u),
        .position_id=trigger_position_descriptor_get32(wire+12u),
        .count_epoch=trigger_position_descriptor_get32(wire+16u),
        .target_count=(uint64_t)trigger_position_descriptor_get32(wire+20u)|
            (uint64_t)trigger_position_descriptor_get32(wire+24u)<<32u,
        .prediction_version=trigger_position_descriptor_get32(wire+28u),
        .target_ms_index=(uint64_t)trigger_position_descriptor_get32(wire+32u)|
            (uint64_t)trigger_position_descriptor_get32(wire+36u)<<32u,
        .target_fine_slice_index=trigger_position_descriptor_get32(wire+40u),
        .grid_generation=trigger_position_descriptor_get32(wire+44u),
        .deployment_generation=trigger_position_descriptor_get32(wire+48u),
        .deployment_crc=trigger_position_descriptor_get32(wire+52u),
        .valid_from_sync=trigger_position_descriptor_get32(wire+56u),
        .expiry_sync=trigger_position_descriptor_get32(wire+60u),
        .descriptor_crc32=trigger_position_descriptor_get32(wire+64u)};
    if (!trigger_position_descriptor_valid(&d, UINT32_MAX) ||
        d.descriptor_crc32 != trigger_position_descriptor_crc(&d)) return false;
    *out=d; return true;
}

typedef enum {
    TRIGGER_POSITION_FRAGMENT_PROGRESS=0u,
    TRIGGER_POSITION_FRAGMENT_COMPLETE=1u,
    TRIGGER_POSITION_FRAGMENT_REJECTED=2u,
} trigger_position_fragment_result_t;

typedef struct {
    uint8_t active, source_slot, target_mask, next_fragment;
    uint16_t first_transport_seq16;
    uint8_t wire[TRIGGER_POSITION_DESCRIPTOR_WIRE_SIZE];
    uint32_t accepted_fragment_count, completed_count, rejected_count;
} trigger_position_fragment_context_t;

static inline void trigger_position_fragment_reset(
    trigger_position_fragment_context_t *c)
{
    if (!c) return;
    const uint32_t a=c->accepted_fragment_count,b=c->completed_count,r=c->rejected_count;
    memset(c,0,sizeof(*c)); c->accepted_fragment_count=a;
    c->completed_count=b; c->rejected_count=r;
}

static inline trigger_position_fragment_result_t trigger_position_fragment_push(
    trigger_position_fragment_context_t *c,uint8_t source_slot,uint8_t target_mask,
    uint16_t transport_seq16,uint8_t fragment_index,uint8_t fragment_count,
    const uint8_t data[TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_SIZE],
    trigger_position_descriptor_t *out)
{
    if(out) memset(out,0,sizeof(*out));
    if(!c || !data || source_slot>=TRIGGER_POSITION_DESCRIPTOR_NODE_COUNT ||
       !target_mask || fragment_count!=TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT ||
       fragment_index>=fragment_count){if(c){c->rejected_count++;trigger_position_fragment_reset(c);}return TRIGGER_POSITION_FRAGMENT_REJECTED;}
    if(fragment_index==0u){if(c->active){c->rejected_count++;trigger_position_fragment_reset(c);return TRIGGER_POSITION_FRAGMENT_REJECTED;}trigger_position_fragment_reset(c);c->active=1u;c->source_slot=source_slot;c->target_mask=target_mask;c->next_fragment=0u;c->first_transport_seq16=transport_seq16;}
    else if(!c->active||c->source_slot!=source_slot||c->target_mask!=target_mask||c->next_fragment!=fragment_index||(uint16_t)(c->first_transport_seq16+fragment_index)!=transport_seq16){c->rejected_count++;trigger_position_fragment_reset(c);return TRIGGER_POSITION_FRAGMENT_REJECTED;}
    memcpy(c->wire+(size_t)fragment_index*TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_SIZE,data,TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_SIZE);c->accepted_fragment_count++;c->next_fragment=(uint8_t)(fragment_index+1u);
    if(c->next_fragment<fragment_count)return TRIGGER_POSITION_FRAGMENT_PROGRESS;
    c->active=0u;c->next_fragment=0u;c->completed_count++;
    return trigger_position_descriptor_decode(c->wire,out)?TRIGGER_POSITION_FRAGMENT_COMPLETE:TRIGGER_POSITION_FRAGMENT_REJECTED;
}

static inline bool trigger_position_descriptor_valid(
    const trigger_position_descriptor_t *d, uint32_t subdivisions)
{
    return d && d->schema == TRIGGER_POSITION_DESCRIPTOR_SCHEMA &&
        d->run_id && d->sync_epoch && d->position_id && d->count_epoch &&
        d->prediction_version && d->grid_generation &&
        d->deployment_generation && d->deployment_crc && subdivisions >= 2u &&
        d->target_fine_slice_index < subdivisions &&
        d->valid_from_sync <= d->expiry_sync;
}

/* Position wire fields become a local absolute grid ordinal only after the
 * receiver validates the descriptor and supplies its configured subdivision. */
static inline bool trigger_position_descriptor_target_q(
    const trigger_position_descriptor_t *d, uint32_t subdivisions,
    uint64_t *out_q)
{
    if (!out_q || !trigger_position_descriptor_valid(d, subdivisions) ||
        d->target_ms_index > (UINT64_MAX - d->target_fine_slice_index) /
            subdivisions) return false;
    *out_q = d->target_ms_index * subdivisions +
        d->target_fine_slice_index;
    return true;
}

#endif
