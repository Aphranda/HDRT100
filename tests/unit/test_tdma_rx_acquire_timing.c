/* Run the real async acquire path and recorder on the advancing DMA bus. */
#define TDMA_SERVICE_TIMING_ENABLED 1
#define main legacy_scanner_main
#include "test_tdma_rx_observation.c"
#undef main

uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }
#include "../../components/tdma/src/tdma_service_timing.c"

static tdma_service_timing_record_t capture_profile(tdma_pio_spi_phys_t *phys,
    bool expected_capture)
{
    size_t received = 123;
    tdma_service_timing_phase_begin();
    const uint64_t start = tdma_service_timing_now();
    const bool captured = tdma_pio_spi_phys_capture_words(
        phys, TDMA_PIO_SPI_RX_DMA_WORD_MAX, &received);
    tdma_service_timing_record(TDMA_TIMING_RX_ACQUIRE, start);
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, start);
    tdma_service_timing_phase_end();
    assert(captured == expected_capture);
    assert(received == (expected_capture ? TDMA_PIO_SPI_RX_DMA_WORD_MAX : 0));
    tdma_service_timing_snapshot_t snapshot;
    assert(tdma_service_timing_try_snapshot(&snapshot));
    assert(snapshot.version == TDMA_SERVICE_TIMING_VERSION && snapshot.last.invalid_count == 0);
    const tdma_service_timing_record_t *last = &snapshot.last;
    uint32_t child_ticks = 0;
    for (uint32_t stage = TDMA_TIMING_RX_DMA_OBSERVE;
         stage <= TDMA_TIMING_RX_RING_COPY; ++stage)
        child_ticks += last->elapsed_ticks[stage];
    assert(child_ticks <= last->elapsed_ticks[TDMA_TIMING_RX_ACQUIRE]);
    uint32_t observe_ticks = 0u, observe_calls = 0u;
    for (uint32_t stage = TDMA_TIMING_RX_DMA_INITIAL;
         stage <= TDMA_TIMING_RX_DMA_DISCOVERY_RECHECK; ++stage) {
        observe_ticks += last->elapsed_ticks[stage];
        observe_calls += last->calls[stage];
    }
    assert(observe_ticks <= last->elapsed_ticks[TDMA_TIMING_RX_DMA_OBSERVE]);
    assert(observe_calls == last->calls[TDMA_TIMING_RX_DMA_OBSERVE]);
    assert(last->elapsed_ticks[TDMA_TIMING_RX_ACQUIRE] <= last->total_ticks);
    return *last;
}

int main(void)
{
    /* The discovery copy must not masquerade as a live located packet. */
    for (unsigned overwrite = 0; overwrite < 2; ++overwrite) {
        tdma_rx_scan_t job = {0};
        tdma_pio_spi_phys_t phys = setup(0, 384, 1000);
        phys.rx_scan_preparation = &job;
        tdma_service_timing_request_reset();
        tdma_service_timing_record_t record = capture_profile(&phys, false);
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_REQUESTED);
        assert(record.calls[TDMA_TIMING_RX_DMA_INITIAL] == 1);
        assert(record.calls[TDMA_TIMING_RX_DMA_DISCOVERY_RECHECK] == 1);
        assert(record.calls[TDMA_TIMING_RX_DMA_FRAME_RECHECK] == 0);
        assert(record.calls[TDMA_TIMING_RX_DMA_OBSERVE] == 2);
        assert(record.calls[TDMA_TIMING_RX_RING_COPY] == 1);
        assert(record.calls[TDMA_TIMING_RX_LOCATE] == 0);
        assert(record.calls[TDMA_TIMING_RX_HEADER_CHECK] == 0);
        tdma_rx_scan_core0_service(&job);
        assert(tdma_rx_scan_state(&job) == TDMA_RX_SCAN_READY && job.result.valid);
        stimulus = overwrite;
        record = capture_profile(&phys, !overwrite);
        tdma_rx_timing_snapshot_t rx_timing;
        assert(tdma_service_timing_rx_try_snapshot(&rx_timing));
        assert(rx_timing.initial_observation_count == 2u && rx_timing.initial_gap_count == 1u);
        assert(rx_timing.initial_gap_max_ticks > 0u && rx_timing.invalid_count == 0u);
        assert(rx_timing.drop_count[TDMA_RX_DROP_CLAMP] == 1u);
        assert(rx_timing.clamp_skipped_words == 1000u - TDMA_PIO_SPI_RX_DMA_WORD_MAX - TDMA_RX_OBSERVATION_SCAN_WORDS);
        assert(rx_timing.drop_count[TDMA_RX_DROP_FRAME_COPY] == overwrite);
        assert(rx_timing.drop_count[TDMA_RX_DROP_DISCOVERY_COPY] == 0u);
        assert(record.calls[TDMA_TIMING_RX_DMA_INITIAL] == 1);
        assert(record.calls[TDMA_TIMING_RX_DMA_FRAME_RECHECK] == 1);
        assert(record.calls[TDMA_TIMING_RX_DMA_DISCOVERY_RECHECK] == 0);
        assert(record.calls[TDMA_TIMING_RX_DMA_OBSERVE] == 2);
        assert(record.calls[TDMA_TIMING_RX_LOCATE] == 1);
        assert(record.calls[TDMA_TIMING_RX_HEADER_CHECK] == (overwrite ? 0u : 1u));
        assert(record.calls[TDMA_TIMING_RX_RING_COPY] == 1);
        assert(record.elapsed_ticks[TDMA_TIMING_RX_RING_COPY] > 0);
        if (overwrite) {
            assert(stimulus_ran && phys.snapshot.rx_observation_drop_count > 0);
            assert(!phys.rx_scan_hint.valid);
        } else {
            assert(phys.snapshot.rx_magic_at_zero == 1);
            assert(s_tdma_pio_spi_rx_frame[0] == TDMA_PIO_SPI_PACKET_MAGIC0);
            assert(s_tdma_pio_spi_rx_frame[1] == TDMA_PIO_SPI_PACKET_MAGIC1);
        }
        assert(tdma_pio_spi_phys_rx_scan_cancel(&phys));
        assert(!phys.rx_scan_hint.valid);
    }
    puts("PASS: async discovery/live timing, inclusive children and rejected overwrite evidence");
    return 0;
}
