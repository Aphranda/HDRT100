#include "vdc_dpll_manager.h"
#include "vdc_dpll_manager_internal.h"
#include "vdc_priority_shared.h"
#include "vdc_time_mapping.h"
#include "vdc_model_projection.h"
#include "vdc_priority_tx.h"
#include "vdc_priority_rx.h"
#include "vdc_priority_match.h"
#include "vdc_priority_follow.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "board_identity.h"
#include "app.h"
#include "distributed_refmem.h"
#include "ota_crc32.h"
#include "osal.h"
#include "ota_ao.h"
#include "storage_manager.h"
#include "product_config.h"
#include "refmem_sync_frame.h"
#include "sync_io.h"
#include "tdma_runtime_owner.h"
#include "tdma_service.h"
#include "tdma_service_timing.h"
#include "vdc_domain.h"
#include "vdc_ring_observer.h"
#include "vdc_sync_io_adapter.h"
#include "vdc_tdma_payload.h"
#include "vdc_timestamp_clock.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#include "pico/time.h"
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) name
#endif

#define VDC_DPLL_MANAGER_SELF_TEST_CLEANUP_MARGIN_MS 250u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_MAGIC 0x4C504444u /* DDPL */
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_SCHEMA 6u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_MASTER 1u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_COMMAND 2u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_STATE 3u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_EVIDENCE 4u
#define VDC_DPLL_MANAGER_WAVEFORM_MAGIC 0x57524D53u /* SMRW */
#define VDC_DPLL_MANAGER_WAVEFORM_SCHEMA 4u
#define VDC_DPLL_MANAGER_WAVEFORM_BUFFER_COUNT 3u
#define VDC_DPLL_MANAGER_PHASE_ARM_AHEAD_PERIODS 2u


#include "vdc_dpll_manager_internal.h"

/* Core0 maintenance observation only. No work is added to Core1. The CAS
 * serializes SCPI and Core0 service without waiting or holding IRQs off. */
static uint32_t s_fixed_output_busy;
static vdc_dpll_manager_fixed_output_status_t s_fixed_output;
static uint32_t s_output_observation_mode;
static uint32_t s_output_marker_high_ns;

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

void vdc_fixed_output_service_core0(void)
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

static bool fixed_output_start(uint32_t period_ns, uint32_t high_ns,
    uint32_t pulse_count, uint32_t tick_period_ns, uint32_t mode,
    uint32_t marker_high_ns, uint32_t *request_id)
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
    /* Preserve FIXED's admitted low-rate range. SUBDIV is an explicit finite
     * interval experiment; its physical timing is qualified by measurement. */
    if ((mode == VDC_OUTPUT_OBSERVATION_FIXED &&
         period_ns < VDC_FIXED_OUTPUT_MIN_PERIOD_NS) ||
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
    s_output_observation_mode = mode;
    s_output_marker_high_ns = marker_high_ns;
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
    bool armed;
    if (mode == VDC_OUTPUT_OBSERVATION_SUBDIVISION_WINDOW) {
        armed = sync_io_sma_observer_subdivision_window_arm(&request, marker_high_ns,
            &runtime, fixed_output_validate_start, NULL);
    } else if (mode == VDC_OUTPUT_OBSERVATION_SUBDIVISION) {
        armed = sync_io_sma_observer_subdivision_arm(&request, marker_high_ns,
            &runtime, fixed_output_validate_start, NULL);
    } else {
        armed = sync_io_sma_observer_fixed_rate_arm(&request, &runtime,
            fixed_output_validate_start, NULL);
    }
    if (!armed) {
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

bool vdc_dpll_manager_fixed_output_start(uint32_t period_ns, uint32_t high_ns,
    uint32_t pulse_count, uint32_t tick_period_ns, uint32_t *request_id)
{
    return fixed_output_start(period_ns, high_ns, pulse_count, tick_period_ns,
        VDC_OUTPUT_OBSERVATION_FIXED, 0u, request_id);
}

bool vdc_dpll_manager_subdivision_output_start(uint32_t interval_ns,
    uint32_t pulse_high_ns, uint32_t subdivision_count, uint32_t marker_high_ns,
    uint32_t tick_period_ns, uint32_t *request_id)
{
    /* Reject before addition, including zero (a marker alone is not an
     * interval observation). Geometry and tick validation remain in SYNC_IO. */
    if (!subdivision_count ||
        subdivision_count >= SYNC_IO_RATE_SCHEDULE_MAX_PULSES) return false;
    return fixed_output_start(interval_ns, pulse_high_ns, subdivision_count + 1u,
        tick_period_ns, VDC_OUTPUT_OBSERVATION_SUBDIVISION, marker_high_ns,
        request_id);
}

bool vdc_dpll_manager_subdivision_window_start(uint32_t sync_period_ns,
    uint32_t interval_ns, uint32_t pulse_high_ns, uint32_t marker_high_ns,
    uint32_t tick_period_ns, uint32_t *request_id)
{
    if (!interval_ns || sync_period_ns % interval_ns) return false;
    const uint32_t pulse_count = sync_period_ns / interval_ns;
    if (pulse_count < 3u || pulse_count > SYNC_IO_RATE_SCHEDULE_MAX_PULSES) {
        return false;
    }
    return fixed_output_start(interval_ns, pulse_high_ns, pulse_count,
        tick_period_ns, VDC_OUTPUT_OBSERVATION_SUBDIVISION_WINDOW, marker_high_ns,
        request_id);
}

static bool fixed_output_stop(uint32_t mode)
{
    if (!fixed_output_enter()) return false;
    if (s_output_observation_mode != mode) {
        fixed_output_leave();
        return false;
    }
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

bool vdc_dpll_manager_fixed_output_stop(void)
{
    return fixed_output_stop(VDC_OUTPUT_OBSERVATION_FIXED);
}

bool vdc_dpll_manager_subdivision_output_stop(void)
{
    return fixed_output_stop(VDC_OUTPUT_OBSERVATION_SUBDIVISION);
}

bool vdc_dpll_manager_subdivision_window_stop(void)
{
    return fixed_output_stop(VDC_OUTPUT_OBSERVATION_SUBDIVISION_WINDOW);
}

bool vdc_dpll_manager_get_fixed_output(vdc_dpll_manager_fixed_output_status_t *out)
{
    if (!out || !fixed_output_enter()) return false;
    const bool stopped = s_output_observation_mode == VDC_OUTPUT_OBSERVATION_FIXED &&
        fixed_output_stopped_state() == 1u;
    if (stopped) {
        if (s_fixed_output.request_id) fixed_output_runtime();
        s_fixed_output.schema = 1u;
        *out = s_fixed_output;
    }
    fixed_output_leave();
    return stopped;
}

static bool subdivision_output_get(uint32_t mode,
    vdc_dpll_manager_subdivision_output_status_t *out)
{
    if (!out || !fixed_output_enter()) return false;
    const bool stopped = s_output_observation_mode == mode &&
        fixed_output_stopped_state() == 1u;
    if (stopped) {
        fixed_output_runtime();
        out->batch = s_fixed_output;
        out->mode = s_output_observation_mode;
        out->marker_high_ns = s_output_marker_high_ns;
        /* Failed requests may have an unsupported/zero tick. No encoded
         * marker exists until the backend has successfully armed. */
        out->marker_high_ticks = s_fixed_output.system_clock_hz
            ? (uint32_t)(((uint64_t)s_output_marker_high_ns +
                s_fixed_output.tick_period_ns - 1u) / s_fixed_output.tick_period_ns)
            : 0u;
        const uint32_t markers = mode == VDC_OUTPUT_OBSERVATION_SUBDIVISION_WINDOW
            ? 2u : 1u;
        out->subdivision_count = s_fixed_output.pulse_count - markers;
    }
    fixed_output_leave();
    return stopped;
}

bool vdc_dpll_manager_get_subdivision_output(
    vdc_dpll_manager_subdivision_output_status_t *out)
{
    return subdivision_output_get(VDC_OUTPUT_OBSERVATION_SUBDIVISION, out);
}

bool vdc_dpll_manager_get_subdivision_window(
    vdc_dpll_manager_subdivision_output_status_t *out)
{
    return subdivision_output_get(VDC_OUTPUT_OBSERVATION_SUBDIVISION_WINDOW, out);
}

/* VDC capability client; SYNC_IO alone touches output PIO/DMA/GPIO. */
#include "vdc_output_edge_cursor.h"
#include "vdc_timer1_coordinate.h"
#include "sync_pulse_sliced_encode.h"
#include "tdma_seat_mount_store.h"
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
static vdc_run_output_slices_t s_run_output_slices = {0u, 500u, 100u, 200u};

static bool run_output_slices_stopped(void *context)
{
    if (__atomic_load_n(&s_run_output_request, __ATOMIC_ACQUIRE)) return false;
    s_run_output_slices = *(const vdc_run_output_slices_t *)context;
    return true;
}

static bool run_output_update_slices(bool change_enable, bool enabled,
    uint32_t interval_ns, uint32_t high_ns, uint32_t marker_ns)
{
    uint32_t idle = 0u;
    if (get_core_num() != 0u || !__atomic_compare_exchange_n(&s_run_output_busy,
            &idle, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return false;
    vdc_run_output_slices_t next = s_run_output_slices;
    if (change_enable) next.enabled = enabled ? 1u : 0u;
    else { next.interval_ns = interval_ns; next.high_ns = high_ns; next.marker_ns = marker_ns; }
    /* Nominal integer TIMER1 geometry. Every cycle's marker continues to
     * follow the absolute DCO plan; quantization within that cycle is explicit. */
    const uint64_t interval_ticks = (uint64_t)next.interval_ns * BOARD_SYS_CLOCK_HZ;
    const uint64_t high_ticks = (uint64_t)next.high_ns * BOARD_SYS_CLOCK_HZ;
    const uint64_t marker_ticks = (uint64_t)next.marker_ns * BOARD_SYS_CLOCK_HZ;
    sync_pulse_sliced_config_t geometry;
    const bool valid = next.interval_ns && VDC_DOMAIN_DEFAULT_PERIOD_NS % next.interval_ns == 0u &&
        VDC_DOMAIN_DEFAULT_PERIOD_NS / next.interval_ns >= 3u &&
        next.high_ns && next.high_ns < next.marker_ns && next.marker_ns < next.interval_ns &&
        interval_ticks % UINT64_C(1000000000) == 0u &&
        high_ticks % UINT64_C(1000000000) == 0u && marker_ticks % UINT64_C(1000000000) == 0u &&
        sync_pulse_sliced_configure(BOARD_SYS_CLOCK_HZ,
            (uint32_t)((uint64_t)VDC_DOMAIN_DEFAULT_PERIOD_NS * BOARD_SYS_CLOCK_HZ / UINT64_C(1000000000)),
            (uint32_t)(interval_ticks / UINT64_C(1000000000)),
            (uint32_t)(high_ticks / UINT64_C(1000000000)),
            (uint32_t)(marker_ticks / UINT64_C(1000000000)), &geometry);
    const bool ok = valid && tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
        run_output_slices_stopped, &next);
    __atomic_store_n(&s_run_output_busy, 0u, __ATOMIC_RELEASE);
    return ok;
}

bool vdc_run_output_set_slices_enabled(bool enabled)
{ return run_output_update_slices(true, enabled, 0u, 0u, 0u); }

bool vdc_run_output_configure_slices(uint32_t interval_ns, uint32_t high_ns, uint32_t marker_ns)
{ return run_output_update_slices(false, false, interval_ns, high_ns, marker_ns); }

bool vdc_run_output_get_slices(vdc_run_output_slices_t *out)
{
    uint32_t idle = 0u;
    if (!out || get_core_num() != 0u || !__atomic_compare_exchange_n(&s_run_output_busy,
            &idle, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return false;
    *out = s_run_output_slices;
    __atomic_store_n(&s_run_output_busy, 0u, __ATOMIC_RELEASE);
    return true;
}
static uint32_t s_run_output_service_sequence, s_run_output_current_service_sequence;
static uint64_t s_run_output_current_service_tick;
static uint64_t s_run_output_last_raw_tick;
static uint64_t s_run_output_slice_tail_ticks;
#define VDC_RUN_OUTPUT_PLAN_STEP_EDGES 4u
static bool s_run_output_coordinate_valid;
#define VDC_RUN_OUTPUT_MAX_COORDINATE_SECONDS 32u

static bool run_output_coordinate_contains(uint64_t tick)
{
    return s_run_output_coordinate_valid && tick>=s_run_output.initial_raw_tick &&
        (!s_run_output.duration_ms ||
        tick-s_run_output.initial_raw_tick<=
            (uint64_t)BOARD_SYS_CLOCK_HZ*VDC_RUN_OUTPUT_MAX_COORDINATE_SECONDS);
}
static bool run_output_observe(uint64_t tick)
{
    if (!run_output_coordinate_contains(tick) || tick<s_run_output_last_raw_tick) return false;
    s_run_output_last_raw_tick=tick;
    return true;
}
/* A private uncommitted suffix, never a DMA source. The whole-client gate
 * covers this cache as well as the public metadata. Preparing it does not
 * consume ordinals; a changed model invalidates it before admission. */
static struct {
    sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_MAX_EDGES];
    uint64_t predecessor_ordinal, predecessor_fall, last_local_ns, target_vdc_ns;
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
        !vdc_timestamp_clock_is_current(BOARD_SYS_CLOCK_HZ)) return false;
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
    const vdc_run_output_slices_t slices = s_run_output_slices;
    const uint32_t high_ns = slices.enabled ? slices.marker_ns : args[1];
    const uint64_t high_ticks=((uint64_t)high_ns*BOARD_SYS_CLOCK_HZ+999999999u)/1000000000u;
    if (high_ticks > UINT32_MAX) return false;
    if (slices.enabled) {
        if (args[0] != VDC_DOMAIN_DEFAULT_PERIOD_NS ||
            !sync_io_run_output_prepare_sliced(BOARD_SYS_CLOCK_HZ, args[2],
                (uint32_t)((uint64_t)args[0] * BOARD_SYS_CLOCK_HZ / UINT64_C(1000000000)),
                (uint32_t)((uint64_t)slices.interval_ns * BOARD_SYS_CLOCK_HZ / UINT64_C(1000000000)),
                (uint32_t)((uint64_t)slices.high_ns * BOARD_SYS_CLOCK_HZ / UINT64_C(1000000000)),
                (uint32_t)high_ticks, &generation)) return false;
    } else if (!sync_io_run_output_prepare_uniform(
            BOARD_SYS_CLOCK_HZ,args[2],(uint32_t)high_ticks,&generation)) return false;
    memset(&s_run_output,0,sizeof(s_run_output));
    memset(&s_run_output_pending,0,sizeof(s_run_output_pending));
    s_run_output_coordinate_valid=false;
    s_run_output.timebase=VDC_RUN_OUTPUT_TIMEBASE_TIMER1_NS;
    s_run_output.last_outcome=VDC_RUN_OUTPUT_OUTCOME_NONE;
    s_run_output_service_sequence=0u;
    s_run_output_current_service_sequence=0u;
    s_run_output_current_service_tick=0u;
    s_run_output_last_raw_tick=0u;
    s_run_output.request=generation; s_run_output.session=session;
    s_run_output.prepared_config=ring.config_seq;
    s_run_output.period_ns=args[0]; s_run_output.high_ns=high_ns; s_run_output.duration_ms=args[2];
    s_run_output.slices = slices;
    s_run_output_slice_tail_ticks = slices.enabled ?
        (uint64_t)(args[0] / slices.interval_ns - 2u) *
            ((uint64_t)slices.interval_ns * BOARD_SYS_CLOCK_HZ / UINT64_C(1000000000)) +
            ((uint64_t)slices.high_ns * BOARD_SYS_CLOCK_HZ / UINT64_C(1000000000)) : 0u;
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
    /* Split so a bench readout names the rejected precondition (temporary). */
    if (get_core_num()!=0u) { tdma_seat_mount_store_note_reject(171u); return false; }
    if (!request) { tdma_seat_mount_store_note_reject(172u); return false; }
    if (period_ns<1000000u || period_ns>100000000u || high_ns<100u ||
        high_ns>period_ns/2u || duration_ms>20000u) {
        tdma_seat_mount_store_note_reject(173u); return false;
    }
    if (!__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) {
        tdma_seat_mount_store_note_reject(174u); return false;
    }
    const uint32_t args[]={period_ns,high_ns,duration_ms};
    const bool ok=tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
        run_output_prepare_stopped,(void *)args);
    if (!ok) {
        /* Only claim the channel when the maintenance gate left no reason of its own. */
        uint32_t seen=0u, seen_gen=0u;
        tdma_seat_mount_store_status(&seen,&seen_gen);
        if (seen==0u) tdma_seat_mount_store_note_reject(175u);
    }
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
    s_run_output_coordinate_valid=false;
    sync_io_run_output_cancel();
}

void run_output_release_core0(void)
{
    if (get_core_num()!=0u) return;
    const uint32_t candidate=__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE);
    sync_io_run_output_snapshot_t hw;
    /* Active output needs no Core0 client ownership. This speculative read
     * only skips work; release still requires the same request and a fresh
     * retired backend snapshot under the whole-client gate below. */
    if (!candidate || !sync_io_run_output_snapshot(&hw) ||
        hw.generation!=candidate || hw.state!=SYNC_IO_RUN_OUTPUT_RETIRED) return;
    uint32_t idle=0u;
    if (!__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,true,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    const uint32_t request=__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE);
    if (request==candidate && sync_io_run_output_snapshot(&hw) && hw.generation==request &&
        hw.state==SYNC_IO_RUN_OUTPUT_RETIRED && sync_io_run_output_release(request))
        __atomic_store_n(&s_run_output_request,0u,__ATOMIC_RELEASE);
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
}

bool vdc_run_output_observe_core1(vdc_run_output_observation_t *out)
{
    uint32_t idle=0u;
    if (!out || get_core_num()!=1u ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,true,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return false;
    sync_io_run_output_snapshot_t hw;
    const bool ok=s_run_output.request && sync_io_run_output_snapshot(&hw) &&
        hw.generation==s_run_output.request;
    if (ok) *out=(vdc_run_output_observation_t){
        .last_ordinal=hw.last_ordinal, .service_last_tick=hw.service_last_tick,
        .submit_last_tick=hw.submit_last_tick, .request=s_run_output.request,
        .session=s_run_output.session, .ring_config=s_run_output.ring_config,
        .state=hw.state, .reason=hw.reason, .service_observations=hw.service_observations};
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
    return ok;
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
        s_run_output_coordinate_valid=false; return;
    }
    s_run_output_current_service_sequence=s_run_output_service_sequence!=UINT32_MAX ?
        ++s_run_output_service_sequence : UINT32_MAX;
    s_run_output.service_sequence=s_run_output_current_service_sequence;
    s_run_output_current_service_tick=hw.service_observations ? hw.service_last_tick : 0u;
    phase=hw.state==SYNC_IO_RUN_OUTPUT_RUNNING ? VDC_RUN_OUTPUT_RUNNING_PHASE : VDC_RUN_OUTPUT_PREPARED_PHASE;
    if (vdc_dpll_manager_feedback_session()!=s_run_output.session ||
        app_realtime_cycle_cycles_core1()!=s_run_output.schedule_cycles) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        if (s_run_output.binding_rejects!=UINT32_MAX) ++s_run_output.binding_rejects;
        run_output_cancel_owned(); return;
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
        if (s_run_output.binding_rejects!=UINT32_MAX) ++s_run_output.binding_rejects;
        run_output_cancel_owned(); return;
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
        if (s_run_output.binding_rejects!=UINT32_MAX) ++s_run_output.binding_rejects;
        run_output_cancel_owned(); return;
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
            if (s_run_output.binding_rejects!=UINT32_MAX) ++s_run_output.binding_rejects;
            run_output_cancel_owned();
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
    /* DCO local time and output deadlines share TIMER1's origin. TIMER0
     * never participates; fresh raw observations still guard admission. */
    if (!vdc_timestamp_clock_is_current(BOARD_SYS_CLOCK_HZ)) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        if (s_run_output.binding_rejects!=UINT32_MAX) ++s_run_output.binding_rejects;
        run_output_cancel_owned(); return;
    }
    uint64_t now;
    if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,&now)) {
        run_output_note(phase,VDC_RUN_OUTPUT_BRIDGE_UNAVAILABLE); return;
    }
    if (!s_run_output_coordinate_valid) {
        if (!first || cached_only || !vdc_timer1_ticks_to_ns(
                BOARD_SYS_CLOCK_HZ,now,false,&s_run_output.initial_local_ns)) {
            run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); run_output_cancel_owned(); return;
        }
        s_run_output.initial_raw_tick=now;
        s_run_output_coordinate_valid=true;
    }
    if (!run_output_observe(now)) {
        run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); run_output_cancel_owned(); return;
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
            if (now==UINT64_MAX || !vdc_timer1_ticks_to_ns(
                    BOARD_SYS_CLOCK_HZ,now+1u,true,&local_now)) {
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
            run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED);
            if (s_run_output.plan_rejects!=UINT32_MAX) ++s_run_output.plan_rejects;
            return;
        }
        const uint32_t end=batch ? s_run_output.block_edges :
            s_run_output.block_edges-s_run_output_pending.planned>VDC_RUN_OUTPUT_PLAN_STEP_EDGES ?
                s_run_output_pending.planned+VDC_RUN_OUTPUT_PLAN_STEP_EDGES : s_run_output.block_edges;
        for (uint32_t i=s_run_output_pending.planned;i<end;++i) {
            vdc_output_edge_plan_t plan;
            uint64_t raw;
            bool planned;
            if (batch && i==0u) { plan=first_plan; planned=true; }
            else if (batch) planned=vdc_output_edge_cursor_next(&cursor,&plan);
            else planned=vdc_output_edge_plan(&model.dco,0u,s_run_output.period_ns,
                s_run_output_pending.next_ordinal,s_run_output_pending.last_local_ns,s_run_output.delay_ns,&plan);
            if (!planned ||
                !vdc_timer1_ns_to_ticks(BOARD_SYS_CLOCK_HZ,plan.physical_local_ns,true,&raw) ||
                !run_output_coordinate_contains(raw) ||
                raw>UINT64_MAX-high_ticks || !run_output_coordinate_contains(raw+high_ticks) ||
                plan.physical_local_ns>UINT64_MAX-s_run_output.high_ns ||
                plan.ordinal==UINT64_MAX) {
                s_run_output_pending.planned=0u;
                run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED);
                if (s_run_output.plan_rejects!=UINT32_MAX) ++s_run_output.plan_rejects;
                return;
            }
            s_run_output_pending.edges[i]=(sync_io_run_output_edge_t){.rising_tick=raw,
                .falling_tick=raw+high_ticks,.ordinal=plan.ordinal,.model_token=model.token};
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
        if (s_run_output.binding_rejects!=UINT32_MAX) ++s_run_output.binding_rejects;
        run_output_cancel_owned(); return;
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
    if (!run_output_observe(now)) {
        run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); run_output_cancel_owned(); return;
    }
    if (!first && tail>now && tail-now>low_ticks) {
        if (s_run_output.refill_waits!=UINT32_MAX) ++s_run_output.refill_waits;
        return;
    }
    const sync_io_run_output_edge_t *last_edge = &s_run_output_pending.edges[s_run_output.block_edges-1u];
    if (s_run_output.slices.enabled && last_edge->rising_tick > UINT64_MAX-s_run_output_slice_tail_ticks) {
        run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED);run_output_cancel_owned();return;
    }
    const uint64_t next_tail=s_run_output.slices.enabled ?
        last_edge->rising_tick+s_run_output_slice_tail_ticks : last_edge->falling_tick;
    if (first && (next_tail<s_run_output_pending.edges[0].rising_tick ||
        next_tail-s_run_output_pending.edges[0].rising_tick>commit_ticks)) {
        s_run_output_pending.valid=false; s_run_output_pending.planned=0u;
        run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED);
        if (s_run_output.plan_rejects!=UINT32_MAX) ++s_run_output.plan_rejects;
        return;
    }
    if (!first && (next_tail>UINT64_MAX-offset || now>UINT64_MAX-commit_ticks)) {
        run_output_note(phase,VDC_RUN_OUTPUT_PLAN_REJECTED); run_output_cancel_owned(); return;
    }
    if (!first && next_tail+offset>now+commit_ticks) {
        if (s_run_output.commit_waits!=UINT32_MAX) ++s_run_output.commit_waits;
        return;
    }
    /* A cache hit skips planning, not TIMER1's clock lifetime check. */
    if (cached && !vdc_timestamp_clock_is_current(BOARD_SYS_CLOCK_HZ)) {
        run_output_note(phase,VDC_RUN_OUTPUT_BINDING_CANCELLED);
        if (s_run_output.binding_rejects!=UINT32_MAX) ++s_run_output.binding_rejects;
        run_output_cancel_owned(); return;
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
        run_output_note(phase,VDC_RUN_OUTPUT_SUBMIT_REJECTED);
        if (s_run_output.plan_rejects!=UINT32_MAX) ++s_run_output.plan_rejects;
        return;
    }
    s_run_output.ring_config=ring.config_seq; s_run_output.role_generation=model.role_generation;
    s_run_output.clock_epoch=model.clock_epoch_id; s_run_output.clock_run=model.clock_run_id;
    s_run_output.last_local_ns=s_run_output_pending.last_local_ns;
    s_run_output.last_target_vdc_ns=s_run_output_pending.target_vdc_ns;
    if (cached && s_run_output.cache_hits!=UINT32_MAX) ++s_run_output.cache_hits;
    s_run_output_pending.valid=false;
    s_run_output_pending.planned=0u;
    if (s_run_output.blocks_planned!=UINT32_MAX) ++s_run_output.blocks_planned;
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

__attribute__((noinline)) uint32_t VDC_RUN_OUTPUT_HOT(vdc_run_output_service_planned_core1)(void)
{
    uint32_t idle=0u;
    if (get_core_num()!=1u ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,true,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return 0u;
    const uint32_t request=__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE);
    const uint64_t admitted=s_run_output.last_local_ns;
    const uint32_t planned=s_run_output.partial_plan_steps;
    if (request && s_run_output.planned_calls!=UINT32_MAX) ++s_run_output.planned_calls;
    run_output_service_owned(false);
    if (request && s_run_output.last_local_ns!=admitted && s_run_output.planned_submissions!=UINT32_MAX)
        ++s_run_output.planned_submissions;
    if (request && s_run_output.partial_plan_steps!=planned && s_run_output.planned_rebuilds!=UINT32_MAX)
        ++s_run_output.planned_rebuilds;
    __atomic_store_n(&s_run_output_busy,0u,__ATOMIC_RELEASE);
    return request;
}

__attribute__((noinline)) void VDC_RUN_OUTPUT_HOT(vdc_run_output_note_planned_wall_core1)(uint32_t request,
    uint32_t cycles,uint32_t budget_cycles)
{
    uint32_t idle=0u;
    if (get_core_num()!=1u || !request ||
        !__atomic_compare_exchange_n(&s_run_output_busy,&idle,1u,true,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    if (request==__atomic_load_n(&s_run_output_request,__ATOMIC_ACQUIRE)) {
        if (s_run_output.planned_wall_samples!=UINT32_MAX) ++s_run_output.planned_wall_samples;
        if (cycles>s_run_output.planned_wall_max_cycles) s_run_output.planned_wall_max_cycles=cycles;
        if (cycles>budget_cycles && s_run_output.planned_budget_overruns!=UINT32_MAX)
            ++s_run_output.planned_budget_overruns;
    }
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
    const uint64_t admitted=s_run_output.last_local_ns;
    if (tracked && s_run_output.fast_calls!=UINT32_MAX) ++s_run_output.fast_calls;
    run_output_service_owned(true);
    if (tracked && s_run_output.last_local_ns!=admitted && s_run_output.fast_submissions!=UINT32_MAX)
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

/* Host-test seam: the harness drives this module's private state directly, so
 * the storage class is lifted only when the unit is built for tests. */
#if defined(DHRT100_UNIT_TEST)
#define VDC_TEST_STATIC
#else
#define VDC_TEST_STATIC static
#endif

/* Core1 consumes one hardware window once, inside the committed-model guard.
 * No transport, configuration, Flash, or reference resource ownership here. */
VDC_TEST_STATIC vdc_reference_discipline_status_t s_reference_discipline;
VDC_TEST_STATIC uint32_t s_reference_discipline_guard;
VDC_TEST_STATIC uint32_t s_reference_discipline_words[sizeof(vdc_reference_discipline_status_t) / 4u];
VDC_TEST_STATIC uint32_t s_reference_bound_config, s_reference_bound_profile, s_reference_bound_servo;
VDC_TEST_STATIC uint64_t s_reference_bound_raw;
VDC_TEST_STATIC bool s_reference_filter_valid;

static void reference_discipline_publish(void)
{
    (void)__atomic_add_fetch(&s_reference_discipline_guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0; i < sizeof(s_reference_discipline) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_reference_discipline + i * 4u, 4u);
        __atomic_store_n(s_reference_discipline_words + i, word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&s_reference_discipline_guard, 1u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_reference_discipline(vdc_reference_discipline_status_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_reference_discipline_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_reference_discipline_status_t value;
    for (size_t i = 0; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_reference_discipline_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + i * 4u, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_reference_discipline_guard, __ATOMIC_ACQUIRE)) return false;
    /* Requested enable is distinct from the Core1 acknowledged request. */
    value.schema = VDC_REFERENCE_DISCIPLINE_SCHEMA;
    value.enabled = __atomic_load_n(&s_reference_discipline_request, __ATOMIC_ACQUIRE) & 1u;
    *out = value;
    return true;
}

static void reference_discipline_hold(uint32_t reason, bool retire)
{
    s_reference_discipline.state = retire ? VDC_REFERENCE_DISCIPLINE_RETIRED : VDC_REFERENCE_DISCIPLINE_HOLD;
    s_reference_discipline.reason = reason;
    s_reference_filter_valid = false;
    reference_discipline_publish();
}

void reference_discipline_service_core1(uint32_t session)
{
    const uint32_t request = __atomic_load_n(&s_reference_discipline_request, __ATOMIC_ACQUIRE);
    vdc_reference_discipline_status_t *s = &s_reference_discipline;
    if (request != s->request) {
        const uint32_t generation = __atomic_load_n(&s_reference_discipline_capture_generation, __ATOMIC_RELAXED);
        uint32_t armed[5];
        for (size_t i = 0; i < 5u; ++i)
            armed[i] = __atomic_load_n(s_reference_discipline_armed + i, __ATOMIC_RELAXED);
        if (request != __atomic_load_n(&s_reference_discipline_request, __ATOMIC_ACQUIRE)) return;
        if (request & 1u) {
            *s = (vdc_reference_discipline_status_t){.schema = VDC_REFERENCE_DISCIPLINE_SCHEMA,
                .request = request, .enabled = 1u, .state = VDC_REFERENCE_DISCIPLINE_WAIT,
                .reference_generation = generation, .baseline_ppb = s_vdc_domain.reference_baseline_ppb,
                .config_generation = armed[0], .config_crc32 = armed[1],
                .config = {armed[2], armed[3], armed[4]}};
        } else {
            s->request = request; s->enabled = 0u; s->state = VDC_REFERENCE_DISCIPLINE_OFF;
        }
        s_reference_filter_valid = false;
        reference_discipline_publish();
    }
    if (!(request & 1u) || s->state == VDC_REFERENCE_DISCIPLINE_RETIRED) return;
    if (!session) {
        if (s->session) reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STOP, true);
        return;
    }
    sync_io_reference_snapshot_t m;
    if (!sync_io_reference_get_snapshot(&m)) return;
    if (m.generation != s->reference_generation || m.state != SYNC_IO_REFERENCE_RUNNING ||
        m.reason != SYNC_IO_REFERENCE_OK) {
        if (s->state != VDC_REFERENCE_DISCIPLINE_HOLD)
            reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_NO_INPUT, false);
        return;
    }
    uint64_t now;
    if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &now)) return;
    if (!m.valid || !m.sample_seq) {
        if (s->session && s->state != VDC_REFERENCE_DISCIPLINE_HOLD)
            reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_NO_INPUT, false);
        return;
    }
    if (m.tick_hz != BOARD_SYS_CLOCK_HZ || m.completed_raw > now ||
        now - m.completed_raw > (uint64_t)m.tick_hz * m.config.timeout_ms / 1000u) {
        if (s->reason != VDC_REFERENCE_DISCIPLINE_STALE)
            reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STALE, false);
        return;
    }
    if (m.sample_seq == s->sample_seq) return;
    if (m.sample_seq < s->sample_seq) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_BINDING, true); return;
    }
    s->sample_seq = m.sample_seq;
    s->measured_ppb = m.frequency_error_ppb;
    tdma_ring_clock_snapshot_t ring;
    tdma_origin_raw_reference_t origin;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) return;
    if (!ring.enabled || !ring.adapter_started) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STOP, s->session != 0u); return;
    }
    /* MASTER is bound to its retained autonomous TX record, not the follower
     * RX observer persona. The origin getter rejects inactive/retired records. */
    if (!tdma_runtime_owner_get_origin_raw_reference(&origin)) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_NO_INPUT, false); return;
    }
    if (!origin.epoch || !origin.published_version || (origin.published_version & 1u) ||
        origin.tick_hz != m.tick_hz || origin.timer_upper < origin.timer_lower ||
        origin.timer_upper > now || now - origin.timer_lower > (uint64_t)m.tick_hz * 2u) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STALE, false); return;
    }
    if (!s_vdc_domain.ready || !s_vdc_domain.control.profile.valid ||
        s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_MASTER ||
        ring.local_slot_id != ring.reference_slot_id ||
        ring.local_slot_id != s_vdc_domain.schedule.local_slot_id ||
        ring.reference_slot_id != s_vdc_domain.schedule.reference_slot_id ||
        ring.config_seq != ring.applied_config_seq ||
        ring.schedule_crc32 != s_vdc_domain.schedule.schedule_crc32) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_BINDING, s->session != 0u); return;
    }
    if (!s->session) {
        s->session = session;
        s->role_generation = s_vdc_domain.control.profile.generation;
        s->clock_epoch = s_vdc_domain.clock.epoch_id;
        s->clock_run = s_vdc_domain.clock.run_id;
        s->origin_epoch = origin.epoch;
        s->origin_sequence = origin.sequence;
        s_reference_bound_config = ring.config_seq;
        s_reference_bound_profile = s_vdc_tdma_service->ring_runtime.ring_profile_crc32;
        s_reference_bound_servo = s_vdc_domain.servo.servo_profile_crc32;
        s_reference_bound_raw = now;
        s->state = VDC_REFERENCE_DISCIPLINE_WAIT;
        reference_discipline_publish(); return;
    }
    if (session != s->session || origin.epoch != s->origin_epoch ||
        ring.config_seq != s_reference_bound_config ||
        s_vdc_tdma_service->ring_runtime.ring_profile_crc32 != s_reference_bound_profile ||
        s_vdc_domain.servo.servo_profile_crc32 != s_reference_bound_servo ||
        s_vdc_domain.control.profile.generation != s->role_generation ||
        s_vdc_domain.clock.epoch_id != s->clock_epoch || s_vdc_domain.clock.run_id != s->clock_run) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_BINDING, true); return;
    }
    s->origin_sequence = origin.sequence;
    /* Reconstruct the actual first edge from the completed TIMER1 low word.
     * Exclude windows straddling this RUN binding, including restart races. */
    const uint64_t age = (uint32_t)((uint32_t)m.completed_raw - m.end_raw32);
    const uint64_t span = age + m.elapsed_ticks + m.pio_bias_ticks;
    if (span > m.completed_raw || m.completed_raw - span < s_reference_bound_raw) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STALE, false); return;
    }
    if ((int64_t)m.frequency_error_ppb < -(int64_t)s->config.max_ppb ||
        (int64_t)m.frequency_error_ppb > (int64_t)s->config.max_ppb) {
        if (s->rejected != UINT32_MAX) ++s->rejected;
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_RANGE, false); return;
    }
    const int64_t difference = (int64_t)m.frequency_error_ppb - s->filtered_ppb;
    const uint32_t divisor = s->config.filter_divisor > 1u ? s->config.filter_divisor : 1u;
    const int64_t filter_step = difference / divisor;
    const int32_t filtered = s_reference_filter_valid
        ? (int32_t)((int64_t)s->filtered_ppb + (filter_step ? filter_step : (difference > 0 ? 1 : difference < 0 ? -1 : 0)))
        : m.frequency_error_ppb;
    const int32_t baseline = s_vdc_domain.reference_baseline_ppb;
    const uint64_t step_budget = (uint64_t)s->config.slew_ppb_per_s * m.config.window_ms / 1000u;
    const int64_t step_limit = step_budget > INT32_MAX ? INT32_MAX : (int64_t)step_budget;
    int64_t step = (int64_t)filtered - baseline;
    if (step > step_limit) step = step_limit;
    if (step < -step_limit) step = -step_limit;
    const int32_t target = (int32_t)((int64_t)baseline + step);
    const uint64_t local_ns = vdc_dpll_manager_now_ns();
    if (local_ns == UINT64_MAX || request != __atomic_load_n(&s_reference_discipline_request, __ATOMIC_ACQUIRE) ||
        !vdc_domain_apply_reference_baseline(&s_vdc_domain, target, local_ns)) {
        if (s->rejected != UINT32_MAX) ++s->rejected;
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_MODEL, false); return;
    }
    s->filtered_ppb = filtered;
    s_reference_filter_valid = true;
    s->baseline_ppb = s_vdc_domain.reference_baseline_ppb;
    s->dco_update_seq = s_vdc_domain.dco.dco_update_seq;
    if (s->accepted != UINT32_MAX) ++s->accepted;
    if (step && s->applied != UINT32_MAX) ++s->applied;
    s->state = VDC_REFERENCE_DISCIPLINE_TRACK;
    s->reason = VDC_REFERENCE_DISCIPLINE_OK;
    if (step && s_published_snapshot_valid) {
        /* The actuator is committed even between servo and finalize beats.
         * Publish its model pair without promoting unfinished evidence or
         * invoking the capture side effects of the full runtime publisher. */
        (void)__atomic_add_fetch(&s_published_snapshot_guard, 1u, __ATOMIC_ACQ_REL);
        s_published_snapshot.clock = s_vdc_domain.clock;
        s_published_snapshot.dco = s_vdc_domain.dco;
        (void)__atomic_add_fetch(&s_published_snapshot_guard, 1u, __ATOMIC_RELEASE);
    }
    reference_discipline_publish();
}
