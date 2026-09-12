/* Only Core1 reads the live ring. Core0 discovers geometry in its private
 * snapshot; a returned hint is revalidated against current completed words. */
static bool tdma_pio_spi_phys_rx_scan_cancel(tdma_pio_spi_phys_t *phys)
{
    phys->rx_scan_hint.valid = false;
    return tdma_rx_scan_cancel(phys->rx_scan_preparation);
}

static bool tdma_pio_spi_phys_rx_scan_request(tdma_pio_spi_phys_t *phys,
    uint64_t produced, uint32_t max_words)
{
    tdma_rx_scan_t *job = phys->rx_scan_preparation;
    if (tdma_rx_scan_state(job) != TDMA_RX_SCAN_IDLE) return false;
    const uint64_t start = s_tdma_pio_spi_rx_scan_produced;
    if (produced < start || produced - start < TDMA_PIO_SPI_PACKET_HEADER_SIZE ||
        produced - start > sizeof(job->bytes)) return false;
    job->window_start = start;
    job->byte_count = (uint32_t)(produced - start);
    job->max_frame_words = max_words;
    job->physical_frame_words = phys->flight_physical_byte_count;
    job->tail_words = phys->flight_tail_bytes;
    job->persona = s_tdma_pio_spi_program_persona;
    job->observation_epoch = s_tdma_pio_spi_rx_sequence.observation_epoch;
    for (uint32_t i = 0u; i < job->byte_count; ++i)
        job->bytes[i] = tdma_pio_spi_phys_rx_ring_byte(start + i);
    __dmb();
    const uint64_t after = tdma_pio_spi_phys_rx_produced_words(phys);
    if (job->observation_epoch != s_tdma_pio_spi_rx_sequence.observation_epoch ||
        after < produced || after - start >= TDMA_PIO_SPI_RX_RING_WORDS) {
        phys->snapshot.rx_observation_drop_count++;
        s_tdma_pio_spi_rx_scan_produced = after >= produced ? after : produced;
        return false;
    }
    return tdma_rx_scan_request(job);
}

static bool tdma_pio_spi_phys_capture_words_async(tdma_pio_spi_phys_t *phys,
    size_t max_words, size_t *received_words)
{
    if (received_words != NULL) *received_words = 0u;
    if (received_words == NULL || max_words == 0u || max_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX ||
        !phys->rx_capture_active || phys->flight_origin_workspace_owned ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN)
        return false;
    const uint64_t produced = tdma_pio_spi_phys_rx_produced_words(phys);
    const uint64_t observation_epoch = s_tdma_pio_spi_rx_sequence.observation_epoch;
    phys->snapshot.rx_dma_produced_words = (uint32_t)produced;
    phys->snapshot.rx_scan_produced_words = (uint32_t)s_tdma_pio_spi_rx_scan_produced;
    phys->snapshot.rx_dma_write_index = tdma_pio_spi_phys_rx_write_index();
    phys->snapshot.rx_dma_channel = (uint32_t)s_tdma_pio_spi_rx_dma_channel;
    tdma_rx_scan_t *job = phys->rx_scan_preparation;
    tdma_rx_scan_hint_t *hint = &phys->rx_scan_hint;
    if (hint->observation_epoch != observation_epoch) hint->valid = false;
    if (tdma_rx_scan_state(job) == TDMA_RX_SCAN_READY) {
        if (job->request_epoch == job->epoch && job->observation_epoch == observation_epoch &&
            job->persona == s_tdma_pio_spi_program_persona && job->max_frame_words == max_words &&
            job->physical_frame_words == phys->flight_physical_byte_count &&
            job->tail_words == phys->flight_tail_bytes) {
            *hint = job->result;
            if (!hint->valid) {
                phys->snapshot.rx_magic_fail_count++;
                phys->snapshot.rx_scan_yield_count++;
            }
        } else {
            hint->valid = false;
            phys->snapshot.rx_observation_drop_count++;
        }
        tdma_rx_scan_release(job);
    }
    if (produced <= s_tdma_pio_spi_rx_scan_produced) return false;
    if (produced - s_tdma_pio_spi_rx_scan_produced > TDMA_PIO_SPI_RX_RING_WORDS)
        phys->snapshot.rx_ring_overrun_count++;
    const uint32_t keep = (uint32_t)max_words + TDMA_RX_OBSERVATION_SCAN_WORDS;
    if (produced - s_tdma_pio_spi_rx_scan_produced > keep) {
        s_tdma_pio_spi_rx_scan_produced = produced - keep;
        phys->snapshot.rx_observation_drop_count++;
    }
    if (!hint->valid) {
        (void)tdma_pio_spi_phys_rx_scan_request(phys, produced, (uint32_t)max_words);
        return false;
    }
    uint64_t candidate;
    if (!tdma_rx_scan_locate(hint, produced, s_tdma_pio_spi_rx_scan_produced, &candidate)) return false;
    const uint32_t shift = hint->bit_shift;
    const uint32_t size = hint->frame_words - TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    if (tdma_pio_spi_phys_rx_ring_aligned_byte(candidate, shift) != TDMA_PIO_SPI_PACKET_MAGIC0 ||
        tdma_pio_spi_phys_rx_ring_aligned_byte(candidate + 1u, shift) != TDMA_PIO_SPI_PACKET_MAGIC1 ||
        ((uint32_t)tdma_pio_spi_phys_rx_ring_aligned_byte(candidate + 2u, shift) |
          ((uint32_t)tdma_pio_spi_phys_rx_ring_aligned_byte(candidate + 3u, shift) << 8u)) != size ||
        !tdma_pio_spi_phys_transport_header_matches(candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE, shift, (uint16_t)size)) {
        hint->valid = false;
        (void)tdma_pio_spi_phys_rx_scan_request(phys, produced, (uint32_t)max_words);
        return false;
    }
    for (uint32_t i = 0u; i < hint->frame_words; ++i)
        s_tdma_pio_spi_rx_frame[i] = tdma_pio_spi_phys_rx_ring_aligned_byte(candidate + i, shift);
    __dmb();
    const uint64_t after = tdma_pio_spi_phys_rx_produced_words(phys);
    if (s_tdma_pio_spi_rx_sequence.observation_epoch != observation_epoch ||
        after < produced || after - candidate >= TDMA_PIO_SPI_RX_RING_WORDS) {
        hint->valid = false;
        phys->snapshot.rx_observation_drop_count++;
        s_tdma_pio_spi_rx_scan_produced = after >= produced ? after : produced;
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
    return true;
}
