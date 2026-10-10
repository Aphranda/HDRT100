#ifndef TDMA_FIRST_CLASS_H
#define TDMA_FIRST_CLASS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tdma_flight_engine.h"

/* TDMA-SEAT-01 first-class seat: one complete local mailbox, prepared by a
 * Core0 owner and boarded by the Core1 origin on the shared fast path.
 * Single depth, single writer: a waiting record is never overwritten and the
 * next one may be staged only after the origin boarded or retired it.
 * TDMA treats the mailbox as opaque; the producer owns its business class. */
enum {
    TDMA_FIRST_CLASS_EMPTY = 0u,
    TDMA_FIRST_CLASS_WRITING,
    TDMA_FIRST_CLASS_READY,
    TDMA_FIRST_CLASS_READING,
};

typedef struct {
    volatile uint32_t state;
    /* Set by the Core1 origin while its fast path is the only route for local
     * mailboxes (priority provider enabled); cleared at STOP. Core0 routes a
     * first-class record here only while this is set. */
    volatile uint32_t route;
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
} tdma_first_class_t;

static inline bool tdma_first_class_route(const tdma_first_class_t *f)
{
    return f && __atomic_load_n(&f->route, __ATOMIC_ACQUIRE) != 0u;
}

static inline bool tdma_first_class_pending(const tdma_first_class_t *f)
{
    return f && __atomic_load_n(&f->state, __ATOMIC_ACQUIRE) == TDMA_FIRST_CLASS_READY;
}

/* Core0: publish one complete mailbox. False if a record is still waiting. */
static inline bool tdma_first_class_stage(tdma_first_class_t *f,
    const uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE])
{
    uint32_t expected = TDMA_FIRST_CLASS_EMPTY;
    if (!f || !mailbox || !__atomic_compare_exchange_n(&f->state, &expected,
            TDMA_FIRST_CLASS_WRITING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return false;
    memcpy(f->mailbox, mailbox, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    __atomic_store_n(&f->state, TDMA_FIRST_CLASS_READY, __ATOMIC_RELEASE);
    return true;
}

/* Core1: lease the waiting record for one boarding attempt. */
static inline bool tdma_first_class_take(tdma_first_class_t *f,
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE])
{
    uint32_t expected = TDMA_FIRST_CLASS_READY;
    if (!f || !mailbox || !__atomic_compare_exchange_n(&f->state, &expected,
            TDMA_FIRST_CLASS_READING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return false;
    memcpy(mailbox, f->mailbox, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    return true;
}

/* Core1: boarded (or rejected) frees the slot; a deferred publish keeps it. */
static inline void tdma_first_class_finish(tdma_first_class_t *f, bool consumed)
{
    if (f) __atomic_store_n(&f->state, consumed ? TDMA_FIRST_CLASS_EMPTY :
        TDMA_FIRST_CLASS_READY, __ATOMIC_RELEASE);
}

/* Core1 owner at route start and STOP: retire a waiting record so it can never
 * board in a later session. A record still being written stays untouched. */
static inline void tdma_first_class_retire(tdma_first_class_t *f, bool route)
{
    if (!f) return;
    __atomic_store_n(&f->route, route ? 1u : 0u, __ATOMIC_RELEASE);
    uint32_t expected = TDMA_FIRST_CLASS_READY;
    (void)__atomic_compare_exchange_n(&f->state, &expected, TDMA_FIRST_CLASS_EMPTY,
        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

#endif
