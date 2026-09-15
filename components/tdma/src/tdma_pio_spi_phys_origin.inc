/* Included in the physical owner: all SRAM/MMIO addresses and resources are
 * bound here. The exchange API never arms hardware or claims a wire event. */
#include "hardware/structs/dma_debug.h"

#define s_tdma_origin (s_tdma_pio_spi_workspace.origin)

void tdma_pio_spi_phys_origin_core0_prepare_service(void)
{
    tdma_origin_build_job_core0_service(&s_tdma_origin_build_job);
}

bool tdma_pio_spi_phys_get_origin_build_probe(tdma_origin_build_probe_t *out)
{
    return tdma_origin_build_job_get_stopped_probe(&s_tdma_origin_build_job, out);
}

static void tdma_pio_spi_phys_origin_first_reset(tdma_pio_spi_phys_t *phys,
    uint32_t epoch, uint32_t expected_sequence)
{
    /* The complete DMA tree and builder have retired before SEED. Revoke the
     * old lifetime before changing either the DMA body or its commit word. */
    const uint32_t guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&phys->flight_origin_record_guard, guard + 1u, __ATOMIC_RELEASE);
    __dmb();
    __atomic_store_n(&phys->flight_origin_first_readable_epoch, 0u, __ATOMIC_RELAXED);
    memset(&phys->flight_origin_first_record, 0, sizeof(phys->flight_origin_first_record));
    __atomic_store_n(&phys->flight_origin_first_expected_sequence, expected_sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&phys->flight_origin_first_readable_epoch, epoch, __ATOMIC_RELAXED);
    __atomic_store_n(&phys->flight_origin_record_guard, guard + 2u, __ATOMIC_RELEASE);
}

static bool tdma_pio_spi_phys_origin_mailbox_valid(const uint8_t *mailbox,
                                                 uint32_t slot,
                                                 uint32_t active_mask)
{
    return mailbox != NULL &&
        ((uint32_t)mailbox[0] | ((uint32_t)mailbox[1] << 8u)) == TDMA_FLIGHT_MAILBOX_MAGIC &&
        mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] == TDMA_FLIGHT_MAILBOX_VERSION &&
        tdma_process_image_transport_class_valid(mailbox[3]) &&
        mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] == slot &&
        (mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] & ~active_mask) == 0u &&
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET) ==
            ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] |
             ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] << 8u));
}

/* Bound work independently of the admitted node count. The parked adapter
 * owns this immutable seed; no live DMA memory or peripheral is accessed. */
#define TDMA_ORIGIN_PREPARE_MAILBOX_BATCH 4u

static bool tdma_pio_spi_phys_origin_mailbox_batch(const uint8_t *seed,
    size_t packet_size, uint32_t node_count, uint32_t *cursor)
{
    if (seed == NULL || cursor == NULL || node_count < 2u ||
        node_count > TDMA_FLIGHT_SHORT_SLOT_COUNT || *cursor >= node_count ||
        packet_size < TDMA_TRANSPORT_FRAME_HEADER_SIZE +
            node_count * TDMA_FLIGHT_SHORT_SLOT_SIZE + TDMA_FLIGHT_DPLL_OBSERVATION_SIZE)
        return false;
    const uint32_t active_mask = (1u << node_count) - 1u;
    for (uint32_t checked = 0u; checked < TDMA_ORIGIN_PREPARE_MAILBOX_BATCH &&
            *cursor < node_count; ++checked) {
        if (!tdma_pio_spi_phys_origin_mailbox_valid(seed + TDMA_TRANSPORT_FRAME_HEADER_SIZE +
                *cursor * TDMA_FLIGHT_SHORT_SLOT_SIZE, *cursor, active_mask)) return false;
        ++*cursor;
    }
    return true;
}

static void tdma_pio_spi_phys_origin_configure_sms(tdma_pio_spi_phys_t *phys,
                                                 uint32_t divider256)
{
    const PIO tx = phys->flight_resources.tx_pio;
    const PIO rx = phys->flight_resources.rx_pio;
    const uint control_sm = phys->flight_resources.tx_control_out_sm;
    const uint capture_sm = phys->flight_resources.tx_data_capture_sm;
    const uint rtt_sm = phys->flight_resources.tx_rtt_evidence_sm;
    tdma_pio_spi_phys_prepare_sm_pair(phys);
    pio_sm_config control = tdma_origin_control_program_get_default_config(TDMA_ORIGIN_CONTROL_PC);
    sm_config_set_set_pins(&control, phys->tx_sck_pin, 2u);
    sm_config_set_out_shift(&control, true, false, 32u);
    sm_config_set_mov_status(&control, STATUS_TX_LESSTHAN, 1u);
    sm_config_set_clkdiv_int_frac(&control, divider256 >> 8u, divider256 & 0xffu);
    pio_sm_init(tx, control_sm, TDMA_ORIGIN_CONTROL_PC, &control);
    pio_sm_set_pins_with_mask64(tx, control_sm, 1ull << phys->tx_csn_pin,
        (1ull << phys->tx_sck_pin) | (1ull << phys->tx_csn_pin));
    pio_sm_set_consecutive_pindirs(tx, control_sm, phys->tx_sck_pin, 2u, true);
    pio_gpio_init(tx, phys->tx_sck_pin);
    pio_gpio_init(tx, phys->tx_csn_pin);

    tdma_pio_spi_flight_origin_data_tx_program_init(rx, phys->tx_sm,
        TDMA_ORIGIN_DATA_PC, phys->tx_pin, phys->rx_csn_pin, phys->rx_sck_pin,
        phys->flight_sck_phase_delay_cycles, phys->flight_data_phase_delay_cycles);
    tx->instr_mem[TDMA_ORIGIN_CAPTURE_PC + 2u] = pio_encode_wait_gpio(true, phys->rx_csn_pin);
    tx->instr_mem[TDMA_ORIGIN_CAPTURE_PC + 3u] = pio_encode_wait_gpio(false, phys->rx_csn_pin);
    tx->instr_mem[TDMA_ORIGIN_CAPTURE_PC + 4u] = pio_encode_wait_gpio(true, phys->rx_sck_pin);
    tx->instr_mem[TDMA_ORIGIN_CAPTURE_PC + 5u] = pio_encode_wait_gpio(false, phys->rx_sck_pin);
    tx->instr_mem[TDMA_ORIGIN_CAPTURE_PC + 7u] = pio_encode_wait_gpio(true, phys->rx_sck_pin) |
        pio_encode_delay(phys->flight_origin_capture_phase_delay_cycles - 1u);
    tx->instr_mem[TDMA_ORIGIN_CAPTURE_PC + 9u] = pio_encode_wait_gpio(false, phys->rx_sck_pin);
    pio_sm_config capture = tdma_origin_capture_program_get_default_config(TDMA_ORIGIN_CAPTURE_PC);
    sm_config_set_in_pins(&capture, phys->rx_pin);
    sm_config_set_in_shift(&capture, false, true, 8u);
    sm_config_set_clkdiv(&capture, 1.0f);
    pio_gpio_init(tx, phys->rx_pin);
    pio_sm_set_consecutive_pindirs(tx, capture_sm, phys->rx_pin, 1u, false);
    pio_sm_init(tx, capture_sm, TDMA_ORIGIN_CAPTURE_PC, &capture);

    tx->instr_mem[TDMA_ORIGIN_RTT_PC + 1u] = pio_encode_wait_gpio(false, phys->tx_csn_pin);
    pio_sm_config rtt = tdma_origin_rtt_program_get_default_config(TDMA_ORIGIN_RTT_PC);
    sm_config_set_jmp_pin(&rtt, phys->rx_csn_pin);
    sm_config_set_clkdiv(&rtt, 1.0f);
    pio_sm_init(tx, rtt_sm, TDMA_ORIGIN_RTT_PC, &rtt);
    pio_sm_config helper = tdma_origin_helper_program_get_default_config(TDMA_ORIGIN_HELPER_PC);
    sm_config_set_out_shift(&helper, true, false, 32u);
    sm_config_set_clkdiv(&helper, 1.0f);
    pio_sm_init(rx, BOARD_TDMA_ORIGIN_HELPER_SM, TDMA_ORIGIN_HELPER_PC, &helper);
    tdma_pio_spi_flight_clock_latch_program_init(tx,
        phys->flight_resources.tx_clock_latch_sm, TDMA_ORIGIN_LATCH_PC, phys->tx_csn_pin);
    /* The graph owns raw capture/rearm. No CPU epoch or VDC-valid flag. */
    phys->flight_clock_latch_armed = false;
    phys->flight_tx_clock_latch_armed = false;
    pio_interrupt_clear(tx, 1u);
}

static bool tdma_pio_spi_phys_origin_reject(tdma_pio_spi_phys_t *phys,
    tdma_origin_reject_reason_t reason, uint32_t detail,
    uint64_t observed, uint64_t expected)
{
    tdma_origin_prepare_t *request = &phys->flight_origin_prepare;
    if (request->reject_code == TDMA_ORIGIN_REJECT_NONE) {
        request->reject_observed = (uint32_t)observed;
        request->reject_expected = (uint32_t)expected;
        request->reject_code = TDMA_ORIGIN_REJECT_CODE(reason, detail);
    }
    return false;
}

static bool tdma_pio_spi_phys_origin_expect(tdma_pio_spi_phys_t *phys,
    tdma_origin_reject_reason_t reason, uint32_t detail,
    uint64_t observed, uint64_t expected)
{
    return observed == expected ||
        tdma_pio_spi_phys_origin_reject(phys, reason, detail, observed, expected);
}

static bool tdma_pio_spi_phys_origin_config_current(tdma_pio_spi_phys_t *phys)
{
    const tdma_origin_prepare_t *request = &phys->flight_origin_prepare;
    const tdma_ring_runtime_config_t *config = &request->config;
    const tdma_origin_cadence_t *cadence = &phys->flight_origin_cadence;
#define TDMA_ORIGIN_CONFIG(detail, observed, expected) \
    tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_CONFIG, detail, observed, expected)
    return TDMA_ORIGIN_CONFIG(1u, request->live_config != NULL, true) &&
        TDMA_ORIGIN_CONFIG(2u, memcmp(request->live_config, config, sizeof(*config)) == 0, true) &&
        TDMA_ORIGIN_CONFIG(3u, clock_get_hz(clk_sys), request->clk_sys_hz) &&
        TDMA_ORIGIN_CONFIG(4u, vdc_timestamp_clock_tick_hz(), request->clk_sys_hz) &&
        TDMA_ORIGIN_CONFIG(5u, config->enabled != 0u, true) &&
        TDMA_ORIGIN_CONFIG(6u, phys->process_image_enabled, true) &&
        TDMA_ORIGIN_CONFIG(7u, phys->role, TDMA_PIO_SPI_ROLE_MASTER) &&
        TDMA_ORIGIN_CONFIG(8u, phys->baud_hz, config->baud_hz) &&
        TDMA_ORIGIN_CONFIG(9u, phys->node_count, config->node_count) &&
        TDMA_ORIGIN_CONFIG(10u, phys->flight_local_slot_id, config->local_slot_id) &&
        TDMA_ORIGIN_CONFIG(11u, request->packet_size, TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size) &&
        TDMA_ORIGIN_CONFIG(12u, phys->flight_physical_byte_count, request->physical_bytes) &&
        TDMA_ORIGIN_CONFIG(13u, phys->flight_marker_phase_delay_cycles, request->marker_phase) &&
        TDMA_ORIGIN_CONFIG(14u, phys->flight_sck_phase_delay_cycles, request->sck_phase) &&
        TDMA_ORIGIN_CONFIG(15u, phys->flight_data_phase_delay_cycles, request->data_phase) &&
        TDMA_ORIGIN_CONFIG(16u, phys->flight_origin_capture_phase_delay_cycles, request->capture_phase) &&
        TDMA_ORIGIN_CONFIG(17u, phys->flight_alignment_bit_shift < 8u, true) &&
        TDMA_ORIGIN_CONFIG(18u,
            ((uint64_t)phys->flight_alignment_byte_shift + TDMA_PIO_SPI_PACKET_HEADER_SIZE) * 8u +
                phys->flight_alignment_bit_shift, request->prefix_bits) &&
        TDMA_ORIGIN_CONFIG(19u, cadence->divider256, request->cadence.divider256) &&
        TDMA_ORIGIN_CONFIG(20u, cadence->guard_count, request->cadence.guard_count) &&
        TDMA_ORIGIN_CONFIG(21u, cadence->period_floor_ticks, request->cadence.period_floor_ticks) &&
        TDMA_ORIGIN_CONFIG(22u, cadence->period_ceiling_ticks, request->cadence.period_ceiling_ticks) &&
        TDMA_ORIGIN_CONFIG(23u, cadence->guard_floor_ticks, request->cadence.guard_floor_ticks);
#undef TDMA_ORIGIN_CONFIG
}

static void tdma_pio_spi_phys_origin_pause(void)
{
    /* Rejection may mean a bad projected PIO/SM. Disable only the two
     * board-owned flight banks, never a mismatched projection's peripheral. */
    for (uint sm = 0u; sm < 4u; ++sm) {
        pio_sm_set_enabled(BOARD_TDMA_TX_PIO, sm, false);
        pio_sm_set_enabled(BOARD_TDMA_RX_PIO, sm, false);
    }
}

static bool tdma_pio_spi_phys_origin_resources_current(tdma_pio_spi_phys_t *phys)
{
    const tdma_state_machine_resource_contract_t *r = &phys->flight_resources;
    const tdma_state_machine_resource_contract_t board = tdma_state_machine_resource_contract();
    const tdma_state_machine_origin_dma_contract_t dma = tdma_state_machine_origin_dma_contract();
    /* The two flags are the sole physical owner's retained arbiter leases.
     * Reading an arbiter snapshot here would take the shared OSAL lock. */
    if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_RESOURCE_LEASE, 1u,
            phys->flight_origin_workspace_owned, true) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_RESOURCE_LEASE, 2u,
            phys->flight_origin_resource_claimed, true) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_RESOURCE_LEASE, 3u,
            phys->flight_resource_claimed, true) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_RESOURCE_LEASE, 4u,
            s_tdma_pio_spi_flight_sms_claimed, true) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_PERSONA, 0u,
            s_tdma_pio_spi_program_persona, TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_DMA_CONTRACT, 0u,
            tdma_state_machine_origin_dma_contract_valid(&dma), true) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_RX_CONTRACT, 0u,
            tdma_state_machine_rx_endpoint_contract_valid(&r->rx_endpoints), true) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_DMA_BINDING, dma.output_dma,
            (uint32_t)s_tdma_pio_spi_tx_dma_channel, dma.output_dma) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_DMA_BINDING, dma.capture_dma,
            (uint32_t)s_tdma_pio_spi_rx_dma_channel, dma.capture_dma) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_DMA_BINDING, dma.loader_dma,
            (uint32_t)s_tdma_pio_spi_command_dma_channel, dma.loader_dma) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_DMA_BINDING, dma.executor_dma,
            (uint32_t)s_tdma_pio_spi_executor_dma_channel, dma.executor_dma)) return false;
    /* Compare fields, not structure padding. Graph MMIO uses board addresses,
     * while SM installation uses this physical projection: both must agree. */
#define TDMA_ORIGIN_BOARD_FIELD(detail, field) \
    if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_BOARD_BINDING, detail, \
        (uintptr_t)r->field, (uintptr_t)board.field)) return false
    TDMA_ORIGIN_BOARD_FIELD(1u, tx_pio);
    TDMA_ORIGIN_BOARD_FIELD(2u, rx_pio);
    TDMA_ORIGIN_BOARD_FIELD(3u, tx_control_out_sm);
    TDMA_ORIGIN_BOARD_FIELD(4u, tx_rtt_evidence_sm);
    TDMA_ORIGIN_BOARD_FIELD(5u, tx_clock_latch_sm);
    TDMA_ORIGIN_BOARD_FIELD(6u, tx_data_capture_sm);
    TDMA_ORIGIN_BOARD_FIELD(7u, rx_reserved_control_sm);
    TDMA_ORIGIN_BOARD_FIELD(8u, rx_reserved_evidence_sm);
    TDMA_ORIGIN_BOARD_FIELD(9u, rx_data_flight_sm);
    TDMA_ORIGIN_BOARD_FIELD(10u, rx_clock_latch_sm);
    TDMA_ORIGIN_BOARD_FIELD(11u, data_in_capture_dma);
    TDMA_ORIGIN_BOARD_FIELD(12u, data_out_dma);
    TDMA_ORIGIN_BOARD_FIELD(13u, data_in_forward_dma);
    TDMA_ORIGIN_BOARD_FIELD(14u, sync_edge_dma);
    TDMA_ORIGIN_BOARD_FIELD(15u, tx_clk_out_pin);
    TDMA_ORIGIN_BOARD_FIELD(16u, tx_sync_out_pin);
    TDMA_ORIGIN_BOARD_FIELD(17u, tx_data_in_pin);
    TDMA_ORIGIN_BOARD_FIELD(18u, rx_clk_in_pin);
    TDMA_ORIGIN_BOARD_FIELD(19u, rx_sync_in_pin);
    TDMA_ORIGIN_BOARD_FIELD(20u, rx_data_out_pin);
#undef TDMA_ORIGIN_BOARD_FIELD
#define TDMA_ORIGIN_PHYSICAL_FIELD(detail, field, expected) \
    if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_PHYSICAL_BINDING, detail, \
        phys->field, expected)) return false
    TDMA_ORIGIN_PHYSICAL_FIELD(1u, tx_sm, r->rx_endpoints.data_output.sm);
    TDMA_ORIGIN_PHYSICAL_FIELD(2u, rx_sm, r->tx_data_capture_sm);
    TDMA_ORIGIN_PHYSICAL_FIELD(3u, tx_sck_pin, r->tx_clk_out_pin);
    TDMA_ORIGIN_PHYSICAL_FIELD(4u, tx_csn_pin, r->tx_sync_out_pin);
    TDMA_ORIGIN_PHYSICAL_FIELD(5u, tx_pin, r->rx_data_out_pin);
    TDMA_ORIGIN_PHYSICAL_FIELD(6u, rx_pin, r->tx_data_in_pin);
    TDMA_ORIGIN_PHYSICAL_FIELD(7u, rx_sck_pin, r->rx_clk_in_pin);
    TDMA_ORIGIN_PHYSICAL_FIELD(8u, rx_csn_pin, r->rx_sync_in_pin);
#undef TDMA_ORIGIN_PHYSICAL_FIELD
    const uint channels[] = {dma.loader_dma, dma.executor_dma, dma.output_dma, dma.capture_dma};
    for (uint i = 0u; i < sizeof(channels) / sizeof(channels[0]); ++i)
        if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_DMA_CLAIM,
                channels[i], dma_channel_is_claimed(channels[i]), true)) return false;
    for (uint sm = 0u; sm < 4u; ++sm)
        if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_SM_CLAIM,
                TDMA_ORIGIN_REJECT_PIO_DETAIL(0u, sm, 0u), pio_sm_is_claimed(r->tx_pio, sm), true) ||
            !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_SM_CLAIM,
                TDMA_ORIGIN_REJECT_PIO_DETAIL(1u, sm, 0u), pio_sm_is_claimed(r->rx_pio, sm), true)) return false;
    return true;
}

static dma_channel_config tdma_pio_spi_phys_origin_loader_config(
    const tdma_state_machine_origin_dma_contract_t *dma)
{
    dma_channel_config loader = dma_channel_get_default_config(dma->loader_dma);
    channel_config_set_transfer_data_size(&loader, DMA_SIZE_32);
    channel_config_set_read_increment(&loader, true);
    channel_config_set_write_increment(&loader, true);
    channel_config_set_ring(&loader, true, dma->descriptor_write_ring_log2);
    channel_config_set_high_priority(&loader, true);
    channel_config_set_chain_to(&loader, dma->loader_dma);
    channel_config_set_enable(&loader, false);
    return loader;
}

static bool tdma_pio_spi_phys_origin_quiescent(tdma_pio_spi_phys_t *phys)
{
    const tdma_state_machine_resource_contract_t *r = &phys->flight_resources;
    const tdma_state_machine_origin_dma_contract_t dma = tdma_state_machine_origin_dma_contract();
#define TDMA_ORIGIN_QUIET(reason, detail, observed, expected) \
    tdma_pio_spi_phys_origin_expect(phys, reason, detail, observed, expected)
    if (!TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_SOFTWARE, 1u, phys->armed, false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_SOFTWARE, 2u, phys->flight_overlay_dma_active, false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_SOFTWARE, 3u, phys->rx_capture_active, false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_SOFTWARE, 4u, phys->flight_tx_pending, false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_SOFTWARE, 5u, phys->flight_tx_completion_pending, false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_BUILDER, 0u,
            tdma_origin_build_job_state(&s_tdma_origin_build_job), TDMA_ORIGIN_JOB_IDLE) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_SM_ENABLED, TDMA_ORIGIN_REJECT_PIO_DETAIL(0u, 0u, 0u),
            r->tx_pio->ctrl & 0xfu, 0u) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_SM_ENABLED, TDMA_ORIGIN_REJECT_PIO_DETAIL(1u, 0u, 0u),
            r->rx_pio->ctrl & 0xfu, 0u) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_DRIVER, BOARD_UP_BISS_DE_PIN,
            gpio_get_out_level(BOARD_UP_BISS_DE_PIN), false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_DRIVER, BOARD_DN_BISS_DE_PIN,
            gpio_get_out_level(BOARD_DN_BISS_DE_PIN), false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_DRIVER, BOARD_TRIG_DE_PIN,
            gpio_get_out_level(BOARD_TRIG_DE_PIN), false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_PAD, phys->tx_csn_pin, gpio_get(phys->tx_csn_pin), true) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_PAD, phys->tx_sck_pin, gpio_get(phys->tx_sck_pin), false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_PAD, phys->rx_csn_pin, gpio_get(phys->rx_csn_pin), true) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_IRQ, 1u, pio_interrupt_get(r->tx_pio, 1u), false) ||
        !TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_FAULT, 0u, s_tdma_origin.state.fault, 0u)) return false;
    const uint channels[] = {dma.loader_dma, dma.executor_dma, dma.output_dma, dma.capture_dma};
    for (uint i = 0u; i < sizeof(channels) / sizeof(channels[0]); ++i) {
        const uint channel = channels[i];
        const uint32_t abort = dma_hw->abort;
        if ((abort & (1u << channel)) != 0u)
            return tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_DMA_ABORT, channel, abort, 0u);
        const uint32_t ctrl = dma_hw->ch[channel].ctrl_trig;
        if ((ctrl & (DMA_CH0_CTRL_TRIG_EN_BITS | DMA_CH0_CTRL_TRIG_BUSY_BITS)) != 0u)
            return tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_DMA_CONTROL, channel, ctrl, 0u);
    }
    for (uint sm = 0u; sm < 4u; ++sm) {
#define TDMA_ORIGIN_FIFO(bank, pio, direction, is_rx) \
        if (!pio_sm_is_##direction##_fifo_empty(pio, sm)) \
            return tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_FIFO, \
                TDMA_ORIGIN_REJECT_PIO_DETAIL(bank, sm, is_rx), (pio)->fstat, \
                1u << ((is_rx ? PIO_FSTAT_RXEMPTY_LSB : PIO_FSTAT_TXEMPTY_LSB) + sm))
        TDMA_ORIGIN_FIFO(0u, r->tx_pio, tx, 0u);
        TDMA_ORIGIN_FIFO(0u, r->tx_pio, rx, 1u);
        TDMA_ORIGIN_FIFO(1u, r->rx_pio, tx, 0u);
        TDMA_ORIGIN_FIFO(1u, r->rx_pio, rx, 1u);
#undef TDMA_ORIGIN_FIFO
    }
#define TDMA_ORIGIN_PC(bank, pio, sm, expected) \
    TDMA_ORIGIN_QUIET(TDMA_ORIGIN_REJECT_PC, TDMA_ORIGIN_REJECT_PIO_DETAIL(bank, sm, 0u), \
        pio_sm_get_pc(pio, sm), expected)
    return TDMA_ORIGIN_PC(0u, r->tx_pio, r->tx_control_out_sm, TDMA_ORIGIN_CONTROL_PC) &&
        TDMA_ORIGIN_PC(0u, r->tx_pio, r->tx_data_capture_sm, TDMA_ORIGIN_CAPTURE_PC) &&
        TDMA_ORIGIN_PC(0u, r->tx_pio, r->tx_rtt_evidence_sm, TDMA_ORIGIN_RTT_PC) &&
        TDMA_ORIGIN_PC(0u, r->tx_pio, r->tx_clock_latch_sm, TDMA_ORIGIN_LATCH_PC) &&
        TDMA_ORIGIN_PC(1u, r->rx_pio, r->rx_endpoints.data_output.sm, TDMA_ORIGIN_DATA_PC) &&
        TDMA_ORIGIN_PC(1u, r->rx_pio, dma.helper_sm, TDMA_ORIGIN_HELPER_PC);
#undef TDMA_ORIGIN_PC
#undef TDMA_ORIGIN_QUIET
}

static bool tdma_pio_spi_phys_origin_plan_ready(tdma_pio_spi_phys_t *phys)
{
    const tdma_origin_plan_t *plan = &s_tdma_origin.plan;
    if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_PLAN_STORAGE, 1u,
            (uintptr_t)plan->runs, (uintptr_t)s_tdma_origin.runs) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_PLAN_STORAGE, 2u,
            (uintptr_t)plan->literals, (uintptr_t)s_tdma_origin.literals) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_PLAN_STORAGE, 3u,
            plan->run_capacity, TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_PLAN_STORAGE, 4u,
            plan->literal_capacity, TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY)) return false;
    if (plan->run_count == 0u || plan->run_count > plan->run_capacity)
        return tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_PLAN_COUNT,
            1u, plan->run_count, plan->run_capacity);
    if (plan->literal_count == 0u || plan->literal_count > plan->literal_capacity)
        return tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_PLAN_COUNT,
            2u, plan->literal_count, plan->literal_capacity);
    const uint32_t base = (uint32_t)(uintptr_t)plan->runs;
    const uint32_t bytes = plan->run_count * sizeof(plan->runs[0]);
    const uint32_t entries[] = {plan->seed_entry, plan->boundary_entry, plan->local_entry[0],
        plan->local_entry[1], plan->fault_entry, plan->record_entry};
    const uint count = phys->flight_origin_prepare.diagnostic_skip_records ? 5u : 6u;
    if (count == 5u && plan->record_entry != 0u)
        return tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_PLAN_ENTRY,
            6u, plan->record_entry, 0u);
    for (uint i = 0u; i < count; ++i)
        if (entries[i] < base || entries[i] - base >= bytes ||
            (entries[i] - base) % sizeof(plan->runs[0]) != 0u)
            return tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_PLAN_ENTRY,
                i, entries[i], base);
    return true;
}

bool tdma_pio_spi_phys_origin_prepare_ready(void *context)
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL) return false;
    if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_STAGE, 0u,
            phys->flight_origin_prepare.stage, TDMA_ORIGIN_PREPARE_READY) ||
        !tdma_pio_spi_phys_origin_config_current(phys) ||
        !tdma_pio_spi_phys_origin_resources_current(phys) ||
        !tdma_pio_spi_phys_origin_quiescent(phys) || !tdma_pio_spi_phys_origin_plan_ready(phys)) return false;
    const tdma_state_machine_origin_dma_contract_t dma = tdma_state_machine_origin_dma_contract();
    const dma_channel_config loader = tdma_pio_spi_phys_origin_loader_config(&dma);
    /* LOADER detail: 1=CTRL, 2=READ_ADDR, 3=WRITE_ADDR, 4=DBG_TCR.
     * RP2350 TRANS_COUNT writes set RELOAD; the live remaining count is only
     * loaded on a trigger. DBG_TCR reads the next transfer length without
     * triggering. Compare all 32 bits, including MODE, so self-trigger/endless
     * encodings cannot pass as a normal descriptor_words transfer. */
    return tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_LOADER, 1u,
            dma_hw->ch[dma.loader_dma].ctrl_trig, loader.ctrl) &&
        tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_LOADER, 2u,
            dma_hw->ch[dma.loader_dma].read_addr, s_tdma_origin.plan.seed_entry) &&
        tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_LOADER, 3u,
            dma_hw->ch[dma.loader_dma].write_addr, (uint32_t)(uintptr_t)&dma_hw->ch[dma.executor_dma].al3_ctrl) &&
        tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_LOADER, 4u,
            dma_debug_hw->ch[dma.loader_dma].dbg_tcr, dma.descriptor_words);
}

bool tdma_pio_spi_phys_origin_release(void *context, uint64_t expires_ticks,
    bool (*authorized)(void))
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL) return false;
    if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_STAGE, 1u,
            phys->flight_origin_prepare.stage, TDMA_ORIGIN_PREPARE_READY)) return false;
    const tdma_state_machine_origin_dma_contract_t dma = tdma_state_machine_origin_dma_contract();
    if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_AUTHORITY, 0u, authorized != NULL, true) ||
        !tdma_pio_spi_phys_origin_prepare_ready(phys)) goto failed;
    if (expires_ticks == 0u) {
        (void)tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_EXPIRY, 0u, 0u, 1u);
        goto failed;
    }
    const uint64_t initial_ticks = vdc_timestamp_clock_read_ticks64();
    if (initial_ticks >= expires_ticks) {
        (void)tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_EXPIRY, 1u, initial_ticks, expires_ticks);
        goto failed;
    }
    /* Consume this READY once before enabling anything. No queued or duplicate
     * release may start a later ARM. The owner, not this deadline, grants it. */
    phys->flight_origin_prepare.stage = TDMA_ORIGIN_PREPARE_FAILED;
    phys->flight_normal_capture_copy_stage = 0u;
    phys->flight_overlay_alignment_locked = true;
    __dmb();
    pio_sm_set_enabled(phys->flight_resources.rx_pio, dma.helper_sm, true);
    pio_sm_set_enabled(phys->flight_resources.tx_pio, phys->flight_resources.tx_control_out_sm, true);
    tdma_pio_spi_phys_set_line_drivers(true);
    dma_channel_config loader = tdma_pio_spi_phys_origin_loader_config(&dma);
    channel_config_set_enable(&loader, true);
    /* CTRL_TRIG writes can start a channel. Use the SDK's non-triggering
     * AL1_CTRL alias so the explicit start below is the sole launch. */
    dma_channel_set_config(dma.loader_dma, &loader, false);
    /* The parked empty FIFOs cannot emit before this trigger. Time may have
     * advanced during the checks/enables, so check the original expiry again. */
    if (!tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_RELEASE_CLOCK, 1u,
            clock_get_hz(clk_sys), phys->flight_origin_prepare.clk_sys_hz) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_RELEASE_CLOCK, 2u,
            vdc_timestamp_clock_tick_hz(), phys->flight_origin_prepare.clk_sys_hz) ||
        !tdma_pio_spi_phys_origin_expect(phys, TDMA_ORIGIN_REJECT_AUTHORITY, 1u, authorized(), true)) goto failed;
    const uint64_t final_ticks = vdc_timestamp_clock_read_ticks64();
    if (final_ticks >= expires_ticks) {
        (void)tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_EXPIRY, 2u, final_ticks, expires_ticks);
        goto failed;
    }
    dma_start_channel_mask(1u << dma.loader_dma);
    phys->flight_overlay_dma_active = true;
    phys->rx_capture_active = true;
    phys->armed = true;
    phys->flight_origin_prepare.stage = TDMA_ORIGIN_PREPARE_COMPLETE;
    phys->flight_origin_prepare.live_config = NULL;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
failed:
    tdma_pio_spi_phys_disable_dma_mask(1u << dma.loader_dma);
    tdma_pio_spi_phys_origin_pause();
    tdma_pio_spi_phys_set_line_drivers(false);
    phys->armed = false;
    phys->flight_origin_prepare.stage = TDMA_ORIGIN_PREPARE_FAILED;
    /* Retain workspace/leases and any foreign DMA state for common STOP. */
    return false;
}

bool tdma_pio_spi_phys_origin_begin(
    void *context, const tdma_ring_runtime_config_t *config,
    const uint8_t *returned_packet, size_t packet_size,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count)
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL || config == NULL || returned_packet == NULL ||
        packet_size != TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size || !phys->armed ||
        !phys->process_image_enabled || phys->role != TDMA_PIO_SPI_ROLE_MASTER ||
        phys->flight_origin_workspace_owned ||
        tdma_origin_build_job_state(&s_tdma_origin_build_job) != TDMA_ORIGIN_JOB_IDLE ||
        tdma_overlay_prepare_state(phys->overlay_preparation) != TDMA_OVERLAY_PREPARE_IDLE ||
        phys->flight_origin_prepare.stage != TDMA_ORIGIN_PREPARE_IDLE || phys->flight_tx_pending ||
        phys->flight_tx_completion_pending ||
        s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        s_tdma_pio_spi_tx_dma_channel != TDMA_PIO_SPI_TX_DMA_CHANNEL ||
        s_tdma_pio_spi_rx_dma_channel != TDMA_PIO_SPI_RX_DMA_CHANNEL ||
        config->enabled == 0u ||
        (config->flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) == 0u ||
        tdma_ring_diagnostic_burst_limit(config->flags) != 0u ||
        config->node_count < 2u || config->node_count > TDMA_FLIGHT_SHORT_SLOT_COUNT ||
        config->node_count != phys->node_count || config->baud_hz != phys->baud_hz ||
        config->local_slot_id != config->reference_slot_id ||
        config->local_slot_id != phys->flight_local_slot_id ||
        config->local_slot_id >= config->node_count ||
        config->tx_dma_channel_id != TDMA_PIO_SPI_TX_DMA_CHANNEL ||
        config->rx_dma_channel_id != TDMA_PIO_SPI_RX_DMA_CHANNEL ||
        config->node_count > tdma_flight_payload_slots(phys->flight_payload_size) ||
        phys->flight_overlay_alignment_samples < TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES ||
        (phys->flight_normal_capture_copy_stage != 0u && phys->flight_normal_capture_copy_stage != 5u) ||
        (phys->flight_sck_waveform_capture_state != TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE &&
         phys->flight_sck_waveform_capture_state != TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED) ||
        phys->flight_data_phase_delay_cycles == 0u || phys->flight_data_phase_delay_cycles > 32u ||
        phys->flight_origin_capture_phase_delay_cycles == 0u ||
        phys->flight_origin_capture_phase_delay_cycles > 31u ||
        rearm_budget_ticks == 0u || abort_poll_count == 0u || abort_poll_count > UINT16_MAX)
        return false;

    /* Asynchronous preparation must not retain a pointer to a live DMA ring
     * or to the union that it will replace. The adapter owns its frozen copy. */
    const uintptr_t workspace_begin = (uintptr_t)&s_tdma_pio_spi_workspace;
    const uintptr_t packet = (uintptr_t)returned_packet;
    const uintptr_t config_address = (uintptr_t)config;
    if (packet > UINTPTR_MAX - packet_size || config_address > UINTPTR_MAX - sizeof(*config) ||
        (packet < workspace_begin + sizeof(s_tdma_pio_spi_workspace) && packet + packet_size > workspace_begin) ||
        (config_address < workspace_begin + sizeof(s_tdma_pio_spi_workspace) && config_address + sizeof(*config) > workspace_begin))
        return false;
    tdma_origin_cadence_t cadence;
    if (!tdma_origin_cadence_calculate(clock_get_hz(clk_sys), phys->baud_hz,
            phys->flight_physical_byte_count, config->cycle_period_ns, &cadence) ||
        cadence.guard_floor_ticks <= rearm_budget_ticks) return false;
    const uint64_t prefix = ((uint64_t)phys->flight_alignment_byte_shift +
        TDMA_PIO_SPI_PACKET_HEADER_SIZE) * 8u + phys->flight_alignment_bit_shift;
    if (phys->flight_alignment_bit_shift >= 8u ||
        phys->flight_physical_byte_count <= TDMA_PIO_SPI_PACKET_HEADER_SIZE + packet_size ||
        prefix > (phys->flight_physical_byte_count - packet_size) * 8u) return false;
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result;
    if (!tdma_transport_frame_decode(returned_packet, packet_size, &view, &result) ||
        view.payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE ||
        view.payload_size != phys->flight_payload_size ||
        view.origin_slot_id != config->local_slot_id || view.hop_count != config->node_count - 1u ||
        view.hop_limit != view.hop_count || view.schedule_crc32 != config->schedule_crc32 ||
        view.ring_profile_crc32 != config->ring_profile_crc32 ||
        view.flags != (TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK | TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE))
        return false;
    /* This scanner owns only its private byte copy. Invalidate its hint now;
     * Core0 may finish cancellation while the owner switches the union. */
    (void)tdma_pio_spi_phys_rx_scan_cancel(phys);
    phys->flight_origin_prepare = (tdma_origin_prepare_t){
        .packet_size = (uint32_t)packet_size,
        .live_config = config, .seed = returned_packet,
        .stage = TDMA_ORIGIN_PREPARE_MAILBOX, .prefix_bits = (uint32_t)prefix,
        .abort_poll_count = abort_poll_count, .clk_sys_hz = clock_get_hz(clk_sys),
        .cadence = cadence, .physical_bytes = phys->flight_physical_byte_count,
        .marker_phase = phys->flight_marker_phase_delay_cycles,
        .sck_phase = phys->flight_sck_phase_delay_cycles,
        .data_phase = phys->flight_data_phase_delay_cycles,
        .capture_phase = phys->flight_origin_capture_phase_delay_cycles};
    memcpy(&phys->flight_origin_prepare.config, config, sizeof(*config));
    phys->flight_origin_cadence = cadence;
    return true;
}

tdma_origin_build_result_t tdma_pio_spi_phys_origin_poll(void *context)
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL) return TDMA_ORIGIN_BUILD_FAILED;
    tdma_origin_prepare_t *request = &phys->flight_origin_prepare;
    if (request->stage == TDMA_ORIGIN_PREPARE_COMPLETE) return TDMA_ORIGIN_BUILD_DONE;
    if (request->stage == TDMA_ORIGIN_PREPARE_IDLE || request->stage == TDMA_ORIGIN_PREPARE_FAILED)
        return TDMA_ORIGIN_BUILD_FAILED;
    /* The owner cannot replace accepted config/clock facts in the middle of
     * preparation. Check again in the installation poll, before the trigger. */
    if (!tdma_pio_spi_phys_origin_config_current(phys)) goto failed;
    const tdma_ring_runtime_config_t *config = &request->config;
    const uint32_t active_mask = (1u << config->node_count) - 1u;
    const tdma_state_machine_origin_dma_contract_t dma = tdma_state_machine_origin_dma_contract();
    switch ((tdma_origin_prepare_stage_t)request->stage) {
    case TDMA_ORIGIN_PREPARE_MAILBOX:
        if (!tdma_pio_spi_phys_origin_mailbox_batch(request->seed, request->packet_size,
                config->node_count, &request->mailbox)) goto failed;
        /* STOP stays in the next poll, after owner grant/config revalidation.
         * It must retire all old DMA readers before the workspace is reused. */
        if (request->mailbox == config->node_count) request->stage = TDMA_ORIGIN_PREPARE_STOP;
        return TDMA_ORIGIN_BUILD_BUSY;
    case TDMA_ORIGIN_PREPARE_STOP: {
    const uint32_t loader = s_tdma_pio_spi_command_dma_channel < 0 ? 0u :
        (1u << (uint)s_tdma_pio_spi_command_dma_channel);
    const uint32_t executor = s_tdma_pio_spi_executor_dma_channel < 0 ? 0u :
        (1u << (uint)s_tdma_pio_spi_executor_dma_channel);
    const uint32_t children = (1u << (uint)s_tdma_pio_spi_tx_dma_channel) |
                              (1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    tdma_pio_spi_phys_pause_sm_pair(phys);
    if (!tdma_pio_spi_phys_stop_dma_chain(loader, executor, children,
            tdma_pio_spi_phys_now_us() + TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US)) {
        phys->flight_overlay_dma_active = true;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY);
        goto failed;
    }
    phys->armed = false;
    phys->rx_capture_active = false;
    phys->flight_overlay_dma_active = false;
    tdma_pio_spi_phys_set_line_drivers(false);
    /* All old readers have retired. Preserve seed banks outside construction
     * scratch; no memset of the entire union may erase this frozen return. */
    tdma_pio_spi_phys_origin_record_invalidate(phys);
    memcpy(s_tdma_origin.capture[0], request->seed, request->packet_size);
    memset(s_tdma_origin.capture[0] + request->packet_size -
        TDMA_FLIGHT_DPLL_OBSERVATION_SIZE, 0, TDMA_FLIGHT_DPLL_OBSERVATION_SIZE);
    request->seed = NULL;
    phys->flight_origin_workspace_owned = true;
    phys->flight_origin_rx_observation_ready = false;
    request->stage = TDMA_ORIGIN_PREPARE_PERSONA;
    return TDMA_ORIGIN_BUILD_BUSY;
    }
    case TDMA_ORIGIN_PREPARE_PERSONA:
    if (!tdma_pio_spi_phys_select_program_persona(phys,
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN)) goto failed;
    if (!tdma_state_machine_origin_dma_contract_valid(&dma) ||
        !phys->flight_origin_resource_claimed || !phys->flight_resource_claimed ||
        s_tdma_pio_spi_tx_dma_channel != dma.output_dma ||
        s_tdma_pio_spi_rx_dma_channel != dma.capture_dma ||
        s_tdma_pio_spi_command_dma_channel != dma.loader_dma ||
        s_tdma_pio_spi_executor_dma_channel != dma.executor_dma) goto failed;
    request->stage = TDMA_ORIGIN_PREPARE_BUILD_BEGIN;
    return TDMA_ORIGIN_BUILD_BUSY;
    case TDMA_ORIGIN_PREPARE_BUILD_BEGIN: {
    if (vdc_timestamp_clock_tick_hz() != request->clk_sys_hz ||
        phys->flight_resources.tx_clock_latch_sm != 6u -
            phys->flight_resources.tx_control_out_sm -
            phys->flight_resources.tx_data_capture_sm -
            phys->flight_resources.tx_rtt_evidence_sm) goto failed;
    tdma_origin_plan_config_t plan_config = {
        .address = {
            .capture_bank = {(uint32_t)(uintptr_t)s_tdma_origin.capture[0],
                             (uint32_t)(uintptr_t)s_tdma_origin.capture[1]},
            .stage = (uint32_t)(uintptr_t)s_tdma_origin.stage,
            .tx_header = (uint32_t)(uintptr_t)s_tdma_origin.tx_header,
            .rx_packet = (uint32_t)(uintptr_t)s_tdma_origin.rx_packet,
            .state = (uint32_t)(uintptr_t)&s_tdma_origin.state,
            .local_shadow = {(uint32_t)(uintptr_t)s_tdma_origin.local_shadow[0],
                             (uint32_t)(uintptr_t)s_tdma_origin.local_shadow[1]},
            .scratch = (uint32_t)(uintptr_t)s_tdma_origin.scratch,
            .runs = (uint32_t)(uintptr_t)s_tdma_origin.runs,
            .literals = (uint32_t)(uintptr_t)s_tdma_origin.literals,
            .records = (uint32_t)(uintptr_t)s_tdma_origin.record,
            .first_record = (uint32_t)(uintptr_t)&phys->flight_origin_first_record,
        },
        .physical_bytes = phys->flight_physical_byte_count,
        .packet_size = request->packet_size,
        .outer_header_bytes = TDMA_PIO_SPI_PACKET_HEADER_SIZE,
        .capture_prefix_bits = request->prefix_bits,
        .guard_count = phys->flight_origin_cadence.guard_count, .abort_poll_count = request->abort_poll_count,
        .local_slot = config->local_slot_id, .active_slot_mask = active_mask,
        .capture_dma = dma.capture_dma, .output_dma = dma.output_dma,
        .loader_dma = dma.loader_dma, .executor_dma = dma.executor_dma,
        .control_pio = BOARD_TDMA_TX_PIO_BLOCK_ID, .data_pio = BOARD_TDMA_RX_PIO_BLOCK_ID,
        .control_sm = phys->flight_resources.tx_control_out_sm,
        .capture_sm = phys->flight_resources.tx_data_capture_sm,
        .data_sm = phys->flight_resources.rx_endpoints.data_output.sm, .helper_sm = dma.helper_sm,
        .control_pc = TDMA_ORIGIN_CONTROL_PC, .capture_pc = TDMA_ORIGIN_CAPTURE_PC,
        .data_pc = TDMA_ORIGIN_DATA_PC, .helper_pc = TDMA_ORIGIN_HELPER_PC,
        .compare_pc = TDMA_ORIGIN_COMPARE_PC,
        .rtt_sm = phys->flight_resources.tx_rtt_evidence_sm, .rtt_pc = TDMA_ORIGIN_RTT_PC,
        .diagnostic_skip_records = request->diagnostic_skip_records,
    };
    memcpy(&plan_config.returned_route_word, s_tdma_origin.capture[0] + 12u, sizeof(uint32_t));
    s_tdma_origin.plan = (tdma_origin_plan_t){.runs = s_tdma_origin.runs,
        .literals = s_tdma_origin.literals, .run_capacity = TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY,
        .literal_capacity = TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY};
    memset(&s_tdma_origin.builder, 0, sizeof(s_tdma_origin.builder));
    if (!tdma_origin_plan_begin(&s_tdma_origin.builder, &plan_config, &s_tdma_origin.plan)) goto failed;
    if (!tdma_origin_build_job_request_probe(&s_tdma_origin_build_job, &s_tdma_origin.builder,
            request->diagnostic_build_probe_epoch, request->diagnostic_build_probe_config_seq)) goto failed;
    request->stage = TDMA_ORIGIN_PREPARE_BUILD_STEP;
    return TDMA_ORIGIN_BUILD_BUSY;
    }
    case TDMA_ORIGIN_PREPARE_BUILD_STEP: {
        /* An explicit diagnostic trial parks a live SRAM writer after real
         * emission. Fail through the ordinary owner STOP, which retains the
         * workspace until that worker acknowledges cancellation. */
        if (tdma_origin_build_job_probe_paused(&s_tdma_origin_build_job)) goto failed;
        const tdma_origin_build_result_t result = tdma_origin_build_job_take(&s_tdma_origin_build_job);
        if (result == TDMA_ORIGIN_BUILD_FAILED) goto failed;
        if (result == TDMA_ORIGIN_BUILD_DONE) request->stage = TDMA_ORIGIN_PREPARE_SEED;
        return TDMA_ORIGIN_BUILD_BUSY;
    }
    case TDMA_ORIGIN_PREPARE_SEED: {
    uint8_t seed[TDMA_FLIGHT_SHORT_PACKET_SIZE];
    memcpy(seed, s_tdma_origin.capture[0], request->packet_size);
    memcpy(s_tdma_origin.capture[1], seed, request->packet_size);
    uint32_t sequence;
    memcpy(&sequence, seed + 8u, sizeof(sequence));
    tdma_transport_result_t result;
    if (!tdma_transport_frame_prepare_resident_position(seed, request->packet_size,
            sequence, 0u, &result)) goto failed;
    /* Construction is complete; retire its scratch before live initialization. */
    memset(s_tdma_origin.stage, 0, sizeof(s_tdma_origin.stage));
    memset(s_tdma_origin.rx_packet, 0, sizeof(s_tdma_origin.rx_packet));
    memcpy(s_tdma_origin.tx_header, seed, sizeof(s_tdma_origin.tx_header));
    memcpy(s_tdma_origin.local_shadow[0], seed + TDMA_TRANSPORT_FRAME_HEADER_SIZE +
        config->local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    s_tdma_origin.local_shadow[0][TDMA_FLIGHT_SHORT_SLOT_SIZE / sizeof(uint32_t)] = 1u;
    memcpy(s_tdma_origin.local_shadow[1], s_tdma_origin.local_shadow[0], TDMA_ORIGIN_PLAN_SHADOW_BYTES);
    s_tdma_origin.stage[0] = TDMA_PIO_SPI_PACKET_MAGIC0 << 8u;
    s_tdma_origin.stage[1] = TDMA_PIO_SPI_PACKET_MAGIC1 << 8u;
    s_tdma_origin.stage[2] = (request->packet_size & 0xffu) << 8u;
    s_tdma_origin.stage[3] = (request->packet_size >> 8u) << 8u;
    uint32_t record_epoch = __atomic_load_n(&phys->flight_origin_record_epoch, __ATOMIC_RELAXED) + 1u;
    if (record_epoch == 0u) ++record_epoch;
    __atomic_store_n(&phys->flight_origin_record_epoch, record_epoch, __ATOMIC_RELAXED);
    memset(s_tdma_origin.record, 0, sizeof(s_tdma_origin.record));
    s_tdma_origin.state = (tdma_origin_plan_state_t){.capture_bank = 1u,
        .local_next_address = s_tdma_origin.plan.local_entry[0],
        .local_selected_generation = 1u, .bank_version = {2u, 2u},
        .record_next_address = s_tdma_origin.plan.record_entry,
        .record_epoch = record_epoch,
        .record_format = TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME,
        .record_time = {.tick_hz = vdc_timestamp_clock_tick_hz()}};
    tdma_pio_spi_phys_origin_first_reset(phys,
        request->diagnostic_skip_records ? 0u : record_epoch, sequence + 1u);
    if (!tdma_origin_exchange_bind(&s_tdma_origin.exchange, &s_tdma_origin.state,
            s_tdma_origin.capture[0], s_tdma_origin.capture[1],
            s_tdma_origin.local_shadow[0], s_tdma_origin.local_shadow[1],
            s_tdma_origin.plan.local_entry[0], s_tdma_origin.plan.local_entry[1], request->packet_size)) goto failed;
    request->stage = TDMA_ORIGIN_PREPARE_SMS;
    return TDMA_ORIGIN_BUILD_BUSY;
    }
    case TDMA_ORIGIN_PREPARE_SMS:
    tdma_pio_spi_phys_origin_configure_sms(phys, phys->flight_origin_cadence.divider256);
    request->stage = TDMA_ORIGIN_PREPARE_INSTALL;
    return TDMA_ORIGIN_BUILD_BUSY;
    case TDMA_ORIGIN_PREPARE_INSTALL: {
    if (!tdma_pio_spi_phys_origin_resources_current(phys) ||
        !tdma_pio_spi_phys_origin_quiescent(phys) || !tdma_pio_spi_phys_origin_plan_ready(phys)) goto failed;
    __dmb();
    const dma_channel_config loader = tdma_pio_spi_phys_origin_loader_config(&dma);
    /* AL3's final write triggers the executor; CHAIN_TO must not trigger it
     * a second time. The graph alone supplies all later child/launch words. */
    dma_channel_configure(dma.loader_dma, &loader, &dma_hw->ch[dma.executor_dma].al3_ctrl,
        (const void *)(uintptr_t)s_tdma_origin.plan.seed_entry, dma.descriptor_words, false);
    request->stage = TDMA_ORIGIN_PREPARE_READY;
    if (!tdma_pio_spi_phys_origin_prepare_ready(phys)) goto failed;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return TDMA_ORIGIN_BUILD_BUSY;
    }
    case TDMA_ORIGIN_PREPARE_READY:
        /* Recheck on every owner service; waiting never renews the grant or
         * starts the graph, even when the same poll is called repeatedly. */
        if (!tdma_pio_spi_phys_origin_prepare_ready(phys)) goto failed;
        return TDMA_ORIGIN_BUILD_BUSY;
    default:
        break;
    }
failed:
    /* Earlier preparation stages retain their original failure path; expose
     * the failing stage when no more specific INSTALL/READY predicate ran. */
    (void)tdma_pio_spi_phys_origin_reject(phys, TDMA_ORIGIN_REJECT_PREPARE_STAGE,
        request->stage, request->stage, 0u);
    tdma_pio_spi_phys_origin_pause();
    tdma_pio_spi_phys_set_line_drivers(false);
    phys->armed = false;
    request->stage = TDMA_ORIGIN_PREPARE_FAILED;
    /* No workspace reset/release here: common STOP owns cancellation. */
    return TDMA_ORIGIN_BUILD_FAILED;
}

bool tdma_pio_spi_phys_origin_active(const void *context)
{
    const tdma_pio_spi_phys_t *phys = context;
    return phys != NULL && phys->armed && phys->flight_origin_workspace_owned &&
        phys->flight_origin_prepare.stage == TDMA_ORIGIN_PREPARE_COMPLETE &&
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN;
}

bool tdma_pio_spi_phys_origin_healthy(const void *context)
{
    const tdma_pio_spi_phys_t *phys = context;
    return tdma_pio_spi_phys_origin_active(context) &&
        __atomic_load_n(&s_tdma_origin.state.fault, __ATOMIC_ACQUIRE) == 0u &&
        !pio_interrupt_get(phys->flight_resources.tx_pio, 1u);
}

bool tdma_pio_spi_phys_origin_ready(void *context)
{
    return tdma_pio_spi_phys_origin_healthy(context) &&
        tdma_origin_exchange_ready(&s_tdma_origin.exchange);
}

bool tdma_pio_spi_phys_origin_record_watermark(const void *context,
    uint32_t *epoch, uint32_t *published_version)
{
    const tdma_pio_spi_phys_t *phys = context;
    if (epoch == NULL || published_version == NULL ||
        !tdma_pio_spi_phys_origin_healthy(context) ||
        phys->flight_origin_prepare.diagnostic_skip_records) return false;
    *epoch = phys->flight_origin_record_epoch;
    *published_version = __atomic_load_n(&s_tdma_origin.state.record_published_version, __ATOMIC_ACQUIRE);
    return true;
}

bool tdma_pio_spi_phys_origin_publish(void *context,
    const uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE], uint32_t *generation)
{
    tdma_pio_spi_phys_t *phys = context;
    if (generation != NULL) *generation = 0u;
    return tdma_pio_spi_phys_origin_healthy(context) &&
        tdma_pio_spi_phys_origin_mailbox_valid(mailbox, phys->flight_local_slot_id,
                                              (1u << phys->node_count) - 1u) &&
        tdma_origin_exchange_publish(&s_tdma_origin.exchange, mailbox, generation);
}

static void tdma_pio_spi_phys_origin_collect_live(tdma_pio_spi_phys_t *phys)
{
    const uint64_t before = vdc_timestamp_clock_read_ticks64();
    const uint32_t epoch = phys->flight_origin_record_epoch;
    tdma_origin_record_frozen_t sample;
    const bool copied = tdma_origin_exchange_copy_live_record(
        &s_tdma_origin.exchange, s_tdma_origin.record, epoch, &sample);
    const uint64_t after = vdc_timestamp_clock_read_ticks64();
    const bool valid = copied && after >= before &&
        after - before <= (uint64_t)vdc_timestamp_clock_tick_hz() *
            TDMA_ORIGIN_RECORD_COPY_MAX_US / 1000000u &&
        epoch == phys->flight_origin_record_epoch;
    tdma_origin_live_snapshot_t *live = &phys->flight_origin_live;
    if (valid && live->retained != 0u && live->active != 0u &&
        live->sample.epoch == sample.epoch &&
        live->sample.published_version == sample.published_version) return;
    const uint32_t guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&phys->flight_origin_record_guard, guard + 1u, __ATOMIC_RELEASE);
    __dmb();
    if (valid) {
        live->sample = sample;
        live->retained = 1u;
        live->active = 1u;
        if (live->copy_count != UINT32_MAX) ++live->copy_count;
    } else if (live->reject_count != UINT32_MAX) {
        ++live->reject_count;
    }
    __atomic_store_n(&phys->flight_origin_record_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool tdma_pio_spi_phys_origin_observe(void *context, tdma_origin_observation_t *observation)
{
    if (observation == NULL || !tdma_pio_spi_phys_origin_healthy(context)) return false;
    tdma_pio_spi_phys_origin_collect_live(context);
    return tdma_origin_exchange_observe(&s_tdma_origin.exchange, observation);
}

bool tdma_pio_spi_phys_origin_get_live_snapshot(const tdma_pio_spi_phys_t *phys,
    tdma_origin_live_snapshot_t *out)
{
    if (phys == NULL || out == NULL) return false;
    const uint64_t before = vdc_timestamp_clock_read_ticks64();
    const uint32_t guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) != 0u) return false;
    const tdma_origin_live_snapshot_t snapshot = phys->flight_origin_live;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint32_t after_guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_ACQUIRE);
    const uint64_t after = vdc_timestamp_clock_read_ticks64();
    if (guard != after_guard || after < before ||
        after - before > (uint64_t)vdc_timestamp_clock_tick_hz() *
            TDMA_ORIGIN_RECORD_COPY_MAX_US / 1000000u) return false;
    *out = snapshot;
    return true;
}

bool tdma_pio_spi_phys_origin_grant_tx(void *context, tdma_overlay_prepare_t *job)
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL || job == NULL || phys->overlay_preparation != job ||
        tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_IDLE ||
        !tdma_pio_spi_phys_origin_ready(context)) return false;
    /* Origin uses only a private mailbox in the existing station. Core0
     * neither receives an MMIO pointer nor leases a live DMA shadow. */
    job->plan = NULL;
    job->config = (tdma_flight_overlay_config_t){
        .packet_size = TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size,
        .local_slot_id = phys->flight_local_slot_id,
    };
    return true;
}

bool tdma_pio_spi_phys_origin_commit_tx(void *context, tdma_overlay_prepare_t *job, uint32_t *generation)
{
    tdma_pio_spi_phys_t *phys = context;
    if (generation != NULL) *generation = 0u;
    return phys != NULL && job != NULL && phys->overlay_preparation == job &&
        tdma_pio_spi_phys_origin_healthy(context) &&
        tdma_overlay_prepare_origin_current(job,
            TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size, phys->flight_local_slot_id) &&
        /* READY is the release/acquire publication of Core0's validation
         * of this same private mailbox. Only Core1 can release/reuse it. */
        tdma_origin_exchange_publish(&s_tdma_origin.exchange, job->tx_data, generation);
}

bool tdma_pio_spi_phys_origin_take_rx_observation(void *context,
    tdma_origin_observation_t *observation)
{
    tdma_pio_spi_phys_t *phys = context;
    if (!tdma_pio_spi_phys_origin_healthy(context) || observation == NULL ||
        !phys->flight_origin_rx_observation_ready) return false;
    *observation = s_tdma_origin.rx_observation;
    phys->flight_origin_rx_observation_ready = false;
    return true;
}

bool tdma_pio_spi_phys_origin_get_frozen_record(const tdma_pio_spi_phys_t *phys,
    uint32_t age, tdma_origin_record_frozen_t *out)
{
    if (phys == NULL || out == NULL || age >= TDMA_ORIGIN_RECORD_COUNT - 1u) return false;
    /* Bound a preempted reader so guard wrap cannot create an old-epoch ABA.
     * This is a copy lifetime bound, not a TDMA phase or bus-time grant. */
    const uint64_t before = vdc_timestamp_clock_read_ticks64();
    const uint32_t guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) != 0u ||
        !__atomic_load_n(&phys->flight_origin_record_frozen, __ATOMIC_ACQUIRE)) return false;
    out->epoch = __atomic_load_n(&phys->flight_origin_record_epoch, __ATOMIC_ACQUIRE);
    out->published_version = __atomic_load_n(&phys->flight_origin_record_published_version, __ATOMIC_ACQUIRE);
    out->fault = __atomic_load_n(&phys->flight_origin_record_fault, __ATOMIC_ACQUIRE);
    const uint32_t index = (out->published_version / 2u - 1u - age) % TDMA_ORIGIN_RECORD_COUNT;
    memcpy(&out->record, &s_tdma_origin.record[index], sizeof(out->record));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint32_t after_guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_ACQUIRE);
    const uint64_t after = vdc_timestamp_clock_read_ticks64();
    return guard == after_guard && after >= before &&
        after - before <= (uint64_t)vdc_timestamp_clock_tick_hz() *
            TDMA_ORIGIN_RECORD_COPY_MAX_US / 1000000u &&
        (out->published_version & 1u) == 0u && out->epoch != 0u &&
        out->record.epoch == out->epoch && out->record.format == TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME &&
        out->record.observation.sequence == out->record.sequence_end;
}

bool tdma_pio_spi_phys_origin_get_first_record(const tdma_pio_spi_phys_t *phys,
    tdma_origin_first_record_t *out)
{
    if (phys == NULL || out == NULL) return false;
    const uint64_t before = vdc_timestamp_clock_read_ticks64();
    const uint32_t guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_ACQUIRE);
    const uint32_t epoch = __atomic_load_n(&phys->flight_origin_first_readable_epoch, __ATOMIC_ACQUIRE);
    const uint32_t expected = __atomic_load_n(&phys->flight_origin_first_expected_sequence, __ATOMIC_RELAXED);
    const uint32_t commit = __atomic_load_n(&phys->flight_origin_first_record.published_version, __ATOMIC_ACQUIRE);
    if ((guard & 1u) != 0u || epoch == 0u || commit != TDMA_ORIGIN_FIRST_RECORD_VERSION ||
        epoch != __atomic_load_n(&phys->flight_origin_record_epoch, __ATOMIC_ACQUIRE)) return false;
    memcpy(&out->record, &phys->flight_origin_first_record.record, sizeof(out->record));
    out->published_version = commit;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint32_t after_commit = __atomic_load_n(&phys->flight_origin_first_record.published_version, __ATOMIC_ACQUIRE);
    const uint32_t after_epoch = __atomic_load_n(&phys->flight_origin_first_readable_epoch, __ATOMIC_ACQUIRE);
    const uint32_t after_guard = __atomic_load_n(&phys->flight_origin_record_guard, __ATOMIC_ACQUIRE);
    const uint64_t after = vdc_timestamp_clock_read_ticks64();
    /* This body is immutable after commit. STOP may retain it before ring0
     * publishes; later faults and rejected returns do not replace the first
     * sample with a better one. Raw timing remains unqualified evidence. */
    return guard == after_guard && epoch == after_epoch && commit == after_commit &&
        after >= before && after - before <= (uint64_t)vdc_timestamp_clock_tick_hz() *
            TDMA_ORIGIN_RECORD_COPY_MAX_US / 1000000u &&
        out->record.epoch == epoch && out->record.format == TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME &&
        out->record.observation.sequence == expected && out->record.sequence_end == expected;
}

static bool tdma_pio_spi_phys_origin_rx(tdma_pio_spi_phys_t *phys, uint8_t *packet,
    size_t capacity, size_t *packet_size, uint64_t *rx_timestamp_ns)
{
    phys->flight_origin_rx_observation_ready = false;
    *rx_timestamp_ns = 0ull;
    if (capacity < s_tdma_origin.exchange.packet_size ||
        !tdma_pio_spi_phys_origin_healthy(phys) ||
        !tdma_origin_exchange_copy_rx_observation(&s_tdma_origin.exchange, packet, capacity,
                                                 &s_tdma_origin.rx_observation)) return false;
    phys->flight_origin_rx_observation_ready = true;
    *packet_size = s_tdma_origin.exchange.packet_size;
    phys->snapshot.rx_count++;
    phys->snapshot.last_rx_size = *packet_size;
    phys->snapshot.last_rx_timestamp_ns = 0ull;
    phys->snapshot.last_rx_edge_timestamp_ns = 0ull;
    phys->snapshot.last_rx_extract_timestamp_ns = vdc_timestamp_clock_now_ns();
    return true;
}

#undef s_tdma_origin
