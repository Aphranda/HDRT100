#include "tdma_priority_rx.h"
#include "tdma_process_image_layout.h"
#include "tdma_profile.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert((TDMA_PRIORITY_RX_CAPACITY & (TDMA_PRIORITY_RX_CAPACITY - 1u)) == 0u,
    "priority records use direct power-of-two sequence indexing");
_Static_assert(sizeof(tdma_priority_rx_record_t) % 4u == 0u &&
    sizeof(tdma_priority_rx_snapshot_t) % 4u == 0u, "atomic publication uses whole words");

static uint32_t u16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8u); }
static uint32_t u32(const uint8_t *p) { return u16(p) | (u16(p + 2) << 16u); }
static void increment(uint32_t *p) { if (*p != UINT32_MAX) ++*p; }
static void store_words(uint32_t *dst, const void *src, uint32_t bytes)
{
    for (uint32_t i = 0u; i < bytes / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)src + i * 4u, 4u);
        __atomic_store_n(dst + i, word, __ATOMIC_RELAXED);
    }
}
static void load_words(void *dst, const uint32_t *src, uint32_t bytes)
{
    for (uint32_t i = 0u; i < bytes / 4u; ++i) {
        const uint32_t word = __atomic_load_n(src + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)dst + i * 4u, &word, 4u);
    }
}
static bool same_transport(const uint32_t *stored, const tdma_priority_rx_record_t *record)
{
    /* Called only by the serialized producer. Its latest slot cannot change
     * during this comparison, so no complete record copy or reader nesting
     * is needed. IRQ arrival metadata deliberately does not define a duplicate. */
    const uint32_t offset = offsetof(tdma_priority_rx_record_t, header);
    for (uint32_t i = 0u; i < (TDMA_PRIORITY_RX_HEADER_BYTES + TDMA_PRIORITY_RX_MAILBOX_BYTES) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)record + offset + i * 4u, sizeof(word));
        if (word != __atomic_load_n(stored + offset / 4u + i, __ATOMIC_RELAXED)) return false;
    }
    return true;
}
static bool begin(tdma_priority_rx_t *lane)
{
    const uint32_t guard = __atomic_load_n(&lane->guard, __ATOMIC_RELAXED);
    if (guard & 1u) return false;
    /* Unsigned wrap preserves odd=in-progress/even=stable. Readers make one
     * bounded attempt and must span fewer than 2^31 publications; a reader
     * suspended for a complete guard circle is outside this API contract. */
    __atomic_store_n(&lane->guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return true;
}
static void end(tdma_priority_rx_t *lane)
{
    store_words(lane->published, &lane->status, sizeof(lane->status));
    /* The IRQ/disabled-source owner is the only serialized writer. An RMW
     * would add an unnecessary unbounded LDREX/STLEX retry to the IRQ tail. */
    const uint32_t guard = __atomic_load_n(&lane->guard, __ATOMIC_RELAXED);
    __atomic_store_n(&lane->guard, guard + 1u, __ATOMIC_RELEASE);
}

bool tdma_priority_rx_start(tdma_priority_rx_t *lane)
{
    if (!lane || lane->status.epoch == UINT32_MAX || !begin(lane)) return false;
    const uint32_t epoch = lane->status.epoch + 1u;
    memset(&lane->status, 0, sizeof(lane->status));
    lane->status.schema = 1u;
    lane->status.active = 1u;
    lane->status.epoch = epoch;
    end(lane);
    return true;
}
bool tdma_priority_rx_rebase(tdma_priority_rx_t *lane)
{
    if (!lane || lane->status.epoch == UINT32_MAX || !begin(lane)) return false;
    ++lane->status.epoch;
    lane->status.retained_mask = 0u;
    lane->status.latest_sequence = lane->status.latest_mailbox_seq16 = 0u;
    memset(lane->status.sequence, 0, sizeof(lane->status.sequence));
    end(lane);
    return true;
}
void tdma_priority_rx_stop(tdma_priority_rx_t *lane)
{
    if (!lane || !begin(lane)) return;
    lane->status.active = 0u;
    end(lane);
}
void tdma_priority_rx_reject(tdma_priority_rx_t *lane, uint32_t reason)
{
    increment(&lane->status.reject_count);
    lane->status.last_reject = reason;
}
void tdma_priority_rx_finish_irq(tdma_priority_rx_t *lane, uint64_t ticks, uint32_t cycles)
{
    if (!lane || !begin(lane)) return;
    increment(&lane->status.irq_count);
    lane->status.last_entry_ticks = ticks;
    lane->status.irq_last_cycles = cycles;
    if (cycles > lane->status.irq_max_cycles) lane->status.irq_max_cycles = cycles;
    if (UINT64_MAX - lane->status.irq_total_cycles >= cycles)
        lane->status.irq_total_cycles += cycles;
    else lane->status.irq_total_cycles = UINT64_MAX;
    end(lane);
}

bool tdma_priority_rx_candidate(uint64_t produced, uint32_t stride, uint32_t frame_words,
    uint32_t byte_shift, uint32_t bit_shift, uint32_t ring_words, uint64_t *candidate)
{
    if (!candidate || !stride || byte_shift >= stride || bit_shift > 7u ||
        frame_words < TDMA_PRIORITY_RX_HEADER_BYTES + 4u || stride < frame_words ||
        frame_words + (bit_shift != 0u) >= ring_words ||
        produced < (uint64_t)frame_words + (bit_shift != 0u) + byte_shift) return false;
    const uint64_t limit = produced - frame_words - (bit_shift != 0u);
    const uint64_t start = limit - (limit - byte_shift) % stride;
    if (produced - start >= ring_words) return false;
    *candidate = start;
    return true;
}

uint32_t tdma_priority_rx_validate(const tdma_priority_rx_binding_t *b,
    const tdma_priority_rx_record_t *r)
{
    if (!b || !r || b->node_count < 2u || b->node_count > TDMA_FLIGHT_SHORT_SLOT_COUNT ||
        b->reference_slot >= b->node_count || b->local_slot >= b->node_count)
        return TDMA_PRIORITY_RX_HEADER;
    const uint8_t *h = r->header, *m = r->mailbox;
    if (u16(h) != TDMA_TRANSPORT_FRAME_MAGIC || h[2] != TDMA_TRANSPORT_FRAME_VERSION ||
        h[3] != TDMA_TRANSPORT_FRAME_CLASS_SHORT || u16(h + 4u) != b->packet_bytes ||
        h[6] != TDMA_TRANSPORT_FRAME_HEADER_SIZE || h[7] != b->reference_slot ||
        h[12] != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE ||
        !(h[13] & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) ||
        h[14] > h[15] || !h[15] || h[15] > b->node_count ||
        u32(h + 16u) != b->schedule_crc32 || u32(h + 20u) != b->profile_crc32 ||
        u32(h + 8u) != r->sequence || !u32(h + 24u)) return TDMA_PRIORITY_RX_HEADER;
    uint32_t crc;
    uint8_t identity[23];
    memcpy(identity, h, 14u);
    memcpy(identity + 14u, h + 15u, 9u); /* Immutable header except hop-count. */
    if (tdma_transport_crc32_compute(identity, sizeof(identity)) != u32(h + 24u))
        return TDMA_PRIORITY_RX_HEADER_CRC;
    /* Mutable transport CRC protects exactly this fixed header. Do not pass
     * an invented full-payload pointer/length to the general frame decoder. */
    if (!tdma_transport_frame_calculate_transport_crc32(h, TDMA_PRIORITY_RX_HEADER_BYTES, &crc) ||
        crc != u32(h + 28u)) return TDMA_PRIORITY_RX_HEADER_CRC;
    if (u16(m) != TDMA_FLIGHT_MAILBOX_MAGIC || m[2] != TDMA_FLIGHT_MAILBOX_VERSION ||
        !tdma_process_image_transport_class_valid(m[3]) || m[4] != b->reference_slot ||
        !(m[5] & (1u << b->local_slot)) || (m[5] & ~((1u << b->node_count) - 1u)))
        return TDMA_PRIORITY_RX_MAILBOX;
    if (tdma_process_image_crc16_ccitt(m, TDMA_PROCESS_IMAGE_CRC_OFFSET) !=
        u16(m + TDMA_PROCESS_IMAGE_CRC_OFFSET)) return TDMA_PRIORITY_RX_MAILBOX_CRC;
    return TDMA_PRIORITY_RX_OK;
}

bool tdma_priority_rx_publish(tdma_priority_rx_t *lane, const tdma_priority_rx_record_t *r)
{
    if (!lane || !r || !lane->status.active || r->epoch != lane->status.epoch) return false;
    const uint32_t index = r->sequence & (TDMA_PRIORITY_RX_CAPACITY - 1u);
    const bool retained = lane->status.retained_mask != 0u;
    const uint32_t advance = r->sequence - lane->status.latest_sequence;
    /* Serial-number order accepts forward distances below half the carrier
     * space, including crossing zero. Half a circle is ambiguous, while an
     * older sequence must never overwrite a newer retained record. */
    if (retained && (advance == 0u || advance > INT32_MAX)) {
        if (advance == 0u &&
            same_transport(lane->record[index], r))
            increment(&lane->status.duplicate_count);
        else tdma_priority_rx_reject(lane, TDMA_PRIORITY_RX_SEQUENCE);
        return false;
    }
    if (!begin(lane)) return false;
    if (retained && r->sequence < lane->status.latest_sequence) {
        if (lane->status.epoch == UINT32_MAX) {
            tdma_priority_rx_reject(lane, TDMA_PRIORITY_RX_EXHAUSTED);
            lane->status.active = 0u;
            end(lane);
            return false; /* A reboot is required rather than reusing IDs. */
        }
        ++lane->status.epoch;
        lane->status.retained_mask = 0u;
        memset(lane->status.sequence, 0, sizeof(lane->status.sequence));
    }
    if (lane->status.retained_mask & (1u << index)) increment(&lane->status.overwrite_count);
    if (retained && advance > 1u) {
        const uint32_t gap = advance - 1u;
        lane->status.sequence_gap_count = UINT32_MAX - lane->status.sequence_gap_count < gap
            ? UINT32_MAX : lane->status.sequence_gap_count + gap;
    }
    store_words(lane->record[index], r, sizeof(*r));
    /* Capture supplied the namespace current at IRQ entry. A carrier wrap
     * retires that namespace in this same publication without a second copy. */
    __atomic_store_n(lane->record[index] + offsetof(tdma_priority_rx_record_t, epoch) / 4u,
        lane->status.epoch, __ATOMIC_RELAXED);
    lane->status.retained_mask |= 1u << index;
    lane->status.sequence[index] = r->sequence;
    lane->status.latest_sequence = r->sequence;
    lane->status.latest_mailbox_seq16 = u16(r->mailbox + TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET);
    lane->status.last_reject = TDMA_PRIORITY_RX_OK;
    increment(&lane->status.publish_count);
    end(lane);
    return true;
}
bool tdma_priority_rx_snapshot(const tdma_priority_rx_t *lane, tdma_priority_rx_snapshot_t *out)
{
    if (!lane || !out) return false;
    const uint32_t before = __atomic_load_n(&lane->guard, __ATOMIC_ACQUIRE);
    if (before & 1u) return false;
    tdma_priority_rx_snapshot_t value;
    load_words(&value, lane->published, sizeof(value));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (before != __atomic_load_n(&lane->guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}
bool tdma_priority_rx_copy(const tdma_priority_rx_t *lane, uint32_t epoch,
    uint32_t sequence, tdma_priority_rx_record_t *out)
{
    if (!lane || !out || !epoch) return false;
    const uint32_t before = __atomic_load_n(&lane->guard, __ATOMIC_ACQUIRE);
    if (before & 1u) return false;
    const uint32_t active_epoch = __atomic_load_n(lane->published +
        offsetof(tdma_priority_rx_snapshot_t, epoch) / 4u, __ATOMIC_RELAXED);
    const uint32_t mask = __atomic_load_n(lane->published +
        offsetof(tdma_priority_rx_snapshot_t, retained_mask) / 4u, __ATOMIC_RELAXED);
    if (active_epoch != epoch || !(mask & (1u << (sequence & (TDMA_PRIORITY_RX_CAPACITY - 1u)))))
        return false;
    tdma_priority_rx_record_t value;
    load_words(&value, lane->record[sequence & (TDMA_PRIORITY_RX_CAPACITY - 1u)], sizeof(value));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (before != __atomic_load_n(&lane->guard, __ATOMIC_ACQUIRE) ||
        value.epoch != epoch || value.sequence != sequence) return false;
    *out = value;
    return true;
}
