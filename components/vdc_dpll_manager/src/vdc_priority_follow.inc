/* Sole Core1 owner. Preparation uses one fresh private matcher ticket before
 * the model writer guard; application revalidates actual owner state inside
 * that guard. Atomic-word publication is STOP diagnostics, never authority. */
#ifndef VDC_PRIORITY_TRACE_DECISION_HOOK
#define VDC_PRIORITY_TRACE_DECISION_HOOK(snapshot) ((void)(snapshot))
#endif
typedef struct {
    uint64_t local_lo, local_hi, expected_lo, expected_hi, raw_lo;
    uint32_t sequence, model_token;
} vdc_priority_follow_sample_t;
typedef struct {
    vdc_priority_match_snapshot_t match;
    vdc_dpll_manager_committed_model_t model;
    uint64_t anchor_before, anchor_after;
    uint32_t path_table_crc, servo_crc, prepared_ms;
} vdc_priority_follow_ticket_t;
typedef struct {
    vdc_priority_follow_snapshot_t status;
    vdc_priority_follow_sample_t previous;
    vdc_priority_follow_ticket_t ticket;
    uint64_t local_delta_lo, local_delta_hi, expected_delta_lo, expected_delta_hi;
    int64_t error_lo, error_hi;
    uint32_t have_baseline, pending, bound, have_consumed, consumed_sequence;
    uint32_t next_evaluation; /* Private next threshold index; never refunded. */
} vdc_priority_follow_work_t;
static vdc_priority_follow_work_t s_priority_follow_work;
static uint32_t s_priority_follow_guard;
static uint32_t s_priority_follow_words[sizeof(vdc_priority_follow_snapshot_t) / 4u];
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
    vdc_priority_follow_work_t *w = &s_priority_follow_work;
    if (w->pending || w->have_baseline) w->status.cancelled = priority_match_inc(w->status.cancelled);
    w->pending = w->have_baseline = w->next_evaluation = 0u;
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
        .expected_lo = m->expected_lo, .expected_hi = m->expected_hi,
        .raw_lo = m->raw_lo, .sequence = m->event_sequence, .model_token = m->model_token};
    w->have_baseline = 1u;
    w->next_evaluation = 0u;
    w->status.baselines = priority_match_inc(w->status.baselines);
    w->status.state = VDC_PRIORITY_FOLLOW_BASELINE;
}

static __attribute__((noinline)) void priority_follow_prepare_core1(void)
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
    if (w->pending) s->cancelled = priority_match_inc(s->cancelled);
    w->pending = 0u;
    w->ticket.match = *m;
    w->ticket.model = match->model;
    w->ticket.anchor_before = match->live.timer1_enable_before;
    w->ticket.anchor_after = match->live.timer1_enable_after;
    w->ticket.path_table_crc = s_vdc_domain.path_delay.table_crc32;
    w->ticket.servo_crc = s_vdc_domain.servo.servo_profile_crc32;
    w->ticket.prepared_ms = board_uptime_ms();
    w->bound = 1u;
    s->tickets = priority_match_inc(s->tickets);
    if (!w->have_baseline || w->previous.model_token != m->model_token ||
        w->previous.raw_lo < w->ticket.model.valid_from_raw ||
        m->raw_lo < w->ticket.model.valid_from_raw) {
        priority_follow_baseline(); s->last_reason = VDC_PRIORITY_FOLLOW_MODEL;
        priority_follow_publish(); return;
    }
    const vdc_priority_follow_sample_t *p = &w->previous;
    if (m->local_lo <= p->local_hi || m->expected_lo <= p->expected_hi ||
        m->local_hi < m->local_lo || m->expected_hi < m->expected_lo ||
        m->raw_lo <= p->raw_lo) {
        priority_follow_baseline(); s->last_reason = VDC_PRIORITY_FOLLOW_INTERVAL;
        priority_follow_publish(); return;
    }
    w->local_delta_lo = m->local_lo - p->local_hi;
    w->local_delta_hi = m->local_hi - p->local_lo;
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
     * remains separate. Intersect bounds on the SAME event output difference.
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
    if (!s_vdc_domain.ready || !s_vdc_domain.clock.valid || !s_vdc_domain.servo.enabled ||
        t->servo_crc != s_vdc_domain.servo.servo_profile_crc32 ||
        !t->model.token || t->model.token == UINT32_MAX || m->raw_lo < t->model.valid_from_raw ||
        memcmp(&t->model, &scratch->model_after, sizeof(t->model)) != 0 ||
        memcmp(&t->model.dco, &s_vdc_domain.dco, sizeof(t->model.dco)) != 0 ||
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

static __attribute__((noinline)) void priority_follow_apply_core1(void)
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
    const uint32_t current = priority_follow_current();
    if (current != VDC_PRIORITY_FOLLOW_OK) {
        if (current != VDC_PRIORITY_FOLLOW_BUSY)
            priority_follow_cancel(current, current == VDC_PRIORITY_FOLLOW_BINDING ||
                current == VDC_PRIORITY_FOLLOW_STOP);
        else s->last_reason = current;
        priority_follow_publish(); return;
    }
    if (!w->pending) return;
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
            w->have_baseline = w->next_evaluation = 0u;
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
}
