/* Calibration owns this volatile trial record; TDMA owns every hardware
 * transition. Core0 publishes one record, Core1 consumes an immutable copy. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define CALIBRATION_ORIGIN_EPOCH_RAM __not_in_flash("calibration_origin_epoch")
#else
#define CALIBRATION_ORIGIN_EPOCH_RAM
#endif

static uint32_t s_origin_timing_guard;
static calibration_origin_timing_t s_origin_timing;
static calibration_origin_attempt_t s_origin_attempt;

bool calibration_manager_origin_get_attempt(calibration_origin_attempt_t *attempt)
{
    if (attempt == NULL || s_origin_attempt.attempt == 0u) return false;
    *attempt = s_origin_attempt;
    return true;
}

static bool calibration_origin_attempt_reject(calibration_origin_attempt_reason_t reason,
    uint64_t observed, uint64_t expected)
{
    s_origin_attempt.reason = reason;
    s_origin_attempt.observed = observed;
    s_origin_attempt.expected = expected;
    return false;
}

CALIBRATION_ORIGIN_EPOCH_RAM
uint32_t calibration_manager_origin_epoch(void)
{
    return __atomic_load_n(&s_origin_timing_guard, __ATOMIC_ACQUIRE);
}

bool calibration_manager_origin_get_timing(calibration_origin_timing_t *timing)
{
    if (timing == NULL) return false;
    const uint32_t begin = calibration_manager_origin_epoch();
    if (begin == 0u || (begin & 1u) != 0u) return false;
    *timing = s_origin_timing;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint32_t end = calibration_manager_origin_epoch();
    return begin == end && timing->epoch == end;
}

void calibration_manager_origin_revoke(void)
{
    const uint32_t guard = calibration_manager_origin_epoch();
    /* Core0 is the sole writer. Once exhausted, stay permanently odd and
     * unavailable until reboot; a historical trial must never become live. */
    if ((guard & 1u) != 0u || guard >= UINT32_MAX - 1u) {
        __atomic_store_n(&s_origin_timing_guard, UINT32_MAX, __ATOMIC_RELEASE);
        s_origin_timing.enabled = 0u;
        return;
    }
    (void)__atomic_exchange_n(&s_origin_timing_guard, guard + 1u, __ATOMIC_ACQ_REL);
    s_origin_timing.enabled = 0u;
    s_origin_timing.epoch = guard + 2u;
    __atomic_store_n(&s_origin_timing_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool calibration_manager_origin_trial(uint32_t trial_id, uint32_t rearm_budget_ticks,
    uint32_t abort_poll_count, uint64_t duration_ticks)
{
    return calibration_manager_origin_trial_configured(trial_id, rearm_budget_ticks,
        abort_poll_count, duration_ticks, 0u);
}

bool calibration_manager_origin_trial_configured(uint32_t trial_id, uint32_t rearm_budget_ticks,
    uint32_t abort_poll_count, uint64_t duration_ticks, uint32_t diagnostic_flags)
{
    /* A rejected replacement must not leave the previous trial authorized. */
    calibration_manager_origin_revoke();
    const uint32_t attempt = s_origin_attempt.attempt;
    s_origin_attempt = (calibration_origin_attempt_t){
        .attempt = attempt == UINT32_MAX ? UINT32_MAX : attempt + 1u,
        .trial_id = trial_id, .trial_epoch = calibration_manager_origin_epoch(),
        .reason = CALIBRATION_ORIGIN_ATTEMPT_PENDING,
        .requested_rearm_ticks = rearm_budget_ticks,
        .requested_abort_polls = abort_poll_count,
        .requested_flags = diagnostic_flags,
        .requested_duration_ticks = duration_ticks};
    if (calibration_manager_origin_epoch() >= UINT32_MAX - 1u)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_EPOCH_EXHAUSTED, 0u, 0u);
    tdma_ring_runtime_snapshot_t ring;
    tdma_ring_calibration_stage_t stage;
    tdma_service_ring_runtime_config_t config;
    refmem_realtime_origin_capability_t capability;
    uint32_t foundation_crc32;
    bool complete = false;
    /* The omission trial needs the archive: NORECORD and BLACKOUT cannot mix. */
    if ((diagnostic_flags != 0u &&
         diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_SKIP_RECORDS &&
         diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT &&
         diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_BUILD_CANCEL &&
         diagnostic_flags != CALIBRATION_ORIGIN_DIAGNOSTIC_DEFER_RELEASE) ||
        trial_id == 0u || rearm_budget_ticks == 0u || abort_poll_count == 0u ||
        abort_poll_count > UINT16_MAX || duration_ticks > INT64_MAX)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_ARGUMENT, 0u, 0u);
    if (!tdma_runtime_owner_get_ring_snapshot(&ring))
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_RING_UNAVAILABLE, 0u, 0u);
    s_origin_attempt.config_seq = ring.config_seq;
    s_origin_attempt.applied_config_seq = ring.applied_config_seq;
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_RING;
    if (ring.enabled == 0u)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_RING_DISABLED, ring.enabled, 1u);
    if (ring.adapter_started == 0u)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_RING_NOT_STARTED, ring.adapter_started, 1u);
    if (ring.config_seq != ring.applied_config_seq)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_CONFIG_PENDING, ring.applied_config_seq, ring.config_seq);
    if (!tdma_runtime_owner_get_staged_ring_config(&config))
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_CONFIG_UNAVAILABLE, 0u, 0u);
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_CONFIG;
    if ((config.flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) == 0u)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_DIAGNOSTIC_REQUIRED, config.flags, TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE);
    if (config.local_slot_id != config.reference_slot_id)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_ROLE, config.local_slot_id, config.reference_slot_id);
    if (!tdma_runtime_owner_get_calibration_stage(&stage, &complete))
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_STAGE_UNAVAILABLE, 0u, 0u);
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_STAGE;
    if (!complete)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_STAGE_INCOMPLETE, 0u, 1u);
    if (!tdma_runtime_owner_get_origin_capability(&capability))
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_CAPABILITY_UNAVAILABLE, 0u, 0u);
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_CAPABILITY;
    /* Admission needs the foundation identity, not live diagnostic snapshots. */
    if (!tdma_service_get_foundation_crc32(tdma_runtime_owner_get(), &foundation_crc32))
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_OWNER_UNAVAILABLE, 0u, 0u);
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_OWNER;
    if (stage.node_count != config.node_count)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_STAGE_NODES, stage.node_count, config.node_count);
    if (stage.profile_crc32 != config.operating_profile_crc32)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_STAGE_PROFILE, stage.profile_crc32, config.operating_profile_crc32);
    if (stage.schedule_crc32 != config.schedule_crc32)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_STAGE_SCHEDULE, stage.schedule_crc32, config.schedule_crc32);
    tdma_origin_cadence_t cadence;
    if (!tdma_origin_cadence_calculate(capability.clk_sys_hz, config.baud_hz,
            capability.physical_bytes, config.cycle_period_ns, &cadence))
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_CADENCE, 0u, 0u);
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_CADENCE;
    if (cadence.guard_floor_ticks <= rearm_budget_ticks)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_REARM_MARGIN, cadence.guard_floor_ticks, rearm_budget_ticks);
    const uint64_t now = vdc_timestamp_clock_read_ticks64();
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_CLOCK;
    if (now > UINT64_MAX - duration_ticks)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_EXPIRY, now, UINT64_MAX - duration_ticks);
    calibration_origin_timing_t record = {
        .version = CALIBRATION_ORIGIN_TIMING_VERSION, .enabled = 1u, .trial_id = trial_id,
        .config_seq = ring.config_seq, .calibration_generation = stage.calibration_generation,
        .topology_generation = stage.topology_generation, .topology_crc32 = stage.topology_crc32,
        .calibration_crc32 = ota_crc32_compute((const uint8_t *)&stage, sizeof(stage)),
        .rearm_budget_ticks = rearm_budget_ticks, .abort_poll_count = abort_poll_count,
        .expires_ticks = duration_ticks ? now + duration_ticks : 0u, .config = config,
        .diagnostic_flags = diagnostic_flags};
    if (!refmem_realtime_contract_admit_origin_trial(&capability, foundation_crc32,
            &record.admission))
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_MODEL_REJECTED, 0u, 0u);
    s_origin_attempt.admitted_model_epoch = record.admission.model_epoch;
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_ADMISSION;
    tdma_ring_runtime_snapshot_t current;
    if (!tdma_runtime_owner_get_ring_snapshot(&current))
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_RECHECK_UNAVAILABLE, 0u, 0u);
    s_origin_attempt.recheck_config_seq = current.config_seq;
    s_origin_attempt.recheck_applied_config_seq = current.applied_config_seq;
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_RECHECK;
    if (!current.enabled)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_RECHECK_DISABLED, current.enabled, 1u);
    if (!current.adapter_started)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_RECHECK_NOT_STARTED, current.adapter_started, 1u);
    if (current.config_seq != ring.config_seq)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_RECHECK_CONFIG, current.config_seq, ring.config_seq);
    if (current.applied_config_seq != ring.config_seq)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_RECHECK_APPLIED, current.applied_config_seq, ring.config_seq);
    s_origin_attempt.observed_model_epoch = refmem_realtime_contract_origin_model_epoch();
    s_origin_attempt.observed_mask |= CALIBRATION_ORIGIN_OBS_MODEL_EPOCH;
    if (record.admission.model_epoch != s_origin_attempt.observed_model_epoch)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_MODEL_CHANGED,
            s_origin_attempt.observed_model_epoch, record.admission.model_epoch);
    const uint32_t guard = calibration_manager_origin_epoch();
    if ((guard & 1u) != 0u || guard >= UINT32_MAX - 1u)
        return calibration_origin_attempt_reject(CALIBRATION_ORIGIN_ATTEMPT_EPOCH_EXHAUSTED, 0u, 0u);
    (void)__atomic_exchange_n(&s_origin_timing_guard, guard + 1u, __ATOMIC_ACQ_REL);
    record.epoch = guard + 2u;
    s_origin_timing = record;
    __atomic_store_n(&s_origin_timing_guard, guard + 2u, __ATOMIC_RELEASE);
    s_origin_attempt.trial_epoch = record.epoch;
    s_origin_attempt.reason = CALIBRATION_ORIGIN_ATTEMPT_ACCEPTED;
    return true;
}
