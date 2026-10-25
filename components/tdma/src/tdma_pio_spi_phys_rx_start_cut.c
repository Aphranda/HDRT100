/* tdma_pio_spi_phys_rx_start_cut unit, split out of tdma_pio_spi_phys_event.inc (2026-10-25).
 * The code below is the former fragment block verbatim. */
#include "tdma_pio_spi_phys_internal.h"

#include <string.h>

#include "pico.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "project_config.h"
#include "tdma_service_timing.h"
#include "vdc_timestamp_clock.h"

/* RX_START_CUT_STORAGE_BEGIN: independent of the general event snapshot. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TDMA_START_CUT_RAM __not_in_flash("tdma_start_cut") __attribute__((noinline, noclone))
#else
#define TDMA_START_CUT_RAM
#endif

tdma_rx_start_cut_t s_tdma_rx_start_cut;
tdma_rx_start_cut_t s_tdma_rx_start_cut_slots[2];
uint32_t s_tdma_rx_start_cut_guard;
uint32_t s_tdma_rx_start_cut_data_phase;
uint32_t s_tdma_rx_start_cut_marker_phase;
_Static_assert(sizeof(s_tdma_rx_start_cut) + sizeof(s_tdma_rx_start_cut_slots) +
    sizeof(s_tdma_rx_start_cut_guard) + sizeof(s_tdma_rx_start_cut_data_phase) +
    sizeof(s_tdma_rx_start_cut_marker_phase) <= 512u,
    "Independent start-cut owner and publication storage must remain bounded");

#define TDMA_RX_START_CUT_DMA_ERRORS (DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS | \
    DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)
#define TDMA_RX_START_CUT_DMA_CONFIG_MASK (DMA_CH0_CTRL_TRIG_BITS & \
    ~(TDMA_RX_START_CUT_DMA_ERRORS | DMA_CH0_CTRL_TRIG_BUSY_BITS))

void tdma_rx_start_cut_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_tdma_rx_start_cut_guard, __ATOMIC_RELAXED);
    const uint32_t next = guard + 2u;
    /* Order the previous slot switch before reusing its older storage. */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_tdma_rx_start_cut_slots[(next >> 1u) & 1u] = s_tdma_rx_start_cut;
    __atomic_store_n(&s_tdma_rx_start_cut_guard, next, __ATOMIC_RELEASE);
}

void tdma_rx_start_cut_arm_begin(void)
{
    /* The physical ARM entry calls this even before admission can fail.
     * Retire live history independently of whether a first cut is recorded. */
    tdma_event_live_retire(TDMA_EVENT_LIVE_ARM);
    tdma_event_live_anchor_reset();
    memset(&s_tdma_rx_start_cut, 0, sizeof(s_tdma_rx_start_cut));
    s_tdma_rx_start_cut.schema = TDMA_RX_START_CUT_SCHEMA;
    tdma_rx_start_cut_publish();
}

void tdma_rx_start_cut_retire(uint32_t reasons)
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
void tdma_rx_start_cut_disarmed(void)
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

bool tdma_rx_start_cut_dma_available(void)
{
    return s_tdma_pio_spi_rx_dma_channel >= 0 &&
        (uint)s_tdma_pio_spi_rx_dma_channel < NUM_DMA_CHANNELS;
}

/* Preparation can be abandoned by the final CS gate. RECORDED is assigned
 * only after the real enable; no failed attempt is exported as an event. */
void tdma_rx_start_cut_before(tdma_pio_spi_phys_t *phys)
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
    if (pio_sm_is_exec_stalled(capture_pio, capture_sm))
        cut->flags |= TDMA_RX_START_CUT_CAPTURE_ENTRY_WAIT_BEFORE;
    __atomic_thread_fence(__ATOMIC_SEQ_CST); /* DMB: completed MMIO before enable. */
}

uint32_t tdma_rx_start_cut_lift(tdma_rx_start_cut_t *cut,
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

void tdma_rx_start_cut_after(tdma_pio_spi_phys_t *phys,
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
    if (pio_sm_is_exec_stalled(capture_pio, capture_sm))
        cut->flags |= TDMA_RX_START_CUT_CAPTURE_ENTRY_WAIT_AFTER;
    __atomic_thread_fence(__ATOMIC_SEQ_CST); /* DMB before the closing tick read. */
    cut->sample_after_ticks = vdc_timestamp_clock_read_ticks64();
    cut->flags |= TDMA_RX_START_CUT_DIAGNOSTIC | TDMA_RX_START_CUT_RECORDED |
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

void TDMA_START_CUT_RAM tdma_rx_start_cut_monitor(tdma_pio_spi_phys_t *phys)
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
