/* Included by the physical owner. Only Core1 mutates this state. Core0 sees
 * a versioned event double buffer; no recorder or filesystem lives here. */
#if PROJECT_TDMA_EVENT_OBSERVER
#include "tdma_event_observer.h"
#include "tdma_event_history.h"
#include "tdma_event.pio.h"

#define TDMA_EVENT_RX_SM BOARD_TDMA_TX_RTT_EVIDENCE_SM
#define TDMA_EVENT_TX_SM BOARD_TDMA_TX_CLOCK_LATCH_SM
#define TDMA_EVENT_SEQUENCE_SM BOARD_TDMA_TX_DATA_CAPTURE_SM
#define TDMA_EVENT_SM_MASK ((1u << TDMA_EVENT_RX_SM) | (1u << TDMA_EVENT_TX_SM) | \
                           (1u << TDMA_EVENT_SEQUENCE_SM))
_Static_assert(TDMA_EVENT_RX_SM == 1u && TDMA_EVENT_TX_SM == 2u &&
               TDMA_EVENT_SEQUENCE_SM == 3u && BOARD_TDMA_TX_CONTROL_OUT_SM == 0u,
               "Relative IRQ layout must match the paired observer programs");

static tdma_event_observer_t s_tdma_event_observer;
static tdma_event_history_t s_tdma_event_history;
static tdma_pio_spi_event_snapshot_t s_tdma_event_snapshot;
static tdma_event_batch_t s_tdma_event_batch;
static tdma_event_record_t s_tdma_event_records[TDMA_EVENT_MAX_RECORDS];
static uint32_t s_tdma_event_epoch;
static uint32_t s_tdma_event_hz;
static uint32_t s_tdma_event_period_ns;
static uint64_t s_tdma_event_base_us;
static uint64_t s_tdma_event_last_service_us;
static bool s_tdma_event_waiting;

static bool tdma_pio_spi_phys_event_selected(const tdma_pio_spi_phys_t *phys)
{
    return phys != NULL && phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
}

static uint64_t tdma_event_cycles(uint64_t us, bool upper)
{
    if (us < s_tdma_event_base_us) return UINT64_MAX;
    uint64_t delta = us - s_tdma_event_base_us;
    if (upper && delta == UINT64_MAX) return UINT64_MAX;
    delta += upper ? 1u : 0u;
    if (s_tdma_event_hz == 0u || delta > (UINT64_MAX - 999999u) / s_tdma_event_hz)
        return UINT64_MAX;
    return (delta * s_tdma_event_hz + (upper ? 999999u : 0u)) / 1000000u;
}

static uint32_t tdma_event_faults(tdma_pio_spi_phys_t *phys)
{
    const PIO pio = phys->flight_resources.tx_pio;
    tdma_pio_spi_event_snapshot_t *s = &s_tdma_event_snapshot;
    s->fdebug = pio->fdebug;
    s->sequence_pc = pio_sm_get_pc(pio, TDMA_EVENT_SEQUENCE_SM);
    uint32_t faults = 0u;
    if ((s->fdebug & (TDMA_EVENT_SM_MASK << PIO_FDEBUG_RXSTALL_LSB)) != 0u)
        faults |= TDMA_EVENT_FAULT_STALL;
    if (s->sequence_pc == s_tdma_event_sequence_offset + tdma_event_sequence_offset_bad)
        faults |= TDMA_EVENT_FAULT_SEQUENCE;
    if ((pio->ctrl & TDMA_EVENT_SM_MASK) != TDMA_EVENT_SM_MASK)
        faults |= TDMA_EVENT_FAULT_DISABLED;
    if (clock_get_hz(clk_sys) != s_tdma_event_hz) faults |= TDMA_EVENT_FAULT_CONFIG;
    return faults;
}

static void tdma_event_publish_state(tdma_pio_spi_phys_t *phys)
{
    const tdma_event_observer_t *o = &s_tdma_event_observer;
    tdma_pio_spi_event_snapshot_t *s = &s_tdma_event_snapshot;
    s->state = o->state;
    s->reason = o->reason;
    s->epoch = o->epoch;
    s->waiting = s_tdma_event_waiting;
    s->joined = o->joined > UINT32_MAX ? UINT32_MAX : (uint32_t)o->joined;
    s->fault_bits = o->fault_bits;
    s->pending_rx = o->pending_count[0];
    s->pending_tx = o->pending_count[1];
    s->pending_sequence = o->pending_count[2];
    const uint32_t guard = __atomic_load_n(&phys->flight_event_guard, __ATOMIC_RELAXED);
    const uint32_t next = guard + 2u;
    tdma_pio_spi_event_snapshot_t *target = ((next >> 1u) & 1u) != 0u
        ? &phys->flight_event_alternate : &phys->snapshot.event;
    /* The published slot stays readable throughout the writer's copy. A
     * single release changes both generation and slot; the reader must still
     * revalidate in case two publications reuse its slot while it is copying. */
    __dmb(); /* Publish the prior slot switch before reusing an older slot. */
    *target = *s;
    __atomic_store_n(&phys->flight_event_guard, next, __ATOMIC_RELEASE);
}

static bool tdma_pio_spi_phys_event_copy(const tdma_pio_spi_phys_t *phys,
                                        tdma_pio_spi_event_snapshot_t *out)
{
    /* Pin only the bounded local copy, so Core0's RTOS cannot consume the
     * guard's copy-lifetime allowance while another task runs. Core1 keeps
     * publishing independently; no cross-core lock or wait is introduced. */
    const uint32_t interrupts = save_and_disable_interrupts();
    const uint64_t before = time_us_64();
    bool copied = false;
    for (uint attempt = 0u; attempt < 3u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&phys->flight_event_guard, __ATOMIC_ACQUIRE);
        if ((guard & 1u) != 0u) continue;
        *out = ((guard >> 1u) & 1u) != 0u ? phys->flight_event_alternate : phys->snapshot.event;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t after_guard = __atomic_load_n(&phys->flight_event_guard, __ATOMIC_ACQUIRE);
        const uint64_t after = time_us_64();
        if (after < before || after - before > 1000u) break;
        if (guard == after_guard) {
            copied = true;
            break;
        }
    }
    restore_interrupts(interrupts);
    return copied;
}

static void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *phys)
{
    /* Retire the installed persona even if configure has already changed the
     * requested role. Do this before a maintenance owner can reuse the SMs. */
    if (phys == NULL || s_tdma_pio_spi_program_persona !=
        TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER) return;
    tdma_event_observer_stop(&s_tdma_event_observer);
    tdma_event_history_retire(&s_tdma_event_history);
    s_tdma_event_waiting = false;
    const PIO pio = phys->flight_resources.tx_pio;
    pio_set_sm_mask_enabled(pio, TDMA_EVENT_SM_MASK, false);
    for (uint sm = TDMA_EVENT_RX_SM; sm <= TDMA_EVENT_SEQUENCE_SM; ++sm) {
        pio_sm_clear_fifos(pio, sm);
        pio_sm_restart(pio, sm);
    }
    pio_interrupt_clear(pio, TDMA_EVENT_RX_SM);
    pio_interrupt_clear(pio, TDMA_EVENT_TX_SM);
    tdma_event_publish_state(phys);
}

static void tdma_pio_spi_phys_event_prepare(tdma_pio_spi_phys_t *phys,
                                            const tdma_ring_runtime_config_t *config)
{
    if (!tdma_pio_spi_phys_event_selected(phys)) return;
    tdma_event_history_retire(&s_tdma_event_history);
    tdma_event_observer_init(&s_tdma_event_observer);
    memset(&s_tdma_event_snapshot, 0, sizeof(s_tdma_event_snapshot));
    s_tdma_event_hz = clock_get_hz(clk_sys);
    s_tdma_event_period_ns = config->cycle_period_ns;
    s_tdma_event_base_us = time_us_64();
    s_tdma_event_last_service_us = 0u;
    s_tdma_event_waiting = true;
    s_tdma_event_snapshot.pio_hz = s_tdma_event_hz;
    tdma_event_publish_state(phys);
}

static void tdma_event_start(tdma_pio_spi_phys_t *phys)
{
    if (!s_tdma_event_waiting || !phys->flight_overlay_alignment_locked ||
        phys->flight_overlay_alignment_samples <
            TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES) return;
    const PIO pio = phys->flight_resources.tx_pio;
    const uint32_t cs_mask = (1u << phys->rx_csn_pin) | (1u << phys->tx_csn_pin);
    if ((pio->dbg_padout & (1u << phys->tx_csn_pin)) == 0u ||
        (gpio_get_all() & cs_mask) != cs_mask) return;
    /* The process RX sampler already established the physical frame offset.
     * Add the transport's sequence field offset and width, not a guessed
     * per-node header size. This remains diagnostic until wire qualification. */
    const uint64_t bits = ((uint64_t)phys->flight_alignment_byte_shift +
        TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_SEQUENCE_OFFSET +
        sizeof(uint32_t)) * 8u +
        phys->flight_alignment_bit_shift;
    if (bits < 32u || bits > (uint64_t)phys->flight_physical_byte_count * 8u ||
        phys->flight_alignment_bit_shift >= 8u || s_tdma_event_hz == 0u ||
        s_tdma_event_epoch == UINT32_MAX) return;
    pio_set_sm_mask_enabled(pio, TDMA_EVENT_SM_MASK, false);
    for (uint sm = TDMA_EVENT_RX_SM; sm <= TDMA_EVENT_TX_SM; ++sm) {
        pio_sm_config c = tdma_event_counter_program_get_default_config(s_tdma_event_counter_offset);
        sm_config_set_jmp_pin(&c, sm == TDMA_EVENT_RX_SM ? phys->rx_csn_pin : phys->tx_csn_pin);
        sm_config_set_in_shift(&c, false, true, 32u);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
        sm_config_set_clkdiv_int_frac(&c, 1u, 0u);
        pio_sm_init(pio, sm, s_tdma_event_counter_offset, &c);
        pio_sm_exec(pio, sm, pio_encode_mov_not(pio_x, pio_null));
        pio_sm_exec(pio, sm, pio_encode_mov_not(pio_y, pio_null));
    }
    pio->instr_mem[s_tdma_event_sequence_offset + tdma_event_sequence_offset_start] =
        pio_encode_wait_gpio(false, phys->rx_csn_pin);
    pio->instr_mem[s_tdma_event_sequence_offset + tdma_event_sequence_offset_sample] =
        pio_encode_wait_gpio(true, phys->rx_sck_pin) |
        pio_encode_delay(phys->flight_data_phase_delay_cycles);
    pio->instr_mem[s_tdma_event_sequence_offset + tdma_event_sequence_offset_sample_low] =
        pio_encode_wait_gpio(false, phys->rx_sck_pin);
    pio_sm_config c = tdma_event_sequence_program_get_default_config(s_tdma_event_sequence_offset);
    sm_config_set_in_pins(&c, phys->rx_pin);
    sm_config_set_in_shift(&c, false, false, 32u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_mov_status(&c, STATUS_IRQ_SET, TDMA_EVENT_RX_SM);
    sm_config_set_clkdiv_int_frac(&c, 1u, 0u);
    pio_sm_init(pio, TDMA_EVENT_SEQUENCE_SM, s_tdma_event_sequence_offset, &c);
    /* JOIN_RX leaves no TX FIFO. Seed OSR through X without a blocking PULL. */
    pio_sm_exec(pio, TDMA_EVENT_SEQUENCE_SM, pio_encode_set(pio_x, 0u));
    for (int shift = 30; shift >= 0; shift -= 5) {
        const uint count = shift == 30 ? 2u : 5u;
        pio_sm_exec(pio, TDMA_EVENT_SEQUENCE_SM, pio_encode_set(pio_x,
            ((uint32_t)bits - 1u) >> (uint)shift & (shift == 30 ? 3u : 31u)));
        pio_sm_exec(pio, TDMA_EVENT_SEQUENCE_SM, pio_encode_in(pio_x, count));
    }
    pio_sm_exec(pio, TDMA_EVENT_SEQUENCE_SM, pio_encode_mov(pio_osr, pio_isr));
    pio_interrupt_clear(pio, TDMA_EVENT_RX_SM);
    pio_interrupt_clear(pio, TDMA_EVENT_TX_SM);
    pio->fdebug = TDMA_EVENT_SM_MASK << PIO_FDEBUG_RXSTALL_LSB;
    tdma_pio_spi_event_snapshot_t *s = &s_tdma_event_snapshot;
    s->prefix_bits = (uint32_t)bits - 32u;
    s->start_pad_before = gpio_get_all();
    /* Configuration/seed work can outlive the earlier idle observation. Do
     * not enable into a frame already in progress; leave the disabled SMs
     * waiting for a later owner phase without consuming an event epoch.
     * Edges after this check still face the post-enable dirty-start check. */
    if ((s->start_pad_before & cs_mask) != cs_mask) {
        tdma_event_publish_state(phys);
        return;
    }
    const uint64_t before = time_us_64();
    pio_enable_sm_mask_in_sync(pio, TDMA_EVENT_SM_MASK);
    const uint64_t after = time_us_64();
    s->start_pad_after = gpio_get_all();
    uint32_t faults = tdma_event_faults(phys);
    if ((s->start_pad_before & cs_mask) != cs_mask ||
        (s->start_pad_after & cs_mask) != cs_mask)
        faults |= TDMA_EVENT_FAULT_DIRTY_START;
    const tdma_event_config_t config = {
        .pio_hz = s_tdma_event_hz,
        .epoch_limit_cycles = (uint64_t)s_tdma_event_hz * 60u,
        .join_timeout_cycles = (uint64_t)s_tdma_event_hz * s_tdma_event_period_ns / 250000000u,
        .min_frame_cycles = (uint64_t)phys->flight_physical_byte_count * 8u *
                            s_tdma_event_hz / phys->baud_hz,
        .min_tx_delay_cycles = 0u,
        .max_tx_delay_cycles = phys->flight_marker_phase_delay_cycles + 8u,
    };
    const tdma_event_interval_t start = {tdma_event_cycles(before, false), tdma_event_cycles(after, true)};
    ++s_tdma_event_epoch;
    (void)tdma_event_observer_start(&s_tdma_event_observer, &config,
                                   s_tdma_event_epoch, start, faults);
    if (s_tdma_event_observer.state == TDMA_EVENT_ACTIVE) {
        (void)tdma_event_history_start(&s_tdma_event_history,
            s_tdma_event_epoch, s_tdma_event_hz, start);
    } else {
        tdma_event_history_retire(&s_tdma_event_history);
    }
    s->start_width_cycles = start.hi - start.lo;
    s_tdma_event_waiting = false;
    if (s_tdma_event_observer.state != TDMA_EVENT_ACTIVE)
        pio_set_sm_mask_enabled(pio, TDMA_EVENT_SM_MASK, false);
    tdma_event_publish_state(phys);
}

static void tdma_pio_spi_phys_event_service(tdma_pio_spi_phys_t *phys)
{
    if (!tdma_pio_spi_phys_event_selected(phys) || !phys->armed) return;
    const uint64_t entered = time_us_64();
    tdma_pio_spi_event_snapshot_t *s = &s_tdma_event_snapshot;
    if (s_tdma_event_waiting) tdma_event_start(phys);
    if (s_tdma_event_observer.state != TDMA_EVENT_ACTIVE) return;
    if (s_tdma_event_last_service_us != 0u) {
        const uint64_t gap = entered - s_tdma_event_last_service_us;
        if (gap > s->service_gap_max_us) s->service_gap_max_us = gap > UINT32_MAX ? UINT32_MAX : (uint32_t)gap;
    }
    s_tdma_event_last_service_us = entered;
    ++s->service_count;
    const PIO pio = phys->flight_resources.tx_pio;
    tdma_event_batch_t *batch = &s_tdma_event_batch;
    memset(batch, 0, sizeof(*batch));
    batch->epoch = s_tdma_event_observer.epoch;
    batch->pre_faults = tdma_event_faults(phys);
    const uint64_t before = time_us_64();
    const uint sms[] = {TDMA_EVENT_RX_SM, TDMA_EVENT_TX_SM, TDMA_EVENT_SEQUENCE_SM};
    uint32_t *maxima[] = {&s->rx_level_max, &s->tx_level_max, &s->sequence_level_max};
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        uint count = pio_sm_get_rx_fifo_level(pio, sms[stream]);
        if (count > *maxima[stream]) *maxima[stream] = count;
        if (count > TDMA_EVENT_FIFO_WORDS) {
            batch->pre_faults |= TDMA_EVENT_FAULT_OVERFLOW;
            count = 0u;
        }
        batch->count[stream] = (uint8_t)count;
        for (uint i = 0u; i < count; ++i) batch->words[stream][i] = pio_sm_get(pio, sms[stream]);
        if (pio_sm_is_rx_fifo_empty(pio, sms[stream])) batch->empty_mask |= (uint8_t)(1u << stream);
    }
    const uint64_t after = time_us_64();
    batch->observed = (tdma_event_interval_t){tdma_event_cycles(before, false), tdma_event_cycles(after, true)};
    batch->post_faults = tdma_event_faults(phys);
    if (batch->count[0]) s->fifo_rx_first = batch->words[0][0];
    if (batch->count[0] > 1u) s->fifo_rx_second = batch->words[0][1];
    if (batch->count[1]) s->fifo_tx_first = batch->words[1][0];
    if (batch->count[1] > 1u) s->fifo_tx_second = batch->words[1][1];
    if (batch->count[2]) s->fifo_sequence_first = batch->words[2][0];
    const size_t count = tdma_event_observer_feed(&s_tdma_event_observer, batch, s_tdma_event_records);
    /* A sticky fault occurring during CPU validation also retires this epoch
     * before any candidate is copied into the published owner snapshot. */
    const uint32_t final_faults = tdma_event_faults(phys);
    if (s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && final_faults != 0u) {
        memset(batch, 0, sizeof(*batch));
        batch->epoch = s_tdma_event_observer.epoch;
        batch->observed = (tdma_event_interval_t){tdma_event_cycles(after, false), tdma_event_cycles(time_us_64(), true)};
        batch->pre_faults = final_faults;
        (void)tdma_event_observer_feed(&s_tdma_event_observer, batch, s_tdma_event_records);
    }
    if (s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && count != 0u) {
        /* Retain the whole validated batch, after the final sticky-fault
         * check. Sequence lookup remains diagnostic; no packet identity or
         * physical-clock qualification follows from retaining these words. */
        (void)tdma_event_history_append(&s_tdma_event_history, s_tdma_event_records,
            count, s_tdma_event_observer.joined);
        const tdma_event_record_t *last = &s_tdma_event_records[count - 1u];
        s->published = count > UINT32_MAX - s->published ? UINT32_MAX :
                       s->published + (uint32_t)count;
        s->sequence = last->sequence;
        s->ordinal = last->ordinal;
        s->rx_elapsed_cycles = last->rx_elapsed_cycles;
        s->tx_elapsed_cycles = last->tx_elapsed_cycles;
    }
    if (s_tdma_event_observer.state == TDMA_EVENT_INVALID) {
        tdma_event_history_retire(&s_tdma_event_history);
        pio_set_sm_mask_enabled(pio, TDMA_EVENT_SM_MASK, false);
    }
    const uint64_t duration = time_us_64() - entered;
    if (duration > s->service_max_us) s->service_max_us = duration > UINT32_MAX ? UINT32_MAX : (uint32_t)duration;
    tdma_event_publish_state(phys);
}
#else
static bool tdma_pio_spi_phys_event_copy(const tdma_pio_spi_phys_t *phys,
                                        tdma_pio_spi_event_snapshot_t *out)
{ (void)phys; memset(out, 0, sizeof(*out)); return true; }
static bool tdma_pio_spi_phys_event_selected(const tdma_pio_spi_phys_t *phys)
{ (void)phys; return false; }
static void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *phys) { (void)phys; }
static void tdma_pio_spi_phys_event_prepare(tdma_pio_spi_phys_t *phys,
                                            const tdma_ring_runtime_config_t *config)
{ (void)phys; (void)config; }
static void tdma_pio_spi_phys_event_service(tdma_pio_spi_phys_t *phys) { (void)phys; }
#endif
