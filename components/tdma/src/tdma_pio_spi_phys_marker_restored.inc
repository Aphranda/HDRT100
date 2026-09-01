static void tdma_pio_spi_phys_marker_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->marker_guard, 1u, __ATOMIC_RELEASE);
}
static void tdma_pio_spi_phys_marker_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->marker_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_marker_publish_error(
    tdma_pio_spi_phys_t *phys, uint32_t epoch,
    tdma_pio_spi_marker_reject_t reason)
{
    tdma_pio_spi_phys_marker_write_begin(phys);
    memset(&phys->marker, 0, sizeof(phys->marker));
    phys->marker.version = TDMA_PIO_SPI_MARKER_SNAPSHOT_VERSION;
    phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
    phys->marker.flags = TDMA_PIO_SPI_MARKER_FLAG_DIAGNOSTIC_ONLY;
    phys->marker.reject_reason = (uint32_t)reason;
    phys->marker.epoch = epoch;
    tdma_pio_spi_phys_marker_write_end(phys);
}

static uint32_t tdma_pio_spi_phys_marker_sample(uint32_t word,
                                                uint32_t sample_index)
{
    return (word >> ((sample_index & 15u) * 2u)) & 0x3u;
}

static void tdma_pio_spi_phys_marker_decode_edges(
    tdma_pio_spi_phys_t *phys)
{
    uint64_t first_tx = 0u;
    uint64_t first_rx = 0u;
    uint32_t previous = 0x3u;
    const uint32_t samples = phys->marker.capture_sample_count;
    for (uint32_t index = 0u; index < samples; index++) {
        const uint32_t sample = tdma_pio_spi_phys_marker_sample(
            s_tdma_pio_spi_marker_rx[index / 16u], index);
        const uint32_t falling = previous & ~sample;
        if (first_tx == 0u && (falling & 0x1u) != 0u) {
            first_tx = (uint64_t)index + 1ull;
        }
        if (first_rx == 0u && (falling & 0x2u) != 0u) {
            first_rx = (uint64_t)index + 1ull;
        }
        previous = sample;
        if (first_tx != 0u && first_rx != 0u) break;
    }

    if (phys->marker.role == TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER) {
        /* The capture program's WAIT 0 GPIO is the hardware input latch.  DMA
         * starts on the following instruction, hence local tick 1 denotes
         * the accepted marker edge and sampled output ticks start at 2. */
        phys->marker.marker_capture_tick = 1ull;
        phys->marker.marker_forward_tick =
            first_tx == 0u ? 0ull : first_tx + 1ull;
        phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_INPUT_EDGE;
        if (first_tx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_OUTPUT_EDGE;
        }
    } else {
        /* Reference TX and capture SMs start from one hardware sync mask.
         * Its two-NOP capture preamble is accounted for by the local origin
         * tick; the later upstream edge is the full-ring return. */
        phys->marker.marker_forward_tick = first_tx != 0u ? 1ull : 0ull;
        phys->marker.marker_return_tick =
            first_rx == 0u ? 0ull : first_rx + 2ull;
        phys->marker.marker_capture_tick = phys->marker.marker_return_tick;
        if (first_tx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_OUTPUT_EDGE;
        }
        if (first_rx != 0u) {
            phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_INPUT_EDGE |
                                  TDMA_PIO_SPI_MARKER_FLAG_RETURN_EDGE;
        }
    }
}

bool tdma_pio_spi_phys_marker_arm(
    tdma_pio_spi_phys_t *phys,
    const tdma_pio_spi_marker_request_t *request)
{
    if (phys == NULL || request == NULL ||
        (request->role != TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR &&
         request->role != TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER) ||
        request->marker_sample_count == 0u ||
        request->offset_sample_count < TDMA_PIO_SPI_MARKER_MIN_OFFSET_SAMPLES ||
        request->offset_sample_count > TDMA_PIO_SPI_MARKER_MAX_OFFSET_SAMPLES ||
        request->capture_phase_delay_cycles >
            TDMA_PIO_SPI_MARKER_MAX_CAPTURE_DELAY_CYCLES ||
        request->capture_sample_count < request->marker_sample_count ||
        request->capture_sample_count >
            TDMA_PIO_SPI_MARKER_BUFFER_WORDS *
                TDMA_PIO_SPI_MARKER_SAMPLES_PER_WORD ||
        (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR &&
         (request->tx_words == NULL || request->tx_word_count == 0u ||
          request->tx_word_count > TDMA_PIO_SPI_MARKER_BUFFER_WORDS))) {
        if (phys != NULL) {
            tdma_pio_spi_phys_marker_publish_error(
                phys, request == NULL ? 0u : request->epoch,
                TDMA_PIO_SPI_MARKER_REJECT_BAD_ARGUMENT);
        }
        return false;
    }
    if (phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
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
    /* Hardware-pad guard: hold the downstream marker pin high throughout
     * program unload/load and GPIO mux changes.  This is the ARM invariant;
     * only marker injection or physical upstream forwarding may create a
     * falling edge after the override is released. */
    gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_HIGH);
    /* Keep persona switching invisible on an already armed physical link.
     * Dropping DE here creates a real falling edge through ISO1452 and can
     * release a downstream follower before the origin is injected.  The
     * maintenance-only path has no active link, so it may still use the
     * traditional disable/restore guard. */
    const bool drivers_were_enabled = phys->armed;
    if (!drivers_were_enabled) {
        gpio_put(BOARD_TRIG_DE_PIN, false);
    }
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER) ||
        !tdma_pio_spi_phys_ensure_rx_dma() ||
        (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR &&
         !tdma_pio_spi_phys_ensure_tx_dma())) {
        if (!drivers_were_enabled) {
            gpio_put(BOARD_TRIG_DE_PIN, true);
        }
        gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
        tdma_pio_spi_phys_marker_publish_error(
            phys, request->epoch, TDMA_PIO_SPI_MARKER_REJECT_RESOURCE);
        return false;
    }
    const bool enable_drivers_after_setup = !phys->armed;
    if (enable_drivers_after_setup) {
        /* Do not expose the SIO input/default-low state to the downstream
         * trigger isolator.  The marker initializers first establish the
         * high-idle PIO latch and output direction; only then may the line
         * drivers be enabled. */
        tdma_pio_spi_phys_prepare_maintenance_mapping(phys);
    }

    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    if (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR) {
        tdma_pio_spi_marker_origin_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_marker_origin_offset, phys->tx_csn_pin);
    } else {
        tdma_pio_spi_marker_forward_program_init(
            BOARD_TDMA_SPI_PIO, phys->tx_sm,
            s_tdma_pio_spi_marker_forward_offset,
            phys->rx_csn_pin, phys->tx_csn_pin,
            TDMA_PIO_SPI_MARKER_FORWARD_DELAY_CYCLES);
    }
    tdma_pio_spi_marker_capture_program_init(
        BOARD_TDMA_SPI_PIO, phys->rx_sm,
        s_tdma_pio_spi_marker_capture_offset,
        phys->tx_csn_pin, phys->rx_csn_pin,
        request->capture_phase_delay_cycles);

    const uint32_t capture_words =
        (request->capture_sample_count + 15u) / 16u;
    memset(s_tdma_pio_spi_marker_rx, 0,
           capture_words * sizeof(s_tdma_pio_spi_marker_rx[0]));
    if (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR) {
        memcpy(s_tdma_pio_spi_marker_tx, request->tx_words,
               request->tx_word_count * sizeof(request->tx_words[0]));
    }

    dma_channel_config rx_dc = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&rx_dc, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_dc, false);
    channel_config_set_write_increment(&rx_dc, true);
    channel_config_set_dreq(
        &rx_dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->rx_sm, false));
    dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &rx_dc,
                          s_tdma_pio_spi_marker_rx,
                          &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                          capture_words, false);

    if (request->role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR) {
        dma_channel_config tx_dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_tx_dma_channel);
        channel_config_set_transfer_data_size(&tx_dc, DMA_SIZE_32);
        channel_config_set_read_increment(&tx_dc, true);
        channel_config_set_write_increment(&tx_dc, false);
        channel_config_set_dreq(
            &tx_dc, pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->tx_sm, true));
        dma_channel_configure((uint)s_tdma_pio_spi_tx_dma_channel, &tx_dc,
                              &BOARD_TDMA_SPI_PIO->txf[phys->tx_sm],
                              s_tdma_pio_spi_marker_tx,
                              request->tx_word_count, false);
    }

    tdma_pio_spi_phys_marker_write_begin(phys);
    memset(&phys->marker, 0, sizeof(phys->marker));
    phys->marker.version = TDMA_PIO_SPI_MARKER_SNAPSHOT_VERSION;
    phys->marker.state = TDMA_PIO_SPI_MARKER_ARMED;
    phys->marker.role = request->role;
    phys->marker.flags = TDMA_PIO_SPI_MARKER_FLAG_DIAGNOSTIC_ONLY |
                         TDMA_PIO_SPI_MARKER_FLAG_HARDWARE_CAPTURE;
    phys->marker.epoch = request->epoch;
    phys->marker.tx_word_count = request->tx_word_count;
    phys->marker.marker_sample_count = request->marker_sample_count;
    phys->marker.capture_word_count = capture_words;
    phys->marker.capture_sample_count = request->capture_sample_count;
    phys->marker.tx_dma_remaining = request->tx_word_count;
    phys->marker.rx_dma_remaining = capture_words;
    phys->marker_deadline_ns = 0u;
    tdma_pio_spi_phys_marker_write_end(phys);

    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    gpio_put(BOARD_TRIG_DE_PIN, true);
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                               (1u << phys->tx_sm) | (1u << phys->rx_sm));
    gpio_set_outover(phys->tx_csn_pin, GPIO_OVERRIDE_NORMAL);
    return true;
}

bool tdma_pio_spi_phys_marker_inject(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        phys->marker.state != TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.role != TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR ||
        s_tdma_pio_spi_tx_dma_channel < 0 ||
        dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
        return false;
    }

    tdma_pio_spi_phys_marker_write_begin(phys);
    phys->marker.state = TDMA_PIO_SPI_MARKER_RUNNING;
    phys->marker_deadline_ns = vdc_timestamp_clock_now_ns() +
                               TDMA_PIO_SPI_MARKER_TIMEOUT_NS;
    tdma_pio_spi_phys_marker_write_end(phys);

    /* The first DMA word releases marker_origin's pull block.  The physical
     * falling edge and all subsequent samples remain entirely in PIO. */
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_tx_dma_channel);
    return true;
}

void tdma_pio_spi_phys_marker_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    tdma_pio_spi_phys_cal_cleanup(phys);
    tdma_pio_spi_phys_marker_write_begin(phys);
    phys->marker.state = TDMA_PIO_SPI_MARKER_IDLE;
    phys->marker.reject_reason = TDMA_PIO_SPI_MARKER_REJECT_NONE;
    phys->marker.tx_dma_remaining = 0u;
    phys->marker.rx_dma_remaining = 0u;
    tdma_pio_spi_phys_marker_write_end(phys);
    (void)tdma_pio_spi_phys_select_program_persona(
        phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

void tdma_pio_spi_phys_marker_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL ||
        (phys->marker.state != TDMA_PIO_SPI_MARKER_ARMED &&
         phys->marker.state != TDMA_PIO_SPI_MARKER_RUNNING) ||
        s_tdma_pio_spi_rx_dma_channel < 0) {
        return;
    }
    const uint32_t rx_remaining =
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
    const uint32_t tx_remaining =
        phys->marker.role == TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR &&
                s_tdma_pio_spi_tx_dma_channel >= 0
            ? dma_hw->ch[(uint)s_tdma_pio_spi_tx_dma_channel].transfer_count
            : 0u;
    const bool stalled =
        pio_sm_is_rx_fifo_full(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    const bool timed_out = phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING &&
        rx_remaining != 0u &&
        vdc_timestamp_clock_now_ns() >= phys->marker_deadline_ns;

    tdma_pio_spi_phys_marker_write_begin(phys);
    phys->marker.rx_dma_remaining = rx_remaining;
    phys->marker.tx_dma_remaining = tx_remaining;
    if (phys->marker.role == TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER &&
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED &&
        rx_remaining < phys->marker.capture_word_count) {
        phys->marker.state = TDMA_PIO_SPI_MARKER_RUNNING;
        phys->marker_deadline_ns = vdc_timestamp_clock_now_ns() +
                                   TDMA_PIO_SPI_MARKER_TIMEOUT_NS;
    }
    if (tx_remaining == 0u) {
        phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_TX_DMA_COMPLETE;
    }
    if (stalled && rx_remaining != 0u) {
        phys->marker.pio_stall_count++;
        phys->marker.dma_overrun_count++;
    }
    if (timed_out) {
        phys->marker.timeout_count++;
        phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
        phys->marker.reject_reason = TDMA_PIO_SPI_MARKER_REJECT_TIMEOUT;
    } else if (rx_remaining == 0u) {
        phys->marker.flags |= TDMA_PIO_SPI_MARKER_FLAG_RX_DMA_COMPLETE;
        tdma_pio_spi_phys_marker_decode_edges(phys);
        if (tx_remaining != 0u) {
            phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
            phys->marker.reject_reason =
                TDMA_PIO_SPI_MARKER_REJECT_CAPTURE_SHORT;
        } else if (phys->marker.pio_stall_count != 0u) {
            phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
            phys->marker.reject_reason =
                TDMA_PIO_SPI_MARKER_REJECT_PIO_STALL;
        } else if (phys->marker.marker_capture_tick == 0u ||
                   phys->marker.marker_forward_tick == 0u) {
            phys->marker.state = TDMA_PIO_SPI_MARKER_ERROR;
            phys->marker.reject_reason =
                TDMA_PIO_SPI_MARKER_REJECT_EDGE_MISSING;
        } else {
            phys->marker.state = TDMA_PIO_SPI_MARKER_COMPLETE;
            phys->marker.reject_reason = TDMA_PIO_SPI_MARKER_REJECT_NONE;
        }
    }
    const bool finished = phys->marker.state == TDMA_PIO_SPI_MARKER_COMPLETE ||
                          phys->marker.state == TDMA_PIO_SPI_MARKER_ERROR;
    tdma_pio_spi_phys_marker_write_end(phys);
    if (finished) {
        if (s_tdma_pio_spi_tx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
        }
        if (s_tdma_pio_spi_rx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        }
        tdma_pio_spi_phys_cal_cleanup(phys);
        (void)tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    }
}

bool tdma_pio_spi_phys_get_marker_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_marker_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->marker_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->marker;
        const uint32_t end =
            __atomic_load_n(&phys->marker_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool tdma_pio_spi_phys_copy_marker_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    tdma_pio_spi_marker_snapshot_t snapshot;
    if (capture_word_count != NULL) *capture_word_count = 0u;
    if (phys == NULL || capture_words == NULL || capture_word_count == NULL ||
        !tdma_pio_spi_phys_get_marker_snapshot(phys, &snapshot) ||
        (snapshot.state != TDMA_PIO_SPI_MARKER_COMPLETE &&
         snapshot.state != TDMA_PIO_SPI_MARKER_ERROR) ||
        (snapshot.flags & TDMA_PIO_SPI_MARKER_FLAG_RX_DMA_COMPLETE) == 0u ||
        snapshot.capture_word_count > capture_word_capacity) {
        return false;
    }
    memcpy(capture_words, s_tdma_pio_spi_marker_rx,
           snapshot.capture_word_count * sizeof(capture_words[0]));
    *capture_word_count = snapshot.capture_word_count;
    return true;
}
