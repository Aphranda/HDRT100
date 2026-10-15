#ifndef TRIGGER_POSITION_WINDOW_H
#define TRIGGER_POSITION_WINDOW_H

/* T2 position-instance window (Step 1 of T2-DEPLOY-003).
 *
 * One place owns "which positions are in flight, in what state, and which of
 * them await confirmation". It is deliberately free of the old conflations:
 *  - position identity is the wire's 32-bit absolute `target_ms` (T2-IDENT-001),
 *  - `position_id` is an in-flight INSTANCE number in [1,15] (wire domain), never
 *    a slot index,
 *  - the acknowledgement mask is "the instances currently awaiting freeze
 *    confirmation", which is exactly what the origin ANDs with its open set
 *    (trigger_t2_runtime.c resend_one_freeze), so the wire's 4-bit run id +
 *    8-bit mask stays unchanged and the window capacity matches the mask width.
 * Records are recycled once an instance is executed/abandoned, so a follower
 * consumes a whole scan instead of stopping after the first few positions.
 */

#include <stdbool.h>
#include <stdint.h>

#define TRIGGER_POSITION_WINDOW_CAPACITY 8u
#define TRIGGER_POSITION_INSTANCE_MAX 15u

_Static_assert(TRIGGER_POSITION_WINDOW_CAPACITY <= 32u,
               "ack mask must fit the wire's frozen-mask byte or a uint32");
_Static_assert(TRIGGER_POSITION_INSTANCE_MAX >= TRIGGER_POSITION_WINDOW_CAPACITY,
               "instance domain must cover a full window");

typedef enum {
    TRIGGER_POSITION_WINDOW_EMPTY = 0u,
    TRIGGER_POSITION_WINDOW_ENTERED,
    TRIGGER_POSITION_WINDOW_ANNOUNCED,
    TRIGGER_POSITION_WINDOW_FROZEN,
    TRIGGER_POSITION_WINDOW_EXECUTED,
    TRIGGER_POSITION_WINDOW_ABANDONED,
} trigger_position_window_state_t;

typedef struct {
    uint32_t instance;      /* 0 = record free, else [1,TRIGGER_POSITION_INSTANCE_MAX] */
    uint32_t target_ms;     /* absolute position identity (wire target_ms_index) */
    uint16_t version;       /* prediction version of this solve */
    uint16_t reserved;
    uint64_t action_q;      /* grid coordinate of the reserved action */
    uint8_t state;          /* trigger_position_window_state_t */
} trigger_position_window_record_t;

typedef struct {
    trigger_position_window_record_t record[TRIGGER_POSITION_WINDOW_CAPACITY];
    uint32_t entered, announced, frozen, executed, abandoned;
} trigger_position_window_t;

static inline uint32_t trigger_position_window_ack_mask(
    const trigger_position_window_t *w)
{
    uint32_t mask = 0u;
    for (uint32_t i = 0u; i < TRIGGER_POSITION_WINDOW_CAPACITY; ++i)
        if (w->record[i].state == TRIGGER_POSITION_WINDOW_FROZEN)
            mask |= 1u << i;
    return mask;
}

static inline uint32_t trigger_position_window_active_mask(
    const trigger_position_window_t *w)
{
    uint32_t mask = 0u;
    for (uint32_t i = 0u; i < TRIGGER_POSITION_WINDOW_CAPACITY; ++i)
        if (w->record[i].instance != 0u) mask |= 1u << i;
    return mask;
}

static inline int32_t trigger_position_window_find(
    const trigger_position_window_t *w, uint32_t instance)
{
    if (instance == 0u || instance > TRIGGER_POSITION_INSTANCE_MAX) return -1;
    for (uint32_t i = 0u; i < TRIGGER_POSITION_WINDOW_CAPACITY; ++i)
        if (w->record[i].instance == instance) return (int32_t)i;
    return -1;
}

/* A record may be reused when it is free or when its instance already reached a
 * terminal state: EXECUTED (its action is done) or ABANDONED (window expired). */
static inline bool trigger_position_window_reusable(
    const trigger_position_window_record_t *r)
{
    return r->instance == 0u ||
        r->state == TRIGGER_POSITION_WINDOW_EXECUTED ||
        r->state == TRIGGER_POSITION_WINDOW_ABANDONED;
}

static inline void trigger_position_window_clear(
    trigger_position_window_record_t *r)
{
    *r = (trigger_position_window_record_t){0};
}

/* Bind an instance to a record: same instance keeps its record, otherwise the
 * first reusable record is recycled. Returns false when the window is full of
 * live (ENTERED..FROZEN) instances - the bounded backpressure case the caller
 * must report, never silently drop. */
static inline bool trigger_position_window_alloc(
    trigger_position_window_t *w, uint32_t instance, uint32_t target_ms,
    uint16_t version, uint64_t action_q)
{
    if (w == NULL || instance == 0u || instance > TRIGGER_POSITION_INSTANCE_MAX)
        return false;
    int32_t existing = trigger_position_window_find(w, instance);
    if (existing >= 0) {
        trigger_position_window_record_t *r = &w->record[existing];
        if (r->target_ms == target_ms && r->version == version) return true;
        /* Same instance re-solved (newer prediction version): replace in place,
         * keeping the window position so an in-flight confirmation stays valid. */
        r->target_ms = target_ms;
        r->version = version;
        r->action_q = action_q;
        return true;
    }
    for (uint32_t i = 0u; i < TRIGGER_POSITION_WINDOW_CAPACITY; ++i) {
        if (!trigger_position_window_reusable(&w->record[i])) continue;
        trigger_position_window_record_t *r = &w->record[i];
        const bool was_empty = r->instance == 0u;
        *r = (trigger_position_window_record_t){
            .instance = instance, .target_ms = target_ms,
            .version = version, .action_q = action_q,
            .state = TRIGGER_POSITION_WINDOW_ENTERED};
        if (was_empty) w->entered++;
        return true;
    }
    return false;
}

/* Monotone progression ENTERED -> ANNOUNCED -> FROZEN -> EXECUTED; ABANDONED is
 * reachable from any live state. Repeating a state is accepted (idempotent). */
static inline bool trigger_position_window_mark(
    trigger_position_window_t *w, uint32_t instance,
    trigger_position_window_state_t state)
{
    if (w == NULL) return false;
    const int32_t index = trigger_position_window_find(w, instance);
    if (index < 0) return false;
    trigger_position_window_record_t *r = &w->record[index];
    if (state == TRIGGER_POSITION_WINDOW_ABANDONED) {
        if (r->state != TRIGGER_POSITION_WINDOW_ABANDONED) {
            r->state = TRIGGER_POSITION_WINDOW_ABANDONED;
            w->abandoned++;
        }
        return true;
    }
    if (state < r->state || state > TRIGGER_POSITION_WINDOW_EXECUTED) return false;
    if (state == r->state) return true;
    /* Strictly one step at a time while entering the pipeline: a record cannot
     * jump straight to FROZEN or EXECUTED without the stages that prove it. */
    if (state != (trigger_position_window_state_t)(r->state + 1u)) return false;
    r->state = (uint8_t)state;
    if (state == TRIGGER_POSITION_WINDOW_ANNOUNCED) w->announced++;
    else if (state == TRIGGER_POSITION_WINDOW_FROZEN) w->frozen++;
    else if (state == TRIGGER_POSITION_WINDOW_EXECUTED) w->executed++;
    return true;
}

/* Release a terminal record so its window slot can carry the next instance. */
static inline bool trigger_position_window_release(
    trigger_position_window_t *w, uint32_t instance)
{
    if (w == NULL) return false;
    const int32_t index = trigger_position_window_find(w, instance);
    if (index < 0) return false;
    trigger_position_window_record_t *r = &w->record[index];
    if (r->state != TRIGGER_POSITION_WINDOW_EXECUTED &&
        r->state != TRIGGER_POSITION_WINDOW_ABANDONED) return false;
    trigger_position_window_clear(r);
    return true;
}

static inline void trigger_position_window_reset(trigger_position_window_t *w)
{
    if (w == NULL) return;
    *w = (trigger_position_window_t){0};
}

#endif /* TRIGGER_POSITION_WINDOW_H */
