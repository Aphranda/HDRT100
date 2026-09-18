/* Core0 publishes only STOP-gated intent. Core1 alone binds a run, projects
 * source events and freezes complete mailbox offers. No per-event Core0 work. */
#include "vdc_priority_tx.h"
#include "vdc_priority_codec.h"

#ifndef VDC_PRIORITY_TRACE_ORIGIN_HOOK
#define VDC_PRIORITY_TRACE_ORIGIN_HOOK(evidence) ((void)(evidence))
#endif

static uint32_t s_priority_tx_request;
static uint32_t s_priority_tx_last_request; /* Core0 only, never wraps. */
static uint32_t s_priority_tx_guard;
static uint32_t s_priority_tx_words[sizeof(vdc_priority_tx_snapshot_t) / 4u];

/* Keep transient source/model copies off the deeply nested TDMA stack.
 * All fields, including the diagnostic snapshot, have one Core1 writer. */
typedef struct {
    vdc_priority_tx_snapshot_t status;
    tdma_ring_runtime_config_t binding;
    tdma_origin_raw_reference_t source, seen;
    vdc_dpll_manager_committed_model_t model, after;
    vdc_dpll_manager_projected_event_t event;
    vdc_priority_codec_record_t record;
    vdc_priority_tx_origin_evidence_t evidence;
    uint32_t bound, have_seen, tick_hz;
    uint8_t candidate[TDMA_FLIGHT_SHORT_SLOT_SIZE];
} vdc_priority_tx_work_t;
static vdc_priority_tx_work_t s_priority_tx_work;
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
    if (generation) s_priority_tx_last_request = generation;
    __atomic_store_n(&s_priority_tx_request, generation, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_priority_sync(uint32_t generation)
{
    return get_core_num() == 0u && s_vdc_tdma_service != NULL &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            priority_tx_set_stopped, &generation);
}

static void priority_tx_publish(void)
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
    if (retire) { s->active = 0u; s->retired = 1u; }
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
    priority_tx_publish();
    return TDMA_PRIORITY_TX_READY;
}

tdma_priority_tx_result_t __attribute__((noinline)) vdc_priority_tx_core1(
    const tdma_ring_runtime_config_t *config,
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE])
{
    const uint32_t request = vdc_dpll_manager_priority_sync_generation();
    if (get_core_num() != 1u)
        return request ? TDMA_PRIORITY_TX_EMPTY : TDMA_PRIORITY_TX_DISABLED;
    vdc_priority_tx_work_t *w = &s_priority_tx_work;
    vdc_priority_tx_snapshot_t *s = &w->status;
    if (!request) {
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
