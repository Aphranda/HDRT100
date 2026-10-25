/* Core1 service boundary: one latest-candidate lookup, no history scan and no
 * Core0 preparation. This first ingress slice retains transport-only bytes;
 * typed event matching and DCO control must not infer timestamps from them. */
#include "vdc_priority_ingress.h"

static vdc_priority_ingress_snapshot_t s_priority_ingress;
static uint32_t s_priority_ingress_guard;
static uint32_t s_priority_ingress_words[sizeof(vdc_priority_ingress_snapshot_t) / 4u];
_Static_assert(sizeof(vdc_priority_ingress_snapshot_t) % 4u == 0u,
    "ingress snapshots publish whole atomic words");

static uint32_t priority_ingress_inc(uint32_t n) { return n == UINT32_MAX ? n : n + 1u; }

static void priority_ingress_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_ingress_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_ingress_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(s_priority_ingress) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_priority_ingress + 4u * i, 4u);
        __atomic_store_n(s_priority_ingress_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_priority_ingress_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_priority_ingress(vdc_priority_ingress_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_ingress_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_ingress_snapshot_t value;
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_priority_ingress_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + 4u * i, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_ingress_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}

static __attribute__((noinline)) void vdc_priority_ingress_core1(void)
{
    uint64_t begin_ticks = 0u, read_ticks = 0u;
    const bool begin_ok = vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &begin_ticks);
    tdma_priority_rx_snapshot_t lane;
    if (!tdma_runtime_owner_get_priority_rx_snapshot(&lane)) return;
    if (!lane.active) {
        if (s_priority_ingress.active) {
            s_priority_ingress.active = 0u;
            priority_ingress_publish();
        }
        return;
    }
    if (!lane.epoch) return;
    if (s_priority_ingress.epoch != lane.epoch) {
        memset(&s_priority_ingress, 0, sizeof(s_priority_ingress));
        s_priority_ingress.schema = 1u;
        s_priority_ingress.epoch = lane.epoch;
    }
    s_priority_ingress.active = 1u;
    s_priority_ingress.service_count = priority_ingress_inc(s_priority_ingress.service_count);
    bool copied = false;
    if (!lane.retained_mask) {
        s_priority_ingress.empty_polls = priority_ingress_inc(s_priority_ingress.empty_polls);
    } else if (s_priority_ingress.have_record &&
        lane.latest_sequence == s_priority_ingress.record.sequence) {
        s_priority_ingress.duplicate_polls = priority_ingress_inc(s_priority_ingress.duplicate_polls);
    } else if (s_priority_ingress.have_record &&
        lane.latest_sequence - s_priority_ingress.record.sequence > INT32_MAX) {
        s_priority_ingress.order_rejects = priority_ingress_inc(s_priority_ingress.order_rejects);
    } else {
        tdma_priority_rx_record_t record;
        if (!tdma_runtime_owner_copy_priority_rx_live(lane.epoch, lane.latest_sequence, &record)) {
            s_priority_ingress.copy_retries = priority_ingress_inc(s_priority_ingress.copy_retries);
        } else {
            if (s_priority_ingress.have_record) {
                /* Includes both producer gaps and records skipped by latest-only
                 * consumption. It is not a count of FIFO drops or lost events. */
                const uint32_t gap = record.sequence - s_priority_ingress.record.sequence - 1u;
                const uint32_t old = s_priority_ingress.skipped_carrier_count;
                s_priority_ingress.skipped_carrier_count = UINT32_MAX - old < gap ? UINT32_MAX : old + gap;
            }
            s_priority_ingress.record = record;
            s_priority_ingress.have_record = 1u;
            s_priority_ingress.last_class = record.mailbox[3];
            if (tdma_process_image_typed_sync_class_valid(record.mailbox[3])) {
                vdc_priority_codec_record_t typed;
                if (vdc_priority_codec_decode(
                        record.mailbox + TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET,
                        &typed)) {
                    s_priority_ingress.typed_record = typed;
                    s_priority_ingress.typed_decode_count =
                        priority_ingress_inc(s_priority_ingress.typed_decode_count);
                } else {
                    s_priority_ingress.typed_reject_count =
                        priority_ingress_inc(s_priority_ingress.typed_reject_count);
                }
            }
            s_priority_ingress.copied_count = priority_ingress_inc(s_priority_ingress.copied_count);
            copied = true;
        }
    }
    const bool read_ok = vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &read_ticks);
    if (!begin_ok || !read_ok || read_ticks < begin_ticks || read_ticks - begin_ticks > UINT32_MAX ||
        (copied && read_ticks < s_priority_ingress.record.irq_entry_ticks)) {
        s_priority_ingress.clock_failures = priority_ingress_inc(s_priority_ingress.clock_failures);
        s_priority_ingress.body_last_cycles = UINT32_MAX;
        if (copied) s_priority_ingress.arrival_last_cycles = UINT64_MAX;
    } else {
        const uint32_t body = (uint32_t)(read_ticks - begin_ticks);
        s_priority_ingress.body_last_cycles = body;
        if (body > s_priority_ingress.body_max_cycles) s_priority_ingress.body_max_cycles = body;
        s_priority_ingress.timing_samples = priority_ingress_inc(s_priority_ingress.timing_samples);
        if (copied) {
            const uint64_t arrival = read_ticks - s_priority_ingress.record.irq_entry_ticks;
            s_priority_ingress.arrival_last_cycles = arrival;
            if (arrival > s_priority_ingress.arrival_max_cycles) s_priority_ingress.arrival_max_cycles = arrival;
        }
    }
    /* Body timing ends before this publication/return. The full DPLL phase
     * remains covered by the existing scheduler's wall-time accounting. */
    priority_ingress_publish();
}
