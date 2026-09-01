static void tdma_pio_spi_phys_p3_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->p3_guard, 1u, __ATOMIC_RELEASE);
}
static void tdma_pio_spi_phys_p3_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->p3_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_phys_p3_set_drivers(uint32_t role)
{
    gpio_put(BOARD_UP_BISS_DE_PIN,
             role == TDMA_PIO_SPI_P3_ROLE_RESPONDER);
    gpio_put(BOARD_DN_BISS_DE_PIN,
             role == TDMA_PIO_SPI_P3_ROLE_INITIATOR);
    gpio_put(BOARD_TRIG_DE_PIN,
             role == TDMA_PIO_SPI_P3_ROLE_INITIATOR);
}

static uint32_t tdma_pio_spi_phys_p3_half_period_ns(uint32_t baud_hz)
{
    const uint64_t sys_hz = clock_get_hz(clk_sys);
    const uint64_t divider_fixed =
        (sys_hz * 256ull + (uint64_t)baud_hz * 2ull) /
        ((uint64_t)baud_hz * 4ull);
    return (uint32_t)((2ull * divider_fixed * 1000000000ull +
                       sys_hz * 128ull) / (sys_hz * 256ull));
}

static bool tdma_pio_spi_phys_p3_unload_source_step(
    tdma_pio_spi_phys_t *phys)
{
    const uint32_t step = phys->p3_program_step;
    uint32_t count = 0u;
    switch (s_tdma_pio_spi_program_persona) {
    case TDMA_PIO_SPI_PROGRAM_PERSONA_NONE:
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
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER:
        count = 3u;
        if (step == 0u) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                &tdma_pio_spi_flight_clock_latch_program,
                s_tdma_pio_spi_flight_clock_latch_offset);
        } else if (step == 1u && s_tdma_pio_spi_program_persona ==
                   TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER) {
            pio_remove_program(BOARD_TDMA_SPI_PIO,
                &tdma_pio_spi_flight_data_follower_program,
                s_tdma_pio_spi_flight_data_follower_offset);
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
    phys->p3_program_step++;
    if (phys->p3_program_step == count) {
        phys->p3_program_step = 0u;
        s_tdma_pio_spi_program_persona =
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        phys->snapshot.program_persona =
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        return true;
    }
    return false;
}

static bool tdma_pio_spi_phys_p3_load_target_step(
    tdma_pio_spi_phys_t *phys)
{
    const bool initiator = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR;
    if (phys->p3_program_count == 0u) {
        const pio_program_t *program = initiator
            ? &tdma_pio_spi_p3_initiator_program
            : &tdma_pio_spi_p3_responder_program;
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO, program)) return false;
        const uint offset = (uint)pio_add_program(BOARD_TDMA_SPI_PIO, program);
        if (initiator) s_tdma_pio_spi_p3_initiator_offset = offset;
        else s_tdma_pio_spi_p3_responder_offset = offset;
    } else if (phys->p3_program_count == 1u) {
        const pio_program_t *program = initiator
            ? &tdma_pio_spi_cal_loopback_capture_program
            : &tdma_pio_spi_p3_responder_capture_program;
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO, program)) return false;
        const uint offset = (uint)pio_add_program(BOARD_TDMA_SPI_PIO, program);
        if (initiator) s_tdma_pio_spi_p3_capture_offset = offset;
        else s_tdma_pio_spi_p3_responder_capture_offset = offset;
    } else {
        return false;
    }
    phys->p3_program_count++;
    if (phys->p3_program_count == 2u) {
        tdma_pio_spi_phys_cal_mark_persona(
            phys, (tdma_pio_spi_program_persona_t)phys->p3_target_persona);
        return true;
    }
    return false;
}

static bool tdma_pio_spi_phys_p3_unload_target_step(
    tdma_pio_spi_phys_t *phys)
{
    if (phys->p3_program_count == 0u) return true;
    const bool initiator = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR;
    const uint32_t index = phys->p3_program_count - 1u;
    if (index == 1u && initiator) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_cal_loopback_capture_program,
            s_tdma_pio_spi_p3_capture_offset);
    } else if (index == 1u) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_p3_responder_capture_program,
            s_tdma_pio_spi_p3_responder_capture_offset);
    } else if (initiator) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_p3_initiator_program,
            s_tdma_pio_spi_p3_initiator_offset);
    } else {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
            &tdma_pio_spi_p3_responder_program,
            s_tdma_pio_spi_p3_responder_offset);
    }
    phys->p3_program_count--;
    if (phys->p3_program_count == 0u) {
        s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        phys->snapshot.program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        return true;
    }
    return false;
}

static bool tdma_pio_spi_phys_p3_load_normal_step(
    tdma_pio_spi_phys_t *phys)
{
    if (phys->p3_program_count == 0u) {
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                                 &tdma_pio_spi_tx_byte_program)) return false;
        s_tdma_pio_spi_tx_offset = (uint)pio_add_program(
            BOARD_TDMA_SPI_PIO, &tdma_pio_spi_tx_byte_program);
    } else if (phys->p3_program_count == 1u) {
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                                 &tdma_pio_spi_rx_byte_program)) return false;
        s_tdma_pio_spi_rx_offset = (uint)pio_add_program(
            BOARD_TDMA_SPI_PIO, &tdma_pio_spi_rx_byte_program);
    } else {
        return false;
    }
    phys->p3_program_count++;
    if (phys->p3_program_count == 2u) {
        tdma_pio_spi_phys_cal_mark_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
        return true;
    }
    return false;
}

static bool tdma_pio_spi_phys_p3_decode_step(tdma_pio_spi_phys_t *phys)
{
    /* signal_group selects the physical line used for the forward leg.
     * The return leg is always DATA for the current P3 wiring.  The third
     * line is only a sync marker and is intentionally absent from t1..t4. */
    const bool forward_is_cs = phys->p3.signal_group ==
        TDMA_PIO_SPI_P3_GROUP_CS_DATA;
    const uint32_t forward_mask = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR
            ? (1u << (forward_is_cs ? 2u : 1u))
            : (1u << (forward_is_cs ? 3u : 4u));
    const uint32_t data_mask = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR ? (1u << 0u) : (1u << 5u);
    const uint32_t period = phys->p3.sample_period_ns;
    const uint32_t words_per_beat = 8u;
    uint32_t end_word = phys->p3_decode_word + words_per_beat;
    if (end_word > phys->p3.requested_words) {
        end_word = phys->p3.requested_words;
    }
    for (uint32_t w = phys->p3_decode_word; w < end_word; w++) {
        for (uint32_t i = 0u; i < 4u; i++) {
            const uint32_t sample = tdma_pio_spi_cal_sample_byte(
                s_tdma_pio_spi_cal_ring[w], i);
            if (!phys->p3_decode_have_previous) {
                phys->p3_decode_previous = sample;
                phys->p3_decode_have_previous = true;
                continue;
            }
            const uint32_t rising = sample & ~phys->p3_decode_previous;
            const uint32_t falling = phys->p3_decode_previous & ~sample;
            const uint64_t timestamp =
                ((uint64_t)w * 4ull + i) * period;
            if ((rising & forward_mask) != 0u) {
                if (!phys->p3_decode_have_clock_rise) {
                    phys->p3_decode_clock_rise = timestamp;
                    phys->p3_decode_have_clock_rise = true;
                }
                if (phys->p3_decode_have_clock_fall) {
                    phys->p3_decode_clock_low_sum +=
                        timestamp - phys->p3_decode_clock_fall;
                    phys->p3_decode_clock_low_count++;
                    phys->p3_decode_have_clock_fall = false;
                }
                phys->p3_decode_clock_rise = timestamp;
            }
            if ((falling & forward_mask) != 0u &&
                phys->p3_decode_have_clock_rise) {
                phys->p3_decode_clock_fall = timestamp;
                phys->p3_decode_have_clock_fall = true;
                phys->p3_decode_clock_high_sum +=
                    timestamp - phys->p3_decode_clock_rise;
                phys->p3_decode_clock_high_count++;
            }
            if ((rising & data_mask) != 0u) {
                phys->p3_decode_data_rise = timestamp;
                phys->p3_decode_have_data_rise = true;
            }
            if ((falling & data_mask) != 0u &&
                phys->p3_decode_have_data_rise) {
                phys->p3_decode_data_high_sum +=
                    timestamp - phys->p3_decode_data_rise;
                phys->p3_decode_data_high_count++;
                phys->p3_decode_have_data_rise = false;
            }
            if (phys->p3.role == TDMA_PIO_SPI_P3_ROLE_INITIATOR) {
                const uint32_t tx_mask = forward_mask;
                if ((rising & tx_mask) != 0u &&
                    (phys->p3_decode_found & 1u) == 0u) {
                    phys->p3_decode_times[0] = timestamp;
                    phys->p3_decode_found |= 1u;
                }
                if ((rising & (1u << 0u)) != 0u &&
                    (phys->p3_decode_found & 8u) == 0u) {
                    phys->p3_decode_times[3] = timestamp;
                    phys->p3_decode_found |= 8u;
                }
            } else {
                const uint32_t rx_mask = forward_mask;
                if ((rising & rx_mask) != 0u &&
                    (phys->p3_decode_found & 2u) == 0u) {
                    phys->p3_decode_times[1] = timestamp;
                    phys->p3_decode_found |= 2u;
                }
                if ((rising & (1u << 5u)) != 0u &&
                    (phys->p3_decode_found & 4u) == 0u) {
                    phys->p3_decode_times[2] = timestamp;
                    phys->p3_decode_found |= 4u;
                }
            }
            phys->p3_decode_previous = sample;
        }
    }
    phys->p3_decode_word = end_word;
    if (end_word != phys->p3.requested_words) return false;
    phys->p3.edge_mask = phys->p3_decode_found;
    /* ABI-compatible field names; semantically these are forward/return
     * edges selected by signal_group, not fixed CLK/DATA functions. */
    phys->p3.t1_clk_tx = phys->p3_decode_times[0];
    phys->p3.t2_clk_rx = phys->p3_decode_times[1];
    phys->p3.t3_data_tx = phys->p3_decode_times[2];
    phys->p3.t4_data_rx = phys->p3_decode_times[3];
    if (phys->p3_decode_clock_high_count != 0u) {
        phys->p3.clock_high_ns = (uint32_t)(
            (phys->p3_decode_clock_high_sum +
             phys->p3_decode_clock_high_count / 2u) /
            phys->p3_decode_clock_high_count);
    }
    if (phys->p3_decode_clock_low_count != 0u) {
        phys->p3.clock_low_ns = (uint32_t)(
            (phys->p3_decode_clock_low_sum +
             phys->p3_decode_clock_low_count / 2u) /
            phys->p3_decode_clock_low_count);
    }
    if (phys->p3_decode_data_high_count != 0u) {
        phys->p3.data_high_ns = (uint32_t)(
            (phys->p3_decode_data_high_sum +
             phys->p3_decode_data_high_count / 2u) /
            phys->p3_decode_data_high_count);
    }
    phys->p3.data_pulse_count = phys->p3_decode_data_high_count;
    return true;
}

bool tdma_pio_spi_phys_p3_start(
    tdma_pio_spi_phys_t *phys, const tdma_pio_spi_p3_request_t *request)
{
    if (phys == NULL || request == NULL ||
        (request->role != TDMA_PIO_SPI_P3_ROLE_INITIATOR &&
         request->role != TDMA_PIO_SPI_P3_ROLE_RESPONDER) ||
        request->signal_group > TDMA_PIO_SPI_P3_GROUP_CS_DATA ||
        (request->baud_hz != 10000000u &&
         request->baud_hz != 25000000u &&
         request->baud_hz != 30000000u) ||
        request->pulse_count < 4u || request->pulse_count > 1024u ||
        request->capture_words == 0u ||
        request->capture_words > TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS ||
        phys->p3_transition != TDMA_PIO_SPI_P3_TRANSITION_IDLE ||
        phys->p3.state == TDMA_PIO_SPI_P3_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING) {
        return false;
    }
    const tdma_pio_spi_program_persona_t persona =
        request->role == TDMA_PIO_SPI_P3_ROLE_INITIATOR
            ? (request->signal_group == TDMA_PIO_SPI_P3_GROUP_CS_DATA
                   ? TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR
                   : TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR)
            : (request->signal_group == TDMA_PIO_SPI_P3_GROUP_CS_DATA
                   ? TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER
                   : TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER);
    const uint32_t half_ns =
        tdma_pio_spi_phys_p3_half_period_ns(request->baud_hz);
    tdma_pio_spi_phys_p3_write_begin(phys);
    memset(&phys->p3, 0, sizeof(phys->p3));
    /* ARMED means the offline request has been accepted.  Persona loading,
     * SM configuration and DMA arming still advance in bounded transitions. */
    phys->p3.state = TDMA_PIO_SPI_P3_ARMED;
    phys->p3.role = request->role;
    phys->p3.signal_group = request->signal_group;
    phys->p3.flags = TDMA_PIO_SPI_P3_FLAG_DIAGNOSTIC_ONLY;
    phys->p3.baud_hz = request->baud_hz;
    phys->p3.epoch = request->epoch;
    phys->p3.sample_period_ns = 4u;
    phys->p3.pulse_count = request->pulse_count;
    phys->p3.requested_words = request->capture_words;
    phys->p3.clock_high_ns = half_ns;
    phys->p3.clock_low_ns = half_ns;
    phys->p3.data_high_ns =
        tdma_pio_spi_p3_data_high_cycles(request->baud_hz) * 4u;
    tdma_pio_spi_phys_p3_write_end(phys);
    phys->p3_target_persona = (uint32_t)persona;
    phys->p3_program_step = 0u;
    phys->p3_program_count = 0u;
    phys->p3_publish_state = TDMA_PIO_SPI_P3_ERROR;
    phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_START_UNLOAD;
    return true;
}

void tdma_pio_spi_phys_p3_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_IDLE &&
        s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL &&
        phys->p3.state != TDMA_PIO_SPI_P3_ARMED) {
        tdma_pio_spi_phys_p3_write_begin(phys);
        memset(&phys->p3, 0, sizeof(phys->p3));
        tdma_pio_spi_phys_p3_write_end(phys);
        return;
    }
    phys->p3_publish_state = TDMA_PIO_SPI_P3_IDLE;
    phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
}

void tdma_pio_spi_phys_p3_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    const uint tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    const uint capture_sm = BOARD_TDMA_SPI_SLAVE_SM;

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_IDLE) {
        if (phys->p3.state == TDMA_PIO_SPI_P3_ARMED &&
            s_tdma_pio_spi_rx_dma_channel >= 0 &&
            dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count ==
                0u) {
            phys->p3_transition =
                TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_FREEZE;
        }
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE ||
        phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_FREEZE) {
        if (s_tdma_pio_spi_rx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        }
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, capture_sm, false);
        if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE) {
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_CLEANUP;
        } else {
            phys->p3_decode_word = 0u;
            phys->p3_decode_previous = 0u;
            phys->p3_decode_found = 0u;
            phys->p3_decode_have_previous = false;
            phys->p3_decode_have_clock_rise = false;
            phys->p3_decode_have_clock_fall = false;
            phys->p3_decode_have_data_rise = false;
            memset(phys->p3_decode_times, 0, sizeof(phys->p3_decode_times));
            phys->p3_decode_clock_rise = 0u;
            phys->p3_decode_clock_fall = 0u;
            phys->p3_decode_clock_high_sum = 0u;
            phys->p3_decode_clock_low_sum = 0u;
            phys->p3_decode_clock_high_count = 0u;
            phys->p3_decode_clock_low_count = 0u;
            phys->p3_decode_data_rise = 0u;
            phys->p3_decode_data_high_sum = 0u;
            phys->p3_decode_data_high_count = 0u;
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_DECODE;
        }
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_STOP_CLEANUP ||
        phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_CLEANUP) {
        tdma_pio_spi_phys_cal_cleanup(phys);
        phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_RESTORE_UNLOAD;
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_START_UNLOAD) {
        if (!tdma_pio_spi_phys_cal_persona_switch_ready()) return;
        const uint32_t before = phys->p3_program_step;
        const bool complete = tdma_pio_spi_phys_p3_unload_source_step(phys);
        if (!complete && before == phys->p3_program_step) {
            phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_RESOURCE;
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
        } else if (complete) {
            phys->p3_program_count = 0u;
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_START_LOAD;
        }
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_START_LOAD) {
        const uint32_t before = phys->p3_program_count;
        const bool complete = tdma_pio_spi_phys_p3_load_target_step(phys);
        if (!complete && before == phys->p3_program_count) {
            phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_RESOURCE;
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
        } else if (complete) {
            phys->p3_transition =
                TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_TX;
        }
        return;
    }

    if (phys->p3_transition ==
        TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_TX) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, tx_sm, false);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, tx_sm);
        pio_sm_restart(BOARD_TDMA_SPI_PIO, tx_sm);
        const bool forward_is_cs = phys->p3.signal_group ==
            TDMA_PIO_SPI_P3_GROUP_CS_DATA;
        if (phys->p3.role == TDMA_PIO_SPI_P3_ROLE_INITIATOR) {
            tdma_pio_spi_p3_initiator_program_init(
                BOARD_TDMA_SPI_PIO, tx_sm,
                s_tdma_pio_spi_p3_initiator_offset,
                forward_is_cs ? BOARD_TDMA_SPI_DOWNLINK_SCK_PIN
                              : BOARD_TDMA_SPI_DOWNLINK_CSN_PIN,
                forward_is_cs ? BOARD_TDMA_SPI_DOWNLINK_CSN_PIN
                              : BOARD_TDMA_SPI_DOWNLINK_SCK_PIN,
                phys->p3.baud_hz);
        } else {
            tdma_pio_spi_p3_responder_program_init(
                BOARD_TDMA_SPI_PIO, tx_sm,
                s_tdma_pio_spi_p3_responder_offset,
                forward_is_cs ? BOARD_TDMA_SPI_UPLINK_SCK_PIN
                              : BOARD_TDMA_SPI_UPLINK_CSN_PIN,
                forward_is_cs ? BOARD_TDMA_SPI_UPLINK_CSN_PIN
                              : BOARD_TDMA_SPI_UPLINK_SCK_PIN,
                BOARD_TDMA_SPI_DOWNLINK_TX_PIN, phys->p3.baud_hz);
        }
        phys->p3_transition =
            TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_CAPTURE;
        return;
    }

    if (phys->p3_transition ==
        TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_CAPTURE) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, capture_sm, false);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, capture_sm);
        pio_sm_restart(BOARD_TDMA_SPI_PIO, capture_sm);
        if (phys->p3.role == TDMA_PIO_SPI_P3_ROLE_INITIATOR) {
            tdma_pio_spi_cal_loopback_capture_program_init(
                BOARD_TDMA_SPI_PIO, capture_sm,
                s_tdma_pio_spi_p3_capture_offset, 250000000u);
        } else {
            const bool forward_is_cs = phys->p3.signal_group ==
                TDMA_PIO_SPI_P3_GROUP_CS_DATA;
            tdma_pio_spi_p3_responder_capture_program_init(
                BOARD_TDMA_SPI_PIO, capture_sm,
                s_tdma_pio_spi_p3_responder_capture_offset,
                forward_is_cs ? BOARD_TDMA_SPI_UPLINK_SCK_PIN
                              : BOARD_TDMA_SPI_UPLINK_CSN_PIN);
        }
        phys->p3_transition =
            TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_DMA;
        return;
    }

    if (phys->p3_transition ==
        TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_DMA) {
        if (!tdma_pio_spi_phys_ensure_rx_dma()) {
            phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_DMA;
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
            return;
        }
        memset(s_tdma_pio_spi_cal_ring, 0, sizeof(s_tdma_pio_spi_cal_ring));
        dma_channel_config dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, false);
        channel_config_set_write_increment(&dc, true);
        channel_config_set_dreq(&dc, pio_get_dreq(
            BOARD_TDMA_SPI_PIO, capture_sm, false));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &dc,
            s_tdma_pio_spi_cal_ring,
            &BOARD_TDMA_SPI_PIO->rxf[capture_sm],
            phys->p3.requested_words, false);
        phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_START_ARM;
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_START_ARM) {
        pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 1u);
        pio_sm_put(BOARD_TDMA_SPI_PIO, tx_sm, phys->p3.pulse_count - 1u);
        phys->armed = true;
        phys->tx_sm = tx_sm;
        phys->rx_sm = capture_sm;
        tdma_pio_spi_phys_p3_set_drivers(phys->p3.role);
        dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
        pio_enable_sm_mask_in_sync(
            BOARD_TDMA_SPI_PIO, (1u << tx_sm) | (1u << capture_sm));
        tdma_pio_spi_phys_p3_write_begin(phys);
        phys->p3.state = TDMA_PIO_SPI_P3_ARMED;
        tdma_pio_spi_phys_p3_write_end(phys);
        phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_IDLE;
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_DECODE) {
        tdma_pio_spi_phys_p3_write_begin(phys);
        const bool complete = tdma_pio_spi_phys_p3_decode_step(phys);
        tdma_pio_spi_phys_p3_write_end(phys);
        if (complete) {
            const uint32_t expected = phys->p3.role ==
                TDMA_PIO_SPI_P3_ROLE_INITIATOR ? 0x09u : 0x06u;
            tdma_pio_spi_phys_p3_write_begin(phys);
            phys->p3.produced_words = phys->p3.requested_words;
            phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_DMA_COMPLETE |
                              TDMA_PIO_SPI_P3_FLAG_HARDWARE_LATCHED |
                              TDMA_PIO_SPI_P3_FLAG_SYNC_MATCH;
            phys->p3_publish_state =
                (phys->p3.edge_mask & expected) == expected
                    ? TDMA_PIO_SPI_P3_COMPLETE
                    : TDMA_PIO_SPI_P3_ERROR;
            if (phys->p3_publish_state == TDMA_PIO_SPI_P3_ERROR) {
                phys->p3.reject_reason =
                    TDMA_PIO_SPI_P3_REJECT_EDGE_MISSING;
            }
            tdma_pio_spi_phys_p3_write_end(phys);
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_CLEANUP;
        }
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_RESTORE_UNLOAD) {
        const uint32_t before = phys->p3_program_count;
        const bool complete = tdma_pio_spi_phys_p3_unload_target_step(phys);
        if (!complete && before == phys->p3_program_count) return;
        if (complete) {
            phys->p3_program_count = 0u;
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_RESTORE_LOAD;
        }
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_RESTORE_LOAD) {
        const uint32_t before = phys->p3_program_count;
        const bool complete = tdma_pio_spi_phys_p3_load_normal_step(phys);
        if (!complete && before == phys->p3_program_count) return;
        if (complete) {
            phys->p3_program_count = 0u;
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_PUBLISH;
        }
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_PUBLISH) {
        tdma_pio_spi_phys_p3_write_begin(phys);
        phys->p3.state = phys->p3_publish_state;
        if (phys->p3.state == TDMA_PIO_SPI_P3_IDLE) {
            memset(&phys->p3, 0, sizeof(phys->p3));
        }
        tdma_pio_spi_phys_p3_write_end(phys);
        phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_IDLE;
    }
}

bool tdma_pio_spi_phys_get_p3_snapshot(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_p3_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->p3;
        const uint32_t end =
            __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

