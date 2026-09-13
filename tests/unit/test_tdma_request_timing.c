#include <assert.h>
#include "tdma_service_timing.h"
#define main adapter_legacy_main
#include "test_tdma_pio_spi_ring_adapter.c"
#undef main

static uint64_t timing_ticks;
uint64_t vdc_timestamp_clock_read_ticks64(void) { return ++timing_ticks; }
uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }
#include "../../components/tdma/src/tdma_service_timing.c"

static bool timed_observe_available;
static uint32_t timed_publication_calls;

static bool timed_origin_observe(void *context, tdma_origin_observation_t *observation)
{
    timing_ticks += 100u;
    return timed_observe_available && origin_adapter_observe(context, observation);
}

static bool timed_origin_publish(void *context, const uint8_t *mailbox, uint32_t *generation)
{
    timing_ticks += 1000u;
    ++timed_publication_calls;
    return origin_adapter_publish(context, mailbox, generation);
}

static void test_origin_service_timing(void)
{
    /* Real adapter/engine/FIFO paths, with distinct physical callback costs.
     * Empty, pending, unchanged, deferred and rejected updates still report
     * their attempted owner work; a pre-service fault reports neither stage. */
    for (uint32_t scenario = 0u; scenario < 8u; ++scenario) {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_flight_engine_t engine;
        tdma_flight_fifo_t fifo;
        tdma_ring_adapter_status_t status;
        origin_adapter_phys_t phys = {.active = true, .generation = 1u, .adapter = &adapter};
        const tdma_process_image_map_t map = make_eight_slot_flight_map();
        assert(tdma_pio_spi_ring_adapter_init(&adapter));
        assert(tdma_flight_fifo_init(&fifo) && tdma_flight_engine_init(&engine));
        assert(tdma_flight_engine_configure(&engine, &map) && tdma_flight_engine_activate(&engine, 0u));
        tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
        tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
        tdma_pio_spi_ring_adapter_set_phys(&adapter, origin_adapter_tx, origin_adapter_rx, &phys);
        tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter, NULL, origin_adapter_stop, NULL, NULL, &phys);
        const tdma_pio_spi_ring_origin_ops_t ops = {
            .begin = origin_adapter_start, .poll = origin_adapter_poll, .healthy = origin_adapter_healthy,
            .ready = origin_adapter_ready, .publish = timed_origin_publish,
            .observe = timed_origin_observe, .take_rx_observation = origin_adapter_take_pair};
        assert(tdma_pio_spi_ring_adapter_set_phys_origin(&adapter, &ops));
        adapter.config = make_valid_config();
        adapter.config.node_count = TDMA_FLIGHT_SHORT_SLOT_COUNT;
        adapter.config.local_slot_id = adapter.config.reference_slot_id = 0u;
        adapter.started = 1u;
        adapter.configured = true;
        adapter.origin.active = 1u;
        adapter.origin.published_generation = 1u;
        adapter.comm_fsm.state = TDMA_ADAPTER_COMM_STATE_AUTONOMOUS;
        uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
        fill_test_process_mailbox(mailbox, 0u, 1u, 0x31u);
        if (scenario == 6u) mailbox[12] ^= 1u;
        if (scenario != 0u)
            assert(tdma_flight_fifo_core0_publish_tx(&fifo, mailbox, sizeof(mailbox), 0x10001u, 1u, 1u));
        timed_observe_available = scenario != 7u;
        if (scenario == 3u) {
            assert(tdma_pio_spi_ring_adapter_ops()->service(&adapter, 1000u, &status));
            phys.pending = false;
        }
        if (scenario == 2u) phys.pending = true;
        if (scenario == 4u) phys.reject_publish = true;
        if (scenario == 5u) phys.fault = true;
        const uint32_t tail = fifo.tx_tail;
        timed_publication_calls = 0u;
        const uint32_t reset = tdma_service_timing_request_reset();
        tdma_service_timing_phase_begin();
        assert(tdma_pio_spi_ring_adapter_ops()->service(&adapter, 2000u, &status) == (scenario != 5u));
        tdma_service_timing_phase_end();
        tdma_service_timing_snapshot_t snapshot;
        assert(tdma_service_timing_try_snapshot(&snapshot) && snapshot.reset_generation == reset);
        const tdma_service_timing_record_t *record = &snapshot.last;
        assert(record->invalid_count == 0u && record->calls[TDMA_TIMING_ADAPTER] == 1u);
        assert(record->calls[TDMA_TIMING_ORIGIN_OBSERVE] == (scenario != 5u));
        assert(record->calls[TDMA_TIMING_ORIGIN_PUBLISH] == (scenario != 5u));
        assert(record->calls[TDMA_TIMING_RX_HANDOFF] == (scenario != 5u));
        assert(record->calls[TDMA_TIMING_OVERLAY_PREPARE] == 0u);
        assert(record->calls[TDMA_TIMING_OVERLAY_BOUNDARY] == 0u);
        if (scenario != 5u) {
            assert(record->elapsed_ticks[TDMA_TIMING_ORIGIN_OBSERVE] >= 100u);
            assert(record->elapsed_ticks[TDMA_TIMING_ORIGIN_OBSERVE] < 200u);
            assert(record->elapsed_ticks[TDMA_TIMING_ORIGIN_PUBLISH] >= 1000u * timed_publication_calls);
            assert(record->elapsed_ticks[TDMA_TIMING_ADAPTER] >=
                record->elapsed_ticks[TDMA_TIMING_ORIGIN_OBSERVE] +
                record->elapsed_ticks[TDMA_TIMING_ORIGIN_PUBLISH] +
                record->elapsed_ticks[TDMA_TIMING_RX_HANDOFF]);
        }
        assert(timed_publication_calls == (scenario == 1u || scenario == 4u || scenario == 7u));
        assert(phys.publications == (scenario == 1u || scenario == 3u || scenario == 7u));
        if (scenario == 0u || scenario == 2u || scenario == 3u || scenario == 5u)
            assert(fifo.tx_tail == tail);
        if (scenario == 4u || scenario == 6u) assert(adapter.origin.published_owner_generation == 0u);
        if (scenario == 7u) assert(adapter.origin.boundary.sequence == 0u);
        assert(tdma_pio_spi_ring_adapter_ops()->stop(&adapter));
    }
}

int main(void)
{
    const int failed = test_rx_prepare_cases() + test_origin_adapter();
    test_origin_service_timing();
    return failed;
}
