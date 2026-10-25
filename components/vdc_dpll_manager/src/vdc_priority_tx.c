#include "vdc_dpll_manager.h"
#include "vdc_dpll_manager_internal.h"

#define VDC_PRIORITY_TRACE_ORIGIN_HOOK(evidence) priority_trace_origin_core1(evidence)
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

/* Core0 publishes only STOP-gated intent. Core1 alone binds a run, projects
 * source events and freezes complete mailbox offers. No per-event Core0 work. */
#include "vdc_priority_tx.h"
#include "vdc_priority_codec.h"

#ifndef VDC_PRIORITY_TRACE_ORIGIN_HOOK
#define VDC_PRIORITY_TRACE_ORIGIN_HOOK(evidence) ((void)(evidence))
#endif

uint32_t s_priority_tx_request;
static uint32_t s_priority_tx_last_request; /* Core0 only, never wraps. */
static uint32_t s_priority_tx_guard;
static uint32_t s_priority_tx_words[sizeof(vdc_priority_tx_snapshot_t) / 4u];

/* Keep transient source/model copies off the deeply nested TDMA stack.
 * All fields, including the diagnostic snapshot, have one Core1 writer. */
vdc_priority_tx_work_t s_priority_tx_work;
_Static_assert(sizeof(vdc_priority_tx_snapshot_t) % 4u == 0u,
    "priority TX diagnostics publish whole atomic words");

static uint32_t priority_tx_inc(uint32_t value)
{
    return value == UINT32_MAX ? value : value + 1u;
}

uint32_t vdc_dpll_manager_priority_sync_generation(void)
{
    return __atomic_load_n(&s_priority_tx_request, __ATOMIC_ACQUIRE);
}

/* STOP-installed intent; the offer provider is the sole runtime writer.
 * Only transitions and retirement publish diagnostics, not every offer. */
static uint32_t s_priority_gap_config_guard, s_priority_gap_status_guard;
static vdc_priority_tx_gap_config_t s_priority_gap_config;
static vdc_priority_tx_gap_snapshot_t s_priority_gap;
static uint32_t s_priority_gap_words[sizeof(vdc_priority_tx_gap_snapshot_t) / 4u];

bool vdc_dpll_manager_get_priority_tx_gap_config(vdc_priority_tx_gap_config_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_gap_config_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    const vdc_priority_tx_gap_config_t value = {
        .generation = __atomic_load_n(&s_priority_gap_config.generation, __ATOMIC_RELAXED),
        .session = __atomic_load_n(&s_priority_gap_config.session, __ATOMIC_RELAXED),
        .after_ms = __atomic_load_n(&s_priority_gap_config.after_ms, __ATOMIC_RELAXED),
        .duration_ms = __atomic_load_n(&s_priority_gap_config.duration_ms, __ATOMIC_RELAXED)};
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_gap_config_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}

static bool priority_gap_set_stopped(void *context)
{
    vdc_priority_tx_gap_config_t *c = context;
    c->session = c->generation ? vdc_dpll_manager_feedback_session() : 0u;
    if (c->generation && (!c->session ||
            c->generation != vdc_dpll_manager_priority_sync_generation())) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_gap_config_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(&s_priority_gap_config.generation, c->generation, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config.session, c->session, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config.after_ms, c->after_ms, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config.duration_ms, c->duration_ms, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config_guard, guard + 2u, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_priority_tx_gap(uint32_t generation, uint32_t after_ms,
    uint32_t duration_ms)
{
    if (get_core_num() != 0u || !s_vdc_tdma_service ||
        (generation ? !duration_ms : (after_ms || duration_ms))) return false;
    vdc_priority_tx_gap_config_t c = {generation, 0u, after_ms, duration_ms};
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_gap_set_stopped, &c);
}

static void priority_gap_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_gap_status_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_status_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(s_priority_gap) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_priority_gap + 4u * i, 4u);
        __atomic_store_n(s_priority_gap_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_priority_gap_status_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_priority_tx_gap(vdc_priority_tx_gap_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_gap_status_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_tx_gap_snapshot_t value;
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_priority_gap_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + 4u * i, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_gap_status_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}

void priority_gap_reset(uint32_t generation)
{
    vdc_priority_tx_gap_config_t c;
    s_priority_gap = (vdc_priority_tx_gap_snapshot_t){.schema = 1u};
    if (vdc_dpll_manager_get_priority_tx_gap_config(&c) && c.generation == generation) {
        s_priority_gap.generation = generation;
        s_priority_gap.session = c.session;
        s_priority_gap.after_ms = c.after_ms;
        s_priority_gap.duration_ms = c.duration_ms;
        s_priority_gap.state = c.session == vdc_dpll_manager_feedback_session()
            ? VDC_PRIORITY_GAP_WAITING : VDC_PRIORITY_GAP_CANCELLED;
    }
    priority_gap_publish();
}

static void priority_gap_cancel(void)
{
    if (s_priority_gap.state < VDC_PRIORITY_GAP_WAITING ||
        s_priority_gap.state > VDC_PRIORITY_GAP_RECOVERING) return;
    s_priority_gap.state = VDC_PRIORITY_GAP_CANCELLED;
    priority_gap_publish();
}

static bool priority_gap_suppress(uint64_t now)
{
    vdc_priority_tx_gap_snapshot_t *g = &s_priority_gap;
    const vdc_priority_tx_work_t *w = &s_priority_tx_work;
    if (g->state < VDC_PRIORITY_GAP_WAITING || g->state > VDC_PRIORITY_GAP_RECOVERING)
        return false;
    if (!g->tick_hz) {
        g->tick_hz = w->source.tick_hz;
        g->anchor_raw = now;
        priority_gap_publish();
    }
    if (now < g->anchor_raw) { priority_gap_cancel(); return false; }
    /* Each multiplication fits uint64 even for maximum uint32 inputs.
     * Divide before adding the two durations to keep the sum bounded. */
    const uint64_t begin = (uint64_t)g->tick_hz * g->after_ms / 1000u;
    const uint64_t end = begin + (uint64_t)g->tick_hz * g->duration_ms / 1000u;
    const uint64_t elapsed = now - g->anchor_raw;
    if (elapsed < begin) return false;
    if (elapsed >= end) {
        if (!g->suppressed) {
            g->state = VDC_PRIORITY_GAP_MISSED;
            priority_gap_publish();
            return false;
        }
        g->state = VDC_PRIORITY_GAP_RECOVERING;
        /* Do not label a cached or in-gap source as recovery. */
        if (w->source.timer_lower >= g->anchor_raw &&
            w->source.timer_lower - g->anchor_raw >= end &&
            w->source.sequence > g->before_sequence) {
            g->resume_raw = now;
            return false;
        }
    }
    const bool first = !g->suppressed;
    if (first) {
        g->state = VDC_PRIORITY_GAP_ACTIVE;
        g->before_sequence = w->status.event_sequence;
        g->first_suppressed_raw = now;
    }
    g->last_suppressed_raw = now;
    g->suppressed = priority_tx_inc(g->suppressed);
    if (first) priority_gap_publish();
    return true;
}

static void priority_gap_resumed(void)
{
    if (s_priority_gap.state != VDC_PRIORITY_GAP_RECOVERING) return;
    s_priority_gap.resume_sequence = s_priority_tx_work.status.event_sequence;
    s_priority_gap.state = VDC_PRIORITY_GAP_DONE;
    priority_gap_publish();
}


bool vdc_priority_tx_origin_trace_eligible_core1(uint32_t generation, uint32_t session)
{
    if (get_core_num() != 1u || !generation || !session ||
        generation != vdc_dpll_manager_priority_sync_generation() ||
        session != vdc_dpll_manager_feedback_session()) return false;
    const vdc_priority_tx_work_t *w = &s_priority_tx_work;
    if (w->status.generation == generation &&
        (w->status.retired || w->status.have_offer || w->status.encoded))
        return false;
    return generation == vdc_dpll_manager_priority_sync_generation() &&
        session == vdc_dpll_manager_feedback_session();
}

static bool priority_tx_set_stopped(void *context)
{
    const uint32_t generation = *(const uint32_t *)context;
    if (generation && (generation <= s_priority_tx_last_request ||
            !vdc_dpll_manager_feedback_session())) return false;
    if (generation) {
        s_priority_tx_last_request = generation;
        priority_note_host_control();
    }
    __atomic_store_n(&s_priority_tx_request, generation, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_priority_sync(uint32_t generation)
{
    return get_core_num() == 0u && s_vdc_tdma_service != NULL &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            priority_tx_set_stopped, &generation);
}

void priority_tx_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_tx_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_tx_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(s_priority_tx_work.status) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_priority_tx_work.status + 4u * i, 4u);
        __atomic_store_n(s_priority_tx_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_priority_tx_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_priority_tx(vdc_priority_tx_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_tx_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_tx_snapshot_t value;
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_priority_tx_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + 4u * i, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_tx_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}

static tdma_priority_tx_result_t priority_tx_empty(uint32_t reason, bool retire)
{
    vdc_priority_tx_snapshot_t *s = &s_priority_tx_work.status;
    s->rejected = priority_tx_inc(s->rejected);
    s->last_reject = reason;
    if (retire) { s->active = 0u; s->retired = 1u; priority_gap_cancel(); }
    priority_tx_publish();
    return TDMA_PRIORITY_TX_EMPTY;
}

static bool priority_tx_config_valid(const tdma_ring_runtime_config_t *c)
{
    return c->enabled && c->node_count >= 2u &&
        c->node_count <= TDMA_FLIGHT_SHORT_SLOT_COUNT &&
        c->local_slot_id < c->node_count && c->reference_slot_id == c->local_slot_id &&
        c->schedule_crc32 && c->ring_profile_crc32 && c->cycle_period_ns;
}

static bool priority_tx_model_valid(const vdc_dpll_manager_committed_model_t *m,
    const tdma_ring_runtime_config_t *c, uint32_t session)
{
    return session && m->session == session && m->token && m->role_generation &&
        m->clock_epoch_id && m->clock_run_id && m->local_slot == c->local_slot_id &&
        m->dco.valid == 1u && m->dco.nominal_period_ns &&
        m->dco.lock_state <= VDC_DOMAIN_LOCK_FAULT &&
        m->dco.tdma_schedule_crc32 == c->schedule_crc32;
}

static bool priority_tx_binding_current(const tdma_ring_runtime_config_t *c,
    const vdc_dpll_manager_committed_model_t *m)
{
    const vdc_priority_tx_work_t *w = &s_priority_tx_work;
    const vdc_priority_tx_snapshot_t *s = &w->status;
    return memcmp(&w->binding, c, sizeof(*c)) == 0 &&
        s->source_epoch == w->source.epoch && w->tick_hz == w->source.tick_hz &&
        s->session == m->session && s->role_generation == m->role_generation &&
        s->clock_epoch == m->clock_epoch_id && s->clock_run == m->clock_run_id;
}

static bool priority_tx_same_source(const tdma_origin_raw_reference_t *a,
    const tdma_origin_raw_reference_t *b)
{
    return a->epoch == b->epoch && a->sequence == b->sequence &&
        a->identity == b->identity && a->published_version == b->published_version &&
        a->tick_hz == b->tick_hz && a->timer_lower == b->timer_lower &&
        a->timer_upper == b->timer_upper;
}

static void priority_tx_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static tdma_priority_tx_result_t priority_tx_ready(uint8_t *mailbox, bool repeated)
{
    vdc_priority_tx_snapshot_t *s = &s_priority_tx_work.status;
    if (repeated) s->repeated = priority_tx_inc(s->repeated);
    s->last_reject = VDC_PRIORITY_TX_REJECT_NONE;
    memcpy(mailbox, s->mailbox, sizeof(s->mailbox));
    priority_gap_resumed();
    priority_tx_publish();
    return TDMA_PRIORITY_TX_READY;
}

tdma_priority_tx_result_t __attribute__((noinline)) vdc_priority_tx_core1(
    const tdma_ring_runtime_config_t *config,
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE])
{
    const uint32_t request = vdc_dpll_manager_priority_sync_generation();
    vdc_priority_tx_work_t *w = &s_priority_tx_work;
    vdc_priority_tx_snapshot_t *s = &w->status;
    /* Provider-entry evidence, counted before every guard below (VDC-ISO-001):
     * `calls` alone cannot tell "the fast path never asked us" from "we were
     * asked off Core1 and had to answer EMPTY", and the difference decides
     * whether the base lane or the load's route owns the problem. */
    s->provider_calls = priority_tx_inc(s->provider_calls);
    if (get_core_num() != 1u) {
        s->provider_offcore = priority_tx_inc(s->provider_offcore);
        priority_tx_publish();
        return request ? TDMA_PRIORITY_TX_EMPTY : TDMA_PRIORITY_TX_DISABLED;
    }
    if (!request) {
        priority_gap_cancel();
        if (s->generation && !s->retired) {
            s->active = 0u; s->retired = 1u; s->last_reject = VDC_PRIORITY_TX_REJECT_STOP;
            priority_tx_publish();
        }
        return TDMA_PRIORITY_TX_DISABLED;
    }
    if (s->generation != request) {
        memset(w, 0, sizeof(*w));
        s->schema = 1u;
        s->generation = request;
        priority_gap_reset(request);
    }
    s->calls = priority_tx_inc(s->calls);
    if (!config) return priority_tx_empty(VDC_PRIORITY_TX_REJECT_STOP, true);
    if (s->retired) return priority_tx_empty(VDC_PRIORITY_TX_REJECT_RETIRED, false);
    if (!mailbox || !priority_tx_config_valid(config))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_CONFIG, w->bound != 0u);
    if (w->bound && memcmp(&w->binding, config, sizeof(*config)) != 0)
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_BINDING, true);
    const uint32_t session = vdc_dpll_manager_feedback_session();
    if (w->bound && s->session != session)
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_BINDING, true);
    if (!vdc_dpll_manager_get_committed_model(&w->model))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_MODEL, false);
    if (w->bound && (s->session != w->model.session ||
            s->role_generation != w->model.role_generation ||
            s->clock_epoch != w->model.clock_epoch_id ||
            s->clock_run != w->model.clock_run_id ||
            s->local_slot != w->model.local_slot))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_BINDING, true);
    if (!priority_tx_model_valid(&w->model, config, session))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_MODEL, false);
    if (!tdma_runtime_owner_get_origin_raw_reference(&w->source))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_SOURCE, false);
    if (!w->source.epoch || !w->source.tick_hz || !w->source.published_version ||
        (w->source.published_version & 1u) || w->source.timer_upper < w->source.timer_lower)
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_SOURCE, false);
    if (w->bound && !priority_tx_binding_current(config, &w->model))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_BINDING, true);
    if (!w->bound) {
        w->binding = *config;
        w->tick_hz = w->source.tick_hz;
        s->source_epoch = w->source.epoch;
        s->session = w->model.session;
        s->role_generation = w->model.role_generation;
        s->clock_epoch = w->model.clock_epoch_id;
        s->clock_run = w->model.clock_run_id;
        s->local_slot = config->local_slot_id;
        s->node_count = config->node_count;
        s->schedule_crc32 = config->schedule_crc32;
        s->profile_crc32 = config->ring_profile_crc32;
        s->active = 1u;
        w->bound = 1u;
    }
    if (w->have_seen) {
        const uint32_t advance = w->source.sequence - w->seen.sequence;
        if (w->source.sequence < w->seen.sequence || advance > INT32_MAX ||
            (advance && (w->source.timer_lower < w->seen.timer_lower ||
                w->source.timer_upper < w->seen.timer_upper)) ||
            (!advance && !priority_tx_same_source(&w->source, &w->seen)))
            return priority_tx_empty(VDC_PRIORITY_TX_REJECT_SEQUENCE, true);
    }
    w->seen = w->source;
    w->have_seen = 1u;
    uint64_t now;
    /* Match the projector's two-second source horizon even when returning a
     * cached offer. Never make an old event fresh with a later carrier. */
    if (w->source.timer_lower < w->model.valid_from_raw ||
        !vdc_timestamp_clock_try_read_ticks64(w->source.tick_hz, &now) ||
        now < w->source.timer_upper ||
        now - w->source.timer_lower > (uint64_t)w->source.tick_hz * 2u)
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_MODEL, false);
    if (priority_gap_suppress(now)) return TDMA_PRIORITY_TX_EMPTY;
    if (s->have_offer && s->event_sequence == w->source.sequence) {
        /* The first encoded projection is immutable. A new model does not
         * authorize reprojection of the same source event. */
        if (s->model_token != w->model.token)
            return priority_tx_empty(VDC_PRIORITY_TX_REJECT_MODEL, false);
        return priority_tx_ready(mailbox, true);
    }
    if (!vdc_dpll_manager_project_timer1_feedback_event(session, w->model.role_generation,
            w->model.clock_epoch_id, w->model.clock_run_id, config->local_slot_id,
            config->schedule_crc32, w->source.tick_hz, w->source.timer_lower,
            w->source.timer_upper, &w->evidence.projection))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_PROJECTION, false);
    w->event = (vdc_dpll_manager_projected_event_t){
        .output_ns_lo = w->evidence.projection.output_lo,
        .output_ns_hi = w->evidence.projection.output_hi,
        .model_token = w->evidence.projection.model.token,
        .applied_command_seq = w->evidence.projection.model.applied_command_seq};
    uint32_t epoch;
    if (!vdc_dpll_manager_get_committed_model(&w->after) ||
        !tdma_runtime_owner_get_origin_reference_epoch(&epoch))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_CHANGED, false);
    if (!priority_tx_binding_current(config, &w->after) || epoch != w->source.epoch ||
        request != vdc_dpll_manager_priority_sync_generation() ||
        session != vdc_dpll_manager_feedback_session())
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_BINDING, true);
    if (w->after.token != w->model.token || w->event.model_token != w->model.token ||
        w->after.valid_from_raw != w->model.valid_from_raw ||
        w->after.applied_command_seq != w->model.applied_command_seq ||
        w->evidence.projection.model.valid_from_raw != w->model.valid_from_raw ||
        w->event.applied_command_seq != w->model.applied_command_seq ||
        !priority_tx_model_valid(&w->after, config, session))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_CHANGED, false);
    if (w->event.output_ns_hi <= w->event.output_ns_lo ||
        w->event.output_ns_hi - w->event.output_ns_lo > UINT32_MAX)
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_WIDTH, false);
    w->record = (vdc_priority_codec_record_t){
        .binding_generation = request, .event_sequence = w->source.sequence,
        .event_time_lower = w->event.output_ns_lo,
        .uncertainty_width = (uint32_t)(w->event.output_ns_hi - w->event.output_ns_lo),
        .flags = 0u};
    memset(w->candidate, 0, sizeof(w->candidate));
    priority_tx_put16(w->candidate, TDMA_FLIGHT_MAILBOX_MAGIC);
    w->candidate[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] = TDMA_FLIGHT_MAILBOX_VERSION;
    w->candidate[3] = TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS;
    w->candidate[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] = (uint8_t)config->local_slot_id;
    w->candidate[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] =
        (uint8_t)(((1u << config->node_count) - 1u) & ~(1u << config->local_slot_id));
    priority_tx_put16(w->candidate + TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET, (uint16_t)w->source.sequence);
    if (!vdc_priority_codec_encode(&w->record,
            w->candidate + TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET))
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_WIDTH, false);
    priority_tx_put16(w->candidate + TDMA_PROCESS_IMAGE_CRC_OFFSET,
        tdma_process_image_crc16_ccitt(w->candidate, TDMA_PROCESS_IMAGE_CRC_OFFSET));
    if (request != vdc_dpll_manager_priority_sync_generation() ||
        session != vdc_dpll_manager_feedback_session())
        return priority_tx_empty(VDC_PRIORITY_TX_REJECT_BINDING, true);
    /* Same-event returns above retain the original encoded bytes. */
    s->event_sequence = w->source.sequence;
    s->source_identity = w->source.identity;
    s->published_version = w->source.published_version;
    s->model_token = w->model.token;
    s->event_time_lower = w->record.event_time_lower;
    s->uncertainty_width = w->record.uncertainty_width;
    memcpy(s->mailbox, w->candidate, sizeof(s->mailbox));
    s->have_offer = 1u;
    s->encoded = priority_tx_inc(s->encoded);
    vdc_priority_tx_origin_evidence_t *e = &w->evidence;
    e->generation = request;
    e->session = session;
    e->role_generation = w->model.role_generation;
    e->clock_epoch = w->model.clock_epoch_id;
    e->clock_run = w->model.clock_run_id;
    e->local_slot = config->local_slot_id;
    e->node_count = config->node_count;
    e->schedule_crc32 = config->schedule_crc32;
    e->profile_crc32 = config->ring_profile_crc32;
    e->source_epoch = w->source.epoch;
    e->event_sequence = w->source.sequence;
    e->source_identity = w->source.identity;
    e->published_version = w->source.published_version;
    e->tick_hz = w->source.tick_hz;
    e->source_raw_lo = w->source.timer_lower;
    e->source_raw_hi = w->source.timer_upper;
    e->encoded_output_lo = w->record.event_time_lower;
    e->encoded_width = w->record.uncertainty_width;
    VDC_PRIORITY_TRACE_ORIGIN_HOOK(e);
    return priority_tx_ready(mailbox, false);
}

#undef VDC_PRIORITY_TRACE_ORIGIN_HOOK
