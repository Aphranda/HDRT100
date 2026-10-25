#include "vdc_dpll_manager.h"
#include "vdc_dpll_manager_internal.h"
#include "vdc_priority_shared.h"
#include "vdc_time_mapping.h"
#include "vdc_model_projection.h"
#include "vdc_priority_tx.h"
#include "vdc_priority_rx.h"
#include "vdc_priority_match.h"
#include "vdc_priority_follow.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "board_identity.h"
#include "app.h"
#include "distributed_refmem.h"
#include "ota_crc32.h"
#include "osal.h"
#include "ota_ao.h"
#include "storage_manager.h"
#include "product_config.h"
#include "refmem_sync_frame.h"
#include "sync_io.h"
#include "tdma_runtime_owner.h"
#include "tdma_service.h"
#include "tdma_service_timing.h"
#include "vdc_domain.h"
#include "vdc_ring_observer.h"
#include "vdc_sync_io_adapter.h"
#include "vdc_tdma_payload.h"
#include "vdc_timestamp_clock.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#include "pico/time.h"
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) name
#endif

/* The parent used to define these hooks around the textual include. Now that
 * the follow/match/phase fragments are their own unit, the unit must bind them
 * itself - otherwise the fragment's `#ifndef` fallback makes them no-ops and
 * the trace phase/decision endpoints are never published. */
#define VDC_PRIORITY_TRACE_PHASE_HOOK(snapshot) priority_trace_phase_core1(snapshot)
#define VDC_PRIORITY_TRACE_DECISION_HOOK(snapshot) priority_trace_decision_core1(snapshot)

#define VDC_DPLL_MANAGER_SELF_TEST_CLEANUP_MARGIN_MS 250u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_MAGIC 0x4C504444u /* DDPL */
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_SCHEMA 6u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_MASTER 1u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_COMMAND 2u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_STATE 3u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_EVIDENCE 4u
#define VDC_DPLL_MANAGER_WAVEFORM_MAGIC 0x57524D53u /* SMRW */
#define VDC_DPLL_MANAGER_WAVEFORM_SCHEMA 4u
#define VDC_DPLL_MANAGER_WAVEFORM_BUFFER_COUNT 3u
#define VDC_DPLL_MANAGER_PHASE_ARM_AHEAD_PERIODS 2u


#include "vdc_dpll_manager_internal.h"

#include "vdc_priority_match.h"
#include "vdc_priority_rx.h"
#include <limits.h>
#include <string.h>

uint32_t s_priority_match_request, s_priority_match_last_request;
uint32_t s_priority_match_request_session;
static uint32_t s_priority_match_guard;
static uint32_t s_priority_match_words[sizeof(vdc_priority_match_snapshot_t) / 4u];
/* The follower lock word has its own small publication so a realtime reader
 * never copies the matcher snapshot (VDC-LOCKPUB-002). */
static uint32_t s_priority_lock_guard;
static uint32_t s_priority_lock_words[sizeof(vdc_priority_lock_word_t) / 4u];
vdc_priority_match_work_t s_priority_match_work;
_Static_assert(sizeof(vdc_priority_match_snapshot_t) % 4u == 0u,
    "match snapshot publishes atomic words");
_Static_assert(sizeof(vdc_priority_lock_word_t) % 4u == 0u,
    "lock word publishes atomic words");
_Static_assert(VDC_PRIORITY_LOCK_ENTER_NS == VDC_DOMAIN_LOCK_TIER_FINE_NS,
    "follower lock enters at the FINE tier");

uint32_t vdc_dpll_manager_priority_match_generation(void)
{ return __atomic_load_n(&s_priority_match_request, __ATOMIC_ACQUIRE); }
static bool priority_match_set_stopped(void *context)
{
    const uint32_t generation = *(const uint32_t *)context;
    const uint32_t session = vdc_dpll_manager_feedback_session();
    if (generation && (generation <= s_priority_match_last_request ||
            !session)) return false;
    if (generation) {
        s_priority_match_last_request = generation;
        priority_note_host_control();
    }
    __atomic_store_n(&s_priority_match_request_session, generation ? session : 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_priority_match_request, generation, __ATOMIC_RELEASE);
    return true;
}
bool vdc_dpll_manager_set_priority_match(uint32_t generation)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_match_set_stopped, &generation);
}
void priority_lock_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_lock_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_lock_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(s_priority_match_work.lock) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_priority_match_work.lock + 4u * i, 4u);
        __atomic_store_n(s_priority_lock_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_priority_lock_guard, guard + 2u, __ATOMIC_RELEASE);
}
bool vdc_dpll_manager_get_priority_lock(vdc_priority_lock_word_t *out)
{
    if (!out) return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&s_priority_lock_guard, __ATOMIC_ACQUIRE);
        if (guard & 1u) continue;
        vdc_priority_lock_word_t value;
        for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
            const uint32_t word = __atomic_load_n(s_priority_lock_words + i, __ATOMIC_RELAXED);
            memcpy((uint8_t *)&value + 4u * i, &word, 4u);
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (guard != __atomic_load_n(&s_priority_lock_guard, __ATOMIC_ACQUIRE)) continue;
        *out = value;
        return true;
    }
    return false;
}
void priority_match_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_match_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_match_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(s_priority_match_work.status) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_priority_match_work.status + 4u * i, 4u);
        __atomic_store_n(s_priority_match_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_priority_match_guard, guard + 2u, __ATOMIC_RELEASE);
    priority_lock_publish();
}
bool vdc_dpll_manager_get_priority_match(vdc_priority_match_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_match_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_match_snapshot_t value;
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_priority_match_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + 4u * i, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_match_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}
static void priority_match_skip(uint32_t reason, bool retire)
{
    vdc_priority_match_snapshot_t *s = &s_priority_match_work.status;
    s->last_reason = reason;
    if (reason == VDC_PRIORITY_MATCH_BUSY) s->busy = priority_match_inc(s->busy);
    else if (reason == VDC_PRIORITY_MATCH_PENDING) s->pending = priority_match_inc(s->pending);
    else if (reason == VDC_PRIORITY_MATCH_EVICTED) s->history_miss = priority_match_inc(s->history_miss);
    else {
        s->rejected = priority_match_inc(s->rejected);
        /* The lumped rejection counter cannot name its cause: latch the first
         * one that is not arming/teardown and happened while no observation
         * had ever bound. Sticky until a fresh request resets it. */
        if (!s->first_reject_reason && !s_priority_match_work.bound &&
            reason != VDC_PRIORITY_MATCH_DISABLED && reason != VDC_PRIORITY_MATCH_STOP)
            s->first_reject_reason = reason;
    }
    if (retire) {
        s->active = 0u; s->retired = 1u;
        /* A retired observation can no longer prove tracking. */
        s_priority_match_work.lock = (vdc_priority_lock_word_t){0};
    }
    priority_match_publish();
}
static bool priority_match_difference(uint64_t a, uint64_t b, int64_t *out)
{
    if (a >= b) {
        if (a - b > INT64_MAX) return false;
        *out = (int64_t)(a - b);
    } else {
        const uint64_t magnitude = b - a;
        if (magnitude > (UINT64_C(1) << 63u)) return false;
        *out = magnitude == (UINT64_C(1) << 63u) ? INT64_MIN : -(int64_t)magnitude;
    }
    return true;
}
static bool priority_match_live_valid(const tdma_pio_spi_event_live_snapshot_t *live)
{
    const uint32_t required = TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID;
    return (live->flags & required) == required && live->arm_epoch && live->record.epoch &&
        live->tick_hz && live->timer1_enable_after >= live->timer1_enable_before;
}
static bool priority_match_ring_equal(const tdma_ring_clock_snapshot_t *a,
    const tdma_ring_clock_snapshot_t *b)
{
    return a->enabled == b->enabled && a->adapter_started == b->adapter_started &&
        a->config_seq == b->config_seq && a->applied_config_seq == b->applied_config_seq &&
        a->node_count == b->node_count && a->local_slot_id == b->local_slot_id &&
        a->reference_slot_id == b->reference_slot_id && a->schedule_crc32 == b->schedule_crc32;
}
static bool priority_match_model_valid(const vdc_dpll_manager_committed_model_t *m)
{
    const vdc_priority_match_work_t *w = &s_priority_match_work;
    return w->session && m->session == w->session && m->token && m->role_generation &&
        m->role_generation == s_vdc_domain.control.profile.generation && m->clock_epoch_id &&
        m->clock_run_id && m->local_slot == w->ring.local_slot_id && m->dco.valid == 1u &&
        m->dco.nominal_period_ns && m->dco.lock_state <= VDC_DOMAIN_LOCK_FAULT &&
        m->dco.tdma_schedule_crc32 == w->ring.schedule_crc32;
}
static bool priority_match_path(vdc_path_delay_entry_t *path)
{
    vdc_priority_match_work_t *w = &s_priority_match_work;
    const tdma_ring_clock_snapshot_t *r = &w->ring;
    const vdc_path_delay_table_t *table = &s_vdc_domain.path_delay;
    /* The existing matrix describes reverse DATA. Its transpose gives the
     * forward-CS path using the same measured per-link scalars, provisionally.
     * Never walk topology or claim independently measured CS endpoints. */
    if (!table->table_crc32 || table->schedule_crc32 != r->schedule_crc32) return false;
    if (w->path_checked_crc != table->table_crc32) {
        if (table->entry_count != r->node_count || table->entry_count > VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT)
            return false;
        /* Once per immutable table: bounded direction checks, no ring walk or
         * delay accumulation. A native forward matrix must not be transposed. */
        for (uint32_t i = 0u; i < table->entry_count; ++i)
            if (!table->entries[i].valid ||
                table->entries[i].direction != VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE) return false;
        w->path_checked_crc = table->table_crc32;
    }
    return vdc_domain_active_observation_path_delay_lookup(table,
            r->reference_slot_id, r->local_slot_id, path);
}
static bool priority_match_binding_current(void)
{
    const vdc_priority_match_work_t *w = &s_priority_match_work;
    const vdc_priority_match_snapshot_t *s = &w->status;
    return s->ring_config_seq == w->ring.config_seq && s->ring_applied_seq == w->ring.applied_config_seq &&
        s->local_slot == w->ring.local_slot_id && s->reference_slot == w->ring.reference_slot_id &&
        s->node_count == w->ring.node_count && s->schedule_crc32 == w->ring.schedule_crc32 &&
        s->profile_crc32 == w->profile_crc32 && s->session == w->session &&
        s->role_generation == w->model.role_generation && s->clock_epoch == w->model.clock_epoch_id &&
        s->clock_run == w->model.clock_run_id && s->rx_epoch == w->rx.epoch &&
        s->arm_epoch == w->live.arm_epoch && s->observer_epoch == w->live.record.epoch &&
        s->tick_hz == w->live.tick_hz && s->path_crc32 == w->path.cal_crc32 && s->delay_ns == w->path.delay_ns;
}

void __attribute__((noinline)) vdc_priority_match_core1(void)
{
    if (get_core_num() != 1u) return;
    vdc_priority_match_work_t *w = &s_priority_match_work;
    vdc_priority_match_snapshot_t *s = &w->status;
    w->fresh = 0u;
    if (w->lock.locked || w->lock.in_band_run || w->lock.out_band_run) {
        /* Age the lock out here, before the owner's declaration of this beat,
         * so a lock whose input stopped is never declared. The last match time
         * stays, so a reader can still say the observation went STALE. */
        const uint64_t now_ns = vdc_dpll_manager_now_ns();
        if (now_ns == UINT64_MAX || now_ns < w->lock.last_match_ns ||
            now_ns - w->lock.last_match_ns > VDC_PRIORITY_LOCK_STALE_NS) {
            w->lock.locked = w->lock.in_band_run = w->lock.out_band_run = 0u;
            priority_lock_publish();
        }
    }
    const uint32_t request = vdc_dpll_manager_priority_match_generation();
    if (!request) {
        if (s->generation && !s->retired) priority_match_skip(VDC_PRIORITY_MATCH_DISABLED, true);
        return;
    }
    if (request != s->generation) {
        memset(w, 0, sizeof(*w)); s->schema = 1u; s->generation = request;
        /* A fresh capture starts with no latched rejection; keep the reset
         * explicit next to the other per-request status fields. */
        s->first_reject_reason = 0u;
        s->session = __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE);
        priority_lock_publish();
    }
    if (s->retired) return;
    s->calls = priority_match_inc(s->calls);
    w->session = vdc_dpll_manager_feedback_session();
    if (!s->session || s->session != w->session)
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (!s_vdc_tdma_service || !tdma_runtime_owner_get_ring_clock_snapshot(&w->ring))
        return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
    if (!w->ring.enabled || !w->ring.adapter_started)
        return priority_match_skip(VDC_PRIORITY_MATCH_STOP, w->bound != 0u);
    w->profile_crc32 = s_vdc_tdma_service->ring_runtime.ring_profile_crc32;
    if (!w->ring.config_seq || w->ring.config_seq != w->ring.applied_config_seq ||
        w->ring.node_count < 2u || w->ring.node_count > TDMA_FLIGHT_SHORT_SLOT_COUNT ||
        w->ring.local_slot_id >= w->ring.node_count || w->ring.reference_slot_id >= w->ring.node_count ||
        w->ring.local_slot_id == w->ring.reference_slot_id || !w->ring.schedule_crc32 || !w->profile_crc32)
        return priority_match_skip(VDC_PRIORITY_MATCH_CONFIG, w->bound != 0u);
    if (s_vdc_domain.control.profile.valid != 1u ||
        s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER ||
        s_vdc_domain.control.profile.follow_master_slot_id != w->ring.reference_slot_id)
        return priority_match_skip(VDC_PRIORITY_MATCH_ROLE, w->bound != 0u);
    if (w->bound && (s->ring_config_seq != w->ring.config_seq ||
        s->ring_applied_seq != w->ring.applied_config_seq || s->local_slot != w->ring.local_slot_id ||
        s->reference_slot != w->ring.reference_slot_id || s->node_count != w->ring.node_count ||
        s->schedule_crc32 != w->ring.schedule_crc32 || s->profile_crc32 != w->profile_crc32 ||
        s->session != w->session || s->role_generation != s_vdc_domain.control.profile.generation))
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (!vdc_dpll_manager_get_committed_model(&w->model))
        return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
    if (w->bound && (s->session != w->session || s->session != w->model.session ||
        s->role_generation != w->model.role_generation || s->clock_epoch != w->model.clock_epoch_id ||
        s->clock_run != w->model.clock_run_id || s->local_slot != w->model.local_slot))
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (!priority_match_model_valid(&w->model))
        return priority_match_skip(VDC_PRIORITY_MATCH_MODEL, false);
    if (!tdma_runtime_owner_get_event_live_snapshot(&w->live))
        return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
    /* Publish what this predicate is about to judge (VDC-ISO-001): a follower
     * whose typed records arrive but never match must be diagnosable from a live
     * page, and the only way to say which of RETAINED / ACTIVE / ANCHOR_VALID or
     * which anchor term is missing is to record the evidence here, before the
     * rejection. The lock word is the matcher's own per-beat publication. */
    {
        const bool bracket_ok =
            w->live.timer1_enable_after >= w->live.timer1_enable_before &&
            w->live.timer1_enable_after != 0u;
        const bool live_valid = priority_match_live_valid(&w->live);
        if (w->lock.live_flags != w->live.flags ||
            w->lock.live_valid != (uint32_t)(live_valid ? 1u : 0u) ||
            w->lock.live_arm_epoch_lo != (uint32_t)w->live.arm_epoch ||
            w->lock.live_observer_epoch != w->live.record.epoch ||
            w->lock.live_tick_hz != w->live.tick_hz ||
            w->lock.live_bracket_ok != (uint32_t)(bracket_ok ? 1u : 0u)) {
            w->lock.live_flags = w->live.flags;
            w->lock.live_valid = live_valid ? 1u : 0u;
            w->lock.live_arm_epoch_lo = (uint32_t)w->live.arm_epoch;
            w->lock.live_observer_epoch = w->live.record.epoch;
            w->lock.live_tick_hz = w->live.tick_hz;
            w->lock.live_bracket_ok = bracket_ok ? 1u : 0u;
            priority_lock_publish();
        }
        w->lock.live_samples = priority_match_inc(w->lock.live_samples);
    }
    if (!priority_match_live_valid(&w->live)) {
        return priority_match_skip(VDC_PRIORITY_MATCH_SOURCE, w->bound != 0u);
    }
    if (w->bound && (s->arm_epoch != w->live.arm_epoch ||
        s->observer_epoch != w->live.record.epoch || s->tick_hz != w->live.tick_hz))
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (!vdc_priority_rx_copy_live(&w->rx))
        return priority_match_skip(VDC_PRIORITY_MATCH_RX, false);
    if (!priority_match_path(&w->path))
        return priority_match_skip(VDC_PRIORITY_MATCH_PATH, w->bound != 0u);
    if (w->bound && !priority_match_binding_current())
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (w->rx.typed_record.binding_generation != request)
        return priority_match_skip(VDC_PRIORITY_MATCH_GENERATION, false);
    if (!w->rx.epoch || w->rx.source_slot != w->ring.reference_slot_id ||
        !(w->rx.target_mask & (1u << w->ring.local_slot_id)))
        return priority_match_skip(VDC_PRIORITY_MATCH_RX, false);
    const uint32_t sequence = w->rx.typed_record.event_sequence;
    if (s->have_match && sequence == s->event_sequence) {
        s->repeated = priority_match_inc(s->repeated); s->last_reason = VDC_PRIORITY_MATCH_OK;
        priority_match_publish(); return;
    }
    if (s->have_match && (sequence < s->event_sequence || sequence - s->event_sequence > INT32_MAX))
        return priority_match_skip(VDC_PRIORITY_MATCH_SEQUENCE, false);
    if (w->unresolved && sequence != w->candidate_sequence)
        s->superseded = priority_match_inc(s->superseded);
    w->candidate_sequence = sequence; w->unresolved = 1u;
    /* Publish the exact inputs this call hands to the facade (VDC-ISO-001/T3):
     * they are otherwise written only on a successful match, which made a
     * RETIRED follower undiagnosable from a live page. */
    s->arm_epoch = w->live.arm_epoch;
    s->observer_epoch = w->live.record.epoch;
    s->tick_hz = w->live.tick_hz;
    s->history_result = tdma_runtime_owner_copy_event_history_exact(w->live.arm_epoch,
        w->live.record.epoch, sequence, &w->exact);
    bool anchor_correlated = false;
    if (s->history_result != TDMA_EVENT_EXACT_OK) {
        if (s->history_result == TDMA_EVENT_EXACT_BUSY)
            return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
        if (s->history_result == TDMA_EVENT_EXACT_PENDING)
            return priority_match_skip(VDC_PRIORITY_MATCH_PENDING, false);
        if (s->history_result == TDMA_EVENT_EXACT_RETIRED)
            return priority_match_skip(VDC_PRIORITY_MATCH_SOURCE,
                w->bound != 0u);
        if (s->history_result != TDMA_EVENT_EXACT_EVICTED)
            return priority_match_skip(VDC_PRIORITY_MATCH_SOURCE, false);
        /* VDC-ISO-001: the typed record's own number can sit outside the
         * retained window once TDMA-SEAT-01 shares the wire - the observer's
         * per-transmission witness then leaves the event numbering that this
         * record carries. The observation itself is still valid and LIVE names
         * it, so correlate with the LIVE retained record and keep every proof
         * below (epoch/arm/tick, live flags, timer1 bracket). A mispaired frame
         * fails the residual/lock verdict instead of producing a lock, and the
         * count below keeps the regime auditable. */
        w->exact = (tdma_pio_spi_event_exact_t){0};
        w->exact.record.rx_elapsed_cycles = w->live.record.rx_elapsed_cycles;
        w->exact.record.tx_elapsed_cycles = w->live.record.tx_elapsed_cycles;
        w->exact.record.raw_rx = w->live.record.raw_rx;
        w->exact.record.raw_tx = w->live.record.raw_tx;
        w->exact.record.sequence = w->live.record.sequence;
        w->exact.record.ordinal = w->live.record.ordinal;
        w->exact.arm_epoch = w->live.arm_epoch;
        w->exact.observer_epoch = w->live.record.epoch;
        w->exact.tick_hz = w->live.tick_hz;
        w->exact.timer1_enable_before = w->live.timer1_enable_before;
        w->exact.timer1_enable_after = w->live.timer1_enable_after;
        w->exact.flags = w->live.flags;
        s->anchor_fallback = priority_match_inc(s->anchor_fallback);
        anchor_correlated = true;
    }
    const uint32_t required = TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID | TDMA_EVENT_LIVE_RETAINED;
    if (w->exact.arm_epoch != w->live.arm_epoch || w->exact.observer_epoch != w->live.record.epoch ||
        w->exact.tick_hz != w->live.tick_hz ||
        (!anchor_correlated && w->exact.record.sequence != sequence) ||
        (w->exact.flags & required) != required ||
        w->exact.timer1_enable_before != w->live.timer1_enable_before ||
        w->exact.timer1_enable_after != w->live.timer1_enable_after ||
        w->exact.timer1_enable_after < w->exact.timer1_enable_before)
        return priority_match_skip(VDC_PRIORITY_MATCH_SOURCE, false);
    if (w->exact.record.rx_elapsed_cycles > UINT64_MAX - w->exact.timer1_enable_after)
        return priority_match_skip(VDC_PRIORITY_MATCH_OVERFLOW, false);
    w->raw_lo = w->exact.timer1_enable_before + w->exact.record.rx_elapsed_cycles;
    w->raw_hi = w->exact.timer1_enable_after + w->exact.record.rx_elapsed_cycles;
    if (w->raw_lo < w->model.valid_from_raw)
        return priority_match_skip(VDC_PRIORITY_MATCH_MODEL, false);
    if (!vdc_dpll_manager_project_timer1_feedback_event(w->session, w->model.role_generation,
            w->model.clock_epoch_id, w->model.clock_run_id, w->ring.local_slot_id,
            w->ring.schedule_crc32, w->live.tick_hz, w->raw_lo, w->raw_hi,
            &w->projection))
        return priority_match_skip(VDC_PRIORITY_MATCH_PROJECTION, false);
    if (!vdc_dpll_manager_get_committed_model(&w->model_after) ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&w->ring_after) ||
        !tdma_runtime_owner_get_event_live_snapshot(&w->live_after) ||
        !vdc_priority_rx_copy_live(&w->rx_after))
        return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
    if (request != vdc_dpll_manager_priority_match_generation() ||
        w->session != vdc_dpll_manager_feedback_session() ||
        w->session != __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) ||
        s_vdc_domain.control.profile.valid != 1u ||
        s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER ||
        s_vdc_domain.control.profile.follow_master_slot_id != w->ring.reference_slot_id ||
        !priority_match_ring_equal(&w->ring, &w->ring_after) ||
        w->profile_crc32 != s_vdc_tdma_service->ring_runtime.ring_profile_crc32 ||
        w->model_after.session != w->model.session || w->model_after.role_generation != w->model.role_generation ||
        w->model_after.clock_epoch_id != w->model.clock_epoch_id || w->model_after.clock_run_id != w->model.clock_run_id ||
        w->model_after.local_slot != w->model.local_slot ||
        !priority_match_live_valid(&w->live_after) || w->live_after.arm_epoch != w->live.arm_epoch ||
        w->live_after.record.epoch != w->live.record.epoch || w->live_after.tick_hz != w->live.tick_hz ||
        w->live_after.timer1_enable_before != w->live.timer1_enable_before ||
        w->live_after.timer1_enable_after != w->live.timer1_enable_after ||
        w->rx_after.epoch != w->rx.epoch || !priority_match_path(&w->path_after) ||
        w->rx_after.typed_record.binding_generation != request ||
        w->rx_after.source_slot != w->ring.reference_slot_id ||
        !(w->rx_after.target_mask & (1u << w->ring.local_slot_id)) ||
        w->path_after.cal_crc32 != w->path.cal_crc32 || w->path_after.delay_ns != w->path.delay_ns)
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (w->projection.model.token != w->model.token ||
        w->projection.model.applied_command_seq != w->model.applied_command_seq ||
        memcmp(&w->model, &w->model_after, sizeof(w->model)) != 0 ||
        !priority_match_model_valid(&w->model_after))
        return priority_match_skip(VDC_PRIORITY_MATCH_CHANGED, false);
    const uint64_t remote = w->rx.typed_record.event_time_lower;
    const uint32_t width = w->rx.typed_record.uncertainty_width;
    if (!width || remote > UINT64_MAX - width || remote + width > UINT64_MAX - w->path.delay_ns ||
        w->projection.output_hi < w->projection.output_lo)
        return priority_match_skip(VDC_PRIORITY_MATCH_OVERFLOW, false);
    w->remote_hi = remote + width; w->expected_lo = remote + w->path.delay_ns;
    w->expected_hi = w->remote_hi + w->path.delay_ns;
    if (!priority_match_difference(w->projection.output_lo, w->expected_hi, &w->residual_lo) ||
        !priority_match_difference(w->projection.output_hi, w->expected_lo, &w->residual_hi))
        return priority_match_skip(VDC_PRIORITY_MATCH_OVERFLOW, false);
    s->source_slot = w->rx.source_slot; s->event_sequence = sequence;
    s->carrier_sequence = w->rx.carrier_sequence; s->rx_epoch = w->rx.epoch;
    s->session = w->session; s->role_generation = w->model.role_generation;
    s->clock_epoch = w->model.clock_epoch_id; s->clock_run = w->model.clock_run_id;
    s->model_token = w->model.token; s->applied_command_seq = w->model.applied_command_seq;
    s->ring_config_seq = w->ring.config_seq; s->ring_applied_seq = w->ring.applied_config_seq;
    s->local_slot = w->ring.local_slot_id; s->reference_slot = w->ring.reference_slot_id;
    s->node_count = w->ring.node_count; s->schedule_crc32 = w->ring.schedule_crc32;
    s->profile_crc32 = w->profile_crc32; s->observer_epoch = w->live.record.epoch;
    s->tick_hz = w->live.tick_hz; s->path_crc32 = w->path.cal_crc32; s->delay_ns = w->path.delay_ns;
    s->path_direction = VDC_PATH_DELAY_DIRECTION_MARKER_FORWARD;
    s->path_qualification = VDC_PRIORITY_MATCH_PATH_PROVISIONAL_TRANSPOSE;
    s->arm_epoch = w->live.arm_epoch; s->valid_from_raw = w->model.valid_from_raw;
    s->raw_lo = w->raw_lo; s->raw_hi = w->raw_hi;
    s->local_lo = w->projection.output_lo; s->local_hi = w->projection.output_hi;
    s->remote_lo = remote; s->remote_hi = w->remote_hi;
    s->expected_lo = w->expected_lo; s->expected_hi = w->expected_hi;
    s->residual_lo = w->residual_lo; s->residual_hi = w->residual_hi;
    s->matched = priority_match_inc(s->matched); s->have_match = s->active = w->bound = 1u;
    s->last_reason = VDC_PRIORITY_MATCH_OK; w->unresolved = 0u;
    w->fresh = 1u;
    vdc_priority_lock_observe(&w->lock, w->residual_lo, w->residual_hi, sequence,
                              vdc_dpll_manager_now_ns());
    priority_match_publish();
}

/* Sole Core1 owner. Preparation uses one fresh private matcher ticket before
 * the model writer guard; application revalidates actual owner state inside
 * that guard. Atomic-word publication is STOP diagnostics, never authority. */
#ifndef VDC_PRIORITY_TRACE_DECISION_HOOK
#define VDC_PRIORITY_TRACE_DECISION_HOOK(snapshot) ((void)(snapshot))
#endif
vdc_priority_follow_work_t s_priority_follow_work;
static void priority_phase_reset_request(void);
static void priority_phase_cancel(uint32_t reason, bool retire);
static bool priority_phase_prepare_ticket(bool new_binding);
static void priority_phase_rate_priority(void);
static void priority_phase_apply_core1(void);
static bool priority_phase_known_successor(
    const vdc_dpll_manager_committed_model_t *before,
    const vdc_dpll_manager_committed_model_t *current);
static uint32_t s_priority_follow_guard;
static uint32_t s_priority_follow_words[sizeof(vdc_priority_follow_snapshot_t) / 4u];
static vdc_priority_follow_health_t s_priority_health;
static uint32_t s_priority_health_guard;
static uint32_t s_priority_health_words[sizeof(vdc_priority_follow_health_t) / 4u];
static bool s_priority_health_dirty;
_Static_assert(sizeof(vdc_priority_follow_snapshot_t) == 216u,
    "typed-follow diagnostics use 54 atomic words");
_Static_assert(VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS <= UINT64_MAX / UINT64_C(1000000000),
    "scaled positive secant numerator must fit uint64");
_Static_assert(VDC_PRIORITY_FOLLOW_MIN_INTERVAL_NS < VDC_PRIORITY_FOLLOW_SECOND_INTERVAL_NS &&
    VDC_PRIORITY_FOLLOW_SECOND_INTERVAL_NS < VDC_PRIORITY_FOLLOW_THIRD_INTERVAL_NS &&
    VDC_PRIORITY_FOLLOW_THIRD_INTERVAL_NS < VDC_PRIORITY_FOLLOW_FOURTH_INTERVAL_NS &&
    VDC_PRIORITY_FOLLOW_FOURTH_INTERVAL_NS < VDC_PRIORITY_FOLLOW_FINAL_INTERVAL_NS &&
    VDC_PRIORITY_FOLLOW_FINAL_INTERVAL_NS < VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS &&
    VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS == 5u,
    "five ordered evaluation thresholds stay below the interval limit");
static const uint64_t s_priority_follow_intervals[VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS] = {
    VDC_PRIORITY_FOLLOW_MIN_INTERVAL_NS, VDC_PRIORITY_FOLLOW_SECOND_INTERVAL_NS,
    VDC_PRIORITY_FOLLOW_THIRD_INTERVAL_NS, VDC_PRIORITY_FOLLOW_FOURTH_INTERVAL_NS,
    VDC_PRIORITY_FOLLOW_FINAL_INTERVAL_NS
};

static bool priority_follow_set_stopped(void *context)
{
    const vdc_boundary_request_t *request = context;
    const uint32_t session = vdc_dpll_manager_feedback_session();
    if (request->mode == VDC_BOUNDARY_MODE_TYPED_FOLLOW &&
        (!session || !request->priority_generation ||
         request->priority_generation != vdc_dpll_manager_priority_match_generation() ||
         __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != session)) return false;
    if (request->mode == VDC_BOUNDARY_MODE_TYPED_FOLLOW) priority_note_host_control();
    return boundary_set_stopped(context);
}

bool vdc_dpll_manager_set_priority_follow(bool enabled)
{
    vdc_boundary_request_t request = {
        .mode = enabled ? VDC_BOUNDARY_MODE_TYPED_FOLLOW : VDC_BOUNDARY_MODE_PROBE,
        .priority_generation = enabled ? vdc_dpll_manager_priority_match_generation() : 0u};
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_follow_set_stopped, &request);
}

static void priority_follow_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_follow_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_follow_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    boundary_store(s_priority_follow_words, &s_priority_follow_work.status,
        sizeof(s_priority_follow_work.status));
    __atomic_store_n(&s_priority_follow_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_priority_follow(vdc_priority_follow_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_follow_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_follow_snapshot_t copy;
    match_load_words(s_priority_follow_words, &copy, sizeof(copy));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_follow_guard, __ATOMIC_ACQUIRE)) return false;
    *out = copy;
    return true;
}

bool vdc_dpll_manager_get_priority_follow_health_revision(
    vdc_priority_follow_health_t *out, uint32_t *revision)
{
    if (!out || !revision) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_health_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_follow_health_t value;
    match_load_words(s_priority_health_words, &value, sizeof(value));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_health_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    *revision = guard;
    return true;
}

bool vdc_dpll_manager_get_priority_follow_health(vdc_priority_follow_health_t *out)
{
    uint32_t revision;
    return vdc_dpll_manager_get_priority_follow_health_revision(out, &revision);
}

static void priority_follow_health_request(void)
{
    const vdc_priority_follow_snapshot_t *s = &s_priority_follow_work.status;
    if (s_priority_health.schema && s_priority_health.request == s->request) return;
    s_priority_health = (vdc_priority_follow_health_t){.schema = 1u,
        .request = s->request, .session = s->session, .generation = s->generation,
        .state = VDC_PRIORITY_HEALTH_WAITING,
        .freshness_limit_us = VDC_PRIORITY_FOLLOW_MAX_AGE_MS * 1000u};
    s_priority_health_dirty = true;
}

static void priority_follow_health_accept(void)
{
    const vdc_priority_match_work_t *match = &s_priority_match_work;
    const vdc_priority_match_snapshot_t *m = &s_priority_follow_work.ticket.match;
    if (!match->fresh || match->status.event_sequence != m->event_sequence ||
        match->status.generation != m->generation || match->status.session != m->session) return;
    priority_follow_health_request();
    if (s_priority_health.tick_hz && m->event_sequence <= s_priority_health.event_sequence) return;
    s_priority_health.event_sequence = m->event_sequence;
    s_priority_health.tick_hz = m->tick_hz;
    s_priority_health.raw_lo = m->raw_lo;
    s_priority_health.raw_hi = m->raw_hi;
    s_priority_health.accepted = priority_match_inc(s_priority_health.accepted);
    s_priority_health_dirty = true;
}

/* Mandatory wrapper calls this even without a fresh carrier or a DCO change. */
void priority_follow_health_service_core1(void)
{
    if (get_core_num() != 1u) return;
    priority_follow_health_request();
    const vdc_priority_follow_snapshot_t *follow = &s_priority_follow_work.status;
    vdc_priority_follow_health_t next = s_priority_health;
    if (!follow->active) {
        next.state = follow->state == VDC_PRIORITY_FOLLOW_DISABLED
            ? VDC_PRIORITY_HEALTH_DISABLED : VDC_PRIORITY_HEALTH_RETIRED;
        next.reason = follow->last_reason;
    } else if (!next.tick_hz) {
        next.state = VDC_PRIORITY_HEALTH_WAITING;
        next.reason = VDC_PRIORITY_FOLLOW_OK;
    } else {
        uint64_t now;
        next.state = VDC_PRIORITY_HEALTH_STALE;
        next.age_us = UINT32_MAX;
        if (!vdc_timestamp_clock_try_read_ticks64(next.tick_hz, &now)) {
            next.reason = VDC_PRIORITY_FOLLOW_BUSY;
        } else if (now < next.raw_hi || next.raw_hi < next.raw_lo) {
            next.reason = VDC_PRIORITY_FOLLOW_AGE;
        } else {
            const uint64_t elapsed = now - next.raw_lo;
            const uint64_t seconds = elapsed / next.tick_hz;
            const uint64_t age = seconds > UINT32_MAX / 1000000u ? UINT32_MAX :
                seconds * 1000000u + (elapsed % next.tick_hz) * 1000000u / next.tick_hz;
            next.age_us = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
            const bool fresh = elapsed <= (uint64_t)next.tick_hz * VDC_PRIORITY_FOLLOW_MAX_AGE_MS / 1000u;
            next.state = fresh ? VDC_PRIORITY_HEALTH_FRESH : VDC_PRIORITY_HEALTH_STALE;
            next.reason = fresh ? VDC_PRIORITY_FOLLOW_OK : VDC_PRIORITY_FOLLOW_AGE;
        }
        if (next.state == VDC_PRIORITY_HEALTH_STALE && s_priority_health.state == VDC_PRIORITY_HEALTH_FRESH)
            next.stale_transitions = priority_match_inc(next.stale_transitions);
        if (next.state == VDC_PRIORITY_HEALTH_FRESH && s_priority_health.state == VDC_PRIORITY_HEALTH_STALE)
            next.recoveries = priority_match_inc(next.recoveries);
    }
    if (!s_priority_health_dirty && !memcmp(&next, &s_priority_health, sizeof(next))) return;
    s_priority_health = next;
    s_priority_health_dirty = false;
    const uint32_t guard = __atomic_load_n(&s_priority_health_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_health_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    boundary_store(s_priority_health_words, &next, sizeof(next));
    __atomic_store_n(&s_priority_health_guard, guard + 2u, __ATOMIC_RELEASE);
}

/* Inputs are positive interval DELTAS, all in actual output ns. Perform
 * outward rounding on positive ratios before subtracting exactly 1e9. */
static uint32_t priority_follow_rate_interval(uint64_t local_lo, uint64_t local_hi,
    uint64_t expected_lo, uint64_t expected_hi, int64_t *lo, int64_t *hi)
{
    if (!lo || !hi || !local_lo || !expected_lo || local_lo > local_hi || expected_lo > expected_hi)
        return VDC_PRIORITY_FOLLOW_INTERVAL;
    if (local_hi > VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS ||
        expected_hi > VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS)
        return VDC_PRIORITY_FOLLOW_OVERFLOW;
    const uint64_t numerator_lo = local_lo * UINT64_C(1000000000);
    const uint64_t numerator_hi = local_hi * UINT64_C(1000000000);
    const uint64_t lower = numerator_lo / expected_hi;
    const uint64_t upper = numerator_hi / expected_lo + (numerator_hi % expected_lo != 0u);
    /* Ten-second positive numerators may exceed INT64_MAX. Subtract the
     * exact 1e9 bias in the unsigned domain BEFORE converting a positive
     * result, so every representable error is retained without a signed
     * overflow or implementation-defined conversion of a tiny denominator. */
    const uint64_t max_quotient = (uint64_t)INT64_MAX + UINT64_C(1000000000);
    if (lower > max_quotient || upper > max_quotient)
        return VDC_PRIORITY_FOLLOW_OVERFLOW;
    *lo = lower >= UINT64_C(1000000000)
        ? (int64_t)(lower - UINT64_C(1000000000))
        : -(int64_t)(UINT64_C(1000000000) - lower);
    *hi = upper >= UINT64_C(1000000000)
        ? (int64_t)(upper - UINT64_C(1000000000))
        : -(int64_t)(UINT64_C(1000000000) - upper);
    return VDC_PRIORITY_FOLLOW_OK;
}

static int32_t priority_follow_delta(int64_t lo, int64_t hi,
    int32_t current_ppb, uint32_t limit_ppb)
{
    if (lo > hi) return 0;
    /* Use half the nearest-to-zero proved error boundary. This increases
     * response while retaining interval uncertainty, deadband and both
     * existing step/total clamps; it is not an interval-midpoint estimate. */
    int64_t step = lo > VDC_BOUNDARY_AUTO_DEADBAND_PPB ? -(lo / 2) :
        hi < -VDC_BOUNDARY_AUTO_DEADBAND_PPB ? -(hi / 2) : 0;
    if (step > VDC_BOUNDARY_AUTO_MAX_DELTA_PPB) step = VDC_BOUNDARY_AUTO_MAX_DELTA_PPB;
    if (step < -VDC_BOUNDARY_AUTO_MAX_DELTA_PPB) step = -VDC_BOUNDARY_AUTO_MAX_DELTA_PPB;
    const int64_t upper = limit_ppb > INT32_MAX ? INT32_MAX : (int64_t)limit_ppb;
    const int64_t lower = upper > 999999999 ? -999999999 : -upper;
    if (current_ppb < lower || current_ppb > upper) return 0;
    if (step > upper - current_ppb) step = upper - current_ppb;
    if (step < lower - current_ppb) step = lower - current_ppb;
    return (int32_t)step;
}

static void priority_follow_cancel(uint32_t reason, bool retire)
{
    priority_phase_cancel(reason, retire);
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    if (w->pending || w->have_baseline) w->status.cancelled = priority_match_inc(w->status.cancelled);
    w->pending = w->have_baseline = w->next_evaluation = w->baseline_replacements = 0u;
    w->status.last_reason = reason;
    w->status.state = retire ? VDC_PRIORITY_FOLLOW_RETIRED : VDC_PRIORITY_FOLLOW_AWAITING;
    if (retire) w->status.active = 0u;
}

static bool priority_follow_same_binding(const vdc_priority_match_snapshot_t *a,
    const vdc_priority_match_snapshot_t *b)
{
    return a->generation == b->generation && a->session == b->session &&
        a->role_generation == b->role_generation && a->clock_epoch == b->clock_epoch &&
        a->clock_run == b->clock_run && a->arm_epoch == b->arm_epoch &&
        a->observer_epoch == b->observer_epoch && a->tick_hz == b->tick_hz &&
        a->rx_epoch == b->rx_epoch && a->source_slot == b->source_slot &&
        a->ring_config_seq == b->ring_config_seq && a->ring_applied_seq == b->ring_applied_seq &&
        a->local_slot == b->local_slot && a->reference_slot == b->reference_slot &&
        a->node_count == b->node_count && a->schedule_crc32 == b->schedule_crc32 &&
        a->profile_crc32 == b->profile_crc32 && a->path_crc32 == b->path_crc32 &&
        a->delay_ns == b->delay_ns;
}

static void priority_follow_baseline(void)
{
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    const vdc_priority_match_snapshot_t *m = &w->ticket.match;
    w->previous = (vdc_priority_follow_sample_t){
        .local_lo = m->local_lo, .local_hi = m->local_hi,
        .normalized_lo = w->ticket.normalized_lo, .normalized_hi = w->ticket.normalized_hi,
        .rate_epoch = w->ticket.rate_epoch,
        .expected_lo = m->expected_lo, .expected_hi = m->expected_hi,
        .raw_lo = m->raw_lo, .sequence = m->event_sequence, .model_token = m->model_token};
    w->have_baseline = 1u;
    w->next_evaluation = 0u;
    w->baseline_replacements = 0u;
    w->status.baselines = priority_match_inc(w->status.baselines);
    w->status.state = VDC_PRIORITY_FOLLOW_BASELINE;
}

__attribute__((noinline)) void priority_follow_prepare_core1(void)
{
    if (get_core_num() != 1u) return;
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    vdc_priority_follow_snapshot_t *s = &w->status;
    const vdc_priority_match_work_t *match = &s_priority_match_work;
    uint32_t request, session, mode;
    int32_t delta;
    if (!boundary_request(&request, &session, &delta, &mode)) return;
    const uint32_t match_generation = __atomic_load_n(
        &s_boundary_request_priority_generation, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (request != __atomic_load_n(&s_boundary_request, __ATOMIC_ACQUIRE)) return;
    if (!request || mode != VDC_BOUNDARY_MODE_TYPED_FOLLOW) {
        if (s->active || w->pending || w->have_baseline) {
            priority_follow_cancel(VDC_PRIORITY_FOLLOW_OFF, true);
            s->state = VDC_PRIORITY_FOLLOW_DISABLED;
            priority_follow_publish();
        }
        return;
    }
    if (request != s->request) {
        memset(w, 0, sizeof(*w));
        priority_phase_reset_request();
        s->schema = 1u; s->request = request; s->session = session;
        s->generation = match_generation;
        s->mode = mode; s->active = 1u; s->state = VDC_PRIORITY_FOLLOW_AWAITING;
    }
    if (!s->active) return;
    s->calls = priority_match_inc(s->calls);
    if (!session || session != s->session || session != vdc_dpll_manager_feedback_session() ||
        !s->generation || s->generation != vdc_dpll_manager_priority_match_generation() ||
        __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != session ||
        (match->status.generation == s->generation && match->status.retired)) {
        priority_follow_cancel(match->status.retired &&
            match->status.last_reason == VDC_PRIORITY_MATCH_STOP ?
                VDC_PRIORITY_FOLLOW_STOP : VDC_PRIORITY_FOLLOW_BINDING, true);
        priority_follow_publish(); return;
    }
    /* A missing/rejected/repeated carrier contributes no sample. The good
     * baseline survives; apply checks its current lifetime even without data. */
    if (!match->fresh) { priority_follow_publish(); return; }
    const vdc_priority_match_snapshot_t *m = &match->status;
    if (m->generation != s->generation || m->session != session || !m->active || m->retired ||
        !m->have_match || m->last_reason != VDC_PRIORITY_MATCH_OK) return;
    if (w->bound && (!priority_follow_same_binding(&w->ticket.match, m) ||
        w->ticket.path_table_crc != s_vdc_domain.path_delay.table_crc32 ||
        w->ticket.anchor_before != match->live.timer1_enable_before ||
        w->ticket.anchor_after != match->live.timer1_enable_after)) {
        priority_follow_cancel(VDC_PRIORITY_FOLLOW_BINDING, true);
        priority_follow_publish(); return;
    }
    if ((w->have_consumed && m->event_sequence <= w->consumed_sequence) ||
        (w->have_baseline && m->event_sequence <= w->previous.sequence)) {
        s->repeated = priority_match_inc(s->repeated);
        s->last_reason = VDC_PRIORITY_FOLLOW_DUPLICATE;
        priority_follow_publish(); return;
    }
    if (!w->bound && !vdc_dpll_manager_get_priority_follow_baseline(&w->baseline_config)) {
        priority_follow_cancel(VDC_PRIORITY_FOLLOW_BINDING, true);
        priority_follow_publish(); return;
    }
    if (w->pending) s->cancelled = priority_match_inc(s->cancelled);
    w->pending = 0u;
    const bool baseline_model_validity_changed = w->have_baseline &&
        w->previous.model_token == m->model_token &&
        w->ticket.model.token == m->model_token &&
        w->ticket.model.valid_from_raw != match->model.valid_from_raw;
    w->ticket.match = *m;
    w->ticket.model = match->model;
    w->ticket.anchor_before = match->live.timer1_enable_before;
    w->ticket.anchor_after = match->live.timer1_enable_after;
    w->ticket.path_table_crc = s_vdc_domain.path_delay.table_crc32;
    w->ticket.servo_crc = s_vdc_domain.servo.servo_profile_crc32;
    w->ticket.prepared_ms = board_uptime_ms();
    if (!priority_phase_prepare_ticket(!w->bound)) {
        priority_follow_cancel(VDC_PRIORITY_FOLLOW_OVERFLOW, true);
        priority_follow_publish(); return;
    }
    w->bound = 1u;
    s->tickets = priority_match_inc(s->tickets);
    /* Each endpoint keeps its own admission/model identity. A known
     * successful phase publication may advance the rate epoch, but a
     * baseline admitted before the new model's validity start cannot be
     * paired with a later event under that model. */
    if (!w->have_baseline || w->previous.rate_epoch != w->ticket.rate_epoch ||
        baseline_model_validity_changed ||
        m->raw_lo < w->ticket.model.valid_from_raw) {
        priority_follow_baseline(); s->last_reason = VDC_PRIORITY_FOLLOW_MODEL;
        priority_follow_publish(); return;
    }
    const vdc_priority_follow_sample_t *p = &w->previous;
    if (w->ticket.normalized_lo <= p->normalized_hi || m->expected_lo <= p->expected_hi ||
        m->local_hi < m->local_lo || m->expected_hi < m->expected_lo ||
        m->raw_lo <= p->raw_lo) {
        priority_follow_baseline(); s->last_reason = VDC_PRIORITY_FOLLOW_INTERVAL;
        priority_follow_publish(); return;
    }
    w->local_delta_lo = w->ticket.normalized_lo - p->normalized_hi;
    w->local_delta_hi = w->ticket.normalized_hi - p->normalized_lo;
    w->expected_delta_lo = m->expected_lo - p->expected_hi;
    w->expected_delta_hi = m->expected_hi - p->expected_lo;
    if (w->local_delta_hi > VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS ||
        w->expected_delta_hi > VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS) {
        priority_follow_baseline(); s->last_reason = VDC_PRIORITY_FOLLOW_INTERVAL;
        priority_follow_publish(); return;
    }
    if (w->next_evaluation >= VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS) {
        priority_follow_baseline(); s->last_reason = VDC_PRIORITY_FOLLOW_INTERVAL;
        priority_follow_publish(); return;
    }
    /* A fresh narrower source interval may replace an early seed only under
     * the same admitted binding/model above. Bound the entire elapsed range,
     * not its midpoint; two quarter-window replacements add at most half the
     * first interval in reference coordinates, not wall time when input is
     * missing. Keep each event's own endpoints. */
    if (!w->next_evaluation && w->baseline_replacements < w->baseline_config.max_replacements &&
        w->expected_delta_hi <= w->baseline_config.window_ns &&
        p->expected_hi > p->expected_lo) {
        const uint64_t old_width = p->expected_hi - p->expected_lo;
        const uint64_t new_width = m->expected_hi - m->expected_lo;
        if (new_width < old_width && new_width <= old_width / 2u) {
            const uint32_t replacements = w->baseline_replacements + 1u;
            priority_follow_baseline();
            w->baseline_replacements = replacements;
            s->last_reason = VDC_PRIORITY_FOLLOW_BASELINE_QUALITY;
            priority_follow_publish(); return;
        }
    }
    const uint64_t next_interval = s_priority_follow_intervals[w->next_evaluation];
    if (w->expected_delta_lo < next_interval) {
        s->waits = priority_match_inc(s->waits); s->state = VDC_PRIORITY_FOLLOW_WAITING;
        s->last_reason = VDC_PRIORITY_FOLLOW_INTERVAL;
        priority_follow_publish(); return;
    }
    /* Consume this opportunity BEFORE either projection or ratio arithmetic.
     * Jump to a strictly future threshold: missed windows never catch up, and
     * BUSY/application deferral or fresh-ticket replacement cannot refund a
     * threshold and add another computation for the same baseline. */
    do {
        ++w->next_evaluation;
    } while (w->next_evaluation < VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS &&
        w->expected_delta_lo >= s_priority_follow_intervals[w->next_evaluation]);
    /* Only a complete observation window pays for the scaled divisions.
     * The binding/anchor checks above hold transitively from the baseline.
     * Both MATCH endpoints retain their admitted absolute output intervals;
     * These typed events target F(floor(X(event))), as does the reference
     * encoder, not a hypothetical microsecond TIMER0 read at the event.
     * raw_lo subtraction cancels their shared observer enable anchor and
     * clock offset under the admitted unchanged clock lifetime. Preserve
     * integer-ns rounding before DCO scaling; the legacy staircase helper
     * remains separate. After phase steps, subtract only the confirmed own
     * cumulative translations: both intervals then describe the SAME event
     * difference in one rate-epoch geometry. Original MATCH endpoints stay
     * intact; an unknown model transition never inherits this authorization.
     * An inconsistency rejects the pair; neither estimate is a fallback. */
    uint64_t correlated_lo, correlated_hi;
    if (!vdc_model_project_event_delta(&w->ticket.model.dco, m->tick_hz,
            m->raw_lo - p->raw_lo, &correlated_lo, &correlated_hi) || !correlated_lo ||
        correlated_lo > w->local_delta_hi || correlated_hi < w->local_delta_lo) {
        priority_follow_baseline(); s->last_reason = VDC_PRIORITY_FOLLOW_INTERVAL;
        s->rejected = priority_match_inc(s->rejected);
        priority_follow_publish(); return;
    }
    if (correlated_lo > w->local_delta_lo) w->local_delta_lo = correlated_lo;
    if (correlated_hi < w->local_delta_hi) w->local_delta_hi = correlated_hi;
    const uint32_t result = priority_follow_rate_interval(w->local_delta_lo, w->local_delta_hi,
        w->expected_delta_lo, w->expected_delta_hi, &w->error_lo, &w->error_hi);
    if (result != VDC_PRIORITY_FOLLOW_OK) {
        priority_follow_baseline(); s->last_reason = result;
        s->rejected = priority_match_inc(s->rejected);
        priority_follow_publish(); return;
    }
    w->pending = 1u; s->state = VDC_PRIORITY_FOLLOW_READY; s->last_reason = VDC_PRIORITY_FOLLOW_OK;
    s->prepared = priority_match_inc(s->prepared);
    priority_follow_publish();
}

/* Readbacks use the existing matcher scratch, not another runtime history or
 * large Core1 stack frame. This function never uses the public model getter:
 * it is called only inside its writer guard after normal Domain work. */
static uint32_t priority_follow_current(void)
{
    const vdc_priority_follow_work_t *w = &s_priority_follow_work;
    const vdc_priority_follow_ticket_t *t = &w->ticket;
    const vdc_priority_match_snapshot_t *m = &t->match;
    vdc_priority_match_work_t *scratch = &s_priority_match_work;
    uint32_t request, session, mode;
    int32_t delta;
    if (!boundary_request(&request, &session, &delta, &mode)) return VDC_PRIORITY_FOLLOW_BUSY;
    if (request != w->status.request || mode != VDC_BOUNDARY_MODE_TYPED_FOLLOW ||
        session != m->session || session != vdc_dpll_manager_feedback_session() ||
        m->generation != vdc_dpll_manager_priority_match_generation() ||
        __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != session ||
        scratch->status.retired || !s_vdc_tdma_service ||
        __atomic_load_n(&s_dpll_role_requested_generation, __ATOMIC_ACQUIRE) !=
            __atomic_load_n(&s_dpll_role_applied_generation, __ATOMIC_ACQUIRE))
        return VDC_PRIORITY_FOLLOW_BINDING;
    if ((uint32_t)(board_uptime_ms() - t->prepared_ms) > VDC_PRIORITY_FOLLOW_MAX_AGE_MS)
        return VDC_PRIORITY_FOLLOW_AGE;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&scratch->ring_after)) return VDC_PRIORITY_FOLLOW_BUSY;
    const tdma_ring_clock_snapshot_t *r = &scratch->ring_after;
    const tdma_pio_spi_event_live_snapshot_t *live = &scratch->live_after;
    const vdc_priority_rx_snapshot_t *rx = &scratch->rx_after;
    if (!r->enabled || !r->adapter_started) return VDC_PRIORITY_FOLLOW_STOP;
    if (r->config_seq != m->ring_config_seq || r->applied_config_seq != m->ring_applied_seq ||
        r->local_slot_id != m->local_slot || r->reference_slot_id != m->reference_slot ||
        r->node_count != m->node_count || r->schedule_crc32 != m->schedule_crc32 ||
        s_vdc_tdma_service->ring_runtime.ring_profile_crc32 != m->profile_crc32)
        return VDC_PRIORITY_FOLLOW_BINDING;
    if (!tdma_runtime_owner_get_event_live_snapshot(&scratch->live_after)) return VDC_PRIORITY_FOLLOW_BUSY;
    if (!priority_match_live_valid(live) || live->arm_epoch != m->arm_epoch ||
        live->record.epoch != m->observer_epoch || live->tick_hz != m->tick_hz ||
        live->timer1_enable_before != t->anchor_before ||
        live->timer1_enable_after != t->anchor_after) return VDC_PRIORITY_FOLLOW_BINDING;
    if (!vdc_priority_rx_copy_live(&scratch->rx_after)) return VDC_PRIORITY_FOLLOW_BUSY;
    if (rx->epoch != m->rx_epoch || rx->source_slot != m->source_slot ||
        rx->typed_record.binding_generation != m->generation ||
        !(rx->target_mask & (1u << m->local_slot)) ||
        s_vdc_domain.path_delay.table_crc32 != t->path_table_crc ||
        s_vdc_domain.path_delay.schedule_crc32 != m->schedule_crc32 ||
        !vdc_domain_active_observation_path_delay_lookup(&s_vdc_domain.path_delay,
            m->reference_slot, m->local_slot, &scratch->path_after) ||
        scratch->path_after.cal_crc32 != m->path_crc32 || scratch->path_after.delay_ns != m->delay_ns ||
        s_vdc_domain.control.profile.valid != 1u ||
        s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER ||
        s_vdc_domain.control.profile.generation != m->role_generation ||
        s_vdc_domain.control.profile.follow_master_slot_id != m->reference_slot ||
        s_vdc_domain.schedule.local_slot_id != m->local_slot ||
        s_vdc_domain.schedule.schedule_crc32 != m->schedule_crc32 ||
        s_vdc_domain.clock.epoch_id != m->clock_epoch || s_vdc_domain.clock.run_id != m->clock_run)
        return VDC_PRIORITY_FOLLOW_BINDING;
    model_feedback_load(&scratch->model_after);
    const bool exact_model =
        memcmp(&t->model, &scratch->model_after, sizeof(t->model)) == 0 &&
        memcmp(&t->model.dco, &s_vdc_domain.dco, sizeof(t->model.dco)) == 0;
    const bool confirmed_successor =
        priority_phase_known_successor(&t->model, &scratch->model_after) &&
        memcmp(&scratch->model_after.dco, &s_vdc_domain.dco, sizeof(t->model.dco)) == 0;
    if (!s_vdc_domain.ready || !s_vdc_domain.clock.valid || !s_vdc_domain.servo.enabled ||
        t->servo_crc != s_vdc_domain.servo.servo_profile_crc32 ||
        !t->model.token || t->model.token == UINT32_MAX || m->raw_lo < t->model.valid_from_raw ||
        (!exact_model && !confirmed_successor) ||
        t->model.applied_command_seq != s_vdc_domain.control.last_follower_command_seq)
        return VDC_PRIORITY_FOLLOW_MODEL;
    uint64_t raw_now;
    if (!vdc_timestamp_clock_try_read_ticks64(m->tick_hz, &raw_now)) return VDC_PRIORITY_FOLLOW_BUSY;
    if (m->raw_hi < m->raw_lo || raw_now < m->raw_hi ||
        raw_now - m->raw_lo > (uint64_t)m->tick_hz * VDC_PRIORITY_FOLLOW_MAX_AGE_MS / 1000u ||
        (uint32_t)(board_uptime_ms() - t->prepared_ms) > VDC_PRIORITY_FOLLOW_MAX_AGE_MS)
        return VDC_PRIORITY_FOLLOW_AGE;
    return VDC_PRIORITY_FOLLOW_OK;
}

/* Included after priority_follow_current(), under the same Core1 owner.
 * Successful Domain commits remain provisional until final publication
 * proves exactly the model that this owner committed. */
#ifndef VDC_PRIORITY_TRACE_PHASE_HOOK
#define VDC_PRIORITY_TRACE_PHASE_HOOK(snapshot) ((void)(snapshot))
#endif

vdc_priority_phase_work_t s_priority_phase_work;
uint32_t s_priority_phase_request;
static uint32_t s_priority_phase_guard;
static uint32_t s_priority_phase_words[sizeof(vdc_priority_phase_snapshot_t) / 4u];

static void priority_phase_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_phase_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_phase_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    boundary_store(s_priority_phase_words, &s_priority_phase_work.status,
        sizeof(s_priority_phase_work.status));
    __atomic_store_n(&s_priority_phase_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_priority_follow_phase(vdc_priority_phase_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_phase_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_phase_snapshot_t copy;
    match_load_words(s_priority_phase_words, &copy, sizeof(copy));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_phase_guard, __ATOMIC_ACQUIRE)) return false;
    *out = copy;
    return true;
}

bool vdc_dpll_manager_try_priority_follow_phase_enabled(bool *enabled)
{
    if (!enabled) return false;
    const uint32_t value = __atomic_load_n(&s_priority_phase_request, __ATOMIC_ACQUIRE);
    if (value > 1u) return false;
    *enabled = value != 0u;
    return true;
}

static bool priority_phase_set_stopped(void *context)
{
    const uint32_t requested = *(const uint32_t *)context;
    __atomic_store_n(&s_priority_phase_request, requested, __ATOMIC_RELEASE);
    if (requested != 0u) priority_note_host_control();
    return true;
}

bool vdc_dpll_manager_set_priority_follow_phase(bool enabled)
{
    const uint32_t requested = enabled ? 1u : 0u;
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            priority_phase_set_stopped, (void *)&requested);
}

static bool priority_phase_add_i64(int64_t a, int64_t b, int64_t *out)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    *out = a + b;
    return true;
}

static bool priority_phase_normalize(uint64_t actual, int64_t cumulative, uint64_t *out)
{
    if (cumulative >= 0) {
        if (actual < (uint64_t)cumulative) return false;
        *out = actual - (uint64_t)cumulative;
    } else {
        const uint64_t magnitude = (uint64_t)(-(cumulative + 1)) + 1u;
        if (UINT64_MAX - actual < magnitude) return false;
        *out = actual + magnitude;
    }
    return true;
}

static int64_t priority_phase_delta(int64_t lo, int64_t hi)
{
    if (lo > hi) return 0;
    /* Control estimate only: retain the full MATCH interval as evidence.
     * Unsigned subtraction also covers [INT64_MIN, INT64_MAX]. The half
     * width fits int64_t and adding it to lo remains inside [lo, hi]. */
    const uint64_t width = (uint64_t)hi - (uint64_t)lo;
    int64_t midpoint = lo + (int64_t)(width / 2u);
    /* Round an odd negative sum toward zero without overflowing lo + hi. */
    if (midpoint < 0 && (width & 1u)) ++midpoint;
    /* Clamp before negating: INT64_MIN is legal input. */
    if (midpoint > VDC_PRIORITY_PHASE_MAX_DELTA_NS) return -VDC_PRIORITY_PHASE_MAX_DELTA_NS;
    if (midpoint < -VDC_PRIORITY_PHASE_MAX_DELTA_NS) return VDC_PRIORITY_PHASE_MAX_DELTA_NS;
    return -midpoint;
}

static void priority_phase_reset_request(void)
{
    memset(&s_priority_phase_work, 0, sizeof(s_priority_phase_work));
    s_priority_phase_work.status.schema = 1u;
    priority_phase_publish();
}

static void priority_phase_cancel(uint32_t reason, bool retire)
{
    vdc_priority_phase_work_t *p = &s_priority_phase_work;
    if (p->pending || p->receipt_pending)
        p->status.cancelled = priority_match_inc(p->status.cancelled);
    p->pending = p->receipt_pending = 0u;
    p->status.reason = reason == VDC_PRIORITY_FOLLOW_STOP ? VDC_PRIORITY_PHASE_STOP :
        reason == VDC_PRIORITY_FOLLOW_MODEL ? VDC_PRIORITY_PHASE_MODEL :
        reason == VDC_PRIORITY_FOLLOW_AGE ? VDC_PRIORITY_PHASE_AGE :
        reason == VDC_PRIORITY_FOLLOW_OVERFLOW ? VDC_PRIORITY_PHASE_OVERFLOW :
        reason == VDC_PRIORITY_FOLLOW_OFF ? VDC_PRIORITY_PHASE_OFF : VDC_PRIORITY_PHASE_BINDING;
    if (retire) {
        p->status.active = 0u;
        p->status.state = VDC_PRIORITY_PHASE_RETIRED;
    }
    priority_phase_publish();
}

static bool priority_phase_model_metadata_equal(
    const vdc_dpll_manager_committed_model_t *a,
    const vdc_dpll_manager_committed_model_t *b)
{
    return a->session == b->session && a->role_generation == b->role_generation &&
        a->applied_command_seq == b->applied_command_seq &&
        a->clock_epoch_id == b->clock_epoch_id && a->clock_run_id == b->clock_run_id &&
        a->local_slot == b->local_slot;
}

/* Only a pure lifecycle check may use this narrow already-confirmed successor.
 * An old event is never an executable candidate under the new model. */
static bool priority_phase_known_successor(
    const vdc_dpll_manager_committed_model_t *before,
    const vdc_dpll_manager_committed_model_t *current)
{
    const vdc_priority_phase_work_t *p = &s_priority_phase_work;
    if (p->pending || p->receipt_pending || s_priority_follow_work.pending ||
        !p->status.applied || before->token != p->status.before_model ||
        current->token != p->known_model || current->token != p->status.after_model ||
        current->valid_from_raw != p->status.valid_from_raw ||
        !priority_phase_model_metadata_equal(before, current) ||
        memcmp(&current->dco, &p->expected_dco, sizeof(current->dco))) return false;
    vdc_dco_control_t expected_before = p->expected_dco;
    expected_before.base_vdc_time64_ns = p->status.before_base_vdc;
    expected_before.dco_update_seq = p->status.before_dco;
    return memcmp(&before->dco, &expected_before, sizeof(expected_before)) == 0;
}

static bool priority_phase_prepare_ticket(bool new_binding)
{
    vdc_priority_phase_work_t *p = &s_priority_phase_work;
    vdc_priority_phase_snapshot_t *s = &p->status;
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    const vdc_priority_match_snapshot_t *m = &w->ticket.match;
    if (new_binding) {
        bool enabled;
        if (!vdc_dpll_manager_try_priority_follow_phase_enabled(&enabled)) return false;
        s->enabled = enabled ? 1u : 0u;
        s->request = w->status.request; s->session = m->session; s->generation = m->generation;
        s->active = s->enabled;
        s->state = s->enabled ? VDC_PRIORITY_PHASE_WAITING : VDC_PRIORITY_PHASE_DISABLED;
    }
    if (p->receipt_pending || !p->initialized || p->known_model != m->model_token ||
        memcmp(&p->expected_dco, &w->ticket.model.dco, sizeof(p->expected_dco))) {
        if (s->rate_epoch == UINT32_MAX) return false;
        ++s->rate_epoch;
        s->cumulative_ns = 0;
        p->known_model = m->model_token;
        p->expected_dco = w->ticket.model.dco;
        p->initialized = 1u;
        p->receipt_pending = 0u;
        w->have_baseline = w->next_evaluation = w->baseline_replacements = 0u;
    }
    if (!priority_phase_normalize(m->local_lo, s->cumulative_ns, &w->ticket.normalized_lo) ||
        !priority_phase_normalize(m->local_hi, s->cumulative_ns, &w->ticket.normalized_hi)) return false;
    w->ticket.rate_epoch = s->rate_epoch;
    p->pending = s->enabled && m->event_sequence > p->consumed_sequence;
    if (p->pending) {
        s->prepared = priority_match_inc(s->prepared);
        s->state = VDC_PRIORITY_PHASE_READY;
    }
    priority_phase_publish();
    return true;
}

static void priority_phase_rate_priority(void)
{
    vdc_priority_phase_work_t *p = &s_priority_phase_work;
    if (p->pending) {
        p->pending = 0u;
        p->status.held = priority_match_inc(p->status.held);
        p->status.state = VDC_PRIORITY_PHASE_HELD;
        p->status.reason = VDC_PRIORITY_PHASE_RATE_PRIORITY;
        priority_phase_publish();
    }
}

static void priority_phase_apply_core1(void)
{
    vdc_priority_phase_work_t *p = &s_priority_phase_work;
    vdc_priority_phase_snapshot_t *s = &p->status;
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    const vdc_priority_match_snapshot_t *m = &w->ticket.match;
    if (!p->pending || !s->enabled || !s->active || p->receipt_pending) return;
    s->calls = priority_match_inc(s->calls);
    const uint32_t ms = board_uptime_ms();
    if (p->have_attempt && (uint32_t)(ms - p->last_attempt_ms) < VDC_PRIORITY_PHASE_MIN_INTERVAL_MS) {
        p->pending = 0u;
        s->state = VDC_PRIORITY_PHASE_WAITING; s->reason = VDC_PRIORITY_PHASE_INTERVAL;
        priority_phase_publish(); return;
    }
    /* Consume the work opportunity before every validation or arithmetic.
     * Rejection, uncertainty and publication failure cannot refund it. */
    p->have_attempt = 1u; p->last_attempt_ms = ms; p->consumed_sequence = m->event_sequence;
    s->attempted = priority_match_inc(s->attempted);
    s->event_sequence = m->event_sequence; s->carrier_sequence = m->carrier_sequence;
    s->source_slot = m->reference_slot; s->target_slot = m->local_slot;
    s->before_model = w->ticket.model.token; s->after_model = 0u;
    s->before_dco = w->ticket.model.dco.dco_update_seq; s->after_dco = 0u;
    s->raw_lo = m->raw_lo; s->raw_hi = m->raw_hi;
    s->local_lo = m->local_lo; s->local_hi = m->local_hi;
    s->expected_lo = m->expected_lo; s->expected_hi = m->expected_hi;
    s->residual_lo = m->residual_lo; s->residual_hi = m->residual_hi;
    s->before_base_vdc = w->ticket.model.dco.base_vdc_time64_ns;
    s->after_base_vdc = s->before_base_vdc;
    s->delta_ns = 0;
    uint32_t reason = VDC_PRIORITY_PHASE_OK;
    const uint64_t now = vdc_dpll_manager_now_ns();
    if (now == UINT64_MAX) reason = VDC_PRIORITY_PHASE_MODEL;
    else if (!(__atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE) & 1u)) reason = VDC_PRIORITY_PHASE_BINDING;
    else {
        const uint32_t current = priority_follow_current();
        if (current != VDC_PRIORITY_FOLLOW_OK) {
            if (current != VDC_PRIORITY_FOLLOW_BUSY) {
                priority_follow_cancel(current, current == VDC_PRIORITY_FOLLOW_BINDING ||
                    current == VDC_PRIORITY_FOLLOW_STOP);
                priority_follow_publish();
            }
            reason = current == VDC_PRIORITY_FOLLOW_BUSY ? VDC_PRIORITY_PHASE_BUSY :
                current == VDC_PRIORITY_FOLLOW_AGE ? VDC_PRIORITY_PHASE_AGE :
                current == VDC_PRIORITY_FOLLOW_STOP ? VDC_PRIORITY_PHASE_STOP : VDC_PRIORITY_PHASE_BINDING;
        }
    }
    p->pending = 0u;
    if (!reason && (s_vdc_domain.dco.lock_state == VDC_DOMAIN_LOCK_LOCKED ||
                    s_vdc_domain.dpll.state == VDC_DOMAIN_LOCK_LOCKED)) reason = VDC_PRIORITY_PHASE_FORMAL_LOCK;
    if (!reason && m->residual_lo > m->residual_hi) reason = VDC_PRIORITY_PHASE_OVERFLOW;
    if (!reason) {
        s->delta_ns = priority_phase_delta(m->residual_lo, m->residual_hi);
        if (!s->delta_ns) reason = VDC_PRIORITY_PHASE_ZERO_CROSSING;
        else if (!priority_phase_add_i64(s->cumulative_ns, s->delta_ns, &p->next_cumulative))
            reason = VDC_PRIORITY_PHASE_OVERFLOW;
    }
    if (!reason) {
        const vdc_dpll_local_phase_delta_t command = {
            .source_slot_id = m->reference_slot, .target_slot_id = m->local_slot,
            .expected_control_generation = m->role_generation, .schedule_crc32 = m->schedule_crc32,
            .servo_profile_crc32 = w->ticket.servo_crc, .clock_epoch_id = m->clock_epoch,
            .clock_run_id = m->clock_run, .expected_dco_update_seq = s->before_dco,
            .delta_phase_ns = s->delta_ns};
        if (!vdc_domain_apply_local_follow_phase_delta(&s_vdc_domain, &command, now)) reason = VDC_PRIORITY_PHASE_DOMAIN;
        else {
            p->expected_dco = s_vdc_domain.dco;
            p->receipt_pending = 1u;
            s->committed = priority_match_inc(s->committed);
            s->after_dco = p->expected_dco.dco_update_seq;
            s->after_base_vdc = p->expected_dco.base_vdc_time64_ns;
            vdc_dpll_manager_publish_runtime_snapshot_locked();
        }
    }
    s->reason = reason;
    if (reason == VDC_PRIORITY_PHASE_ZERO_CROSSING) {
        s->held = priority_match_inc(s->held); s->state = VDC_PRIORITY_PHASE_HELD;
    } else if (reason) {
        s->rejected = priority_match_inc(s->rejected); s->state = VDC_PRIORITY_PHASE_REJECTED;
    }
    priority_phase_publish();
}

void phase_model_committed_core1(const vdc_dpll_manager_committed_model_t *model)
{
    vdc_priority_phase_work_t *p = &s_priority_phase_work;
    vdc_priority_phase_snapshot_t *s = &p->status;
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    if (!p->receipt_pending) return;
    p->receipt_pending = 0u;
    if (!model || !model->token || model->token <= s->before_model ||
        model->valid_from_raw < w->ticket.model.valid_from_raw || model->valid_from_raw < s->raw_hi ||
        !priority_phase_model_metadata_equal(&w->ticket.model, model) ||
        memcmp(&model->dco, &p->expected_dco, sizeof(p->expected_dco)) ||
        memcmp(&s_vdc_domain.dco, &p->expected_dco, sizeof(p->expected_dco))) {
        s->rejected = priority_match_inc(s->rejected);
        s->state = VDC_PRIORITY_PHASE_REJECTED; s->reason = VDC_PRIORITY_PHASE_LEDGER;
        p->known_model = 0u;
        w->have_baseline = w->pending = w->next_evaluation = w->baseline_replacements = 0u;
        priority_phase_publish(); return;
    }
    p->known_model = model->token;
    s->after_model = model->token; s->valid_from_raw = model->valid_from_raw;
    s->cumulative_ns = p->next_cumulative;
    s->applied = priority_match_inc(s->applied);
    s->state = VDC_PRIORITY_PHASE_APPLIED; s->reason = VDC_PRIORITY_PHASE_OK;
    priority_phase_publish();
    VDC_PRIORITY_TRACE_PHASE_HOOK(s);
}


static void priority_follow_record_decision(int32_t step)
{
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    vdc_priority_follow_snapshot_t *s = &w->status;
    const vdc_priority_match_snapshot_t *m = &w->ticket.match;
    s->baseline_sequence = w->previous.sequence; s->event_sequence = m->event_sequence;
    s->carrier_sequence = m->carrier_sequence; s->observer_epoch = m->observer_epoch;
    s->rx_epoch = m->rx_epoch; s->model_token = m->model_token;
    s->expected_dco_seq = w->ticket.model.dco.dco_update_seq;
    s->before_dco_seq = w->ticket.model.dco.dco_update_seq;
    s->after_dco_seq = s_vdc_domain.dco.dco_update_seq;
    s->before_ppb = w->ticket.model.dco.period_adjust_ppb;
    s->after_ppb = s_vdc_domain.dco.period_adjust_ppb; s->selected_delta_ppb = step;
    s->local_slot = m->local_slot; s->reference_slot = m->reference_slot;
    s->arm_epoch = m->arm_epoch; s->baseline_raw = w->previous.raw_lo;
    s->raw_lo = m->raw_lo; s->raw_hi = m->raw_hi;
    s->interval_lo = w->expected_delta_lo; s->interval_hi = w->expected_delta_hi;
    s->local_interval_lo = w->local_delta_lo; s->local_interval_hi = w->local_delta_hi;
    s->error_lo_ppb = w->error_lo; s->error_hi_ppb = w->error_hi;
}

__attribute__((noinline)) void priority_follow_apply_core1(void)
{
    if (get_core_num() != 1u) return;
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    vdc_priority_follow_snapshot_t *s = &w->status;
    if (!s->active || !w->bound) return;
    if (!(__atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE) & 1u)) {
        priority_follow_cancel(VDC_PRIORITY_FOLLOW_MODE, true);
        priority_follow_publish(); return;
    }
    /* Read actual local service time BEFORE the final owner validation: a
     * pending role/config change cannot hide in a time-read/commit gap. */
    const uint64_t now = vdc_dpll_manager_now_ns();
    if (now == UINT64_MAX) {
        s->last_reason = VDC_PRIORITY_FOLLOW_MODEL;
        priority_follow_publish(); return;
    }
    const uint32_t current = priority_follow_current();
    if (current != VDC_PRIORITY_FOLLOW_OK) {
        if (current != VDC_PRIORITY_FOLLOW_BUSY)
            priority_follow_cancel(current, current == VDC_PRIORITY_FOLLOW_BINDING ||
                current == VDC_PRIORITY_FOLLOW_STOP);
        else s->last_reason = current;
        priority_follow_publish(); return;
    }
    priority_follow_health_accept();
    if (!w->pending) { priority_phase_apply_core1(); return; }
    const vdc_priority_match_snapshot_t *m = &w->ticket.match;
    if (w->have_consumed && m->event_sequence <= w->consumed_sequence) {
        w->pending = 0u; s->repeated = priority_match_inc(s->repeated);
        s->last_reason = VDC_PRIORITY_FOLLOW_DUPLICATE;
        priority_follow_publish(); return;
    }
    const int32_t step = priority_follow_delta(w->error_lo, w->error_hi,
        s_vdc_domain.dco.period_adjust_ppb, s_vdc_domain.servo.sanity_freq_limit_ppb);
    bool applied = false;
    if (step) {
        priority_phase_rate_priority();
        const vdc_dpll_local_rate_delta_t command = {
            .source_slot_id = m->reference_slot, .target_slot_id = m->local_slot,
            .expected_control_generation = m->role_generation, .schedule_crc32 = m->schedule_crc32,
            .servo_profile_crc32 = w->ticket.servo_crc, .clock_epoch_id = m->clock_epoch,
            .clock_run_id = m->clock_run,
            .expected_dco_update_seq = w->ticket.model.dco.dco_update_seq, .delta_rate_ppb = step};
        applied = vdc_domain_apply_local_follow_rate_delta(&s_vdc_domain, &command, now);
    }
    priority_follow_record_decision(step);
    w->pending = 0u;
    if (step && !applied) {
        s->rejected = priority_match_inc(s->rejected);
        priority_follow_cancel(VDC_PRIORITY_FOLLOW_DOMAIN, false);
    } else {
        w->have_consumed = 1u; w->consumed_sequence = m->event_sequence;
        if (applied) {
            s->applied = priority_match_inc(s->applied); s->state = VDC_PRIORITY_FOLLOW_APPLIED;
            s->last_reason = VDC_PRIORITY_FOLLOW_OK;
            /* Own DCO commit changes the actual model/valid_from at guard end.
             * Only a later event projected by that model can seed a new pair. */
            w->have_baseline = w->next_evaluation = w->baseline_replacements = 0u;
            vdc_dpll_manager_publish_runtime_snapshot_locked();
        } else {
            s->no_adjust = priority_match_inc(s->no_adjust);
            const bool overlaps_deadband = w->error_lo <= VDC_BOUNDARY_AUTO_DEADBAND_PPB &&
                w->error_hi >= -VDC_BOUNDARY_AUTO_DEADBAND_PPB;
            const bool inside_deadband = w->error_lo >= -VDC_BOUNDARY_AUTO_DEADBAND_PPB &&
                w->error_hi <= VDC_BOUNDARY_AUTO_DEADBAND_PPB;
            /* Only unresolved uncertainty buys a later window. A narrow
             * deadband result, a saturated zero step, or the final consumed
             * threshold starts a fresh baseline without forcing adjustment. */
            if (!overlaps_deadband || inside_deadband ||
                w->next_evaluation >= VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS)
                priority_follow_baseline();
            s->state = VDC_PRIORITY_FOLLOW_NO_ADJUST;
            s->last_reason = overlaps_deadband ? VDC_PRIORITY_FOLLOW_DEADBAND : VDC_PRIORITY_FOLLOW_DOMAIN;
        }
    }
    VDC_PRIORITY_TRACE_DECISION_HOOK(s);
    priority_follow_publish();
    if (!step) priority_phase_apply_core1();
}
