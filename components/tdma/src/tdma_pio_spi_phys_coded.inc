static void tdma_pio_spi_phys_coded_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->coded_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_coded_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->coded_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_coded_publish_error(
    tdma_pio_spi_phys_t *phys,
    uint32_t epoch,
    tdma_pio_spi_coded_reject_t reason)
{
    tdma_pio_spi_phys_coded_write_begin(phys);
    memset(&phys->coded, 0, sizeof(phys->coded));
    phys->coded.version = TDMA_PIO_SPI_CODED_SNAPSHOT_VERSION;
    phys->coded.state = TDMA_PIO_SPI_CODED_ERROR;
    phys->coded.role = phys->role;
    phys->coded.flags = TDMA_PIO_SPI_CODED_FLAG_DIAGNOSTIC_ONLY;
    phys->coded.reject_reason = (uint32_t)reason;
    phys->coded.epoch = epoch;
    phys->coded.tx_dma_channel = TDMA_PIO_SPI_TX_DMA_CHANNEL;
    phys->coded.rx_dma_channel = TDMA_PIO_SPI_RX_DMA_CHANNEL;
    tdma_pio_spi_phys_coded_write_end(phys);
}

static void tdma_pio_spi_phys_prepare_maintenance_mapping(
    tdma_pio_spi_phys_t *phys)
{
    phys->tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    phys->rx_sm = BOARD_TDMA_SPI_SLAVE_SM;
    phys->tx_sck_pin = BOARD_TDMA_SPI_DOWNLINK_SCK_PIN;
    phys->tx_csn_pin = BOARD_TDMA_SPI_DOWNLINK_CSN_PIN;
    phys->tx_pin = BOARD_TDMA_SPI_DOWNLINK_TX_PIN;
    phys->rx_sck_pin = BOARD_TDMA_SPI_UPLINK_SCK_PIN;
    phys->rx_csn_pin = BOARD_TDMA_SPI_UPLINK_CSN_PIN;
    phys->rx_pin = BOARD_TDMA_SPI_UPLINK_RX_PIN;
    phys->armed = true;
}

static void tdma_pio_spi_phys_reset_normal_capture(void)
{
    (void)__atomic_add_fetch(&s_tdma_pio_spi_tx_history_guard,
                             1u, __ATOMIC_ACQ_REL);
    memset(s_tdma_pio_spi_tx_last_frame, 0,
           sizeof(s_tdma_pio_spi_tx_last_frame));
    s_tdma_pio_spi_tx_history_produced = 0u;
    s_tdma_pio_spi_tx_last_frame_bytes = 0u;
    s_tdma_pio_spi_tx_complete_frame_count = 0u;
    (void)__atomic_add_fetch(&s_tdma_pio_spi_tx_history_guard,
                             1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_record_complete_tx_frame(
    const uint8_t *header,
    const uint8_t *packet,
    size_t packet_size)
{
    const uint32_t frame_bytes =
        (uint32_t)packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    if (header == NULL || packet == NULL ||
        frame_bytes > TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES) {
        return;
    }
    (void)__atomic_add_fetch(&s_tdma_pio_spi_tx_history_guard,
                             1u, __ATOMIC_ACQ_REL);
    for (uint32_t index = 0u;
         index < TDMA_PIO_SPI_PACKET_HEADER_SIZE; index++) {
        s_tdma_pio_spi_tx_last_frame[index] = header[index];
    }
    for (uint32_t index = 0u; index < (uint32_t)packet_size; index++) {
        s_tdma_pio_spi_tx_last_frame[
            TDMA_PIO_SPI_PACKET_HEADER_SIZE + index] = packet[index];
    }
    s_tdma_pio_spi_tx_last_frame_bytes = frame_bytes;
    s_tdma_pio_spi_tx_history_produced += frame_bytes;
    s_tdma_pio_spi_tx_complete_frame_count++;
    (void)__atomic_add_fetch(&s_tdma_pio_spi_tx_history_guard,
                             1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_prepare_maintenance_pins(
    tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_prepare_maintenance_mapping(phys);
    tdma_pio_spi_phys_set_line_drivers(true);
}

bool tdma_pio_spi_phys_coded_start(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_coded_request_t *request)
{
    if (phys == NULL || request == NULL) {
        if (phys != NULL) {
            tdma_pio_spi_phys_coded_publish_error(
                phys, 0u, TDMA_PIO_SPI_CODED_REJECT_BAD_ARGUMENT);
        }
        return false;
    }
    /* A rejected overlapping request must not overwrite the guarded facts of
     * the transfer that still owns both DMA channels. */
    if (phys->coded.state == TDMA_PIO_SPI_CODED_RUNNING ||
        phys->coded.state == TDMA_PIO_SPI_CODED_FORWARDING ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING) {
        return false;
    }
    if (phys->role != TDMA_PIO_SPI_ROLE_MASTER &&
        phys->role != TDMA_PIO_SPI_ROLE_SLAVE) {
        tdma_pio_spi_phys_coded_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_CODED_REJECT_BAD_ARGUMENT);
        return false;
    }

    const uint32_t capture_words =
        (request->capture_sample_count + 31u) / 32u;
    if (phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        (request->tx_words == NULL || request->tx_word_count == 0u ||
         request->tx_word_count > TDMA_PIO_SPI_CODED_BUFFER_WORDS ||
         request->tx_sample_count == 0u ||
         request->tx_sample_count > request->tx_word_count * 32u ||
         request->capture_sample_count < request->tx_sample_count ||
         capture_words == 0u ||
         capture_words > TDMA_PIO_SPI_CODED_BUFFER_WORDS)) {
        tdma_pio_spi_phys_coded_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_CODED_REJECT_BAD_ARGUMENT);
        return false;
    }

    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    phys->rx_capture_active = false;
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED)) {
        tdma_pio_spi_phys_coded_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_CODED_REJECT_RESOURCE);
        return false;
    }

    if (!phys->armed) {
        tdma_pio_spi_phys_prepare_maintenance_pins(phys);
    }
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);

    tdma_pio_spi_phys_coded_write_begin(phys);
    memset(&phys->coded, 0, sizeof(phys->coded));
    phys->coded.version = TDMA_PIO_SPI_CODED_SNAPSHOT_VERSION;
    phys->coded.role = phys->role;
    phys->coded.flags = TDMA_PIO_SPI_CODED_FLAG_DIAGNOSTIC_ONLY;
    phys->coded.epoch = request->epoch;
    phys->coded.tx_dma_channel = TDMA_PIO_SPI_TX_DMA_CHANNEL;
    phys->coded.rx_dma_channel = TDMA_PIO_SPI_RX_DMA_CHANNEL;
    phys->coded.tx_word_count = request->tx_word_count;
    phys->coded.tx_sample_count = request->tx_sample_count;
    phys->coded.capture_word_count = capture_words;
    phys->coded.capture_sample_count = request->capture_sample_count;
    phys->coded.timing_field_tx_origin_sample =
        request->timing_field_tx_origin_sample;

    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        tdma_pio_spi_clk_forward_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_clk_forward_offset,
            phys->rx_sck_pin, phys->tx_sck_pin);
        phys->coded.state = TDMA_PIO_SPI_CODED_FORWARDING;
        phys->coded.flags |= TDMA_PIO_SPI_CODED_FLAG_FORWARD_ONLY;
        tdma_pio_spi_phys_coded_write_end(phys);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, true);
        return true;
    }
    tdma_pio_spi_phys_coded_write_end(phys);

    if (!tdma_pio_spi_phys_ensure_tx_dma() ||
        !tdma_pio_spi_phys_ensure_rx_dma()) {
        tdma_pio_spi_phys_coded_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_CODED_REJECT_RESOURCE);
        tdma_pio_spi_phys_cal_cleanup(phys);
        return false;
    }

    memcpy(s_tdma_pio_spi_coded_tx, request->tx_words,
           request->tx_word_count * sizeof(request->tx_words[0]));
    memset(s_tdma_pio_spi_coded_rx, 0,
           capture_words * sizeof(s_tdma_pio_spi_coded_rx[0]));
    tdma_pio_spi_clk_coded_tx_program_init(
        BOARD_TDMA_SPI_PIO, phys->tx_sm,
        s_tdma_pio_spi_clk_coded_tx_offset, phys->tx_sck_pin, false);
    tdma_pio_spi_clk_oversample_program_init(
        BOARD_TDMA_SPI_PIO, phys->rx_sm,
        s_tdma_pio_spi_clk_oversample_offset, phys->rx_sck_pin);

    dma_channel_config tx_dc = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_tx_dma_channel);
    channel_config_set_transfer_data_size(&tx_dc, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_dc, true);
    channel_config_set_write_increment(&tx_dc, false);
    channel_config_set_dreq(
        &tx_dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->tx_sm, true));
    dma_channel_configure((uint)s_tdma_pio_spi_tx_dma_channel, &tx_dc,
                          &BOARD_TDMA_SPI_PIO->txf[phys->tx_sm],
                          s_tdma_pio_spi_coded_tx,
                          request->tx_word_count, false);

    dma_channel_config rx_dc = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_dc, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_dc, false);
    channel_config_set_write_increment(&rx_dc, true);
    channel_config_set_dreq(
        &rx_dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, false));
    dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &rx_dc,
                          s_tdma_pio_spi_coded_rx,
                          &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                          capture_words, false);

    tdma_pio_spi_phys_coded_write_begin(phys);
    phys->coded.state = TDMA_PIO_SPI_CODED_RUNNING;
    phys->coded.capture_origin_tick = vdc_timestamp_clock_now_ns();
    phys->coded.tx_dma_remaining = request->tx_word_count;
    phys->coded.rx_dma_remaining = capture_words;
    tdma_pio_spi_phys_coded_write_end(phys);

    dma_start_channel_mask((1u << (uint)s_tdma_pio_spi_tx_dma_channel) |
                           (1u << (uint)s_tdma_pio_spi_rx_dma_channel));
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                               (1u << phys->tx_sm) | (1u << phys->rx_sm));
    return true;
}

void tdma_pio_spi_phys_coded_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    tdma_pio_spi_phys_cal_cleanup(phys);
    tdma_pio_spi_phys_coded_write_begin(phys);
    phys->coded.state = TDMA_PIO_SPI_CODED_IDLE;
    phys->coded.reject_reason = TDMA_PIO_SPI_CODED_REJECT_NONE;
    phys->coded.tx_dma_remaining = 0u;
    phys->coded.rx_dma_remaining = 0u;
    tdma_pio_spi_phys_coded_write_end(phys);
    (void)tdma_pio_spi_phys_select_program_persona(
        phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

void tdma_pio_spi_phys_coded_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || phys->coded.state != TDMA_PIO_SPI_CODED_RUNNING ||
        s_tdma_pio_spi_tx_dma_channel < 0 ||
        s_tdma_pio_spi_rx_dma_channel < 0) {
        return;
    }
    const uint32_t tx_remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_tx_dma_channel].transfer_count;
    const uint32_t rx_remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
    const bool rx_stalled =
        pio_sm_is_rx_fifo_full(BOARD_TDMA_SPI_PIO, phys->rx_sm);

    tdma_pio_spi_phys_coded_write_begin(phys);
    phys->coded.tx_dma_remaining = tx_remaining;
    phys->coded.rx_dma_remaining = rx_remaining;
    if (tx_remaining == 0u) {
        phys->coded.flags |= TDMA_PIO_SPI_CODED_FLAG_TX_DMA_COMPLETE;
    }
    if (rx_stalled && rx_remaining != 0u) {
        phys->coded.pio_stall_count++;
        phys->coded.dma_overrun_count++;
    }
    if (rx_remaining == 0u) {
        phys->coded.flags |= TDMA_PIO_SPI_CODED_FLAG_RX_DMA_COMPLETE;
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
        if (tx_remaining != 0u) {
            phys->coded.state = TDMA_PIO_SPI_CODED_ERROR;
            phys->coded.reject_reason =
                TDMA_PIO_SPI_CODED_REJECT_CAPTURE_SHORT;
        } else if (phys->coded.pio_stall_count != 0u) {
            phys->coded.state = TDMA_PIO_SPI_CODED_ERROR;
            phys->coded.reject_reason = TDMA_PIO_SPI_CODED_REJECT_PIO_STALL;
        } else {
            phys->coded.state = TDMA_PIO_SPI_CODED_COMPLETE;
            phys->coded.reject_reason = TDMA_PIO_SPI_CODED_REJECT_NONE;
        }
    }
    const bool finished =
        phys->coded.state == TDMA_PIO_SPI_CODED_COMPLETE ||
        phys->coded.state == TDMA_PIO_SPI_CODED_ERROR;
    tdma_pio_spi_phys_coded_write_end(phys);
    if (finished) {
        tdma_pio_spi_phys_cal_cleanup(phys);
        (void)tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    }
}

bool tdma_pio_spi_phys_get_coded_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_coded_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->coded_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->coded;
        const uint32_t end =
            __atomic_load_n(&phys->coded_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool tdma_pio_spi_phys_copy_coded_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    tdma_pio_spi_coded_snapshot_t snapshot;
    if (capture_word_count != NULL) *capture_word_count = 0u;
    if (phys == NULL || capture_words == NULL || capture_word_count == NULL ||
        !tdma_pio_spi_phys_get_coded_snapshot(phys, &snapshot) ||
        snapshot.state != TDMA_PIO_SPI_CODED_COMPLETE ||
        snapshot.capture_word_count > capture_word_capacity) {
        return false;
    }
    memcpy(capture_words, s_tdma_pio_spi_coded_rx,
           snapshot.capture_word_count * sizeof(capture_words[0]));
    *capture_word_count = snapshot.capture_word_count;
    return true;
}
