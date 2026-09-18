/* VDC capability client; SYNC_IO alone touches output PIO/DMA/GPIO. */
#include "vdc_output_edge_cursor.h"
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define VDC_RUN_OUTPUT_HOT(name) __not_in_flash_func(name)
#else
#define VDC_RUN_OUTPUT_HOT(name) name
#endif
static vdc_run_output_status_t s_run_output;
/* A single nonblocking cross-core ownership gate covers the WHOLE client
 * invocation, including backend retirement. Backend RETIRED alone cannot
 * authorize overwriting client metadata while its wrapper still executes. */
static uint32_t s_run_output_request, s_run_output_busy;
static uint32_t s_run_output_service_sequence, s_run_output_current_service_sequence;
static uint64_t s_run_output_current_service_tick;
#define VDC_RUN_OUTPUT_PLAN_STEP_EDGES 4u
static vdc_timestamp_clock_bridge_t s_run_output_timeline;
static bool s_run_output_timeline_valid;
/* A private uncommitted suffix, never a DMA source. The whole-client gate
 * covers this cache as well as the public metadata. Preparing it does not
 * consume ordinals; a changed model invalidates it before admission. */
static struct {
    sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_MAX_EDGES];
    uint64_t predecessor_ordinal, predecessor_fall, last_local_ns, target_vdc_ns, maximum_width;
    uint64_t next_ordinal;
    uint32_t model_token, planned;
    bool valid, first;
} s_run_output_pending;

bool vdc_run_output_configuration_idle(void)
{
    return get_core_num()==0u &&
        !__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE) &&
        !__atomic_load_n(&s_run_output_busy,__ATOMIC_ACQUIRE);
}

static void run_output_note(uint32_t phase,vdc_run_output_outcome_t outcome)
{
    uint32_t *count=&s_run_output.outcomes[phase][outcome];
    if (*count!=UINT32_MAX) ++*count;
    s_run_output.last_phase=phase;
    s_run_output.last_outcome=(uint32_t)outcome;
    s_run_output.last_outcome_service_sequence=s_run_output_current_service_sequence;
    s_run_output.last_outcome_tick=s_run_output_current_service_tick;
}

static bool run_output_prepare_stopped(void *context)
{
    const uint32_t *args=context;
    const uint32_t session=vdc_dpll_manager_feedback_session();
    tdma_ring_clock_snapshot_t ring;
    vdc_output_timing_profile_t timing;
    app_realtime_schedule_snapshot_t schedule;
    if (!session || __atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE) ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || ring.config_seq!=ring.applied_config_seq ||
        !vdc_dpll_manager_get_output_timing_profile(&timing) ||
        !app_realtime_get_schedule_snapshot(&schedule) || !schedule.cycle_cycles ||
        !vdc_timestamp_clock_configuration_supported(BOARD_SYS_CLOCK_HZ)) return false;
    /* These capacity checks are not a proof that the full planner actually
     * runs every table period. Runtime misses and hardware runway stay visible. */
    if (!vdc_output_timing_profile_valid(&timing)) return false;
    const uint64_t count=(uint64_t)(timing.commit_ahead_us-timing.refill_low_us)*1000u/args[0];
    if (!count || count>SYNC_IO_RUN_OUTPUT_MAX_EDGES) return false;
    const uint64_t cycle_ns=((uint64_t)schedule.cycle_cycles*1000000000u+
        BOARD_SYS_CLOCK_HZ-1u)/BOARD_SYS_CLOCK_HZ;
    const uint64_t steps=(count+VDC_RUN_OUTPUT_PLAN_STEP_EDGES-1u)/VDC_RUN_OUTPUT_PLAN_STEP_EDGES;
    const uint64_t handoff_ns=((uint64_t)PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES*
        1000000000u+BOARD_SYS_CLOCK_HZ-1u)/BOARD_SYS_CLOCK_HZ;
    if ((uint64_t)(timing.plan_ahead_us-timing.refill_low_us)*1000u<(steps+1u)*cycle_ns ||
        (uint64_t)timing.refill_low_us*1000u<2u*cycle_ns+handoff_ns+
            (uint64_t)SYNC_IO_RUN_OUTPUT_MIN_GUARD_US*1000u) return false;
    uint32_t generation;
    /* RUN fixes its pulse width at PREPARE. Keep that width in the PIO ISR
     * so each immutable edge consumes only one FIFO word. Match the planner's
     * ceiling conversion exactly; ordinary variable-width clients stay paired. */
    const uint64_t high_ticks=((uint64_t)args[1]*BOARD_SYS_CLOCK_HZ+999999999u)/1000000000u;
    if (high_ticks>UINT32_MAX || !sync_io_run_output_prepare_uniform(
            BOARD_SYS_CLOCK_HZ,args[2],(uint32_t)high_ticks,&generation)) return false;
    memset(&s_run_output,0,sizeof(s_run_output));
    memset(&s_run_output_pending,0,sizeof(s_run_output_pending));
    memset(&s_run_output_timeline,0,sizeof(s_run_output_timeline));
    s_run_output_timeline_valid=false;
    s_run_output.last_outcome=VDC_RUN_OUTPUT_OUTCOME_NONE;
    s_run_output_service_sequence=0u;
    s_run_output_current_service_sequence=0u;
    s_run_output_current_service_tick=0u;
    s_run_output.request=generation; s_run_output.session=session;
    s_run_output.prepared_config=ring.config_seq;
    s_run_output.period_ns=args[0]; s_run_output.high_ns=args[1]; s_run_output.duration_ms=args[2];
    s_run_output.plan_ahead_us=timing.plan_ahead_us;
    s_run_output.commit_ahead_us=timing.commit_ahead_us;
    s_run_output.refill_low_us=timing.refill_low_us;
    s_run_output.block_edges=(uint32_t)count;
    s_run_output.schedule_cycles=schedule.cycle_cycles;
    (void)vdc_dpll_manager_get_output_delay_ns(&s_run_output.delay_ns);
    __atomic_store_n(&s_run_output_request,generation,__ATOMIC_RELEASE);
    return true;
}

bool vdc_run_output_prepare(uint32_t period_ns,uint32_t high_ns,
                            uint32_t duration_ms,uint32_t *request)
{
    uint32_t idle=0u;
    if (get_core_num()!=0u || !request || period_ns<1000000u || period_ns>100000000u ||
        high_ns<100u || high_ns>period_ns/2u || !duration_ms || duration_ms>20000u ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))
        return false;
    const uint32_t args[]={period_ns,high_ns,duration_ms};
    const bool ok=tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
        run_output_prepare_stopped,(void *)args);
    if (ok) *request=s_run_output.request;
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
    return ok;
}

void vdc_run_output_cancel(void) { sync_io_run_output_cancel(); }

/* Only the client owner may touch the private cache. Core0 cancellation
 * remains atomic intent and is consumed by the next service boundary. */
static void run_output_cancel_owned(void)
{
    s_run_output_pending.valid=false;
    s_run_output_pending.planned=0u;
    s_run_output_timeline_valid=false;
    sync_io_run_output_cancel();
}

static void run_output_release_core0(void)
{
    uint32_t idle=0u;
    if (get_core_num()!=0u || !__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE) ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    const uint32_t request=__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE);
    sync_io_run_output_snapshot_t hw;
    if (request && sync_io_run_output_snapshot(&hw) && hw.generation==request &&
        hw.state==SYNC_IO_RUN_OUTPUT_RETIRED && sync_io_run_output_release(request))
        __atomic_store_n(&s_run_output_request,0u,__ATOMIC_RELEASE);
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
}

bool vdc_run_output_status(vdc_run_output_status_t *out)
{
    /* Execution metadata is Core1-owned and exported only after retirement.
     * Prepared state may start at any time: never copy it on Core0. */
    if (!out || get_core_num()!=0u) return false;
    sync_io_run_output_snapshot_t hw;
    if (!sync_io_run_output_snapshot(&hw) ||
        (hw.state!=SYNC_IO_RUN_OUTPUT_RETIRED && hw.state!=SYNC_IO_RUN_OUTPUT_IDLE)) return false;
    uint32_t idle=0u;
    if (!__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return false;
    if (!sync_io_run_output_snapshot(&hw) ||
        (hw.state!=SYNC_IO_RUN_OUTPUT_RETIRED && hw.state!=SYNC_IO_RUN_OUTPUT_IDLE)) {
        __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE); return false;
    }
    *out=s_run_output; out->hardware=hw;
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
    return true;
}

/* Keep the shared body in main SRAM even when called by an XIP wrapper.
 * Placement does not qualify its execution budget; hardware records do. */
static __attribute__((noinline)) void VDC_RUN_OUTPUT_HOT(run_output_service_owned)(bool cached_only)
{
    sync_io_run_output_service_core1();
    const uint32_t request=__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE);
    if (!request) return;
    /* The last admitted block classifies an unavailable initial snapshot;
     * after a valid snapshot use the actual active backend state. */
    uint32_t phase=s_run_output.blocks_planned ? VDC_RUN_OUTPUT_RUNNING_PHASE : VDC_RUN_OUTPUT_PREPARED_PHASE;
    sync_io_run_output_snapshot_t hw;
    if (!sync_io_run_output_snapshot(&hw) || hw.generation!=request) {
        run_output_note(phase,VDC_RUN_OUTPUT_BACKEND_SNAPSHOT_UNAVAILABLE); return;
    }
    if (hw.state!=SYNC_IO_RUN_OUTPUT_PREPARED && hw.state!=SYNC_IO_RUN_OUTPUT_RUNNING) {
        s_run_output_pending.valid=false; s_run_output_pending.planned=0u;
        s_run_output_timeline_valid=false; return;
    }
    s_run_output_current_service_sequence=s_run_output_service_sequence!=UINT32_MAX ?
        ++s_run_output_service_sequence : UINT32_MAX;
    s_run_output.service_sequence=s_run_output_current_service_sequence;
    s_run_output_current_service_tick=hw.service_observations ? hw.service_last_tick : 0u;
    phase=hw.state==SYNC_IO_RUN_OUTPUT_RUNNING ? VDC_RUN_OUTPUT_RUNNING_PHASE : VDC_RUN_OUTPUT_PREPARED_PHASE;
    if (vdc_dpll_manager_feedback_session()!=s_run_output.session ||
        app_realtime_cycle_cycles_core1()!=s_run_output.schedule_cycles) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        ++s_run_output.binding_rejects; run_output_cancel_owned(); return;
    }
    tdma_ring_clock_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) {
        run_output_note(phase,VDC_RUN_OUTPUT_RING_UNAVAILABLE); return;
    }
    if (!ring.enabled) {
        /* Initial STOP is the legal wait-for-ARM state. A later STOP changes
         * config even when ARM and STOP occurred between client services. */
        if (s_run_output.arm_config || ring.config_seq!=s_run_output.prepared_config) {
            run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
            run_output_cancel_owned();
        } else run_output_note(phase,VDC_RUN_OUTPUT_RING_WAIT);
        return;
    }
    if (!s_run_output.arm_config) s_run_output.arm_config=ring.config_seq;
    if (!ring.config_seq || ring.config_seq!=s_run_output.arm_config) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        ++s_run_output.binding_rejects; run_output_cancel_owned(); return;
    }
    if (!ring.adapter_started || ring.config_seq!=ring.applied_config_seq || !ring.data_enabled) {
        if (s_run_output.ring_config) {
            run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
            run_output_cancel_owned();
        } else run_output_note(phase,VDC_RUN_OUTPUT_RING_WAIT);
        return;
    }
    if (s_run_output.ring_config &&
        (s_vdc_domain.control.profile.generation!=s_run_output.role_generation ||
         s_vdc_domain.clock.epoch_id!=s_run_output.clock_epoch ||
         s_vdc_domain.clock.run_id!=s_run_output.clock_run)) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        ++s_run_output.binding_rejects; run_output_cancel_owned(); return;
    }
    vdc_dpll_manager_committed_model_t model;
    if (!vdc_dpll_manager_get_committed_model(&model)) {
        run_output_note(phase,VDC_RUN_OUTPUT_MODEL_UNAVAILABLE); return;
    }
    /* A prior committed snapshot may still be visible before the first VDC
     * phase after START. Wait for a current model, without requiring LOCKED.
     * Once running, identity invalidation must cancel even while DMA is busy. */
    if (!model.token || !model.dco.valid || model.session!=s_run_output.session ||
        model.local_slot!=ring.local_slot_id || model.dco.tdma_schedule_crc32!=ring.schedule_crc32 ||
        model.role_generation!=s_vdc_domain.control.profile.generation ||
        model.clock_epoch_id!=s_vdc_domain.clock.epoch_id ||
        model.clock_run_id!=s_vdc_domain.clock.run_id ||
        model.local_slot!=s_vdc_domain.schedule.local_slot_id ||
        model.dco.tdma_schedule_crc32!=s_vdc_domain.schedule.schedule_crc32 ||
        (s_run_output.ring_config && (ring.config_seq!=s_run_output.ring_config ||
         model.role_generation!=s_run_output.role_generation ||
         model.clock_epoch_id!=s_run_output.clock_epoch || model.clock_run_id!=s_run_output.clock_run))) {
        if (s_run_output.ring_config) {
            run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
            ++s_run_output.binding_rejects; run_output_cancel_owned();
        } else run_output_note(phase,VDC_RUN_OUTPUT_IDENTITY_WAIT);
        return;
    }
    const bool first=hw.state==SYNC_IO_RUN_OUTPUT_PREPARED;
    if ((s_run_output_pending.valid || s_run_output_pending.planned) &&
        (s_run_output_pending.first!=first ||
        s_run_output_pending.model_token!=model.token ||
        s_run_output_pending.predecessor_ordinal!=hw.last_ordinal ||
        s_run_output_pending.predecessor_fall!=hw.last_falling_tick)) {
        s_run_output_pending.valid=false;
        s_run_output_pending.planned=0u;
        if (s_run_output.cache_invalidations!=UINT32_MAX) ++s_run_output.cache_invalidations;
        s_run_output.last_invalidation_service_sequence=s_run_output_current_service_sequence;
        s_run_output.last_invalidation_tick=s_run_output_current_service_tick;
    }
    const bool cached=s_run_output_pending.valid;
    if (cached_only && (first || !cached)) {
        if (s_run_output.fast_empty!=UINT32_MAX) ++s_run_output.fast_empty;
        return;
    }
    const bool can_submit=sync_io_run_output_can_submit_core1(request);
    if (!can_submit && (first || s_run_output_pending.valid)) {
        run_output_note(phase,VDC_RUN_OUTPUT_DMA_NOT_READY); return;
    }
    /* Keep one raw/local mapping for this finite request. Only the ordinary
     * planner acquires the first bridge; the short route never starts a block.
     * Fresh raw time remains mandatory for planning and hardware admission. */
    if (!vdc_timestamp_clock_configuration_supported(BOARD_SYS_CLOCK_HZ)) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        ++s_run_output.binding_rejects; run_output_cancel_owned(); return;
    }
    if (!s_run_output_timeline_valid) {
        if (!vdc_timestamp_clock_try_read_bridge(BOARD_SYS_CLOCK_HZ,&s_run_output_timeline)) {
            run_output_note(phase,VDC_RUN_OUTPUT_BRIDGE_UNAVAILABLE); return;
        }
        s_run_output_timeline_valid=true;
        s_run_output.timeline_raw_before=s_run_output_timeline.raw_before;
        s_run_output.timeline_local_ns=s_run_output_timeline.local_ns;
        s_run_output.timeline_raw_after=s_run_output_timeline.raw_after;
        if (s_run_output.timeline_bridge_samples!=UINT32_MAX) ++s_run_output.timeline_bridge_samples;
    }
    uint64_t now;
    if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,&now)) {
        run_output_note(phase,VDC_RUN_OUTPUT_BRIDGE_UNAVAILABLE); return;
    }
    if (now<model.valid_from_raw) {
        run_output_note(phase,VDC_RUN_OUTPUT_MODEL_UNAVAILABLE); return;
    }
    const uint64_t ticks_per_us=BOARD_SYS_CLOCK_HZ/1000000u;
    const uint64_t plan_ticks=(uint64_t)s_run_output.plan_ahead_us*ticks_per_us;
    const uint64_t low_ticks=(uint64_t)s_run_output.refill_low_us*ticks_per_us;
    const uint64_t commit_ticks=(uint64_t)s_run_output.commit_ahead_us*ticks_per_us;
    const uint64_t offset=hw.anchor_after>=hw.anchor_before ?
        hw.anchor_after-hw.anchor_before : UINT64_MAX;
    if (!first && (offset==UINT64_MAX || hw.last_falling_tick>UINT64_MAX-offset)) {
        run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); run_output_cancel_owned(); return;
    }
    /* Earliest possible tail drives refill urgency; the upper enable bound
     * is used separately to enforce the maximum new hardware commitment. */
    const uint64_t tail=first ? 0u : hw.last_falling_tick;
    /* This watermark describes the latest irrevocably submitted tail, not
     * the software cache or DMA remaining count. Source retirement remains
     * a separate necessary condition for actually refilling the FIFO. */
    if (!s_run_output_pending.valid) {
        if (!first && tail>now && tail-now>plan_ticks) {
            if (s_run_output.plan_waits!=UINT32_MAX) ++s_run_output.plan_waits;
            return;
        }
        if (!s_run_output_pending.planned) {
            if (!sync_io_run_output_snapshot(&hw) || hw.generation!=request ||
                (hw.state!=SYNC_IO_RUN_OUTPUT_PREPARED && hw.state!=SYNC_IO_RUN_OUTPUT_RUNNING) ||
                (hw.state==SYNC_IO_RUN_OUTPUT_PREPARED)!=first) {
                run_output_note(phase,VDC_RUN_OUTPUT_BACKEND_SNAPSHOT_UNAVAILABLE); return;
            }
            uint64_t local_now;
            if (!vdc_timestamp_timeline_local_now_upper(&s_run_output_timeline,now,&local_now)) {
                run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); run_output_cancel_owned(); return;
            }
            /* PRESTART's existing lead is separate from the RUN inventory
             * cap: no stream has started yet. It is not renewed at block boundaries. */
            const uint64_t lead=first ? UINT64_C(20000000) : UINT64_C(200000);
            if (local_now>UINT64_MAX-lead || (!first && hw.last_ordinal==UINT64_MAX)) {
                run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); run_output_cancel_owned(); return;
            }
            s_run_output_pending.last_local_ns=local_now+lead;
            if (!first && s_run_output.last_local_ns>s_run_output_pending.last_local_ns)
                s_run_output_pending.last_local_ns=s_run_output.last_local_ns;
            s_run_output_pending.next_ordinal=first ? 0u : hw.last_ordinal+1u;
            s_run_output_pending.predecessor_ordinal=hw.last_ordinal;
            s_run_output_pending.predecessor_fall=hw.last_falling_tick;
            s_run_output_pending.maximum_width=s_run_output.maximum_bridge_width_ticks;
            s_run_output_pending.model_token=model.token;
            s_run_output_pending.first=first;
        }
        const uint64_t high_ticks=((uint64_t)s_run_output.high_ns*BOARD_SYS_CLOCK_HZ+999999999u)/1000000000u;
        /* A late model change must not reintroduce four table periods of
         * inverse calculations while the irrevocable FIFO tail drains.
         * For a continuous same-model grid, seed the exact inverse once and
         * advance its quotient/remainder for the fixed suffix. PRESTART and
         * wide-pulse/skipped-grid cases retain the bounded scalar path.
         * The cached-only service never reaches new planning above. */
        vdc_output_edge_cursor_t cursor={0};
        vdc_output_edge_plan_t first_plan={0};
        const bool batch=!first && !s_run_output_pending.planned &&
            vdc_output_edge_cursor_supported(&model.dco,s_run_output.period_ns,s_run_output.high_ns);
        if (batch && !vdc_output_edge_cursor_init(&model.dco,0u,s_run_output.period_ns,
                s_run_output_pending.next_ordinal,s_run_output_pending.last_local_ns,
                s_run_output.delay_ns,s_run_output.high_ns,&cursor,&first_plan)) {
            run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); ++s_run_output.plan_rejects; return;
        }
        const uint32_t end=batch ? s_run_output.block_edges :
            s_run_output.block_edges-s_run_output_pending.planned>VDC_RUN_OUTPUT_PLAN_STEP_EDGES ?
                s_run_output_pending.planned+VDC_RUN_OUTPUT_PLAN_STEP_EDGES : s_run_output.block_edges;
        for (uint32_t i=s_run_output_pending.planned;i<end;++i) {
            vdc_output_edge_plan_t plan;
            vdc_local_raw_bracket_t raw;
            bool planned;
            if (batch && i==0u) { plan=first_plan; planned=true; }
            else if (batch) planned=vdc_output_edge_cursor_next(&cursor,&plan);
            else planned=vdc_output_edge_plan(&model.dco,0u,s_run_output.period_ns,
                s_run_output_pending.next_ordinal,s_run_output_pending.last_local_ns,s_run_output.delay_ns,&plan);
            if (!planned ||
                !vdc_timestamp_timeline_local_to_raw(&s_run_output_timeline,plan.physical_local_ns,&raw) ||
                raw.hi>UINT64_MAX-high_ticks || plan.physical_local_ns>UINT64_MAX-s_run_output.high_ns ||
                plan.ordinal==UINT64_MAX) {
                s_run_output_pending.planned=0u;
                run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); ++s_run_output.plan_rejects; return;
            }
            s_run_output_pending.edges[i]=(sync_io_run_output_edge_t){.rising_tick=raw.hi,
                .falling_tick=raw.hi+high_ticks,.ordinal=plan.ordinal,.model_token=model.token};
            if (raw.hi-raw.lo>s_run_output_pending.maximum_width)
                s_run_output_pending.maximum_width=raw.hi-raw.lo;
            s_run_output_pending.last_local_ns=plan.physical_local_ns+s_run_output.high_ns;
            s_run_output_pending.next_ordinal=plan.ordinal+1u;
            s_run_output_pending.target_vdc_ns=plan.target_vdc_ns;
            s_run_output_pending.planned=i+1u;
        }
        if (s_run_output.partial_plan_steps!=UINT32_MAX) ++s_run_output.partial_plan_steps;
        if (s_run_output_pending.planned!=s_run_output.block_edges) return;
        s_run_output_pending.valid=true;
        if (!can_submit && s_run_output.prefetched_blocks!=UINT32_MAX) ++s_run_output.prefetched_blocks;
    }
    /* Core0 may publish STOP/session changes while planning. Reobserve the
     * small ring mailbox before admitting a suffix. A still-later STOP is
     * consumed at the next mandatory boundary; model commits are Core1-only. */
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) {
        run_output_note(phase,VDC_RUN_OUTPUT_POSTPLAN_RING_UNAVAILABLE); return;
    }
    if (!ring.enabled || !ring.adapter_started || !ring.data_enabled ||
        ring.config_seq!=s_run_output.arm_config || ring.applied_config_seq!=ring.config_seq ||
        ring.local_slot_id!=model.local_slot || ring.schedule_crc32!=model.dco.tdma_schedule_crc32 ||
        vdc_dpll_manager_feedback_session()!=s_run_output.session) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        ++s_run_output.binding_rejects; run_output_cancel_owned(); return;
    }
    /* DMA may finish while the bounded planning step executes. Recheck that
     * ownership boundary once instead of carrying a stale busy result into
     * another table period; the backend still performs its final guard. */
    if (!can_submit && !sync_io_run_output_can_submit_core1(request)) {
        run_output_note(phase,VDC_RUN_OUTPUT_DMA_NOT_READY); return;
    }
    /* Reobserve time after the bounded planning step. Never grant a new
     * hardware horizon using the older planning-time observation. */
    if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,&now)) {
        run_output_note(phase,VDC_RUN_OUTPUT_BRIDGE_UNAVAILABLE); return;
    }
    if (!first && tail>now && tail-now>low_ticks) {
        if (s_run_output.refill_waits!=UINT32_MAX) ++s_run_output.refill_waits;
        return;
    }
    const uint64_t next_tail=s_run_output_pending.edges[s_run_output.block_edges-1u].falling_tick;
    if (first && (next_tail<s_run_output_pending.edges[0].rising_tick ||
        next_tail-s_run_output_pending.edges[0].rising_tick>commit_ticks)) {
        s_run_output_pending.valid=false; s_run_output_pending.planned=0u;
        run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); ++s_run_output.plan_rejects; return;
    }
    if (!first && (next_tail>UINT64_MAX-offset || now>UINT64_MAX-commit_ticks)) {
        run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); run_output_cancel_owned(); return;
    }
    if (!first && next_tail+offset>now+commit_ticks) {
        if (s_run_output.commit_waits!=UINT32_MAX) ++s_run_output.commit_waits;
        return;
    }
    /* A cache hit skips bridge acquisition, not the clock lifetime check.
     * This also covers TIMER0/ref/PLL configuration beyond backend checks. */
    if (cached && !vdc_timestamp_clock_configuration_supported(BOARD_SYS_CLOCK_HZ)) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        ++s_run_output.binding_rejects; run_output_cancel_owned(); return;
    }
    if (!sync_io_run_output_submit_count_core1(request,s_run_output_pending.edges,s_run_output.block_edges)) {
        const sync_io_run_output_submit_failure_t failure=
            sync_io_run_output_last_submit_failure();
        s_run_output.last_submit_failure=(uint32_t)failure;
        /* A NOT_READY result can be a same-boundary race between the
         * admission check above and the backend's final check.  Keep the
         * complete private suffix so the next service can revalidate the
         * generation/tail and retry it without rebuilding or touching the
         * already admitted prefix.  All other branches are terminal for
         * this suffix: in particular GUARD means the hardware runway has
         * already entered the minimum safety window, so retaining it would
         * retry an expired block.  Cancellation, clock/encoding/deadline,
         * DMA and start failures therefore discard the cache here.
         */
        if (failure!=SYNC_IO_RUN_OUTPUT_SUBMIT_FAILURE_NOT_READY) {
            s_run_output_pending.valid=false;
            s_run_output_pending.planned=0u;
        }
        run_output_note(phase,VDC_RUN_OUTPUT_SUBMIT_REJECTED); ++s_run_output.plan_rejects; return;
    }
    s_run_output.ring_config=ring.config_seq; s_run_output.role_generation=model.role_generation;
    s_run_output.clock_epoch=model.clock_epoch_id; s_run_output.clock_run=model.clock_run_id;
    s_run_output.last_local_ns=s_run_output_pending.last_local_ns;
    s_run_output.last_target_vdc_ns=s_run_output_pending.target_vdc_ns;
    s_run_output.maximum_bridge_width_ticks=s_run_output_pending.maximum_width;
    if (cached && s_run_output.cache_hits!=UINT32_MAX) ++s_run_output.cache_hits;
    s_run_output_pending.valid=false;
    s_run_output_pending.planned=0u;
    ++s_run_output.blocks_planned;
    run_output_note(phase,VDC_RUN_OUTPUT_SUBMITTED);
}

__attribute__((noinline)) void VDC_RUN_OUTPUT_HOT(vdc_run_output_service_core1)(void)
{
    uint32_t idle=0u;
    if (get_core_num()!=1u ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    run_output_service_owned(false);
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
}

__attribute__((noinline)) uint32_t VDC_RUN_OUTPUT_HOT(vdc_run_output_service_cached_core1)(void)
{
    uint32_t idle=0u;
    if (get_core_num()!=1u ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return 0u;
    const uint32_t request=__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE);
    const bool tracked=request!=0u;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    const uint32_t before=time_us_32();
#endif
    const uint32_t admitted=s_run_output.blocks_planned;
    if (tracked && s_run_output.fast_calls!=UINT32_MAX) ++s_run_output.fast_calls;
    run_output_service_owned(true);
    if (tracked && s_run_output.blocks_planned!=admitted && s_run_output.fast_submissions!=UINT32_MAX)
        ++s_run_output.fast_submissions;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    const uint32_t elapsed=time_us_32()-before;
    const uint32_t upper=elapsed==UINT32_MAX ? UINT32_MAX : elapsed+1u;
    if (tracked && upper>s_run_output.fast_body_max_us) s_run_output.fast_body_max_us=upper;
#endif
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
    return request;
}

__attribute__((noinline)) void VDC_RUN_OUTPUT_HOT(vdc_run_output_note_cached_wall_core1)(uint32_t request,
    uint32_t cycles,uint32_t budget_cycles)
{
    uint32_t idle=0u;
    if (get_core_num()!=1u || !request ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    /* Core0 may retire/reprepare after the caller returned. A delayed sample
     * must never contaminate the next request. While the same request is
     * retained, refused reports leave fast_calls > fast_wall_samples; a later
     * preparation resets those old statistics and cannot qualify them. */
    if (request==__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE)) {
        if (s_run_output.fast_wall_samples!=UINT32_MAX) ++s_run_output.fast_wall_samples;
        if (cycles>s_run_output.fast_wall_max_cycles) s_run_output.fast_wall_max_cycles=cycles;
        if (cycles>budget_cycles && s_run_output.fast_budget_overruns!=UINT32_MAX)
            ++s_run_output.fast_budget_overruns;
    }
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
}

#undef VDC_RUN_OUTPUT_HOT
