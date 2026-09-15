/* Included by the physical owner. Only Core1 mutates this state. Core0 sees
 * a versioned event double buffer; no recorder or filesystem lives here. */
#include "tdma_rx_start_cut.h"
static tdma_pio_spi_event_snapshot_t s_tdma_event_snapshot;
static bool s_tdma_event_candidate_dirty;

static void tdma_event_publish_snapshot(tdma_pio_spi_phys_t *phys)
{
    const uint32_t guard = __atomic_load_n(&phys->flight_event_guard, __ATOMIC_RELAXED);
    const uint32_t next = guard + 2u;
    tdma_pio_spi_event_snapshot_t *target = ((next >> 1u) & 1u) != 0u
        ? &phys->flight_event_alternate : &phys->snapshot.event;
    /* The published slot stays readable throughout the writer's copy. A
     * single release changes both generation and slot; the reader must still
     * revalidate in case two publications reuse its slot while it is copying. */
    __dmb(); /* Publish the prior slot switch before reusing an older slot. */
    *target = s_tdma_event_snapshot;
    __atomic_store_n(&phys->flight_event_guard, next, __ATOMIC_RELEASE);
    s_tdma_event_candidate_dirty = false;
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

static void tdma_event_candidate_retire(void)
{
    /* Retain the completed query as history, never as available evidence. */
    if (s_tdma_event_snapshot.candidate.query_count != 0u)
        s_tdma_event_snapshot.candidate.flags |= TDMA_RX_EVENT_RETIRED;
}

static void tdma_event_candidate_increment(uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}

static void tdma_event_candidate_begin(const tdma_rx_capture_t *capture,
    uint32_t pinned_epoch, uint32_t sequence, uint64_t station_age_ns)
{
    tdma_rx_event_candidate_snapshot_t *s = &s_tdma_event_snapshot.candidate;
    tdma_event_candidate_increment(&s->query_count);
    memset(&s->capture_id, 0,
        sizeof(*s) - offsetof(tdma_rx_event_candidate_snapshot_t, capture_id));
    if (capture != NULL) {
        s->capture_id = capture->capture_id;
        s->arm_epoch = capture->arm_epoch;
        s->observation_epoch = capture->observation_epoch;
        s->dma_candidate = capture->candidate;
    }
    s->capture_observer_epoch = pinned_epoch;
    s->packet_sequence = sequence;
    s->station_age_ns = station_age_ns;
    s->event_ordinal = s->history_oldest_ordinal = UINT32_MAX;
    /* HEADER_VALIDATED describes the READY caller's checked packet header;
     * it does not validate mutable mailbox contents or this association. */
    s->flags = TDMA_RX_EVENT_DIAGNOSTIC_ONLY | TDMA_RX_EVENT_HISTORICAL |
        TDMA_RX_EVENT_HEADER_VALIDATED | TDMA_RX_EVENT_MAILBOX_UNKNOWN |
        TDMA_RX_EVENT_IDENTITY_UNPROVED | TDMA_RX_EVENT_PHYSICAL_FIRST_UNPROVED;
}

static void tdma_event_candidate_finish(tdma_rx_event_reason_t reason)
{
    tdma_rx_event_candidate_snapshot_t *s = &s_tdma_event_snapshot.candidate;
    s->reason = (uint32_t)reason;
    switch (reason) {
    case TDMA_RX_EVENT_MATCHED:
        tdma_event_candidate_increment(&s->matched_count);
        s->flags |= TDMA_RX_EVENT_MATCH_PRESENT;
        break;
    case TDMA_RX_EVENT_ARM_STALE:
    case TDMA_RX_EVENT_OBSERVATION_STALE:
    case TDMA_RX_EVENT_PIN_STALE:
    case TDMA_RX_EVENT_CAPTURE_STALE:
        tdma_event_candidate_increment(&s->stale_count);
        break;
    case TDMA_RX_EVENT_NOT_FOUND:
        tdma_event_candidate_increment(&s->missing_count);
        break;
    case TDMA_RX_EVENT_AMBIGUOUS:
        tdma_event_candidate_increment(&s->ambiguous_count);
        break;
    default:
        tdma_event_candidate_increment(&s->unavailable_count);
        break;
    }
    s_tdma_event_candidate_dirty = true;
}

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
static tdma_event_batch_t s_tdma_event_batch;
static tdma_event_record_t s_tdma_event_records[TDMA_EVENT_MAX_RECORDS];
static uint32_t s_tdma_event_epoch;
static uint32_t s_tdma_event_hz;
static uint32_t s_tdma_event_period_ns;
static uint64_t s_tdma_event_base_us;
static uint64_t s_tdma_event_last_service_us;
static uint64_t s_tdma_event_arm_epoch;
static bool s_tdma_event_waiting;
/* Separate hypothesis: never install frozen DMA offsets as live alignment. */
static tdma_frozen_geometry_snapshot_t s_tdma_event_geometry;
static bool s_tdma_event_prelaunch;
static uint32_t s_tdma_event_prelaunch_reason;
_Static_assert(sizeof(s_tdma_event_geometry) + sizeof(s_tdma_event_prelaunch) +
    sizeof(s_tdma_event_prelaunch_reason) <= 192u, "prelaunch storage is bounded");

static void tdma_event_prelaunch_mark(uint32_t state, uint32_t reason)
{
    if (!s_tdma_event_prelaunch) return;
    tdma_geometry_observer_record(s_tdma_event_geometry.generation,
        s_tdma_event_observer.epoch, state, reason, s_tdma_event_snapshot.prefix_bits);
}

static uint32_t tdma_event_prelaunch_geometry(const tdma_pio_spi_phys_t *phys)
{
    if (clock_get_hz(clk_sys) != s_tdma_event_geometry.clk_sys_hz)
        return TDMA_GEOMETRY_OBSERVER_CLOCK;
    if (s_tdma_pio_spi_rx_sequence.observation_epoch !=
        s_tdma_event_geometry.bound_observation_epoch)
        return TDMA_GEOMETRY_OBSERVER_OBSERVATION;
    if (!tdma_geometry_observer_current(phys, &s_tdma_event_geometry))
        return TDMA_GEOMETRY_OBSERVER_BINDING;
    /* Training is still allowed to govern the live overlay. It may disprove
     * this observer hypothesis, but cannot silently move its sequence tap. */
    if ((phys->flight_overlay_alignment_locked || phys->flight_overlay_alignment_samples >=
            TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES) &&
        (phys->flight_alignment_byte_shift != s_tdma_event_geometry.dma_byte_shift ||
         phys->flight_alignment_bit_shift != s_tdma_event_geometry.dma_bit_shift))
        return TDMA_GEOMETRY_OBSERVER_GEOMETRY;
    return TDMA_GEOMETRY_OBSERVER_OK;
}

/* RX_START_CUT_STORAGE_BEGIN: independent of the general event snapshot. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TDMA_START_CUT_RAM __not_in_flash("tdma_start_cut") __attribute__((noinline, noclone))
#else
#define TDMA_START_CUT_RAM
#endif

static tdma_rx_start_cut_t s_tdma_rx_start_cut;
static tdma_rx_start_cut_t s_tdma_rx_start_cut_slots[2];
static uint32_t s_tdma_rx_start_cut_guard;
static uint32_t s_tdma_rx_start_cut_data_phase;
static uint32_t s_tdma_rx_start_cut_marker_phase;
_Static_assert(sizeof(s_tdma_rx_start_cut) + sizeof(s_tdma_rx_start_cut_slots) +
    sizeof(s_tdma_rx_start_cut_guard) + sizeof(s_tdma_rx_start_cut_data_phase) +
    sizeof(s_tdma_rx_start_cut_marker_phase) <= 512u,
    "Independent start-cut owner and publication storage must remain bounded");

#define TDMA_RX_START_CUT_DMA_ERRORS (DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS | \
    DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)
#define TDMA_RX_START_CUT_DMA_CONFIG_MASK (DMA_CH0_CTRL_TRIG_BITS & \
    ~(TDMA_RX_START_CUT_DMA_ERRORS | DMA_CH0_CTRL_TRIG_BUSY_BITS))

static void tdma_rx_start_cut_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_tdma_rx_start_cut_guard, __ATOMIC_RELAXED);
    const uint32_t next = guard + 2u;
    /* Order the previous slot switch before reusing its older storage. */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_tdma_rx_start_cut_slots[(next >> 1u) & 1u] = s_tdma_rx_start_cut;
    __atomic_store_n(&s_tdma_rx_start_cut_guard, next, __ATOMIC_RELEASE);
}

static void tdma_rx_start_cut_arm_begin(void)
{
    memset(&s_tdma_rx_start_cut, 0, sizeof(s_tdma_rx_start_cut));
    s_tdma_rx_start_cut.schema = TDMA_RX_START_CUT_SCHEMA;
    tdma_rx_start_cut_publish();
}

static void tdma_rx_start_cut_retire(uint32_t reasons)
{
    if ((s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RECORDED) == 0u || reasons == 0u)
        return;
    const uint32_t before = s_tdma_rx_start_cut.retire_reasons;
    s_tdma_rx_start_cut.retire_reasons |= reasons;
    s_tdma_rx_start_cut.flags |= TDMA_RX_START_CUT_RETIRED;
    if (before != s_tdma_rx_start_cut.retire_reasons) tdma_rx_start_cut_publish();
}

/* Called only by successful physical disarm, after cancellation ACK. Event
 * STOP/INVALID alone never makes this record available to Core0. */
static void tdma_rx_start_cut_disarmed(void)
{
    if ((s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RECORDED) == 0u) return;
    tdma_rx_start_cut_retire(TDMA_RX_START_CUT_STOP);
    s_tdma_rx_start_cut.flags |= TDMA_RX_START_CUT_STOPPED;
    tdma_rx_start_cut_publish();
}

bool tdma_pio_spi_phys_get_rx_start_cut(tdma_rx_start_cut_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    const uint32_t interrupts = save_and_disable_interrupts();
    const uint64_t before = time_us_64();
    bool copied = false;
    for (uint attempt = 0u; attempt < 3u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&s_tdma_rx_start_cut_guard, __ATOMIC_ACQUIRE);
        if ((guard & 1u) != 0u) continue;
        *out = s_tdma_rx_start_cut_slots[(guard >> 1u) & 1u];
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t after_guard = __atomic_load_n(&s_tdma_rx_start_cut_guard, __ATOMIC_ACQUIRE);
        const uint64_t after = time_us_64();
        if (after < before || after - before > 1000u) break;
        if (guard == after_guard) {
            copied = (out->flags & (TDMA_RX_START_CUT_RECORDED | TDMA_RX_START_CUT_STOPPED)) ==
                (TDMA_RX_START_CUT_RECORDED | TDMA_RX_START_CUT_STOPPED);
            break;
        }
    }
    restore_interrupts(interrupts);
    if (!copied) memset(out, 0, sizeof(*out));
    return copied;
}

static bool tdma_rx_start_cut_dma_available(void)
{
    return s_tdma_pio_spi_rx_dma_channel >= 0 &&
        (uint)s_tdma_pio_spi_rx_dma_channel < NUM_DMA_CHANNELS;
}

/* Preparation can be abandoned by the final CS gate. RECORDED is assigned
 * only after the real enable; no failed attempt is exported as an event. */
static void tdma_rx_start_cut_before(tdma_pio_spi_phys_t *phys)
{
    tdma_rx_start_cut_t *cut = &s_tdma_rx_start_cut;
    memset(cut, 0, sizeof(*cut));
    cut->schema = TDMA_RX_START_CUT_SCHEMA;
    cut->arm_epoch = s_tdma_pio_spi_rx_arm_epoch;
    cut->observer_epoch = s_tdma_event_epoch + 1u;
    cut->pio_hz = s_tdma_event_hz;
    cut->physical_frame_words = phys->flight_physical_byte_count;
    cut->alignment_byte_shift = s_tdma_event_prelaunch
        ? s_tdma_event_geometry.dma_byte_shift : phys->flight_alignment_byte_shift;
    cut->alignment_bit_shift = s_tdma_event_prelaunch
        ? s_tdma_event_geometry.dma_bit_shift : phys->flight_alignment_bit_shift;
    cut->dma_channel = tdma_rx_start_cut_dma_available()
        ? (uint32_t)s_tdma_pio_spi_rx_dma_channel : UINT32_MAX;
    s_tdma_rx_start_cut_data_phase = phys->flight_data_phase_delay_cycles;
    s_tdma_rx_start_cut_marker_phase = phys->flight_marker_phase_delay_cycles;
    cut->sample_before_ticks = vdc_timestamp_clock_read_ticks64();
    if (tdma_rx_start_cut_dma_available()) {
        cut->dma_count_before = dma_hw->ch[cut->dma_channel].transfer_count;
        cut->dma_ctrl_before = dma_hw->ch[cut->dma_channel].ctrl_trig;
    }
    const PIO capture_pio = tdma_pio_spi_phys_capture_pio(phys);
    const uint capture_sm = tdma_pio_spi_phys_capture_sm(phys);
    cut->capture_fdebug_before = capture_pio->fdebug;
    cut->capture_fifo_before = (uint8_t)pio_sm_get_rx_fifo_level(capture_pio, capture_sm);
    cut->capture_pc_before = (uint8_t)pio_sm_get_pc(capture_pio, capture_sm);
    __atomic_thread_fence(__ATOMIC_SEQ_CST); /* DMB: completed MMIO before enable. */
}

static uint32_t tdma_rx_start_cut_lift(tdma_rx_start_cut_t *cut,
                                      tdma_rx_dma_counter_t *counter)
{
    const uint64_t original_epoch = counter->observation_epoch;
    const uint32_t mode = DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB;
    const uint32_t mask = DMA_CH0_TRANS_COUNT_COUNT_BITS;
    if (cut->physical_frame_words == 0u ||
        cut->physical_frame_words > TDMA_RX_DMA_COUNT_MAX / TDMA_RX_DMA_RELOAD_FRAMES ||
        counter->reload_words != cut->physical_frame_words * TDMA_RX_DMA_RELOAD_FRAMES)
        return TDMA_RX_START_CUT_DMA_CONFIG;
    if ((cut->dma_count_before & DMA_CH0_TRANS_COUNT_MODE_BITS) != mode ||
        (cut->dma_count_after & DMA_CH0_TRANS_COUNT_MODE_BITS) != mode)
        return TDMA_RX_START_CUT_DMA_CONFIG;
    bool lost_before = false, lost_after = false;
    /* Both count samples have this conservative whole-enable bracket. The
     * local copy alone advances; RX scanning/drop accounting is untouched. */
    if (!tdma_rx_dma_counter_observe(counter, cut->dma_count_before & mask,
            cut->sample_before_ticks, cut->sample_after_ticks,
            &cut->produced_before, &lost_before)) return TDMA_RX_START_CUT_COUNTER;
    cut->observation_epoch_before = counter->observation_epoch;
    if (!tdma_rx_dma_counter_observe(counter, cut->dma_count_after & mask,
            cut->sample_before_ticks, cut->sample_after_ticks,
            &cut->produced_after, &lost_after)) return TDMA_RX_START_CUT_COUNTER;
    cut->observation_epoch_after = counter->observation_epoch;
    if ((cut->dma_count_after & mask) > (cut->dma_count_before & mask))
        cut->flags |= TDMA_RX_START_CUT_COUNT_WRAP;
    return lost_before || lost_after || original_epoch != counter->observation_epoch
        ? TDMA_RX_START_CUT_OBSERVATION_EPOCH : 0u;
}

static void tdma_rx_start_cut_after(tdma_pio_spi_phys_t *phys,
                                   tdma_rx_dma_counter_t *counter,
                                   uint32_t pads_before, uint32_t pads_after)
{
    tdma_rx_start_cut_t *cut = &s_tdma_rx_start_cut;
    cut->pads_before = pads_before;
    cut->pads_after = pads_after;
    if (tdma_rx_start_cut_dma_available()) {
        cut->dma_count_after = dma_hw->ch[cut->dma_channel].transfer_count;
        cut->dma_ctrl_after = dma_hw->ch[cut->dma_channel].ctrl_trig;
    }
    const PIO capture_pio = tdma_pio_spi_phys_capture_pio(phys);
    const uint capture_sm = tdma_pio_spi_phys_capture_sm(phys);
    cut->capture_fdebug_after = capture_pio->fdebug;
    cut->capture_fifo_after = (uint8_t)pio_sm_get_rx_fifo_level(capture_pio, capture_sm);
    cut->capture_pc_after = (uint8_t)pio_sm_get_pc(capture_pio, capture_sm);
    __atomic_thread_fence(__ATOMIC_SEQ_CST); /* DMB before the closing tick read. */
    cut->sample_after_ticks = vdc_timestamp_clock_read_ticks64();
    cut->flags = TDMA_RX_START_CUT_DIAGNOSTIC | TDMA_RX_START_CUT_RECORDED |
        TDMA_RX_START_CUT_UNRESOLVED | TDMA_RX_START_CUT_INFLIGHT_UNKNOWN |
        TDMA_RX_START_CUT_PRESTART_BACKLOG_UNEXCLUDED;
    cut->observation_epoch_before = cut->observation_epoch_after = counter->observation_epoch;
    uint32_t reasons = tdma_rx_start_cut_dma_available()
        ? tdma_rx_start_cut_lift(cut, counter) : TDMA_RX_START_CUT_DMA_CONFIG;
    if (((cut->capture_fdebug_before | cut->capture_fdebug_after) &
            (1u << (capture_sm + PIO_FDEBUG_RXSTALL_LSB))) != 0u)
        reasons |= TDMA_RX_START_CUT_CAPTURE_RXSTALL;
    if (((cut->dma_ctrl_before | cut->dma_ctrl_after) & TDMA_RX_START_CUT_DMA_ERRORS) != 0u)
        reasons |= TDMA_RX_START_CUT_DMA_ERROR;
    if ((cut->dma_ctrl_before & DMA_CH0_CTRL_TRIG_EN_BITS) == 0u ||
        (cut->dma_ctrl_before & TDMA_RX_START_CUT_DMA_CONFIG_MASK) !=
            (cut->dma_ctrl_after & TDMA_RX_START_CUT_DMA_CONFIG_MASK))
        reasons |= TDMA_RX_START_CUT_DMA_CONFIG;
    if (!s_tdma_pio_spi_rx_arm_valid) reasons |= TDMA_RX_START_CUT_ARM;
    const uint32_t cs_mask = (1u << phys->rx_csn_pin) | (1u << phys->tx_csn_pin);
    if ((pads_before & cs_mask) != cs_mask || (pads_after & cs_mask) != cs_mask)
        reasons |= TDMA_RX_START_CUT_DIRTY_START;
    cut->retire_reasons = reasons;
    if (reasons != 0u) cut->flags |= TDMA_RX_START_CUT_RETIRED;
    tdma_rx_start_cut_publish();
}

static void TDMA_START_CUT_RAM tdma_rx_start_cut_monitor(tdma_pio_spi_phys_t *phys)
{
    const tdma_rx_start_cut_t *cut = &s_tdma_rx_start_cut;
    if ((cut->flags & TDMA_RX_START_CUT_RECORDED) == 0u ||
        (cut->flags & TDMA_RX_START_CUT_STOPPED) != 0u ||
        (cut->retire_reasons & TDMA_RX_START_CUT_STOP) != 0u) return;
    if (phys->role != TDMA_PIO_SPI_ROLE_SLAVE || s_tdma_pio_spi_program_persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER) {
        /* Requested role may change before the installed follower retires.
         * Do not inspect a new endpoint and label it as a capture fault. */
        tdma_rx_start_cut_retire(TDMA_RX_START_CUT_GEOMETRY);
        return;
    }
    uint32_t reasons = 0u;
    if (s_tdma_pio_spi_rx_arm_epoch != cut->arm_epoch)
        reasons |= TDMA_RX_START_CUT_ARM;
    if (s_tdma_pio_spi_rx_sequence.observation_epoch != cut->observation_epoch_after)
        reasons |= TDMA_RX_START_CUT_OBSERVATION_EPOCH;
    if (s_tdma_event_prelaunch ?
        tdma_event_prelaunch_geometry(phys) != TDMA_GEOMETRY_OBSERVER_OK :
        (!phys->flight_overlay_alignment_locked ||
        phys->flight_physical_byte_count != cut->physical_frame_words ||
        phys->flight_alignment_byte_shift != cut->alignment_byte_shift ||
        phys->flight_alignment_bit_shift != cut->alignment_bit_shift ||
        phys->flight_data_phase_delay_cycles != s_tdma_rx_start_cut_data_phase ||
        phys->flight_marker_phase_delay_cycles != s_tdma_rx_start_cut_marker_phase))
        reasons |= TDMA_RX_START_CUT_GEOMETRY;
    if (clock_get_hz(clk_sys) != cut->pio_hz) reasons |= TDMA_RX_START_CUT_CLOCK;
    const PIO capture_pio = tdma_pio_spi_phys_capture_pio(phys);
    const uint capture_sm = tdma_pio_spi_phys_capture_sm(phys);
    if ((capture_pio->fdebug & (1u << (capture_sm + PIO_FDEBUG_RXSTALL_LSB))) != 0u)
        reasons |= TDMA_RX_START_CUT_CAPTURE_RXSTALL;
    if ((capture_pio->ctrl & (1u << capture_sm)) == 0u)
        reasons |= TDMA_RX_START_CUT_CAPTURE_DISABLED;
    if (!tdma_rx_start_cut_dma_available() ||
        (uint32_t)s_tdma_pio_spi_rx_dma_channel != cut->dma_channel) {
        reasons |= TDMA_RX_START_CUT_DMA_CONFIG;
    } else {
        const uint32_t ctrl = dma_hw->ch[cut->dma_channel].ctrl_trig;
        if ((ctrl & TDMA_RX_START_CUT_DMA_ERRORS) != 0u) reasons |= TDMA_RX_START_CUT_DMA_ERROR;
        if ((ctrl & TDMA_RX_START_CUT_DMA_CONFIG_MASK) !=
            (cut->dma_ctrl_after & TDMA_RX_START_CUT_DMA_CONFIG_MASK))
            reasons |= TDMA_RX_START_CUT_DMA_CONFIG;
        tdma_rx_dma_counter_t counter = s_tdma_pio_spi_rx_sequence;
        const uint64_t before = vdc_timestamp_clock_read_ticks64();
        const uint32_t count = dma_hw->ch[cut->dma_channel].transfer_count;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        const uint64_t after = vdc_timestamp_clock_read_ticks64();
        uint64_t produced;
        bool lost;
        if ((count & DMA_CH0_TRANS_COUNT_MODE_BITS) !=
                (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB) ||
            !tdma_rx_dma_counter_observe(&counter, count & DMA_CH0_TRANS_COUNT_COUNT_BITS,
                before, after, &produced, &lost)) reasons |= TDMA_RX_START_CUT_COUNTER;
        else if (lost || counter.observation_epoch != cut->observation_epoch_after)
            reasons |= TDMA_RX_START_CUT_OBSERVATION_EPOCH;
    }
    if (s_tdma_event_observer.state != TDMA_EVENT_ACTIVE ||
        s_tdma_event_observer.epoch != cut->observer_epoch)
        reasons |= TDMA_RX_START_CUT_OBSERVER;
    tdma_rx_start_cut_retire(reasons);
}
/* RX_START_CUT_STORAGE_END */

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
    tdma_event_publish_snapshot(phys);
}

static void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *phys)
{
    /* Retire the installed persona even if configure has already changed the
     * requested role. Do this before a maintenance owner can reuse the SMs. */
    if (phys == NULL || s_tdma_pio_spi_program_persona !=
        TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER) return;
    tdma_rx_start_cut_monitor(phys);
    uint32_t stop_reason = TDMA_GEOMETRY_OBSERVER_STOP;
    if (s_tdma_event_prelaunch) {
        const uint32_t reason = tdma_event_prelaunch_geometry(phys);
        if (reason != TDMA_GEOMETRY_OBSERVER_OK) stop_reason = reason;
        else if ((s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_OBSERVATION_EPOCH) != 0u)
            stop_reason = TDMA_GEOMETRY_OBSERVER_OBSERVATION;
        else if ((s_tdma_rx_start_cut.retire_reasons & ~TDMA_RX_START_CUT_STOP) != 0u)
            stop_reason = TDMA_GEOMETRY_OBSERVER_FAULT;
    }
    tdma_event_prelaunch_mark(TDMA_GEOMETRY_OBSERVER_RETIRED, stop_reason);
    tdma_rx_start_cut_retire(TDMA_RX_START_CUT_STOP);
    tdma_event_observer_stop(&s_tdma_event_observer);
    tdma_event_history_retire(&s_tdma_event_history);
    s_tdma_event_arm_epoch = 0u;
    tdma_event_candidate_retire();
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
    if (phys == NULL) return;
    tdma_rx_start_cut_arm_begin();
    s_tdma_event_prelaunch = false;
    s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_OK;
    memset(&s_tdma_event_geometry, 0, sizeof(s_tdma_event_geometry));
    if (!tdma_pio_spi_phys_event_selected(phys)) return;
    tdma_event_history_retire(&s_tdma_event_history);
    s_tdma_event_arm_epoch = 0u;
    tdma_event_candidate_retire();
    tdma_event_observer_init(&s_tdma_event_observer);
    /* Candidate counters and the last historical result survive new ARM. */
    memset(&s_tdma_event_snapshot, 0, offsetof(tdma_pio_spi_event_snapshot_t, candidate));
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
    if (!s_tdma_event_waiting || (!s_tdma_event_prelaunch &&
        (!phys->flight_overlay_alignment_locked || phys->flight_overlay_alignment_samples <
            TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES))) return;
    const PIO pio = phys->flight_resources.tx_pio;
    const uint32_t cs_mask = (1u << phys->rx_csn_pin) | (1u << phys->tx_csn_pin);
    if ((pio->dbg_padout & (1u << phys->tx_csn_pin)) == 0u ||
        (gpio_get_all() & cs_mask) != cs_mask) {
        s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_CS;
        return;
    }
    /* Ordinary startup uses live RX training. Selected startup instead tests
     * the separately frozen DMA-offset hypothesis against the next CS frame.
     * Neither coordinate is promoted to a qualified physical identity. */
    const uint32_t byte_shift = s_tdma_event_prelaunch
        ? s_tdma_event_geometry.dma_byte_shift : phys->flight_alignment_byte_shift;
    const uint32_t bit_shift = s_tdma_event_prelaunch
        ? s_tdma_event_geometry.dma_bit_shift : phys->flight_alignment_bit_shift;
    const uint64_t bits = ((uint64_t)byte_shift +
        TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_SEQUENCE_OFFSET +
        sizeof(uint32_t)) * 8u +
        bit_shift;
    if (bits < 32u || bits > (uint64_t)phys->flight_physical_byte_count * 8u ||
        bit_shift >= 8u || s_tdma_event_hz == 0u ||
        s_tdma_event_epoch == UINT32_MAX) {
        s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_GEOMETRY;
        return;
    }
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
    tdma_rx_dma_counter_t cut_counter = s_tdma_pio_spi_rx_sequence;
    tdma_rx_start_cut_before(phys);
    s->start_pad_before = gpio_get_all();
    /* Configuration/seed work can outlive the earlier idle observation. Do
     * not enable into a frame already in progress; leave the disabled SMs
     * waiting for a later owner phase without consuming an event epoch.
     * Edges after this check still face the post-enable dirty-start check. */
    if ((s->start_pad_before & cs_mask) != cs_mask) {
        s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_CS;
        tdma_event_publish_state(phys);
        return;
    }
    const uint64_t before = time_us_64();
    pio_enable_sm_mask_in_sync(pio, TDMA_EVENT_SM_MASK);
    const uint64_t after = time_us_64();
    s->start_pad_after = gpio_get_all();
    tdma_rx_start_cut_after(phys, &cut_counter, s->start_pad_before, s->start_pad_after);
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
    s_tdma_event_arm_epoch = 0u;
    (void)tdma_event_observer_start(&s_tdma_event_observer, &config,
                                   s_tdma_event_epoch, start, faults);
    if (s_tdma_event_observer.state == TDMA_EVENT_ACTIVE) {
        (void)tdma_event_history_start(&s_tdma_event_history,
            s_tdma_event_epoch, s_tdma_event_hz, start);
        if (s_tdma_event_history.active && s_tdma_pio_spi_rx_arm_valid)
            s_tdma_event_arm_epoch = s_tdma_pio_spi_rx_arm_epoch;
    } else {
        tdma_event_history_retire(&s_tdma_event_history);
        tdma_event_candidate_retire();
    }
    s->start_width_cycles = start.hi - start.lo;
    s_tdma_event_waiting = false;
    if (s_tdma_event_observer.state != TDMA_EVENT_ACTIVE)
        pio_set_sm_mask_enabled(pio, TDMA_EVENT_SM_MASK, false);
    tdma_rx_start_cut_monitor(phys);
    tdma_event_publish_state(phys);
}

/* Exactly one synchronous attempt after RX ARM, final injected WAIT and SM
 * enable. Failure is an ARM rejection; service must never retry it later. */
static bool tdma_pio_spi_phys_event_prelaunch(tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config)
{
    if (config->geometry_generation == 0u) return true;
    s_tdma_event_prelaunch = true;
    uint32_t reason = TDMA_GEOMETRY_OBSERVER_BINDING;
    if (!tdma_geometry_observer_select(phys, &s_tdma_event_geometry) ||
        s_tdma_event_geometry.generation != config->geometry_generation ||
        s_tdma_event_geometry.bound_config_seq != config->owner_config_seq ||
        !phys->armed || !phys->rx_capture_active || !s_tdma_event_waiting ||
        !tdma_pio_spi_phys_event_selected(phys)) goto rejected;
    tdma_event_prelaunch_mark(TDMA_GEOMETRY_OBSERVER_ARMING, TDMA_GEOMETRY_OBSERVER_OK);
    reason = tdma_event_prelaunch_geometry(phys);
    if (reason != TDMA_GEOMETRY_OBSERVER_OK) goto rejected;
    reason = TDMA_GEOMETRY_OBSERVER_FIFO;
    const PIO pio = phys->flight_resources.tx_pio;
    for (uint sm = TDMA_EVENT_RX_SM; sm <= TDMA_EVENT_SEQUENCE_SM; ++sm)
        if (pio_sm_get_rx_fifo_level(pio, sm) != 0u) goto rejected;
    const PIO capture = tdma_pio_spi_phys_capture_pio(phys);
    const uint capture_sm = tdma_pio_spi_phys_capture_sm(phys);
    if (pio_sm_get_rx_fifo_level(capture, capture_sm) != 0u ||
        (capture->fdebug & (1u << (capture_sm + PIO_FDEBUG_RXSTALL_LSB))) != 0u ||
        (pio->fdebug & (TDMA_EVENT_SM_MASK << PIO_FDEBUG_RXSTALL_LSB)) != 0u) goto rejected;
    reason = TDMA_GEOMETRY_OBSERVER_DMA_PROGRESS;
    if (!tdma_rx_start_cut_dma_available() || s_tdma_pio_spi_rx_sequence.produced_words != 0u ||
        s_tdma_pio_spi_rx_sequence.position != 0u ||
        dma_hw->ch[s_tdma_pio_spi_rx_dma_channel].transfer_count !=
            ((DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB) |
             s_tdma_pio_spi_rx_sequence.reload_words)) goto rejected;
    tdma_event_start(phys);
    reason = s_tdma_event_prelaunch_reason;
    if (s_tdma_event_waiting) goto rejected;
    reason = TDMA_GEOMETRY_OBSERVER_DIRTY_START;
    if ((s_tdma_event_observer.fault_bits & TDMA_EVENT_FAULT_DIRTY_START) != 0u) goto rejected;
    reason = tdma_event_prelaunch_geometry(phys);
    if (reason != TDMA_GEOMETRY_OBSERVER_OK) goto rejected;
    reason = TDMA_GEOMETRY_OBSERVER_FAULT;
    if (s_tdma_event_observer.state != TDMA_EVENT_ACTIVE) goto rejected;
    reason = TDMA_GEOMETRY_OBSERVER_DMA_PROGRESS;
    if ((s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_OBSERVATION_EPOCH) != 0u) {
        reason = TDMA_GEOMETRY_OBSERVER_OBSERVATION;
        goto rejected;
    }
    if (s_tdma_rx_start_cut.produced_before != 0u || s_tdma_rx_start_cut.produced_after != 0u ||
        s_tdma_rx_start_cut.capture_fifo_before != 0u || s_tdma_rx_start_cut.capture_fifo_after != 0u ||
        (s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RETIRED) != 0u) goto rejected;
    tdma_event_prelaunch_mark(TDMA_GEOMETRY_OBSERVER_ACTIVE, TDMA_GEOMETRY_OBSERVER_OK);
    return true;
rejected:
    tdma_event_prelaunch_mark(TDMA_GEOMETRY_OBSERVER_REJECTED, reason);
    tdma_pio_spi_phys_event_stop(phys);
    return false;
}

/* One real count observation per owner phase keeps a pre-START ARM honest.
 * It never resets coordinates or infers zero progress from idle GPIOs. */
static uint32_t tdma_event_prelaunch_observe(tdma_pio_spi_phys_t *phys)
{
    if (!tdma_rx_start_cut_dma_available()) return TDMA_GEOMETRY_OBSERVER_OBSERVATION;
    const uint64_t before = vdc_timestamp_clock_read_ticks64();
    const uint32_t count = dma_hw->ch[s_tdma_pio_spi_rx_dma_channel].transfer_count;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const uint64_t after = vdc_timestamp_clock_read_ticks64();
    uint64_t produced;
    bool lost;
    if ((count & DMA_CH0_TRANS_COUNT_MODE_BITS) !=
        (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB) ||
        !tdma_rx_dma_counter_observe(&s_tdma_pio_spi_rx_sequence,
            count & DMA_CH0_TRANS_COUNT_COUNT_BITS, before, after, &produced, &lost))
        return TDMA_GEOMETRY_OBSERVER_OBSERVATION;
    if (lost) {
        phys->snapshot.rx_observation_drop_count++;
        tdma_service_timing_rx_drop(TDMA_RX_DROP_EPOCH, 0u);
        s_tdma_pio_spi_rx_scan_produced = produced;
        if (!phys->flight_overlay_alignment_locked) phys->flight_overlay_alignment_samples = 0u;
        return TDMA_GEOMETRY_OBSERVER_OBSERVATION;
    }
    const uint32_t reason = tdma_event_prelaunch_geometry(phys);
    if (reason != TDMA_GEOMETRY_OBSERVER_OK) return reason;
    tdma_rx_start_cut_monitor(phys);
    return (s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RETIRED) != 0u
        ? TDMA_GEOMETRY_OBSERVER_FAULT : TDMA_GEOMETRY_OBSERVER_OK;
}

static void tdma_pio_spi_phys_event_service(tdma_pio_spi_phys_t *phys)
{
    if (!tdma_pio_spi_phys_event_selected(phys) || !phys->armed) return;
    uint64_t stage_start = tdma_service_timing_now();
    const uint64_t entered = time_us_64();
    tdma_pio_spi_event_snapshot_t *s = &s_tdma_event_snapshot;
    if (s_tdma_event_prelaunch && s_tdma_event_observer.state == TDMA_EVENT_ACTIVE) {
        const uint32_t reason = tdma_event_prelaunch_observe(phys);
        if (reason != TDMA_GEOMETRY_OBSERVER_OK) {
            tdma_event_prelaunch_mark(TDMA_GEOMETRY_OBSERVER_RETIRED, reason);
            tdma_pio_spi_phys_event_stop(phys);
        }
    }
    if (s_tdma_event_waiting) tdma_event_start(phys);
    if (s_tdma_event_observer.state != TDMA_EVENT_ACTIVE) {
        tdma_service_timing_record(TDMA_TIMING_EVENT_ENTRY, stage_start);
        stage_start = tdma_service_timing_now();
        tdma_rx_start_cut_monitor(phys);
        tdma_service_timing_record(TDMA_TIMING_EVENT_START_CUT, stage_start);
        if (s_tdma_event_candidate_dirty) {
            stage_start = tdma_service_timing_now();
            tdma_event_publish_state(phys);
            tdma_service_timing_record(TDMA_TIMING_EVENT_PUBLISH, stage_start);
        }
        return;
    }
    if (s_tdma_event_last_service_us != 0u) {
        const uint64_t gap = entered - s_tdma_event_last_service_us;
        if (gap > s->service_gap_max_us) s->service_gap_max_us = gap > UINT32_MAX ? UINT32_MAX : (uint32_t)gap;
    }
    s_tdma_event_last_service_us = entered;
    ++s->service_count;
    tdma_service_timing_record(TDMA_TIMING_EVENT_ENTRY, stage_start);
    stage_start = tdma_service_timing_now();
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
    tdma_service_timing_record(TDMA_TIMING_EVENT_HARVEST, stage_start);
    stage_start = tdma_service_timing_now();
    batch->observed = (tdma_event_interval_t){tdma_event_cycles(before, false), tdma_event_cycles(after, true)};
    tdma_service_timing_record(TDMA_TIMING_EVENT_CONVERT, stage_start);
    stage_start = tdma_service_timing_now();
    batch->post_faults = tdma_event_faults(phys);
    if (batch->count[0]) s->fifo_rx_first = batch->words[0][0];
    if (batch->count[0] > 1u) s->fifo_rx_second = batch->words[0][1];
    if (batch->count[1]) s->fifo_tx_first = batch->words[1][0];
    if (batch->count[1] > 1u) s->fifo_tx_second = batch->words[1][1];
    if (batch->count[2]) s->fifo_sequence_first = batch->words[2][0];
    const size_t count = tdma_event_observer_feed(&s_tdma_event_observer, batch, s_tdma_event_records);
    tdma_service_timing_record(TDMA_TIMING_EVENT_FEED, stage_start);
    stage_start = tdma_service_timing_now();
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
    tdma_service_timing_record(TDMA_TIMING_EVENT_FINAL_CHECK, stage_start);
    stage_start = tdma_service_timing_now();
    tdma_rx_start_cut_monitor(phys);
    tdma_service_timing_record(TDMA_TIMING_EVENT_START_CUT, stage_start);
    stage_start = tdma_service_timing_now();
    if (s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && count != 0u) {
        /* Retain the whole validated batch, after the final sticky-fault
         * check. Sequence lookup remains diagnostic; no packet identity or
         * physical-clock qualification follows from retaining these words. */
        (void)tdma_event_history_append(&s_tdma_event_history, s_tdma_event_records,
            count, s_tdma_event_observer.joined);
        if (!s_tdma_event_history.active) tdma_event_candidate_retire();
        const tdma_event_record_t *last = &s_tdma_event_records[count - 1u];
        s->published = count > UINT32_MAX - s->published ? UINT32_MAX :
                       s->published + (uint32_t)count;
        s->sequence = last->sequence;
        s->ordinal = last->ordinal;
        s->rx_elapsed_cycles = last->rx_elapsed_cycles;
        s->tx_elapsed_cycles = last->tx_elapsed_cycles;
    }
    if (s_tdma_event_observer.state == TDMA_EVENT_INVALID) {
        tdma_event_prelaunch_mark(TDMA_GEOMETRY_OBSERVER_RETIRED, TDMA_GEOMETRY_OBSERVER_FAULT);
        tdma_event_history_retire(&s_tdma_event_history);
        s_tdma_event_arm_epoch = 0u;
        tdma_event_candidate_retire();
        pio_set_sm_mask_enabled(pio, TDMA_EVENT_SM_MASK, false);
    }
    const uint64_t duration = time_us_64() - entered;
    if (duration > s->service_max_us) s->service_max_us = duration > UINT32_MAX ? UINT32_MAX : (uint32_t)duration;
    tdma_service_timing_record(TDMA_TIMING_EVENT_RETAIN, stage_start);
    stage_start = tdma_service_timing_now();
    tdma_event_publish_state(phys);
    tdma_service_timing_record(TDMA_TIMING_EVENT_PUBLISH, stage_start);
}

/* Software-only eligibility. No MMIO access, packet decode or timestamp
 * grant is hidden in either the capture pin or the delayed READY query. */
static tdma_rx_event_reason_t tdma_event_capture_current(
    const tdma_pio_spi_phys_t *phys, const tdma_rx_capture_t *capture)
{
    if (capture == NULL || capture->flags != TDMA_RX_CAPTURE_PRIVATE_COPY ||
        capture->capture_id == 0u || capture->arm_epoch == 0u)
        return TDMA_RX_EVENT_NO_CAPTURE;
    if (!tdma_pio_spi_phys_event_selected(phys) || capture->persona !=
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)
        return TDMA_RX_EVENT_UNSUPPORTED_PERSONA;
    if (!phys->armed || !phys->rx_capture_active || !s_tdma_pio_spi_rx_arm_valid ||
        capture->arm_epoch != s_tdma_pio_spi_rx_arm_epoch)
        return TDMA_RX_EVENT_ARM_STALE;
    /* One physical capture can occupy the station. A later capture cannot
     * extend an older token's query eligibility under the same ARM. */
    if (capture->capture_id != s_tdma_pio_spi_rx_capture_id)
        return TDMA_RX_EVENT_CAPTURE_STALE;
    /* RX ARM resets the DMA observation epoch to zero. Its lifetime is
     * scoped by the nonzero ARM and capture identities checked above;
     * only a mismatch denotes observation loss within that ARM. */
    if (capture->observation_epoch !=
            s_tdma_pio_spi_rx_sequence.observation_epoch)
        return TDMA_RX_EVENT_OBSERVATION_STALE;
    return TDMA_RX_EVENT_NONE;
}

uint32_t tdma_pio_spi_phys_rx_event_pin(void *context,
                                      const tdma_rx_capture_t *capture)
{
    const tdma_pio_spi_phys_t *phys = (const tdma_pio_spi_phys_t *)context;
    if (phys == NULL || tdma_event_capture_current(phys, capture) != TDMA_RX_EVENT_NONE ||
        s_tdma_event_observer.state != TDMA_EVENT_ACTIVE || s_tdma_event_waiting ||
        !s_tdma_event_history.active || s_tdma_event_observer.epoch == 0u ||
        s_tdma_event_history.epoch != s_tdma_event_observer.epoch ||
        s_tdma_event_arm_epoch != capture->arm_epoch) return 0u;
    /* This is the epoch at private-copy handoff, not proof that DMA bytes
     * arrived after observer start or correspond to a particular ordinal. */
    return s_tdma_event_observer.epoch;
}

static bool tdma_event_capture_envelope(const tdma_rx_capture_t *capture,
                                       size_t packet_size)
{
    if (packet_size < TDMA_TRANSPORT_FRAME_HEADER_SIZE ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX || capture->bit_shift > 7u ||
        capture->frame_words != packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE ||
        capture->frame_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX ||
        capture->candidate > capture->produced_before ||
        capture->produced_after < capture->produced_before) return false;
    const uint64_t copied_words = (uint64_t)capture->frame_words + (capture->bit_shift != 0u);
    return capture->produced_before - capture->candidate >= copied_words &&
        capture->produced_after - capture->candidate < TDMA_PIO_SPI_RX_RING_WORDS;
}

static void tdma_event_candidate_history_state(void)
{
    tdma_rx_event_candidate_snapshot_t *s = &s_tdma_event_snapshot.candidate;
    const tdma_event_history_t *history = &s_tdma_event_history;
    s->observer_arm_epoch = s_tdma_event_arm_epoch;
    s->query_observer_epoch = s_tdma_event_observer.epoch;
    s->observer_state = (uint32_t)s_tdma_event_observer.state;
    s->history_reason = (uint32_t)history->reason;
    s->history_active = history->active ? 1u : 0u;
    s->history_count = history->count;
    s->history_oldest_ordinal = history->oldest_ordinal;
    s->history_accepted = history->accepted;
    s->history_evicted = history->evicted;
    s->pio_hz = history->pio_hz;
}

void tdma_pio_spi_phys_rx_event_query(void *context,
    const tdma_rx_capture_t *capture, uint32_t capture_observer_epoch,
    uint32_t sequence, size_t packet_size, uint64_t station_age_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL) return;
    tdma_event_candidate_begin(capture, capture_observer_epoch, sequence, station_age_ns);
    tdma_rx_event_candidate_snapshot_t *s = &s_tdma_event_snapshot.candidate;
    /* A skipped lookup must not expose a prior query's history result. */
    s->history_query_reason = UINT32_MAX;
    tdma_rx_event_reason_t reason = tdma_event_capture_current(phys, capture);
    if (reason == TDMA_RX_EVENT_NONE) {
        if (capture_observer_epoch == 0u) reason = TDMA_RX_EVENT_PIN_MISSING;
        else if (s_tdma_event_observer.state != TDMA_EVENT_ACTIVE || s_tdma_event_waiting)
            reason = TDMA_RX_EVENT_OBSERVER_UNAVAILABLE;
        else if (capture_observer_epoch != s_tdma_event_observer.epoch)
            reason = TDMA_RX_EVENT_PIN_STALE;
        else if (s_tdma_event_arm_epoch != capture->arm_epoch)
            reason = TDMA_RX_EVENT_ARM_STALE;
        else if (!s_tdma_event_history.active) reason = TDMA_RX_EVENT_HISTORY_UNAVAILABLE;
        else if (capture_observer_epoch != s_tdma_event_history.epoch)
            reason = TDMA_RX_EVENT_PIN_STALE;
        else if (!tdma_event_capture_envelope(capture, packet_size))
            reason = TDMA_RX_EVENT_BAD_ENVELOPE;
    }
    if (reason == TDMA_RX_EVENT_NONE) {
        tdma_event_record_t record;
        if (tdma_event_history_lookup(&s_tdma_event_history,
                capture_observer_epoch, sequence, &record)) {
            /* Copy only independent event fields. Packet sequence is kept
             * separately; no expected identity becomes observed identity. */
            s->event_sequence = record.sequence;
            s->event_ordinal = record.ordinal;
            s->rx_elapsed_cycles = record.rx_elapsed_cycles;
            s->tx_elapsed_cycles = record.tx_elapsed_cycles;
            s->start_lo_cycles = record.start_bounds.lo;
            s->start_hi_cycles = record.start_bounds.hi;
            reason = TDMA_RX_EVENT_MATCHED;
        } else if (s_tdma_event_history.query_reason == TDMA_EVENT_HISTORY_NOT_FOUND) {
            reason = TDMA_RX_EVENT_NOT_FOUND;
        } else if (s_tdma_event_history.query_reason == TDMA_EVENT_HISTORY_AMBIGUOUS) {
            reason = TDMA_RX_EVENT_AMBIGUOUS;
        } else {
            reason = TDMA_RX_EVENT_HISTORY_REJECTED;
        }
        s->history_query_reason = (uint32_t)s_tdma_event_history.query_reason;
    }
    tdma_event_candidate_history_state();
    tdma_event_candidate_finish(reason);
    if (!s_tdma_event_history.active || reason == TDMA_RX_EVENT_ARM_STALE ||
        reason == TDMA_RX_EVENT_OBSERVATION_STALE || reason == TDMA_RX_EVENT_CAPTURE_STALE ||
        reason == TDMA_RX_EVENT_PIN_STALE) tdma_event_candidate_retire();
    /* Active observers publish at the next existing service boundary. An
     * inactive/unsupported owner has no guaranteed later event publication. */
    if (!tdma_pio_spi_phys_event_selected(phys) || !phys->armed ||
        s_tdma_event_observer.state != TDMA_EVENT_ACTIVE) tdma_event_publish_state(phys);
}
#else
static bool tdma_pio_spi_phys_event_prelaunch(tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config)
{
    (void)phys;
    if (config->geometry_generation == 0u) return true;
    tdma_geometry_observer_record(config->geometry_generation, 0u,
        TDMA_GEOMETRY_OBSERVER_REJECTED, TDMA_GEOMETRY_OBSERVER_DISABLED, 0u);
    return false;
}
static void tdma_rx_start_cut_arm_begin(void) { }
static void tdma_rx_start_cut_disarmed(void) { }
bool tdma_pio_spi_phys_get_rx_start_cut(tdma_rx_start_cut_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    return false;
}
static bool tdma_pio_spi_phys_event_selected(const tdma_pio_spi_phys_t *phys)
{ (void)phys; return false; }
static void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return;
    tdma_event_candidate_retire();
    if (s_tdma_event_snapshot.candidate.query_count != 0u) tdma_event_publish_snapshot(phys);
}
static void tdma_pio_spi_phys_event_prepare(tdma_pio_spi_phys_t *phys,
                                            const tdma_ring_runtime_config_t *config)
{ (void)config; tdma_pio_spi_phys_event_stop(phys); }
static void tdma_pio_spi_phys_event_service(tdma_pio_spi_phys_t *phys)
{ if (phys != NULL && s_tdma_event_candidate_dirty) tdma_event_publish_snapshot(phys); }
uint32_t tdma_pio_spi_phys_rx_event_pin(void *context, const tdma_rx_capture_t *capture)
{ (void)context; (void)capture; return 0u; }
void tdma_pio_spi_phys_rx_event_query(void *context,
    const tdma_rx_capture_t *capture, uint32_t capture_observer_epoch,
    uint32_t sequence, size_t packet_size, uint64_t station_age_ns)
{
    (void)packet_size;
    if (context == NULL) return;
    tdma_event_candidate_begin(capture, capture_observer_epoch, sequence, station_age_ns);
    s_tdma_event_snapshot.candidate.history_query_reason = UINT32_MAX;
    tdma_event_candidate_finish(TDMA_RX_EVENT_OBSERVER_DISABLED);
    tdma_event_candidate_retire();
    tdma_event_publish_snapshot((tdma_pio_spi_phys_t *)context);
}
#endif
