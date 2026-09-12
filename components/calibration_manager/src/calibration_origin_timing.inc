/* Calibration owns this volatile trial record; TDMA owns every hardware
 * transition. Core0 publishes one record, Core1 consumes an immutable copy. */
static uint32_t s_origin_timing_guard;
static calibration_origin_timing_t s_origin_timing;

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
    (void)__atomic_add_fetch(&s_origin_timing_guard, 1u, __ATOMIC_ACQ_REL);
    s_origin_timing.enabled = 0u;
    s_origin_timing.epoch = s_origin_timing_guard + 1u;
    (void)__atomic_add_fetch(&s_origin_timing_guard, 1u, __ATOMIC_RELEASE);
}

bool calibration_manager_origin_trial(uint32_t trial_id, uint32_t rearm_budget_ticks,
    uint32_t abort_poll_count, uint64_t duration_ticks)
{
    /* A rejected replacement must not leave the previous trial authorized. */
    calibration_manager_origin_revoke();
    tdma_ring_runtime_snapshot_t ring;
    tdma_ring_calibration_stage_t stage;
    tdma_service_ring_runtime_config_t config;
    refmem_realtime_origin_capability_t capability;
    tdma_service_snapshot_t owner;
    bool complete = false;
    if (trial_id == 0u || rearm_budget_ticks == 0u || abort_poll_count == 0u ||
        abort_poll_count > UINT16_MAX || duration_ticks == 0u || duration_ticks > INT64_MAX ||
        !tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled == 0u ||
        ring.adapter_started == 0u || ring.config_seq != ring.applied_config_seq ||
        !tdma_runtime_owner_get_staged_ring_config(&config) ||
        (config.flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) == 0u ||
        config.local_slot_id != config.reference_slot_id ||
        !tdma_runtime_owner_get_calibration_stage(&stage, &complete) || !complete ||
        !tdma_runtime_owner_get_origin_capability(&capability) ||
        !tdma_service_get_snapshot(tdma_runtime_owner_get(), &owner) ||
        stage.node_count != config.node_count || stage.profile_crc32 != config.operating_profile_crc32 ||
        stage.schedule_crc32 != config.schedule_crc32) return false;
    tdma_origin_cadence_t cadence;
    if (!tdma_origin_cadence_calculate(capability.clk_sys_hz, config.baud_hz,
            capability.physical_bytes, config.cycle_period_ns, &cadence) ||
        cadence.guard_floor_ticks <= rearm_budget_ticks) return false;
    const uint64_t now = vdc_timestamp_clock_read_ticks64();
    if (now > UINT64_MAX - duration_ticks) return false;
    calibration_origin_timing_t record = {
        .version = CALIBRATION_ORIGIN_TIMING_VERSION, .enabled = 1u, .trial_id = trial_id,
        .config_seq = ring.config_seq, .calibration_generation = stage.calibration_generation,
        .topology_generation = stage.topology_generation, .topology_crc32 = stage.topology_crc32,
        .calibration_crc32 = ota_crc32_compute((const uint8_t *)&stage, sizeof(stage)),
        .rearm_budget_ticks = rearm_budget_ticks, .abort_poll_count = abort_poll_count,
        .expires_ticks = now + duration_ticks, .config = config};
    if (!refmem_realtime_contract_admit_origin_trial(&capability, owner.foundation_profile_crc32,
            &record.admission)) return false;
    tdma_ring_runtime_snapshot_t current;
    if (!tdma_runtime_owner_get_ring_snapshot(&current) || !current.enabled ||
        !current.adapter_started || current.config_seq != ring.config_seq ||
        current.applied_config_seq != ring.config_seq ||
        record.admission.model_epoch != refmem_realtime_contract_origin_model_epoch()) return false;
    (void)__atomic_add_fetch(&s_origin_timing_guard, 1u, __ATOMIC_ACQ_REL);
    record.epoch = s_origin_timing_guard + 1u;
    s_origin_timing = record;
    (void)__atomic_add_fetch(&s_origin_timing_guard, 1u, __ATOMIC_RELEASE);
    return true;
}
