#include "distributed_refmem_internal.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) __not_in_flash_func(name)
#define DISTRIBUTED_REFMEM_RAM_TEXT __attribute__((section(".time_critical.refmem_position")))
#else
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) name
#define DISTRIBUTED_REFMEM_RAM_TEXT
#endif

bool distributed_refmem_stage_position_descriptor(
    const trigger_position_descriptor_t *descriptor, uint32_t target_mask)
{
    /* Producing never fails for backpressure any more: the newest value for a position is
     * written into the latest-value table and the transport decides when to take it. A
     * false return now means only "this descriptor cannot be represented" (bad mask, no
     * position id, slice out of range, or a cancelled run). */
    /* Same admission the mailbox boundary used to apply: a record that is not a fully
     * formed position descriptor is refused here, before it can occupy a table slot. */
    if (!descriptor || target_mask == 0u || target_mask > 0xFFu ||
        descriptor->schema != TRIGGER_POSITION_DESCRIPTOR_SCHEMA ||
        descriptor->position_id == 0u ||
        descriptor->position_id > TRIGGER_POSITION_INSTANCE_MAX ||
        descriptor->prediction_version == 0u ||
        (descriptor->stage != 0u &&
         descriptor->stage != TRIGGER_POSITION_STAGE_PREVIEW &&
         descriptor->stage != TRIGGER_POSITION_STAGE_FREEZE) ||
        __atomic_load_n(&s_position_cancel_requested, __ATOMIC_ACQUIRE)) return false;
    const trigger_position_slice_t slice = {
        .target_ms_index = (uint32_t)descriptor->target_ms_index,
        .target_fine_slice_index = descriptor->target_fine_slice_index,
        .prediction_version = (uint16_t)descriptor->prediction_version,
        .position_id = (uint8_t)descriptor->position_id,
        .stage = descriptor->stage == TRIGGER_POSITION_STAGE_FREEZE
            ? TRIGGER_POSITION_SLICE_FREEZE : TRIGGER_POSITION_SLICE_PREVIEW,
        .next_position_id = descriptor->next_position_id,
        .next_target_ms_index = descriptor->next_target_ms_index,
    };
    distributed_refmem_position_pending_t next;
    memset(&next, 0, sizeof(next));
    next.target_mask = (uint8_t)target_mask;
    next.stage = (uint8_t)slice.stage;
    next.position_id = (uint8_t)descriptor->position_id;
    if (descriptor->target_ms_index > UINT32_MAX ||
        descriptor->prediction_version > UINT16_MAX ||
        !trigger_position_slice_encode(&slice, next.wire)) return false;
    distributed_refmem_position_pending_t *entry =
        &s_position_pending[(descriptor->position_id - 1u) %
                            DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS];
    if (entry->pending) {
        /* MERGE: the value that has not gone out yet is superseded. A PREVIEW never
         * displaces a FREEZE that is still waiting for that position. */
        if (entry->stage == TRIGGER_POSITION_SLICE_FREEZE &&
            next.stage != TRIGGER_POSITION_SLICE_FREEZE) {
            s_position_coalesced_count++;
            return true;
        }
        s_position_coalesced_count++;
    }
    next.pending = 1u;
    *entry = next;
    return true;
}

void distributed_refmem_request_position_cancel(void)
{
    __atomic_store_n(&s_position_cancel_requested, 1u, __ATOMIC_RELEASE);
}

/* T_drain: how often the transport may take the next pending value. Configurable at run
 * time so the traffic can be tuned against the measured seat turnaround without a
 * re-flash; 0 disables pacing (drain as fast as the seat frees) and > MAX is refused. */
bool distributed_refmem_position_drain_ms(uint32_t drain_ms)
{
    if (drain_ms > DISTRIBUTED_REFMEM_POSITION_DRAIN_MAX_MS) return false;
    s_position_drain_ms = drain_ms;
    return true;
}

void distributed_refmem_position_drain_get(uint32_t *drain_ms)
{
    if (drain_ms) *drain_ms = s_position_drain_ms;
}

void distributed_refmem_position_cancel_service(void)
{
    if (!__atomic_load_n(&s_position_cancel_requested, __ATOMIC_ACQUIRE)) return;
    /* Producer and consumer are both Core0, so a cancel cannot race a half-written entry:
     * dropping the table and the in-flight record is the whole action. */
    memset(s_position_pending, 0, sizeof(s_position_pending));
    s_position_wait_counted = 0u;
    __atomic_store_n(&distributed_refmem_tdma_flight_sync()->position_tx_active, 0u,
                     __ATOMIC_RELEASE);
    distributed_refmem_tdma_flight_sync()->position_tx_fragment_index = 0u;
    __atomic_store_n(&s_position_cancel_requested, 0u, __ATOMIC_RELEASE);
}

DISTRIBUTED_REFMEM_RAM_TEXT
bool distributed_refmem_position_descriptor_pending(void)
{
    if (__atomic_load_n(&distributed_refmem_tdma_flight_sync()->position_tx_active,
                        __ATOMIC_ACQUIRE) != 0u) return true;
    for (uint32_t slot = 0u; slot < DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS; ++slot)
        if (s_position_pending[slot].pending) return true;
    return false;
}

/* Core1 T2 polls this every slice; keep it out of XIP. */
DISTRIBUTED_REFMEM_RAM_TEXT
bool distributed_refmem_get_position_descriptor(
    uint32_t source_slot, trigger_position_descriptor_t *descriptor)
{
    uint32_t target_mask;
    if (source_slot >= DISTRIBUTED_REFMEM_NODE_COUNT) return false;
    /* A latched FREEZE is final; it overtakes any PREVIEW still waiting. */
    if (trigger_position_freeze_latch_take(&s_position_freeze_latches[source_slot],
                                           descriptor)) {
        distributed_refmem_position_note_freeze_taken();
        return true;
    }
    if (trigger_position_mailbox_take(&s_position_rx_mailboxes[source_slot],
                                      descriptor, &target_mask)) return true;
    /* Nothing staged: retry one held first-class record, then hand it to this
     * slice. This is the bounded recovery that no longer depends on the origin
     * repeating a refused record on the wire (TDMA-SEAT-03). */
    return distributed_refmem_position_priority_drain(source_slot) &&
        trigger_position_mailbox_take(&s_position_rx_mailboxes[source_slot],
                                      descriptor, &target_mask);
}
