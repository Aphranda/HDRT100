#ifndef REFMEM_ACTION_PROBE_H
#define REFMEM_ACTION_PROBE_H

/* Core0 diagnostic SET_VALUE only: no GPIO, READY, fence, or T2 timing claim.
 * The caller serializes this state and validates physical segment ownership,
 * transport sequence, live deployment, and STOP/session cancellation. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define REFMEM_ACTION_PROBE_WIRE_SIZE 60u
#define REFMEM_ACTION_PROBE_FRAGMENT_SIZE 6u
#define REFMEM_ACTION_PROBE_FRAGMENT_COUNT 10u
#define REFMEM_ACTION_PROBE_NODE_COUNT 8u
#define REFMEM_ACTION_PROBE_ROLE_COUNT 4u
#define REFMEM_ACTION_PROBE_REQUEST 0xD1u
#define REFMEM_ACTION_PROBE_RECEIPT 0xD2u
#define REFMEM_ACTION_PROBE_SET_VALUE 1u

typedef enum {
    REFMEM_ACTION_PROBE_INCOMPLETE = 0,
    REFMEM_ACTION_PROBE_COMPLETE,
    REFMEM_ACTION_PROBE_APPLIED,
    REFMEM_ACTION_PROBE_DUPLICATE,
    REFMEM_ACTION_PROBE_REJECT_ARGUMENT,
    REFMEM_ACTION_PROBE_REJECT_FRAGMENT,
    REFMEM_ACTION_PROBE_REJECT_CRC,
    REFMEM_ACTION_PROBE_REJECT_INACTIVE,
    REFMEM_ACTION_PROBE_REJECT_KIND,
    REFMEM_ACTION_PROBE_REJECT_IDENTITY,
    REFMEM_ACTION_PROBE_REJECT_OLD,
    REFMEM_ACTION_PROBE_REJECT_CONFLICT
} refmem_action_probe_result_t;

typedef struct {
    uint32_t session;
    uint32_t deployment_generation;
    uint32_t deployment_crc32;
    uint32_t request_id;
    uint64_t source_uid;
    uint64_t target_uid;
    uint32_t source_slot;
    uint32_t target_slot;
    uint32_t business_role;
    uint32_t opcode;
    uint32_t argument;
} refmem_action_probe_record_t;

typedef struct {
    uint8_t wire[REFMEM_ACTION_PROBE_WIRE_SIZE];
    uint8_t next_index;
    uint8_t kind;
    bool active;
} refmem_action_probe_assembly_t;

typedef struct {
    uint32_t session;
    uint32_t deployment_generation;
    uint32_t deployment_crc32;
    uint64_t local_uid;
    uint32_t local_slot;
    uint32_t business_role;
} refmem_action_probe_config_t;

typedef struct {
    refmem_action_probe_config_t config;
    refmem_action_probe_record_t last[REFMEM_ACTION_PROBE_NODE_COUNT];
    uint32_t session_high_water;
    uint32_t action_count;
    uint32_t value;
    bool active;
} refmem_action_probe_engine_t;

static inline void refmem_action_probe_put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}

static inline uint32_t refmem_action_probe_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u |
        (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}

static inline uint32_t refmem_action_probe_crc32(const uint8_t *p, unsigned n)
{
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static inline bool refmem_action_probe_record_valid(const refmem_action_probe_record_t *r)
{
    return r != NULL && r->session != 0u && r->deployment_generation != 0u &&
        r->request_id != 0u && r->source_uid != 0u && r->target_uid != 0u &&
        r->source_uid != r->target_uid && r->source_slot < REFMEM_ACTION_PROBE_NODE_COUNT &&
        r->target_slot < REFMEM_ACTION_PROBE_NODE_COUNT && r->source_slot != r->target_slot &&
        r->business_role < REFMEM_ACTION_PROBE_ROLE_COUNT && r->opcode == REFMEM_ACTION_PROBE_SET_VALUE;
}

static inline bool refmem_action_probe_encode(const refmem_action_probe_record_t *r,
    uint8_t wire[REFMEM_ACTION_PROBE_WIRE_SIZE])
{
    if (wire == NULL || !refmem_action_probe_record_valid(r)) return false;
    refmem_action_probe_put32(wire, r->session);
    refmem_action_probe_put32(wire + 4u, r->deployment_generation);
    refmem_action_probe_put32(wire + 8u, r->deployment_crc32);
    refmem_action_probe_put32(wire + 12u, r->request_id);
    refmem_action_probe_put32(wire + 16u, (uint32_t)r->source_uid);
    refmem_action_probe_put32(wire + 20u, (uint32_t)(r->source_uid >> 32u));
    refmem_action_probe_put32(wire + 24u, (uint32_t)r->target_uid);
    refmem_action_probe_put32(wire + 28u, (uint32_t)(r->target_uid >> 32u));
    refmem_action_probe_put32(wire + 32u, r->source_slot);
    refmem_action_probe_put32(wire + 36u, r->target_slot);
    refmem_action_probe_put32(wire + 40u, r->business_role);
    refmem_action_probe_put32(wire + 44u, r->opcode);
    refmem_action_probe_put32(wire + 48u, r->argument);
    refmem_action_probe_put32(wire + 52u, 0u); /* Canonical reserved word. */
    refmem_action_probe_put32(wire + 56u, refmem_action_probe_crc32(wire, 56u));
    return true;
}

static inline bool refmem_action_probe_decode(const uint8_t wire[REFMEM_ACTION_PROBE_WIRE_SIZE],
    refmem_action_probe_record_t *r)
{
    if (wire == NULL || r == NULL || refmem_action_probe_get32(wire + 52u) != 0u ||
        refmem_action_probe_get32(wire + 56u) != refmem_action_probe_crc32(wire, 56u)) return false;
    refmem_action_probe_record_t next = {
        .session = refmem_action_probe_get32(wire),
        .deployment_generation = refmem_action_probe_get32(wire + 4u),
        .deployment_crc32 = refmem_action_probe_get32(wire + 8u),
        .request_id = refmem_action_probe_get32(wire + 12u),
        .source_uid = (uint64_t)refmem_action_probe_get32(wire + 16u) |
            (uint64_t)refmem_action_probe_get32(wire + 20u) << 32u,
        .target_uid = (uint64_t)refmem_action_probe_get32(wire + 24u) |
            (uint64_t)refmem_action_probe_get32(wire + 28u) << 32u,
        .source_slot = refmem_action_probe_get32(wire + 32u),
        .target_slot = refmem_action_probe_get32(wire + 36u),
        .business_role = refmem_action_probe_get32(wire + 40u),
        .opcode = refmem_action_probe_get32(wire + 44u),
        .argument = refmem_action_probe_get32(wire + 48u)
    };
    if (!refmem_action_probe_record_valid(&next)) return false;
    *r = next;
    return true;
}

static inline bool refmem_action_probe_equal(const refmem_action_probe_record_t *a,
    const refmem_action_probe_record_t *b)
{
    uint8_t aw[REFMEM_ACTION_PROBE_WIRE_SIZE], bw[REFMEM_ACTION_PROBE_WIRE_SIZE];
    return refmem_action_probe_encode(a, aw) && refmem_action_probe_encode(b, bw) &&
        memcmp(aw, bw, sizeof(aw)) == 0;
}

static inline bool refmem_action_probe_fragment_encode(const refmem_action_probe_record_t *r,
    uint8_t index, uint16_t *field_id, uint32_t *value)
{
    uint8_t wire[REFMEM_ACTION_PROBE_WIRE_SIZE];
    if (field_id == NULL || value == NULL || index >= REFMEM_ACTION_PROBE_FRAGMENT_COUNT ||
        !refmem_action_probe_encode(r, wire)) return false;
    const uint8_t *p = wire + index * REFMEM_ACTION_PROBE_FRAGMENT_SIZE;
    *field_id = (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8u);
    *value = refmem_action_probe_get32(p + 2u);
    return true;
}

static inline void refmem_action_probe_assembly_reset(refmem_action_probe_assembly_t *a)
{
    if (a != NULL) memset(a, 0, sizeof(*a));
}

/* Caller maintains separate assemblers per physical source and message kind.
 * An index-zero fragment restarts assembly. A transport duplicate is accepted
 * only when it exactly repeats the immediately preceding fragment. */
static inline refmem_action_probe_result_t refmem_action_probe_assembly_push(
    refmem_action_probe_assembly_t *a, uint8_t kind, uint8_t index,
    uint16_t field_id, uint32_t value, refmem_action_probe_record_t *out)
{
    if (a == NULL || out == NULL) return REFMEM_ACTION_PROBE_REJECT_ARGUMENT;
    if ((kind != REFMEM_ACTION_PROBE_REQUEST && kind != REFMEM_ACTION_PROBE_RECEIPT) ||
        index >= REFMEM_ACTION_PROBE_FRAGMENT_COUNT) {
        refmem_action_probe_assembly_reset(a);
        return REFMEM_ACTION_PROBE_REJECT_FRAGMENT;
    }
    uint8_t part[REFMEM_ACTION_PROBE_FRAGMENT_SIZE];
    part[0] = (uint8_t)field_id; part[1] = (uint8_t)(field_id >> 8u);
    refmem_action_probe_put32(part + 2u, value);
    if (index == 0u) {
        refmem_action_probe_assembly_reset(a);
        a->active = true;
        a->kind = kind;
    } else if (a->active && a->kind == kind && a->next_index == index + 1u &&
        memcmp(a->wire + index * REFMEM_ACTION_PROBE_FRAGMENT_SIZE, part, sizeof(part)) == 0) {
        return REFMEM_ACTION_PROBE_INCOMPLETE;
    }
    if (!a->active || a->kind != kind || index != a->next_index) {
        refmem_action_probe_assembly_reset(a);
        return REFMEM_ACTION_PROBE_REJECT_FRAGMENT;
    }
    memcpy(a->wire + index * REFMEM_ACTION_PROBE_FRAGMENT_SIZE, part, sizeof(part));
    ++a->next_index;
    if (a->next_index < REFMEM_ACTION_PROBE_FRAGMENT_COUNT) return REFMEM_ACTION_PROBE_INCOMPLETE;
    a->active = false;
    return refmem_action_probe_decode(a->wire, out) ? REFMEM_ACTION_PROBE_COMPLETE :
        REFMEM_ACTION_PROBE_REJECT_CRC;
}

static inline bool refmem_action_probe_engine_configure(refmem_action_probe_engine_t *e,
    const refmem_action_probe_config_t *c)
{
    if (e == NULL || c == NULL || c->session == 0u || c->deployment_generation == 0u ||
        c->local_uid == 0u || c->local_slot >= REFMEM_ACTION_PROBE_NODE_COUNT ||
        c->business_role >= REFMEM_ACTION_PROBE_ROLE_COUNT) return false;
    if (e->active && c->session == e->session_high_water) {
        return c->deployment_generation == e->config.deployment_generation &&
            c->deployment_crc32 == e->config.deployment_crc32 &&
            c->local_uid == e->config.local_uid && c->local_slot == e->config.local_slot &&
            c->business_role == e->config.business_role;
    }
    if (c->session <= e->session_high_water) return false;
    memset(e->last, 0, sizeof(e->last));
    e->config = *c;
    e->session_high_water = c->session;
    e->active = true;
    return true;
}

static inline void refmem_action_probe_engine_cancel(refmem_action_probe_engine_t *e)
{
    if (e != NULL) e->active = false; /* Do not resurrect a revoked session. */
}

static inline refmem_action_probe_result_t refmem_action_probe_engine_execute(
    refmem_action_probe_engine_t *e, uint8_t kind, const refmem_action_probe_record_t *r)
{
    if (e == NULL || !refmem_action_probe_record_valid(r)) return REFMEM_ACTION_PROBE_REJECT_ARGUMENT;
    if (kind != REFMEM_ACTION_PROBE_REQUEST) return REFMEM_ACTION_PROBE_REJECT_KIND;
    if (!e->active) return REFMEM_ACTION_PROBE_REJECT_INACTIVE;
    const refmem_action_probe_config_t *c = &e->config;
    if (r->session != c->session || r->deployment_generation != c->deployment_generation ||
        r->deployment_crc32 != c->deployment_crc32 || r->target_uid != c->local_uid ||
        r->target_slot != c->local_slot || r->business_role != c->business_role)
        return REFMEM_ACTION_PROBE_REJECT_IDENTITY;
    refmem_action_probe_record_t *last = &e->last[r->source_slot];
    if (r->request_id < last->request_id) return REFMEM_ACTION_PROBE_REJECT_OLD;
    if (r->request_id == last->request_id) return refmem_action_probe_equal(last, r) ?
        REFMEM_ACTION_PROBE_DUPLICATE : REFMEM_ACTION_PROBE_REJECT_CONFLICT;
    /* No modulo wrap. A new request after UINT32_MAX needs a new session. */
    *last = *r;
    e->value = r->argument;
    if (e->action_count != UINT32_MAX) ++e->action_count;
    return REFMEM_ACTION_PROBE_APPLIED;
}

#endif
