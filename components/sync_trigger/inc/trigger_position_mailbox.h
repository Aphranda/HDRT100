#ifndef TRIGGER_POSITION_MAILBOX_H
#define TRIGGER_POSITION_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_position_descriptor.h"

/* One producer and one consumer. The payload is written only in WRITING and
 * read only in READING; READY is the release/acquire handoff. */
typedef struct {
    volatile uint32_t state;
    uint32_t target_mask;
    trigger_position_descriptor_t descriptor;
} trigger_position_mailbox_t;

enum {
    TRIGGER_POSITION_MAILBOX_EMPTY = 0u,
    TRIGGER_POSITION_MAILBOX_WRITING = 1u,
    TRIGGER_POSITION_MAILBOX_READY = 2u,
    TRIGGER_POSITION_MAILBOX_READING = 3u,
};

static inline bool trigger_position_mailbox_pending(
    const trigger_position_mailbox_t *mailbox)
{
    return mailbox && __atomic_load_n(&mailbox->state, __ATOMIC_ACQUIRE) !=
        TRIGGER_POSITION_MAILBOX_EMPTY;
}

static inline bool trigger_position_mailbox_stage(
    trigger_position_mailbox_t *mailbox,
    const trigger_position_descriptor_t *descriptor, uint32_t target_mask)
{
    /* The RX side stages a record decoded from the compact single-frame wire,
     * which carries no run identity; the T2 owner completes and fully
     * validates it against its own start-up configuration on take. */
    if (!mailbox || !target_mask || !descriptor ||
        descriptor->schema != TRIGGER_POSITION_DESCRIPTOR_SCHEMA ||
        !descriptor->position_id || !descriptor->prediction_version ||
        (descriptor->stage != 0u && descriptor->stage != TRIGGER_POSITION_STAGE_PREVIEW &&
         descriptor->stage != TRIGGER_POSITION_STAGE_FREEZE)) return false;
    uint32_t expected = TRIGGER_POSITION_MAILBOX_EMPTY;
    if (!__atomic_compare_exchange_n(&mailbox->state, &expected,
            TRIGGER_POSITION_MAILBOX_WRITING, false, __ATOMIC_ACQ_REL,
            __ATOMIC_ACQUIRE)) return false;
    mailbox->descriptor = *descriptor;
    mailbox->target_mask = target_mask;
    __atomic_store_n(&mailbox->state, TRIGGER_POSITION_MAILBOX_READY,
                     __ATOMIC_RELEASE);
    return true;
}

static inline bool trigger_position_mailbox_take(
    trigger_position_mailbox_t *mailbox,
    trigger_position_descriptor_t *descriptor, uint32_t *target_mask)
{
    if (!mailbox || !descriptor || !target_mask) return false;
    uint32_t expected = TRIGGER_POSITION_MAILBOX_READY;
    if (!__atomic_compare_exchange_n(&mailbox->state, &expected,
            TRIGGER_POSITION_MAILBOX_READING, false, __ATOMIC_ACQ_REL,
            __ATOMIC_ACQUIRE)) return false;
    *descriptor = mailbox->descriptor;
    *target_mask = mailbox->target_mask;
    __atomic_store_n(&mailbox->state, TRIGGER_POSITION_MAILBOX_EMPTY,
                     __ATOMIC_RELEASE);
    return true;
}

#endif
