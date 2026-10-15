#ifndef TRIGGER_POSITION_MAILBOX_H
#define TRIGGER_POSITION_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>
#include "trigger_position_window.h"   /* instance domain owner */

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

/* FREEZE is final and published once per position. RX latches it per position
 * beside the single-depth PREVIEW mailbox, so a PREVIEW still waiting for its
 * consumer can never drop it. One producer, one consumer; a version is written
 * only while its bit is clear and read only while it is set. */
#define TRIGGER_POSITION_FREEZE_LATCH_IDS TRIGGER_POSITION_INSTANCE_MAX
typedef struct {
    volatile uint32_t mask; /* bit position_id - 1 */
    uint32_t version[TRIGGER_POSITION_FREEZE_LATCH_IDS];
} trigger_position_freeze_latch_t;

/* Producer. A position already latched keeps its first FREEZE. */
static inline bool trigger_position_freeze_latch_put(
    trigger_position_freeze_latch_t *latch, uint32_t position_id, uint32_t version)
{
    if (!latch || position_id < 1u || position_id > TRIGGER_POSITION_FREEZE_LATCH_IDS ||
        !version) return false;
    const uint32_t bit = 1u << (position_id - 1u);
    if (__atomic_load_n(&latch->mask, __ATOMIC_ACQUIRE) & bit) return true;
    __atomic_store_n(&latch->version[position_id - 1u], version, __ATOMIC_RELAXED);
    (void)__atomic_fetch_or(&latch->mask, bit, __ATOMIC_RELEASE);
    return true;
}

/* Consumer: the lowest latched position first, as a FREEZE descriptor. */
static inline bool trigger_position_freeze_latch_take(
    trigger_position_freeze_latch_t *latch, trigger_position_descriptor_t *descriptor)
{
    if (!latch || !descriptor) return false;
    const uint32_t mask = __atomic_load_n(&latch->mask, __ATOMIC_ACQUIRE);
    if (!mask) return false;
    const uint32_t index = (uint32_t)__builtin_ctz(mask);
    const uint32_t version = __atomic_load_n(&latch->version[index], __ATOMIC_RELAXED);
    (void)__atomic_fetch_and(&latch->mask, ~(1u << index), __ATOMIC_ACQ_REL);
    *descriptor = (trigger_position_descriptor_t){
        .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
        .stage = TRIGGER_POSITION_STAGE_FREEZE,
        .position_id = index + 1u,
        .prediction_version = version,
    };
    return true;
}

#endif
