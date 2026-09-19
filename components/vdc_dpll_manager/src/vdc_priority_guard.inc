/* Opt-in diagnostic supervision. Core1 alone classifies observations; Core0
 * requests local retirement. No live record export or realtime host traffic. */
static vdc_priority_guard_status_t s_priority_guard_work;
static uint64_t s_priority_guard_start;
static uint32_t s_priority_guard_started, s_priority_guard_sequence;
static uint32_t s_priority_guard_words[sizeof(vdc_priority_guard_status_t)/4u];
/* Core0-only bookkeeping, always matched to capture identity. */
static uint32_t s_priority_guard_stop_capture, s_priority_guard_stop_config;
static uint32_t s_priority_guard_ring_retired, s_priority_guard_output_retired;

static void priority_guard_output(uint64_t now, bool initial)
{
    vdc_priority_guard_status_t *g=&s_priority_guard_work;
    vdc_run_output_observation_t o;
    if (!vdc_run_output_observe_core1(&o)) {
        g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_READ;
        return;
    }
    const uint64_t prior=((uint64_t)g->output_ordinal_hi<<32u)|g->output_ordinal_lo;
    g->output_state=o.state; g->output_reason=o.reason;
    g->output_ordinal_lo=(uint32_t)o.last_ordinal;
    g->output_ordinal_hi=(uint32_t)(o.last_ordinal>>32u);
    if (initial) g->output_request=o.request;
    if (!o.request || o.request!=g->output_request || o.session!=g->session ||
        (!initial && o.ring_config!=g->ring_config_seq))
        g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_IDENTITY;
    /* The first observation may precede first-block admission. Minute
     * checkpoints require running output, a fresh owner and real admission
     * progress. Never drive/service PIO from the diagnostic observer. */
    if (initial) return;
    if (o.state!=SYNC_IO_RUN_OUTPUT_RUNNING || o.reason!=SYNC_IO_RUN_OUTPUT_OK)
        g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_STOPPED;
    if (!o.service_observations || o.service_last_tick>now || o.submit_last_tick>now ||
        now-o.service_last_tick>BOARD_SYS_CLOCK_HZ || now-o.submit_last_tick>BOARD_SYS_CLOCK_HZ)
        g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_STALE;
    if (o.last_ordinal<=prior) g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_PROGRESS;
}

static void priority_guard_publish(void)
{
    const uint32_t seq = __atomic_load_n(&s_priority_guard_sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_guard_sequence, seq+1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    boundary_store(s_priority_guard_words, &s_priority_guard_work, sizeof(s_priority_guard_work));
    __atomic_store_n(&s_priority_guard_sequence, seq+2u, __ATOMIC_RELEASE);
}

static bool priority_guard_copy(vdc_priority_guard_status_t *out)
{
    const uint32_t seq = __atomic_load_n(&s_priority_guard_sequence, __ATOMIC_ACQUIRE);
    if (seq & 1u) return false;
    match_load_words(s_priority_guard_words, out, sizeof(*out));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return seq == __atomic_load_n(&s_priority_guard_sequence, __ATOMIC_ACQUIRE);
}

static void priority_guard_reset_core1(uint32_t duration_s)
{
    const vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    s_priority_guard_work = (vdc_priority_guard_status_t){
        .schema=VDC_PRIORITY_GUARD_SCHEMA, .capture_id=s->capture_id,
        .session=s->session, .generation=s->generation, .target_s=duration_s,
        .state=duration_s ? VDC_PRIORITY_GUARD_ARMED : VDC_PRIORITY_GUARD_DISABLED};
    s_priority_guard_start=0u; s_priority_guard_started=0u;
    priority_guard_publish();
}

static void priority_guard_note_bin(uint32_t flags, uint32_t success_gap, bool nonempty)
{
    vdc_priority_guard_status_t *g=&s_priority_guard_work;
    if (!g->target_s || g->state>=VDC_PRIORITY_GUARD_PASS) return;
    /* A zero-length terminal record claims no new coverage. Existing clock,
     * service, saturation and counter failures remain failures even there. */
    uint32_t reasons=flags & (SUMMARY_SERVICE_GAP|SUMMARY_COUNTER_RESET|
        SUMMARY_COUNTER_SATURATED|SUMMARY_FIELD_SATURATED|SUMMARY_CLOCK_INVALID);
    if (nonempty) reasons |= flags & (SUMMARY_NO_SUCCESS|SUMMARY_UNBOUND);
    if (success_gap>BOARD_SYS_CLOCK_HZ) reasons |= VDC_PRIORITY_GUARD_SUCCESS_GAP;
    g->reason_mask |= reasons;
}

static void priority_guard_ring_core1(uint32_t config_seq)
{
    /* Called exactly once before trace seen_running; config zero is valid. */
    if (s_priority_guard_work.state==VDC_PRIORITY_GUARD_ARMED) {
        s_priority_guard_work.ring_config_seq=config_seq;
        priority_guard_publish();
    }
}

static void priority_guard_service_core1(void)
{
    vdc_priority_guard_status_t *g=&s_priority_guard_work;
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    if (get_core_num()!=1u || !g->target_s || g->state>=VDC_PRIORITY_GUARD_PASS) return;
    if (!w->seen_running) return; /* ARM alone is not elapsed run time. */
    if (!s_priority_guard_started) {
        /* First bin may already be frozen by a startup failure. */
        const vdc_priority_summary_record_t *first=(const void *)&s_dpll_capture_records[0];
        if (!w->summary.open && !w->status.record_count) {
            g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
            g->state=VDC_PRIORITY_GUARD_FAIL; priority_guard_publish(); return;
        }
        s_priority_guard_start=first->observed_start_raw;
        s_priority_guard_started=1u; g->state=VDC_PRIORITY_GUARD_RUNNING;
        priority_guard_output(s_priority_guard_start,true);
        priority_guard_publish();
    }
    tdma_ring_clock_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) {
        g->reason_mask |= VDC_PRIORITY_GUARD_RING_READ;
    } else if (ring.config_seq!=g->ring_config_seq) {
        g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
    }
    uint64_t now;
    if (!priority_summary_now(&now) || now<s_priority_guard_start) {
        g->reason_mask |= SUMMARY_CLOCK_INVALID;
        g->state=VDC_PRIORITY_GUARD_FAIL;
    } else {
        const uint64_t ms=(now-s_priority_guard_start)/(BOARD_SYS_CLOCK_HZ/1000u);
        g->elapsed_ms=ms>UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
        if (w->status.state==VDC_PRIORITY_TRACE_FROZEN) {
            g->reason_mask |= VDC_PRIORITY_GUARD_EARLY_FREEZE;
            g->state=VDC_PRIORITY_GUARD_FAIL;
        } else if (g->elapsed_ms >= (g->checked_s+VDC_PRIORITY_GUARD_CHECKPOINT_S)*1000u) {
            priority_guard_output(now,false);
            /* Include the open partial bin: bin closure is not a minute
             * boundary and cannot hide a current gap or missing input. */
            vdc_priority_summary_record_t *p=priority_summary_record();
            if (p) priority_guard_note_bin(p->flags | (!p->success_count ? SUMMARY_NO_SUCCESS : 0u) |
                (!w->bound ? SUMMARY_UNBOUND : 0u), p->max_success_gap_ticks,
                p->observed_end_raw>p->observed_start_raw);
            else g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
            if (g->elapsed_ms >= (g->checked_s+2u*VDC_PRIORITY_GUARD_CHECKPOINT_S)*1000u)
                g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
            g->checked_s += VDC_PRIORITY_GUARD_CHECKPOINT_S;
            if (g->reason_mask) g->state=VDC_PRIORITY_GUARD_FAIL;
            else {
                g->passed_mask |= 1u << (g->checked_s/VDC_PRIORITY_GUARD_CHECKPOINT_S-1u);
                if (g->checked_s>=g->target_s) g->state=VDC_PRIORITY_GUARD_PASS;
            }
            if (g->state==VDC_PRIORITY_GUARD_FAIL) g->first_failure_ms=g->elapsed_ms;
            priority_guard_publish();
        }
    }
    if (g->state==VDC_PRIORITY_GUARD_FAIL) {
        if (!g->first_failure_ms) g->first_failure_ms=g->elapsed_ms;
        priority_guard_publish();
    }
}

/* Called inside the TDMA control guard: no new ARM can slip between identity
 * validation, output cancellation and the ring disable publication. */
static bool priority_guard_cancel_current(void *context)
{
    const vdc_priority_guard_status_t *g=context;
    vdc_priority_trace_status_t trace;
    if (!vdc_dpll_manager_get_priority_trace(&trace) || trace.request_seq!=trace.ack_seq ||
        trace.capture_id!=g->capture_id || trace.session!=g->session ||
        trace.generation!=g->generation || vdc_dpll_manager_feedback_session()!=g->session) return false;
    vdc_run_output_cancel();
    return true;
}

static void priority_guard_service_core0(void)
{
    vdc_priority_guard_status_t g;
    if (get_core_num()!=0u || !s_vdc_tdma_service || !priority_guard_copy(&g) ||
        g.state!=VDC_PRIORITY_GUARD_FAIL) return;
    if (g.capture_id==s_priority_guard_stop_capture) {
        /* Retain proof that this exact STOP retired, even after an ordinary
         * cleanup STOP advances config again. Never borrow a later run. */
        tdma_ring_clock_snapshot_t ring;
        if (tdma_runtime_owner_get_ring_clock_snapshot(&ring) &&
            ring.config_seq==s_priority_guard_stop_config && !ring.enabled) {
            if (!ring.adapter_started && ring.applied_config_seq==ring.config_seq)
                s_priority_guard_ring_retired=1u;
            if (vdc_run_output_configuration_idle()) s_priority_guard_output_retired=1u;
        }
        return;
    }
    uint32_t stopped=0u;
    if (tdma_service_ring_stop_if_current(s_vdc_tdma_service, g.ring_config_seq,
            priority_guard_cancel_current, &g, &stopped)) {
        s_priority_guard_stop_config=stopped;
        s_priority_guard_stop_capture=g.capture_id;
        s_priority_guard_ring_retired=s_priority_guard_output_retired=0u;
    }
}

bool vdc_dpll_manager_get_priority_guard(vdc_priority_guard_status_t *out)
{
    if (get_core_num()!=0u || !out) return false;
    vdc_priority_guard_status_t g;
    vdc_priority_trace_status_t trace;
    if (!priority_guard_copy(&g) || !vdc_dpll_manager_get_priority_trace(&trace) ||
        trace.request_seq!=trace.ack_seq || trace.capture_id!=g.capture_id ||
        trace.session!=g.session || trace.generation!=g.generation) return false;
    if (g.capture_id && g.capture_id==s_priority_guard_stop_capture) {
        g.stop_accepted=1u; g.stop_config_seq=s_priority_guard_stop_config;
        g.ring_retired=s_priority_guard_ring_retired;
        g.output_retired=s_priority_guard_output_retired;
        tdma_ring_clock_snapshot_t ring;
        if (tdma_runtime_owner_get_ring_clock_snapshot(&ring) &&
            ring.config_seq==g.stop_config_seq && !ring.enabled) {
            g.ring_retired|=!ring.adapter_started && ring.applied_config_seq==ring.config_seq;
            g.output_retired|=vdc_run_output_configuration_idle();
        }
    }
    *out=g; return true;
}
