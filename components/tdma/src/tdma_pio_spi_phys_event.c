/* Event owner unit, split out of tdma_pio_spi_phys.c (2026-10-25).
 * The code below is the former tdma_pio_spi_phys_event.inc verbatim. */
/* Harnesses that supply their own types/macros define
 * TDMA_PHYS_EVENT_HARNESS and skip this production prologue. */
#ifndef TDMA_PHYS_EVENT_HARNESS
#include "tdma_pio_spi_phys_internal.h"

#include <string.h>

#include "pico.h"
#include "pico/time.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "drv_watchdog.h"
#include "project_config.h"
#include "tdma_service_timing.h"
#include "vdc_timestamp_clock.h"
#include "tdma_pio_spi_phys_timing.h"
#endif

/* Included by the physical owner. Core1 owns observer/applied state; Core0
 * alone writes the guarded STOP tap request. Core0 sees versioned readback;
 * no recorder or filesystem lives here. */
#include "tdma_rx_start_cut.h"
#include "tdma_rx_first_window.h"
#if PROJECT_TDMA_EVENT_OBSERVER && defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "tdma_event_enable_anchor.h"
#endif
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

bool tdma_pio_spi_phys_event_copy(const tdma_pio_spi_phys_t *phys,
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

/* EVENT_TAP_STORAGE_BEGIN -- bounded STOP request and ARM-frozen readback. */
static tdma_pio_spi_event_tap_config_t s_tdma_event_tap_requested;
static uint32_t s_tdma_event_tap_requested_guard;
static tdma_pio_spi_event_tap_config_t s_tdma_event_tap_applied;
static uint32_t s_tdma_event_tap_applied_guard;
static uint32_t s_tdma_event_tap_applied_valid, s_tdma_event_tap_actual_valid;
static uint32_t s_tdma_event_tap_actual_prefix, s_tdma_event_tap_actual_delay;

static tdma_pio_spi_event_tap_config_t tdma_event_tap_load(
    const tdma_pio_spi_event_tap_config_t *source)
{
    const tdma_pio_spi_event_tap_config_t result = {
        .enabled = __atomic_load_n(&source->enabled, __ATOMIC_RELAXED),
        .prefix_bits = __atomic_load_n(&source->prefix_bits, __ATOMIC_RELAXED),
        .sample_delay_cycles = __atomic_load_n(&source->sample_delay_cycles, __ATOMIC_RELAXED),
        .generation = __atomic_load_n(&source->generation, __ATOMIC_RELAXED),
    };
    return result;
}

static void tdma_event_tap_store(tdma_pio_spi_event_tap_config_t *target,
    const tdma_pio_spi_event_tap_config_t *value)
{
    __atomic_store_n(&target->enabled, value->enabled, __ATOMIC_RELAXED);
    __atomic_store_n(&target->prefix_bits, value->prefix_bits, __ATOMIC_RELAXED);
    __atomic_store_n(&target->sample_delay_cycles, value->sample_delay_cycles, __ATOMIC_RELAXED);
    __atomic_store_n(&target->generation, value->generation, __ATOMIC_RELAXED);
}

static bool tdma_event_tap_requested_copy(tdma_pio_spi_event_tap_config_t *out)
{
    for (uint attempt = 0u; attempt < 3u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&s_tdma_event_tap_requested_guard, __ATOMIC_ACQUIRE);
        if ((guard & 1u) != 0u) continue;
        const tdma_pio_spi_event_tap_config_t value = tdma_event_tap_load(&s_tdma_event_tap_requested);
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        if (guard == __atomic_load_n(&s_tdma_event_tap_requested_guard, __ATOMIC_ACQUIRE)) {
            *out = value;
            return true;
        }
    }
    return false;
}

bool tdma_pio_spi_phys_event_tap_set(tdma_pio_spi_phys_t *phys,
    uint32_t enabled, uint32_t prefix_bits, uint32_t sample_delay_cycles)
{
    if (phys == NULL || phys->armed || enabled > 1u ||
        prefix_bits > UINT32_MAX - 32u ||
        sample_delay_cycles > TDMA_PIO_SPI_EVENT_TAP_MAX_DELAY_CYCLES ||
        (enabled == 0u && (prefix_bits != 0u || sample_delay_cycles != 0u))) return false;
    const uint32_t guard = __atomic_load_n(&s_tdma_event_tap_requested_guard, __ATOMIC_RELAXED);
    /* Never wrap the request guard: even a preempted ARM reader cannot see
     * an ABA. The STOP facade supplies the sole-writer/ARM exclusion. */
    if ((guard & 1u) != 0u || guard > UINT32_MAX - 2u ||
        s_tdma_event_tap_requested.generation == UINT32_MAX) return false;
    const tdma_pio_spi_event_tap_config_t value = {
        .enabled = enabled, .prefix_bits = prefix_bits,
        .sample_delay_cycles = sample_delay_cycles,
        .generation = s_tdma_event_tap_requested.generation + 1u,
    };
    (void)__atomic_add_fetch(&s_tdma_event_tap_requested_guard, 1u, __ATOMIC_ACQ_REL);
    tdma_event_tap_store(&s_tdma_event_tap_requested, &value);
    __atomic_store_n(&s_tdma_event_tap_requested_guard, guard + 2u, __ATOMIC_RELEASE);
    return true;
}

bool tdma_pio_spi_phys_event_tap_get(const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_event_tap_snapshot_t *out)
{
    if (phys == NULL || out == NULL) return false;
    /* IRQ pinning bounds the lifetime of the wrapping applied guard, as in
     * event snapshot copy. All shared words are atomic, including payload. */
    const uint32_t interrupts = save_and_disable_interrupts();
    const uint64_t before = time_us_64();
    bool copied = false;
    for (uint attempt = 0u; attempt < 3u; ++attempt) {
        tdma_pio_spi_event_tap_snapshot_t value;
        const uint32_t request_guard = __atomic_load_n(&s_tdma_event_tap_requested_guard, __ATOMIC_ACQUIRE);
        if ((request_guard & 1u) != 0u) continue;
        value.requested = tdma_event_tap_load(&s_tdma_event_tap_requested);
        const uint32_t guard = __atomic_load_n(&s_tdma_event_tap_applied_guard, __ATOMIC_ACQUIRE);
        if ((guard & 1u) != 0u) continue;
        value.applied = tdma_event_tap_load(&s_tdma_event_tap_applied);
        value.applied_valid = __atomic_load_n(&s_tdma_event_tap_applied_valid, __ATOMIC_RELAXED);
        value.actual_valid = __atomic_load_n(&s_tdma_event_tap_actual_valid, __ATOMIC_RELAXED);
        value.actual_prefix_bits = __atomic_load_n(&s_tdma_event_tap_actual_prefix, __ATOMIC_RELAXED);
        value.actual_sample_delay_cycles = __atomic_load_n(&s_tdma_event_tap_actual_delay, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        const uint32_t after_guard = __atomic_load_n(&s_tdma_event_tap_applied_guard, __ATOMIC_ACQUIRE);
        const uint32_t after_request = __atomic_load_n(&s_tdma_event_tap_requested_guard, __ATOMIC_ACQUIRE);
        const uint64_t after = time_us_64();
        if (after < before || after - before > 1000u) break;
        if (guard == after_guard && request_guard == after_request) {
            *out = value;
            copied = true;
            break;
        }
    }
    restore_interrupts(interrupts);
    return copied;
}

static bool tdma_event_tap_freeze(const tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config, bool selected)
{
    tdma_pio_spi_event_tap_config_t value = {0};
    const bool copied = tdma_event_tap_requested_copy(&value);
    const bool valid = copied && config != NULL && (value.enabled == 0u ||
        (selected && config->geometry_generation == 0u && phys->baud_hz != 0u &&
         (uint64_t)value.prefix_bits + 32u <= (uint64_t)phys->flight_physical_byte_count * 8u));
    (void)__atomic_add_fetch(&s_tdma_event_tap_applied_guard, 1u, __ATOMIC_ACQ_REL);
    tdma_event_tap_store(&s_tdma_event_tap_applied, &value);
    __atomic_store_n(&s_tdma_event_tap_applied_valid, valid, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_event_tap_actual_valid, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_event_tap_actual_prefix, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_event_tap_actual_delay, 0u, __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&s_tdma_event_tap_applied_guard, 1u, __ATOMIC_RELEASE);
    return valid;
}

static void tdma_event_tap_loaded(uint32_t prefix, uint32_t delay)
{
    (void)__atomic_add_fetch(&s_tdma_event_tap_applied_guard, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&s_tdma_event_tap_actual_prefix, prefix, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_event_tap_actual_delay, delay, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_event_tap_actual_valid, 1u, __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&s_tdma_event_tap_applied_guard, 1u, __ATOMIC_RELEASE);
}
/* EVENT_TAP_STORAGE_END */

/* EVENT_RECOVERY_STORAGE_BEGIN -- Core1 state and atomic readback only. */
static tdma_pio_spi_event_recovery_snapshot_t s_tdma_event_recovery;
static uint32_t s_tdma_event_recovery_words[18];
static uint32_t s_tdma_event_recovery_guard;
_Static_assert(sizeof(tdma_pio_spi_event_recovery_snapshot_t) ==
    sizeof(s_tdma_event_recovery_words), "Recovery readback contains only u32 words");
_Static_assert(sizeof(s_tdma_event_recovery) + sizeof(s_tdma_event_recovery_words) +
    sizeof(s_tdma_event_recovery_guard) <= 160u, "Recovery storage is bounded");

static void tdma_event_recovery_increment(uint32_t *value)
{
    if (*value != UINT32_MAX) ++*value;
}

static void tdma_event_recovery_publish(void)
{
    uint32_t words[18];
    memcpy(words, &s_tdma_event_recovery, sizeof(words));
    (void)__atomic_add_fetch(&s_tdma_event_recovery_guard, 1u, __ATOMIC_ACQ_REL);
    for (uint i = 0u; i < 18u; ++i)
        __atomic_store_n(&s_tdma_event_recovery_words[i], words[i], __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&s_tdma_event_recovery_guard, 1u, __ATOMIC_RELEASE);
}

bool tdma_pio_spi_phys_event_recovery_get(const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_event_recovery_snapshot_t *out)
{
    if (phys == NULL || out == NULL) return false;
    const uint32_t interrupts = save_and_disable_interrupts();
    const uint64_t before = time_us_64();
    bool copied = false;
    for (uint attempt = 0u; attempt < 3u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&s_tdma_event_recovery_guard, __ATOMIC_ACQUIRE);
        if ((guard & 1u) != 0u) continue;
        uint32_t words[18];
        for (uint i = 0u; i < 18u; ++i)
            words[i] = __atomic_load_n(&s_tdma_event_recovery_words[i], __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        const uint32_t after_guard = __atomic_load_n(&s_tdma_event_recovery_guard, __ATOMIC_ACQUIRE);
        const uint64_t after = time_us_64();
        if (after < before || after - before > 1000u) break;
        if (guard == after_guard) {
            memcpy(out, words, sizeof(words));
            copied = true;
            break;
        }
    }
    restore_interrupts(interrupts);
    return copied;
}

static void tdma_event_recovery_cancel(uint32_t reason)
{
    if (s_tdma_event_recovery.pending == TDMA_EVENT_RECOVERY_NONE) return;
    s_tdma_event_recovery.pending = TDMA_EVENT_RECOVERY_NONE;
    s_tdma_event_recovery.cancel_reason = reason;
    tdma_event_recovery_increment(&s_tdma_event_recovery.cancel_count);
    tdma_event_recovery_publish();
}
/* EVENT_RECOVERY_STORAGE_END */

#define TDMA_EVENT_RX_SM BOARD_TDMA_TX_RTT_EVIDENCE_SM
#define TDMA_EVENT_TX_SM BOARD_TDMA_TX_CLOCK_LATCH_SM
#define TDMA_EVENT_SEQUENCE_SM BOARD_TDMA_TX_DATA_CAPTURE_SM
#define TDMA_EVENT_SM_MASK ((1u << TDMA_EVENT_RX_SM) | (1u << TDMA_EVENT_TX_SM) | \
                           (1u << TDMA_EVENT_SEQUENCE_SM))
_Static_assert(TDMA_EVENT_RX_SM == 1u && TDMA_EVENT_TX_SM == 2u &&
               TDMA_EVENT_SEQUENCE_SM == 3u && BOARD_TDMA_TX_CONTROL_OUT_SM == 0u,
               "Relative IRQ layout must match the paired observer programs");

void tdma_pio_spi_phys_rx_ring_copy(uint8_t *destination,
    uint64_t produced, uint32_t count, uint32_t bit_shift);
tdma_event_observer_t s_tdma_event_observer;
static tdma_event_history_t s_tdma_event_history;
static tdma_event_batch_t s_tdma_event_batch;
tdma_event_record_t s_tdma_event_records[TDMA_EVENT_MAX_RECORDS];

/* Wire-sequence witness readback (VDC-ISO-001). The counters live in the
 * observer's own state, so no published snapshot layout changes; a maintenance
 * reader takes them through this bounded accessor. */
bool tdma_pio_spi_phys_event_sequence_copy(
    const tdma_pio_spi_phys_t *phys, tdma_pio_spi_event_sequence_evidence_t *out) {
    (void)phys;
    if (out == NULL) return false;
    tdma_event_sequence_evidence_t evidence;
    tdma_event_observer_sequence_evidence(&s_tdma_event_observer, &evidence);
    *out = (tdma_pio_spi_event_sequence_evidence_t){
        .forward_count = evidence.forward_count,
        .step_max = evidence.step_max,
        .repeat_count = evidence.repeat_count,
        .wrap_count = evidence.wrap_count,
        .backwards_count = evidence.backwards_count,
        .zero_count = evidence.zero_count,
        .ones_count = evidence.ones_count,
        .refused = evidence.refused,
        .last = evidence.last,
        .history_active = s_tdma_event_history.active ? 1u : 0u,
        .history_epoch = s_tdma_event_history.epoch,
        .history_count = s_tdma_event_history.count,
        .history_reason = (uint32_t)s_tdma_event_history.reason,
        .observer_epoch = s_tdma_event_observer.epoch,
        .armed = phys->armed ? 1u : 0u,
        .arm_valid = s_tdma_pio_spi_rx_arm_valid ? 1u : 0u,
        .arm_epoch_lo = (uint32_t)s_tdma_pio_spi_rx_arm_epoch,
        .live_epoch = 0u
    };
    return true;
}

/* The Core1 dispatcher is deliberately allowed to spend a whole static
 * phase on another owner.  A PIO RX FIFO is only eight words deep, so a
 * delayed observer service used to turn an ordinary scheduler gap into a
 * sticky RXSTALL.  DMA drains each observer SM continuously into an SRAM
 * ring; Core1 consumes the ring at the existing service boundary.  The ring
 * is large enough for the bounded scheduler gap while keeping the PIO and
 * timestamp paths unchanged. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TDMA_EVENT_DMA_RING_WORDS 64u
#define TDMA_EVENT_DMA_RING_BYTES \
    (TDMA_EVENT_DMA_RING_WORDS * sizeof(uint32_t))
#define TDMA_EVENT_DMA_RING_BITS 8u
#define TDMA_EVENT_DMA_TRANSFERS DMA_CH0_TRANS_COUNT_COUNT_BITS
typedef struct {
    int channel;
    uint32_t produced;
    uint32_t consumed;
    bool running;
} tdma_event_dma_stream_t;
static volatile uint32_t __attribute__((aligned(TDMA_EVENT_DMA_RING_BYTES)))
    s_tdma_event_dma_ring[TDMA_EVENT_STREAMS][TDMA_EVENT_DMA_RING_WORDS];
static tdma_event_dma_stream_t s_tdma_event_dma[TDMA_EVENT_STREAMS] = {
    {-1, 0u, 0u, false}, {-1, 0u, 0u, false},
    {-1, 0u, 0u, false}};
bool s_tdma_event_dma_active;

static uint32_t tdma_event_dma_produced(uint stream)
{
    tdma_event_dma_stream_t *state = &s_tdma_event_dma[stream];
    if (!state->running || state->channel < 0) return state->produced;
    const uint32_t transfer_count = dma_hw->ch[(uint)state->channel].transfer_count;
    const uint32_t ctrl = dma_hw->ch[(uint)state->channel].ctrl_trig;
    const uint32_t required = DMA_CH0_CTRL_TRIG_EN_BITS | DMA_CH0_CTRL_TRIG_BUSY_BITS;
    const uint32_t errors = DMA_CH0_CTRL_TRIG_READ_ERROR_BITS |
                            DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    /* NORMAL mode decrements only after each SRAM write completes. Unlike
     * ENDLESS or a modulo write pointer, it exposes complete ring overruns.
     * Exhaustion retires this epoch; never reload underneath the consumer. */
    if ((transfer_count & DMA_CH0_TRANS_COUNT_MODE_BITS) != 0u ||
        transfer_count == 0u || transfer_count > TDMA_EVENT_DMA_TRANSFERS ||
        (ctrl & required) != required || (ctrl & errors) != 0u ||
        TDMA_EVENT_DMA_TRANSFERS - transfer_count < state->produced) {
        state->running = false;
        return state->produced;
    }
    state->produced = TDMA_EVENT_DMA_TRANSFERS - transfer_count;
    return state->produced;
}

static bool tdma_event_dma_stop(void)
{
    uint32_t mask = 0u;
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream)
        if (s_tdma_event_dma[stream].channel >= 0)
            mask |= 1u << (uint)s_tdma_event_dma[stream].channel;
    if (!tdma_pio_spi_phys_stop_dma_level(mask,
            time_us_64() + TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US)) return false;
    __dmb();
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        tdma_event_dma_stream_t *state = &s_tdma_event_dma[stream];
        if (state->channel >= 0)
            dma_channel_unclaim((uint)state->channel);
        *state = (tdma_event_dma_stream_t){-1, 0u, 0u, false};
    }
    s_tdma_event_dma_active = false;
    return true;
}

static bool tdma_event_dma_start(const PIO pio)
{
    if (s_tdma_event_dma_active) return false;
    const uint sms[TDMA_EVENT_STREAMS] = {
        TDMA_EVENT_RX_SM, TDMA_EVENT_TX_SM, TDMA_EVENT_SEQUENCE_SM};
    int claimed[TDMA_EVENT_STREAMS] = {-1, -1, -1};
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        claimed[stream] = dma_claim_unused_channel(false);
        if (claimed[stream] < 0) {
            for (uint i = 0u; i < stream; ++i)
                dma_channel_unclaim((uint)claimed[i]);
            return false;
        }
    }
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        tdma_event_dma_stream_t *state = &s_tdma_event_dma[stream];
        state->channel = claimed[stream];
        state->produced = 0u;
        state->consumed = 0u;
        state->running = true;
        dma_channel_config config = dma_channel_get_default_config(
            (uint)state->channel);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
        channel_config_set_read_increment(&config, false);
        channel_config_set_write_increment(&config, true);
        channel_config_set_ring(&config, true, TDMA_EVENT_DMA_RING_BITS);
        channel_config_set_dreq(&config, pio_get_dreq(pio, sms[stream], false));
        dma_channel_configure((uint)state->channel, &config,
            s_tdma_event_dma_ring[stream], &pio->rxf[sms[stream]],
            TDMA_EVENT_DMA_TRANSFERS, false);
    }
    dma_start_channel_mask((1u << (uint)claimed[0]) |
                           (1u << (uint)claimed[1]) |
                           (1u << (uint)claimed[2]));
    s_tdma_event_dma_active = true;
    return true;
}

static uint32_t tdma_event_dma_available(uint stream, bool *overrun)
{
    tdma_event_dma_stream_t *state = &s_tdma_event_dma[stream];
    const uint32_t produced = tdma_event_dma_produced(stream);
    const uint32_t available = produced - state->consumed;
    if (overrun != NULL) *overrun = !state->running ||
        available >= TDMA_EVENT_DMA_RING_WORDS;
    return available;
}

static void tdma_event_dma_harvest(tdma_event_batch_t *batch,
    const tdma_event_observer_t *observer, uint32_t *maxima[TDMA_EVENT_STREAMS])
{
    for (uint stream = 0u; stream < TDMA_EVENT_STREAMS; ++stream) {
        tdma_event_dma_stream_t *state = &s_tdma_event_dma[stream];
        bool overrun = false;
        const uint32_t available = tdma_event_dma_available(stream, &overrun);
        if (available > *maxima[stream]) *maxima[stream] = available;
        if (overrun || observer->pending_count[stream] > TDMA_EVENT_FIFO_WORDS) {
            batch->pre_faults |= TDMA_EVENT_FAULT_OVERFLOW;
            continue;
        }
        const uint32_t free = TDMA_EVENT_FIFO_WORDS - observer->pending_count[stream];
        const uint32_t count = available > free ? free : available;
        const uint32_t consumed = state->consumed;
        __dmb();
        for (uint i = 0u; i < count; ++i)
            batch->words[stream][i] = s_tdma_event_dma_ring[stream][
                (consumed + i) & (TDMA_EVENT_DMA_RING_WORDS - 1u)];
        __dmb();
        (void)tdma_event_dma_available(stream, &overrun);
        if (overrun) {
            batch->post_faults |= TDMA_EVENT_FAULT_OVERFLOW;
            continue;
        }
        state->consumed = consumed + count;
        batch->count[stream] = (uint8_t)count;
        /* Never assert FIFO empty from a DMA count/address observation: an
         * older FIFO word may still be in flight to SRAM. Keep the age lower
         * bound conservative; an ambiguous long epoch is rejected by lift. */
    }
}
#else
bool s_tdma_event_dma_active;
void tdma_event_dma_stop(void) { s_tdma_event_dma_active = false; }
#endif
uint32_t s_tdma_event_epoch;
uint32_t s_tdma_event_hz;
static uint32_t s_tdma_event_period_ns;
uint64_t s_tdma_event_base_us;
static uint64_t s_tdma_event_last_service_us;
static uint64_t s_tdma_event_arm_epoch;
static bool s_tdma_event_waiting;

/* EVENT_LIVE_STORAGE_BEGIN -- one atomic publication, no second owner copy. */
static uint32_t s_tdma_event_live_words[24];
static uint32_t s_tdma_event_live_guard;
static struct {
    uint64_t before, after, arm_epoch;
    uint32_t epoch, flags;
} s_tdma_event_live_anchor;
_Static_assert(sizeof(tdma_pio_spi_event_live_snapshot_t) ==
    sizeof(s_tdma_event_live_words), "Live record snapshot has a fixed word layout");
_Static_assert(sizeof(s_tdma_event_live_words) + sizeof(s_tdma_event_live_guard) +
    sizeof(s_tdma_event_live_anchor) <= 160u, "Live event storage is bounded");
enum { TDMA_EVENT_LIVE_CANCELLED = TDMA_EVENT_LIVE_STOP | TDMA_EVENT_LIVE_INVALID |
    TDMA_EVENT_LIVE_ARM | TDMA_EVENT_LIVE_CLOCK | TDMA_EVENT_LIVE_BINDING };

void tdma_event_live_anchor_reset(void)
{
    memset(&s_tdma_event_live_anchor, 0, sizeof(s_tdma_event_live_anchor));
}

void tdma_event_live_retire(uint32_t reason)
{
    /* Ordinary service keeps running after STOP. A later idle/clock check
     * must not relabel an already retired historical publication. */
    if ((reason == TDMA_EVENT_LIVE_BINDING || reason == TDMA_EVENT_LIVE_CLOCK) &&
        (s_tdma_event_live_anchor.epoch == 0u ||
         (s_tdma_event_live_anchor.flags & TDMA_EVENT_LIVE_CANCELLED) != 0u)) return;
    s_tdma_event_live_anchor.flags =
        (s_tdma_event_live_anchor.flags & ~TDMA_EVENT_LIVE_ANCHOR_VALID) | reason;
    const uint index = offsetof(tdma_pio_spi_event_live_snapshot_t, flags) / sizeof(uint32_t);
    const uint32_t old = __atomic_load_n(&s_tdma_event_live_words[index], __ATOMIC_RELAXED);
    if ((old & TDMA_EVENT_LIVE_RETAINED) == 0u) return;
    const uint32_t next = (old & ~TDMA_EVENT_LIVE_ACTIVE) | reason;
    if (old == next) return;
    (void)__atomic_add_fetch(&s_tdma_event_live_guard, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&s_tdma_event_live_words[index], next, __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&s_tdma_event_live_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_event_live_monitor(void)
{
    if (s_tdma_event_live_anchor.epoch == 0u ||
        (s_tdma_event_live_anchor.flags & TDMA_EVENT_LIVE_CANCELLED) != 0u) return;
    if (s_tdma_event_live_anchor.epoch != 0u &&
        (!s_tdma_pio_spi_rx_arm_valid ||
         s_tdma_event_live_anchor.arm_epoch != s_tdma_pio_spi_rx_arm_epoch ||
         s_tdma_event_live_anchor.epoch != s_tdma_event_observer.epoch))
        tdma_event_live_retire(TDMA_EVENT_LIVE_BINDING);
    if (s_tdma_event_live_anchor.epoch != 0u &&
        (s_tdma_event_live_anchor.flags & TDMA_EVENT_LIVE_CLOCK) == 0u &&
        !vdc_timestamp_clock_is_current(s_tdma_event_hz))
        tdma_event_live_retire(TDMA_EVENT_LIVE_CLOCK);
}

static void tdma_event_live_bind(uint64_t before, uint64_t after, bool sampled)
{
    /* Private new-epoch anchor: the old exported record keeps its own body
     * until a complete record from this epoch survives the final check. */
    s_tdma_event_live_anchor.before = before;
    s_tdma_event_live_anchor.after = after;
    s_tdma_event_live_anchor.arm_epoch = s_tdma_pio_spi_rx_arm_valid
        ? s_tdma_pio_spi_rx_arm_epoch : 0u;
    s_tdma_event_live_anchor.epoch = s_tdma_event_observer.epoch;
    s_tdma_event_live_anchor.flags = sampled && after >= before
        ? TDMA_EVENT_LIVE_ANCHOR_VALID : TDMA_EVENT_LIVE_ANCHOR_UNAVAILABLE;
    if (!vdc_timestamp_clock_is_current(s_tdma_event_hz))
        s_tdma_event_live_anchor.flags = TDMA_EVENT_LIVE_CLOCK;
    if (s_tdma_event_observer.state != TDMA_EVENT_ACTIVE)
        s_tdma_event_live_anchor.flags |= TDMA_EVENT_LIVE_INVALID;
}

static void tdma_event_live_record(const tdma_event_record_t *record)
{
    tdma_event_live_monitor();
    if (record == NULL || record->epoch != s_tdma_event_live_anchor.epoch ||
        !s_tdma_pio_spi_rx_arm_valid || s_tdma_event_live_anchor.arm_epoch == 0u ||
        s_tdma_event_live_anchor.arm_epoch != s_tdma_pio_spi_rx_arm_epoch) {
        tdma_event_live_retire(TDMA_EVENT_LIVE_BINDING);
        return;
    }
    if ((s_tdma_event_live_anchor.flags & TDMA_EVENT_LIVE_CANCELLED) != 0u) return;
    const union {
        tdma_pio_spi_event_live_snapshot_t snapshot;
        uint32_t words[24];
    } next = {.snapshot = {
        .record = *record,
        .arm_epoch = s_tdma_event_live_anchor.arm_epoch,
        .timer1_enable_before = s_tdma_event_live_anchor.before,
        .timer1_enable_after = s_tdma_event_live_anchor.after,
        .tick_hz = s_tdma_event_hz,
        .flags = TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE |
            s_tdma_event_live_anchor.flags,
    }};
    (void)__atomic_add_fetch(&s_tdma_event_live_guard, 1u, __ATOMIC_ACQ_REL);
    for (uint i = 0u; i < 24u; ++i)
        __atomic_store_n(&s_tdma_event_live_words[i], next.words[i], __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&s_tdma_event_live_guard, 1u, __ATOMIC_RELEASE);
}

bool tdma_pio_spi_phys_event_get_live_snapshot(const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_event_live_snapshot_t *out)
{
    if (phys == NULL || out == NULL) return false;
    const uint32_t interrupts = save_and_disable_interrupts();
    const uint64_t before = time_us_64();
    bool copied = false;
    for (uint attempt = 0u; attempt < 3u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&s_tdma_event_live_guard, __ATOMIC_ACQUIRE);
        if ((guard & 1u) != 0u) continue;
        uint32_t words[24];
        for (uint i = 0u; i < 24u; ++i)
            words[i] = __atomic_load_n(&s_tdma_event_live_words[i], __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        const uint32_t after_guard = __atomic_load_n(&s_tdma_event_live_guard, __ATOMIC_ACQUIRE);
        const uint64_t after = time_us_64();
        if (after < before || after - before > 1000u) break;
        if (guard == after_guard) {
            memcpy(out, words, sizeof(words));
            copied = true;
            break;
        }
    }
    restore_interrupts(interrupts);
    return copied;
}
/* EVENT_LIVE_STORAGE_END */

/* EVENT_HISTORY_WINDOW_BEGIN -- no second static history or hardware reads. */
tdma_event_window_result_t tdma_pio_spi_phys_event_copy_history_window(
    const tdma_pio_spi_phys_t *phys, uint32_t expected_observer_epoch,
    uint64_t next_ordinal, tdma_pio_spi_event_window_t *out)
{
    const uint64_t ordinal_end = (uint64_t)UINT32_MAX + 1u;
    if (phys == NULL || out == NULL || next_ordinal > ordinal_end ||
        (expected_observer_epoch == 0u && next_ordinal != 0u))
        return TDMA_EVENT_WINDOW_BAD_ARGUMENT;
    const uint32_t interrupts = save_and_disable_interrupts();
    const uint64_t before = time_us_64();
    const uint32_t history_guard = __atomic_load_n(
        &s_tdma_event_history.publication_guard, __ATOMIC_ACQUIRE);
    const uint32_t live_guard = __atomic_load_n(&s_tdma_event_live_guard, __ATOMIC_ACQUIRE);
    tdma_event_window_result_t result = TDMA_EVENT_WINDOW_BUSY;
    tdma_pio_spi_event_window_t candidate = {0};
    if (((history_guard | live_guard) & 1u) == 0u) {
        do {
            const bool active = __atomic_load_n(&s_tdma_event_history.active, __ATOMIC_RELAXED);
            const uint32_t epoch = __atomic_load_n(&s_tdma_event_history.epoch, __ATOMIC_RELAXED);
            const uint32_t hz = __atomic_load_n(&s_tdma_event_history.pio_hz, __ATOMIC_RELAXED);
            const uint32_t count = __atomic_load_n(&s_tdma_event_history.count, __ATOMIC_RELAXED);
            const uint32_t next = __atomic_load_n(&s_tdma_event_history.next, __ATOMIC_RELAXED);
            const uint32_t oldest = __atomic_load_n(&s_tdma_event_history.oldest_ordinal, __ATOMIC_RELAXED);
            if (expected_observer_epoch != 0u && expected_observer_epoch != epoch) {
                result = TDMA_EVENT_WINDOW_EPOCH_CHANGED; break;
            }
            if (!active) { result = TDMA_EVENT_WINDOW_UNAVAILABLE; break; }
            if (epoch == 0u || hz == 0u || count > TDMA_EVENT_HISTORY_CAPACITY ||
                next >= TDMA_EVENT_HISTORY_CAPACITY ||
                (count == 0u && (oldest != UINT32_MAX || next != 0u))) {
                result = TDMA_EVENT_WINDOW_BAD_STATE; break;
            }
            union { tdma_pio_spi_event_live_snapshot_t snapshot; uint32_t words[24]; } live;
            for (uint i = 0u; i < 24u; ++i)
                live.words[i] = __atomic_load_n(&s_tdma_event_live_words[i], __ATOMIC_RELAXED);
            const uint32_t required = TDMA_EVENT_LIVE_RETAINED |
                TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID;
            /* The LIVE record may precede this history batch; only its immutable
             * same-epoch enable anchor is borrowed, never its event identity. */
            if (live.snapshot.record.epoch != epoch || live.snapshot.tick_hz != hz ||
                live.snapshot.arm_epoch == 0u || (live.snapshot.flags & required) != required ||
                (live.snapshot.flags & (TDMA_EVENT_LIVE_CANCELLED | TDMA_EVENT_LIVE_ANCHOR_UNAVAILABLE)) != 0u ||
                live.snapshot.timer1_enable_before > live.snapshot.timer1_enable_after) {
                result = TDMA_EVENT_WINDOW_ANCHOR_UNAVAILABLE; break;
            }
            const uint64_t end = count != 0u ? (uint64_t)oldest + count : 0u;
            if (end > ordinal_end) { result = TDMA_EVENT_WINDOW_BAD_STATE; break; }
            if (next_ordinal > end) { result = TDMA_EVENT_WINDOW_BAD_ARGUMENT; break; }
            const uint64_t first = count != 0u && next_ordinal < oldest ? oldest : next_ordinal;
            candidate.arm_epoch = live.snapshot.arm_epoch;
            candidate.timer1_enable_before = live.snapshot.timer1_enable_before;
            candidate.timer1_enable_after = live.snapshot.timer1_enable_after;
            candidate.observer_epoch = epoch;
            candidate.tick_hz = hz;
            candidate.oldest_ordinal = oldest;
            candidate.available_end_ordinal = end;
            candidate.lost_count = (uint32_t)(first - next_ordinal);
            candidate.count = (uint32_t)(end - first);
            if (candidate.count > TDMA_EVENT_HISTORY_WINDOW_CAPACITY)
                candidate.count = TDMA_EVENT_HISTORY_WINDOW_CAPACITY;
            candidate.next_ordinal = first + candidate.count;
            candidate.flags = live.snapshot.flags;
            candidate.diagnostic_only = true;
            candidate.physical_first_unproved = true;
            candidate.identity_unproved = true;
            result = TDMA_EVENT_WINDOW_OK;
            const uint32_t ring_oldest = (next + TDMA_EVENT_HISTORY_CAPACITY - count) &
                (TDMA_EVENT_HISTORY_CAPACITY - 1u);
            for (uint32_t i = 0u; i < candidate.count; ++i) {
                const uint32_t index = (ring_oldest + (uint32_t)(first - oldest) + i) &
                    (TDMA_EVENT_HISTORY_CAPACITY - 1u);
                tdma_event_history_record_t *record = &candidate.records[i];
                for (uint32_t j = 0u; j < 8u; ++j)
                    record->words[j] = __atomic_load_n(
                        &s_tdma_event_history.records[index].words[j], __ATOMIC_RELAXED);
                if ((uint64_t)record->ordinal != first + i ||
                    record->rx_elapsed_cycles == 0u ||
                    record->tx_elapsed_cycles < record->rx_elapsed_cycles ||
                    candidate.timer1_enable_after > UINT64_MAX - record->tx_elapsed_cycles ||
                    (i != 0u && (record->sequence != (uint32_t)(candidate.records[i - 1u].sequence + 1u) ||
                        record->rx_elapsed_cycles <= candidate.records[i - 1u].rx_elapsed_cycles ||
                        record->tx_elapsed_cycles <= candidate.records[i - 1u].tx_elapsed_cycles))) {
                    result = TDMA_EVENT_WINDOW_BAD_STATE; break;
                }
            }
        } while (false);
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        const uint32_t history_after = __atomic_load_n(
            &s_tdma_event_history.publication_guard, __ATOMIC_ACQUIRE);
        const uint32_t live_after = __atomic_load_n(&s_tdma_event_live_guard, __ATOMIC_ACQUIRE);
        const uint64_t after = time_us_64();
        /* Like LIVE, exclude a full uint32 guard ABA within one copy. Local
         * IRQ masking bounds preemption; > 1000 us is rejected, never waited. */
        if (history_guard != history_after || live_guard != live_after ||
            after < before || after - before > 1000u) result = TDMA_EVENT_WINDOW_BUSY;
        if (result == TDMA_EVENT_WINDOW_OK) *out = candidate;
    }
    restore_interrupts(interrupts);
    return result;
}
/* EVENT_HISTORY_WINDOW_END */

/* EVENT_HISTORY_EXACT_BEGIN -- Core1 foreground; no second history/cache. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TDMA_EVENT_EXACT_RAM __not_in_flash("tdma_event_exact") __attribute__((noinline, noclone))
#else
#define TDMA_EVENT_EXACT_RAM
#endif

TDMA_EVENT_EXACT_RAM
tdma_event_exact_result_t tdma_pio_spi_phys_event_copy_history_exact(
    const tdma_pio_spi_phys_t *phys, uint64_t expected_arm_epoch,
    uint32_t expected_observer_epoch, uint32_t source_sequence,
    tdma_pio_spi_event_exact_t *out)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (get_core_num() != 1u) return TDMA_EVENT_EXACT_BAD_ARGUMENT;
#endif
    if (phys == NULL || out == NULL || expected_arm_epoch == 0u || expected_observer_epoch == 0u)
        return TDMA_EVENT_EXACT_BAD_ARGUMENT;
    /* History and LIVE are written only by the same Core1 foreground owner:
     * service harvest, ARM, STOP and observer recovery. The capture IRQ writes
     * only its priority lane, never these publications. It may preempt this
     * bounded reader, but cannot advance either guard, so no IRQ mask or time
     * sample is needed to exclude a guard ABA. Retain both guard checks to
     * reject any inconsistent publication; this API is not a Core0 reader. */
    const uint32_t history_guard = __atomic_load_n(
        &s_tdma_event_history.publication_guard, __ATOMIC_ACQUIRE);
    const uint32_t live_guard = __atomic_load_n(&s_tdma_event_live_guard, __ATOMIC_ACQUIRE);
    if ((history_guard | live_guard) & 1u) return TDMA_EVENT_EXACT_BUSY;
    union {
        tdma_pio_spi_event_exact_t snapshot;
        uint32_t words[sizeof(tdma_pio_spi_event_exact_t) / sizeof(uint32_t)];
    } candidate = {0};
    tdma_event_exact_result_t result = TDMA_EVENT_EXACT_RETIRED;
    do {
        const bool active = __atomic_load_n(&s_tdma_event_history.active, __ATOMIC_RELAXED);
        const uint32_t epoch = __atomic_load_n(&s_tdma_event_history.epoch, __ATOMIC_RELAXED);
        const uint32_t hz = __atomic_load_n(&s_tdma_event_history.pio_hz, __ATOMIC_RELAXED);
        const uint32_t count = __atomic_load_n(&s_tdma_event_history.count, __ATOMIC_RELAXED);
        const uint32_t next = __atomic_load_n(&s_tdma_event_history.next, __ATOMIC_RELAXED);
        const uint32_t oldest = __atomic_load_n(&s_tdma_event_history.oldest_ordinal, __ATOMIC_RELAXED);
        if (!active || epoch != expected_observer_epoch || !phys->armed ||
            !s_tdma_pio_spi_rx_arm_valid || s_tdma_pio_spi_rx_arm_epoch != expected_arm_epoch)
            break;
        if (hz == 0u || count > TDMA_EVENT_HISTORY_CAPACITY || next >= TDMA_EVENT_HISTORY_CAPACITY ||
            (count == 0u && (oldest != UINT32_MAX || next != 0u))) {
            result = TDMA_EVENT_EXACT_BAD_STATE; break;
        }
        /* Read only LIVE's lifetime/anchor, not its possibly older event. */
        const uint32_t live_epoch = __atomic_load_n(&s_tdma_event_live_words[
            (offsetof(tdma_pio_spi_event_live_snapshot_t, record) +
             offsetof(tdma_event_record_t, epoch)) / sizeof(uint32_t)], __ATOMIC_RELAXED);
        const uint32_t live_hz = __atomic_load_n(&s_tdma_event_live_words[
            offsetof(tdma_pio_spi_event_live_snapshot_t, tick_hz) / sizeof(uint32_t)], __ATOMIC_RELAXED);
        const uint32_t flags = __atomic_load_n(&s_tdma_event_live_words[
            offsetof(tdma_pio_spi_event_live_snapshot_t, flags) / sizeof(uint32_t)], __ATOMIC_RELAXED);
        const uint32_t required = TDMA_EVENT_LIVE_RETAINED |
            TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID;
        if (live_epoch != epoch || live_hz != hz || (flags & required) != required ||
            (flags & (TDMA_EVENT_LIVE_CANCELLED | TDMA_EVENT_LIVE_ANCHOR_UNAVAILABLE)) != 0u)
            break;
        _Static_assert(offsetof(tdma_pio_spi_event_live_snapshot_t, timer1_enable_after) ==
            offsetof(tdma_pio_spi_event_live_snapshot_t, arm_epoch) + 2u * sizeof(uint64_t) &&
            offsetof(tdma_pio_spi_event_exact_t, timer1_enable_after) ==
            offsetof(tdma_pio_spi_event_exact_t, arm_epoch) + 2u * sizeof(uint64_t),
            "ARM and TIMER1 anchor must be six consecutive words");
        for (uint32_t i = 0u; i < 6u; ++i)
            candidate.words[offsetof(tdma_pio_spi_event_exact_t, arm_epoch) / sizeof(uint32_t) + i] =
                __atomic_load_n(&s_tdma_event_live_words[
                    offsetof(tdma_pio_spi_event_live_snapshot_t, arm_epoch) / sizeof(uint32_t) + i],
                    __ATOMIC_RELAXED);
        if (candidate.snapshot.arm_epoch != expected_arm_epoch) break;
        if (candidate.snapshot.timer1_enable_before > candidate.snapshot.timer1_enable_after) {
            result = TDMA_EVENT_EXACT_BAD_STATE; break;
        }
        if (count == 0u) { result = TDMA_EVENT_EXACT_PENDING; break; }
        const uint64_t end = (uint64_t)oldest + count;
        if (end > (uint64_t)UINT32_MAX + 1u) {
            result = TDMA_EVENT_EXACT_BAD_STATE; break;
        }
        const uint32_t newest = (next + TDMA_EVENT_HISTORY_CAPACITY - 1u) &
            (TDMA_EVENT_HISTORY_CAPACITY - 1u);
        const uint32_t newest_sequence = __atomic_load_n(
            &s_tdma_event_history.records[newest].sequence, __ATOMIC_RELAXED);
        const uint32_t newest_ordinal = __atomic_load_n(
            &s_tdma_event_history.records[newest].ordinal, __ATOMIC_RELAXED);
        if ((uint64_t)newest_ordinal != end - 1u || newest_sequence < count - 1u) {
            result = TDMA_EVENT_EXACT_BAD_STATE; break;
        }
        /* Production observer epochs never span sequence wrap, unlike the
         * generic history container. Never alias a future/high source word
         * onto a retained slot through uint32 subtraction wrap. */
        if (source_sequence > newest_sequence) { result = TDMA_EVENT_EXACT_PENDING; break; }
        const uint32_t age = newest_sequence - source_sequence;
        if (age >= count) {
            result = TDMA_EVENT_EXACT_EVICTED; break;
        }
        if (newest_ordinal < age) { result = TDMA_EVENT_EXACT_BAD_STATE; break; }
        const uint32_t index = (next + TDMA_EVENT_HISTORY_CAPACITY - 1u - age) &
            (TDMA_EVENT_HISTORY_CAPACITY - 1u);
        for (uint32_t i = 0u; i < 8u; ++i)
            candidate.snapshot.record.words[i] = __atomic_load_n(
                &s_tdma_event_history.records[index].words[i], __ATOMIC_RELAXED);
        const tdma_event_history_record_t *record = &candidate.snapshot.record;
        if (record->sequence != source_sequence || record->ordinal != newest_ordinal - age ||
            record->rx_elapsed_cycles == 0u || record->tx_elapsed_cycles < record->rx_elapsed_cycles ||
            candidate.snapshot.timer1_enable_after > UINT64_MAX - record->tx_elapsed_cycles) {
            result = TDMA_EVENT_EXACT_BAD_STATE; break;
        }
        candidate.snapshot.observer_epoch = epoch;
        candidate.snapshot.tick_hz = hz;
        candidate.snapshot.flags = flags;
        result = TDMA_EVENT_EXACT_OK;
    } while (false);
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (history_guard != __atomic_load_n(&s_tdma_event_history.publication_guard, __ATOMIC_ACQUIRE) ||
        live_guard != __atomic_load_n(&s_tdma_event_live_guard, __ATOMIC_ACQUIRE))
        return TDMA_EVENT_EXACT_BUSY;
    if (result == TDMA_EVENT_EXACT_OK) *out = candidate.snapshot;
    return result;
}
#undef TDMA_EVENT_EXACT_RAM
/* EVENT_HISTORY_EXACT_END */

/* Separate hypothesis: never install frozen DMA offsets as live alignment. */
tdma_frozen_geometry_snapshot_t s_tdma_event_geometry;
bool s_tdma_event_prelaunch;
static uint32_t s_tdma_event_prelaunch_reason;
_Static_assert(sizeof(s_tdma_event_geometry) + sizeof(s_tdma_event_prelaunch) +
    sizeof(s_tdma_event_prelaunch_reason) <= 192u, "prelaunch storage is bounded");

static void tdma_event_prelaunch_mark(uint32_t state, uint32_t reason)
{
    if (!s_tdma_event_prelaunch) return;
    tdma_geometry_observer_record(s_tdma_event_geometry.generation,
        s_tdma_event_observer.epoch, state, reason, s_tdma_event_snapshot.prefix_bits);
}

uint32_t tdma_event_prelaunch_geometry(const tdma_pio_spi_phys_t *phys)
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



bool tdma_pio_spi_phys_event_selected(const tdma_pio_spi_phys_t *phys) {
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

static void tdma_event_recovery_record_failure(tdma_pio_spi_phys_t *phys,
    const tdma_event_batch_t *batch, uint32_t final_faults)
{
    const tdma_event_observer_t *o = &s_tdma_event_observer;
    tdma_pio_spi_event_recovery_snapshot_t *r = &s_tdma_event_recovery;
    if (o->state != TDMA_EVENT_INVALID || o->epoch == 0u ||
        r->last_failure_epoch == o->epoch || !s_tdma_event_tap_applied.enabled ||
        s_tdma_event_prelaunch) return;
    tdma_event_recovery_increment(&r->failure_count);
    r->last_failure_epoch = o->epoch;
    r->last_failure_reason = o->reason;
    /* SEQUENCE may invalidate feed before the final sticky-fault check; that
     * final fault must still forbid recovery even if o->fault_bits is zero. */
    r->last_failure_fault_bits = o->fault_bits | final_faults;
    r->last_accepted_sequence = s_tdma_event_snapshot.sequence;
    r->last_accepted_ordinal = s_tdma_event_snapshot.published != 0u
        ? s_tdma_event_snapshot.ordinal : UINT32_MAX;
    r->last_batch_sequence_valid = batch != NULL && batch->count[2] != 0u;
    r->last_batch_sequence_first = r->last_batch_sequence_valid ? batch->words[2][0] : 0u;
    if (o->reason == TDMA_EVENT_SEQUENCE && r->last_failure_fault_bits == 0u &&
        phys->armed && tdma_pio_spi_phys_event_selected(phys) &&
        s_tdma_event_tap_applied_valid && s_tdma_pio_spi_rx_arm_valid &&
        s_tdma_event_arm_epoch == s_tdma_pio_spi_rx_arm_epoch) {
        r->binding_tap_generation = s_tdma_event_tap_applied.generation;
        r->binding_arm_epoch_lo = (uint32_t)s_tdma_event_arm_epoch;
        r->binding_arm_epoch_hi = (uint32_t)(s_tdma_event_arm_epoch >> 32u);
        r->pending = TDMA_EVENT_RECOVERY_RESET_PENDING;
        r->cancel_reason = TDMA_EVENT_RECOVERY_CANCEL_NONE;
    }
    tdma_event_recovery_publish();
}

void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *phys) {
    if (phys != NULL) tdma_event_live_retire(TDMA_EVENT_LIVE_STOP);
    if (phys != NULL) tdma_event_recovery_cancel(TDMA_EVENT_RECOVERY_CANCEL_STOP);
    /* DMA owns the three observer RX FIFOs for the whole follower lifetime.
     * Abort and release those channels before the PIO program can be reused
     * by a maintenance persona. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (phys != NULL && s_tdma_pio_spi_program_persona ==
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)
        pio_set_sm_mask_enabled(phys->flight_resources.tx_pio, TDMA_EVENT_SM_MASK, false);
    if (!tdma_event_dma_stop()) {
        s_tdma_event_waiting = false;
        tdma_event_observer_stop(&s_tdma_event_observer);
        tdma_event_history_retire(&s_tdma_event_history);
        tdma_event_candidate_retire();
        s_tdma_event_arm_epoch = 0u;
        if (phys != NULL) tdma_event_publish_state(phys);
        return;
    }
#endif
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
    /* Internal ARM preparation can stop the old observer after a new sparse
     * admission. Only retire an archive that acquired an observer lifetime. */
    if ((s_tdma_rx_first_window.flags & TDMA_RX_FIRST_PRELAUNCH_ACTIVE) != 0u)
        tdma_rx_first_window_retire(stop_reason == TDMA_GEOMETRY_OBSERVER_STOP
            ? TDMA_RX_FIRST_STOP_BEFORE_COMPLETE : TDMA_RX_FIRST_OBSERVER_FAILED, false);
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

void tdma_pio_spi_phys_event_prepare(tdma_pio_spi_phys_t *phys,
                                            const tdma_ring_runtime_config_t *config) {
    if (phys == NULL) return;
    tdma_event_recovery_cancel(TDMA_EVENT_RECOVERY_CANCEL_STOP);
    tdma_rx_start_cut_arm_begin();
    s_tdma_event_prelaunch = false;
    s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_OK;
    memset(&s_tdma_event_geometry, 0, sizeof(s_tdma_event_geometry));
    const bool tap_valid = tdma_event_tap_freeze(phys, config,
        tdma_pio_spi_phys_event_selected(phys));
    if (!tdma_pio_spi_phys_event_selected(phys)) return;
    tdma_event_history_retire(&s_tdma_event_history);
    s_tdma_event_arm_epoch = 0u;
    tdma_event_candidate_retire();
    tdma_event_observer_init(&s_tdma_event_observer);
    /* Candidate counters and the last historical result survive new ARM. */
    memset(&s_tdma_event_snapshot, 0, offsetof(tdma_pio_spi_event_snapshot_t, candidate));
    s_tdma_event_hz = clock_get_hz(clk_sys);
    s_tdma_event_period_ns = config != NULL ? config->cycle_period_ns : 0u;
    s_tdma_event_base_us = time_us_64();
    s_tdma_event_last_service_us = 0u;
    s_tdma_event_waiting = tap_valid;
    s_tdma_event_snapshot.pio_hz = s_tdma_event_hz;
    tdma_event_publish_state(phys);
}

static void tdma_event_start(tdma_pio_spi_phys_t *phys)
{
    const bool recovering = s_tdma_event_recovery.pending == TDMA_EVENT_RECOVERY_WAIT_IDLE;
    if (!s_tdma_event_waiting || !s_tdma_event_tap_applied_valid ||
        (!s_tdma_event_tap_applied.enabled && !s_tdma_event_prelaunch &&
        (!phys->flight_overlay_alignment_locked || phys->flight_overlay_alignment_samples <
            TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES))) return;
    const PIO pio = phys->flight_resources.tx_pio;
    const uint32_t cs_mask = (1u << phys->rx_csn_pin) | (1u << phys->tx_csn_pin);
    if ((pio->dbg_padout & (1u << phys->tx_csn_pin)) == 0u ||
        (gpio_get_all() & cs_mask) != cs_mask) {
        s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_CS;
        return;
    }
    /* Explicit taps use only the independent ARM-frozen CS coordinates.
     * Ordinary startup uses live RX training. Selected startup instead tests
     * the separately frozen DMA-offset hypothesis against the next CS frame.
     * Neither coordinate is promoted to a qualified physical identity. */
    uint64_t bits;
    uint32_t bit_shift = 0u;
    uint32_t sample_delay;
    if (s_tdma_event_tap_applied.enabled != 0u) {
        bits = (uint64_t)s_tdma_event_tap_applied.prefix_bits + 32u;
        sample_delay = s_tdma_event_tap_applied.sample_delay_cycles;
    } else {
        const uint32_t byte_shift = s_tdma_event_prelaunch
            ? s_tdma_event_geometry.dma_byte_shift : phys->flight_alignment_byte_shift;
        bit_shift = s_tdma_event_prelaunch
            ? s_tdma_event_geometry.dma_bit_shift : phys->flight_alignment_bit_shift;
        bits = ((uint64_t)byte_shift + TDMA_PIO_SPI_PACKET_HEADER_SIZE +
            TDMA_TRANSPORT_FRAME_SEQUENCE_OFFSET + sizeof(uint32_t)) * 8u + bit_shift;
        sample_delay = phys->flight_data_phase_delay_cycles;
    }
    if (bits < 32u || bits > (uint64_t)phys->flight_physical_byte_count * 8u ||
        bits > UINT32_MAX || bit_shift >= 8u ||
        sample_delay > TDMA_PIO_SPI_EVENT_TAP_MAX_DELAY_CYCLES ||
        phys->baud_hz == 0u || s_tdma_event_hz == 0u ||
        s_tdma_event_epoch == UINT32_MAX) {
        s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_GEOMETRY;
        return;
    }
    pio_set_sm_mask_enabled(pio, TDMA_EVENT_SM_MASK, false);
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    /* Quiesce any deferred start before resetting FIFOs or PIO registers. */
    if (!tdma_event_dma_stop()) {
        s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_FIFO;
        tdma_event_publish_state(phys);
        return;
    }
#endif
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
        pio_encode_delay(sample_delay);
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
    /* Start the drain before enabling the PIO state machines. A failed
     * allocation leaves this observer stopped; a new ARM may retry it. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (!tdma_event_dma_start(pio)) {
        s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_FIFO;
        s_tdma_event_waiting = false;
        tdma_event_publish_state(phys);
        return;
    }
#endif
    tdma_pio_spi_event_snapshot_t *s = &s_tdma_event_snapshot;
    s->prefix_bits = (uint32_t)bits - 32u;
    tdma_event_tap_loaded(s->prefix_bits, sample_delay);
    tdma_rx_dma_counter_t cut_counter = {0};
    if (!recovering) {
        cut_counter = s_tdma_pio_spi_rx_sequence;
        tdma_rx_start_cut_before(phys);
    }
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
    uint64_t timer_before = 0u, timer_after = 0u;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    /* Clock qualification remains outside the actual enable bracket. The
     * owner-local SRAM helper retains both raw samples and never retries or
     * narrows their observed interval. Dirty-start/epoch checks below remain
     * authoritative even when no valid timestamp anchor can be published. */
    const bool timer_anchor_ok = tdma_event_enable_anchor_capture(pio,
        TDMA_EVENT_SM_MASK, s_tdma_event_hz, &timer_before, &timer_after);
#else
    const bool timer_before_ok = vdc_timestamp_clock_try_read_ticks64(s_tdma_event_hz, &timer_before);
    pio_enable_sm_mask_in_sync(pio, TDMA_EVENT_SM_MASK);
    const bool timer_after_ok = vdc_timestamp_clock_try_read_ticks64(s_tdma_event_hz, &timer_after);
    const bool timer_anchor_ok = timer_before_ok && timer_after_ok;
#endif
    const uint64_t after = time_us_64();
    s->start_pad_after = gpio_get_all();
    if (!recovering)
        tdma_rx_start_cut_after(phys, &cut_counter, s->start_pad_before, s->start_pad_after);
    uint32_t faults = tdma_event_faults(phys);
    if ((s->start_pad_before & cs_mask) != cs_mask ||
        (s->start_pad_after & cs_mask) != cs_mask)
        faults |= TDMA_EVENT_FAULT_DIRTY_START;
    uint32_t join_window_frames = TDMA_EVENT_FIFO_WORDS / 2u;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    /* Two counter words per frame: tolerate the DMA ring's bounded backlog,
     * but never discard partial tuples or resume the same epoch after loss. */
    join_window_frames = TDMA_EVENT_DMA_RING_WORDS / 2u;
#endif
    const tdma_event_config_t config = {
        .pio_hz = s_tdma_event_hz,
        /* This observer feeds the live DPLL, not a timed trace capture.
         * Keep one epoch until STOP/fault. Each feed still proves ordered
         * read brackets, unique counter lifts, bounded joins and arithmetic. */
        .epoch_limit_cycles = UINT64_MAX,
        .join_timeout_cycles = (uint64_t)s_tdma_event_hz * s_tdma_event_period_ns /
                               1000000000u * join_window_frames,
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
    tdma_event_live_bind(timer_before, timer_after, timer_anchor_ok);
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
    if (!recovering) tdma_rx_start_cut_monitor(phys);
    if (s_tdma_event_observer.state == TDMA_EVENT_INVALID)
        tdma_event_recovery_record_failure(phys, NULL, faults);
    tdma_event_publish_state(phys);
}

/* Exactly one synchronous attempt after RX ARM, final injected WAIT and SM
 * enable. Failure is an ARM rejection; service must never retry it later. */
bool tdma_pio_spi_phys_event_prelaunch(tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config) {
    /* Configuration admission is distinct from runtime sample failure. An
     * independent tap cannot also claim the legacy DMA-first-window launch. */
    if (!s_tdma_event_tap_applied_valid || config == NULL ||
        (s_tdma_event_tap_applied.enabled != 0u && config->geometry_generation != 0u)) return false;
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
    /* Zero completed DMA writes and empty FIFOs cannot exclude a partial ISR
     * or an in-flight word. The final ARM injection is WAIT 0 RXCS; no owner
     * SM_INSTR write or restart may replace it before these observations.
     * EXEC_STALLED witnesses that same injection, while SM_INSTR reads only
     * the program instruction at PC. Never repair/reinject or retry a failed
     * selected generation, and retain all diagnostic/identity-unproved flags. */
    reason = TDMA_GEOMETRY_OBSERVER_CAPTURE_ENTRY;
    const uint32_t entry_wait = TDMA_RX_START_CUT_CAPTURE_ENTRY_WAIT_BEFORE |
                                TDMA_RX_START_CUT_CAPTURE_ENTRY_WAIT_AFTER;
    if ((s_tdma_rx_start_cut.flags & entry_wait) != entry_wait ||
        s_tdma_rx_start_cut.capture_pc_before != s_tdma_pio_spi_flight_process_follower_offset ||
        s_tdma_rx_start_cut.capture_pc_after != s_tdma_pio_spi_flight_process_follower_offset)
        goto rejected;
    tdma_event_prelaunch_mark(TDMA_GEOMETRY_OBSERVER_ACTIVE, TDMA_GEOMETRY_OBSERVER_OK);
    tdma_rx_first_window_prelaunch(phys, config, TDMA_GEOMETRY_OBSERVER_OK);
    return true;
rejected:
    tdma_rx_first_window_prelaunch(phys, config, reason);
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

/* One reset transition or one idle/start attempt per existing owner phase.
 * This does not call physical ARM/prepare/STOP or consume a DMA candidate. */
static void tdma_event_recovery_step(tdma_pio_spi_phys_t *phys)
{
    tdma_pio_spi_event_recovery_snapshot_t *r = &s_tdma_event_recovery;
    const uint64_t arm_epoch = ((uint64_t)r->binding_arm_epoch_hi << 32u) |
        r->binding_arm_epoch_lo;
    uint32_t cancel = TDMA_EVENT_RECOVERY_CANCEL_NONE;
    if (!phys->armed || !tdma_pio_spi_phys_event_selected(phys) ||
        s_tdma_event_prelaunch || !s_tdma_event_tap_applied_valid ||
        !s_tdma_event_tap_applied.enabled ||
        r->binding_tap_generation != s_tdma_event_tap_applied.generation ||
        !s_tdma_pio_spi_rx_arm_valid || arm_epoch != s_tdma_pio_spi_rx_arm_epoch)
        cancel = TDMA_EVENT_RECOVERY_CANCEL_BINDING;
    else if (clock_get_hz(clk_sys) != s_tdma_event_hz)
        cancel = TDMA_EVENT_RECOVERY_CANCEL_CLOCK;
    else if (s_tdma_event_epoch == UINT32_MAX)
        cancel = TDMA_EVENT_RECOVERY_CANCEL_EPOCH;
    else {
        /* A sticky hardware fault can arrive after the failure phase's last
         * observation. Recheck before clearing/resetting any observer state;
         * the intentional disabled SM mask is not a hardware failure here. */
        const PIO pio = phys->flight_resources.tx_pio;
        uint32_t faults = (pio->fdebug & (TDMA_EVENT_SM_MASK << PIO_FDEBUG_RXSTALL_LSB)) != 0u
            ? TDMA_EVENT_FAULT_STALL : 0u;
        if (pio_sm_get_pc(pio, TDMA_EVENT_SEQUENCE_SM) ==
            s_tdma_event_sequence_offset + tdma_event_sequence_offset_bad)
            faults |= TDMA_EVENT_FAULT_SEQUENCE;
        if (faults != 0u) {
            r->last_failure_fault_bits |= faults;
            cancel = TDMA_EVENT_RECOVERY_CANCEL_START;
        }
    }
    if (cancel != TDMA_EVENT_RECOVERY_CANCEL_NONE) {
        s_tdma_event_waiting = false;
        tdma_event_recovery_cancel(cancel);
        tdma_event_publish_state(phys);
        return;
    }
    if (r->pending == TDMA_EVENT_RECOVERY_RESET_PENDING) {
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
        pio_set_sm_mask_enabled(phys->flight_resources.tx_pio, TDMA_EVENT_SM_MASK, false);
        if (!tdma_event_dma_stop()) return;
#endif
        tdma_event_observer_stop(&s_tdma_event_observer);
        tdma_event_history_retire(&s_tdma_event_history);
        tdma_event_candidate_retire();
        s_tdma_event_arm_epoch = 0u;
        const PIO pio = phys->flight_resources.tx_pio;
        pio_set_sm_mask_enabled(pio, TDMA_EVENT_SM_MASK, false);
        for (uint sm = TDMA_EVENT_RX_SM; sm <= TDMA_EVENT_SEQUENCE_SM; ++sm) {
            pio_sm_clear_fifos(pio, sm);
            pio_sm_restart(pio, sm);
        }
        pio_interrupt_clear(pio, TDMA_EVENT_RX_SM);
        pio_interrupt_clear(pio, TDMA_EVENT_TX_SM);
        /* Preserve the first physical-ARM cut's raw body; its observer epoch
         * is historical. A recovered epoch never overwrites this archive. */
        tdma_rx_start_cut_retire(TDMA_RX_START_CUT_OBSERVER);
        memset(&s_tdma_event_snapshot, 0, offsetof(tdma_pio_spi_event_snapshot_t, candidate));
        s_tdma_event_snapshot.pio_hz = s_tdma_event_hz;
        s_tdma_event_last_service_us = 0u;
        s_tdma_event_waiting = true;
        r->pending = TDMA_EVENT_RECOVERY_WAIT_IDLE;
        tdma_event_recovery_publish();
        tdma_event_publish_state(phys);
        return;
    }
    tdma_event_recovery_increment(&r->attempt_count);
    s_tdma_event_prelaunch_reason = TDMA_GEOMETRY_OBSERVER_OK;
    tdma_event_start(phys);
    if (s_tdma_event_observer.state == TDMA_EVENT_ACTIVE) {
        tdma_event_recovery_increment(&r->enable_count);
        r->pending = TDMA_EVENT_RECOVERY_NONE;
    } else if (s_tdma_event_waiting && s_tdma_event_prelaunch_reason == TDMA_GEOMETRY_OBSERVER_CS) {
        tdma_event_recovery_increment(&r->deferral_count);
    } else {
        s_tdma_event_waiting = false;
        tdma_event_recovery_cancel(TDMA_EVENT_RECOVERY_CANCEL_START);
    }
    tdma_event_recovery_publish();
    tdma_event_publish_state(phys);
}

__attribute__((aligned(32)))
void tdma_pio_spi_phys_event_service(tdma_pio_spi_phys_t *phys)
{
    if (!tdma_pio_spi_phys_event_selected(phys) || !phys->armed) {
        tdma_event_live_retire(TDMA_EVENT_LIVE_BINDING);
        if (s_tdma_event_recovery.pending != TDMA_EVENT_RECOVERY_NONE) {
            s_tdma_event_waiting = false;
            tdma_event_recovery_cancel(TDMA_EVENT_RECOVERY_CANCEL_BINDING);
        }
        return;
    }
    uint64_t stage_start = tdma_service_timing_now();
    const uint64_t entered = time_us_64();
    tdma_event_live_monitor();
    tdma_pio_spi_event_snapshot_t *s = &s_tdma_event_snapshot;
    if (s_tdma_event_recovery.pending != TDMA_EVENT_RECOVERY_NONE) {
        tdma_event_recovery_step(phys);
        const uint64_t duration = time_us_64() - entered;
        if (duration > s_tdma_event_recovery.service_max_us)
            s_tdma_event_recovery.service_max_us = duration > UINT32_MAX ? UINT32_MAX : (uint32_t)duration;
        tdma_event_recovery_publish();
        tdma_service_timing_record(TDMA_TIMING_EVENT_ENTRY, stage_start);
        return;
    }
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
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (s_tdma_event_dma_active) {
        tdma_event_dma_harvest(batch, &s_tdma_event_observer, maxima);
    } else
#endif
    {
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
    }
    const uint64_t after = time_us_64();
    tdma_service_timing_record(TDMA_TIMING_EVENT_HARVEST, stage_start);
    stage_start = tdma_service_timing_now();
    batch->observed = (tdma_event_interval_t){tdma_event_cycles(before, false), tdma_event_cycles(after, true)};
    tdma_service_timing_record(TDMA_TIMING_EVENT_CONVERT, stage_start);
    stage_start = tdma_service_timing_now();
    batch->post_faults |= tdma_event_faults(phys);
    if (batch->count[0]) s->fifo_rx_first = batch->words[0][0];
    if (batch->count[0] > 1u) s->fifo_rx_second = batch->words[0][1];
    if (batch->count[1]) s->fifo_tx_first = batch->words[1][0];
    if (batch->count[1] > 1u) s->fifo_tx_second = batch->words[1][1];
    if (batch->count[2]) s->fifo_sequence_first = batch->words[2][0];
    tdma_rx_first_window_words(batch);
    const size_t count = tdma_event_observer_feed(&s_tdma_event_observer, batch, s_tdma_event_records);
    tdma_rx_first_window_raw(phys);
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
    tdma_rx_first_window_event(phys, count);
    if (s_tdma_event_observer.state == TDMA_EVENT_ACTIVE && count != 0u) {
        /* Retain the whole validated batch, after the final sticky-fault
         * check. Sequence lookup remains diagnostic; no packet identity or
         * physical-clock qualification follows from retaining these words. */
        (void)tdma_event_history_append(&s_tdma_event_history, s_tdma_event_records,
            count, s_tdma_event_observer.joined);
        if (!s_tdma_event_history.active) tdma_event_candidate_retire();
        const tdma_event_record_t *last = &s_tdma_event_records[count - 1u];
        tdma_event_live_record(last);
        s->published = count > UINT32_MAX - s->published ? UINT32_MAX :
                       s->published + (uint32_t)count;
        s->sequence = last->sequence;
        s->ordinal = last->ordinal;
        s->rx_elapsed_cycles = last->rx_elapsed_cycles;
        s->tx_elapsed_cycles = last->tx_elapsed_cycles;
    }
    if (s_tdma_event_observer.state == TDMA_EVENT_INVALID) {
        tdma_event_live_retire(TDMA_EVENT_LIVE_INVALID);
        tdma_event_recovery_record_failure(phys, batch, final_faults);
        tdma_rx_first_window_retire(TDMA_RX_FIRST_OBSERVER_FAILED, false);
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
tdma_event_exact_result_t tdma_pio_spi_phys_event_copy_history_exact(
    const tdma_pio_spi_phys_t *phys, uint64_t expected_arm_epoch,
    uint32_t expected_observer_epoch, uint32_t source_sequence,
    tdma_pio_spi_event_exact_t *out)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (get_core_num() != 1u) return TDMA_EVENT_EXACT_BAD_ARGUMENT;
#endif
    (void)source_sequence;
    if (phys == NULL || out == NULL || expected_arm_epoch == 0u || expected_observer_epoch == 0u)
        return TDMA_EVENT_EXACT_BAD_ARGUMENT;
    return TDMA_EVENT_EXACT_RETIRED;
}

tdma_event_window_result_t tdma_pio_spi_phys_event_copy_history_window(
    const tdma_pio_spi_phys_t *phys, uint32_t expected_observer_epoch,
    uint64_t next_ordinal, tdma_pio_spi_event_window_t *out)
{
    (void)phys; (void)expected_observer_epoch; (void)next_ordinal; (void)out;
    return TDMA_EVENT_WINDOW_UNAVAILABLE;
}
bool tdma_pio_spi_phys_event_get_live_snapshot(const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_event_live_snapshot_t *out)
{
    (void)phys; (void)out;
    return false;
}
bool tdma_pio_spi_phys_event_recovery_get(const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_event_recovery_snapshot_t *out)
{
    (void)phys; (void)out;
    return false;
}
bool tdma_pio_spi_phys_event_tap_set(tdma_pio_spi_phys_t *phys,
    uint32_t enabled, uint32_t prefix_bits, uint32_t sample_delay_cycles)
{
    (void)phys; (void)enabled; (void)prefix_bits; (void)sample_delay_cycles;
    return false;
}
bool tdma_pio_spi_phys_event_tap_get(const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_event_tap_snapshot_t *out)
{
    (void)phys; (void)out;
    return false;
}
void tdma_rx_first_window_admit(const tdma_ring_runtime_config_t *config) { (void)config; }
void tdma_pio_spi_phys_rx_first_window_persona_unload(void) { }
void tdma_rx_first_window_arm_failed(void) { }
void tdma_rx_first_window_retire(uint32_t reason, bool stopped) { (void)reason; (void)stopped; }
bool tdma_pio_spi_phys_get_rx_first_window(tdma_rx_first_window_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    return false;
}
bool tdma_pio_spi_phys_event_prelaunch(tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config) {
    (void)phys;
    if (config->geometry_generation == 0u) return true;
    tdma_geometry_observer_record(config->geometry_generation, 0u,
        TDMA_GEOMETRY_OBSERVER_REJECTED, TDMA_GEOMETRY_OBSERVER_DISABLED, 0u);
    return false;
}
void tdma_rx_start_cut_arm_begin(void) { }
void tdma_rx_start_cut_disarmed(void) { }
bool tdma_pio_spi_phys_get_rx_start_cut(tdma_rx_start_cut_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    return false;
}
bool tdma_pio_spi_phys_event_selected(const tdma_pio_spi_phys_t *phys) { (void)phys; return false; }
void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *phys) {
    if (phys == NULL) return;
    tdma_event_candidate_retire();
    if (s_tdma_event_snapshot.candidate.query_count != 0u) tdma_event_publish_snapshot(phys);
}
void tdma_pio_spi_phys_event_prepare(tdma_pio_spi_phys_t *phys,
                                            const tdma_ring_runtime_config_t *config) { (void)config; tdma_pio_spi_phys_event_stop(phys); }
void tdma_pio_spi_phys_event_service(tdma_pio_spi_phys_t *phys) { if (phys != NULL && s_tdma_event_candidate_dirty) tdma_event_publish_snapshot(phys); }
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
