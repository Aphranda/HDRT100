/* Synchronous typed RX sink; deliberately separate from polled ingress. */
#include "vdc_priority_rx.h"
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/platform.h"
#define VDC_PRIORITY_RX_RAM_FUNC(name) __not_in_flash_func(name)
#else
#define VDC_PRIORITY_RX_RAM_FUNC(name) name
#endif

static vdc_priority_rx_snapshot_t s_priority_rx = {.schema = VDC_PRIORITY_RX_SCHEMA};
static vdc_priority_codec_record_t s_priority_rx_decode;
static uint32_t s_priority_rx_guard;
static uint32_t s_priority_rx_words[sizeof(vdc_priority_rx_snapshot_t) / 4u] = {
    VDC_PRIORITY_RX_SCHEMA
};
_Static_assert(sizeof(vdc_priority_rx_snapshot_t) % 4u == 0u,
    "direct RX snapshot must contain whole atomic words");

static inline uint32_t priority_rx_inc(uint32_t n)
{ return n == UINT32_MAX ? n : n + 1u; }

static void VDC_PRIORITY_RX_RAM_FUNC(priority_rx_publish)(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_rx_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_rx_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(s_priority_rx) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_priority_rx + 4u * i, 4u);
        __atomic_store_n(s_priority_rx_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_priority_rx_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_priority_rx(vdc_priority_rx_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_rx_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_rx_snapshot_t value;
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_priority_rx_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + 4u * i, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_rx_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}

void VDC_PRIORITY_RX_RAM_FUNC(vdc_priority_rx_core1)(const tdma_priority_rx_record_t *record)
{
    if (!record) {
        s_priority_rx.active = 0u;
        s_priority_rx.last_status = VDC_PRIORITY_RX_RETIRED;
        priority_rx_publish();
        return;
    }
    if (!record->epoch) return;
    if (record->epoch != s_priority_rx.epoch) {
        memset(&s_priority_rx, 0, sizeof(s_priority_rx));
        s_priority_rx.schema = VDC_PRIORITY_RX_SCHEMA;
        s_priority_rx.epoch = record->epoch;
        s_priority_rx.active = 1u;
    } else if (!s_priority_rx.active) {
        return;
    }
    s_priority_rx.carrier_count = priority_rx_inc(s_priority_rx.carrier_count);
    if (!tdma_process_image_typed_sync_class_valid(record->mailbox[3])) {
        s_priority_rx.last_status = VDC_PRIORITY_RX_OTHER_CLASS;
        priority_rx_publish();
        return;
    }
    uint32_t reject = VDC_PRIORITY_RX_EMPTY;
    if (!vdc_priority_codec_decode(
            record->mailbox + TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET,
            &s_priority_rx_decode)) {
        reject = VDC_PRIORITY_RX_CODEC_REJECT;
    } else if ((uint16_t)s_priority_rx_decode.event_sequence !=
            (uint16_t)((uint16_t)record->mailbox[6] | ((uint16_t)record->mailbox[7] << 8u))) {
        reject = VDC_PRIORITY_RX_SEQUENCE_REJECT;
    }
    const bool duplicate = !reject && s_priority_rx.have_record &&
        s_priority_rx.source_slot == record->mailbox[4] &&
        s_priority_rx.typed_record.binding_generation == s_priority_rx_decode.binding_generation &&
        s_priority_rx.typed_record.event_sequence == s_priority_rx_decode.event_sequence;
    if (duplicate &&
            (s_priority_rx.typed_record.event_time_lower != s_priority_rx_decode.event_time_lower ||
             s_priority_rx.typed_record.uncertainty_width != s_priority_rx_decode.uncertainty_width ||
             s_priority_rx.typed_record.flags != s_priority_rx_decode.flags)) {
        reject = VDC_PRIORITY_RX_CONFLICT;
        s_priority_rx.conflict_count = priority_rx_inc(s_priority_rx.conflict_count);
    }
    if (reject) {
        s_priority_rx.typed_reject_count = priority_rx_inc(s_priority_rx.typed_reject_count);
        s_priority_rx.last_status = reject;
    } else {
        s_priority_rx.typed_accept_count = priority_rx_inc(s_priority_rx.typed_accept_count);
        if (duplicate) {
            s_priority_rx.duplicate_count = priority_rx_inc(s_priority_rx.duplicate_count);
            s_priority_rx.last_status = VDC_PRIORITY_RX_DUPLICATE;
        } else {
            s_priority_rx.unique_count = priority_rx_inc(s_priority_rx.unique_count);
            s_priority_rx.last_status = VDC_PRIORITY_RX_UNIQUE;
        }
        s_priority_rx.have_record = 1u;
        s_priority_rx.source_slot = record->mailbox[4];
        s_priority_rx.target_mask = record->mailbox[5];
        s_priority_rx.carrier_sequence = record->sequence;
        s_priority_rx.irq_entry_ticks = record->irq_entry_ticks;
        s_priority_rx.typed_record = s_priority_rx_decode;
    }
    priority_rx_publish();
}

#undef VDC_PRIORITY_RX_RAM_FUNC
