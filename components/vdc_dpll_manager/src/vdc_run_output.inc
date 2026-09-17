/* VDC capability client; SYNC_IO alone touches output PIO/DMA/GPIO. */
static vdc_run_output_status_t s_run_output;
/* A single nonblocking cross-core ownership gate covers the WHOLE client
 * invocation, including backend retirement. Backend RETIRED alone cannot
 * authorize overwriting client metadata while its wrapper still executes. */
static uint32_t s_run_output_request, s_run_output_busy;

static bool run_output_prepare_stopped(void *context)
{
    const uint32_t *args=context;
    const uint32_t session=vdc_dpll_manager_feedback_session();
    tdma_ring_clock_snapshot_t ring;
    if (!session || __atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE) ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || ring.config_seq!=ring.applied_config_seq ||
        !vdc_timestamp_clock_configuration_supported(BOARD_SYS_CLOCK_HZ)) return false;
    uint32_t generation;
    if (!sync_io_run_output_prepare(BOARD_SYS_CLOCK_HZ,args[2],&generation)) return false;
    memset(&s_run_output,0,sizeof(s_run_output));
    s_run_output.request=generation; s_run_output.session=session;
    s_run_output.prepared_config=ring.config_seq;
    s_run_output.period_ns=args[0]; s_run_output.high_ns=args[1]; s_run_output.duration_ms=args[2];
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

static void run_output_service_owned(void)
{
    sync_io_run_output_service_core1();
    const uint32_t request=__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE);
    if (!request) return;
    sync_io_run_output_snapshot_t hw;
    if (!sync_io_run_output_snapshot(&hw) || hw.generation!=request ||
        (hw.state!=SYNC_IO_RUN_OUTPUT_PREPARED && hw.state!=SYNC_IO_RUN_OUTPUT_RUNNING)) return;
    if (vdc_dpll_manager_feedback_session()!=s_run_output.session) {
        ++s_run_output.binding_rejects; sync_io_run_output_cancel(); return;
    }
    tdma_ring_clock_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) return;
    if (!ring.enabled) {
        /* Initial STOP is the legal wait-for-ARM state. A later STOP changes
         * config even when ARM and STOP occurred between client services. */
        if (s_run_output.arm_config || ring.config_seq!=s_run_output.prepared_config)
            sync_io_run_output_cancel();
        return;
    }
    if (!s_run_output.arm_config) s_run_output.arm_config=ring.config_seq;
    if (!ring.config_seq || ring.config_seq!=s_run_output.arm_config) {
        ++s_run_output.binding_rejects; sync_io_run_output_cancel(); return;
    }
    if (!ring.adapter_started || ring.config_seq!=ring.applied_config_seq || !ring.data_enabled) {
        if (s_run_output.ring_config) sync_io_run_output_cancel();
        return;
    }
    if (s_run_output.ring_config &&
        (s_vdc_domain.control.profile.generation!=s_run_output.role_generation ||
         s_vdc_domain.clock.epoch_id!=s_run_output.clock_epoch ||
         s_vdc_domain.clock.run_id!=s_run_output.clock_run)) {
        ++s_run_output.binding_rejects; sync_io_run_output_cancel(); return;
    }
    vdc_dpll_manager_committed_model_t model;
    if (!vdc_dpll_manager_get_committed_model(&model)) return;
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
        if (s_run_output.ring_config) { ++s_run_output.binding_rejects; sync_io_run_output_cancel(); }
        return;
    }
    if (!sync_io_run_output_can_submit_core1(request)) return;
    vdc_timestamp_clock_bridge_t bridge;
    if (!vdc_timestamp_clock_try_read_bridge(BOARD_SYS_CLOCK_HZ,&bridge)) return;
    if (!sync_io_run_output_snapshot(&hw) || hw.generation!=request) return;
    const bool first=hw.state==SYNC_IO_RUN_OUTPUT_PREPARED;
    const uint64_t lead=first ? UINT64_C(20000000) : UINT64_C(200000);
    if (bridge.local_ns>UINT64_MAX-lead) { sync_io_run_output_cancel(); return; }
    uint64_t minimum=bridge.local_ns+lead;
    if (!first && s_run_output.last_local_ns>minimum) minimum=s_run_output.last_local_ns;
    uint64_t ordinal=first ? 0u : hw.last_ordinal+1u;
    if (!first && hw.last_ordinal==UINT64_MAX) { sync_io_run_output_cancel(); return; }
    sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES];
    vdc_output_edge_plan_t plan;
    uint64_t maximum_width=s_run_output.maximum_bridge_width_ticks;
    const uint64_t high_ticks=((uint64_t)s_run_output.high_ns*BOARD_SYS_CLOCK_HZ+999999999u)/1000000000u;
    for (uint32_t i=0;i<SYNC_IO_RUN_OUTPUT_BLOCK_EDGES;++i) {
        vdc_local_raw_bracket_t raw;
        if (!vdc_output_edge_plan(&model.dco,0u,s_run_output.period_ns,ordinal,
                minimum,s_run_output.delay_ns,&plan) ||
            !vdc_timestamp_bridge_local_to_raw(&bridge,plan.physical_local_ns,&raw) ||
            raw.hi>UINT64_MAX-high_ticks || plan.physical_local_ns>UINT64_MAX-s_run_output.high_ns ||
            plan.ordinal==UINT64_MAX) { ++s_run_output.plan_rejects; return; }
        edges[i]=(sync_io_run_output_edge_t){.rising_tick=raw.hi,
            .falling_tick=raw.hi+high_ticks,.ordinal=plan.ordinal,.model_token=model.token};
        if (raw.hi-raw.lo>maximum_width) maximum_width=raw.hi-raw.lo;
        minimum=plan.physical_local_ns+s_run_output.high_ns;
        ordinal=plan.ordinal+1u;
    }
    /* Core0 may publish STOP/session changes while planning. Reobserve the
     * small ring mailbox before admitting a suffix. A still-later STOP is
     * consumed at the next mandatory boundary; model commits are Core1-only. */
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) return;
    if (!ring.enabled || !ring.adapter_started || !ring.data_enabled ||
        ring.config_seq!=s_run_output.arm_config || ring.applied_config_seq!=ring.config_seq ||
        ring.local_slot_id!=model.local_slot || ring.schedule_crc32!=model.dco.tdma_schedule_crc32 ||
        vdc_dpll_manager_feedback_session()!=s_run_output.session) {
        ++s_run_output.binding_rejects; sync_io_run_output_cancel(); return;
    }
    if (!sync_io_run_output_submit_core1(request,edges)) { ++s_run_output.plan_rejects; return; }
    s_run_output.ring_config=ring.config_seq; s_run_output.role_generation=model.role_generation;
    s_run_output.clock_epoch=model.clock_epoch_id; s_run_output.clock_run=model.clock_run_id;
    s_run_output.last_local_ns=minimum; s_run_output.last_target_vdc_ns=plan.target_vdc_ns;
    s_run_output.maximum_bridge_width_ticks=maximum_width;
    ++s_run_output.blocks_planned;
}

void vdc_run_output_service_core1(void)
{
    uint32_t idle=0u;
    if (get_core_num()!=1u ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    run_output_service_owned();
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
}
