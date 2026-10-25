/* tdma_pio_spi_phys_rx_first unit, split out of tdma_pio_spi_phys_event.inc (2026-10-25).
 * The code below is the former fragment block verbatim. */
#include "tdma_pio_spi_phys_internal.h"

#include <string.h>

#include "pico.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "project_config.h"
#include "tdma_service_timing.h"
#include "vdc_timestamp_clock.h"

/* RX_FIRST_WINDOW_STORAGE_BEGIN: one guarded archive, never a DMA workspace. */
tdma_rx_first_window_t s_tdma_rx_first_window;
uint32_t s_tdma_rx_first_guard;

bool tdma_rx_first_write_begin(void)
{
    const uint32_t guard = __atomic_load_n(&s_tdma_rx_first_guard, __ATOMIC_RELAXED);
    if ((guard & 1u) != 0u || guard >= UINT32_MAX - 2u) {
        __atomic_store_n(&s_tdma_rx_first_guard, UINT32_MAX, __ATOMIC_RELEASE);
        return false;
    }
    __atomic_store_n(&s_tdma_rx_first_guard, guard + 1u, __ATOMIC_RELEASE);
    __dmb();
    return true;
}

void tdma_rx_first_write_end(void)
{
    const uint32_t both = TDMA_RX_FIRST_RAW_DONE | TDMA_RX_FIRST_EVENT_DONE;
    if ((s_tdma_rx_first_window.flags & both) == both)
        s_tdma_rx_first_window.flags |= TDMA_RX_FIRST_COLLECTION_TERMINAL;
    __atomic_store_n(&s_tdma_rx_first_guard, s_tdma_rx_first_guard + 1u, __ATOMIC_RELEASE);
}

void tdma_rx_first_reason(uint32_t reason)
{
    if (s_tdma_rx_first_window.first_reason == TDMA_RX_FIRST_NONE)
        s_tdma_rx_first_window.first_reason = reason;
}

void tdma_rx_first_window_admit(const tdma_ring_runtime_config_t *config)
{
    if (config == NULL || config->owner_config_seq == s_tdma_rx_first_window.config_seq ||
        !tdma_rx_first_write_begin()) return;
    memset(&s_tdma_rx_first_window, 0, sizeof(s_tdma_rx_first_window));
    s_tdma_rx_first_window.schema = TDMA_RX_FIRST_WINDOW_SCHEMA;
    s_tdma_rx_first_window.config_seq = config->owner_config_seq;
    s_tdma_rx_first_window.geometry_generation = config->geometry_generation;
    s_tdma_rx_first_window.event_ordinal = UINT32_MAX;
    s_tdma_rx_first_window.dma_channel = UINT32_MAX;
    if (config->geometry_generation != 0u)
        s_tdma_rx_first_window.flags = TDMA_RX_FIRST_AVAILABLE | TDMA_RX_FIRST_DIAGNOSTIC_ONLY |
            TDMA_RX_FIRST_IDENTITY_UNPROVED | TDMA_RX_FIRST_PHYSICAL_FIRST_UNPROVED;
    tdma_rx_first_write_end();
}

void tdma_pio_spi_phys_rx_first_window_persona_unload(void)
{
    if ((s_tdma_rx_first_window.flags & TDMA_RX_FIRST_AVAILABLE) == 0u ||
        /* A new admission has not acquired this old persona's lifetime. */
        (s_tdma_rx_first_window.arm_epoch == 0u &&
         (s_tdma_rx_first_window.flags & TDMA_RX_FIRST_COLLECTION_TERMINAL) == 0u) ||
        !tdma_rx_first_write_begin()) return;
    s_tdma_rx_first_window.flags &= ~TDMA_RX_FIRST_AVAILABLE;
    s_tdma_rx_first_window.flags |= TDMA_RX_FIRST_RETIRED;
    s_tdma_rx_first_window.retire_reasons |= 1u << TDMA_RX_FIRST_PERSONA_OR_CONFIG_RETIRED;
    tdma_rx_first_write_end();
}

bool tdma_pio_spi_phys_get_rx_first_window(tdma_rx_first_window_t *out)
{
    if (out == NULL) return false;
    const uint32_t interrupts = save_and_disable_interrupts();
    const uint64_t before = time_us_64();
    bool copied = false;
    for (uint attempt = 0u; attempt < 3u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&s_tdma_rx_first_guard, __ATOMIC_ACQUIRE);
        if ((guard & 1u) != 0u) continue;
        *out = s_tdma_rx_first_window;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t after_guard = __atomic_load_n(&s_tdma_rx_first_guard, __ATOMIC_ACQUIRE);
        const uint64_t after = time_us_64();
        if (after < before || after - before > 1000u) break;
        if (guard == after_guard) { copied = (out->flags & TDMA_RX_FIRST_AVAILABLE) != 0u; break; }
    }
    restore_interrupts(interrupts);
    if (!copied) memset(out, 0, sizeof(*out));
    return copied;
}

void tdma_rx_first_raw_done(uint32_t reason)
{
    s_tdma_rx_first_window.raw_reason = reason;
    s_tdma_rx_first_window.flags |= TDMA_RX_FIRST_RAW_DONE;
    if (reason == TDMA_RX_FIRST_NONE) s_tdma_rx_first_window.flags |= TDMA_RX_FIRST_RAW_COPY_VALID;
    else tdma_rx_first_reason(reason);
}

void tdma_rx_first_window_retire(uint32_t reason, bool stopped)
{
    tdma_rx_first_window_t *w = &s_tdma_rx_first_window;
    if ((w->flags & TDMA_RX_FIRST_AVAILABLE) == 0u ||
        ((w->retire_reasons & (1u << reason)) != 0u &&
         (!stopped || (w->flags & TDMA_RX_FIRST_STOPPED) != 0u)) ||
        !tdma_rx_first_write_begin()) return;
    w->flags |= TDMA_RX_FIRST_RETIRED | (stopped ? TDMA_RX_FIRST_STOPPED : 0u);
    w->retire_reasons |= 1u << reason;
    if ((w->flags & TDMA_RX_FIRST_RAW_DONE) == 0u) tdma_rx_first_raw_done(reason);
    if ((w->flags & TDMA_RX_FIRST_EVENT_DONE) == 0u) {
        /* Adapter cleanup may STOP a newly admitted ARM before prelaunch.
         * Its sparse archive never acquired the previous observer's tuple. */
        if (w->arm_epoch != 0u && w->observer_epoch != 0u &&
            w->observer_epoch == s_tdma_event_observer.epoch) {
            w->event_state = s_tdma_event_observer.state;
            w->event_reason = s_tdma_event_observer.reason;
            w->event_fault_bits = s_tdma_event_observer.fault_bits;
        }
        w->flags |= TDMA_RX_FIRST_EVENT_DONE;
        tdma_rx_first_reason(reason);
    }
    tdma_rx_first_write_end();
}

void tdma_rx_first_window_arm_failed(void)
{
    tdma_rx_first_window_t *w = &s_tdma_rx_first_window;
    if ((w->flags & TDMA_RX_FIRST_AVAILABLE) == 0u ||
        (w->flags & (TDMA_RX_FIRST_PRELAUNCH_ACTIVE | TDMA_RX_FIRST_COLLECTION_TERMINAL)) != 0u ||
        !tdma_rx_first_write_begin()) return;
    /* This admission never bound an observer: do not attach the prior one. */
    w->prelaunch_reason = TDMA_GEOMETRY_OBSERVER_GEOMETRY;
    w->raw_reason = TDMA_RX_FIRST_PRELAUNCH_REJECTED;
    w->first_reason = TDMA_RX_FIRST_PRELAUNCH_REJECTED;
    w->retire_reasons |= 1u << TDMA_RX_FIRST_PRELAUNCH_REJECTED;
    w->flags |= TDMA_RX_FIRST_RETIRED | TDMA_RX_FIRST_RAW_DONE | TDMA_RX_FIRST_EVENT_DONE;
    tdma_rx_first_write_end();
}

void tdma_rx_first_window_prelaunch(const tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config, uint32_t reason)
{
    tdma_rx_first_window_t *w = &s_tdma_rx_first_window;
    if ((w->flags & TDMA_RX_FIRST_AVAILABLE) == 0u || w->config_seq != config->owner_config_seq ||
        (w->flags & (TDMA_RX_FIRST_PRELAUNCH_ACTIVE | TDMA_RX_FIRST_COLLECTION_TERMINAL)) != 0u ||
        !tdma_rx_first_write_begin()) return;
    w->arm_epoch = s_tdma_pio_spi_rx_arm_epoch;
    w->observation_epoch = s_tdma_pio_spi_rx_sequence.observation_epoch;
    w->observer_epoch = s_tdma_event_observer.epoch;
    w->observer_base_us = s_tdma_event_base_us;
    w->pio_hz = s_tdma_event_hz;
    w->physical_frame_words = phys->flight_physical_byte_count;
    const uint64_t packet = (uint64_t)TDMA_PIO_SPI_PACKET_HEADER_SIZE +
        TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size;
    w->outer_packet_words = packet <= UINT32_MAX ? (uint32_t)packet : 0u;
    if (s_tdma_event_geometry.generation == config->geometry_generation &&
        s_tdma_event_geometry.bound_config_seq == config->owner_config_seq) {
        w->map_generation = s_tdma_event_geometry.bound_map_generation;
        w->alignment_byte_shift = s_tdma_event_geometry.dma_byte_shift;
        w->alignment_bit_shift = s_tdma_event_geometry.dma_bit_shift;
    }
    const uint64_t end = packet + w->alignment_byte_shift + (w->alignment_bit_shift != 0u);
    w->envelope_end = end <= UINT32_MAX ? (uint32_t)end : UINT32_MAX;
    w->prelaunch_reason = reason;
    if (reason != TDMA_GEOMETRY_OBSERVER_OK) {
        tdma_rx_first_reason(TDMA_RX_FIRST_PRELAUNCH_REJECTED);
    } else {
        w->flags |= TDMA_RX_FIRST_PRELAUNCH_ACTIVE;
        if (packet > UINT32_MAX || w->physical_frame_words == 0u ||
            w->physical_frame_words > TDMA_RX_FIRST_WINDOW_RAW_CAPACITY ||
            w->physical_frame_words >= TDMA_PIO_SPI_RX_RING_WORDS ||
            w->alignment_bit_shift >= 8u || w->physical_frame_words < packet)
            tdma_rx_first_raw_done(TDMA_RX_FIRST_GEOMETRY_OR_CAPACITY);
        else if (end > w->physical_frame_words) {
            w->flags |= TDMA_RX_FIRST_ENVELOPE_CROSSES_WINDOW;
            tdma_rx_first_reason(TDMA_RX_FIRST_CROSSES_WINDOW);
        }
    }
    tdma_rx_first_write_end();
    if (reason != TDMA_GEOMETRY_OBSERVER_OK)
        tdma_rx_first_window_retire(TDMA_RX_FIRST_PRELAUNCH_REJECTED, false);
}

bool tdma_rx_first_current(const tdma_pio_spi_phys_t *phys)
{
    const tdma_rx_first_window_t *w = &s_tdma_rx_first_window;
    return (w->flags & (TDMA_RX_FIRST_AVAILABLE | TDMA_RX_FIRST_PRELAUNCH_ACTIVE | TDMA_RX_FIRST_RETIRED)) ==
        (TDMA_RX_FIRST_AVAILABLE | TDMA_RX_FIRST_PRELAUNCH_ACTIVE) &&
        s_tdma_event_prelaunch && phys->armed && phys->rx_capture_active &&
        w->arm_epoch == s_tdma_pio_spi_rx_arm_epoch &&
        w->observer_epoch == s_tdma_event_observer.epoch &&
        w->observation_epoch == s_tdma_pio_spi_rx_sequence.observation_epoch &&
        w->geometry_generation == s_tdma_event_geometry.generation &&
        w->config_seq == s_tdma_event_geometry.bound_config_seq &&
        w->map_generation == s_tdma_event_geometry.bound_map_generation &&
        s_tdma_rx_start_cut.retire_reasons == 0u &&
        tdma_event_prelaunch_geometry(phys) == TDMA_GEOMETRY_OBSERVER_OK;
}

void tdma_rx_first_window_words(const tdma_event_batch_t *batch)
{
    tdma_rx_first_window_t *w = &s_tdma_rx_first_window;
    if ((w->flags & (TDMA_RX_FIRST_PRELAUNCH_ACTIVE | TDMA_RX_FIRST_RETIRED | TDMA_RX_FIRST_EVENT_DONE)) !=
        TDMA_RX_FIRST_PRELAUNCH_ACTIVE || batch->epoch != w->observer_epoch ||
        !tdma_rx_first_write_begin()) return;
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        const uint base = 2u * stream, need = stream == 2u ? 1u : 2u;
        uint source = 0u;
        for (uint i = 0u; i < need && source < batch->count[stream]; ++i) {
            if ((w->event_word_mask & (1u << (base + i))) != 0u) continue;
            w->event_words[base + i] = batch->words[stream][source++];
            w->event_word_mask |= 1u << (base + i);
        }
    }
    tdma_rx_first_write_end();
}

/* Reads actual completed-write counts independently around the one raw copy.
 * The local counter never alters scanning, capture IDs or drop accounting. */
uint32_t tdma_rx_first_raw_observe(tdma_pio_spi_phys_t *phys,
    tdma_rx_dma_counter_t *counter, bool post)
{
    tdma_rx_first_window_t *w = &s_tdma_rx_first_window;
    if (!tdma_rx_start_cut_dma_available() ||
        (uint32_t)s_tdma_pio_spi_rx_dma_channel != s_tdma_rx_start_cut.dma_channel)
        return TDMA_RX_FIRST_RAW_DMA_CONFIG;
    const uint channel = (uint)s_tdma_pio_spi_rx_dma_channel;
    const PIO pio = tdma_pio_spi_phys_capture_pio(phys);
    const uint sm = tdma_pio_spi_phys_capture_sm(phys);
    const uint64_t before = vdc_timestamp_clock_read_ticks64();
    const uint32_t count = dma_hw->ch[channel].transfer_count;
    const uint32_t ctrl = dma_hw->ch[channel].ctrl_trig;
    const uint32_t fdebug = pio->fdebug;
    const uint32_t enabled = pio->ctrl;
    __dmb();
    const uint64_t after = vdc_timestamp_clock_read_ticks64();
    w->dma_channel = channel;
    if (post) {
        w->dma_count_after = count; w->dma_ctrl_after = ctrl;
        w->capture_fdebug_after = fdebug; w->copy_after_ticks = after;
    } else {
        w->dma_count_before = count; w->dma_ctrl_before = ctrl;
        w->capture_fdebug_before = fdebug; w->copy_before_ticks = before;
    }
    if ((ctrl & TDMA_RX_START_CUT_DMA_ERRORS) != 0u) return TDMA_RX_FIRST_RAW_DMA_ERROR;
    if ((ctrl & DMA_CH0_CTRL_TRIG_EN_BITS) == 0u ||
        (ctrl & TDMA_RX_START_CUT_DMA_CONFIG_MASK) !=
            (s_tdma_rx_start_cut.dma_ctrl_after & TDMA_RX_START_CUT_DMA_CONFIG_MASK) ||
        counter->reload_words != w->physical_frame_words * TDMA_RX_DMA_RELOAD_FRAMES ||
        (count & DMA_CH0_TRANS_COUNT_MODE_BITS) !=
            (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB))
        return TDMA_RX_FIRST_RAW_DMA_CONFIG;
    if ((fdebug & (1u << (sm + PIO_FDEBUG_RXSTALL_LSB))) != 0u || (enabled & (1u << sm)) == 0u)
        return TDMA_RX_FIRST_RAW_CAPTURE_FAULT;
    bool lost = false;
    uint64_t produced = 0u;
    if (!tdma_rx_dma_counter_observe(counter, count & DMA_CH0_TRANS_COUNT_COUNT_BITS,
            before, after, &produced, &lost)) return TDMA_RX_FIRST_RAW_COUNTER_OR_EPOCH;
    if (post) w->produced_after = produced; else w->produced_before = produced;
    if (lost || counter->observation_epoch != w->observation_epoch)
        return TDMA_RX_FIRST_RAW_COUNTER_OR_EPOCH;
    return TDMA_RX_FIRST_NONE;
}

void tdma_rx_first_window_raw(tdma_pio_spi_phys_t *phys)
{
    tdma_rx_first_window_t *w = &s_tdma_rx_first_window;
    if ((w->flags & (TDMA_RX_FIRST_PRELAUNCH_ACTIVE | TDMA_RX_FIRST_RAW_DONE | TDMA_RX_FIRST_RETIRED)) !=
        TDMA_RX_FIRST_PRELAUNCH_ACTIVE || !tdma_rx_first_current(phys) ||
        !tdma_rx_first_write_begin()) return;
    tdma_rx_dma_counter_t counter = s_tdma_pio_spi_rx_sequence;
    uint32_t reason = tdma_rx_first_raw_observe(phys, &counter, false);
    if (reason == TDMA_RX_FIRST_NONE && w->produced_before < w->physical_frame_words) {
        tdma_rx_first_write_end(); return;
    }
    if (reason == TDMA_RX_FIRST_NONE && w->produced_before >= TDMA_PIO_SPI_RX_RING_WORDS)
        reason = TDMA_RX_FIRST_RAW_OVERWRITTEN;
    if (reason == TDMA_RX_FIRST_NONE) {
        w->flags |= TDMA_RX_FIRST_RAW_ATTEMPTED;
        w->raw_count = w->physical_frame_words;
        tdma_pio_spi_phys_rx_ring_copy(w->raw, 0u, w->raw_count, 0u);
        __dmb();
        reason = tdma_rx_first_raw_observe(phys, &counter, true);
        if (reason == TDMA_RX_FIRST_NONE && !tdma_rx_first_current(phys))
            reason = TDMA_RX_FIRST_PERSONA_OR_CONFIG_RETIRED;
        if (reason == TDMA_RX_FIRST_NONE && w->produced_after < w->produced_before)
            reason = TDMA_RX_FIRST_RAW_COUNTER_OR_EPOCH;
        if (reason == TDMA_RX_FIRST_NONE && w->produced_after >= TDMA_PIO_SPI_RX_RING_WORDS)
            reason = TDMA_RX_FIRST_RAW_OVERWRITTEN;
    }
    tdma_rx_first_raw_done(reason);
    tdma_rx_first_write_end();
}

void tdma_rx_first_window_event(tdma_pio_spi_phys_t *phys, size_t count)
{
    tdma_rx_first_window_t *w = &s_tdma_rx_first_window;
    if ((w->flags & (TDMA_RX_FIRST_PRELAUNCH_ACTIVE | TDMA_RX_FIRST_RETIRED | TDMA_RX_FIRST_EVENT_DONE)) !=
        TDMA_RX_FIRST_PRELAUNCH_ACTIVE) return;
    const bool current = tdma_rx_first_current(phys);
    if (current && s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && count == 0u) return;
    if (!tdma_rx_first_write_begin()) return;
    w->event_state = s_tdma_event_observer.state;
    w->event_reason = s_tdma_event_observer.reason;
    w->event_fault_bits = s_tdma_event_observer.fault_bits;
    w->flags |= TDMA_RX_FIRST_EVENT_DONE;
    if (!current || s_tdma_event_observer.state != TDMA_EVENT_ACTIVE || count == 0u) {
        tdma_rx_first_reason(TDMA_RX_FIRST_OBSERVER_FAILED);
    } else if (s_tdma_event_records[0].ordinal != 0u ||
        s_tdma_event_records[0].epoch != w->observer_epoch || w->event_word_mask != 31u) {
        tdma_rx_first_reason(TDMA_RX_FIRST_ORDINAL_MISSING);
    } else {
        const tdma_event_record_t *record = &s_tdma_event_records[0];
        w->event_sequence = record->sequence; w->event_ordinal = record->ordinal;
        w->rx_elapsed_cycles = record->rx_elapsed_cycles;
        w->tx_elapsed_cycles = record->tx_elapsed_cycles;
        w->start_lo_cycles = record->start_bounds.lo; w->start_hi_cycles = record->start_bounds.hi;
        w->flags |= TDMA_RX_FIRST_EVENT_VALID;
    }
    tdma_rx_first_write_end();
    if (!current) tdma_rx_first_window_retire(TDMA_RX_FIRST_PERSONA_OR_CONFIG_RETIRED, false);
}
/* RX_FIRST_WINDOW_STORAGE_END */
