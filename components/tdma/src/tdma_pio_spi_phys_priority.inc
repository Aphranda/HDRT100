/* Core1-only IRQ3 capture. DMA remains the sole DATA FIFO reader. Geometry
 * is frozen from the existing successful overlay training, not rediscovered
 * in an ISR. No ordinary scanner, FIFO tail, timing accumulator or PI call. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TDMA_PRIORITY_IRQ_HOT __not_in_flash("tdma_priority_irq")
#else
#define TDMA_PRIORITY_IRQ_HOT
#endif
/* Keep the fixed record table below Core1's reserved SCRATCH_X stack.
 * Both app linkers reject data/stack overlap. Startup initializes this
 * section through the SDK's existing scratch copy table. */
_Static_assert(sizeof(tdma_priority_rx_t) + sizeof(tdma_priority_rx_record_t) <= 1024u,
    "priority records must fit within the scratch data budget");
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
static tdma_priority_rx_t __scratch_x("tdma_priority_rx") s_tdma_priority_rx;
static tdma_priority_rx_record_t __scratch_x("tdma_priority_rx_work") s_tdma_priority_work;
#else
static tdma_priority_rx_t s_tdma_priority_rx;
static tdma_priority_rx_record_t s_tdma_priority_work;
#endif
static tdma_rx_dma_counter_t s_tdma_priority_counter;
static tdma_priority_rx_binding_t s_tdma_priority_binding;
static tdma_pio_spi_phys_t *s_tdma_priority_phys;
static uint32_t s_tdma_priority_stride, s_tdma_priority_byte_shift, s_tdma_priority_bit_shift;
static uint32_t s_tdma_priority_boundaries_seen;
static bool s_tdma_priority_geometry, s_tdma_priority_installed;
static tdma_priority_rx_sink_t s_tdma_priority_sink;
static bool s_tdma_priority_window;
static uint32_t s_tdma_priority_quota, s_tdma_priority_deadline;

bool tdma_pio_spi_phys_set_priority_rx_sink(tdma_priority_rx_sink_t sink)
{
    if (s_tdma_priority_installed || s_tdma_priority_rx.status.active) return false;
    __atomic_store_n(&s_tdma_priority_sink, sink, __ATOMIC_RELEASE);
    return true;
}

static TDMA_PRIORITY_IRQ_HOT void tdma_priority_deliver(const tdma_priority_rx_record_t *record)
{
    const tdma_priority_rx_sink_t sink =
        __atomic_load_n(&s_tdma_priority_sink, __ATOMIC_ACQUIRE);
    if (sink) sink(record);
}
/* Bounded STOP-time diagnostics in main RAM, not the IRQ working/stack bank.
 * Core1 owns every write. Atomic words plus one guard avoid a second copy. */
static struct {
    uint32_t guard;
    tdma_priority_rx_timing_t value;
} s_tdma_priority_timing;
_Static_assert(sizeof(s_tdma_priority_timing) == 24u, "bounded timing RAM");

static void tdma_priority_timing_reset(void)
{
    const uint32_t guard = __atomic_load_n(&s_tdma_priority_timing.guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_priority_timing.guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(&s_tdma_priority_timing.value.samples, 0u, __ATOMIC_RELAXED);
    for (uint32_t i = 0u; i < 4u; ++i)
        __atomic_store_n(&s_tdma_priority_timing.value.max_cycles[i], 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_priority_timing.guard, guard + 2u, __ATOMIC_RELEASE);
}

static TDMA_PRIORITY_IRQ_HOT void tdma_priority_timing_account(uint32_t entry, uint32_t end,
    const uint32_t cuts[3])
{
    const uint32_t spans[4] = {cuts[0] - entry, cuts[1] - cuts[0],
        cuts[2] - cuts[1], end - cuts[2]};
    /* The caller established a valid full 64-bit interval below 2^32. This
     * additional sum rejects non-monotonic probes while allowing low wrap. */
    if ((uint64_t)spans[0] + spans[1] + spans[2] + spans[3] != end - entry) return;
    const uint32_t guard = __atomic_load_n(&s_tdma_priority_timing.guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_priority_timing.guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    uint32_t samples = __atomic_load_n(&s_tdma_priority_timing.value.samples, __ATOMIC_RELAXED);
    if (samples != UINT32_MAX) ++samples;
    __atomic_store_n(&s_tdma_priority_timing.value.samples, samples, __ATOMIC_RELAXED);
    for (uint32_t i = 0u; i < 4u; ++i) {
        const uint32_t maximum = __atomic_load_n(&s_tdma_priority_timing.value.max_cycles[i], __ATOMIC_RELAXED);
        if (spans[i] > maximum)
            __atomic_store_n(&s_tdma_priority_timing.value.max_cycles[i], spans[i], __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_tdma_priority_timing.guard, guard + 2u, __ATOMIC_RELEASE);
}

/* Diagnostic breadcrumbs only: existing watchdog scratch[6] survives reset.
 * Normal IRQ returns restore the interrupted foreground marker. No samples,
 * queue state or runtime control are communicated through these MMIO writes. */
#define TDMA_PRIORITY_PROGRESS_ENTRY          0x1e01u
#define TDMA_PRIORITY_PROGRESS_ACK            0x1e02u
#define TDMA_PRIORITY_PROGRESS_DMA_BEFORE     0x1e03u
#define TDMA_PRIORITY_PROGRESS_DMA_AFTER      0x1e04u
#define TDMA_PRIORITY_PROGRESS_CANDIDATE      0x1e05u
#define TDMA_PRIORITY_PROGRESS_COPY           0x1e06u
#define TDMA_PRIORITY_PROGRESS_RECHECK        0x1e07u
#define TDMA_PRIORITY_PROGRESS_VALIDATE       0x1e08u
#define TDMA_PRIORITY_PROGRESS_VALIDATED      0x1e09u
#define TDMA_PRIORITY_PROGRESS_PUBLISH        0x1e0au
#define TDMA_PRIORITY_PROGRESS_PUBLISHED      0x1e0bu
#define TDMA_PRIORITY_PROGRESS_FINISH         0x1e0cu
#define TDMA_PRIORITY_PROGRESS_FINISHED       0x1e0du

static bool tdma_priority_ticks(uint64_t *ticks)
{
    const uint32_t hi = timer1_hw->timerawh;
    const uint32_t lo = timer1_hw->timerawl;
    if (hi != timer1_hw->timerawh) return false;
    *ticks = ((uint64_t)hi << 32u) | lo;
    return true;
}

static TDMA_PRIORITY_IRQ_HOT bool tdma_priority_produced(uint64_t *produced)
{
    uint64_t before, after;
    if (!tdma_priority_ticks(&before)) return false;
    const uint32_t count = dma_hw->ch[s_tdma_pio_spi_rx_dma_channel].transfer_count;
    __dmb();
    if (!tdma_priority_ticks(&after)) return false;
    bool lost;
    if ((count & DMA_CH0_TRANS_COUNT_MODE_BITS) !=
            (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB) ||
        !tdma_rx_dma_counter_observe(&s_tdma_priority_counter,
            count & DMA_CH0_TRANS_COUNT_COUNT_BITS, before, after, produced, &lost)) return false;
    if (lost) {
        /* A delayed START or blackout retires old records, not transport.
         * Missing reload periods are multiples of both stride and SRAM ring
         * length, so the next lower-bound coordinate retains their moduli.
         * Reject this copy and use a fresh explicit namespace next entry. */
        if (!tdma_priority_rx_rebase(&s_tdma_priority_rx))
            tdma_priority_rx_stop(&s_tdma_priority_rx);
        tdma_priority_deliver(NULL);
        return false;
    }
    return true;
}

static TDMA_PRIORITY_IRQ_HOT uint32_t tdma_priority_capture(uint64_t entry, uint32_t cuts[3], bool *accepted)
{
    const tdma_pio_spi_phys_t *phys = s_tdma_priority_phys;
    if (!phys || !s_tdma_priority_rx.status.active) return TDMA_PRIORITY_RX_INACTIVE;
    if (!s_tdma_priority_geometry) return TDMA_PRIORITY_RX_GEOMETRY;
    /* An old sticky IRQ serviced after the next CS-low is not this completed
     * lap. Reject it rather than selecting a partly arriving later header. */
    if (!gpio_get(phys->rx_csn_pin)) return TDMA_PRIORITY_RX_INCOMPLETE;
    uint64_t produced, after, candidate;
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_DMA_BEFORE);
    if (!tdma_priority_produced(&produced)) return TDMA_PRIORITY_RX_DMA;
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_DMA_AFTER);
    const uint32_t needed = TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE +
        (s_tdma_priority_binding.reference_slot + 1u) * TDMA_FLIGHT_SHORT_SLOT_SIZE;
    if (!tdma_priority_rx_candidate(produced, s_tdma_priority_stride, needed,
            s_tdma_priority_byte_shift, s_tdma_priority_bit_shift,
            TDMA_PIO_SPI_RX_RING_WORDS, &candidate)) return TDMA_PRIORITY_RX_INCOMPLETE;
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_CANDIDATE);
    /* Only this non-reentrant Core1 IRQ writes the working record. ARM/STOP
     * disable its source and never touch this buffer; readers only see slots. */
    tdma_priority_rx_record_t *const record = &s_tdma_priority_work;
    /* The fixed copies below overwrite header/mailbox and sequence is read
     * only after that copy. Avoid clearing those same bytes first. */
    record->irq_entry_ticks = entry;
    record->candidate_word = candidate;
    record->epoch = s_tdma_priority_rx.status.epoch;
    uint8_t outer[TDMA_PIO_SPI_PACKET_HEADER_SIZE];
    tdma_pio_spi_phys_rx_ring_copy(outer, candidate, sizeof(outer), s_tdma_priority_bit_shift);
    tdma_pio_spi_phys_rx_ring_copy(record->header, candidate + sizeof(outer),
        sizeof(record->header), s_tdma_priority_bit_shift);
    tdma_pio_spi_phys_rx_ring_copy(record->mailbox, candidate + sizeof(outer) + sizeof(record->header) +
        s_tdma_priority_binding.reference_slot * TDMA_FLIGHT_SHORT_SLOT_SIZE,
        sizeof(record->mailbox), s_tdma_priority_bit_shift);
    __dmb();
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_COPY);
    cuts[0] = timer1_hw->timerawl;
    if (!tdma_priority_produced(&after)) return TDMA_PRIORITY_RX_DMA;
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_RECHECK);
    if (after < produced || after - candidate >= TDMA_PIO_SPI_RX_RING_WORDS)
        return TDMA_PRIORITY_RX_OVERWRITTEN;
    if (outer[0] != TDMA_PIO_SPI_PACKET_MAGIC0 || outer[1] != TDMA_PIO_SPI_PACKET_MAGIC1 ||
        ((uint32_t)outer[2] | ((uint32_t)outer[3] << 8u)) != s_tdma_priority_binding.packet_bytes)
        return TDMA_PRIORITY_RX_HEADER;
    record->sequence = (uint32_t)record->header[8] | ((uint32_t)record->header[9] << 8u) |
        ((uint32_t)record->header[10] << 16u) | ((uint32_t)record->header[11] << 24u);
    cuts[1] = timer1_hw->timerawl;
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_VALIDATE);
    const uint32_t reason = tdma_priority_rx_validate(&s_tdma_priority_binding, record);
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_VALIDATED);
    if (reason != TDMA_PRIORITY_RX_OK) return reason;
    cuts[2] = timer1_hw->timerawl;
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_PUBLISH);
    *accepted = tdma_priority_rx_publish(&s_tdma_priority_rx, record);
    if (*accepted) {
        /* Publication may advance the namespace on carrier wrap. Deliver
         * exactly the published identity, never the pre-wrap working epoch. */
        record->epoch = s_tdma_priority_rx.status.epoch;
        tdma_priority_deliver(record);
    } else if (!s_tdma_priority_rx.status.active) {
        tdma_priority_deliver(NULL);
    }
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_PUBLISHED);
    return TDMA_PRIORITY_RX_OK;
}

static TDMA_PRIORITY_IRQ_HOT void tdma_priority_irq(void)
{
    const uint32_t foreground_progress = watchdog_hw->scratch[6];
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_ENTRY);
    uint64_t entry = 0u, end = 0u;
    uint32_t cuts[3];
    bool accepted = false;
    const bool clock_valid = tdma_priority_ticks(&entry);
    if (!pio_interrupt_get(BOARD_TDMA_RX_PIO, 3u)) goto restore_progress;
    if (!s_tdma_priority_window || !s_tdma_priority_quota || !clock_valid ||
        (int32_t)((uint32_t)entry - s_tdma_priority_deadline) >= 0) {
        irq_set_enabled(pio_get_irq_num(BOARD_TDMA_RX_PIO, 0u), false);
        goto restore_progress; /* Pending source remains for the next admitted window. */
    }
    --s_tdma_priority_quota;
    /* Sole running-persona ACK. Clear once at entry; a later boundary stays
     * pending for another invocation instead of being cleared at exit. */
    pio_interrupt_clear(BOARD_TDMA_RX_PIO, 3u);
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_ACK);
    const uint32_t reason = clock_valid ? tdma_priority_capture(entry, cuts, &accepted) : TDMA_PRIORITY_RX_DMA;
    if (reason != TDMA_PRIORITY_RX_OK) tdma_priority_rx_reject(&s_tdma_priority_rx, reason);
    const bool interval_valid = tdma_priority_ticks(&end) && end >= entry && end - entry <= UINT32_MAX;
    if (!interval_valid) end = entry + UINT32_MAX;
    /* Measured body includes capture, CRC and slot publication. The fixed
     * finish/accounting tail is reported separately in the IRQ budget; the
     * phase wall clock continues to charge the complete interrupt. */
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_FINISH);
    tdma_priority_rx_finish_irq(&s_tdma_priority_rx, entry, (uint32_t)(end - entry));
    /* Probes are inside the measured body; this fixed diagnostic update is
     * after its end and remains charged by the enclosing phase wall time. */
    if (accepted && interval_valid)
        tdma_priority_timing_account((uint32_t)entry, (uint32_t)end, cuts);
    drv_watchdog_mark_progress(1u, TDMA_PRIORITY_PROGRESS_FINISHED);
    if (!s_tdma_priority_quota || !s_tdma_priority_rx.status.active)
        irq_set_enabled(pio_get_irq_num(BOARD_TDMA_RX_PIO, 0u), false);
restore_progress:
    drv_watchdog_mark_progress(1u, foreground_progress);
}

void tdma_pio_spi_phys_priority_rx_window_core1(bool enable, uint32_t remaining_entries,
    uint32_t window_deadline_tick_low)
{
    if (get_core_num() != 1u) return;
    const uint32_t saved = save_and_disable_interrupts();
    s_tdma_priority_window = enable;
    s_tdma_priority_quota = enable ? remaining_entries : 0u;
    s_tdma_priority_deadline = window_deadline_tick_low;
    if (s_tdma_priority_installed)
        irq_set_enabled(pio_get_irq_num(BOARD_TDMA_RX_PIO, 0u),
            enable && remaining_entries && s_tdma_priority_rx.status.active);
    restore_interrupts(saved);
}

static void tdma_priority_stop(void)
{
    if (!s_tdma_priority_installed) return;
    /* ARM/STOP/persona owner executes on Core1; no Core0 NVIC manipulation. */
    if (get_core_num() != 1u) return;
    const uint irq = pio_get_irq_num(BOARD_TDMA_RX_PIO, 0u);
    pio_set_irq0_source_enabled(BOARD_TDMA_RX_PIO, pis_interrupt3, false);
    irq_set_enabled(irq, false);
    irq_clear(irq);
    pio_interrupt_clear(BOARD_TDMA_RX_PIO, 3u);
    tdma_priority_rx_stop(&s_tdma_priority_rx);
    tdma_priority_deliver(NULL);
    s_tdma_priority_geometry = false;
    s_tdma_priority_phys = NULL;
    irq_remove_handler(irq, tdma_priority_irq);
    s_tdma_priority_installed = false;
}

static bool tdma_priority_start(tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config)
{
    if (get_core_num() != 1u || s_tdma_priority_installed || !s_tdma_pio_spi_rx_arm_valid ||
        s_tdma_pio_spi_rx_dma_channel < 0) return false;
    if ((uint64_t)config->cycle_period_ns * BOARD_SYS_CLOCK_HZ <
            (uint64_t)PROJECT_CORE1_PRIORITY_RX_MIN_PHYSICAL_CYCLES * 1000000000u) {
        if (!tdma_priority_rx_start(&s_tdma_priority_rx)) return false;
        tdma_priority_timing_reset();
        tdma_priority_rx_reject(&s_tdma_priority_rx, TDMA_PRIORITY_RX_CADENCE);
        tdma_priority_rx_stop(&s_tdma_priority_rx);
        return true; /* Ordinary TDMA remains available at other cadences. */
    }
    const uint irq = pio_get_irq_num(BOARD_TDMA_RX_PIO, 0u);
    if (irq_get_exclusive_handler(irq) || irq_has_shared_handler(irq) ||
        BOARD_TDMA_RX_PIO->inte0 != 0u || !tdma_priority_rx_start(&s_tdma_priority_rx)) return false;
    tdma_priority_timing_reset();
    s_tdma_priority_binding = (tdma_priority_rx_binding_t){
        .packet_bytes = TDMA_TRANSPORT_FRAME_HEADER_SIZE + phys->flight_payload_size,
        .reference_slot = config->reference_slot_id, .local_slot = config->local_slot_id,
        .node_count = config->node_count, .schedule_crc32 = config->schedule_crc32,
        .profile_crc32 = config->ring_profile_crc32};
    /* This copy is made while the source is disabled. Thereafter the ISR has
     * its own counter; ordinary phase/event services never touch it. */
    s_tdma_priority_counter = s_tdma_pio_spi_rx_sequence;
    s_tdma_priority_stride = phys->flight_physical_byte_count;
    s_tdma_priority_geometry = false;
    s_tdma_priority_boundaries_seen = 0u;
    s_tdma_priority_phys = phys;
    irq_set_exclusive_handler(irq, tdma_priority_irq);
    irq_set_priority(irq, PICO_DEFAULT_IRQ_PRIORITY);
    irq_clear(irq);
    s_tdma_priority_installed = true;
    /* Keep a frame-end flag already pending after ARM. Enabling services it;
     * clearing it here would hide the first observed boundary. */
    pio_set_irq0_source_enabled(BOARD_TDMA_RX_PIO, pis_interrupt3, true);
    irq_set_enabled(irq, s_tdma_priority_window && s_tdma_priority_quota);
    return true;
}

static void tdma_priority_boundary_service(tdma_pio_spi_phys_t *phys)
{
    if (!s_tdma_priority_installed) {
        /* Outside the admitted priority cadence/persona the ordinary phase
         * retains its old ACK ownership. No live IRQ consumer exists. */
        if (pio_interrupt_get(BOARD_TDMA_RX_PIO, 3u)) {
            pio_interrupt_clear(BOARD_TDMA_RX_PIO, 3u);
            phys->snapshot.overlay_frame_boundary_count++;
            if (!phys->flight_overlay_pending && phys->flight_overlay_alignment_locked)
                phys->snapshot.overlay_reuse_observation_count++;
        }
        return;
    }
    if (s_tdma_priority_phys != phys) return;
    if (!s_tdma_priority_geometry && phys->flight_overlay_alignment_locked) {
        const uint32_t irq = save_and_disable_interrupts();
        s_tdma_priority_byte_shift = phys->flight_alignment_byte_shift;
        s_tdma_priority_bit_shift = phys->flight_alignment_bit_shift;
        __dmb();
        s_tdma_priority_geometry = true;
        restore_interrupts(irq);
    }
    tdma_priority_rx_snapshot_t snapshot;
    if (!tdma_priority_rx_snapshot(&s_tdma_priority_rx, &snapshot)) return;
    const uint32_t delta = snapshot.irq_count - s_tdma_priority_boundaries_seen;
    s_tdma_priority_boundaries_seen = snapshot.irq_count;
    phys->snapshot.overlay_frame_boundary_count += delta;
    if (!phys->flight_overlay_pending && phys->flight_overlay_alignment_locked)
        phys->snapshot.overlay_reuse_observation_count += delta;
}

bool tdma_pio_spi_phys_get_priority_rx_snapshot(tdma_priority_rx_snapshot_t *out)
{
    return tdma_priority_rx_snapshot(&s_tdma_priority_rx, out);
}
bool tdma_pio_spi_phys_get_priority_rx_timing(tdma_priority_rx_timing_t *out)
{
    if (!out) return false;
    const uint32_t before = __atomic_load_n(&s_tdma_priority_timing.guard, __ATOMIC_ACQUIRE);
    if (before & 1u) return false;
    tdma_priority_rx_timing_t value;
    value.samples = __atomic_load_n(&s_tdma_priority_timing.value.samples, __ATOMIC_RELAXED);
    for (uint32_t i = 0u; i < 4u; ++i)
        value.max_cycles[i] = __atomic_load_n(&s_tdma_priority_timing.value.max_cycles[i], __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (before != __atomic_load_n(&s_tdma_priority_timing.guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}
bool tdma_pio_spi_phys_priority_rx_counters_core1(tdma_priority_rx_counters_t *out)
{
    if (!out || get_core_num() != 1u ||
        irq_is_enabled(pio_get_irq_num(BOARD_TDMA_RX_PIO, 0u))) return false;
    /* The only writers are this core's masked IRQ and its serialized owner.
     * Neither can run concurrently here, including for the 64-bit total. */
    __dmb();
    const tdma_priority_rx_snapshot_t *const status = &s_tdma_priority_rx.status;
    *out = (tdma_priority_rx_counters_t){
        .cycles = status->irq_total_cycles, .epoch = status->epoch,
        .count = status->irq_count, .maximum = status->irq_max_cycles,
        .active = status->active};
    return true;
}
bool tdma_pio_spi_phys_copy_priority_rx(uint32_t epoch, uint32_t sequence,
    tdma_priority_rx_record_t *out)
{
    return tdma_priority_rx_copy(&s_tdma_priority_rx, epoch, sequence, out);
}
bool tdma_pio_spi_phys_copy_priority_rx_live(uint32_t epoch, uint32_t sequence,
    tdma_priority_rx_record_t *out)
{
    return tdma_priority_rx_copy_live(&s_tdma_priority_rx, epoch, sequence, out);
}
