/* The sole TDMA owner consumes Calibration's explicit diagnostic trial.
 * This file never constructs a product timing grant. */
static calibration_origin_timing_t s_origin_trial;
static uint32_t s_origin_consumed_epoch;
static bool s_origin_trial_fault;

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
         * unproven. A finite trial must carry these product rejections. */
        .product_reject_mask = REFMEM_RT_ORIGIN_PRODUCT_RAM | REFMEM_RT_ORIGIN_PRODUCT_WCET |
                               REFMEM_RT_ORIGIN_PRODUCT_TIMING};
    return true;
}

static bool tdma_runtime_owner_origin_authorized(void)
{
    const calibration_origin_timing_t *trial = &s_origin_trial;
    const tdma_ring_calibration_stage_t *stage = &s_tdma_runtime_owner.calibration_stage;
    return !s_origin_trial_fault && trial->epoch != 0u && trial->enabled != 0u &&
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
        vdc_timestamp_clock_read_ticks64() < trial->expires_ticks;
}

static tdma_origin_admission_result_t tdma_runtime_owner_origin_admit(
    void *context, const tdma_ring_runtime_config_t *config,
    uint32_t *rearm_budget_ticks, uint32_t *abort_poll_count)
{
    calibration_origin_timing_t trial;
    if (!calibration_manager_origin_get_timing(&trial) || !trial.enabled ||
        trial.epoch == s_origin_consumed_epoch) return TDMA_ORIGIN_ADMISSION_NONE;
    s_origin_consumed_epoch = trial.epoch;
    s_origin_trial = trial;
    s_origin_trial_fault = false;
    if (context != &s_tdma_pio_spi_phys || config == NULL ||
        rearm_budget_ticks == NULL || abort_poll_count == NULL ||
        !tdma_runtime_owner_origin_authorized() ||
        memcmp(config, &trial.config, sizeof(*config)) != 0 ||
        s_tdma_pio_spi_phys.flight_physical_byte_count != trial.admission.capability.physical_bytes ||
        trial.calibration_crc32 != ota_crc32_compute(
            (const uint8_t *)&s_tdma_runtime_owner.calibration_stage,
            sizeof(s_tdma_runtime_owner.calibration_stage))) {
        s_origin_trial_fault = true;
        return TDMA_ORIGIN_ADMISSION_REJECTED;
    }
    *rearm_budget_ticks = trial.rearm_budget_ticks;
    *abort_poll_count = trial.abort_poll_count;
    return TDMA_ORIGIN_ADMISSION_READY;
}

static bool tdma_runtime_owner_origin_begin(void *context,
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
    if (!started) s_origin_trial_fault = true;
    return started;
}

static tdma_origin_build_result_t tdma_runtime_owner_origin_poll(void *context)
{
    const tdma_origin_build_result_t result = context == &s_tdma_pio_spi_phys &&
        tdma_runtime_owner_origin_authorized()
        ? tdma_pio_spi_phys_origin_poll(context) : TDMA_ORIGIN_BUILD_FAILED;
    if (result == TDMA_ORIGIN_BUILD_FAILED) s_origin_trial_fault = true;
    return result;
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
    if (s_tdma_pio_spi_ring_adapter.origin.active != 0u &&
        (!tdma_runtime_owner_origin_authorized() ||
         s_tdma_pio_spi_ring_adapter.comm_fsm.state == TDMA_ADAPTER_COMM_STATE_FAULT) &&
        s_tdma_runtime_owner.ring_runtime.enabled != 0u) {
        /* Revoke through the existing STOP lifecycle. The ring runtime keeps
         * retrying failed cleanup; never fall back to the legacy emitter. */
        (void)tdma_ring_runtime_configure(&s_tdma_runtime_owner.ring_runtime, NULL);
    }
}
