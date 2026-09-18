/* Included after priority_follow_current(), under the same Core1 owner.
 * Successful Domain commits remain provisional until final publication
 * proves exactly the model that this owner committed. */
#ifndef VDC_PRIORITY_TRACE_PHASE_HOOK
#define VDC_PRIORITY_TRACE_PHASE_HOOK(snapshot) ((void)(snapshot))
#endif

typedef struct {
    vdc_priority_phase_snapshot_t status;
    vdc_dco_control_t expected_dco;
    int64_t next_cumulative;
    uint32_t known_model, initialized, pending, receipt_pending;
    uint32_t have_attempt, last_attempt_ms, consumed_sequence;
} vdc_priority_phase_work_t;
static vdc_priority_phase_work_t s_priority_phase_work;
static uint32_t s_priority_phase_request;
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
    __atomic_store_n(&s_priority_phase_request, *(const uint32_t *)context, __ATOMIC_RELEASE);
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
    if (lo > hi || (lo <= 0 && hi >= 0)) return 0;
    if (lo > 0) return lo > VDC_PRIORITY_PHASE_MAX_DELTA_NS
        ? -VDC_PRIORITY_PHASE_MAX_DELTA_NS : -lo;
    /* Compare before negating: INT64_MIN is legal input. */
    return hi < -VDC_PRIORITY_PHASE_MAX_DELTA_NS
        ? VDC_PRIORITY_PHASE_MAX_DELTA_NS : -hi;
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

static void phase_model_committed_core1(const vdc_dpll_manager_committed_model_t *model)
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
