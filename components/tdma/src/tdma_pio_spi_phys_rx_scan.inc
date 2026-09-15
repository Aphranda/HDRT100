/* Only Core1 reads the live ring. Core0 discovers geometry in its private
 * snapshot; a returned hint is revalidated against current completed words. */
#include "tdma_service_timing.h"

static bool tdma_pio_spi_phys_rx_scan_cancel(tdma_pio_spi_phys_t *phys)
{
    phys->rx_scan_hint.valid = false;
    return tdma_rx_scan_cancel(phys->rx_scan_preparation);
}

static bool tdma_pio_spi_phys_rx_scan_request_window(tdma_pio_spi_phys_t *phys,
    uint64_t produced, uint32_t max_words, uint64_t start, uint32_t count,
    bool retire_cursor_on_failure)
{
    tdma_rx_scan_t *job = phys->rx_scan_preparation;
    if (tdma_rx_scan_state(job) != TDMA_RX_SCAN_IDLE) return false;
    if (produced < start || count < TDMA_PIO_SPI_PACKET_HEADER_SIZE ||
        count > sizeof(job->bytes) || count > produced - start ||
        produced - start >= TDMA_PIO_SPI_RX_RING_WORDS) return false;
    job->window_start = start;
    job->byte_count = count;
    job->max_frame_words = max_words;
    job->physical_frame_words = phys->flight_physical_byte_count;
    job->tail_words = phys->flight_tail_bytes;
    job->persona = s_tdma_pio_spi_program_persona;
    job->observation_epoch = s_tdma_pio_spi_rx_sequence.observation_epoch;
    const uint64_t copy_start = tdma_service_timing_now();
    tdma_pio_spi_phys_rx_ring_copy(job->bytes, start, job->byte_count, 0u);
    tdma_service_timing_record(TDMA_TIMING_RX_RING_COPY, copy_start);
    __dmb();
    const uint64_t observe_start = tdma_service_timing_now();
    const uint64_t after = tdma_pio_spi_phys_rx_produced_words(phys);
    tdma_service_timing_record(TDMA_TIMING_RX_DMA_DISCOVERY_RECHECK, observe_start);
    tdma_service_timing_record(TDMA_TIMING_RX_DMA_OBSERVE, observe_start);
    if (job->observation_epoch != s_tdma_pio_spi_rx_sequence.observation_epoch ||
        after < produced || after - start >= TDMA_PIO_SPI_RX_RING_WORDS) {
        tdma_service_timing_rx_drop(TDMA_RX_DROP_DISCOVERY_COPY, 0u);
        phys->snapshot.rx_observation_drop_count++;
        if (retire_cursor_on_failure)
            s_tdma_pio_spi_rx_scan_produced = after >= produced ? after : produced;
        return false;
    }
    return tdma_rx_scan_request(job);
}

static bool tdma_pio_spi_phys_rx_scan_request(tdma_pio_spi_phys_t *phys,
    uint64_t produced, uint32_t max_words)
{
    const uint64_t start = s_tdma_pio_spi_rx_scan_produced;
    if (produced < start || produced - start > TDMA_RX_SCAN_WINDOW_BYTES) return false;
    return tdma_pio_spi_phys_rx_scan_request_window(phys, produced, max_words,
        start, (uint32_t)(produced - start), true);
}

/* A one-frame hint can keep receiving while decimation skips every adjacent
 * station delivery. Re-discover two completed neighbours in a private copy;
 * selecting their predicted location does not grant live alignment. Core0
 * must still decode both frames. This never moves the ordinary RX cursor. */
__attribute__((noinline, noclone))
static bool tdma_pio_spi_phys_rx_scan_refresh(tdma_pio_spi_phys_t *phys,
    uint64_t produced, uint32_t max_words)
{
    const tdma_rx_scan_hint_t *hint = &phys->rx_scan_hint;
    if (!hint->valid || hint->stable_frames >= TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES ||
        hint->stride_words != phys->flight_physical_byte_count ||
        tdma_rx_scan_state(phys->rx_scan_preparation) != TDMA_RX_SCAN_IDLE) return false;
    const uint64_t count = (uint64_t)hint->stride_words + hint->frame_words + (hint->bit_shift != 0u);
    if (count > TDMA_RX_SCAN_WINDOW_BYTES || produced < count || hint->stride_words == 0u) return false;
    const uint64_t limit = produced - count;
    const uint64_t back = hint->stride_words - 1u;
    const uint64_t cursor = limit >= back ? limit - back : 0u;
    uint64_t start;
    if (!tdma_rx_scan_locate(hint, produced, cursor, &start) || start > limit) return false;
    (void)tdma_pio_spi_phys_rx_scan_request_window(phys, produced, max_words,
        start, (uint32_t)count, false);
    /* Even a rejected copy consumes this call's discovery allowance. */
    return true;
}

/* Worker result admission is outside the steady locked receive path. Keep
 * it in flash, along with training refresh, rather than growing the aligned
 * DMA workspace's SRAM allocation through cold control-code inlining. */
__attribute__((noinline, noclone))
static void tdma_pio_spi_phys_rx_scan_accept_ready(tdma_pio_spi_phys_t *phys,
    uint32_t max_words, uint64_t observation_epoch)
{
    tdma_rx_scan_t *job = phys->rx_scan_preparation;
    tdma_rx_scan_hint_t *hint = &phys->rx_scan_hint;
    if (job->request_epoch == job->epoch && job->observation_epoch == observation_epoch &&
        job->persona == s_tdma_pio_spi_program_persona && job->max_frame_words == max_words &&
        job->physical_frame_words == phys->flight_physical_byte_count &&
        job->tail_words == phys->flight_tail_bytes) {
        if (job->result.valid || !hint->valid) *hint = job->result;
        if (!job->result.valid) {
            phys->snapshot.rx_magic_fail_count++;
            phys->snapshot.rx_scan_yield_count++;
        }
    } else {
        hint->valid = false;
        tdma_service_timing_rx_drop(TDMA_RX_DROP_STALE_HINT, 0u);
        phys->snapshot.rx_observation_drop_count++;
    }
    tdma_rx_scan_release(job);
}

/* Format only the already-validated private-copy coordinates. This bounded
 * diagnostic helper never reads DMA/MMIO; keep it out of the resident control
 * path's code allocation so provenance does not consume a BSS alignment page. */
__attribute__((noinline, noclone))
static void tdma_pio_spi_phys_capture_record(tdma_rx_capture_t *capture,
    const tdma_rx_scan_hint_t *hint, uint64_t observation_epoch,
    uint64_t candidate, uint64_t produced, uint64_t after)
{
    const uint32_t shift = hint->bit_shift;
    if (s_tdma_pio_spi_rx_capture_id != UINT64_MAX) {
        ++s_tdma_pio_spi_rx_capture_id;
        if (capture != NULL && s_tdma_pio_spi_rx_arm_valid) {
            *capture = (tdma_rx_capture_t){
                .arm_epoch = s_tdma_pio_spi_rx_arm_epoch,
                .capture_id = s_tdma_pio_spi_rx_capture_id,
                .observation_epoch = observation_epoch,
                .candidate = candidate,
                .produced_before = produced,
                .produced_after = after,
                .frame_words = hint->frame_words,
                .bit_shift = shift,
                .persona = s_tdma_pio_spi_program_persona,
                .flags = TDMA_RX_CAPTURE_PRIVATE_COPY};
        }
    }
}

/* Keep the bounded live-window control path resident as well as the copy
 * leaf. Core0 discovery still supplies geometry only; completion, epoch and
 * overwrite checks stay together in the Core1 call. Account for this code
 * in the linked SRAM budget independently of any measured timing benefit.
 * Compact only this control function; the copy leaf retains its build options.
 * The dispatcher clears a non-NULL capture before entering this internal
 * function. Its only other caller is the legacy wrapper passing NULL. */
__attribute__((noinline, section(".time_critical.tdma_pio_spi_phys_capture_words_async"), optimize("Os")))
static bool tdma_pio_spi_phys_capture_words_async_ex(tdma_pio_spi_phys_t *phys,
    size_t max_words, size_t *received_words, tdma_rx_capture_t *capture)
{
    if (received_words != NULL) *received_words = 0u;
    if (phys == NULL || received_words == NULL || max_words == 0u || max_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX ||
        !phys->rx_capture_active || phys->flight_origin_workspace_owned ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN)
        return false;
    const uint64_t observe_start = tdma_service_timing_now();
    const uint64_t produced = tdma_pio_spi_phys_rx_produced_words(phys);
    tdma_service_timing_record(TDMA_TIMING_RX_DMA_INITIAL, observe_start);
    tdma_service_timing_record(TDMA_TIMING_RX_DMA_OBSERVE, observe_start);
    tdma_service_timing_rx_initial(observe_start,
        produced > s_tdma_pio_spi_rx_scan_produced ? produced - s_tdma_pio_spi_rx_scan_produced : 0u);
    const uint64_t observation_epoch = s_tdma_pio_spi_rx_sequence.observation_epoch;
    phys->snapshot.rx_dma_produced_words = (uint32_t)produced;
    phys->snapshot.rx_scan_produced_words = (uint32_t)s_tdma_pio_spi_rx_scan_produced;
    phys->snapshot.rx_dma_write_index = tdma_pio_spi_phys_rx_write_index();
    phys->snapshot.rx_dma_channel = (uint32_t)s_tdma_pio_spi_rx_dma_channel;
    tdma_rx_scan_t *job = phys->rx_scan_preparation;
    tdma_rx_scan_hint_t *hint = &phys->rx_scan_hint;
    if (hint->observation_epoch != observation_epoch) hint->valid = false;
    if (tdma_rx_scan_state(job) == TDMA_RX_SCAN_READY)
        tdma_pio_spi_phys_rx_scan_accept_ready(phys, (uint32_t)max_words, observation_epoch);
    if (produced <= s_tdma_pio_spi_rx_scan_produced) return false;
    if (produced - s_tdma_pio_spi_rx_scan_produced > TDMA_PIO_SPI_RX_RING_WORDS)
        phys->snapshot.rx_ring_overrun_count++;
    const uint32_t keep = (uint32_t)max_words + TDMA_RX_OBSERVATION_SCAN_WORDS;
    if (produced - s_tdma_pio_spi_rx_scan_produced > keep) {
        tdma_service_timing_rx_drop(TDMA_RX_DROP_CLAMP,
            produced - s_tdma_pio_spi_rx_scan_produced - keep);
        s_tdma_pio_spi_rx_scan_produced = produced - keep;
        phys->snapshot.rx_observation_drop_count++;
    }
    if (!hint->valid) {
        (void)tdma_pio_spi_phys_rx_scan_request(phys, produced, (uint32_t)max_words);
        return false;
    }
    const bool refresh_attempted = phys->process_image_enabled &&
        !phys->flight_overlay_alignment_locked &&
        phys->flight_overlay_alignment_samples < TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES &&
        tdma_pio_spi_phys_rx_scan_refresh(phys, produced, (uint32_t)max_words);
    uint64_t candidate;
    const uint64_t locate_start = tdma_service_timing_now();
    const bool located = tdma_rx_scan_locate(
        hint, produced, s_tdma_pio_spi_rx_scan_produced, &candidate);
    tdma_service_timing_record(TDMA_TIMING_RX_LOCATE, locate_start);
    if (!located) return false;
    const uint32_t shift = hint->bit_shift;
    const uint32_t size = hint->frame_words - TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    /* locate proves geometry and completion independently of packet bytes.
     * Copy once, then validate the same private bytes that phys_rx hands off.
     * No live-ring header read is needed before this bounded copy. */
    const uint64_t copy_start = tdma_service_timing_now();
    tdma_pio_spi_phys_rx_ring_copy(s_tdma_pio_spi_rx_frame,
        candidate, hint->frame_words, shift);
    tdma_service_timing_record(TDMA_TIMING_RX_RING_COPY, copy_start);
    __dmb();
    const uint64_t recheck_start = tdma_service_timing_now();
    const uint64_t after = tdma_pio_spi_phys_rx_produced_words(phys);
    tdma_service_timing_record(TDMA_TIMING_RX_DMA_FRAME_RECHECK, recheck_start);
    tdma_service_timing_record(TDMA_TIMING_RX_DMA_OBSERVE, recheck_start);
    if (s_tdma_pio_spi_rx_sequence.observation_epoch != observation_epoch ||
        after < produced || after - candidate >= TDMA_PIO_SPI_RX_RING_WORDS) {
        tdma_service_timing_rx_drop(TDMA_RX_DROP_FRAME_COPY, 0u);
        hint->valid = false;
        phys->snapshot.rx_observation_drop_count++;
        s_tdma_pio_spi_rx_scan_produced = after >= produced ? after : produced;
        return false;
    }
    const uint64_t header_start = tdma_service_timing_now();
    const uint8_t *header = s_tdma_pio_spi_rx_frame;
    const uint8_t *transport = header + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    const bool header_invalid =
        header[0] != TDMA_PIO_SPI_PACKET_MAGIC0 ||
        header[1] != TDMA_PIO_SPI_PACKET_MAGIC1 ||
        ((uint32_t)header[2] | ((uint32_t)header[3] << 8u)) != size ||
        ((uint32_t)transport[0] | ((uint32_t)transport[1] << 8u)) != TDMA_TRANSPORT_FRAME_MAGIC ||
        transport[2] != TDMA_TRANSPORT_FRAME_VERSION ||
        (transport[3] != TDMA_TRANSPORT_FRAME_CLASS_SHORT &&
         transport[3] != TDMA_TRANSPORT_FRAME_CLASS_LONG) ||
        ((uint32_t)transport[4] | ((uint32_t)transport[5] << 8u)) != size ||
        transport[6] != TDMA_TRANSPORT_FRAME_HEADER_SIZE;
    tdma_service_timing_record(TDMA_TIMING_RX_HEADER_CHECK, header_start);
    if (header_invalid) {
        hint->valid = false;
        if (!refresh_attempted)
            (void)tdma_pio_spi_phys_rx_scan_request(phys, after, (uint32_t)max_words);
        return false;
    }
    s_tdma_pio_spi_rx_scan_produced = candidate + hint->frame_words;
    if (phys->process_image_enabled && !phys->flight_overlay_alignment_locked &&
        phys->flight_physical_byte_count != 0u && hint->stride_words == phys->flight_physical_byte_count) {
        const uint32_t byte_shift = (uint32_t)(candidate % phys->flight_physical_byte_count);
        if (phys->flight_overlay_alignment_samples != 0u &&
            candidate - phys->flight_overlay_alignment_candidate == phys->flight_physical_byte_count &&
            byte_shift == phys->flight_alignment_byte_shift && shift == phys->flight_alignment_bit_shift) {
            if (phys->flight_overlay_alignment_samples < TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES)
                phys->flight_overlay_alignment_samples++;
        } else {
            phys->flight_overlay_alignment_samples = hint->stable_frames >= TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES
                ? TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES : 1u;
        }
        phys->flight_overlay_alignment_candidate = candidate;
        phys->flight_alignment_byte_shift = byte_shift;
        phys->flight_alignment_bit_shift = shift;
    }
    if (shift == 0u) phys->snapshot.rx_magic_at_zero++;
    else phys->snapshot.rx_magic_at_shift++;
    *received_words = hint->frame_words;
    tdma_pio_spi_phys_capture_record(capture, hint, observation_epoch,
                                    candidate, produced, after);
    return true;
}

static bool tdma_pio_spi_phys_capture_words_async(tdma_pio_spi_phys_t *phys,
    size_t max_words, size_t *received_words)
{
    return tdma_pio_spi_phys_capture_words_async_ex(phys, max_words, received_words, NULL);
}
