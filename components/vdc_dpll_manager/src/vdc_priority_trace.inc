/* Shared 76 x 100-byte maintenance pool, no second record ring. Core0 only
 * reserves the pool and publishes a stopped command. Core1 processes that
 * command between producer calls; its ACK is the ownership-transfer proof.
 * FROZEN bytes and metadata remain immutable until another acknowledged
 * command. READ holds a lease that excludes both submission and consumption. */
typedef struct {
    uint32_t sequence, command, capture_id, session, generation, mode, boundary_request, schema;
} vdc_priority_trace_request_t;
static uint32_t s_priority_trace_request_guard, s_priority_trace_ack;
static vdc_priority_trace_request_t s_priority_trace_request;
static uint32_t s_priority_trace_last_capture; /* Core0, never reused. */
static uint32_t s_priority_trace_status_guard;
static uint32_t s_priority_trace_status_words[sizeof(vdc_priority_trace_status_t) / 4u];

typedef struct {
    uint64_t last_service, last_success;
    uint32_t counters[7];
    uint32_t first_ms, last_ms, open, have_success, last_model;
    uint32_t clock_epoch, clock_run;
    uint32_t counter_valid_mask;
} vdc_priority_summary_work_t;
_Static_assert(sizeof(vdc_priority_summary_work_t) <= VDC_PRIORITY_TRACE_ORIGIN_EXTENSION_BYTES,
    "summary scratch must fit existing origin extension");
typedef struct {
    vdc_priority_trace_status_t status;
    uint64_t last_sample_raw;
    union {
        uint8_t origin[VDC_PRIORITY_TRACE_ORIGIN_EXTENSION_BYTES];
        vdc_priority_summary_work_t summary;
    };
    uint32_t boundary_request, role_generation, seen_running, bound;
    uint32_t have_match, last_match_sequence, have_decision, last_decision_sequence;
} vdc_priority_trace_work_t;
static vdc_priority_trace_work_t s_priority_trace_work;
static bool priority_trace_origin_family(uint32_t schema)
{ return schema == VDC_PRIORITY_TRACE_ORIGIN_SCHEMA || schema == VDC_PRIORITY_TRACE_SUMMARY_ORIGIN_SCHEMA; }
static bool priority_trace_summary_family(uint32_t schema)
{ return schema == VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA || schema == VDC_PRIORITY_TRACE_SUMMARY_ORIGIN_SCHEMA; }
static void priority_summary_terminal(void);
static void priority_summary_service(bool end);
_Static_assert(sizeof(vdc_priority_trace_status_t) == 148u, "typed trace status ABI");
_Static_assert(sizeof(vdc_priority_trace_header_t) == 168u, "typed trace header ABI");
_Static_assert(VDC_PRIORITY_TRACE_ORIGIN_SCHEMA == 5u, "origin schema 5 uses TIMER1 directly");
_Static_assert(sizeof(vdc_priority_trace_record_t) == sizeof(s_dpll_capture_records[0]),
    "typed trace reuses each complete legacy slot");

static bool priority_trace_request_copy(vdc_priority_trace_request_t *out)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    const vdc_priority_trace_request_t copy = {
        .sequence = __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED),
        .command = __atomic_load_n(&s_priority_trace_request.command, __ATOMIC_RELAXED),
        .capture_id = __atomic_load_n(&s_priority_trace_request.capture_id, __ATOMIC_RELAXED),
        .session = __atomic_load_n(&s_priority_trace_request.session, __ATOMIC_RELAXED),
        .generation = __atomic_load_n(&s_priority_trace_request.generation, __ATOMIC_RELAXED),
        .mode = __atomic_load_n(&s_priority_trace_request.mode, __ATOMIC_RELAXED),
        .schema = __atomic_load_n(&s_priority_trace_request.schema, __ATOMIC_RELAXED),
        .boundary_request = __atomic_load_n(&s_priority_trace_request.boundary_request, __ATOMIC_RELAXED)};
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE)) return false;
    *out = copy; return true;
}

static void priority_trace_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_trace_status_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    boundary_store(s_priority_trace_status_words, &s_priority_trace_work.status,
        sizeof(s_priority_trace_work.status));
    __atomic_store_n(&s_priority_trace_status_guard, guard + 2u, __ATOMIC_RELEASE);
}

static bool priority_trace_status_copy(vdc_priority_trace_status_t *out)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_trace_status_t copy;
    match_load_words(s_priority_trace_status_words, &copy, sizeof(copy));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_ACQUIRE)) return false;
    *out = copy; return true;
}

bool vdc_dpll_manager_get_priority_trace(vdc_priority_trace_status_t *out)
{
    if (!out) return false;
    vdc_priority_trace_request_t request;
    vdc_priority_trace_status_t copy;
    if (!priority_trace_request_copy(&request) || !priority_trace_status_copy(&copy)) return false;
    /* The requested identity is distinct from the owner's acknowledged one. */
    if (!copy.schema) copy.schema = VDC_PRIORITY_TRACE_SCHEMA;
    copy.request_seq = request.sequence;
    copy.command = request.command;
    if (copy.ack_seq != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE)) return false;
    if (copy.ack_seq == request.sequence &&
        (copy.state == VDC_PRIORITY_TRACE_REJECTED ||
         (copy.state == VDC_PRIORITY_TRACE_IDLE && copy.reason == VDC_PRIORITY_TRACE_RELEASED))) {
        const uint32_t owner = __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE);
        if (owner != DPLL_CAPTURE_POOL_LEGACY && owner != DPLL_CAPTURE_POOL_LEGACY_ACCESS) return false;
    }
    if (request.sequence != __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_ACQUIRE) ||
        (__atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE) & 1u)) return false;
    *out = copy; return true;
}

typedef struct { uint32_t command, capture_id, schema; } vdc_priority_trace_submit_t;
static bool priority_trace_submit_stopped(void *context)
{
    const vdc_priority_trace_submit_t *input = context;
    uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) || guard > UINT32_MAX - 2u ||
        !__atomic_compare_exchange_n(&s_priority_trace_request_guard, &guard, guard + 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    bool accepted = false, reserved = false;
    const uint32_t previous = __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED);
    const uint32_t owner = __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE);
    vdc_priority_trace_status_t status;
    uint32_t request = 0u, boundary_session = 0u, mode = 0u, generation = 0u;
    const uint32_t session = vdc_dpll_manager_feedback_session();
    int32_t delta;
    if (previous == UINT32_MAX || previous != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) ||
        (owner != DPLL_CAPTURE_POOL_LEGACY && owner != DPLL_CAPTURE_POOL_TYPED) ||
        !priority_trace_status_copy(&status)) goto done;
    if (input->command == VDC_PRIORITY_TRACE_COMMAND_ARM) {
        bool phase_enabled;
        if (!priority_trace_origin_family(input->schema) &&
            (!vdc_dpll_manager_try_priority_follow_phase_enabled(&phase_enabled) ||
             (input->schema == VDC_PRIORITY_TRACE_SCHEMA && phase_enabled) ||
             ((input->schema == VDC_PRIORITY_TRACE_PHASE_SCHEMA || input->schema == VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA) && !phase_enabled))) goto done;
        if (!input->capture_id || input->capture_id <= s_priority_trace_last_capture ||
            (owner == DPLL_CAPTURE_POOL_TYPED && status.state != VDC_PRIORITY_TRACE_FROZEN) ||
            !session || session != vdc_dpll_manager_feedback_session()) goto done;
        if (priority_trace_origin_family(input->schema)) {
            generation = vdc_dpll_manager_priority_sync_generation();
            if (!generation || session != vdc_dpll_manager_feedback_session() ||
                generation != vdc_dpll_manager_priority_sync_generation()) goto done;
        } else {
            generation = vdc_dpll_manager_priority_match_generation();
            if (!boundary_request(&request, &boundary_session, &delta, &mode) || !generation ||
                __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != session) goto done;
        }
        if (owner == DPLL_CAPTURE_POOL_LEGACY) {
            uint32_t expected = DPLL_CAPTURE_POOL_LEGACY;
            if (!__atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
                    DPLL_CAPTURE_POOL_TYPED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) goto done;
            reserved = true;
        }
        __atomic_store_n(&s_priority_trace_request.capture_id, input->capture_id, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.session, session, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.generation,
            generation, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.schema, input->schema, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.mode, mode, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.boundary_request, request, __ATOMIC_RELAXED);
        s_priority_trace_last_capture = input->capture_id;
    } else if (owner != DPLL_CAPTURE_POOL_TYPED) goto done;
    __atomic_store_n(&s_priority_trace_request.command, input->command, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_trace_request.sequence, previous + 1u, __ATOMIC_RELAXED);
    accepted = true;
done:
    if (reserved && !accepted)
        __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_LEGACY, __ATOMIC_RELEASE);
    __atomic_store_n(&s_priority_trace_request_guard, guard + 2u, __ATOMIC_RELEASE);
    return accepted;
}

static bool priority_trace_submit(uint32_t command, uint32_t capture_id, uint32_t schema)
{
    vdc_priority_trace_submit_t input = {command, capture_id, schema};
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_trace_submit_stopped, &input);
}
bool vdc_dpll_manager_priority_trace_arm(uint32_t capture_id)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, VDC_PRIORITY_TRACE_SCHEMA); }
bool vdc_dpll_manager_priority_trace_phase_arm(uint32_t capture_id)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, VDC_PRIORITY_TRACE_PHASE_SCHEMA); }
bool vdc_dpll_manager_priority_trace_origin_arm(uint32_t capture_id)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, VDC_PRIORITY_TRACE_ORIGIN_SCHEMA); }
bool vdc_dpll_manager_priority_trace_summary_arm(uint32_t capture_id, bool origin)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, origin ? VDC_PRIORITY_TRACE_SUMMARY_ORIGIN_SCHEMA : VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA); }
bool vdc_dpll_manager_priority_trace_stop(void)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_STOP, 0u, 0u); }
bool vdc_dpll_manager_priority_trace_release(void)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_RELEASE, 0u, 0u); }

static bool priority_trace_active(void)
{
    const uint32_t state = s_priority_trace_work.status.state;
    return state == VDC_PRIORITY_TRACE_ARMED || state == VDC_PRIORITY_TRACE_RUNNING;
}
static void priority_trace_freeze(uint32_t reason)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    if (!priority_trace_active()) return;
    priority_summary_terminal();
    s->state = VDC_PRIORITY_TRACE_FROZEN; s->reason = reason; s->freeze_ms = board_uptime_ms();
    priority_trace_publish();
}

#include "vdc_priority_trace_summary.inc"

static __attribute__((noinline)) void priority_trace_service_core1(void)
{
    if (get_core_num() != 1u ||
        __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE) != DPLL_CAPTURE_POOL_TYPED) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    vdc_priority_trace_status_t *s = &w->status;
    vdc_priority_trace_request_t command;
    if (priority_trace_request_copy(&command) &&
        command.sequence != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_RELAXED)) {
        if (command.command == VDC_PRIORITY_TRACE_COMMAND_ARM) {
            /* Same Core1 boundary follows every earlier legacy append. The
             * reservation never cancelled a live legacy capture on Core0. */
            const bool origin_invalid = priority_trace_origin_family(command.schema) &&
                !vdc_priority_tx_origin_trace_eligible_core1(command.generation, command.session);
            if (s_dpll_capture_armed || origin_invalid) {
                memset(w, 0, sizeof(*w));
                s->schema = command.schema; s->capture_id = command.capture_id;
                s->state = VDC_PRIORITY_TRACE_REJECTED; s->reason = origin_invalid ? VDC_PRIORITY_TRACE_BINDING : VDC_PRIORITY_TRACE_LEGACY_BUSY;
            } else {
                /* No pool memset: only the next committed record becomes
                 * visible, so ARM work does not scale with capture capacity. */
                /* The pool's old bytes will change format. Retire legacy
                 * completion metadata here, after its sole producer has
                 * quiesced, so RELEASE cannot export typed bytes as schema 6. */
                s_dpll_capture_complete = false;
                s_dpll_capture_count = s_dpll_capture_dropped = 0u;
                s_dpll_capture_first_update_seq = s_dpll_capture_last_update_seq = 0u;
                s_dpll_capture_start_ms = s_dpll_capture_end_ms = 0u;
                memset(w, 0, sizeof(*w));
                s->schema = command.schema; s->capture_id = command.capture_id;
                s->session = command.session; s->generation = command.generation; s->mode = command.mode;
                s->capacity = VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES;
                s->sample_interval_ms = priority_trace_summary_family(command.schema) ? VDC_PRIORITY_TRACE_SUMMARY_INTERVAL_MS : (priority_trace_origin_family(command.schema) ? 0u : VDC_PRIORITY_TRACE_MATCH_INTERVAL_MS);
                s->state = VDC_PRIORITY_TRACE_ARMED;
                w->boundary_request = command.boundary_request;
            }
        } else if (command.command == VDC_PRIORITY_TRACE_COMMAND_STOP) {
            priority_trace_freeze(VDC_PRIORITY_TRACE_STOP);
        } else if (command.command == VDC_PRIORITY_TRACE_COMMAND_RELEASE) {
            priority_trace_freeze(VDC_PRIORITY_TRACE_RELEASED);
            s->state = VDC_PRIORITY_TRACE_IDLE; s->reason = VDC_PRIORITY_TRACE_RELEASED;
        }
        s->request_seq = s->ack_seq = command.sequence; s->command = command.command;
        const bool release_pool = s->state == VDC_PRIORITY_TRACE_REJECTED ||
            command.command == VDC_PRIORITY_TRACE_COMMAND_RELEASE;
        /* No new command can enter between ACK publication and pool return.
         * The public getter withholds this ACK until return is complete. */
        if (release_pool)
            __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_RELEASING, __ATOMIC_RELEASE);
        priority_trace_publish();
        /* ACK release makes every record/status write precede Core0 reuse. */
        __atomic_store_n(&s_priority_trace_ack, command.sequence, __ATOMIC_RELEASE);
        if (release_pool)
            __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_LEGACY, __ATOMIC_RELEASE);
    }
    if (!priority_trace_active()) return;
    uint32_t request, session, mode;
    int32_t delta;
    if (priority_trace_origin_family(s->schema)) {
        if (vdc_dpll_manager_feedback_session() != s->session ||
            vdc_dpll_manager_priority_sync_generation() != s->generation) {
            priority_trace_freeze(VDC_PRIORITY_TRACE_SESSION); return;
        }
    } else if (vdc_dpll_manager_feedback_session() != s->session ||
        vdc_dpll_manager_priority_match_generation() != s->generation ||
        __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != s->session) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_SESSION); return;
    }
    if (!priority_trace_origin_family(s->schema) &&
        boundary_request(&request, &session, &delta, &mode) &&
        (request != w->boundary_request || mode != s->mode)) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_MODE); return;
    }
    tdma_ring_clock_snapshot_t *ring = &s_priority_match_work.ring_after;
    if (tdma_runtime_owner_get_ring_clock_snapshot(ring)) {
        if (ring->enabled && ring->adapter_started) { w->seen_running = 1u; priority_summary_service(false); }
        else if (w->seen_running || w->bound) priority_trace_freeze(VDC_PRIORITY_TRACE_STOP);
    }
}

static bool priority_trace_binding(const vdc_priority_match_snapshot_t *m)
{
    const vdc_priority_trace_work_t *w = &s_priority_trace_work;
    const vdc_priority_trace_status_t *s = &w->status;
    const uint64_t arm = ((uint64_t)s->arm_epoch_hi << 32u) | s->arm_epoch_lo;
    return m->session == s->session && m->generation == s->generation &&
        m->ring_config_seq == s->ring_config_seq && m->ring_applied_seq == s->ring_applied_seq &&
        m->local_slot == s->local_slot && m->reference_slot == s->reference_slot &&
        m->node_count == s->node_count && m->schedule_crc32 == s->schedule_crc32 &&
        m->profile_crc32 == s->profile_crc32 && m->path_crc32 == s->path_crc32 &&
        m->delay_ns == s->delay_ns && m->clock_epoch == s->clock_epoch && m->clock_run == s->clock_run &&
        m->arm_epoch == arm && m->observer_epoch == s->observer_epoch && m->rx_epoch == s->rx_epoch &&
        m->tick_hz == s->tick_hz && m->role_generation == w->role_generation &&
        s_vdc_domain.path_delay.table_crc32 == s->path_table_crc32;
}

static void priority_trace_bind(const vdc_priority_match_snapshot_t *m)
{
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    vdc_priority_trace_status_t *s = &w->status;
    s->ring_config_seq = m->ring_config_seq; s->ring_applied_seq = m->ring_applied_seq;
    s->local_slot = m->local_slot; s->reference_slot = m->reference_slot; s->node_count = m->node_count;
    s->schedule_crc32 = m->schedule_crc32; s->profile_crc32 = m->profile_crc32;
    s->path_table_crc32 = s_vdc_domain.path_delay.table_crc32;
    s->path_crc32 = m->path_crc32; s->delay_ns = m->delay_ns;
    s->clock_epoch = m->clock_epoch; s->clock_run = m->clock_run;
    s->arm_epoch_lo = (uint32_t)m->arm_epoch; s->arm_epoch_hi = (uint32_t)(m->arm_epoch >> 32u);
    s->observer_epoch = m->observer_epoch; s->rx_epoch = m->rx_epoch; s->tick_hz = m->tick_hz;
    w->role_generation = m->role_generation; w->bound = 1u; s->state = VDC_PRIORITY_TRACE_RUNNING;
}

static uint8_t *priority_trace_record_begin(uint32_t kind, uint32_t event, uint32_t carrier)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    if (!priority_trace_active() ||
        __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE) != DPLL_CAPTURE_POOL_TYPED) return NULL;
    if (s->record_count >= VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES) {
        s->dropped_count = priority_match_inc(s->dropped_count);
        priority_trace_freeze(VDC_PRIORITY_TRACE_FULL); return NULL;
    }
    uint8_t *record = (uint8_t *)&s_dpll_capture_records[s->record_count];
    memset(record, 0, VDC_PRIORITY_TRACE_RECORD_BYTES);
    boundary_capture_u32(record, s->record_count); boundary_capture_u32(record + 4u, kind);
    boundary_capture_u32(record + 8u, board_uptime_ms());
    boundary_capture_u32(record + 12u, event); boundary_capture_u32(record + 16u, carrier);
    return record;
}

static void priority_trace_record_commit(const uint8_t *record, bool decision)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    uint32_t now;
    memcpy(&now, record + 8u, 4u);
    if (!s->record_count) s->first_ms = now;
    s->last_ms = now; ++s->record_count;
    if (decision) s->decision_count = priority_match_inc(s->decision_count);
    else s->match_count = priority_match_inc(s->match_count);
    priority_trace_publish();
}

static __attribute__((noinline)) void priority_trace_match_core1(void)
{
    if (get_core_num() != 1u || !priority_trace_active() ||
        (s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_PHASE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA)) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    const vdc_priority_match_work_t *match = &s_priority_match_work;
    const vdc_priority_match_snapshot_t *m = &match->status;
    if (m->generation == w->status.generation && m->retired) {
        priority_trace_freeze(m->last_reason == VDC_PRIORITY_MATCH_STOP ?
            VDC_PRIORITY_TRACE_STOP : VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (!match->fresh) return;
    if (!m->active || m->retired || !m->have_match || m->last_reason != VDC_PRIORITY_MATCH_OK ||
        m->session != w->status.session || m->generation != w->status.generation) return;
    if (!w->bound) priority_trace_bind(m);
    else if (!priority_trace_binding(m)) { priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return; }
    if (w->have_match && m->event_sequence <= w->last_match_sequence) return;
    w->last_match_sequence = m->event_sequence;
    if (priority_trace_summary_family(w->status.schema)) {
        priority_summary_success(m->event_sequence,m->model_token,match->model.dco.period_adjust_ppb,
            m->residual_lo,m->residual_hi,(uint64_t)m->residual_hi-(uint64_t)m->residual_lo);
        w->have_match=1u; return;
    }
    const uint64_t period = (uint64_t)m->tick_hz * VDC_PRIORITY_TRACE_MATCH_INTERVAL_MS / 1000u;
    if (w->have_match && (m->raw_lo < w->last_sample_raw || m->raw_lo - w->last_sample_raw < period)) {
        w->status.skipped_count = priority_match_inc(w->status.skipped_count); return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_MATCH, m->event_sequence, m->carrier_sequence);
    if (!p) return;
    boundary_capture_u64(p + 20u, m->raw_lo); boundary_capture_u64(p + 28u, m->raw_hi);
    boundary_capture_u64(p + 36u, (uint64_t)m->residual_lo);
    boundary_capture_u64(p + 44u, (uint64_t)m->residual_hi);
    boundary_capture_u64(p + 52u, m->local_lo); boundary_capture_u64(p + 60u, m->local_hi);
    boundary_capture_u64(p + 68u, m->remote_lo); boundary_capture_u64(p + 76u, m->remote_hi);
    boundary_capture_u32(p + 84u, m->model_token);
    boundary_capture_u32(p + 88u, match->model.dco.dco_update_seq);
    boundary_capture_u32(p + 92u, (uint32_t)match->model.dco.period_adjust_ppb);
    boundary_capture_u32(p + 96u, m->last_reason);
    w->have_match = 1u; w->last_sample_raw = m->raw_lo;
    priority_trace_record_commit(p, false);
}

static void priority_trace_decision_core1(const vdc_priority_follow_snapshot_t *decision)
{
    if (get_core_num() != 1u || !priority_trace_active() || !decision ||
        (s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_PHASE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA)) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    if (!w->bound || decision->session != w->status.session || decision->generation != w->status.generation ||
        !priority_trace_binding(&s_priority_follow_work.ticket.match)) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (w->have_decision && decision->event_sequence <= w->last_decision_sequence) return;
    if (priority_trace_summary_family(w->status.schema)) {
        vdc_priority_summary_record_t *p=priority_summary_record();
        if (p) {
            p->decision_count=priority_summary_add16(p->decision_count,1u,p);
            if (decision->after_dco_seq!=decision->before_dco_seq)
                p->frequency_applied_count=priority_summary_add16(p->frequency_applied_count,1u,p);
        }
        w->have_decision=1u;w->last_decision_sequence=decision->event_sequence;return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_DECISION,
        decision->event_sequence, decision->carrier_sequence);
    if (!p) return;
    boundary_capture_u32(p + 20u, decision->baseline_sequence);
    boundary_capture_u32(p + 24u, decision->model_token);
    boundary_capture_u32(p + 28u, decision->before_dco_seq);
    boundary_capture_u32(p + 32u, decision->after_dco_seq);
    boundary_capture_u32(p + 36u, (uint32_t)decision->before_ppb);
    boundary_capture_u32(p + 40u, (uint32_t)decision->after_ppb);
    boundary_capture_u32(p + 44u, (uint32_t)decision->selected_delta_ppb);
    boundary_capture_u32(p + 48u, decision->last_reason);
    boundary_capture_u64(p + 52u, (uint64_t)decision->error_lo_ppb);
    boundary_capture_u64(p + 60u, (uint64_t)decision->error_hi_ppb);
    boundary_capture_u64(p + 68u, decision->interval_lo); boundary_capture_u64(p + 76u, decision->interval_hi);
    boundary_capture_u64(p + 84u, decision->local_interval_lo); boundary_capture_u64(p + 92u, decision->local_interval_hi);
    w->have_decision = 1u; w->last_decision_sequence = decision->event_sequence;
    priority_trace_record_commit(p, true);
}


/* A complete consecutive origin history, beginning at an acknowledged empty
 * sync generation. This hook runs only after successful encoding. */
static __attribute__((noinline)) void priority_trace_origin_core1(
    const vdc_priority_tx_origin_evidence_t *e)
{
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    vdc_priority_trace_status_t *s = &w->status;
    if (get_core_num() != 1u || !e || !priority_trace_active() ||
        !priority_trace_origin_family(s->schema)) return;
    if (e->session != s->session || e->generation != s->generation ||
        (w->bound && (e->role_generation != w->role_generation ||
            e->source_epoch != w->boundary_request || e->clock_epoch != s->clock_epoch ||
            e->clock_run != s->clock_run || e->tick_hz != s->tick_hz ||
            e->local_slot != s->local_slot || e->node_count != s->node_count ||
            e->schedule_crc32 != s->schedule_crc32 || e->profile_crc32 != s->profile_crc32))) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (!w->bound) {
        w->role_generation = e->role_generation; w->boundary_request = e->source_epoch;
        s->clock_epoch = e->clock_epoch; s->clock_run = e->clock_run; s->tick_hz = e->tick_hz;
        s->local_slot = e->local_slot; s->node_count = e->node_count;
        s->schedule_crc32 = e->schedule_crc32; s->profile_crc32 = e->profile_crc32;
        w->bound = 1u; s->state = VDC_PRIORITY_TRACE_RUNNING;
    } else if (e->event_sequence <= w->last_match_sequence) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (priority_trace_summary_family(s->schema)) {
        priority_summary_success(e->event_sequence,e->projection.model.token,
            e->projection.model.dco.period_adjust_ppb,0,0,e->encoded_width);
        w->last_match_sequence=e->event_sequence;return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_ORIGIN, 0u, 0u);
    if (!p) return;
    const vdc_dco_control_t *dco = &e->projection.model.dco;
    boundary_capture_u32(p + 8u, e->event_sequence);
    boundary_capture_u32(p + 12u, e->projection.model.token);
    boundary_capture_u32(p + 16u, e->tick_hz);
    boundary_capture_u64(p + 20u, e->source_raw_lo); boundary_capture_u64(p + 28u, e->source_raw_hi);
    boundary_capture_u64(p + 36u, e->projection.raw_now);
    boundary_capture_u64(p + 44u, e->projection.local_lo);
    boundary_capture_u64(p + 52u, e->projection.local_hi);
    boundary_capture_u64(p + 60u, dco->base_local_tick64);
    boundary_capture_u64(p + 68u, dco->base_vdc_time64_ns);
    boundary_capture_u32(p + 76u, (uint32_t)dco->period_adjust_ppb);
    boundary_capture_u32(p + 80u, (uint32_t)dco->phase_offset_ns);
    boundary_capture_u32(p + 84u, dco->dco_update_seq);
    boundary_capture_u64(p + 88u, e->encoded_output_lo);
    boundary_capture_u32(p + 96u, e->encoded_width);
    uint8_t *h = w->origin;
    if (!s->record_count) {
        boundary_capture_u32(h, e->role_generation); boundary_capture_u32(h + 4u, e->source_epoch);
        boundary_capture_u32(h + 8u, e->source_identity);
        boundary_capture_u32(h + 16u, e->published_version);
        s->first_ms = board_uptime_ms();
    }
    boundary_capture_u32(h + 12u, e->source_identity);
    boundary_capture_u32(h + 20u, e->published_version);
    boundary_capture_u32(h + 24u, e->event_sequence);
    w->last_match_sequence = e->event_sequence;
    s->last_ms = board_uptime_ms(); ++s->record_count;
    if (s->record_count == VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES)
        priority_trace_freeze(VDC_PRIORITY_TRACE_FULL);
    else priority_trace_publish();
}

/* Only a receipt-confirmed full model publication may produce PHASE. */
static void priority_trace_phase_core1(const vdc_priority_phase_snapshot_t *phase)
{
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    if (get_core_num() != 1u || !phase || !priority_trace_active()) return;
    if (w->status.schema == VDC_PRIORITY_TRACE_SCHEMA) {
        /* A STOP reconfiguration after trace ARM cannot silently reinterpret
         * legacy frequency records once a real phase translation occurs. */
        priority_trace_freeze(VDC_PRIORITY_TRACE_MODE); return;
    }
    if (w->status.schema != VDC_PRIORITY_TRACE_PHASE_SCHEMA &&
        w->status.schema != VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA) return;
    if (!w->bound || phase->session != w->status.session ||
        phase->generation != w->status.generation ||
        !priority_trace_binding(&s_priority_follow_work.ticket.match)) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (priority_trace_summary_family(w->status.schema)) {
        vdc_priority_summary_record_t *p=priority_summary_record();
        if (p) p->phase_applied_count=priority_summary_add16(p->phase_applied_count,1u,p);
        return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_PHASE,
        phase->event_sequence, phase->carrier_sequence);
    if (!p) return;
    boundary_capture_u32(p + 20u, phase->before_model);
    boundary_capture_u32(p + 24u, phase->after_model);
    boundary_capture_u32(p + 28u, phase->before_dco);
    boundary_capture_u32(p + 32u, phase->after_dco);
    boundary_capture_u32(p + 36u, phase->rate_epoch);
    boundary_capture_u32(p + 40u, phase->reason);
    boundary_capture_u64(p + 44u, (uint64_t)phase->delta_ns);
    boundary_capture_u64(p + 52u, (uint64_t)phase->cumulative_ns);
    boundary_capture_u64(p + 60u, (uint64_t)phase->residual_lo);
    boundary_capture_u64(p + 68u, (uint64_t)phase->residual_hi);
    boundary_capture_u64(p + 76u, phase->before_base_vdc);
    boundary_capture_u64(p + 84u, phase->after_base_vdc);
    boundary_capture_u64(p + 92u, phase->raw_lo);
    priority_trace_record_commit(p, true);
}

static bool priority_trace_read_frozen(uint32_t capture_id, uint32_t offset,
    uint8_t *data, uint32_t size, uint32_t *total_bytes, uint32_t *file_crc32)
{
    struct { vdc_priority_trace_header_t base; uint8_t origin[VDC_PRIORITY_TRACE_ORIGIN_EXTENSION_BYTES]; } file;
    vdc_priority_trace_header_t *header = &file.base;
    tdma_ring_clock_snapshot_t ring;
    if (!vdc_dpll_manager_get_priority_trace(&header->status) ||
        header->status.state != VDC_PRIORITY_TRACE_FROZEN || header->status.capture_id != capture_id ||
        !capture_id || header->status.request_seq != header->status.ack_seq ||
        header->status.ack_seq != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) ||
        header->status.record_count > VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled || ring.adapter_started ||
        ring.config_seq != ring.applied_config_seq) return false;
    const uint32_t config = ring.config_seq;
    const uint32_t header_size = sizeof(*header) + (header->status.schema == VDC_PRIORITY_TRACE_ORIGIN_SCHEMA ? sizeof(file.origin) : 0u);
    if (header->status.schema == VDC_PRIORITY_TRACE_ORIGIN_SCHEMA)
        memcpy(file.origin, s_priority_trace_work.origin, sizeof(file.origin));
    const uint32_t bytes = header->status.record_count * VDC_PRIORITY_TRACE_RECORD_BYTES;
    const uint32_t total = header_size + bytes;
    if (offset > total || size > total - offset) return false;
    header->magic = VDC_PRIORITY_TRACE_MAGIC; header->schema = header->status.schema;
    header->header_size = header_size; header->record_size = VDC_PRIORITY_TRACE_RECORD_BYTES;
    header->payload_crc32 = ota_crc32_compute((const uint8_t *)s_dpll_capture_records, bytes);
    uint32_t crc = ota_crc32_update(0u, (const uint8_t *)&file, header_size);
    crc = ota_crc32_update(crc, (const uint8_t *)s_dpll_capture_records, bytes);
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled || ring.adapter_started ||
        ring.config_seq != config || ring.applied_config_seq != config) return false;
    uint32_t copied = 0u;
    if (offset < header_size) {
        copied = header_size - offset;
        if (copied > size) copied = size;
        memcpy(data, (const uint8_t *)&file + offset, copied);
    }
    if (copied < size) memcpy(data + copied, (const uint8_t *)s_dpll_capture_records +
        offset + copied - header_size, size - copied);
    *total_bytes = total; *file_crc32 = crc; return true;
}

bool vdc_dpll_manager_priority_trace_read(uint32_t capture_id, uint32_t offset,
    uint8_t *data, uint32_t size, uint32_t *total_bytes, uint32_t *file_crc32)
{
    if (get_core_num() != 0u || !data || !total_bytes || !file_crc32 || !size ||
        size > VDC_PRIORITY_TRACE_READ_MAX_BYTES) return false;
    /* Exclude a submission while proving no command is in flight. Taking a
     * READ lease from RUNNING/pending state could otherwise cross a Core1
     * command that already passed its owner check and is about to RELEASE. */
    uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) || guard > UINT32_MAX - 2u ||
        !__atomic_compare_exchange_n(&s_priority_trace_request_guard, &guard, guard + 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    vdc_priority_trace_status_t status;
    uint32_t expected = DPLL_CAPTURE_POOL_TYPED;
    const bool leased = priority_trace_status_copy(&status) &&
        status.state == VDC_PRIORITY_TRACE_FROZEN && status.capture_id == capture_id &&
        status.ack_seq == __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) &&
        status.ack_seq == __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED) &&
        __atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
            DPLL_CAPTURE_POOL_TYPED_READ, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    __atomic_store_n(&s_priority_trace_request_guard, guard + 2u, __ATOMIC_RELEASE);
    if (!leased) return false;
    const bool result = priority_trace_read_frozen(capture_id, offset, data, size, total_bytes, file_crc32);
    expected = DPLL_CAPTURE_POOL_TYPED_READ;
    return __atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
        DPLL_CAPTURE_POOL_TYPED, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED) && result;
}
