/* Included in the physical owner: all SRAM/MMIO addresses and resources are
 * bound here. The exchange API never arms hardware or claims a wire event. */
#define s_tdma_origin (s_tdma_pio_spi_workspace.origin)

static bool tdma_pio_spi_phys_origin_mailbox_valid(const uint8_t *mailbox,
                                                 uint32_t slot,
                                                 uint32_t active_mask)
{
    return mailbox != NULL &&
        ((uint32_t)mailbox[0] | ((uint32_t)mailbox[1] << 8u)) == TDMA_FLIGHT_MAILBOX_MAGIC &&
        mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] == TDMA_FLIGHT_MAILBOX_VERSION &&
        mailbox[3] == TDMA_PROCESS_IMAGE_MESSAGE_CLASS &&
        mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] == slot &&
        (mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] & ~active_mask) == 0u &&
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET) ==
            ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] |
             ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] << 8u));
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
        pio_encode_delay(phys->flight_data_phase_delay_cycles - 1u);
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
    /* Latch words stay reserved. Raw RTT does not establish an absolute epoch. */
    phys->flight_clock_latch_armed = false;
    phys->flight_tx_clock_latch_armed = false;
    pio_interrupt_clear(tx, 1u);
}

bool tdma_pio_spi_phys_origin_begin(
    void *context, const tdma_ring_runtime_config_t *config,
    const uint8_t *returned_packet, size_t packet_size,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count)
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL || config == NULL || returned_packet == NULL ||
        packet_size != TDMA_TRANSPORT_SHORT_PACKET_MAX || !phys->armed ||
        !phys->process_image_enabled || phys->role != TDMA_PIO_SPI_ROLE_MASTER ||
        phys->flight_origin_workspace_owned ||
        phys->flight_origin_prepare.stage != TDMA_ORIGIN_PREPARE_IDLE || phys->flight_tx_pending ||
        phys->flight_tx_completion_pending ||
        s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        s_tdma_pio_spi_tx_dma_channel != TDMA_PIO_SPI_TX_DMA_CHANNEL ||
        s_tdma_pio_spi_rx_dma_channel != TDMA_PIO_SPI_RX_DMA_CHANNEL ||
        config->enabled == 0u ||
        (config->flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) == 0u ||
        config->node_count < 2u || config->node_count > TDMA_FLIGHT_SHORT_SLOT_COUNT ||
        config->node_count != phys->node_count || config->baud_hz != phys->baud_hz ||
        config->local_slot_id != config->reference_slot_id ||
        config->local_slot_id != phys->flight_local_slot_id ||
        config->local_slot_id >= config->node_count ||
        config->tx_dma_channel_id != TDMA_PIO_SPI_TX_DMA_CHANNEL ||
        config->rx_dma_channel_id != TDMA_PIO_SPI_RX_DMA_CHANNEL ||
        phys->flight_payload_size != TDMA_FLIGHT_SHORT_PAYLOAD_SIZE ||
        phys->flight_overlay_alignment_samples < TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES ||
        (phys->flight_normal_capture_copy_stage != 0u && phys->flight_normal_capture_copy_stage != 5u) ||
        (phys->flight_sck_waveform_capture_state != TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE &&
         phys->flight_sck_waveform_capture_state != TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED) ||
        phys->flight_data_phase_delay_cycles == 0u || phys->flight_data_phase_delay_cycles > 32u ||
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
        view.payload_size != TDMA_FLIGHT_SHORT_PAYLOAD_SIZE ||
        view.origin_slot_id != config->local_slot_id || view.hop_count != config->node_count - 1u ||
        view.hop_limit != view.hop_count || view.schedule_crc32 != config->schedule_crc32 ||
        view.ring_profile_crc32 != config->ring_profile_crc32 ||
        view.flags != (TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK | TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE))
        return false;
    phys->flight_origin_prepare = (tdma_origin_prepare_t){
        .live_config = config, .seed = returned_packet,
        .stage = TDMA_ORIGIN_PREPARE_MAILBOX, .prefix_bits = (uint32_t)prefix,
        .abort_poll_count = abort_poll_count, .clk_sys_hz = clock_get_hz(clk_sys)};
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
    if (request->live_config == NULL ||
        memcmp(request->live_config, &request->config, sizeof(request->config)) != 0 ||
        clock_get_hz(clk_sys) != request->clk_sys_hz) goto failed;
    const tdma_ring_runtime_config_t *config = &request->config;
    const uint32_t active_mask = (1u << config->node_count) - 1u;
    const tdma_state_machine_origin_dma_contract_t dma = tdma_state_machine_origin_dma_contract();
    switch ((tdma_origin_prepare_stage_t)request->stage) {
    case TDMA_ORIGIN_PREPARE_MAILBOX:
        if (!tdma_pio_spi_phys_origin_mailbox_valid(request->seed + TDMA_TRANSPORT_FRAME_HEADER_SIZE +
                request->mailbox * TDMA_FLIGHT_SHORT_SLOT_SIZE, request->mailbox, active_mask)) goto failed;
        if (++request->mailbox == config->node_count) request->stage = TDMA_ORIGIN_PREPARE_STOP;
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
    memcpy(s_tdma_origin.capture[0], request->seed, TDMA_TRANSPORT_SHORT_PACKET_MAX);
    memset(s_tdma_origin.capture[0] + TDMA_TRANSPORT_FRAME_HEADER_SIZE +
        TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET, 0, TDMA_FLIGHT_DPLL_OBSERVATION_SIZE);
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
        },
        .physical_bytes = phys->flight_physical_byte_count,
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
    };
    memcpy(&plan_config.returned_route_word, s_tdma_origin.capture[0] + 12u, sizeof(uint32_t));
    s_tdma_origin.plan = (tdma_origin_plan_t){.runs = s_tdma_origin.runs,
        .literals = s_tdma_origin.literals, .run_capacity = TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY,
        .literal_capacity = TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY};
    memset(&s_tdma_origin.builder, 0, sizeof(s_tdma_origin.builder));
    if (!tdma_origin_plan_begin(&s_tdma_origin.builder, &plan_config, &s_tdma_origin.plan)) goto failed;
    request->stage = TDMA_ORIGIN_PREPARE_BUILD_STEP;
    return TDMA_ORIGIN_BUILD_BUSY;
    }
    case TDMA_ORIGIN_PREPARE_BUILD_STEP: {
        const tdma_origin_build_result_t result = tdma_origin_plan_step(&s_tdma_origin.builder);
        if (result == TDMA_ORIGIN_BUILD_FAILED) goto failed;
        if (result == TDMA_ORIGIN_BUILD_DONE) request->stage = TDMA_ORIGIN_PREPARE_SEED;
        return TDMA_ORIGIN_BUILD_BUSY;
    }
    case TDMA_ORIGIN_PREPARE_SEED: {
    uint8_t seed[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    memcpy(seed, s_tdma_origin.capture[0], sizeof(seed));
    memcpy(s_tdma_origin.capture[1], seed, sizeof(seed));
    uint32_t sequence;
    memcpy(&sequence, seed + 8u, sizeof(sequence));
    tdma_transport_result_t result;
    if (!tdma_transport_frame_prepare_resident_position(seed, sizeof(seed),
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
    s_tdma_origin.stage[2] = (sizeof(seed) & 0xffu) << 8u;
    s_tdma_origin.stage[3] = (sizeof(seed) >> 8u) << 8u;
    s_tdma_origin.state = (tdma_origin_plan_state_t){.capture_bank = 1u,
        .local_next_address = s_tdma_origin.plan.local_entry[0],
        .local_selected_generation = 1u, .bank_version = {2u, 2u}};
    if (!tdma_origin_exchange_bind(&s_tdma_origin.exchange, &s_tdma_origin.state,
            s_tdma_origin.capture[0], s_tdma_origin.capture[1],
            s_tdma_origin.local_shadow[0], s_tdma_origin.local_shadow[1],
            s_tdma_origin.plan.local_entry[0], s_tdma_origin.plan.local_entry[1])) goto failed;
    request->stage = TDMA_ORIGIN_PREPARE_SMS;
    return TDMA_ORIGIN_BUILD_BUSY;
    }
    case TDMA_ORIGIN_PREPARE_SMS:
    tdma_pio_spi_phys_origin_configure_sms(phys, phys->flight_origin_cadence.divider256);
    request->stage = TDMA_ORIGIN_PREPARE_INSTALL;
    return TDMA_ORIGIN_BUILD_BUSY;
    case TDMA_ORIGIN_PREPARE_INSTALL: {
    if (!phys->flight_origin_workspace_owned || !phys->flight_origin_resource_claimed ||
        !phys->flight_resource_claimed || phys->armed ||
        s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN ||
        !tdma_state_machine_origin_dma_contract_valid(&dma) ||
        s_tdma_pio_spi_tx_dma_channel != dma.output_dma || s_tdma_pio_spi_rx_dma_channel != dma.capture_dma ||
        s_tdma_pio_spi_command_dma_channel != dma.loader_dma ||
        s_tdma_pio_spi_executor_dma_channel != dma.executor_dma) goto failed;
    phys->flight_normal_capture_copy_stage = 0u;
    phys->flight_overlay_alignment_locked = true;
    phys->flight_overlay_dma_active = true;
    phys->rx_capture_active = true;
    __dmb();
    dma_channel_config loader = dma_channel_get_default_config(dma.loader_dma);
    channel_config_set_transfer_data_size(&loader, DMA_SIZE_32);
    channel_config_set_read_increment(&loader, true);
    channel_config_set_write_increment(&loader, true);
    channel_config_set_ring(&loader, true, dma.descriptor_write_ring_log2);
    channel_config_set_high_priority(&loader, true);
    channel_config_set_chain_to(&loader, dma.loader_dma);
    /* AL3's final write triggers the executor; CHAIN_TO must not trigger it
     * a second time. The graph alone supplies all later child/launch words. */
    dma_channel_configure(dma.loader_dma, &loader, &dma_hw->ch[dma.executor_dma].al3_ctrl,
        (const void *)(uintptr_t)s_tdma_origin.plan.seed_entry, dma.descriptor_words, false);
    pio_sm_set_enabled(phys->flight_resources.rx_pio, dma.helper_sm, true);
    pio_sm_set_enabled(phys->flight_resources.tx_pio, phys->flight_resources.tx_control_out_sm, true);
    tdma_pio_spi_phys_set_line_drivers(true);
    dma_start_channel_mask(1u << dma.loader_dma);
    phys->armed = true;
    request->stage = TDMA_ORIGIN_PREPARE_COMPLETE;
    request->live_config = NULL;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return TDMA_ORIGIN_BUILD_DONE;
    }
    default:
        break;
    }
failed:
    tdma_pio_spi_phys_pause_sm_pair(phys);
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

bool tdma_pio_spi_phys_origin_observe(void *context, tdma_origin_observation_t *observation)
{
    return tdma_pio_spi_phys_origin_healthy(context) &&
        tdma_origin_exchange_observe(&s_tdma_origin.exchange, observation);
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

static bool tdma_pio_spi_phys_origin_rx(tdma_pio_spi_phys_t *phys, uint8_t *packet,
    size_t capacity, size_t *packet_size, uint64_t *rx_timestamp_ns)
{
    phys->flight_origin_rx_observation_ready = false;
    *rx_timestamp_ns = 0ull;
    if (capacity < TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        !tdma_pio_spi_phys_origin_healthy(phys) ||
        !tdma_origin_exchange_copy_rx_observation(&s_tdma_origin.exchange, packet,
                                                 &s_tdma_origin.rx_observation)) return false;
    phys->flight_origin_rx_observation_ready = true;
    *packet_size = TDMA_TRANSPORT_SHORT_PACKET_MAX;
    phys->snapshot.rx_count++;
    phys->snapshot.last_rx_size = *packet_size;
    phys->snapshot.last_rx_timestamp_ns = 0ull;
    phys->snapshot.last_rx_edge_timestamp_ns = 0ull;
    phys->snapshot.last_rx_extract_timestamp_ns = vdc_timestamp_clock_now_ns();
    return true;
}

#undef s_tdma_origin
