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
    return vdc_timestamp_clock_configuration_supported(BOARD_SYS_CLOCK_HZ);
}

static uint32_t fixed_output_model_state(void)
{
    vdc_dpll_manager_committed_model_t model;
    if (!s_fixed_output.model.session || vdc_dpll_manager_feedback_session() !=
            s_fixed_output.model.session) return 0u;
    if (!vdc_dpll_manager_get_committed_model(&model)) return 2u;
    return memcmp(&model, &s_fixed_output.model, sizeof(model)) == 0;
}

/* Read-only execution context check. This diagnostic is supported on the
 * RP2350 ARM thread context; never enable IRQs on behalf of a caller. */
static bool fixed_output_irq_ready(void)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && !defined(__riscv)
    uint32_t primask, basepri, faultmask, ipsr;
    pico_default_asm_volatile (
        "mrs %0, PRIMASK\n"
        "mrs %1, BASEPRI\n"
        "mrs %2, FAULTMASK\n"
        "mrs %3, IPSR\n"
        : "=r" (primask), "=r" (basepri), "=r" (faultmask), "=r" (ipsr)
        : : "memory");
    return !(primask | basepri | faultmask | ipsr);
#else
    return false;
#endif
}

/* Only a missing snapshot authorizes another observation. Do not carry a
 * successful predicate across rounds, or let ring contention hide a proved
 * model/session/clock/selftest failure. The selected model never changes in
 * the pre-enable and post-arm stages. The first complete observation is
 * unconditional; only a missing snapshot starts the extra-read time budget.
 * It gates another read, not a completed valid result. IRQ latency is not a
 * qualification failure or a claimed WCET. No retry runs in Core1 or with
 * IRQs off. */
static bool fixed_output_validate(uint32_t ring_unavailable,
    const vdc_dpll_manager_committed_model_t *selected,
    vdc_dpll_manager_committed_model_t *admitted)
{
    uint32_t retry_started = 0u;
    const uint32_t expected_session = selected ? selected->session :
        vdc_dpll_manager_feedback_session();
    uint32_t unavailable = ring_unavailable;
    for (uint32_t attempt = 0u; attempt < VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS; ++attempt) {
        if (!fixed_output_irq_ready()) {
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_IRQ_CONTEXT;
            return false;
        }
        if (attempt != 0u && (uint32_t)(time_us_32() - retry_started) >=
                VDC_FIXED_OUTPUT_SNAPSHOT_BUDGET_US) {
            s_fixed_output.last_reason = ring_unavailable + 2u;
            return false;
        }
        const uint32_t session = vdc_dpll_manager_feedback_session();
        const uint32_t stopped = fixed_output_stopped_state();
        if (stopped == 0u) {
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_NOT_STOPPED;
            return false;
        }
        vdc_dpll_manager_committed_model_t model;
        const bool available = vdc_dpll_manager_get_committed_model(&model);
        const uint32_t after_session = vdc_dpll_manager_feedback_session();
        if (!session || session != expected_session || session != after_session ||
            (available && (!model.token || model.session != session ||
                model.dco.valid != 1u || !model.dco.dco_update_seq ||
                model.dco.period_adjust_ppb <= -1000000000 ||
                (selected && memcmp(&model, selected, sizeof(model)) != 0)))) {
            s_fixed_output.last_reason = selected ? VDC_FIXED_OUTPUT_CHANGED : VDC_FIXED_OUTPUT_MODEL;
            return false;
        }
        if (!fixed_output_clock()) {
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_CLOCK;
            return false;
        }
        if (s_observation_self_test.active) {
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_SELFTEST;
            return false;
        }
        if (session != vdc_dpll_manager_feedback_session()) {
            s_fixed_output.last_reason = selected ? VDC_FIXED_OUTPUT_CHANGED : VDC_FIXED_OUTPUT_MODEL;
            return false;
        }
        if (!fixed_output_irq_ready()) {
            s_fixed_output.last_reason = VDC_FIXED_OUTPUT_IRQ_CONTEXT;
            return false;
        }
        if (stopped == 1u && available) {
            if (admitted) *admitted = model;
            return true;
        }
        unavailable = stopped == 2u ? ring_unavailable : ring_unavailable + 1u;
        /* Start once, after the first unknown observation. Unsigned elapsed
         * handles the normal low-word wrap; count also bounds a frozen clock. */
        if (attempt == 0u) retry_started = time_us_32();
    }
    s_fixed_output.last_reason = unavailable;
    return false;
}

static bool fixed_output_validate_start(void *unused)
{
    (void)unused;
    return fixed_output_validate(VDC_FIXED_OUTPUT_PRE_RING_UNAVAILABLE,
        &s_fixed_output.model, NULL);
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
    vdc_dpll_manager_committed_model_t model;
    if (!fixed_output_validate(VDC_FIXED_OUTPUT_INITIAL_RING_UNAVAILABLE, NULL, &model)) {
        goto done;
    }
    /* This observer is a low-rate finite diagnostic, not an unrestricted
     * pulse generator. Leave ample DMA service time between each pair. */
    if (period_ns < VDC_FIXED_OUTPUT_MIN_PERIOD_NS ||
        s_fixed_output.request_id == UINT32_MAX) {
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
    s_fixed_output.last_reason = VDC_FIXED_OUTPUT_RESOURCE_OR_ARGUMENT;
    sync_io_fixed_rate_runtime_t runtime;
    if (!sync_io_sma_observer_fixed_rate_arm(&request, &runtime,
            fixed_output_validate_start, NULL)) {
        /* The pre-enable callback may have captured a more precise cause.
         * Lower resource failures leave the generic reason intact. */
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
    if (!fixed_output_validate(VDC_FIXED_OUTPUT_POST_RING_UNAVAILABLE,
            &s_fixed_output.model, NULL)) {
        sync_io_sma_observer_fixed_rate_disarm();
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
