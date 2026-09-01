static bool tdma_pio_spi_phys_flight_origin_tx(
    tdma_pio_spi_phys_t *phys,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t *tx_timestamp_ns)
{
    if (phys == NULL || packet == NULL || packet_size == 0u ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        phys->role != TDMA_PIO_SPI_ROLE_MASTER ||
        s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        s_tdma_pio_spi_tx_dma_channel < 0) {
        return false;
    }
    /* A burst is accepted at launch and completed by
     * tdma_pio_spi_phys_service_tx().  Never wait here for the wire, DMA or
     * PIO TXSTALL: doing so would let a line delay consume the TDMA phase. */
    if (phys->flight_tx_pending ||
        dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
        phys->snapshot.tx_busy_count++;
        return false;
    }
    if (!pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        /* A failed prior burst must not leave the cyclic path permanently
         * TX_BUSY.  No successful call returns while this FIFO is occupied. */
        tdma_pio_spi_phys_flight_origin_recover(phys);
    }

    const uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE] = {
        TDMA_PIO_SPI_PACKET_MAGIC0,
        TDMA_PIO_SPI_PACKET_MAGIC1,
        (uint8_t)(packet_size & 0xFFu),
        (uint8_t)(packet_size >> 8u),
    };
    const uint32_t wire_bytes =
        (uint32_t)packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    const uint32_t clock_bytes = wire_bytes + phys->flight_tail_bytes;
    if (clock_bytes == 0u ||
        clock_bytes != phys->flight_physical_byte_count ||
        clock_bytes > TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS) {
        tdma_pio_spi_phys_set_error(phys,
                                    TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        return false;
    }
    for (uint32_t index = 0u; index < clock_bytes; index++) {
        const uint8_t value = index >= wire_bytes
            ? 0u
            : (index < TDMA_PIO_SPI_PACKET_HEADER_SIZE
                   ? header[index]
                   : packet[index - TDMA_PIO_SPI_PACKET_HEADER_SIZE]);
        s_tdma_pio_spi_flight_tx_words[index] = ((uint32_t)value) << 24u;
    }

    dma_channel_config dma_cfg = dma_channel_get_default_config(
        (uint)s_tdma_pio_spi_tx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(
        &dma_cfg,
        pio_get_dreq(BOARD_TDMA_SPI_PIO, phys->tx_sm, true));
    dma_channel_configure(
        (uint)s_tdma_pio_spi_tx_dma_channel,
        &dma_cfg,
        &BOARD_TDMA_SPI_PIO->txf[phys->tx_sm],
        s_tdma_pio_spi_flight_tx_words,
        clock_bytes,
        false);

    const uint32_t clock_bits = clock_bytes * 8u;
    const uint32_t clock_txstall_mask =
        tdma_pio_spi_phys_txstall_mask(phys->rx_sm);
    if (pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO, phys->tx_sm) ||
        pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO,
                               BOARD_TDMA_SPI_RTT_SM) ||
        pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO, phys->rx_sm)) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
        phys->snapshot.tx_busy_count++;
        return false;
    }
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 1u);
    /* The DATA SM consumes one frame control word before the DMA payload.
     * This keeps CS in the outer PIO loop and bytes in the inner loop.  The
     * FIFO was checked above, so the non-blocking put is deterministic. */
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm, clock_bytes - 1u);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_tx_dma_channel);
    pio_sm_put(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM, UINT32_MAX);
    const uint64_t tx_edge_diagnostic_ns = vdc_timestamp_clock_now_ns();
    /* This FIFO word is the sole launch event. The control PIO consumes it,
     * lowers CS, generates every SCK edge, then restores idle CS atomically. */
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->rx_sm, clock_bits - 1u);
    /* The clock SM was parked at its blocking PULL before the word above.
     * Clear that old sticky TXSTALL only after releasing the PULL. */
    BOARD_TDMA_SPI_PIO->fdebug = clock_txstall_mask;
    const uint64_t now_ns = tx_edge_diagnostic_ns;
    const uint64_t wire_ns = phys->baud_hz == 0u
        ? 0ull
        : ((uint64_t)clock_bits * 1000000000ull + phys->baud_hz - 1ull) /
              phys->baud_hz;
    phys->flight_tx_pending = true;
    phys->flight_tx_completion_pending = false;
    phys->flight_tx_completion_timestamp_ns = 0ull;
    phys->flight_tx_packet_size = (uint32_t)packet_size;
    phys->flight_tx_wire_bytes = wire_bytes;
    phys->flight_tx_launch_timestamp_ns = now_ns;
    phys->flight_tx_deadline_ns =
        UINT64_MAX - now_ns < wire_ns + 1000000ull
            ? UINT64_MAX
            : now_ns + wire_ns + 1000000ull;
    if (tx_timestamp_ns != NULL) {
        /* Completion owns the hardware latch timestamp.  Zero is explicit:
         * the adapter must not promote a software launch time to DPLL
         * evidence. */
        *tx_timestamp_ns = 0ull;
    }
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    return true;
}

void tdma_pio_spi_phys_service_tx(void *context, uint64_t now_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->flight_tx_pending ||
        s_tdma_pio_spi_tx_dma_channel < 0 ||
        s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN) {
        return;
    }

    const uint32_t stall_mask = tdma_pio_spi_phys_txstall_mask(phys->rx_sm);
    const bool clock_done =
        (BOARD_TDMA_SPI_PIO->fdebug & stall_mask) != 0u;
    const bool data_done =
        !dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel);
    if (!clock_done || !data_done) {
        if (now_ns >= phys->flight_tx_deadline_ns) {
            phys->snapshot.tx_timeout_count++;
            if (!clock_done) {
                phys->snapshot.origin_clock_timeout_count++;
            }
            if (!data_done) {
                phys->snapshot.origin_data_timeout_count++;
            }
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            tdma_pio_spi_phys_flight_origin_recover(phys);
            phys->flight_tx_pending = false;
            phys->flight_tx_completion_pending = false;
        }
        return;
    }

    phys->snapshot.origin_done_txstall_count++;
    pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 1u);
    BOARD_TDMA_SPI_PIO->fdebug = stall_mask;
    uint64_t tx_edge_timestamp_ns = 0ull;
    (void)tdma_pio_spi_phys_clock_latch_read_and_rearm(
        phys, &tx_edge_timestamp_ns);

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE];
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    const uint32_t packet_size = phys->flight_tx_packet_size;
    const uint32_t wire_bytes = phys->flight_tx_wire_bytes;
    if (packet_size == 0u || wire_bytes != packet_size +
            TDMA_PIO_SPI_PACKET_HEADER_SIZE ||
        wire_bytes > TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        phys->flight_tx_pending = false;
        phys->flight_tx_completion_pending = false;
        return;
    }
    for (uint32_t index = 0u; index < wire_bytes; index++) {
        const uint8_t value = (uint8_t)(
            s_tdma_pio_spi_flight_tx_words[index] >> 24u);
        if (index < TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
            header[index] = value;
        } else {
            packet[index - TDMA_PIO_SPI_PACKET_HEADER_SIZE] = value;
        }
    }
    tdma_pio_spi_phys_record_complete_tx_frame(header, packet, packet_size);
    phys->snapshot.tx_count++;
    phys->snapshot.tx_edge_count++;
    phys->snapshot.last_tx_size = packet_size;
    phys->snapshot.last_tx_edge_timestamp_ns =
        tx_edge_timestamp_ns != 0ull
            ? tx_edge_timestamp_ns
            : phys->flight_tx_launch_timestamp_ns;
    phys->snapshot.last_tx_done_timestamp_ns = vdc_timestamp_clock_now_ns();
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    phys->flight_tx_pending = false;
    phys->flight_tx_completion_pending = true;
    phys->flight_tx_completion_timestamp_ns = tx_edge_timestamp_ns;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
}

bool tdma_pio_spi_phys_take_tx_completion(void *context,
                                          uint64_t *tx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = 0ull;
    }
    if (phys == NULL || !phys->flight_tx_completion_pending) {
        return false;
    }
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = phys->flight_tx_completion_timestamp_ns;
    }
    phys->flight_tx_completion_pending = false;
    return true;
}

bool tdma_pio_spi_phys_tx(void *context,
                          const uint8_t *packet,
                          size_t packet_size,
                          uint64_t *tx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || packet == NULL || packet_size == 0u ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX || !phys->armed) {
        if (phys != NULL) {
            tdma_pio_spi_phys_set_error(phys,
                                        TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        }
        return false;
    }
    if (s_tdma_pio_spi_program_persona ==
        TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN) {
        return tdma_pio_spi_phys_flight_origin_tx(
            phys, packet, packet_size, tx_timestamp_ns);
    }
    if (s_tdma_pio_spi_program_persona !=
        TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_ROLE);
        return false;
    }
    if (!pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
        phys->snapshot.tx_busy_count++;
        return false;
    }

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE] = {
        TDMA_PIO_SPI_PACKET_MAGIC0,
        TDMA_PIO_SPI_PACKET_MAGIC1,
        (uint8_t)(packet_size & 0xFFu),
        (uint8_t)(packet_size >> 8u),
    };
    const uint64_t tx_edge_timestamp_ns = vdc_timestamp_clock_now_ns();
    gpio_put(phys->tx_csn_pin, false);
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_PACKET_HEADER_SIZE; i++) {
        if (!tdma_pio_spi_phys_tx_put(phys, ((uint32_t)header[i]) << 24u)) {
            gpio_put(phys->tx_csn_pin, true);
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            phys->snapshot.tx_busy_count++;
            return false;
        }
    }
    for (size_t i = 0u; i < packet_size; i++) {
        if (!tdma_pio_spi_phys_tx_put(phys, ((uint32_t)packet[i]) << 24u)) {
            gpio_put(phys->tx_csn_pin, true);
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            phys->snapshot.tx_busy_count++;
            return false;
        }
    }
    busy_wait_us_32(tdma_pio_spi_phys_frame_tail_us(
        phys->baud_hz, packet_size, TDMA_PIO_SPI_PACKET_HEADER_SIZE));
    gpio_put(phys->tx_csn_pin, true);
    const uint64_t tx_done_timestamp_ns = vdc_timestamp_clock_now_ns();
    tdma_pio_spi_phys_record_complete_tx_frame(header, packet, packet_size);

    phys->snapshot.tx_count++;
    phys->snapshot.tx_edge_count++;
    phys->snapshot.last_tx_size = (uint32_t)packet_size;
    phys->snapshot.last_tx_edge_timestamp_ns = tx_edge_timestamp_ns;
    phys->snapshot.last_tx_done_timestamp_ns = tx_done_timestamp_ns;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = tx_edge_timestamp_ns;
    }
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

bool tdma_pio_spi_phys_rx(void *context,
                          uint8_t *packet,
                          size_t packet_capacity,
                          size_t *packet_size,
                          uint64_t *rx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (packet_size != NULL) {
        *packet_size = 0u;
    }
    if (phys == NULL || packet == NULL || packet_capacity == 0u ||
        packet_size == NULL || rx_timestamp_ns == NULL || !phys->armed) {
        if (phys != NULL) {
            tdma_pio_spi_phys_set_error(phys,
                                        TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        }
        return false;
    }

    size_t received_words = 0u;
    if (!tdma_pio_spi_phys_capture_words(phys,
                                         TDMA_PIO_SPI_RX_DMA_WORD_MAX,
                                         &received_words)) {
        return false;
    }
    if (received_words < TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE];
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_PACKET_HEADER_SIZE; i++) {
        header[i] = (uint8_t)(s_tdma_pio_spi_rx_frame[i] & 0xFFu);
    }
    const uint16_t frame_size =
        (uint16_t)header[2] | ((uint16_t)header[3] << 8u);
    if (header[0] != TDMA_PIO_SPI_PACKET_MAGIC0 ||
        header[1] != TDMA_PIO_SPI_PACKET_MAGIC1 ||
        frame_size == 0u) {
        phys->snapshot.last_bad_header0 = s_tdma_pio_spi_rx_frame[0];
        phys->snapshot.last_bad_header1 = s_tdma_pio_spi_rx_frame[1];
        phys->snapshot.last_bad_header2 = s_tdma_pio_spi_rx_frame[2];
        phys->snapshot.last_bad_header3 = s_tdma_pio_spi_rx_frame[3];
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }
    if ((size_t)frame_size > packet_capacity) {
        phys->snapshot.last_bad_header0 =
            header[2] | ((uint32_t)header[3] << 8u);
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys,
                                    TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE);
        return false;
    }
    if (received_words <
        (size_t)frame_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }

    for (uint16_t i = 0u; i < frame_size; i++) {
        packet[i] = (uint8_t)(
            s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_PACKET_HEADER_SIZE + i] &
            0xFFu);
    }
    *packet_size = frame_size;
    const uint64_t rx_extract_timestamp_ns = vdc_timestamp_clock_now_ns();
    const uint64_t wire_time_ns =
        tdma_pio_spi_phys_wire_time_ns(
            phys->baud_hz, frame_size, TDMA_PIO_SPI_PACKET_HEADER_SIZE);
    const uint64_t rx_edge_timestamp_ns =
        rx_extract_timestamp_ns > wire_time_ns
            ? rx_extract_timestamp_ns - wire_time_ns
            : rx_extract_timestamp_ns;
    uint64_t hardware_rx_edge_timestamp_ns = 0ull;
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        (void)tdma_pio_spi_phys_clock_latch_read_and_rearm(
            phys, &hardware_rx_edge_timestamp_ns);
    }
    *rx_timestamp_ns = phys->role == TDMA_PIO_SPI_ROLE_SLAVE
        ? hardware_rx_edge_timestamp_ns
        : rx_edge_timestamp_ns;

    phys->snapshot.rx_count++;
    phys->snapshot.last_rx_size = frame_size;
    phys->snapshot.last_rx_timestamp_ns = *rx_timestamp_ns;
    phys->snapshot.rx_edge_count++;
    phys->snapshot.last_rx_edge_timestamp_ns =
        hardware_rx_edge_timestamp_ns != 0ull
            ? hardware_rx_edge_timestamp_ns
            : rx_edge_timestamp_ns;
    phys->snapshot.last_rx_extract_timestamp_ns = rx_extract_timestamp_ns;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

bool tdma_pio_spi_phys_feedback_round_trip(
    void *context,
    uint32_t *round_trip_ns,
    uint32_t *resolution_ns,
    uint32_t *flags)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (round_trip_ns != NULL) {
        *round_trip_ns = 0u;
    }
    if (resolution_ns != NULL) {
        *resolution_ns = 0u;
    }
    if (flags != NULL) {
        *flags = TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    }
    if (phys == NULL || round_trip_ns == NULL || resolution_ns == NULL ||
        flags == NULL || !phys->armed ||
        phys->role != TDMA_PIO_SPI_ROLE_MASTER ||
        s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        pio_sm_is_rx_fifo_empty(BOARD_TDMA_SPI_PIO,
                                BOARD_TDMA_SPI_RTT_SM)) {
        return false;
    }

    /* A rejected transport frame can leave an older duration in this FIFO.
     * Wire ordering is strict, so the newest completed CS interval belongs
     * to the newest complete frame returned by phys_rx. */
    uint32_t remaining = 0u;
    do {
        remaining = pio_sm_get(BOARD_TDMA_SPI_PIO,
                               BOARD_TDMA_SPI_RTT_SM);
    } while (!pio_sm_is_rx_fifo_empty(BOARD_TDMA_SPI_PIO,
                                      BOARD_TDMA_SPI_RTT_SM));

    const uint32_t clk_hz = clock_get_hz(clk_sys);
    if (clk_hz == 0u) {
        return false;
    }
    const uint32_t edge_resolution_ns = (uint32_t)(
        (2000000000ull + clk_hz / 2u) / clk_hz);
    const uint64_t elapsed_counts = (uint64_t)UINT32_MAX - remaining;
    const uint64_t duration_ns =
        elapsed_counts * (uint64_t)edge_resolution_ns;
    if (elapsed_counts == 0ull || edge_resolution_ns == 0u ||
        duration_ns == 0ull || duration_ns > UINT32_MAX) {
        return false;
    }
    *round_trip_ns = (uint32_t)duration_ns;
    *resolution_ns = edge_resolution_ns;
    *flags = TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    return true;
}

bool tdma_pio_spi_phys_get_snapshot(const tdma_pio_spi_phys_t *phys,
                                    tdma_pio_spi_phys_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = phys->snapshot;
    snapshot->pio_irq_flags = BOARD_TDMA_SPI_PIO->irq;
    snapshot->pio_fdebug = BOARD_TDMA_SPI_PIO->fdebug;
    snapshot->tx_sm_pc = pio_sm_get_pc(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    snapshot->rx_sm_pc = pio_sm_get_pc(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    snapshot->tx_sm_tx_fifo_level =
        pio_sm_get_tx_fifo_level(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    snapshot->tx_sm_rx_fifo_level =
        pio_sm_get_rx_fifo_level(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    snapshot->rx_sm_tx_fifo_level =
        pio_sm_get_tx_fifo_level(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    snapshot->rx_sm_rx_fifo_level =
        pio_sm_get_rx_fifo_level(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    snapshot->gpio_input_levels =
        (gpio_get(phys->tx_sck_pin) ? (1u << 0u) : 0u) |
        (gpio_get(phys->tx_csn_pin) ? (1u << 1u) : 0u) |
        (gpio_get(phys->tx_pin) ? (1u << 2u) : 0u) |
        (gpio_get(phys->rx_sck_pin) ? (1u << 3u) : 0u) |
        (gpio_get(phys->rx_csn_pin) ? (1u << 4u) : 0u) |
        (gpio_get(phys->rx_pin) ? (1u << 5u) : 0u);
    return true;
}

tdma_pio_spi_normal_capture_copy_result_t
tdma_pio_spi_phys_copy_normal_capture(
    tdma_pio_spi_phys_t *phys,
    uint32_t *rx_bytes,
    size_t rx_capacity,
    uint32_t *tx_bytes,
    size_t tx_capacity,
    tdma_pio_spi_normal_capture_snapshot_t *snapshot)
{
    if (phys == NULL || rx_bytes == NULL || tx_bytes == NULL ||
        snapshot == NULL || rx_capacity == 0u || tx_capacity == 0u ||
        rx_capacity > TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES ||
        tx_capacity > TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES ||
        !phys->armed || !phys->rx_capture_active ||
        (s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER &&
         s_tdma_pio_spi_program_persona !=
             TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)) {
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
    }

    /* Stage 0 owns only the SCK evidence FIFO. Clock-latch restoration and
     * raw RX history use later fixed-size beats so the request never borrows
     * time from another realtime phase. */
    if (phys->flight_normal_capture_copy_stage == 0u) {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->version = TDMA_PIO_SPI_NORMAL_CAPTURE_VERSION;
        snapshot->baud_hz = phys->baud_hz;
        snapshot->bit_period_ns = phys->baud_hz == 0u
            ? 0u
            : (uint32_t)((1000000000ull + phys->baud_hz / 2u) /
                         phys->baud_hz);
        if (s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        if (tdma_pio_spi_phys_service_ring_waveform_capture(phys) !=
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        phys->flight_normal_capture_sck_cursor = 0u;
        phys->flight_normal_capture_copy_stage = 1u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    /* Drain one SCK evidence word per scheduled beat. */
    if (phys->flight_normal_capture_copy_stage == 1u) {
        pio_sm_set_enabled(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM, false);
        const uint32_t sck_word_count = pio_sm_get_rx_fifo_level(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
        const uint32_t sck_words_remaining =
            TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS -
            phys->flight_normal_capture_sck_cursor;
        if (phys->flight_normal_capture_sck_cursor >=
                TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS ||
            sck_word_count != sck_words_remaining) {
            (void)tdma_pio_spi_phys_restore_clock_latch(phys, true);
            phys->flight_sck_waveform_capture_state =
                TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED;
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        const uint32_t index = phys->flight_normal_capture_sck_cursor++;
        snapshot->sck_words[index] = pio_sm_get(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
        if (phys->flight_normal_capture_sck_cursor <
            TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
        }
        snapshot->sck_word_count = TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS;
        snapshot->sck_sample_count =
            TDMA_PIO_SPI_FLIGHT_SCK_CAPTURE_WORDS *
            TDMA_PIO_SPI_FLIGHT_SCK_SAMPLES_PER_WORD;
        snapshot->sck_sample_period_ns =
            TDMA_PIO_SPI_FLIGHT_SCK_SAMPLE_PERIOD_NS;
        phys->flight_normal_capture_copy_stage = 2u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    /* Restore the resident clock-latch program in its own beat. */
    if (phys->flight_normal_capture_copy_stage == 2u) {
        bool restore_complete = false;
        if (!tdma_pio_spi_phys_capture_restore_step(
                phys, &restore_complete)) {
            phys->flight_sck_waveform_capture_state =
                TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED;
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        if (!restore_complete) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
        }
        phys->flight_normal_capture_copy_stage = 3u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    /* Rearm separately, then freeze the raw RX history boundary retained by
     * all subsequent fixed-size chunk copies. */
    if (phys->flight_normal_capture_copy_stage == 3u) {
        if (!tdma_pio_spi_phys_clock_latch_rearm(phys)) {
            phys->flight_sck_waveform_capture_state =
                TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_FAILED;
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
        }
        phys->flight_normal_capture_rx_produced =
            tdma_pio_spi_phys_rx_produced_words(phys);
        phys->flight_normal_capture_rx_count =
            phys->flight_normal_capture_rx_produced < rx_capacity
            ? phys->flight_normal_capture_rx_produced
            : (uint32_t)rx_capacity;
        phys->flight_normal_capture_rx_start =
            phys->flight_normal_capture_rx_produced -
            phys->flight_normal_capture_rx_count;
        phys->flight_normal_capture_rx_cursor = 0u;
        snapshot->rx_byte_count = phys->flight_normal_capture_rx_count;
        snapshot->rx_produced_bytes =
            phys->flight_normal_capture_rx_produced;
        phys->flight_normal_capture_copy_stage = 4u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    if (phys->flight_normal_capture_copy_stage == 4u) {
        const uint32_t remaining = phys->flight_normal_capture_rx_count -
            phys->flight_normal_capture_rx_cursor;
        const uint32_t count =
            remaining < TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_CHUNK_BYTES
            ? remaining : TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_CHUNK_BYTES;
        for (uint32_t index = 0u; index < count; index++) {
            const uint32_t cursor =
                phys->flight_normal_capture_rx_cursor + index;
            rx_bytes[cursor] = tdma_pio_spi_phys_rx_ring_byte(
                phys->flight_normal_capture_rx_start + cursor);
        }
        phys->flight_normal_capture_rx_cursor += count;
        if (phys->flight_normal_capture_rx_cursor <
            phys->flight_normal_capture_rx_count) {
            return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
        }
        phys->flight_normal_capture_copy_stage = 5u;
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING;
    }

    if (phys->flight_normal_capture_copy_stage != 5u) {
        return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_FAILED;
    }

    /* Flight evidence is the physical RX loop stream. The origin's software
     * TX history is a continuously rewritten single buffer, not a waveform;
     * seqlock-copying it here can consume an unbounded number of retries and
     * violate the calibration beat. Offline reference selection therefore
     * uses the adjacent Node's immutable physical RX capture. */
    (void)tx_bytes;
    snapshot->tx_byte_count = 0u;
    snapshot->tx_produced_bytes = 0u;
    snapshot->tx_complete_frame_count = 0u;
    phys->flight_normal_capture_copy_stage = 5u;
    return TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_READY;
}
