/* Core0 maintenance observation only. No work is added to Core1. The CAS
 * serializes SCPI and Core0 service without waiting or holding IRQs off. */
static uint32_t s_fixed_output_busy;
static vdc_dpll_manager_fixed_output_status_t s_fixed_output;

static bool fixed_output_enter(void)
{
    uint32_t expected = 0u;
    return get_core_num() == 0u && __atomic_compare_exchange_n(
        &s_fixed_output_busy, &expected, 1u, false,
        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void fixed_output_leave(void)
{
    __atomic_store_n(&s_fixed_output_busy, 0u, __ATOMIC_RELEASE);
}

/* 0 = disproved, 1 = proved, 2 = transient snapshot contention. */
static uint32_t fixed_output_stopped_state(void)
{
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring)) return 2u;
    return !ring.enabled && !ring.adapter_started &&
        ring.config_seq == ring.applied_config_seq;
}

static bool fixed_output_clock(void)
{
    vdc_timestamp_clock_bridge_diagnostic_t diagnostic;
    return vdc_timestamp_clock_read_bridge_diagnostic(
        BOARD_SYS_CLOCK_HZ, &diagnostic) &&
        diagnostic.configuration_supported && diagnostic.clock_ready;
}

static uint32_t fixed_output_model_state(void)
{
    vdc_dpll_manager_committed_model_t model;
    if (!s_fixed_output.model.session || vdc_dpll_manager_feedback_session() !=
            s_fixed_output.model.session) return 0u;
    if (!vdc_dpll_manager_get_committed_model(&model)) return 2u;
    return memcmp(&model, &s_fixed_output.model, sizeof(model)) == 0;
}

static bool fixed_output_validate_start(void *unused)
{
    (void)unused;
    return fixed_output_stopped_state() == 1u && fixed_output_model_state() == 1u &&
        fixed_output_clock() && !s_observation_self_test.active;
}

static void fixed_output_runtime(void)
{
    sync_io_fixed_rate_runtime_t runtime;
    sync_io_sma_observer_fixed_rate_get_runtime(&runtime);
    s_fixed_output.model_unchanged = fixed_output_model_state();
    if (!runtime.configured || runtime.request.request_id != s_fixed_output.request_id) {
        /* A rejected new attempt must not acquire an older batch's counters. */
        s_fixed_output.completed_pulses = 0u;
        s_fixed_output.transfer_count = 0u;
        s_fixed_output.pio_enabled = 0u;
        s_fixed_output.dma_busy = 0u;
        if (s_fixed_output.state == VDC_FIXED_OUTPUT_RUNNING) {
            s_fixed_output.state = VDC_FIXED_OUTPUT_FAILED;
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_CHANGED;
        }
        return;
    }
    s_fixed_output.completed_pulses = runtime.pulse.completed_pulses;
    s_fixed_output.transfer_count = runtime.pulse.transfer_count;
    s_fixed_output.pio_enabled = runtime.pulse.pio_enabled;
    s_fixed_output.dma_busy = runtime.pulse.dma_busy;
    if (s_fixed_output.state == VDC_FIXED_OUTPUT_RUNNING &&
        !runtime.pulse.running) {
        if (runtime.configured && !runtime.pulse.fault_code &&
            runtime.pulse.completed_pulses == s_fixed_output.pulse_count) {
            s_fixed_output.state = VDC_FIXED_OUTPUT_COMPLETE;
        } else {
            s_fixed_output.state = VDC_FIXED_OUTPUT_FAILED;
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_CHANGED;
        }
    }
}

static void vdc_fixed_output_service_core0(void)
{
    if (!fixed_output_enter()) return;
    if (s_fixed_output.state == VDC_FIXED_OUTPUT_RUNNING) {
        /* A fixed batch never follows a later DCO. If the diagnostic context
         * is revoked, stop its hardware and retain the admitted identity. */
        /* A single seqlock miss is not revocation. Hardware keeps executing
         * its immutable batch; a later Core0 service resolves contention. */
        if (fixed_output_stopped_state() == 0u || fixed_output_model_state() == 0u ||
            !fixed_output_clock() || s_observation_self_test.active) {
            sync_io_sma_observer_fixed_rate_disarm();
            s_fixed_output.state = VDC_FIXED_OUTPUT_CANCELLED;
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_CHANGED;
        }
        fixed_output_runtime();
    }
    fixed_output_leave();
}

bool vdc_dpll_manager_fixed_output_start(uint32_t period_ns, uint32_t high_ns,
    uint32_t pulse_count, uint32_t tick_period_ns, uint32_t *request_id)
{
    if (!request_id || !fixed_output_enter()) return false;
    bool ok = false;
    s_fixed_output.schema = 1u;
    if (s_fixed_output.state == VDC_FIXED_OUTPUT_RUNNING) {
        s_fixed_output.last_reason = VDC_FIXED_OUTPUT_BUSY;
        goto done;
    }
    if (fixed_output_stopped_state() != 1u) {
        s_fixed_output.last_reason = VDC_FIXED_OUTPUT_NOT_STOPPED;
        goto done;
    }
    vdc_dpll_manager_committed_model_t model;
    if (!vdc_dpll_manager_get_committed_model(&model) || !model.token ||
        !model.session || model.session != vdc_dpll_manager_feedback_session() ||
        model.dco.valid != 1u || !model.dco.dco_update_seq ||
        model.dco.period_adjust_ppb <= -1000000000) {
        s_fixed_output.last_reason = VDC_FIXED_OUTPUT_MODEL;
        goto done;
    }
    if (!fixed_output_clock()) {
        s_fixed_output.last_reason = VDC_FIXED_OUTPUT_CLOCK;
        goto done;
    }
    /* This observer is a low-rate finite diagnostic, not an unrestricted
     * pulse generator. Leave ample DMA service time between each pair. */
    if (period_ns < VDC_FIXED_OUTPUT_MIN_PERIOD_NS ||
        s_observation_self_test.active || s_fixed_output.request_id == UINT32_MAX) {
        s_fixed_output.last_reason = VDC_FIXED_OUTPUT_RESOURCE_OR_ARGUMENT;
        goto done;
    }
    const sync_io_rate_schedule_request_t request = {
        .request_id = s_fixed_output.request_id + 1u,
        .rate_ppb = model.dco.period_adjust_ppb,
        .period_ns = period_ns, .high_ns = high_ns,
        .pulse_count = pulse_count, .tick_period_ns = tick_period_ns,
    };
    memset(&s_fixed_output, 0, sizeof(s_fixed_output));
    s_fixed_output.schema = 1u;
    s_fixed_output.model = model;
    s_fixed_output.request_id = request.request_id;
    s_fixed_output.period_ns = period_ns;
    s_fixed_output.high_ns = high_ns;
    s_fixed_output.pulse_count = pulse_count;
    s_fixed_output.tick_period_ns = tick_period_ns;
    s_fixed_output.state = VDC_FIXED_OUTPUT_FAILED;
    sync_io_fixed_rate_runtime_t runtime;
    if (!sync_io_sma_observer_fixed_rate_arm(&request, &runtime,
            fixed_output_validate_start, NULL)) {
        s_fixed_output.last_reason = VDC_FIXED_OUTPUT_RESOURCE_OR_ARGUMENT;
        goto done;
    }
    s_fixed_output.first_edge_ticks = runtime.encoding.first_edge_ticks;
    s_fixed_output.last_edge_ticks = runtime.encoding.last_edge_ticks;
    s_fixed_output.total_ticks = runtime.encoding.total_ticks;
    s_fixed_output.min_period_ticks = runtime.encoding.min_period_ticks;
    s_fixed_output.max_period_ticks = runtime.encoding.max_period_ticks;
    s_fixed_output.high_ticks = runtime.encoding.high_ticks;
    s_fixed_output.system_clock_hz = runtime.system_clock_hz;
    s_fixed_output.pio_divider256 = runtime.pio_divider256;
    if (!fixed_output_validate_start(NULL)) {
        sync_io_sma_observer_fixed_rate_disarm();
        s_fixed_output.last_reason = VDC_FIXED_OUTPUT_CHANGED;
        goto done;
    }
    s_fixed_output.state = VDC_FIXED_OUTPUT_RUNNING;
    s_fixed_output.last_reason = VDC_FIXED_OUTPUT_OK;
    fixed_output_runtime();
    *request_id = request.request_id;
    ok = true;
done:
    fixed_output_leave();
    return ok;
}

bool vdc_dpll_manager_fixed_output_stop(void)
{
    if (!fixed_output_enter()) return false;
    if (s_fixed_output.state == VDC_FIXED_OUTPUT_RUNNING) {
        fixed_output_runtime();
        sync_io_sma_observer_fixed_rate_disarm();
        if (s_fixed_output.state == VDC_FIXED_OUTPUT_RUNNING) {
            s_fixed_output.state = VDC_FIXED_OUTPUT_CANCELLED;
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_CANCEL;
        }
        fixed_output_runtime();
    }
    fixed_output_leave();
    return true;
}

bool vdc_dpll_manager_get_fixed_output(vdc_dpll_manager_fixed_output_status_t *out)
{
    if (!out || !fixed_output_enter()) return false;
    const bool stopped = fixed_output_stopped_state() == 1u;
    if (stopped) {
        if (s_fixed_output.request_id) fixed_output_runtime();
        s_fixed_output.schema = 1u;
        *out = s_fixed_output;
    }
    fixed_output_leave();
    return stopped;
}
