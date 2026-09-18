/* The sole TDMA owner consumes Calibration's explicit diagnostic trial.
 * This file never constructs a product timing grant. */
#include "tdma_origin_calibration_crc.h"
#include "tdma_origin_release.h"
#include "project_config.h"
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define TDMA_ORIGIN_OWNER_RAM __not_in_flash("tdma_origin_owner")
#else
#define TDMA_ORIGIN_OWNER_RAM
#endif

static calibration_origin_timing_t s_origin_trial;
static uint32_t s_origin_consumed_epoch;
static bool s_origin_trial_fault;
static tdma_origin_blackout_t s_origin_blackout;
static tdma_origin_handoff_t s_origin_handoff;

/* One writer per object: Core0 publishes request/attempt, Core1 publishes
 * READY/result and retires requests. Request sequence IS its seqlock guard;
 * it never wraps, so even a delayed old publication cannot authorize reuse. */
static struct { uint32_t sequence, trial_epoch, config_seq; } s_origin_release_request;
static tdma_origin_release_attempt_t s_origin_release_attempt;
static uint32_t s_origin_release_ready_epoch, s_origin_release_consumed_seq;
static uint32_t s_origin_release_terminal_epoch;
static tdma_origin_release_result_t s_origin_release_result;

static bool tdma_runtime_owner_origin_release_reject(tdma_origin_release_attempt_reason_t reason)
{
    s_origin_release_attempt.reason = reason;
    if (s_origin_release_attempt.rejected != UINT32_MAX) ++s_origin_release_attempt.rejected;
    return false;
}

bool tdma_runtime_owner_request_origin_release(uint32_t trial_epoch, uint32_t config_seq,
    uint32_t *request_seq)
{
    tdma_origin_release_attempt_t *attempt = &s_origin_release_attempt;
    if (attempt->attempts != UINT32_MAX) ++attempt->attempts;
    attempt->trial_epoch = trial_epoch;
    attempt->config_seq = config_seq;
    attempt->request_seq = 0u;
    if (request_seq == NULL || trial_epoch == 0u || (trial_epoch & 1u) || config_seq == 0u)
        return tdma_runtime_owner_origin_release_reject(TDMA_ORIGIN_RELEASE_ATTEMPT_ARGUMENT);
    *request_seq = 0u;
    const uint32_t sequence = __atomic_load_n(&s_origin_release_request.sequence, __ATOMIC_ACQUIRE);
    if ((sequence & 1u) || sequence >= UINT32_MAX - 1u)
        return tdma_runtime_owner_origin_release_reject(TDMA_ORIGIN_RELEASE_ATTEMPT_EXHAUSTED);
    if (sequence && __atomic_load_n(&s_origin_release_request.trial_epoch, __ATOMIC_RELAXED) == trial_epoch)
        return tdma_runtime_owner_origin_release_reject(TDMA_ORIGIN_RELEASE_ATTEMPT_DUPLICATE);
    calibration_origin_timing_t trial;
    tdma_ring_runtime_snapshot_t ring;
    if (!calibration_manager_origin_get_timing(&trial) || !trial.enabled || trial.epoch != trial_epoch ||
        trial.config_seq != config_seq || trial.diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_DEFER_RELEASE ||
        !tdma_runtime_owner_get_ring_snapshot(&ring) || !ring.enabled || !ring.adapter_started ||
        ring.config_seq != config_seq || ring.applied_config_seq != config_seq)
        return tdma_runtime_owner_origin_release_reject(TDMA_ORIGIN_RELEASE_ATTEMPT_STALE);
    if (trial.expires_ticks && vdc_timestamp_clock_read_ticks64() >= trial.expires_ticks)
        return tdma_runtime_owner_origin_release_reject(TDMA_ORIGIN_RELEASE_ATTEMPT_EXPIRED);
    if (__atomic_load_n(&s_origin_release_ready_epoch, __ATOMIC_ACQUIRE) != trial_epoch)
        return tdma_runtime_owner_origin_release_reject(TDMA_ORIGIN_RELEASE_ATTEMPT_NOT_READY);
    /* Publication is intent only. STOP/revoke can race this final read; the
     * owner revalidates again next phase and immediately before DMA trigger. */
    (void)__atomic_exchange_n(&s_origin_release_request.sequence, sequence + 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&s_origin_release_request.trial_epoch, trial_epoch, __ATOMIC_RELAXED);
    __atomic_store_n(&s_origin_release_request.config_seq, config_seq, __ATOMIC_RELAXED);
    __atomic_store_n(&s_origin_release_request.sequence, sequence + 2u, __ATOMIC_RELEASE);
    attempt->reason = TDMA_ORIGIN_RELEASE_ATTEMPT_ACCEPTED;
    attempt->request_seq = *request_seq = sequence + 2u;
    return true;
}

static bool tdma_runtime_owner_origin_take_release(void)
{
    const uint32_t sequence = __atomic_load_n(&s_origin_release_request.sequence, __ATOMIC_ACQUIRE);
    if (sequence == 0u || (sequence & 1u) || sequence == s_origin_release_consumed_seq) return false;
    const uint32_t epoch = __atomic_load_n(&s_origin_release_request.trial_epoch, __ATOMIC_RELAXED);
    const uint32_t config = __atomic_load_n(&s_origin_release_request.config_seq, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&s_origin_release_request.sequence, __ATOMIC_ACQUIRE) != sequence) return false;
    s_origin_release_consumed_seq = sequence;
    if (epoch != s_origin_trial.epoch || config != s_origin_trial.config_seq) return false;
    s_origin_release_result.request_seq = sequence;
    return true;
}

static void tdma_runtime_owner_origin_cancel_release(void)
{
    __atomic_store_n(&s_origin_release_ready_epoch, 0u, __ATOMIC_RELEASE);
    if (s_origin_release_result.state != TDMA_ORIGIN_RELEASE_PREPARING &&
        s_origin_release_result.state != TDMA_ORIGIN_RELEASE_READY) return;
    /* Never wait for an interrupted Core0 publication, or write its mailbox.
     * Any later even old request remains bound to its non-reusable trial. */
    (void)tdma_runtime_owner_origin_take_release();
    s_origin_release_result.state = TDMA_ORIGIN_RELEASE_CANCELLED;
    const uint64_t now = vdc_timestamp_clock_read_ticks64();
    tdma_origin_handoff_record(&s_origin_handoff, s_tdma_pio_spi_phys.flight_origin_prepare.stage,
        now, now, TDMA_ORIGIN_BUILD_FAILED);
    __atomic_store_n(&s_origin_release_terminal_epoch, s_origin_release_result.trial_epoch, __ATOMIC_RELEASE);
}

static bool tdma_runtime_owner_origin_capability_matches(
    const refmem_realtime_origin_capability_t *capability)
{
    const tdma_state_machine_origin_dma_contract_t dma = tdma_state_machine_origin_dma_contract();
    return tdma_state_machine_origin_dma_contract_valid(&dma) &&
        capability->features == REFMEM_RT_ORIGIN_REQUIRED_FEATURES &&
        capability->persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN &&
        capability->physical_bytes == s_tdma_pio_spi_phys.flight_physical_byte_count &&
        capability->resource_mask == (TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK |
                                     TDMA_STATE_MACHINE_ORIGIN_ADDITIONAL_RESOURCE_MASK) &&
        capability->dma_mask == ((1u << dma.capture_dma) | (1u << dma.output_dma) |
                                (1u << dma.loader_dma) | (1u << dma.executor_dma)) &&
        capability->tx_pio == BOARD_TDMA_TX_PIO_BLOCK_ID &&
        capability->rx_pio == BOARD_TDMA_RX_PIO_BLOCK_ID;
}

bool tdma_runtime_owner_get_origin_capability(refmem_realtime_origin_capability_t *capability)
{
    tdma_pio_spi_phys_snapshot_t physical;
    const tdma_state_machine_origin_dma_contract_t dma = tdma_state_machine_origin_dma_contract();
    if (capability == NULL || !tdma_runtime_owner_get_phys_snapshot(&physical) ||
        !physical.armed || physical.program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        physical.overlay_physical_byte_count == 0u ||
        !tdma_state_machine_origin_dma_contract_valid(&dma)) return false;
    *capability = (refmem_realtime_origin_capability_t){
        .features = REFMEM_RT_ORIGIN_REQUIRED_FEATURES,
        .persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN,
        .clk_sys_hz = clock_get_hz(clk_sys),
        .physical_bytes = physical.overlay_physical_byte_count,
        .resource_mask = TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK |
                         TDMA_STATE_MACHINE_ORIGIN_ADDITIONAL_RESOURCE_MASK,
        .dma_mask = (1u << dma.capture_dma) | (1u << dma.output_dma) |
                    (1u << dma.loader_dma) | (1u << dma.executor_dma),
        .tx_pio = BOARD_TDMA_TX_PIO_BLOCK_ID, .rx_pio = BOARD_TDMA_RX_PIO_BLOCK_ID,
        /* Formal SRAM, full Core1 WCET and measured rearm admission remain
         * unproven. Every diagnostic trial carries these product rejections. */
        .product_reject_mask = REFMEM_RT_ORIGIN_PRODUCT_RAM | REFMEM_RT_ORIGIN_PRODUCT_WCET |
                               REFMEM_RT_ORIGIN_PRODUCT_TIMING};
    return true;
}

/* Revalidate every existing boundary; only instruction residency changes. */
TDMA_ORIGIN_OWNER_RAM
static bool tdma_runtime_owner_origin_authorized(void)
{
    const calibration_origin_timing_t *trial = &s_origin_trial;
    const tdma_ring_calibration_stage_t *stage = &s_tdma_runtime_owner.calibration_stage;
    return !s_origin_trial_fault && trial->epoch != 0u && trial->enabled != 0u &&
        tdma_ring_diagnostic_burst_limit(trial->config.flags) == 0u &&
        trial->version == CALIBRATION_ORIGIN_TIMING_VERSION &&
        trial->epoch == calibration_manager_origin_epoch() &&
        trial->admission.model_epoch == refmem_realtime_contract_origin_model_epoch() &&
        trial->admission.diagnostic_valid == 1u && trial->admission.product_valid == 0u &&
        tdma_runtime_owner_origin_capability_matches(&trial->admission.capability) &&
        trial->config_seq == __atomic_load_n(&s_tdma_runtime_owner.ring_runtime.config_seq, __ATOMIC_ACQUIRE) &&
        s_tdma_runtime_owner.ring_runtime.enabled != 0u &&
        trial->admission.foundation_crc32 == s_tdma_runtime_owner.foundation_profile_crc32 &&
        trial->calibration_generation == stage->calibration_generation &&
        trial->topology_generation == stage->topology_generation && trial->topology_crc32 == stage->topology_crc32 &&
        trial->admission.capability.clk_sys_hz == clock_get_hz(clk_sys) &&
        (!trial->expires_ticks || vdc_timestamp_clock_read_ticks64() < trial->expires_ticks);
}

static uint32_t tdma_runtime_owner_origin_stage_crc(void)
{
    const uint64_t started = tdma_service_timing_now();
    const uint32_t crc = tdma_origin_calibration_crc32(
        (const uint8_t *)&s_tdma_runtime_owner.calibration_stage,
        sizeof(s_tdma_runtime_owner.calibration_stage));
    tdma_service_timing_record(TDMA_TIMING_ORIGIN_CALIBRATION_CRC, started);
    return crc;
}

static tdma_origin_admission_result_t tdma_runtime_owner_origin_admit_impl(
    void *context, const tdma_ring_runtime_config_t *config,
    uint32_t *rearm_budget_ticks, uint32_t *abort_poll_count)
{
    calibration_origin_timing_t trial;
    if (!calibration_manager_origin_get_timing(&trial) || !trial.enabled ||
        trial.epoch == s_origin_consumed_epoch) return TDMA_ORIGIN_ADMISSION_NONE;
    s_origin_consumed_epoch = trial.epoch;
    /* Only the independently authorized trial sequence can supply this
     * owner binding. Normalize the local copy after checking the input. */
    const bool owner_sequence_matches = config != NULL &&
        config->owner_config_seq == trial.config_seq;
    trial.config.owner_config_seq = trial.config_seq;
    s_origin_trial = trial;
    s_origin_trial_fault = false;
    if (context != &s_tdma_pio_spi_phys || config == NULL ||
        rearm_budget_ticks == NULL || abort_poll_count == NULL ||
        !owner_sequence_matches || !tdma_runtime_owner_origin_authorized() ||
        memcmp(config, &trial.config, sizeof(*config)) != 0 ||
        s_tdma_pio_spi_phys.flight_physical_byte_count != trial.admission.capability.physical_bytes ||
        trial.calibration_crc32 != tdma_runtime_owner_origin_stage_crc()) {
        s_origin_trial_fault = true;
        return TDMA_ORIGIN_ADMISSION_REJECTED;
    }
    *rearm_budget_ticks = trial.rearm_budget_ticks;
    *abort_poll_count = trial.abort_poll_count;
    tdma_origin_blackout_reset(&s_origin_blackout,
        trial.diagnostic_flags == CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT,
        trial.epoch, trial.config_seq);
    return TDMA_ORIGIN_ADMISSION_READY;
}

static tdma_origin_admission_result_t tdma_runtime_owner_origin_admit(
    void *context, const tdma_ring_runtime_config_t *config,
    uint32_t *rearm_budget_ticks, uint32_t *abort_poll_count)
{
    const uint64_t started = tdma_service_timing_now();
    const tdma_origin_admission_result_t result = tdma_runtime_owner_origin_admit_impl(
        context, config, rearm_budget_ticks, abort_poll_count);
    tdma_service_timing_record(TDMA_TIMING_ORIGIN_ADMIT, started);
    return result;
}

static bool tdma_runtime_owner_origin_begin_impl(void *context,
    const tdma_ring_runtime_config_t *config, const uint8_t *packet, size_t size,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count)
{
    const bool started = context == &s_tdma_pio_spi_phys && config != NULL &&
        tdma_runtime_owner_origin_authorized() &&
        rearm_budget_ticks == s_origin_trial.rearm_budget_ticks &&
        abort_poll_count == s_origin_trial.abort_poll_count &&
        memcmp(config, &s_origin_trial.config, sizeof(*config)) == 0 &&
        tdma_pio_spi_phys_origin_begin(context, config, packet, size,
                                       rearm_budget_ticks, abort_poll_count);
    if (started) {
        (void)__atomic_exchange_n(&s_origin_release_terminal_epoch, 0u, __ATOMIC_ACQ_REL);
        __atomic_store_n(&s_origin_release_ready_epoch, 0u, __ATOMIC_RELEASE);
        s_origin_release_result = (tdma_origin_release_result_t){
            .state = TDMA_ORIGIN_RELEASE_PREPARING, .trial_epoch = s_origin_trial.epoch,
            .config_seq = s_origin_trial.config_seq};
        tdma_origin_handoff_begin(&s_origin_handoff, s_origin_trial.epoch,
            s_origin_trial.config_seq, s_origin_trial.admission.capability.clk_sys_hz,
            vdc_timestamp_clock_read_ticks64());
        /* Owner-private request, frozen before the first preparation poll.
         * No running descriptor or hardware register is modified here. */
        s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_skip_records =
            (s_origin_trial.diagnostic_flags & CALIBRATION_ORIGIN_DIAGNOSTIC_SKIP_RECORDS) != 0u;
        s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_build_probe_epoch =
            s_origin_trial.diagnostic_flags == CALIBRATION_ORIGIN_DIAGNOSTIC_BUILD_CANCEL ?
                s_origin_trial.epoch : 0u;
        s_tdma_pio_spi_phys.flight_origin_prepare.diagnostic_build_probe_config_seq = s_origin_trial.config_seq;
    } else s_origin_trial_fault = true;
    return started;
}

static bool tdma_runtime_owner_origin_begin(void *context,
    const tdma_ring_runtime_config_t *config, const uint8_t *packet, size_t size,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count)
{
    const uint64_t started = tdma_service_timing_now();
    const bool result = tdma_runtime_owner_origin_begin_impl(
        context, config, packet, size, rearm_budget_ticks, abort_poll_count);
    tdma_service_timing_record(TDMA_TIMING_ORIGIN_BEGIN, started);
    return result;
}

/* Only already-ready preparation transitions may share a service. The
 * half-budget threshold stops adding work; it is not a WCET certificate for
 * the final step or the full TDMA phase. The static scheduler keeps its gate. */
#define TDMA_ORIGIN_PREPARE_BATCH_MAX_STEPS 4u
#define TDMA_ORIGIN_PREPARE_BATCH_YIELD_CYCLES (PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES / 2u)

static bool tdma_runtime_owner_origin_follow_ready(uint32_t stage, uint32_t next)
{
    switch ((tdma_origin_prepare_stage_t)stage) {
    case TDMA_ORIGIN_PREPARE_STOP: return next == TDMA_ORIGIN_PREPARE_PERSONA;
    case TDMA_ORIGIN_PREPARE_PERSONA: return next == TDMA_ORIGIN_PREPARE_BUILD_BEGIN;
    case TDMA_ORIGIN_PREPARE_BUILD_STEP: return next == TDMA_ORIGIN_PREPARE_SEED;
    case TDMA_ORIGIN_PREPARE_SEED: return next == TDMA_ORIGIN_PREPARE_SMS;
    case TDMA_ORIGIN_PREPARE_SMS: return next == TDMA_ORIGIN_PREPARE_INSTALL;
    /* Keep mailbox/STOP separate. BUILD_BEGIN lends the workspace to Core0;
     * neither that handoff nor an unchanged BUSY stage may be polled again. */
    default: return false;
    }
}

static tdma_origin_build_result_t tdma_runtime_owner_origin_poll(void *context)
{
    const uint64_t batch_started = vdc_timestamp_clock_read_ticks64();
    for (uint32_t step = 0u; step < TDMA_ORIGIN_PREPARE_BATCH_MAX_STEPS; ++step) {
        const uint32_t stage = s_tdma_pio_spi_phys.flight_origin_prepare.stage;
        const uint64_t started = vdc_timestamp_clock_read_ticks64();
        /* A revoke, STOP, epoch or clock change between merged stages must
         * stop the next physical operation, including the final DMA trigger. */
        tdma_origin_build_result_t result = context == &s_tdma_pio_spi_phys &&
            tdma_runtime_owner_origin_authorized()
            ? tdma_pio_spi_phys_origin_poll(context) : TDMA_ORIGIN_BUILD_FAILED;
        if (result == TDMA_ORIGIN_BUILD_BUSY &&
            s_tdma_pio_spi_phys.flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_READY) {
            /* poll has checked actual disabled DMA/SMs, parked FIFO/PCs,
             * drivers, configuration, clocks and the installed loader. */
            tdma_origin_release_result_t *release = &s_origin_release_result;
            const uint64_t now = vdc_timestamp_clock_read_ticks64();
            if (release->state == TDMA_ORIGIN_RELEASE_PREPARING) {
                release->seed_sequence = s_tdma_pio_spi_ring_adapter.origin.returned.sequence;
                release->seed_identity = s_tdma_pio_spi_ring_adapter.origin.returned.identity;
                release->record_epoch = s_tdma_pio_spi_phys.flight_origin_record_epoch;
                release->ready_ticks = now;
                release->state = TDMA_ORIGIN_RELEASE_READY;
                if (s_origin_trial.diagnostic_flags == CALIBRATION_ORIGIN_DIAGNOSTIC_DEFER_RELEASE)
                    __atomic_store_n(&s_origin_release_ready_epoch, s_origin_trial.epoch, __ATOMIC_RELEASE);
            }
            if (release->ready_checks != UINT32_MAX) ++release->ready_checks;
            release->last_ready_ticks = now;
            /* INSTALL always returns through the scheduler before release.
             * The legacy trial also uses this one physical release path. */
            if (stage == TDMA_ORIGIN_PREPARE_READY &&
                (s_origin_trial.diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_DEFER_RELEASE ||
                 tdma_runtime_owner_origin_take_release())) {
                __atomic_store_n(&s_origin_release_ready_epoch, 0u, __ATOMIC_RELEASE);
                const bool released = tdma_runtime_owner_origin_authorized() &&
                    tdma_pio_spi_phys_origin_release(context, s_origin_trial.expires_ticks,
                        tdma_runtime_owner_origin_authorized);
                release->state = released ? TDMA_ORIGIN_RELEASED : TDMA_ORIGIN_RELEASE_FAILED;
                if (released) release->release_ticks = vdc_timestamp_clock_read_ticks64();
                result = released ? TDMA_ORIGIN_BUILD_DONE : TDMA_ORIGIN_BUILD_FAILED;
            }
        }
        const uint64_t ended = vdc_timestamp_clock_read_ticks64();
        tdma_origin_handoff_record(&s_origin_handoff, stage, started, ended, result);
        if (result == TDMA_ORIGIN_BUILD_FAILED) {
            s_origin_trial_fault = true;
            if (__atomic_load_n(&s_origin_release_terminal_epoch, __ATOMIC_RELAXED) == 0u) {
                s_origin_release_result.physical_reject = s_tdma_pio_spi_phys.flight_origin_prepare.reject_code;
                s_origin_release_result.physical_observed = s_tdma_pio_spi_phys.flight_origin_prepare.reject_observed;
                s_origin_release_result.physical_expected = s_tdma_pio_spi_phys.flight_origin_prepare.reject_expected;
            }
            tdma_runtime_owner_origin_cancel_release();
        }
        if (s_origin_release_result.state == TDMA_ORIGIN_RELEASED ||
            s_origin_release_result.state == TDMA_ORIGIN_RELEASE_FAILED)
            __atomic_store_n(&s_origin_release_terminal_epoch, s_origin_release_result.trial_epoch, __ATOMIC_RELEASE);
        if (result != TDMA_ORIGIN_BUILD_BUSY || ended < batch_started ||
            ended - batch_started >= TDMA_ORIGIN_PREPARE_BATCH_YIELD_CYCLES ||
            !tdma_runtime_owner_origin_follow_ready(stage,
                s_tdma_pio_spi_phys.flight_origin_prepare.stage)) return result;
    }
    return TDMA_ORIGIN_BUILD_BUSY;
}

static bool tdma_runtime_owner_origin_healthy(const void *context)
{
    const bool healthy = context == &s_tdma_pio_spi_phys &&
        tdma_runtime_owner_origin_authorized() && tdma_pio_spi_phys_origin_healthy(context);
    if (!healthy) s_origin_trial_fault = true;
    return healthy;
}

static void tdma_runtime_owner_origin_lifetime_core1(void)
{
    if (s_origin_release_result.state == TDMA_ORIGIN_RELEASE_PREPARING ||
        s_origin_release_result.state == TDMA_ORIGIN_RELEASE_READY) {
        if (!tdma_runtime_owner_origin_authorized() ||
            s_tdma_pio_spi_ring_adapter.origin.active == 0u ||
            s_tdma_pio_spi_ring_adapter.comm_fsm.state == TDMA_ADAPTER_COMM_STATE_FAULT)
            tdma_runtime_owner_origin_cancel_release();
    }
    if (s_tdma_pio_spi_ring_adapter.origin.active != 0u &&
        (!tdma_runtime_owner_origin_authorized() ||
         s_tdma_pio_spi_ring_adapter.comm_fsm.state == TDMA_ADAPTER_COMM_STATE_FAULT) &&
        s_tdma_runtime_owner.ring_runtime.enabled != 0u) {
        /* Revoke through the existing STOP lifecycle. The ring runtime keeps
         * retrying failed cleanup; never fall back to the legacy emitter. */
        (void)tdma_ring_runtime_configure(&s_tdma_runtime_owner.ring_runtime, NULL);
    }
}

/* Keep the diagnostic slow branch out of the always-resident fast gate. */
__attribute__((noinline)) static bool tdma_runtime_owner_origin_blackout_step(void)
{
    tdma_origin_blackout_input_t input = {
        .authorized = tdma_runtime_owner_origin_authorized(),
        .clock_hz = clock_get_hz(clk_sys)};
    /* This reads only a DMA publication watermark; it cannot advance the
     * graph, consume a FIFO or choose a new payload. The retained RTT records
     * and these CPU boundary times do not establish absolute edge cadence. */
    input.ready = input.authorized && s_tdma_pio_spi_ring_adapter.origin.active &&
        s_tdma_pio_spi_ring_adapter.comm_fsm.state == TDMA_ADAPTER_COMM_STATE_AUTONOMOUS &&
        tdma_pio_spi_phys_origin_record_watermark(&s_tdma_pio_spi_phys,
            &input.record_epoch, &input.record_version);
    input.now_ticks = vdc_timestamp_clock_read_ticks64();
    const tdma_origin_blackout_action_t action = tdma_origin_blackout_step(&s_origin_blackout, &input);
    if (action == TDMA_ORIGIN_BLACKOUT_STOP) {
        /* Resume the ordinary service to run its existing lifetime STOP.
         * That path retries cleanup and freezes the complete DMA archive. */
        s_origin_trial_fault = true;
    }
    return action == TDMA_ORIGIN_BLACKOUT_SKIP;
}

TDMA_ORIGIN_OWNER_RAM
bool tdma_runtime_owner_skip_tdma_service(void)
{
    const uint32_t state = s_origin_blackout.snapshot.state;
    if (state != TDMA_ORIGIN_BLACKOUT_WAITING && state != TDMA_ORIGIN_BLACKOUT_ACTIVE) return false;
    return tdma_runtime_owner_origin_blackout_step();
}

bool tdma_runtime_owner_get_origin_blackout(tdma_origin_blackout_snapshot_t *out)
{
    return tdma_origin_blackout_get(&s_origin_blackout, out);
}

bool tdma_runtime_owner_get_origin_build_probe(tdma_origin_build_probe_t *out)
{
    tdma_ring_runtime_snapshot_t before, after;
    if (!tdma_runtime_owner_get_ring_snapshot(&before) || before.enabled || before.adapter_started ||
        before.config_seq != before.applied_config_seq ||
        !tdma_pio_spi_phys_get_origin_build_probe(out)) return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return tdma_runtime_owner_get_ring_snapshot(&after) && !after.enabled && !after.adapter_started &&
        after.config_seq == before.config_seq && after.applied_config_seq == before.applied_config_seq;
}

bool tdma_runtime_owner_get_origin_handoff(tdma_origin_handoff_snapshot_t *out)
{
    tdma_ring_runtime_snapshot_t before, after;
    if (!tdma_runtime_owner_get_ring_snapshot(&before) || before.enabled || before.adapter_started ||
        before.config_seq != before.applied_config_seq ||
        !tdma_origin_handoff_get(&s_origin_handoff, out)) return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return tdma_runtime_owner_get_ring_snapshot(&after) && !after.enabled && !after.adapter_started &&
        after.config_seq == before.config_seq && after.applied_config_seq == before.applied_config_seq;
}

bool tdma_runtime_owner_get_origin_release(tdma_origin_release_snapshot_t *out)
{
    tdma_ring_runtime_snapshot_t before, after;
    /* STOP ACK can precede next-phase cancellation. A distinct terminal
     * publication is required; after it Core1 leaves this result immutable
     * until a new begin first invalidates the non-reusable epoch token. */
    const uint32_t terminal = __atomic_load_n(&s_origin_release_terminal_epoch, __ATOMIC_ACQUIRE);
    if (out == NULL || !tdma_runtime_owner_get_ring_snapshot(&before) || before.enabled ||
        before.adapter_started || before.config_seq != before.applied_config_seq ||
        __atomic_load_n(&s_origin_release_ready_epoch, __ATOMIC_ACQUIRE) != 0u) return false;
    const bool never_begun = terminal == 0u && s_origin_release_result.state == TDMA_ORIGIN_RELEASE_NONE &&
        s_origin_release_result.trial_epoch == 0u;
    if (!never_begun && (terminal == 0u || terminal != s_origin_release_result.trial_epoch)) return false;
    /* A fresh boot has an explicit NONE result, but may have real rejected
     * Core0 attempts. Report those counters; never infer zero from UNAVAILABLE. */
    out->result = never_begun ? (tdma_origin_release_result_t){0} : s_origin_release_result;
    out->attempt = s_origin_release_attempt;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return __atomic_load_n(&s_origin_release_terminal_epoch, __ATOMIC_ACQUIRE) == terminal &&
        (!never_begun || (s_origin_release_result.state == TDMA_ORIGIN_RELEASE_NONE &&
                         s_origin_release_result.trial_epoch == 0u)) &&
        tdma_runtime_owner_get_ring_snapshot(&after) && !after.enabled && !after.adapter_started &&
        after.config_seq == before.config_seq && after.applied_config_seq == before.applied_config_seq;
}
