static void tdma_pio_spi_phys_p3_write_begin(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->p3_guard, 1u, __ATOMIC_ACQ_REL);
}
static void tdma_pio_spi_phys_p3_write_end(tdma_pio_spi_phys_t *phys)
{
    (void)__atomic_add_fetch(&phys->p3_guard, 1u, __ATOMIC_RELEASE);
}

static bool tdma_pio_spi_phys_p3_is_rxphase(uint32_t role)
{
    return role == TDMA_PIO_SPI_P3_ROLE_SCK_RXPHASE || role == TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE;
}

static bool tdma_pio_spi_phys_p3_is_codeword(uint32_t role)
{
    return role == TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK ||
           role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK ||
           tdma_pio_spi_phys_p3_is_rxphase(role);
}

static bool tdma_pio_spi_phys_p3_is_dual(uint32_t role)
{
    return role == TDMA_PIO_SPI_P3_ROLE_LOOPBACK ||
           role == TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK ||
           role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK;
}

static void tdma_pio_spi_phys_p3_set_drivers(uint32_t role)
{
    gpio_put(BOARD_UP_BISS_DE_PIN,
             role == TDMA_PIO_SPI_P3_ROLE_RESPONDER ||
             tdma_pio_spi_phys_p3_is_dual(role));
    gpio_put(BOARD_DN_BISS_DE_PIN,
             role != TDMA_PIO_SPI_P3_ROLE_RESPONDER &&
             role != TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK);
    gpio_put(BOARD_TRIG_DE_PIN,
             role != TDMA_PIO_SPI_P3_ROLE_RESPONDER &&
             role != TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK);
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

_Static_assert(sizeof(tdma_pio_spi_p3_initiator_program_instructions) /
                   sizeof(uint16_t) +
               sizeof(tdma_pio_spi_p3_responder_program_instructions) /
                   sizeof(uint16_t) +
               sizeof(tdma_pio_spi_cal_loopback_capture_program_instructions) /
                   sizeof(uint16_t) <= 32u,
               "Concurrent P3 endpoints must fit one maintenance PIO");
_Static_assert(BOARD_TDMA_SPI_MASTER_SM != BOARD_TDMA_SPI_SLAVE_SM &&
               BOARD_TDMA_SPI_MASTER_SM != BOARD_TDMA_SPI_CAPTURE_SM &&
               BOARD_TDMA_SPI_SLAVE_SM != BOARD_TDMA_SPI_CAPTURE_SM,
               "P3 endpoints and capture require distinct SM slots");
_Static_assert(sizeof(tdma_pio_spi_marker_origin_program_instructions) /
                   sizeof(uint16_t) +
               sizeof(tdma_pio_spi_marker_forward_program_instructions) /
                   sizeof(uint16_t) +
               sizeof(tdma_pio_spi_cal_loopback_capture_program_instructions) /
                   sizeof(uint16_t) <= 32u,
               "MARK loopback catalog must fit maintenance PIO");
_Static_assert(sizeof(tdma_pio_spi_sck_loopback_origin_program_instructions) /
                   sizeof(uint16_t) +
               sizeof(tdma_pio_spi_sck_loopback_forward_program_instructions) /
                   sizeof(uint16_t) +
               sizeof(tdma_pio_spi_cal_loopback_capture_program_instructions) /
                   sizeof(uint16_t) <= 32u,
               "SCK loopback catalog must fit maintenance PIO");
_Static_assert(TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_LEAD_SAMPLES % 32u == 0u &&
               TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_HIGH_SAMPLES % 32u == 0u,
               "SCK diagnostic origin pulse uses whole DMA words");
_Static_assert(sizeof(tdma_pio_spi_sck_loopback_origin_program_instructions) /
                   sizeof(uint16_t) + sizeof(tdma_pio_spi_sck_train_sink_program_instructions) /
                   sizeof(uint16_t) <= 32u, "RXPHASE source and sink must fit one PIO");
_Static_assert(sizeof(tdma_pio_spi_mark_rxphase_origin_program_instructions) /
                   sizeof(uint16_t) + sizeof(tdma_pio_spi_mark_rxphase_sink_program_instructions) /
                   sizeof(uint16_t) <= 32u, "MARK RXPHASE programs must fit one PIO");
_Static_assert(TDMA_PIO_SPI_SCK_LOOPBACK_LAUNCH_GUARD_SAMPLES % 32u == 0u,
               "MARK RXPHASE codeword starts at a whole DMA word");

static void tdma_pio_spi_phys_p3_decode_reset(tdma_pio_spi_phys_t *phys)
{
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
    if (tdma_pio_spi_phys_p3_is_rxphase(phys->p3.role)) {
        const bool mark = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE;
        const pio_program_t *programs[] = {mark ? &tdma_pio_spi_mark_rxphase_origin_program : &tdma_pio_spi_sck_loopback_origin_program,
                                         mark ? &tdma_pio_spi_mark_rxphase_sink_program : &tdma_pio_spi_sck_train_sink_program};
        uint *offsets[] = {mark ? &s_tdma_pio_spi_mark_rxphase_origin_offset : &s_tdma_pio_spi_rxphase_origin_offset,
                          mark ? &s_tdma_pio_spi_mark_rxphase_sink_offset : &s_tdma_pio_spi_rxphase_sink_offset};
        const uint32_t i = phys->p3_program_count;
        if (i >= 2u || !pio_can_add_program(BOARD_TDMA_SPI_PIO, programs[i])) return false;
        *offsets[i] = pio_add_program(BOARD_TDMA_SPI_PIO, programs[i]);
        if (++phys->p3_program_count != 2u) return false;
        tdma_pio_spi_phys_cal_mark_persona(phys, mark ? TDMA_PIO_SPI_PROGRAM_PERSONA_MARK_RXPHASE : TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_RXPHASE);
        return true;
    }
    if (tdma_pio_spi_phys_p3_is_dual(phys->p3.role)) {
        const bool marker = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK;
        const bool sck = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK;
        const pio_program_t *programs[] = {
            sck ? &tdma_pio_spi_sck_loopback_origin_program : marker ? &tdma_pio_spi_marker_origin_program
                   : &tdma_pio_spi_p3_initiator_program,
            sck ? &tdma_pio_spi_sck_loopback_forward_program : marker ? &tdma_pio_spi_marker_forward_program
                   : &tdma_pio_spi_p3_responder_program,
            &tdma_pio_spi_cal_loopback_capture_program,
        };
        uint *offsets[] = {
            sck ? &s_tdma_pio_spi_sck_loopback_origin_offset : &s_tdma_pio_spi_p3_initiator_offset,
            sck ? &s_tdma_pio_spi_sck_loopback_forward_offset : &s_tdma_pio_spi_p3_responder_offset,
            &s_tdma_pio_spi_p3_capture_offset,
        };
        const uint32_t index = phys->p3_program_count;
        if (index >= 3u || !pio_can_add_program(
                BOARD_TDMA_SPI_PIO, programs[index])) return false;
        *offsets[index] = (uint)pio_add_program(
            BOARD_TDMA_SPI_PIO, programs[index]);
        if (++phys->p3_program_count != 3u) return false;
        tdma_pio_spi_phys_cal_mark_persona(
            phys, sck ? TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_LOOPBACK : marker ? TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER_LOOPBACK
                         : TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE);
        return true;
    }
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
    if (tdma_pio_spi_phys_p3_is_rxphase(phys->p3.role)) {
        const bool mark = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE;
        const pio_program_t *programs[] = {mark ? &tdma_pio_spi_mark_rxphase_origin_program : &tdma_pio_spi_sck_loopback_origin_program,
                                         mark ? &tdma_pio_spi_mark_rxphase_sink_program : &tdma_pio_spi_sck_train_sink_program};
        const uint offsets[] = {mark ? s_tdma_pio_spi_mark_rxphase_origin_offset : s_tdma_pio_spi_rxphase_origin_offset,
                                mark ? s_tdma_pio_spi_mark_rxphase_sink_offset : s_tdma_pio_spi_rxphase_sink_offset};
        const uint32_t i = --phys->p3_program_count;
        pio_remove_program(BOARD_TDMA_SPI_PIO, programs[i], offsets[i]);
        if (phys->p3_program_count != 0u) return false;
        tdma_pio_spi_phys_cal_mark_persona(phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NONE);
        return true;
    }
    if (tdma_pio_spi_phys_p3_is_dual(phys->p3.role)) {
        const bool marker = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK;
        const bool sck = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK;
        const pio_program_t *programs[] = {
            sck ? &tdma_pio_spi_sck_loopback_origin_program : marker ? &tdma_pio_spi_marker_origin_program
                   : &tdma_pio_spi_p3_initiator_program,
            sck ? &tdma_pio_spi_sck_loopback_forward_program : marker ? &tdma_pio_spi_marker_forward_program
                   : &tdma_pio_spi_p3_responder_program,
            &tdma_pio_spi_cal_loopback_capture_program,
        };
        const uint offsets[] = {
            sck ? s_tdma_pio_spi_sck_loopback_origin_offset : s_tdma_pio_spi_p3_initiator_offset,
            sck ? s_tdma_pio_spi_sck_loopback_forward_offset : s_tdma_pio_spi_p3_responder_offset,
            s_tdma_pio_spi_p3_capture_offset,
        };
        const uint32_t index = --phys->p3_program_count;
        pio_remove_program(BOARD_TDMA_SPI_PIO, programs[index], offsets[index]);
        if (phys->p3_program_count != 0u) return false;
        s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        phys->snapshot.program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
        return true;
    }
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
    const bool marker = tdma_pio_spi_phys_p3_is_codeword(phys->p3.role);
    const bool sck = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK;
    tdma_pio_spi_p3_snapshot_t *raw = phys->p3_decode_endpoint ==
        TDMA_PIO_SPI_P3_ROLE_RESPONDER ? &phys->p3_responder : &phys->p3;
    /* signal_group selects the physical line used for the forward leg.
     * The return leg is always DATA for the current P3 wiring.  The third
     * line is only a sync marker and is intentionally absent from t1..t4. */
    const bool forward_is_cs = raw->signal_group ==
        TDMA_PIO_SPI_P3_GROUP_CS_DATA;
    const uint32_t forward_mask = raw->role !=
        TDMA_PIO_SPI_P3_ROLE_RESPONDER
            ? (1u << (forward_is_cs ? 2u : 1u))
            : (1u << (forward_is_cs ? 3u : 4u));
    const uint32_t data_mask = raw->role !=
        TDMA_PIO_SPI_P3_ROLE_RESPONDER ? (1u << 0u) : (1u << 5u);
    const uint32_t period = raw->sample_period_ns;
    const uint32_t words_per_beat = 8u;
    const uint32_t *capture = marker ? s_tdma_pio_spi_marker_loopback_capture
                                     : s_tdma_pio_spi_cal_ring;
    uint32_t end_word = phys->p3_decode_word + words_per_beat;
    if (end_word > phys->p3.requested_words) {
        end_word = phys->p3.requested_words;
    }
    bool publish_preedge = false;
    for (uint32_t w = phys->p3_decode_word; w < end_word; w++) {
        for (uint32_t i = 0u; i < 4u; i++) {
            const uint32_t sample = tdma_pio_spi_cal_sample_byte(
                capture[w], i);
            if (!phys->p3_decode_have_previous) {
                if (marker && (sck ? (sample & 0x33u) == 0u : (sample & 0x2du) == 0x2du)) {
                    publish_preedge = true;
                }
                phys->p3_decode_previous = sample;
                phys->p3_decode_have_previous = true;
                continue;
            }
            const uint32_t rising = sample & ~phys->p3_decode_previous;
            const uint32_t falling = phys->p3_decode_previous & ~sample;
            const uint32_t edge = marker ? falling : rising;
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
            if (raw->role != TDMA_PIO_SPI_P3_ROLE_RESPONDER) {
                const uint32_t tx_mask = forward_mask;
                if ((edge & tx_mask) != 0u &&
                    (phys->p3_decode_found & 1u) == 0u) {
                    phys->p3_decode_times[0] = timestamp;
                    phys->p3_decode_found |= 1u;
                }
                if ((edge & (1u << 0u)) != 0u &&
                    (phys->p3_decode_found & 8u) == 0u) {
                    phys->p3_decode_times[3] = timestamp;
                    phys->p3_decode_found |= 8u;
                }
            } else {
                const uint32_t rx_mask = forward_mask;
                if ((edge & rx_mask) != 0u &&
                    (phys->p3_decode_found & 2u) == 0u) {
                    phys->p3_decode_times[1] = timestamp;
                    phys->p3_decode_found |= 2u;
                }
                if ((edge & (1u << 5u)) != 0u &&
                    (phys->p3_decode_found & 4u) == 0u) {
                    phys->p3_decode_times[2] = timestamp;
                    phys->p3_decode_found |= 4u;
                }
            }
            phys->p3_decode_previous = sample;
        }
    }
    phys->p3_decode_word = end_word;
    if (publish_preedge) {
        tdma_pio_spi_phys_p3_write_begin(phys);
        phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_PREEDGE_HIGH;
        tdma_pio_spi_phys_p3_write_end(phys);
    }
    if (end_word != phys->p3.requested_words) return false;
    /* Decode cursors and sums are Core1-private. Publish only the completed
     * endpoint, keeping sample scans and division outside the snapshot guard. */
    tdma_pio_spi_p3_snapshot_t decoded = *raw;
    decoded.edge_mask = phys->p3_decode_found;
    /* ABI-compatible field names; semantically these are forward/return
     * edges selected by signal_group, not fixed CLK/DATA functions. */
    decoded.t1_clk_tx = phys->p3_decode_times[0];
    decoded.t2_clk_rx = phys->p3_decode_times[1];
    decoded.t3_data_tx = phys->p3_decode_times[2];
    decoded.t4_data_rx = phys->p3_decode_times[3];
    if (phys->p3_decode_clock_high_count != 0u) {
        decoded.clock_high_ns = (uint32_t)(
            (phys->p3_decode_clock_high_sum +
             phys->p3_decode_clock_high_count / 2u) /
            phys->p3_decode_clock_high_count);
    }
    if (phys->p3_decode_clock_low_count != 0u) {
        decoded.clock_low_ns = (uint32_t)(
            (phys->p3_decode_clock_low_sum +
             phys->p3_decode_clock_low_count / 2u) /
            phys->p3_decode_clock_low_count);
    }
    if (phys->p3_decode_data_high_count != 0u) {
        decoded.data_high_ns = (uint32_t)(
            (phys->p3_decode_data_high_sum +
             phys->p3_decode_data_high_count / 2u) /
            phys->p3_decode_data_high_count);
    }
    decoded.data_pulse_count = phys->p3_decode_data_high_count;
    tdma_pio_spi_phys_p3_write_begin(phys);
    *raw = decoded;
    tdma_pio_spi_phys_p3_write_end(phys);
    return true;
}

bool tdma_pio_spi_phys_p3_start(
    tdma_pio_spi_phys_t *phys, const tdma_pio_spi_p3_request_t *request)
{
    if (request != NULL && request->role == TDMA_PIO_SPI_P3_ROLE_DATA_LOOPBACK)
        return tdma_pio_spi_phys_data_loop_start(phys, request);
    const bool marker = request != NULL && tdma_pio_spi_phys_p3_is_codeword(request->role);
    const bool rxphase = request != NULL && tdma_pio_spi_phys_p3_is_rxphase(request->role);
    const bool mark_phase = request != NULL && request->role == TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE;
    const bool sck = request != NULL && (request->role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK || request->role == TDMA_PIO_SPI_P3_ROLE_SCK_RXPHASE);
    const uint64_t base_samples = rxphase ? ((uint64_t)request->rxphase_base_ns + 2u) / 4u : 0u;
    const int64_t effective = (int64_t)base_samples + (request == NULL ? 0 : request->sck_launch_offset_samples);
    if (phys == NULL || request == NULL ||
        (request->role != TDMA_PIO_SPI_P3_ROLE_INITIATOR &&
         request->role != TDMA_PIO_SPI_P3_ROLE_RESPONDER &&
         request->role != TDMA_PIO_SPI_P3_ROLE_LOOPBACK && !marker) ||
        request->signal_group > TDMA_PIO_SPI_P3_GROUP_CS_DATA ||
        (!marker && request->baud_hz != 10000000u &&
         request->baud_hz != 25000000u &&
         request->baud_hz != 30000000u) ||
        (!marker && (request->pulse_count < 4u || request->pulse_count > 1024u)) ||
        (marker && (request->signal_group != (sck ? TDMA_PIO_SPI_P3_GROUP_CLK_DATA : TDMA_PIO_SPI_P3_GROUP_CS_DATA) ||
         request->marker_words == NULL || request->marker_word_count == 0u ||
         request->marker_word_count > TDMA_PIO_SPI_MARKER_BUFFER_WORDS -
             TDMA_PIO_SPI_MARKER_LOOPBACK_LEAD_WORDS -
             TDMA_PIO_SPI_MARKER_LOOPBACK_TAIL_WORDS - (sck ? 1u : 0u) ||
         request->marker_sample_count == 0u ||
         request->marker_sample_count > request->marker_word_count * 32u ||
         request->marker_word_count != (request->marker_sample_count + 31u) / 32u ||
         request->marker_codebook > 3u || request->marker_fault > 1u ||
         request->marker_generation == 0u ||
         request->capture_words < 16u || request->epoch == 0u ||
         request->epoch > 255u)) ||
        (sck && (request->sck_launch_offset_samples < TDMA_PIO_SPI_SCK_LOOPBACK_MIN_OFFSET ||
                 request->sck_launch_offset_samples > TDMA_PIO_SPI_SCK_LOOPBACK_MAX_OFFSET)) ||
        (rxphase && (request->rxphase_base_ns == 0u || request->rxphase_direction > 1u ||
                     (mark_phase && (request->rxphase_direction != 0u || request->capture_words != 256u)) ||
                     request->capture_words > 256u || effective < 0 || effective > 31 ||
                     request->rxphase_destination_cycles != (uint32_t)effective + 1u)) ||
        request->capture_words == 0u ||
        request->capture_words > (marker ? TDMA_PIO_SPI_MARKER_LOOPBACK_CAPTURE_MAX_WORDS
                                         : TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS) ||
        phys->p3_transition != TDMA_PIO_SPI_P3_TRANSITION_IDLE ||
        phys->p3.state == TDMA_PIO_SPI_P3_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_ARMED ||
        phys->marker.state == TDMA_PIO_SPI_MARKER_RUNNING) {
        return false;
    }
    if (!tdma_pio_spi_phys_leave_flight_for_maintenance(phys)) return false;
    if (s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE &&
        s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL) return false;
    const tdma_pio_spi_program_persona_t persona =
        rxphase ? (mark_phase ? TDMA_PIO_SPI_PROGRAM_PERSONA_MARK_RXPHASE : TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_RXPHASE) : tdma_pio_spi_phys_p3_is_dual(request->role)
            ? (sck ? TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_LOOPBACK : marker ? TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER_LOOPBACK
                      : TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE)
            : request->role == TDMA_PIO_SPI_P3_ROLE_INITIATOR
            ? (request->signal_group == TDMA_PIO_SPI_P3_GROUP_CS_DATA
                   ? TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR
                   : TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR)
            : (request->signal_group == TDMA_PIO_SPI_P3_GROUP_CS_DATA
                   ? TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER
                   : TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER);
    const uint32_t half_ns =
        marker ? 0u : tdma_pio_spi_phys_p3_half_period_ns(request->baud_hz);
    tdma_pio_spi_phys_p3_write_begin(phys);
    memset(&phys->p3, 0, sizeof(phys->p3));
    memset(&phys->p3_responder, 0, sizeof(phys->p3_responder));
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
        marker ? 0u : tdma_pio_spi_p3_data_high_cycles(request->baud_hz) * 4u;
    if (tdma_pio_spi_phys_p3_is_dual(request->role)) {
        /* Loopback quality is measured from both pad pairs, never inferred
         * from the programmed divider when an edge/period is absent. */
        phys->p3.clock_high_ns = 0u;
        phys->p3.clock_low_ns = 0u;
        phys->p3.data_high_ns = 0u;
        phys->p3_responder = phys->p3;
        phys->p3_responder.role = TDMA_PIO_SPI_P3_ROLE_RESPONDER;
    }
    phys->p3_decode_endpoint = TDMA_PIO_SPI_P3_ROLE_INITIATOR;
    phys->p3_stop_pending = false;
    phys->p3_marker_codebook = marker ? request->marker_codebook : 0u;
    phys->p3_marker_generation = marker ? request->marker_generation : 0u;
    phys->p3_sck_launch_offset_samples = (sck || rxphase) ? request->sck_launch_offset_samples : 0;
    phys->p3_rxphase_base_ns = rxphase ? request->rxphase_base_ns : 0u;
    phys->p3_rxphase_direction = rxphase ? request->rxphase_direction : 0u;
    phys->p3_rxphase_destination_cycles = rxphase ? request->rxphase_destination_cycles : 0u;
    phys->p3_rxphase_instruction = 0u;
    phys->p3_rxphase_remaining = request->capture_words;
    phys->p3_rxphase_fault = request->marker_fault;
    phys->p3_rxphase_capture_configured = false;
    phys->p3_marker_samples = marker ? request->marker_sample_count : 0u;
    phys->p3_marker_tx_words = 0u;
    phys->p3_marker_tx_remaining = 0u;
    phys->p3_marker_drain_check = false;
    phys->p3_marker_deadline_us = time_us_64() + TDMA_PIO_SPI_MARKER_LOOPBACK_TIMEOUT_US;
    if (mark_phase) {
        const uint32_t code_start = TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_LEAD_SAMPLES +
            TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_HIGH_SAMPLES + TDMA_PIO_SPI_SCK_LOOPBACK_LAUNCH_GUARD_SAMPLES;
        phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_MARKER_CODEWORD;
        phys->p3_marker_tx_words = (code_start + request->marker_sample_count +
            TDMA_PIO_SPI_SCK_LOOPBACK_TAIL_SAMPLES + 31u) / 32u;
        phys->p3_marker_tx_remaining = phys->p3_marker_tx_words;
        for (uint32_t i = 0u; i < phys->p3_marker_tx_words; ++i) s_tdma_pio_spi_marker_tx[i] = UINT32_MAX;
        if (request->marker_fault == 0u) {
            for (uint32_t i = TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_LEAD_SAMPLES / 32u;
                 i < (TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_LEAD_SAMPLES + TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_HIGH_SAMPLES) / 32u; ++i)
                s_tdma_pio_spi_marker_tx[i] = 0u;
            memcpy(s_tdma_pio_spi_marker_tx + code_start / 32u, request->marker_words,
                   request->marker_word_count * sizeof(uint32_t));
            const uint32_t tail = request->marker_sample_count & 31u;
            if (tail != 0u) s_tdma_pio_spi_marker_tx[code_start / 32u + request->marker_word_count - 1u] |= UINT32_MAX << tail;
        }
    } else if (sck) {
        const uint32_t code_start = (uint32_t)((int32_t)(TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_LEAD_SAMPLES +
            TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_HIGH_SAMPLES +
            TDMA_PIO_SPI_SCK_LOOPBACK_LAUNCH_GUARD_SAMPLES) + (rxphase ? 0 : request->sck_launch_offset_samples));
        phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_MARKER_CODEWORD;
        phys->p3_marker_tx_words = (code_start + request->marker_sample_count +
            TDMA_PIO_SPI_SCK_LOOPBACK_TAIL_SAMPLES + 31u) / 32u;
        phys->p3_marker_tx_remaining = phys->p3_marker_tx_words;
        for (uint32_t i = 0; i < phys->p3_marker_tx_words; ++i) s_tdma_pio_spi_marker_tx[i] = 0u;
        if (request->marker_fault == 0u) {
            for (uint32_t i = TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_LEAD_SAMPLES / 32u;
                 i < (TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_LEAD_SAMPLES +
                      TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_HIGH_SAMPLES) / 32u; ++i)
                s_tdma_pio_spi_marker_tx[i] = UINT32_MAX;
            const uint32_t shift = code_start & 31u;
            for (uint32_t i = 0; i < request->marker_word_count; ++i) {
                uint32_t value = request->marker_words[i];
                const uint32_t valid = request->marker_sample_count - i * 32u;
                if (valid < 32u) value &= (1u << valid) - 1u;
                s_tdma_pio_spi_marker_tx[code_start / 32u + i] |= value << shift;
                if (shift) s_tdma_pio_spi_marker_tx[code_start / 32u + i + 1u] |= value >> (32u - shift);
            }
        }
    } else if (marker) {
        phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_MARKER_CODEWORD;
        phys->p3_marker_tx_words = request->marker_word_count +
            TDMA_PIO_SPI_MARKER_LOOPBACK_LEAD_WORDS +
            TDMA_PIO_SPI_MARKER_LOOPBACK_TAIL_WORDS;
        phys->p3_marker_tx_remaining = phys->p3_marker_tx_words;
        for (uint32_t i = 0; i < phys->p3_marker_tx_words; ++i) {
            s_tdma_pio_spi_marker_tx[i] = UINT32_MAX;
        }
        if (request->marker_fault == 0u) {
            memcpy(s_tdma_pio_spi_marker_tx + TDMA_PIO_SPI_MARKER_LOOPBACK_LEAD_WORDS,
                   request->marker_words, request->marker_word_count * sizeof(uint32_t));
            const uint32_t tail_bits = request->marker_sample_count & 31u;
            if (tail_bits != 0u) {
                s_tdma_pio_spi_marker_tx[TDMA_PIO_SPI_MARKER_LOOPBACK_LEAD_WORDS +
                    request->marker_word_count - 1u] |= UINT32_MAX << tail_bits;
            }
        }
    }
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
    if (phys->p3.role == TDMA_PIO_SPI_P3_ROLE_DATA_LOOPBACK) {
        tdma_pio_spi_phys_data_loop_stop(phys);
        return;
    }
    const bool p3_persona =
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER_LOOPBACK ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_LOOPBACK ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_RXPHASE ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_MARK_RXPHASE;
    /* An idle P3 STOP does not own another persona's resident programs, SMs
     * or DMA. In particular, restoring NORMAL here would leak flight code. */
    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_IDLE &&
        !p3_persona &&
        phys->p3.state != TDMA_PIO_SPI_P3_ARMED) {
        tdma_pio_spi_phys_p3_write_begin(phys);
        memset(&phys->p3, 0, sizeof(phys->p3));
        memset(&phys->p3_responder, 0, sizeof(phys->p3_responder));
        tdma_pio_spi_phys_p3_write_end(phys);
        return;
    }
    phys->p3_publish_state = TDMA_PIO_SPI_P3_IDLE;
    /* A partial source unload must finish before restoring NORMAL. During
     * restoration the counter already names NORMAL programs, not P3. */
    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_START_UNLOAD) {
        phys->p3_stop_pending = true;
        return;
    }
    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_RESTORE_UNLOAD ||
        phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_RESTORE_LOAD ||
        phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE ||
        phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_STOP_CLEANUP ||
        phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_PUBLISH) return;
    phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
}

void tdma_pio_spi_phys_p3_service(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    if (phys->p3.role == TDMA_PIO_SPI_P3_ROLE_DATA_LOOPBACK) {
        tdma_pio_spi_phys_data_loop_service(phys);
        return;
    }
    const uint tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    const bool marker = tdma_pio_spi_phys_p3_is_codeword(phys->p3.role);
    const bool sck = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK;
    const bool rxphase = tdma_pio_spi_phys_p3_is_rxphase(phys->p3.role);
    const bool mark_phase = phys->p3.role == TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE;
    const bool loopback = tdma_pio_spi_phys_p3_is_dual(phys->p3.role);
    const uint capture_sm = loopback ? BOARD_TDMA_SPI_CAPTURE_SM
                                    : BOARD_TDMA_SPI_SLAVE_SM;

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_IDLE) {
        if (marker && phys->p3.state == TDMA_PIO_SPI_P3_ARMED) {
            const bool captured = s_tdma_pio_spi_rx_dma_channel >= 0 &&
                dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count == 0u;
            if (captured) pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, capture_sm, false);
            const uint32_t remaining = s_tdma_pio_spi_tx_dma_channel < 0
                ? phys->p3_marker_tx_words
                : dma_hw->ch[(uint)s_tdma_pio_spi_tx_dma_channel].transfer_count;
            const uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + tx_sm);
            tdma_pio_spi_phys_p3_write_begin(phys);
            phys->p3_marker_tx_remaining = remaining;
            if (rxphase && phys->p3_rxphase_capture_configured)
                phys->p3_rxphase_remaining = dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
            if (remaining == 0u && pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO, tx_sm)) {
                if (!phys->p3_marker_drain_check) {
                    /* Discard any old midstream stall; require a freshly
                     * reasserted empty-OSR autopull stall on a later beat. */
                    BOARD_TDMA_SPI_PIO->fdebug = stall;
                    phys->p3_marker_drain_check = true;
                } else if ((BOARD_TDMA_SPI_PIO->fdebug & stall) != 0u) {
                    phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_TX_DRAINED;
                }
            }
            if (captured && (phys->p3.flags & TDMA_PIO_SPI_P3_FLAG_TX_DRAINED)) {
                phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_FREEZE;
            } else if (time_us_64() >= phys->p3_marker_deadline_us) {
                phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_TIMEOUT;
                phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
            }
            tdma_pio_spi_phys_p3_write_end(phys);
            return;
        }
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
        if (rxphase && phys->p3_rxphase_capture_configured) {
            /* Latch completed DMA words before abort changes the count. No
             * partial ISR word is represented as a completed raw word. */
            const uint32_t remaining = dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].transfer_count;
            tdma_pio_spi_phys_p3_write_begin(phys);
            phys->p3_rxphase_remaining = remaining;
            phys->p3.produced_words = remaining <= phys->p3.requested_words ? phys->p3.requested_words - remaining : 0u;
            if (remaining == 0u) phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_DMA_COMPLETE;
            tdma_pio_spi_phys_p3_write_end(phys);
        }
        if (marker && s_tdma_pio_spi_tx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
        }
        if (s_tdma_pio_spi_rx_dma_channel >= 0 &&
            dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel)) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        }
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, capture_sm, false);
        if (loopback) {
            pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,
                               BOARD_TDMA_SPI_SLAVE_SM, false);
        }
        if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE) {
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_CLEANUP;
        } else if (rxphase) {
            phys->p3_publish_state = phys->p3.reject_reason == TDMA_PIO_SPI_P3_REJECT_NONE ?
                TDMA_PIO_SPI_P3_COMPLETE : TDMA_PIO_SPI_P3_ERROR;
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_CLEANUP;
        } else {
            tdma_pio_spi_phys_p3_decode_reset(phys);
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
            tdma_pio_spi_phys_p3_write_begin(phys);
            phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_RESOURCE;
            tdma_pio_spi_phys_p3_write_end(phys);
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
        } else if (complete) {
            phys->p3_program_count = 0u;
            phys->p3_transition = phys->p3_stop_pending
                ? TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE
                : TDMA_PIO_SPI_P3_TRANSITION_START_LOAD;
            phys->p3_stop_pending = false;
        }
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_START_LOAD) {
        if (!tdma_pio_spi_phys_ensure_sms_claimed()) {
            tdma_pio_spi_phys_p3_write_begin(phys);
            phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_RESOURCE;
            tdma_pio_spi_phys_p3_write_end(phys);
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
            return;
        }
        const uint32_t before = phys->p3_program_count;
        const bool complete = tdma_pio_spi_phys_p3_load_target_step(phys);
        if (!complete && before == phys->p3_program_count) {
            tdma_pio_spi_phys_p3_write_begin(phys);
            phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_RESOURCE;
            tdma_pio_spi_phys_p3_write_end(phys);
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
        if (mark_phase) {
            tdma_pio_spi_mark_rxphase_origin_program_init(BOARD_TDMA_SPI_PIO, tx_sm,
                s_tdma_pio_spi_mark_rxphase_origin_offset, BOARD_TDMA_SPI_DOWNLINK_CSN_PIN);
        } else if (rxphase) {
            tdma_pio_spi_sck_loopback_origin_program_init(BOARD_TDMA_SPI_PIO, tx_sm,
                s_tdma_pio_spi_rxphase_origin_offset,
                phys->p3_rxphase_direction == 0u ? BOARD_TDMA_SPI_DOWNLINK_SCK_PIN : BOARD_TDMA_SPI_DOWNLINK_TX_PIN);
        } else if (sck) {
            tdma_pio_spi_sck_loopback_origin_program_init(
                BOARD_TDMA_SPI_PIO, tx_sm, s_tdma_pio_spi_sck_loopback_origin_offset,
                BOARD_TDMA_SPI_DOWNLINK_SCK_PIN);
        } else if (marker) {
            tdma_pio_spi_marker_origin_program_init(
                BOARD_TDMA_SPI_PIO, tx_sm,
                s_tdma_pio_spi_p3_initiator_offset,
                BOARD_TDMA_SPI_DOWNLINK_CSN_PIN);
        } else if (phys->p3.role != TDMA_PIO_SPI_P3_ROLE_RESPONDER) {
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
        phys->p3_transition = loopback
            ? TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_RESPONDER
            : TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_CAPTURE;
        return;
    }

    if (phys->p3_transition ==
        TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_RESPONDER) {
        const bool forward_is_cs = phys->p3.signal_group ==
            TDMA_PIO_SPI_P3_GROUP_CS_DATA;
        const uint responder_sm = BOARD_TDMA_SPI_SLAVE_SM;
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, responder_sm, false);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, responder_sm);
        pio_sm_restart(BOARD_TDMA_SPI_PIO, responder_sm);
        if (sck) {
            tdma_pio_spi_sck_loopback_forward_program_init(
                BOARD_TDMA_SPI_PIO, responder_sm, s_tdma_pio_spi_sck_loopback_forward_offset,
                BOARD_TDMA_SPI_UPLINK_SCK_PIN, BOARD_TDMA_SPI_DOWNLINK_TX_PIN);
            tdma_pio_spi_phys_p3_set_drivers(phys->p3.role);
        } else if (marker) {
            tdma_pio_spi_marker_forward_program_init(
                BOARD_TDMA_SPI_PIO, responder_sm,
                s_tdma_pio_spi_p3_responder_offset,
                BOARD_TDMA_SPI_UPLINK_CSN_PIN,
                BOARD_TDMA_SPI_DOWNLINK_TX_PIN,
                TDMA_PIO_SPI_MARKER_FORWARD_DELAY_CYCLES);
            /* Both output latches are high before DE; let the physical RX
             * pads settle through the isolators before the capture starts. */
            tdma_pio_spi_phys_p3_set_drivers(phys->p3.role);
        } else tdma_pio_spi_p3_responder_program_init(
            BOARD_TDMA_SPI_PIO, responder_sm,
            s_tdma_pio_spi_p3_responder_offset,
            forward_is_cs ? BOARD_TDMA_SPI_UPLINK_SCK_PIN
                          : BOARD_TDMA_SPI_UPLINK_CSN_PIN,
            forward_is_cs ? BOARD_TDMA_SPI_UPLINK_CSN_PIN
                          : BOARD_TDMA_SPI_UPLINK_SCK_PIN,
            BOARD_TDMA_SPI_DOWNLINK_TX_PIN, phys->p3.baud_hz);
        phys->p3_transition =
            TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_CAPTURE;
        return;
    }

    if (phys->p3_transition ==
        TDMA_PIO_SPI_P3_TRANSITION_START_CONFIGURE_CAPTURE) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, capture_sm, false);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, capture_sm);
        pio_sm_restart(BOARD_TDMA_SPI_PIO, capture_sm);
        if (rxphase) {
            const uint32_t programmed = mark_phase ?
                tdma_pio_spi_mark_rxphase_sink_program_init(BOARD_TDMA_SPI_PIO, capture_sm,
                    s_tdma_pio_spi_mark_rxphase_sink_offset, BOARD_TDMA_SPI_UPLINK_CSN_PIN,
                    phys->p3_rxphase_destination_cycles) :
                tdma_pio_spi_sck_train_sink_program_init(BOARD_TDMA_SPI_PIO, capture_sm,
                s_tdma_pio_spi_rxphase_sink_offset,
                phys->p3_rxphase_direction == 0u ? BOARD_TDMA_SPI_UPLINK_SCK_PIN : 24u,
                phys->p3_rxphase_destination_cycles);
            tdma_pio_spi_phys_p3_write_begin(phys);
            /* INSTR_MEM is write-only on RP2350. Publish the value written by
             * the initializer; raw phase scans must verify its timing effect. */
            phys->p3_rxphase_instruction = programmed;
            phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_HARDWARE_LATCHED;
            tdma_pio_spi_phys_p3_write_end(phys);
            gpio_put(BOARD_UP_BISS_DE_PIN, !mark_phase && phys->p3_rxphase_direction == 1u);
            gpio_put(BOARD_DN_BISS_DE_PIN, !mark_phase && phys->p3_rxphase_direction == 0u);
            gpio_put(BOARD_TRIG_DE_PIN, mark_phase);
        } else if (phys->p3.role != TDMA_PIO_SPI_P3_ROLE_RESPONDER) {
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
        if (!tdma_pio_spi_phys_ensure_rx_dma() ||
            (marker && !tdma_pio_spi_phys_ensure_tx_dma())) {
            tdma_pio_spi_phys_p3_write_begin(phys);
            phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_DMA;
            tdma_pio_spi_phys_p3_write_end(phys);
            phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
            return;
        }
        if (!marker) memset(s_tdma_pio_spi_cal_ring, 0, sizeof(s_tdma_pio_spi_cal_ring));
        dma_channel_config dc = dma_channel_get_default_config(
            (uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, false);
        channel_config_set_write_increment(&dc, true);
        channel_config_set_dreq(&dc, pio_get_dreq(
            BOARD_TDMA_SPI_PIO, capture_sm, false));
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel, &dc,
            marker ? s_tdma_pio_spi_marker_loopback_capture : s_tdma_pio_spi_cal_ring,
            &BOARD_TDMA_SPI_PIO->rxf[capture_sm],
            phys->p3.requested_words, false);
        if (rxphase) phys->p3_rxphase_capture_configured = true;
        if (marker) {
            dma_channel_config tc = dma_channel_get_default_config(
                (uint)s_tdma_pio_spi_tx_dma_channel);
            channel_config_set_transfer_data_size(&tc, DMA_SIZE_32);
            channel_config_set_read_increment(&tc, true);
            channel_config_set_write_increment(&tc, false);
            channel_config_set_dreq(&tc, pio_get_dreq(BOARD_TDMA_SPI_PIO, tx_sm, true));
            dma_channel_configure((uint)s_tdma_pio_spi_tx_dma_channel, &tc,
                &BOARD_TDMA_SPI_PIO->txf[tx_sm], s_tdma_pio_spi_marker_tx,
                phys->p3_marker_tx_words, true);
        }
        phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_START_ARM;
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_START_ARM) {
        pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 1u);
        if (marker) {
            if (pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO, tx_sm)) {
                if (time_us_64() >= phys->p3_marker_deadline_us) {
                    tdma_pio_spi_phys_p3_write_begin(phys);
                    phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_TIMEOUT;
                    tdma_pio_spi_phys_p3_write_end(phys);
                    phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_STOP_FREEZE;
                }
                return;
            }
            BOARD_TDMA_SPI_PIO->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + tx_sm);
        } else pio_sm_put(BOARD_TDMA_SPI_PIO, tx_sm, phys->p3.pulse_count - 1u);
        if (loopback && !marker) {
            pio_sm_put(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM,
                       phys->p3.pulse_count - 1u);
        }
        phys->armed = true;
        phys->tx_sm = tx_sm;
        phys->rx_sm = capture_sm;
        if (rxphase) {
            gpio_put(BOARD_UP_BISS_DE_PIN, !mark_phase && phys->p3_rxphase_direction == 1u);
            gpio_put(BOARD_DN_BISS_DE_PIN, !mark_phase && phys->p3_rxphase_direction == 0u);
            gpio_put(BOARD_TRIG_DE_PIN, mark_phase);
        } else tdma_pio_spi_phys_p3_set_drivers(phys->p3.role);
        dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
        pio_enable_sm_mask_in_sync(
            BOARD_TDMA_SPI_PIO, (1u << tx_sm) | (1u << capture_sm) |
                (loopback ? (1u << BOARD_TDMA_SPI_SLAVE_SM) : 0u));
        tdma_pio_spi_phys_p3_write_begin(phys);
        phys->p3.state = TDMA_PIO_SPI_P3_ARMED;
        tdma_pio_spi_phys_p3_write_end(phys);
        phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_IDLE;
        return;
    }

    if (phys->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_DECODE) {
        const bool complete = tdma_pio_spi_phys_p3_decode_step(phys);
        if (complete) {
            if (loopback && phys->p3_decode_endpoint ==
                    TDMA_PIO_SPI_P3_ROLE_INITIATOR) {
                phys->p3_decode_endpoint = TDMA_PIO_SPI_P3_ROLE_RESPONDER;
                tdma_pio_spi_phys_p3_decode_reset(phys);
                return;
            }
            const uint32_t expected = loopback ? 0x0fu : phys->p3.role ==
                TDMA_PIO_SPI_P3_ROLE_INITIATOR ? 0x09u : 0x06u;
            tdma_pio_spi_phys_p3_write_begin(phys);
            if (loopback) {
                phys->p3.edge_mask |= phys->p3_responder.edge_mask;
                phys->p3.t2_clk_rx = phys->p3_responder.t2_clk_rx;
                phys->p3.t3_data_tx = phys->p3_responder.t3_data_tx;
            }
            phys->p3.produced_words = phys->p3.requested_words;
            phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_DMA_COMPLETE |
                              TDMA_PIO_SPI_P3_FLAG_HARDWARE_LATCHED;
            phys->p3_publish_state =
                (phys->p3.edge_mask & expected) == expected
                    ? TDMA_PIO_SPI_P3_COMPLETE
                    : TDMA_PIO_SPI_P3_ERROR;
            if (phys->p3_publish_state == TDMA_PIO_SPI_P3_ERROR) {
                phys->p3.reject_reason =
                    TDMA_PIO_SPI_P3_REJECT_EDGE_MISSING;
            }
            if (marker && !(phys->p3.flags & TDMA_PIO_SPI_P3_FLAG_PREEDGE_HIGH)) {
                phys->p3_publish_state = TDMA_PIO_SPI_P3_ERROR;
                phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_PREEDGE;
            } else if (marker && phys->p3_publish_state == TDMA_PIO_SPI_P3_COMPLETE &&
                !(phys->p3.t1_clk_tx < phys->p3.t2_clk_rx &&
                  phys->p3.t2_clk_rx <= phys->p3.t3_data_tx &&
                  phys->p3.t3_data_tx < phys->p3.t4_data_rx)) {
                phys->p3_publish_state = TDMA_PIO_SPI_P3_ERROR;
                phys->p3.reject_reason = TDMA_PIO_SPI_P3_REJECT_EDGE_ORDER;
            }
            if (!marker || phys->p3_publish_state == TDMA_PIO_SPI_P3_COMPLETE)
                phys->p3.flags |= TDMA_PIO_SPI_P3_FLAG_SYNC_MATCH;
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
        if (loopback) {
            phys->p3_responder.state = phys->p3.state;
            phys->p3_responder.reject_reason = phys->p3.reject_reason;
            phys->p3_responder.produced_words = phys->p3.produced_words;
            phys->p3_responder.flags = phys->p3.flags;
        }
        if (phys->p3.state == TDMA_PIO_SPI_P3_IDLE) {
            memset(&phys->p3, 0, sizeof(phys->p3));
            memset(&phys->p3_responder, 0, sizeof(phys->p3_responder));
        }
        tdma_pio_spi_phys_p3_write_end(phys);
        phys->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_IDLE;
    }
}

bool tdma_pio_spi_phys_get_p3_endpoint_snapshot(
    const tdma_pio_spi_phys_t *phys, uint32_t endpoint,
    tdma_pio_spi_p3_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL ||
        (endpoint != TDMA_PIO_SPI_P3_ROLE_INITIATOR &&
         endpoint != TDMA_PIO_SPI_P3_ROLE_RESPONDER)) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        const uint32_t role = phys->p3.role;
        *snapshot = endpoint == TDMA_PIO_SPI_P3_ROLE_RESPONDER
            ? phys->p3_responder : phys->p3;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t end =
            __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if (begin != end || (end & 1u) != 0u) continue;
        if (role != TDMA_PIO_SPI_P3_ROLE_LOOPBACK) return false;
        snapshot->role = endpoint;
        if (endpoint == TDMA_PIO_SPI_P3_ROLE_INITIATOR) {
            snapshot->edge_mask &= 0x09u;
            snapshot->t2_clk_rx = 0u;
            snapshot->t3_data_tx = 0u;
        }
        return true;
    }
    return false;
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
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t end =
            __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

static bool tdma_pio_spi_phys_get_codeword_loopback_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_marker_loopback_snapshot_t *snapshot, uint32_t role)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0; attempt < 64u; ++attempt) {
        const uint32_t begin = __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if (begin & 1u) continue;
        snapshot->raw = phys->p3;
        snapshot->codebook = phys->p3_marker_codebook;
        snapshot->marker_samples = phys->p3_marker_samples;
        snapshot->tx_words = phys->p3_marker_tx_words;
        snapshot->tx_dma_remaining = phys->p3_marker_tx_remaining;
        snapshot->capture_generation = phys->p3_marker_generation;
        snapshot->sck_launch_offset_samples = phys->p3_sck_launch_offset_samples;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (begin != __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE)) continue;
        if (snapshot->raw.state == TDMA_PIO_SPI_P3_IDLE) {
            memset(snapshot, 0, sizeof(*snapshot));
            return true;
        }
        return snapshot->raw.role == role;
    }
    return false;
}

static bool tdma_pio_spi_phys_copy_codeword_loopback_capture(
    const tdma_pio_spi_phys_t *phys, uint32_t epoch, uint32_t offset,
    uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total, uint32_t role)
{
    if (phys == NULL || words == NULL || total == NULL || count == 0u || count > 32u)
        return false;
    const uint32_t begin = __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
    if (begin & 1u) return false;
    const tdma_pio_spi_p3_snapshot_t raw = phys->p3;
    if (raw.role != role || raw.epoch != epoch ||
        (role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK && generation == 0u) ||
        (generation != 0u && generation != phys->p3_marker_generation) ||
        (generation == 0u && raw.produced_words > TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS) ||
        (raw.state != TDMA_PIO_SPI_P3_COMPLETE && raw.state != TDMA_PIO_SPI_P3_ERROR) ||
        !(raw.flags & TDMA_PIO_SPI_P3_FLAG_DMA_COMPLETE) ||
        raw.produced_words > TDMA_PIO_SPI_MARKER_LOOPBACK_CAPTURE_MAX_WORDS ||
        offset > raw.produced_words || count > raw.produced_words - offset) return false;
    memcpy(words, s_tdma_pio_spi_marker_loopback_capture + offset, count * sizeof(*words));
    *total = raw.produced_words;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return begin == __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
}

bool tdma_pio_spi_phys_get_marker_loopback_snapshot(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_marker_loopback_snapshot_t *snapshot)
{
    return tdma_pio_spi_phys_get_codeword_loopback_snapshot(phys, snapshot, TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK);
}

bool tdma_pio_spi_phys_get_sck_loopback_snapshot(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_marker_loopback_snapshot_t *snapshot)
{
    return tdma_pio_spi_phys_get_codeword_loopback_snapshot(phys, snapshot, TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK);
}

bool tdma_pio_spi_phys_copy_marker_loopback_capture(
    const tdma_pio_spi_phys_t *phys, uint32_t epoch, uint32_t offset,
    uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total)
{
    return tdma_pio_spi_phys_copy_codeword_loopback_capture(phys, epoch, offset, count,
        generation, words, total, TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK);
}

bool tdma_pio_spi_phys_copy_sck_loopback_capture(
    const tdma_pio_spi_phys_t *phys, uint32_t epoch, uint32_t offset,
    uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total)
{
    return tdma_pio_spi_phys_copy_codeword_loopback_capture(phys, epoch, offset, count,
        generation, words, total, TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK);
}

static bool tdma_pio_spi_phys_get_rxphase_snapshot_for_role(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_rxphase_snapshot_t *snapshot, uint32_t role)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0; attempt < 64u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
        if (guard & 1u) continue;
        const tdma_pio_spi_p3_snapshot_t raw = phys->p3;
        uint32_t *v = snapshot->fields;
        memset(snapshot, 0, sizeof(*snapshot));
        /* v2 fields 17/18 describe the programmed value, never readback. */
        v[0] = 2u; v[1] = raw.state; v[2] = role == TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE ? 5u : 4u;
        if (raw.state != TDMA_PIO_SPI_P3_IDLE) {
            v[3] = 1u | ((raw.flags & TDMA_PIO_SPI_P3_FLAG_HARDWARE_LATCHED) ? 2u : 0u) |
                ((raw.flags & TDMA_PIO_SPI_P3_FLAG_DMA_COMPLETE) ? 4u : 0u) |
                ((raw.flags & TDMA_PIO_SPI_P3_FLAG_TX_DRAINED) ? 8u : 0u);
            v[4] = raw.reject_reason; v[5] = raw.epoch; v[6] = phys->p3_marker_generation;
            v[7] = phys->p3_marker_codebook; v[8] = phys->p3_rxphase_direction; v[9] = 4u;
            v[10] = phys->p3_rxphase_base_ns;
            v[11] = (uint32_t)(((uint64_t)v[10] + 2u) / 4u); v[12] = v[11] * 4u;
            v[13] = (uint32_t)phys->p3_sck_launch_offset_samples;
            v[14] = phys->p3_rxphase_destination_cycles - 1u; v[15] = 1u;
            v[16] = phys->p3_rxphase_destination_cycles; v[17] = phys->p3_rxphase_instruction;
            v[18] = v[17] == 0u ? 0u : ((v[17] >> 8u) & 31u) + 1u;
            v[19] = v[8] == 0u ? 25u : 29u; v[20] = v[8] == 0u ? 28u : 24u;
            if (role == TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE) { v[19] = 26u; v[20] = 27u; }
            v[21] = phys->p3_marker_samples; v[22] = phys->p3_marker_tx_words;
            v[23] = phys->p3_marker_tx_remaining; v[24] = (raw.flags & TDMA_PIO_SPI_P3_FLAG_TX_DRAINED) != 0u;
            v[25] = raw.requested_words; v[26] = raw.produced_words; v[27] = phys->p3_rxphase_remaining;
            v[28] = TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_LEAD_SAMPLES;
            v[29] = TDMA_PIO_SPI_SCK_LOOPBACK_ORIGIN_HIGH_SAMPLES;
            v[30] = TDMA_PIO_SPI_SCK_LOOPBACK_LAUNCH_GUARD_SAMPLES; v[31] = phys->p3_rxphase_fault;
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (guard != __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE)) continue;
        return raw.state == TDMA_PIO_SPI_P3_IDLE || raw.role == role;
    }
    return false;
}

bool tdma_pio_spi_phys_get_rxphase_snapshot(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_rxphase_snapshot_t *snapshot)
{
    return tdma_pio_spi_phys_get_rxphase_snapshot_for_role(phys, snapshot, TDMA_PIO_SPI_P3_ROLE_SCK_RXPHASE);
}

bool tdma_pio_spi_phys_get_mark_rxphase_snapshot(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_rxphase_snapshot_t *snapshot)
{
    return tdma_pio_spi_phys_get_rxphase_snapshot_for_role(phys, snapshot, TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE);
}

static bool tdma_pio_spi_phys_copy_rxphase_capture_for_role(
    const tdma_pio_spi_phys_t *phys, uint32_t epoch, uint32_t offset,
    uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total, uint32_t role)
{
    if (phys == NULL || words == NULL || total == NULL || generation == 0u || count == 0u || count > 32u)
        return false;
    const uint32_t guard = __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    const tdma_pio_spi_p3_snapshot_t raw = phys->p3;
    if (raw.role != role || raw.epoch != epoch ||
        phys->p3_marker_generation != generation ||
        (raw.state != TDMA_PIO_SPI_P3_COMPLETE && raw.state != TDMA_PIO_SPI_P3_ERROR) ||
        raw.produced_words > 256u || offset > raw.produced_words || count > raw.produced_words - offset)
        return false;
    memcpy(words, s_tdma_pio_spi_marker_loopback_capture + offset, count * sizeof(*words));
    *total = raw.produced_words;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return guard == __atomic_load_n(&phys->p3_guard, __ATOMIC_ACQUIRE);
}

bool tdma_pio_spi_phys_copy_rxphase_capture(
    const tdma_pio_spi_phys_t *phys, uint32_t epoch, uint32_t offset,
    uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total)
{
    return tdma_pio_spi_phys_copy_rxphase_capture_for_role(phys, epoch, offset, count, generation,
        words, total, TDMA_PIO_SPI_P3_ROLE_SCK_RXPHASE);
}

bool tdma_pio_spi_phys_copy_mark_rxphase_capture(
    const tdma_pio_spi_phys_t *phys, uint32_t epoch, uint32_t offset,
    uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total)
{
    return tdma_pio_spi_phys_copy_rxphase_capture_for_role(phys, epoch, offset, count, generation,
        words, total, TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE);
}

