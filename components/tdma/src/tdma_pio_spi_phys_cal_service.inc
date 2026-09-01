static void tdma_pio_spi_phys_cal_mark_persona(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona)
{
    s_tdma_pio_spi_program_persona = persona;
    phys->snapshot.program_persona = (uint32_t)persona;
    phys->snapshot.program_switch_count++;
}
static bool tdma_pio_spi_phys_cal_persona_switch_ready(void)
{
    const uint32_t sm_mask = (1u << BOARD_TDMA_SPI_MASTER_SM) |
                             (1u << BOARD_TDMA_SPI_SLAVE_SM) |
                             (1u << BOARD_TDMA_SPI_CAPTURE_SM);
    return tdma_pio_spi_phys_ensure_sms_claimed() &&
           (BOARD_TDMA_SPI_PIO->ctrl & sm_mask) == 0u &&
           (s_tdma_pio_spi_tx_dma_channel < 0 ||
            !dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) &&
           (s_tdma_pio_spi_rx_dma_channel < 0 ||
            !dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel));
}

static bool tdma_pio_spi_phys_cal_unload_source_step(
    tdma_pio_spi_phys_t *phys,
    bool *complete)
{
    const uint32_t step = phys->cal_loopback_program_step;
    uint32_t count = 0u;
    *complete = false;
    switch (s_tdma_pio_spi_program_persona) {
    case TDMA_PIO_SPI_PROGRAM_PERSONA_NONE:
        *complete = true;
        return true;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL:
        count = 2u;
        if (step == 0u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_rx_byte_program,
                               s_tdma_pio_spi_rx_offset);
        } else if (step == 1u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_tx_byte_program,
                               s_tdma_pio_spi_tx_offset);
        }
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN:
        count = 4u;
        if (step == 0u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_clock_latch_program,
                               s_tdma_pio_spi_flight_clock_latch_offset);
        } else if (step == 1u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_origin_rtt_program,
                               s_tdma_pio_spi_flight_origin_rtt_offset);
        } else if (step == 2u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_origin_data_tx_program,
                               s_tdma_pio_spi_flight_origin_data_offset);
        } else if (step == 3u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_origin_clock_rx_program,
                               s_tdma_pio_spi_flight_origin_clock_offset);
        }
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER:
        count = 3u;
        if (step == 0u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_clock_latch_program,
                               s_tdma_pio_spi_flight_clock_latch_offset);
        } else if (step == 1u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_data_follower_program,
                               s_tdma_pio_spi_flight_data_follower_offset);
        } else if (step == 2u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_control_forward_program,
                               s_tdma_pio_spi_flight_control_forward_offset);
        }
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER:
        count = 3u;
        if (step == 0u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_clock_latch_program,
                               s_tdma_pio_spi_flight_clock_latch_offset);
        } else if (step == 1u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_process_follower_program,
                               s_tdma_pio_spi_flight_process_follower_offset);
        } else if (step == 2u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                               &tdma_pio_spi_flight_control_forward_program,
                               s_tdma_pio_spi_flight_control_forward_offset);
        }
        break;
    default:
        return false;
    }
    phys->cal_loopback_program_step++;
    if (phys->cal_loopback_program_step >= count) {
        s_tdma_pio_spi_program_persona =
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        phys->snapshot.program_persona =
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        phys->cal_loopback_program_step = 0u;
        *complete = true;
    }
    return true;
}

static bool tdma_pio_spi_phys_cal_load_p3_step(
    tdma_pio_spi_phys_t *phys)
{
    switch (phys->cal_loopback_program_count) {
    case 0u:
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                                 &tdma_pio_spi_p3_initiator_program)) {
            return false;
        }
        s_tdma_pio_spi_p3_initiator_offset = (uint)pio_add_program(
            BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_initiator_program);
        break;
    case 1u:
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                                 &tdma_pio_spi_p3_responder_program)) {
            return false;
        }
        s_tdma_pio_spi_p3_responder_offset = (uint)pio_add_program(
            BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_responder_program);
        break;
    case 2u:
        if (!pio_can_add_program(
                BOARD_TDMA_SPI_PIO,
                &tdma_pio_spi_cal_loopback_capture_program)) {
            return false;
        }
        s_tdma_pio_spi_p3_capture_offset = (uint)pio_add_program(
            BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_cal_loopback_capture_program);
        break;
    default:
        return false;
    }
    phys->cal_loopback_program_count++;
    if (phys->cal_loopback_program_count == 3u) {
        tdma_pio_spi_phys_cal_mark_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE);
    }
    return true;
}

static void tdma_pio_spi_phys_cal_unload_p3_step(
    tdma_pio_spi_phys_t *phys)
{
    if (phys->cal_loopback_program_count == 0u) {
        s_tdma_pio_spi_program_persona =
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        phys->snapshot.program_persona =
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        return;
    }
    const uint32_t index = phys->cal_loopback_program_count - 1u;
    if (index == 2u) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_p3_capture_offset);
    } else if (index == 1u) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
    } else {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
    }
    phys->cal_loopback_program_count--;
    if (phys->cal_loopback_program_count == 0u) {
        s_tdma_pio_spi_program_persona =
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        phys->snapshot.program_persona =
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
    }
}

static bool tdma_pio_spi_phys_cal_load_normal_step(
    tdma_pio_spi_phys_t *phys)
{
    if (phys->cal_loopback_program_count == 0u) {
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                                 &tdma_pio_spi_tx_byte_program)) {
            return false;
        }
        s_tdma_pio_spi_tx_offset = (uint)pio_add_program(
            BOARD_TDMA_SPI_PIO, &tdma_pio_spi_tx_byte_program);
    } else if (phys->cal_loopback_program_count == 1u) {
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                                 &tdma_pio_spi_rx_byte_program)) {
            return false;
        }
        s_tdma_pio_spi_rx_offset = (uint)pio_add_program(
            BOARD_TDMA_SPI_PIO, &tdma_pio_spi_rx_byte_program);
    } else {
        return false;
    }
    phys->cal_loopback_program_count++;
    if (phys->cal_loopback_program_count == 2u) {
        tdma_pio_spi_phys_cal_mark_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    }
    return true;
}

static void tdma_pio_spi_phys_cal_transition_fail(
    tdma_pio_spi_phys_t *phys)
{
    phys->snapshot.program_switch_fail_count++;
    phys->cal_loopback_start_pending = false;
    phys->cal_loopback_stop_pending = true;
    phys->cal_loopback_transition =
        TDMA_PIO_SPI_CAL_TRANSITION_STOP_FREEZE;
    tdma_pio_spi_phys_cal_reject(phys, phys->cal_loopback_epoch, 1u);
}

void tdma_pio_spi_phys_cal_loopback_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_STOP_FREEZE) {
        if (s_tdma_pio_spi_rx_dma_channel >= 0) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        }
        if (phys->cal_loopback.armed != 0u) {
            pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,
                               phys->cal_loopback_tx_sm, false);
            pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,
                               phys->cal_loopback_capture_sm, false);
        }
        tdma_pio_spi_phys_cal_write_begin(phys);
        phys->cal_loopback.armed = 0u;
        tdma_pio_spi_phys_cal_write_end(phys);
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_STOP_CLEANUP;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_STOP_CLEANUP) {
        tdma_pio_spi_phys_cal_cleanup(phys);
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_STOP_UNLOAD;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_STOP_UNLOAD) {
        if (!tdma_pio_spi_phys_cal_persona_switch_ready()) {
            return;
        }
        if (s_tdma_pio_spi_program_persona ==
                TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE ||
            (s_tdma_pio_spi_program_persona ==
                 TDMA_PIO_SPI_PROGRAM_PERSONA_NONE &&
             phys->cal_loopback_program_count != 0u)) {
            tdma_pio_spi_phys_cal_unload_p3_step(phys);
        } else if (s_tdma_pio_spi_program_persona !=
                   TDMA_PIO_SPI_PROGRAM_PERSONA_NONE) {
            bool complete = false;
            if (!tdma_pio_spi_phys_cal_unload_source_step(
                    phys, &complete)) {
                phys->snapshot.program_switch_fail_count++;
                return;
            }
        }
        if (s_tdma_pio_spi_program_persona ==
                TDMA_PIO_SPI_PROGRAM_PERSONA_NONE &&
            phys->cal_loopback_program_count == 0u) {
            phys->cal_loopback_program_step = 0u;
            phys->cal_loopback_transition =
                TDMA_PIO_SPI_CAL_TRANSITION_STOP_LOAD;
        }
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_STOP_LOAD) {
        if (!tdma_pio_spi_phys_cal_load_normal_step(phys)) {
            phys->snapshot.program_switch_fail_count++;
            return;
        }
        if (phys->cal_loopback_program_count == 2u) {
            phys->cal_loopback_program_count = 0u;
            phys->cal_loopback_stop_pending = false;
            phys->cal_loopback_transition =
                TDMA_PIO_SPI_CAL_TRANSITION_IDLE;
        }
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_START_UNLOAD) {
        if (s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE) {
            phys->cal_loopback_program_count = 3u;
            phys->cal_loopback_transition =
                TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_TX;
            return;
        }
        if (!tdma_pio_spi_phys_cal_persona_switch_ready()) {
            tdma_pio_spi_phys_cal_transition_fail(phys);
            return;
        }
        bool complete = false;
        if (!tdma_pio_spi_phys_cal_unload_source_step(phys, &complete)) {
            tdma_pio_spi_phys_cal_transition_fail(phys);
            return;
        }
        if (complete) {
            phys->cal_loopback_program_count = 0u;
            phys->cal_loopback_transition =
                TDMA_PIO_SPI_CAL_TRANSITION_START_LOAD;
        }
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_START_LOAD) {
        if (!tdma_pio_spi_phys_cal_load_p3_step(phys)) {
            tdma_pio_spi_phys_cal_transition_fail(phys);
            return;
        }
        if (phys->cal_loopback_program_count == 3u) {
            phys->cal_loopback_transition =
                TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_TX;
        }
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_TX) {
        tdma_pio_spi_p3_initiator_program_init(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM,
            s_tdma_pio_spi_p3_initiator_offset,
            BOARD_TDMA_SPI_DOWNLINK_CSN_PIN,
            BOARD_TDMA_SPI_DOWNLINK_SCK_PIN,
            BOARD_TDMA_SPI_BAUD_HZ);
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_RESPONDER;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_RESPONDER) {
        tdma_pio_spi_p3_responder_program_init(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM,
            s_tdma_pio_spi_p3_responder_offset,
            BOARD_TDMA_SPI_UPLINK_CSN_PIN,
            BOARD_TDMA_SPI_UPLINK_SCK_PIN,
            BOARD_TDMA_SPI_DOWNLINK_TX_PIN,
            BOARD_TDMA_SPI_BAUD_HZ);
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_CAPTURE;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_CAPTURE) {
        tdma_pio_spi_cal_loopback_capture_program_init(
            BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM,
            s_tdma_pio_spi_p3_capture_offset,
            phys->cal_loopback_sample_hz);
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_DMA;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_START_CONFIGURE_DMA) {
        if (!tdma_pio_spi_phys_ensure_rx_dma()) {
            tdma_pio_spi_phys_cal_transition_fail(phys);
            return;
        }
        memset(s_tdma_pio_spi_cal_ring, 0,
               sizeof(s_tdma_pio_spi_cal_ring));
        dma_channel_config dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, false);
        channel_config_set_write_increment(&dc, true);
        channel_config_set_dreq(
            &dc, pio_get_dreq(BOARD_TDMA_SPI_PIO,
                              BOARD_TDMA_SPI_CAPTURE_SM, false));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &dc,
                              s_tdma_pio_spi_cal_ring,
                              &BOARD_TDMA_SPI_PIO->rxf[
                                  BOARD_TDMA_SPI_CAPTURE_SM],
                              phys->cal_loopback_sample_words, false);
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_START_ARM;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_START_ARM) {
    const uint tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    const uint responder_sm = BOARD_TDMA_SPI_SLAVE_SM;
    const uint capture_sm = BOARD_TDMA_SPI_CAPTURE_SM;
    tdma_pio_spi_phys_cal_write_begin(phys);
    memset(&phys->cal_loopback, 0, sizeof(phys->cal_loopback));
    phys->cal_loopback.armed = 1u;
    phys->cal_loopback.sample_hz = phys->cal_loopback_sample_hz;
    phys->cal_loopback.sample_period_ns =
        1000000000u / phys->cal_loopback_sample_hz;
    phys->cal_loopback.requested_words = phys->cal_loopback_sample_words;
    phys->cal_loopback.flags = TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_PIO_DMA;
    phys->cal_loopback.epoch = phys->cal_loopback_epoch;
    phys->cal_loopback_tx_sm = tx_sm;
    phys->cal_loopback_capture_sm = capture_sm;
    phys->cal_loopback_start_pending = false;
    phys->cal_loopback_transition = TDMA_PIO_SPI_CAL_TRANSITION_IDLE;
    tdma_pio_spi_phys_cal_write_end(phys);
    pio_sm_put(BOARD_TDMA_SPI_PIO, tx_sm, 15u);
    pio_sm_put(BOARD_TDMA_SPI_PIO, responder_sm, 15u);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,
                               (1u << tx_sm) | (1u << responder_sm) |
                               (1u << capture_sm));
        return;
    }

    if (phys->cal_loopback_transition ==
            TDMA_PIO_SPI_CAL_TRANSITION_IDLE &&
        phys->cal_loopback.armed != 0u &&
        s_tdma_pio_spi_rx_dma_channel >= 0 &&
        dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count == 0u) {
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_FREEZE;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_FREEZE) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,
                           phys->cal_loopback_tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,
                           BOARD_TDMA_SPI_SLAVE_SM, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,
                           phys->cal_loopback_capture_sm, false);
        tdma_pio_spi_phys_cal_write_begin(phys);
        phys->cal_loopback.produced_words =
            phys->cal_loopback.requested_words;
        phys->cal_loopback.armed = 0u;
        tdma_pio_spi_phys_cal_write_end(phys);
        phys->cal_loopback_decode_word = 0u;
        phys->cal_loopback_decode_previous = 0u;
        phys->cal_loopback_decode_found = 0u;
        phys->cal_loopback_decode_sync_edges = 0u;
        phys->cal_loopback_decode_have_previous = false;
        memset(phys->cal_loopback_decode_times, 0,
               sizeof(phys->cal_loopback_decode_times));
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_DECODE;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_DECODE) {
        tdma_pio_spi_phys_cal_write_begin(phys);
        const bool complete = tdma_pio_spi_phys_cal_decode_step(phys);
        tdma_pio_spi_phys_cal_write_end(phys);
        if (complete) {
            phys->cal_loopback_transition =
                TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_CLEANUP;
        }
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_CLEANUP) {
        tdma_pio_spi_phys_cal_cleanup(phys);
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_PUBLISH;
        return;
    }

    if (phys->cal_loopback_transition ==
        TDMA_PIO_SPI_CAL_TRANSITION_CAPTURE_PUBLISH) {
        phys->cal_loopback_transition =
            TDMA_PIO_SPI_CAL_TRANSITION_IDLE;
        tdma_pio_spi_phys_cal_write_begin(phys);
        phys->cal_loopback.complete = 1u;
        tdma_pio_spi_phys_cal_write_end(phys);
    }
}

static uint32_t tdma_pio_spi_cal_sample_byte(uint32_t word, uint32_t index)
{
    return (word >> (index * 8u)) & 0xFFu;
}

static bool tdma_pio_spi_phys_cal_decode_step(tdma_pio_spi_phys_t *phys)
{
    const uint32_t words_per_beat = 8u;
    uint32_t end_word = phys->cal_loopback_decode_word + words_per_beat;
    if (end_word > phys->cal_loopback.requested_words) {
        end_word = phys->cal_loopback.requested_words;
    }
    const uint32_t period = phys->cal_loopback.sample_period_ns;
    while (phys->cal_loopback_decode_word < end_word &&
           phys->cal_loopback_decode_found != 0x0Fu) {
        const uint32_t word = phys->cal_loopback_decode_word;
        for (uint32_t i = 0u;
             i < 4u && phys->cal_loopback_decode_found != 0x0Fu; i++) {
            const uint32_t sample = tdma_pio_spi_cal_sample_byte(
                s_tdma_pio_spi_cal_ring[word], i);
            if (!phys->cal_loopback_decode_have_previous) {
                phys->cal_loopback_decode_previous = sample;
                phys->cal_loopback_decode_have_previous = true;
                continue;
            }
            const uint32_t rising =
                sample & ~phys->cal_loopback_decode_previous;
            const uint64_t t =
                ((uint64_t)word * 4ull + i) * period;
            if ((rising & (1u << 2u)) != 0u) {
                phys->cal_loopback_decode_sync_edges |= 1u;
            }
            if ((rising & (1u << 3u)) != 0u) {
                phys->cal_loopback_decode_sync_edges |= 2u;
            }
            if ((rising & (1u << 1u)) != 0u &&
                (phys->cal_loopback_decode_found & 1u) == 0u) {
                phys->cal_loopback_decode_times[0] = t;
                phys->cal_loopback_decode_found |= 1u;
            }
            if ((rising & (1u << 4u)) != 0u &&
                (phys->cal_loopback_decode_found & 2u) == 0u) {
                phys->cal_loopback_decode_times[1] = t;
                phys->cal_loopback_decode_found |= 2u;
            }
            if ((rising & (1u << 5u)) != 0u &&
                (phys->cal_loopback_decode_found & 4u) == 0u) {
                phys->cal_loopback_decode_times[2] = t;
                phys->cal_loopback_decode_found |= 4u;
            }
            if ((rising & (1u << 0u)) != 0u &&
                (phys->cal_loopback_decode_found & 8u) == 0u) {
                phys->cal_loopback_decode_times[3] = t;
                phys->cal_loopback_decode_found |= 8u;
            }
            phys->cal_loopback_decode_previous = sample;
        }
        phys->cal_loopback_decode_word++;
    }
    if (phys->cal_loopback_decode_found != 0x0Fu &&
        phys->cal_loopback_decode_word <
            phys->cal_loopback.requested_words) {
        return false;
    }
    phys->cal_loopback.edge_mask = phys->cal_loopback_decode_found;
    phys->cal_loopback.t1_clk_tx = phys->cal_loopback_decode_times[0];
    phys->cal_loopback.t2_clk_rx = phys->cal_loopback_decode_times[1];
    phys->cal_loopback.t3_data_tx = phys->cal_loopback_decode_times[2];
    phys->cal_loopback.t4_data_rx = phys->cal_loopback_decode_times[3];
    if (phys->cal_loopback_decode_sync_edges == 3u) {
        phys->cal_loopback.flags |=
            TDMA_PIO_SPI_CAL_LOOPBACK_FLAG_SYNC_MATCH;
    }
    return true;
}

bool tdma_pio_spi_phys_get_cal_loopback_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_cal_loopback_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(&phys->cal_loopback_guard,
                                               __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->cal_loopback;
        const uint32_t end = __atomic_load_n(&phys->cal_loopback_guard,
                                             __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}
