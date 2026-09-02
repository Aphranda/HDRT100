static void tdma_pio_spi_phys_data_train_write_begin(
    tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->data_train_guard, 1u, __ATOMIC_RELEASE);
}
static void tdma_pio_spi_phys_data_train_write_end(
    tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->data_train_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_data_train_publish_error(
    tdma_pio_spi_phys_t *phys, uint32_t epoch,
    tdma_pio_spi_data_train_reject_t reason)
{
    tdma_pio_spi_phys_data_train_write_begin(phys);
    memset(&phys->data_train, 0, sizeof(phys->data_train));
    phys->data_train.version = TDMA_PIO_SPI_DATA_TRAIN_SNAPSHOT_VERSION;
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
    phys->data_train.flags =
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_DIAGNOSTIC_ONLY;
    phys->data_train.reject_reason = (uint32_t)reason;
    phys->data_train.epoch = epoch;
    tdma_pio_spi_phys_data_train_write_end(phys);
}

static void tdma_pio_spi_phys_data_train_set_drivers(uint32_t role)
{
    /* DATA0_OUT returns toward the link initiator through the upstream
     * transceiver. The initiator drives only the downstream TRIG marker. */
    gpio_put(BOARD_UP_BISS_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER);
    gpio_put(BOARD_DN_BISS_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE);
    gpio_put(BOARD_TRIG_DE_PIN,
             role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR);
}

static uint64_t tdma_pio_spi_phys_data_train_capture_tick(
    uint32_t marker_to_data_delay_cycles,
    uint32_t phase_delay_cycles,
    uint32_t source_phase_delay_cycles)
{
    /* A diagnostic candidate may put the requested phase before the source
     * phase.  Keep the published tick bounded instead of allowing an
     * unsigned subtraction to wrap into a misleading huge timestamp. */
    const int64_t relative_tick =
        (int64_t)marker_to_data_delay_cycles +
        (int64_t)phase_delay_cycles -
        (int64_t)source_phase_delay_cycles;
    return relative_tick > 0 ? (uint64_t)relative_tick + 1ull : 1ull;
}

bool tdma_pio_spi_phys_data_train_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_data_train_request_t *request)
{
    if (phys == NULL || request == NULL ||
        (request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER &&
         request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE &&
         request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION) ||
        request->marker_to_data_delay_cycles == 0u ||
        request->marker_to_data_delay_cycles >
            TDMA_PIO_SPI_DATA_TRAIN_MAX_DELAY_CYCLES ||
        request->source_phase_delay_cycles == 0u ||
        request->source_phase_delay_cycles >
            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES ||
        request->phase_delay_cycles == 0u ||
        request->phase_delay_cycles >
            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES ||
        (request->diagnostic_fault_flags &
         ~TDMA_PIO_SPI_DATA_TRAIN_FAULT_ALL) != 0u ||
        (request->diagnostic_fault_flags != 0u &&
         request->role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR) ||
        (request->diagnostic_fault_flags != 0u &&
         (request->diagnostic_fault_flags &
          (request->diagnostic_fault_flags - 1u)) != 0u) ||
        request->data_sample_count == 0u ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         (request->marker_words == NULL ||
          request->marker_word_count == 0u ||
          request->marker_word_count > TDMA_PIO_SPI_MARKER_BUFFER_WORDS ||
          request->capture_sample_count < request->data_sample_count ||
           request->capture_sample_count == 0u ||
           request->capture_sample_count >
               TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS * 32u ||
           (((request->diagnostic_fault_flags &
              TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u) &&
            request->capture_sample_count <= 32u))) ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER &&
         (request->data_words == NULL || request->data_word_count == 0u ||
          request->data_word_count > TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS ||
          request->data_sample_count > request->data_word_count * 32u)) ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE &&
         (request->data_words == NULL || request->data_word_count == 0u ||
          request->data_word_count > TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS ||
          request->data_sample_count > request->data_word_count * 32u)) ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION &&
         (request->capture_sample_count < request->data_sample_count ||
          request->capture_sample_count == 0u ||
          request->capture_sample_count >
              TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS * 32u))) {
        if (phys != NULL) {
            tdma_pio_spi_phys_data_train_publish_error(
                phys, request == NULL ? 0u : request->epoch,
                TDMA_PIO_SPI_DATA_TRAIN_REJECT_BAD_ARGUMENT);
        }
        return false;
    }
    if (phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_ARMED ||
        phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_RUNNING ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING ||
        phys->coded.state == TDMA_PIO_SPI_CODED_RUNNING ||
        phys->coded.state == TDMA_PIO_SPI_CODED_FORWARDING ||
        phys->p3.state == TDMA_PIO_SPI_P3_ARMED ||
        phys->cal_loopback.armed != 0u) {
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
    tdma_pio_spi_phys_prepare_maintenance_mapping(phys);
    if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER) {
        gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_HIGH);
        gpio_set_outover(phys->tx_pin, GPIO_OVERRIDE_LOW);
    } else if (request->role ==
               TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE) {
        gpio_set_outover(phys->tx_sck_pin, GPIO_OVERRIDE_LOW);
    }
    tdma_pio_spi_phys_data_train_set_drivers(
        TDMA_PIO_SPI_DATA_TRAIN_ROLE_NONE);
    if (!tdma_pio_spi_phys_select_program_persona(
            phys,
            (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
             request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION)
                ? TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN
                : TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN) ||
        !tdma_pio_spi_phys_ensure_rx_dma() ||
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         !tdma_pio_spi_phys_ensure_tx_dma())) {
        gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
        gpio_set_outover(phys->tx_pin, GPIO_OVERRIDE_NORMAL);
        gpio_set_outover(phys->tx_sck_pin, GPIO_OVERRIDE_NORMAL);
        tdma_pio_spi_phys_data_train_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_DATA_TRAIN_REJECT_RESOURCE);
        return false;
    }

    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 4u);
    BOARD_TDMA_SPI_PIO->fdebug =
        1u << (PIO_FDEBUG_TXSTALL_LSB + phys->rx_sm);

    const uint32_t capture_words =
        (request->capture_sample_count + 31u) / 32u;
    const bool inject_rx_dma_pause =
        (request->diagnostic_fault_flags &
         TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_PAUSE) != 0u;
    const bool inject_rx_dma_short =
        (request->diagnostic_fault_flags &
         TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u;
    const uint32_t capture_dma_words =
        inject_rx_dma_short ? capture_words - 1u : capture_words;
    const bool phase_order_warning =
        request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
        request->phase_delay_cycles < request->source_phase_delay_cycles;
    if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR) {
        memcpy(s_tdma_pio_spi_marker_tx, request->marker_words,
               request->marker_word_count * sizeof(request->marker_words[0]));
        tdma_pio_spi_marker_origin_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_marker_origin_offset, phys->tx_csn_pin);
        tdma_pio_spi_data_train_sink_program_init(
            BOARD_TDMA_SPI_PIO, phys->rx_sm,
            s_tdma_pio_spi_data_train_sink_offset,
            phys->tx_csn_pin, phys->rx_pin, request->phase_delay_cycles);
        pio_sm_put(BOARD_TDMA_SPI_PIO, phys->rx_sm,
                   request->marker_to_data_delay_cycles - 1u);

        dma_channel_config marker_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_tx_dma_channel);
        channel_config_set_transfer_data_size(&marker_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&marker_dc, true);
        channel_config_set_write_increment(&marker_dc, false);
        channel_config_set_dreq(
            &marker_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->tx_sm, true));
        dma_channel_configure((uint)s_tdma_pio_spi_tx_dma_channel,
                              &marker_dc,
                              &BOARD_TDMA_SPI_PIO->txf[phys->tx_sm],
                              s_tdma_pio_spi_marker_tx,
                              request->marker_word_count, false);

        memset(s_tdma_pio_spi_data_train_rx, 0,
               capture_words * sizeof(s_tdma_pio_spi_data_train_rx[0]));
        dma_channel_config capture_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&capture_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&capture_dc, false);
        channel_config_set_write_increment(&capture_dc, true);
        channel_config_set_dreq(
            &capture_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, false));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel,
                               &capture_dc, s_tdma_pio_spi_data_train_rx,
                               &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                               capture_dma_words, false);
    } else if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER) {
        memcpy(s_tdma_pio_spi_data_train_tx, request->data_words,
               request->data_word_count * sizeof(request->data_words[0]));
        tdma_pio_spi_data_train_source_program_init(
            BOARD_TDMA_SPI_PIO, phys->rx_sm,
            s_tdma_pio_spi_data_train_source_offset,
            phys->rx_csn_pin, phys->tx_pin, request->phase_delay_cycles);
        pio_sm_put(BOARD_TDMA_SPI_PIO, phys->rx_sm,
                   request->marker_to_data_delay_cycles - 1u);

        dma_channel_config data_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&data_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&data_dc, true);
        channel_config_set_write_increment(&data_dc, false);
        channel_config_set_dreq(
            &data_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, true));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &data_dc,
                              &BOARD_TDMA_SPI_PIO->txf[phys->rx_sm],
                              s_tdma_pio_spi_data_train_tx,
                              request->data_word_count, false);
    } else if (request->role ==
               TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE) {
        memcpy(s_tdma_pio_spi_data_train_tx, request->data_words,
               request->data_word_count * sizeof(request->data_words[0]));
        tdma_pio_spi_sck_train_trigger_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_sck_train_trigger_offset, phys->tx_csn_pin);
        tdma_pio_spi_sck_train_source_program_init(
            BOARD_TDMA_SPI_PIO, phys->rx_sm,
            s_tdma_pio_spi_sck_train_source_offset, phys->tx_csn_pin,
            phys->tx_sck_pin, request->phase_delay_cycles);
        pio_sm_put(BOARD_TDMA_SPI_PIO, phys->rx_sm,
                   request->marker_to_data_delay_cycles - 1u);

        dma_channel_config sck_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&sck_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&sck_dc, true);
        channel_config_set_write_increment(&sck_dc, false);
        channel_config_set_dreq(
            &sck_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, true));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &sck_dc,
                              &BOARD_TDMA_SPI_PIO->txf[phys->rx_sm],
                              s_tdma_pio_spi_data_train_tx,
                              request->data_word_count, false);
    } else {
        memset(s_tdma_pio_spi_data_train_rx, 0,
               capture_words * sizeof(s_tdma_pio_spi_data_train_rx[0]));
        tdma_pio_spi_sck_train_sink_program_init(
            BOARD_TDMA_SPI_PIO, phys->rx_sm,
            s_tdma_pio_spi_sck_train_sink_offset, phys->rx_sck_pin,
            request->phase_delay_cycles);

        dma_channel_config capture_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&capture_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&capture_dc, false);
        channel_config_set_write_increment(&capture_dc, true);
        channel_config_set_dreq(
            &capture_dc,
            pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, false));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel,
                              &capture_dc, s_tdma_pio_spi_data_train_rx,
                              &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                              capture_words, false);
    }

    tdma_pio_spi_phys_data_train_write_begin(phys);
    memset(&phys->data_train, 0, sizeof(phys->data_train));
    phys->data_train.version = TDMA_PIO_SPI_DATA_TRAIN_SNAPSHOT_VERSION;
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ARMED;
    phys->data_train.role = request->role;
    phys->data_train.flags =
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_DIAGNOSTIC_ONLY |
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_ORIGIN |
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_DATA |
        (phase_order_warning
             ? TDMA_PIO_SPI_DATA_TRAIN_FLAG_PHASE_ORDER_WARNING
             : 0u);
    phys->data_train.epoch = request->epoch;
    phys->data_train.marker_word_count =
        request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE
            ? 1u
            : request->marker_word_count;
    phys->data_train.data_word_count = request->data_word_count;
    phys->data_train.data_sample_count = request->data_sample_count;
    phys->data_train.capture_word_count = capture_words;
    phys->data_train.capture_sample_count = request->capture_sample_count;
    phys->data_train.marker_to_data_delay_cycles =
        request->marker_to_data_delay_cycles;
    phys->data_train.source_phase_delay_cycles =
        request->source_phase_delay_cycles;
    phys->data_train.phase_delay_cycles = request->phase_delay_cycles;
    phys->data_train.marker_dma_remaining =
        request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE
            ? 0u
            : request->marker_word_count;
    phys->data_train.data_dma_remaining =
        (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER ||
         request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE)
            ? request->data_word_count : capture_dma_words;
    phys->data_train.diagnostic_fault_flags =
        request->diagnostic_fault_flags;
    /* A destination may be armed several seconds before the source while
     * four USB CDC endpoints are configured sequentially.  Like the TRN-01
     * follower, it starts its transfer timeout only after RX DMA proves that
     * the physical marker gate opened and DATA capture actually began. */
    phys->data_train_deadline_ns = 0u;
    tdma_pio_spi_phys_data_train_write_end(phys);

    if (!inject_rx_dma_pause) {
        dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    }
    tdma_pio_spi_phys_data_train_set_drivers(request->role);
    if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR ||
        request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE) {
        pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                                   (1u << phys->tx_sm) |
                                   (1u << phys->rx_sm));
        gpio_set_outover(
            request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE
                ? phys->tx_sck_pin
                : phys->tx_csn_pin,
            GPIO_OVERRIDE_NORMAL);
        if (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE) {
            gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
        }
    } else {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, true);
        gpio_set_outover(phys->tx_pin, GPIO_OVERRIDE_NORMAL);
        gpio_set_outover(phys->tx_sck_pin, GPIO_OVERRIDE_NORMAL);
        gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
    }
    return true;
}

bool tdma_pio_spi_phys_data_train_inject(tdma_pio_spi_phys_t *phys)
{
    const bool sck_source = phys != NULL &&
        phys->data_train.role ==
            TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE;
    if (phys == NULL ||
        phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_ARMED ||
        (phys->data_train.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         !sck_source) ||
        (!sck_source &&
         (s_tdma_pio_spi_tx_dma_channel < 0 ||
          dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)))) {
        return false;
    }
    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_RUNNING;
    phys->data_train.marker_capture_tick = 1ull;
    phys->data_train.data_capture_tick =
        tdma_pio_spi_phys_data_train_capture_tick(
            phys->data_train.marker_to_data_delay_cycles,
            phys->data_train.phase_delay_cycles,
            phys->data_train.source_phase_delay_cycles);
    phys->data_train_deadline_ns = vdc_timestamp_clock_now_ns() +
                                   TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS;
    tdma_pio_spi_phys_data_train_write_end(phys);
    if (sck_source) {
        pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm,
                   s_tdma_pio_spi_sck_train_inject_word);
    } else {
        dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_tx_dma_channel);
    }
    return true;
}

void tdma_pio_spi_phys_data_train_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 4u);
    tdma_pio_spi_phys_cal_cleanup(phys);
    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_IDLE;
    phys->data_train.reject_reason = TDMA_PIO_SPI_DATA_TRAIN_REJECT_NONE;
    phys->data_train.marker_dma_remaining = 0u;
    phys->data_train.data_dma_remaining = 0u;
    tdma_pio_spi_phys_data_train_write_end(phys);
    (void)tdma_pio_spi_phys_select_program_persona(
        phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

void tdma_pio_spi_phys_data_train_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        (phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_ARMED &&
         phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_RUNNING) ||
        s_tdma_pio_spi_rx_dma_channel < 0) {
        return;
    }
    const bool initiator =
        phys->data_train.role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR ||
        phys->data_train.role ==
            TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION;
    const bool injects_dma_origin =
        phys->data_train.role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR;
    const uint32_t marker_remaining =
        injects_dma_origin && s_tdma_pio_spi_tx_dma_channel >= 0
            ? dma_hw->ch[(uint)s_tdma_pio_spi_tx_dma_channel].transfer_count
            : 0u;
    const uint32_t hardware_data_remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
    const uint32_t responder_stall_mask =
        1u << (PIO_FDEBUG_TXSTALL_LSB + phys->rx_sm);
    const bool responder_done = !initiator &&
        (BOARD_TDMA_SPI_PIO->fdebug & responder_stall_mask) != 0u;
    /* A full responder TX FIFO is expected DMA backpressure while the SM waits
     * for CS. Only an undrained initiator RX FIFO represents capture loss. */
    const bool stalled = initiator &&
        pio_sm_is_rx_fifo_full(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    const bool inject_rx_dma_short =
        (phys->data_train.diagnostic_fault_flags &
         TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u;
    /* RX_DMA_PAUSE deliberately leaves the channel untriggered.  On the
     * target DMA implementation an untriggered channel may report a zero
     * hardware transfer_count even though the configured capture has not
     * consumed a single word.  Treating that value as completion races the
     * real PIO FIFO-full evidence and turns the requested PIO_STALL fault
     * into a false COMPLETE/correlation result.  Keep the logical remainder
     * at the requested capture size until the FIFO reports backpressure. */
    const bool inject_rx_dma_pause =
        (phys->data_train.diagnostic_fault_flags &
         TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_PAUSE) != 0u;
    const uint32_t data_remaining = inject_rx_dma_pause
                                        ? phys->data_train.capture_word_count
                                        : hardware_data_remaining;
    const uint32_t armed_capture_words =
        inject_rx_dma_short && phys->data_train.capture_word_count != 0u
            ? phys->data_train.capture_word_count - 1u
            : phys->data_train.capture_word_count;
    const bool capture_started = initiator &&
        data_remaining < armed_capture_words;
    const bool timed_out = phys->data_train_deadline_ns != 0u &&
        vdc_timestamp_clock_now_ns() >= phys->data_train_deadline_ns &&
        ((initiator && data_remaining != 0u) ||
         (!initiator && !responder_done));

    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.marker_dma_remaining = marker_remaining;
    phys->data_train.data_dma_remaining = data_remaining;
    if (initiator && capture_started &&
        phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_ARMED) {
        phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_RUNNING;
        phys->data_train_deadline_ns = vdc_timestamp_clock_now_ns() +
                                       TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS;
    }
    if (initiator && capture_started) {
        phys->data_train.marker_capture_tick = 1ull;
        phys->data_train.data_capture_tick =
            phys->data_train.role ==
                    TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION
                ? 1ull + phys->data_train.phase_delay_cycles
                : tdma_pio_spi_phys_data_train_capture_tick(
                      phys->data_train.marker_to_data_delay_cycles,
                      phys->data_train.phase_delay_cycles,
                      phys->data_train.source_phase_delay_cycles);
    }
    if (marker_remaining == 0u) {
        phys->data_train.flags |=
            TDMA_PIO_SPI_DATA_TRAIN_FLAG_MARKER_DMA_COMPLETE;
    }
    if (data_remaining == 0u) {
        phys->data_train.flags |=
            TDMA_PIO_SPI_DATA_TRAIN_FLAG_DATA_DMA_COMPLETE;
    }
    if (responder_done) {
        phys->data_train.flags |= TDMA_PIO_SPI_DATA_TRAIN_FLAG_SOURCE_IRQ;
    }
    if (inject_rx_dma_short && data_remaining == 0u) {
        /* A deliberately short RX transfer is a DMA boundary fault even if
         * the PIO FIFO happens to drain before it becomes full. */
        if (phys->data_train.dma_overrun_count == 0u) {
            phys->data_train.dma_overrun_count++;
        }
    } else if (stalled && data_remaining != 0u) {
        /* A paused RX DMA is only classified as PIO_STALL after the real PIO
         * RX FIFO reports backpressure.  The DMA count is derivative evidence
         * and must not replace the physical root cause. */
        phys->data_train.pio_stall_count++;
        phys->data_train.dma_overrun_count++;
    }
    if (inject_rx_dma_short &&
        phys->data_train.dma_overrun_count != 0u) {
        phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
        phys->data_train.reject_reason = TDMA_PIO_SPI_DATA_TRAIN_REJECT_DMA;
    } else if (stalled && data_remaining != 0u &&
               (phys->data_train.diagnostic_fault_flags &
                TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_PAUSE) != 0u) {
        /* Do not let the watchdog turn an observed FIFO blockage into a
         * generic timeout.  The injected pause is diagnostic-only; the
         * physical FIFO full indication is the required root-cause evidence. */
        phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
        phys->data_train.reject_reason = TDMA_PIO_SPI_DATA_TRAIN_REJECT_PIO_STALL;
    } else if (timed_out) {
        phys->data_train.timeout_count++;
        phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
        phys->data_train.reject_reason =
            TDMA_PIO_SPI_DATA_TRAIN_REJECT_TIMEOUT;
    } else if ((initiator && !inject_rx_dma_short && marker_remaining == 0u &&
                data_remaining == 0u) ||
               (!initiator && responder_done && data_remaining == 0u)) {
        if (phys->data_train.pio_stall_count != 0u) {
            phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_ERROR;
            phys->data_train.reject_reason =
                TDMA_PIO_SPI_DATA_TRAIN_REJECT_PIO_STALL;
        } else {
            phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_COMPLETE;
            phys->data_train.reject_reason =
                TDMA_PIO_SPI_DATA_TRAIN_REJECT_NONE;
        }
    }
    const bool finished =
        phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE ||
        phys->data_train.state == TDMA_PIO_SPI_DATA_TRAIN_ERROR;
    tdma_pio_spi_phys_data_train_write_end(phys);
    if (finished) {
        if (s_tdma_pio_spi_tx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
        }
        if (s_tdma_pio_spi_rx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        }
        pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 4u);
        tdma_pio_spi_phys_cal_cleanup(phys);
        (void)tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    }
}

bool tdma_pio_spi_phys_get_data_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_data_train_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->data_train_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->data_train;
        const uint32_t end =
            __atomic_load_n(&phys->data_train_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool tdma_pio_spi_phys_copy_data_train_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    tdma_pio_spi_data_train_snapshot_t snapshot;
    if (capture_word_count != NULL) *capture_word_count = 0u;
    if (phys == NULL || capture_words == NULL || capture_word_count == NULL ||
        !tdma_pio_spi_phys_get_data_train_snapshot(phys, &snapshot) ||
        (snapshot.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         snapshot.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION) ||
        (snapshot.state != TDMA_PIO_SPI_DATA_TRAIN_COMPLETE &&
         !(snapshot.state == TDMA_PIO_SPI_DATA_TRAIN_ERROR &&
           snapshot.reject_reason == TDMA_PIO_SPI_DATA_TRAIN_REJECT_DMA &&
           (snapshot.diagnostic_fault_flags &
            TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u &&
           snapshot.dma_overrun_count != 0u))) {
        return false;
    }
    const size_t available_words =
        snapshot.state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE
            ? snapshot.capture_word_count
            : snapshot.capture_word_count - 1u;
    if (available_words == 0u || available_words > capture_word_capacity) {
        return false;
    }
    memcpy(capture_words, s_tdma_pio_spi_data_train_rx,
           available_words * sizeof(capture_words[0]));
    *capture_word_count = available_words;
    return true;
}

bool tdma_pio_spi_phys_sck_train_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_data_train_request_t *request)
{
    return request != NULL &&
           (request->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
            request->role ==
                TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION) &&
           tdma_pio_spi_phys_data_train_arm(phys, request);
}

bool tdma_pio_spi_phys_sck_train_inject(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        phys->data_train.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
        phys->data_train.state != TDMA_PIO_SPI_DATA_TRAIN_ARMED) {
        return false;
    }
    tdma_pio_spi_phys_data_train_write_begin(phys);
    phys->data_train.state = TDMA_PIO_SPI_DATA_TRAIN_RUNNING;
    phys->data_train.marker_capture_tick = 1ull;
    phys->data_train.data_capture_tick =
        tdma_pio_spi_phys_data_train_capture_tick(
            phys->data_train.marker_to_data_delay_cycles,
            phys->data_train.phase_delay_cycles,
            phys->data_train.source_phase_delay_cycles);
    phys->data_train_deadline_ns = vdc_timestamp_clock_now_ns() +
                                   TDMA_PIO_SPI_DATA_TRAIN_TIMEOUT_NS;
    tdma_pio_spi_phys_data_train_write_end(phys);
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm,
               s_tdma_pio_spi_sck_train_inject_word);
    return true;
}

void tdma_pio_spi_phys_sck_train_stop(tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_data_train_stop(phys);
}

void tdma_pio_spi_phys_sck_train_service(tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_phys_data_train_service(phys);
}

bool tdma_pio_spi_phys_get_sck_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_data_train_snapshot_t *snapshot)
{
    return tdma_pio_spi_phys_get_data_train_snapshot(phys, snapshot) &&
           (snapshot->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
            snapshot->role ==
                TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION);
}

bool tdma_pio_spi_phys_copy_sck_train_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    return tdma_pio_spi_phys_copy_data_train_capture(
        phys, capture_words, capture_word_capacity, capture_word_count);
}
