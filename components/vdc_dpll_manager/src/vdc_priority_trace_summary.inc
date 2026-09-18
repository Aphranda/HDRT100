/* Core1-only diagnostics. The current unpublished shared-pool slot is the
 * accumulator; committing a bin makes that slot immutable. No second ring. */
enum {
    SUMMARY_PARTIAL=1u, SUMMARY_NO_SUCCESS=2u, SUMMARY_SERVICE_GAP=4u,
    SUMMARY_COUNTER_RESET=8u, SUMMARY_COUNTER_SATURATED=16u,
    SUMMARY_FIELD_SATURATED=32u, SUMMARY_CLOCK_INVALID=64u,
    SUMMARY_UNBOUND=128u, SUMMARY_TERMINAL=256u
};
typedef struct __attribute__((packed, may_alias)) {
    uint16_t bin_index, flags;
    uint64_t observed_start_raw, observed_end_raw;
    uint32_t max_service_gap_ticks, max_success_gap_ticks;
    uint32_t first_event, last_event, first_success_offset_ticks, last_success_offset_ticks;
    uint16_t service_count, success_count, rejected_count, cancelled_count;
    uint16_t phase_held_count, decision_count, frequency_applied_count, phase_applied_count;
    int64_t residual_min_ns, residual_max_ns;
    uint32_t max_width_ns, first_model, last_model;
    int32_t min_ppb, max_ppb;
    uint16_t model_changes, outcome_mask;
} vdc_priority_summary_record_t;
_Static_assert(sizeof(vdc_priority_summary_record_t)==VDC_PRIORITY_TRACE_RECORD_BYTES,
    "summary native ABI is exactly one legacy slot");
_Static_assert(__builtin_offsetof(vdc_priority_summary_record_t,residual_min_ns)==60u,
    "summary residual ABI offset");

static vdc_priority_summary_record_t *priority_summary_record(void)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    if (!priority_trace_summary_family(w->status.schema) || !priority_trace_active() ||
        !w->summary.open || w->status.record_count>=w->status.capacity) return NULL;
    return (vdc_priority_summary_record_t *)&s_dpll_capture_records[w->status.record_count];
}
static uint16_t priority_summary_add16(uint16_t a, uint32_t b, vdc_priority_summary_record_t *p)
{
    if (b>(uint32_t)UINT16_MAX-a) {p->flags|=SUMMARY_FIELD_SATURATED;return UINT16_MAX;}
    return (uint16_t)(a+b);
}
static uint32_t priority_summary_u32(uint64_t value, vdc_priority_summary_record_t *p)
{
    if (value>UINT32_MAX) {p->flags|=SUMMARY_FIELD_SATURATED;return UINT32_MAX;}
    return (uint32_t)value;
}
static void priority_summary_counters(bool baseline)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!p) return;
    vdc_priority_tx_snapshot_t tx={0};
    if (priority_trace_origin_family(w->status.schema) &&
        !vdc_dpll_manager_get_priority_tx(&tx)) {
        /* The service observation was incomplete. Preserve the last actual
         * counter value; a failed snapshot is neither zero nor a reset. */
        p->flags|=SUMMARY_SERVICE_GAP;return;
    }
    if (priority_trace_origin_family(w->status.schema) &&
        tx.generation!=w->status.generation) {
        /* START can open the first bin before the new TX owner is called.
         * Its published status still belongs to the preceding generation;
         * do not adopt those counters as the new capture's baseline. Once
         * the current owner has been observed, a mismatch is a real gap. */
        if (w->summary.counter_valid_mask & (1u<<6))
            p->flags|=SUMMARY_SERVICE_GAP;
        return;
    }
    const uint32_t values[7]={s_priority_match_work.status.rejected,
        s_priority_follow_work.status.rejected,s_priority_follow_work.status.cancelled,
        s_priority_phase_work.status.rejected,s_priority_phase_work.status.cancelled,
        s_priority_phase_work.status.held,tx.rejected};
    const uint16_t masks[7]={1u,2u,4u,8u,16u,0u,32u};
    for (unsigned i=0;i<7u;++i) {
        if ((priority_trace_origin_family(w->status.schema) && i!=6u) ||
            (!priority_trace_origin_family(w->status.schema) && i==6u)) continue;
        uint32_t old=w->summary.counters[i];
        const uint32_t value=values[i];
        w->summary.counters[i]=value;
        const bool known=(w->summary.counter_valid_mask & (1u<<i))!=0u;
        w->summary.counter_valid_mask|=1u<<i;
        if (baseline || !known) {
            if (baseline) continue;
            if (i!=6u) {p->flags|=SUMMARY_SERVICE_GAP;continue;}
            /* TX initializes the current generation from zero. Include its
             * first rejects, even when its first published snapshot is not
             * observed until after several provider calls. A failed read
             * above already records SERVICE_GAP and is never forgiven. */
            old=0u;
        }
        if (value==UINT32_MAX || old==UINT32_MAX) p->flags|=SUMMARY_COUNTER_SATURATED;
        if (value<old) p->flags|=SUMMARY_COUNTER_RESET;
        const uint32_t delta=value>=old?value-old:value;
        if (!delta) continue;
        p->outcome_mask|=masks[i];
        if (i==2u || i==4u) p->cancelled_count=priority_summary_add16(p->cancelled_count,delta,p);
        else if (i==5u) p->phase_held_count=priority_summary_add16(p->phase_held_count,delta,p);
        else p->rejected_count=priority_summary_add16(p->rejected_count,delta,p);
    }
}
static bool priority_summary_now(uint64_t *raw)
{
    return vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,raw);
}
static bool priority_summary_observe(uint64_t raw)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!p) return false;
    if (raw<p->observed_end_raw) {p->flags|=SUMMARY_CLOCK_INVALID;return false;}
    p->observed_end_raw=raw;w->summary.last_ms=board_uptime_ms();
    const uint64_t since=w->summary.last_success;
    const uint32_t gap=priority_summary_u32(raw-since,p);
    if (gap>p->max_success_gap_ticks) p->max_success_gap_ticks=gap;
    return true;
}
static void priority_summary_commit(bool terminal)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    vdc_priority_trace_status_t *s=&w->status;
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!p) return;
    if (terminal) p->flags|=SUMMARY_PARTIAL|SUMMARY_TERMINAL;
    if (!p->success_count) p->flags|=SUMMARY_NO_SUCCESS;
    if (!w->bound) p->flags|=SUMMARY_UNBOUND;
    priority_guard_note_bin(p->flags, p->max_success_gap_ticks,
        p->observed_end_raw > p->observed_start_raw);
    if (!s->record_count) s->first_ms=w->summary.first_ms;
    s->last_ms=w->summary.last_ms;
    s->match_count=UINT32_MAX-s->match_count<p->success_count?UINT32_MAX:s->match_count+p->success_count;
    s->decision_count=UINT32_MAX-s->decision_count<p->decision_count?UINT32_MAX:s->decision_count+p->decision_count;
    ++s->record_count;w->summary.open=0u;
    if (s->record_count==s->capacity && !terminal) {
        s->state=VDC_PRIORITY_TRACE_FROZEN;s->reason=VDC_PRIORITY_TRACE_FULL;s->freeze_ms=board_uptime_ms();
    }
    priority_trace_publish();
}
static void priority_summary_terminal(void)
{
    if (get_core_num()!=1u ||
        __atomic_load_n(&s_dpll_capture_pool_owner,__ATOMIC_ACQUIRE)!=DPLL_CAPTURE_POOL_TYPED ||
        !priority_trace_summary_family(s_priority_trace_work.status.schema)) return;
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!p) return;
    uint64_t raw;
    if (s_priority_trace_work.summary.clock_epoch!=s_vdc_domain.clock.epoch_id ||
        s_priority_trace_work.summary.clock_run!=s_vdc_domain.clock.run_id) {
        p->flags|=SUMMARY_CLOCK_INVALID;
    } else if (priority_summary_now(&raw)) {
        const bool ordered=priority_summary_observe(raw);
        if (ordered && raw>=s_priority_trace_work.summary.last_service) {
            const uint64_t gap=raw-s_priority_trace_work.summary.last_service;
            const uint32_t g=priority_summary_u32(gap,p);
            if (g>p->max_service_gap_ticks) p->max_service_gap_ticks=g;
            if (gap>=BOARD_SYS_CLOCK_HZ) p->flags|=SUMMARY_SERVICE_GAP;
        }
    }
    else p->flags|=SUMMARY_CLOCK_INVALID;
    priority_summary_counters(false);priority_summary_commit(true);
}
static void priority_summary_service(bool end)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    if (get_core_num()!=1u ||
        __atomic_load_n(&s_dpll_capture_pool_owner,__ATOMIC_ACQUIRE)!=DPLL_CAPTURE_POOL_TYPED ||
        !priority_trace_summary_family(w->status.schema) || !priority_trace_active() || !w->seen_running) return;
    if (w->summary.open && (w->summary.clock_epoch!=s_vdc_domain.clock.epoch_id ||
        w->summary.clock_run!=s_vdc_domain.clock.run_id)) {
        priority_summary_record()->flags|=SUMMARY_CLOCK_INVALID;
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING);return;
    }
    uint64_t raw=0u;
    const bool valid=priority_summary_now(&raw);
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!valid || (p && !priority_summary_observe(raw))) {
        if (p) p->flags|=SUMMARY_CLOCK_INVALID;
        /* No bin may open without an actual clock observation. On rollback,
         * retain the last ordered observation and freeze before rollover;
         * never splice a new lower start onto the previous larger end. */
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING);return;
    }
    if (!end && p && valid && raw>=p->observed_start_raw &&
        raw-p->observed_start_raw>=w->summary.interval_ticks) {
        const uint64_t gap=raw>=w->summary.last_service?raw-w->summary.last_service:0u;
        const uint32_t g=priority_summary_u32(gap,p);
        if (g>p->max_service_gap_ticks) p->max_service_gap_ticks=g;
        if (gap>=BOARD_SYS_CLOCK_HZ || raw-p->observed_start_raw>=w->summary.interval_ticks+BOARD_SYS_CLOCK_HZ)
            p->flags|=SUMMARY_SERVICE_GAP;
        priority_summary_counters(false);priority_summary_commit(false);p=NULL;
    }
    if (!priority_trace_active()) return;
    if (!p) {
        if (end) return;
        w->summary.open=1u;
        p=priority_summary_record();if (!p) return;
        memset(p,0,sizeof(*p));p->bin_index=(uint16_t)w->status.record_count;
        p->observed_start_raw=p->observed_end_raw=valid?raw:w->summary.last_service;
        w->summary.first_ms=w->summary.last_ms=board_uptime_ms();
        if (!w->status.record_count) {
            w->summary.last_service=w->summary.last_success=p->observed_start_raw;
            w->summary.clock_epoch=s_vdc_domain.clock.epoch_id;
            w->summary.clock_run=s_vdc_domain.clock.run_id;
            priority_summary_counters(true);
        }
        w->status.tick_hz=BOARD_SYS_CLOCK_HZ;
        w->status.state=VDC_PRIORITY_TRACE_RUNNING;
    }
    if (!valid) p->flags|=SUMMARY_CLOCK_INVALID;
    if (end) {priority_summary_counters(false);return;}
    if (valid && raw>=w->summary.last_service) {
        const uint64_t gap=raw-w->summary.last_service;
        const uint32_t g=priority_summary_u32(gap,p);
        if (g>p->max_service_gap_ticks) p->max_service_gap_ticks=g;
        if (gap>=BOARD_SYS_CLOCK_HZ) p->flags|=SUMMARY_SERVICE_GAP;
        w->summary.last_service=raw;
    } else if (valid) p->flags|=SUMMARY_CLOCK_INVALID;
    p->service_count=priority_summary_add16(p->service_count,1u,p);
}
static void priority_summary_success(uint32_t event,uint32_t model,int32_t ppb,
    int64_t lo,int64_t hi,uint64_t width)
{
    vdc_priority_summary_record_t *p=priority_summary_record();
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    if (!p) return;
    uint64_t raw;
    if (!priority_summary_now(&raw) || raw<p->observed_start_raw || raw<p->observed_end_raw) {
        p->flags|=SUMMARY_CLOCK_INVALID;
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING);return;
    }
    priority_summary_observe(raw);
    const uint32_t offset=priority_summary_u32(raw-p->observed_start_raw,p);
    if (!p->success_count) {
        p->first_event=event;p->first_model=model;p->first_success_offset_ticks=offset;
        p->residual_min_ns=lo;p->residual_max_ns=hi;p->min_ppb=p->max_ppb=ppb;
    } else {
        if (lo<p->residual_min_ns) p->residual_min_ns=lo;
        if (hi>p->residual_max_ns) p->residual_max_ns=hi;
        if (ppb<p->min_ppb) p->min_ppb=ppb;
        if (ppb>p->max_ppb) p->max_ppb=ppb;

    }
    if (w->summary.have_success && model!=w->summary.last_model)
        p->model_changes=priority_summary_add16(p->model_changes,1u,p);
    w->summary.last_model=model;
    p->last_event=event;p->last_model=model;p->last_success_offset_ticks=offset;
    const uint32_t compressed=priority_summary_u32(width,p);
    if (compressed>p->max_width_ns) p->max_width_ns=compressed;
    p->success_count=priority_summary_add16(p->success_count,1u,p);
    w->summary.last_success=raw;w->summary.have_success=1u;
}
