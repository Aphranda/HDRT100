#include <assert.h>

typedef struct {
    loopback_phys_t wire;
    uint32_t latch_calls, feedback_calls, stops;
    uint64_t latch;
    uint64_t next_capture;
    bool fail_capture, unqualified_capture;
    uint32_t pin_epoch, pin_calls, query_calls, queried_epoch, queried_sequence;
    uint64_t queried_age_ns;
    size_t queried_size;
    tdma_rx_capture_t queried_capture;
    uint8_t *last_packet;
    size_t last_capacity;
} rx_station_phys_t;

static uint32_t rx_station_event_pin(void *context, const tdma_rx_capture_t *capture)
{
    rx_station_phys_t *phys = context;
    assert(capture->flags == TDMA_RX_CAPTURE_PRIVATE_COPY);
    ++phys->pin_calls;
    return phys->pin_epoch;
}

static void rx_station_event_query(void *context, const tdma_rx_capture_t *capture,
    uint32_t epoch, uint32_t sequence, size_t size, uint64_t age_ns)
{
    rx_station_phys_t *phys = context;
    ++phys->query_calls;
    phys->queried_capture = *capture;
    phys->queried_epoch = epoch;
    phys->queried_sequence = sequence;
    phys->queried_size = size;
    phys->queried_age_ns = age_ns;
}

static bool rx_station_capture(void *context, uint8_t *packet, size_t capacity,
    size_t *size, uint64_t *timestamp, tdma_rx_capture_t *capture)
{
    rx_station_phys_t *phys = context;
    phys->last_packet = packet;
    phys->last_capacity = capacity;
    const bool received = loopback_rx(context, packet, capacity, size, timestamp);
    if (capture != NULL) *capture = (tdma_rx_capture_t){
        .arm_epoch = 17u, .capture_id = ++phys->next_capture,
        .observation_epoch = 3u, .candidate = phys->next_capture * 301u,
        .produced_before = phys->next_capture * 301u + 302u,
        .produced_after = phys->next_capture * 301u + 303u,
        .frame_words = (uint32_t)*size + 4u, .bit_shift = 5u, .persona = 8u,
        .flags = TDMA_RX_CAPTURE_PRIVATE_COPY};
    if (capture != NULL && phys->unqualified_capture) *capture = (tdma_rx_capture_t){0};
    /* Deliberately return a dirty object on failure: adapter must not publish it. */
    return received && !phys->fail_capture;
}

static bool rx_station_edge(void *context, uint32_t sequence, uint32_t identity,
                            tdma_ring_local_tx_edge_evidence_t *e)
{
    rx_station_phys_t *p = context;
    p->latch_calls++;
    *e = (tdma_ring_local_tx_edge_evidence_t){.timestamp_ns = p->latch,
        .sequence = sequence, .identity_crc32 = identity, .capture_generation = p->latch_calls,
        .flags = TDMA_RING_LOCAL_TX_EDGE_FLAG_TIMESTAMP_VALID |
            TDMA_RING_LOCAL_TX_EDGE_FLAG_SEQUENCE_BOUND | TDMA_RING_LOCAL_TX_EDGE_FLAG_IDENTITY_BOUND};
    return true;
}

static bool rx_station_feedback(void *context, uint32_t *rtt, uint32_t *res, uint32_t *flags)
{
    rx_station_phys_t *p = context;
    p->feedback_calls++;
    *rtt = (uint32_t)p->latch; *res = 1u; *flags = TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    return true;
}

static bool rx_station_stop(void *context)
{
    ((rx_station_phys_t *)context)->stops++;
    return true;
}

static int test_rx_queue_storage_lifetime(void)
{
    int failed = 0;
    const tdma_ring_adapter_ops_t *ops = tdma_pio_spi_ring_adapter_ops();
    for (uint32_t mode = 0u; mode < 3u; mode++) {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_rx_prepare_t external = {0};
        rx_station_phys_t phys = {.wire = {.rx_timestamp_ns = 123u, .suppress_echo = true}};
        tdma_ring_adapter_status_t status;
        tdma_ring_runtime_config_t config = make_valid_config();
        config.node_count = 4u; config.local_slot_id = 1u; config.feedback_timeout_ns = 4000u;
        tdma_pio_spi_ring_adapter_init(&adapter);
        if (mode == 1u) adapter.rx_preparation = &external;
        if (mode == 2u) assert(tdma_pio_spi_ring_adapter_enable_async_rx(&adapter));
        tdma_rx_prepare_t *job = adapter.rx_preparation;
        set_test_sequential_topology(&adapter, 4u);
        tdma_pio_spi_ring_adapter_set_phys(&adapter, loopback_tx, loopback_rx, &phys);
        tdma_pio_spi_ring_adapter_set_phys_rx_ex(&adapter, rx_station_capture);
        tdma_pio_spi_ring_adapter_set_forwarding_mode(&adapter, TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT);
        assert(ops->start(&adapter, &config));
        uint8_t payload[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE] = {0};
        for (uint32_t slot = 0u; slot < 4u; slot++)
            fill_test_process_mailbox(payload + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE, slot, 7u, (uint8_t)slot);
        tdma_transport_frame_build_t build = {.frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = 0u, .transport_sequence = 81u,
            .payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
            .schedule_crc32 = config.schedule_crc32, .ring_profile_crc32 = config.ring_profile_crc32,
            .hop_limit = 3u, .payload = payload, .payload_size = sizeof(payload)};
        uint8_t queued[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t queued_size;
        tdma_transport_result_t result;
        assert(tdma_transport_frame_encode(&build, phys.wire.echo_packet,
            sizeof(phys.wire.echo_packet), &phys.wire.echo_packet_size, &result));
        if (job != NULL) {
            phys.wire.rx_pending = true;
            ops->service(&adapter, 100u, &status);
            assert(tdma_rx_prepare_state(job) == TDMA_RX_PREPARE_REQUESTED);
            assert(phys.last_packet == job->packet && phys.last_capacity == sizeof(job->packet));
            assert(adapter.rx_count == 0u);
        }
        build.transport_sequence = 82u;
        assert(tdma_transport_frame_encode(&build, queued, sizeof(queued), &queued_size, &result));
        failed += expect_bool("RX storage injected queue eligibility",
            tdma_pio_spi_ring_adapter_inject_rx(&adapter, queued, queued_size, 124u), mode != 2u);
        const uint32_t calls = phys.wire.rx_calls;
        if (job != NULL) {
            ops->service(&adapter, 101u, &status);
            assert(phys.wire.rx_calls == calls && adapter.rx_count == 0u);
            assert(adapter.rx_queue_count == (mode == 1u ? 1u : 0u));
            tdma_rx_prepare_core0_service(job);
            ops->service(&adapter, 102u, &status);
            assert(adapter.rx_count == 1u && adapter.down_rx_sequence == 81u);
            assert(tdma_rx_prepare_state(job) == TDMA_RX_PREPARE_IDLE);
            assert(adapter.rx_queue_count == (mode == 1u ? 1u : 0u));
        }
        build.transport_sequence = 83u;
        assert(tdma_transport_frame_encode(&build, phys.wire.echo_packet,
            sizeof(phys.wire.echo_packet), &phys.wire.echo_packet_size, &result));
        phys.wire.rx_pending = true;
        if (mode != 2u) {
            ops->service(&adapter, 103u, &status);
            assert(adapter.down_rx_sequence == 82u && adapter.rx_queue_count == 0u);
            assert(phys.wire.rx_calls == calls); /* Queue precedes another DMA capture. */
            if (job != NULL) assert(tdma_rx_prepare_state(job) == TDMA_RX_PREPARE_IDLE);
        }
        ops->service(&adapter, 104u, &status);
        assert(phys.wire.rx_calls == calls + 1u);
        if (job != NULL) {
            assert(phys.last_packet == job->packet && tdma_rx_prepare_state(job) == TDMA_RX_PREPARE_REQUESTED);
            tdma_rx_prepare_core0_service(job);
            ops->service(&adapter, 105u, &status);
        }
        assert(adapter.down_rx_sequence == 83u);
        assert(adapter.rx_count == (mode == 1u ? 3u : 2u));
        assert(ops->stop(&adapter));
    }
    return failed;
}

static int test_rx_prepare_cases(void)
{
    int failed = 0;
    const tdma_ring_adapter_ops_t *ops = tdma_pio_spi_ring_adapter_ops();
    tdma_pio_spi_ring_adapter_t adapter;
    tdma_rx_prepare_t job = {0};
    rx_station_phys_t phys = {.wire = {.rx_timestamp_ns = 123u, .suppress_echo = true},
        .latch = 456u, .pin_epoch = 29u};
    tdma_ring_adapter_status_t status;
    tdma_ring_runtime_config_t config = make_valid_config();
    config.node_count = 4u; config.local_slot_id = 1u;
    config.feedback_timeout_ns = 4000u;
    tdma_pio_spi_ring_adapter_init(&adapter);
    adapter.rx_preparation = &job;
    set_test_sequential_topology(&adapter, 4u);
    tdma_pio_spi_ring_adapter_set_phys(&adapter, loopback_tx, loopback_rx, &phys);
    tdma_pio_spi_ring_adapter_set_phys_rx_ex(&adapter, rx_station_capture);
    tdma_pio_spi_ring_adapter_set_phys_rx_event(&adapter, rx_station_event_pin, rx_station_event_query);
    tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter, NULL, rx_station_stop, NULL, NULL, &phys);
    tdma_pio_spi_ring_adapter_set_phys_local_tx_edge_ex(&adapter, rx_station_edge);
    tdma_pio_spi_ring_adapter_set_forwarding_mode(&adapter, TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT);
    failed += expect_bool("RX station starts", ops->start(&adapter, &config), true);
    uint8_t payload[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE] = {0};
    for (uint32_t i = 0u; i < 4u; ++i)
        fill_test_process_mailbox(payload + i * TDMA_FLIGHT_SHORT_SLOT_SIZE, i, 7u, (uint8_t)i);
    tdma_transport_frame_build_t build = {.frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id = 0u, .transport_sequence = 41u,
        .payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
        .schedule_crc32 = config.schedule_crc32, .ring_profile_crc32 = config.ring_profile_crc32,
        .hop_limit = 3u, .payload = payload, .payload_size = sizeof(payload)};
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t size;
    tdma_transport_result_t result;
    failed += expect_bool("RX station encode", tdma_transport_frame_encode(&build,
        packet, sizeof(packet), &size, &result), true);
    memcpy(phys.wire.echo_packet, packet, size);
    phys.wire.echo_packet_size = size; phys.wire.rx_pending = true;
#if TDMA_SERVICE_TIMING_ENABLED
    tdma_service_timing_request_reset();
    tdma_service_timing_phase_begin();
#endif
    ops->service(&adapter, 100u, &status);
#if TDMA_SERVICE_TIMING_ENABLED
    tdma_service_timing_phase_end();
    tdma_service_timing_snapshot_t timing;
    assert(tdma_service_timing_try_snapshot(&timing));
    assert(timing.last.invalid_count == 0u);
    assert(timing.last.calls[TDMA_TIMING_RX_REQUEST] == 1u);
    assert(timing.last.calls[TDMA_TIMING_RX_REQUEST_HINT] == 1u);
    assert(timing.last.calls[TDMA_TIMING_RX_LOCAL_TX_EDGE] == 1u);
    assert(timing.last.calls[TDMA_TIMING_RX_REQUEST_PUBLISH] == 1u);
    assert(timing.last.calls[TDMA_TIMING_RX_PARSE] == 0u);
    assert(timing.last.elapsed_ticks[TDMA_TIMING_RX_REQUEST_HINT] +
        timing.last.elapsed_ticks[TDMA_TIMING_RX_LOCAL_TX_EDGE] +
        timing.last.elapsed_ticks[TDMA_TIMING_RX_REQUEST_PUBLISH] <=
        timing.last.elapsed_ticks[TDMA_TIMING_RX_REQUEST]);
    assert(timing.last.elapsed_ticks[TDMA_TIMING_RX_CAPTURE] +
        timing.last.elapsed_ticks[TDMA_TIMING_RX_REQUEST] <=
        timing.last.elapsed_ticks[TDMA_TIMING_RX_HANDOFF]);
#endif
    failed += expect_u32("RX admission not acceptance", adapter.rx_count, 0u);
    failed += expect_u32("RX admitted", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_REQUESTED);
    const tdma_rx_capture_t first_capture = job.capture;
    const uint32_t first_cancel_epoch = job.request_epoch;
    assert(job.capture_observer_epoch == 29u && phys.pin_calls == 1u && phys.query_calls == 0u);
    phys.pin_epoch = 30u; /* READY must not borrow this new observer epoch. */
    failed += expect_u64("capture belongs to this receive", first_capture.capture_id, 1u);
    failed += expect_u32("private copy is diagnostic only", first_capture.flags, TDMA_RX_CAPTURE_PRIVATE_COPY);
    failed += expect_u32("physical envelope retained", first_capture.frame_words, (uint32_t)size + 4u);
    failed += expect_u32("latch drained at admission", phys.latch_calls, 1u);
    const uint32_t captures = phys.wire.rx_calls;
    memset(phys.wire.echo_packet, 0xA5, size); phys.latch = 999u;
    for (uint32_t i = 0u; i < 20u; ++i) {
        if (i == 10u) assert(tdma_rx_prepare_core0_claim(&job));
#if TDMA_SERVICE_TIMING_ENABLED
        tdma_service_timing_phase_begin();
#endif
        ops->service(&adapter, 101u + i, &status);
#if TDMA_SERVICE_TIMING_ENABLED
        tdma_service_timing_phase_end();
#endif
    }
    failed += expect_u32("paused worker never reacquires RX", phys.wire.rx_calls, captures);
    failed += expect_bool("DMA source reusable", memcmp(job.packet, packet, size) == 0, true);
    tdma_rx_prepare_core0_build_claimed(&job);
    failed += expect_u32("worker cannot commit adapter", adapter.rx_count, 0u);
    failed += expect_u32("worker ready", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_READY);
    failed += expect_bool("worker preserves capture bytes",
        memcmp(&first_capture, &job.capture, sizeof(first_capture)) == 0, true);
#if TDMA_SERVICE_TIMING_ENABLED
    tdma_service_timing_phase_begin();
#endif
    ops->service(&adapter, 150u, &status);
#if TDMA_SERVICE_TIMING_ENABLED
    tdma_service_timing_phase_end();
    assert(tdma_service_timing_try_snapshot(&timing));
    assert(timing.last.calls[TDMA_TIMING_RX_CAPTURE] == 0u);
    assert(timing.last.calls[TDMA_TIMING_RX_REQUEST] == 0u);
    assert(timing.last.calls[TDMA_TIMING_RX_LOCAL_TX_EDGE] == 0u);
    assert(timing.last.calls[TDMA_TIMING_RX_REQUEST_PUBLISH] == 0u);
    assert(timing.last.calls[TDMA_TIMING_RX_PARSE] == 1u);
    tdma_rx_timing_snapshot_t rx_timing;
    assert(tdma_service_timing_rx_try_snapshot(&rx_timing));
    assert(rx_timing.invalid_count == 0u);
    assert(rx_timing.station_polls[TDMA_RX_PREPARE_IDLE] == 1u);
    assert(rx_timing.station_polls[TDMA_RX_PREPARE_REQUESTED] == 10u);
    assert(rx_timing.station_polls[TDMA_RX_PREPARE_BUILDING] == 10u);
    assert(rx_timing.station_polls[TDMA_RX_PREPARE_READY] == 1u);
    assert(rx_timing.station_age_max_ns[TDMA_RX_PREPARE_REQUESTED - 1u] == 10u);
    assert(rx_timing.station_age_max_ns[TDMA_RX_PREPARE_BUILDING - 1u] == 20u);
    assert(rx_timing.station_age_max_ns[TDMA_RX_PREPARE_READY - 1u] == 50u);
#endif
    failed += expect_u32("Core1 accepts RX", adapter.rx_count, 1u);
    failed += expect_u32("no late latch read", phys.latch_calls, 1u);
    failed += expect_u64("original RX time", adapter.feedback_rx_timestamp_ns, 123u);
    failed += expect_u64("original TX time", adapter.local_tx_edge_timestamp_ns, 456u);
    assert(phys.pin_calls == 1u && phys.query_calls == 1u);
    assert(phys.queried_epoch == 29u && phys.queried_sequence == build.transport_sequence);
    assert(phys.queried_age_ns == 50u && phys.queried_size == size);
    assert(memcmp(&phys.queried_capture, &first_capture, sizeof(first_capture)) == 0);

    /* Cancellation epoch stays fixed for successive packets; the capture ID does not. */
    memcpy(phys.wire.echo_packet, packet, size); phys.wire.rx_pending = true;
    ops->service(&adapter, 160u, &status);
    failed += expect_u32("same cancellation epoch", job.request_epoch, first_cancel_epoch);
    failed += expect_bool("different capture ID", job.capture.capture_id > first_capture.capture_id, true);
    tdma_rx_prepare_core0_service(&job);
    ops->service(&adapter, 161u, &status);
    assert(phys.query_calls == 2u && phys.queried_epoch == 30u);
    assert(phys.queried_capture.capture_id != first_capture.capture_id);
    const uint32_t good_pins = phys.pin_calls;
    phys.fail_capture = true; phys.wire.rx_pending = true;
    ops->service(&adapter, 162u, &status);
    failed += expect_u32("failed copy not requested", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_IDLE);
    const tdma_rx_capture_t unavailable = {0};
    failed += expect_bool("failed callback cannot leak old provenance",
        memcmp(&unavailable, &job.capture, sizeof(unavailable)) == 0, true);
    phys.fail_capture = false;
    assert(job.capture_observer_epoch == 0u && phys.pin_calls == good_pins && phys.query_calls == 2u);
    /* A true receive without provenance remains ordinary transport; it does
     * not pay for an empty candidate publication on every legacy/origin RX. */
    phys.unqualified_capture = true;
    memcpy(phys.wire.echo_packet, packet, size); phys.wire.rx_pending = true;
    ops->service(&adapter, 170u, &status);
    assert(job.capture_observer_epoch == 0u && phys.pin_calls == good_pins);
    tdma_rx_prepare_core0_service(&job);
    ops->service(&adapter, 171u, &status);
    assert(phys.query_calls == 2u);
    phys.unqualified_capture = false;

    /* STOP at REQUESTED, BUILDING, READY; only a claimed worker delays ACK.
     * No continuation may touch the new ARM's slot after ACK. */
    for (uint32_t phase = 0u; phase < 3u; ++phase) {
        memcpy(phys.wire.echo_packet, packet, size); phys.wire.rx_pending = true;
        ops->service(&adapter, 200u, &status);
        if (phase == 1u) failed += expect_bool("RX claim", tdma_rx_prepare_core0_claim(&job), true);
        if (phase == 2u) tdma_rx_prepare_core0_service(&job);
        const tdma_rx_capture_t cancelled_capture = job.capture;
        const uint32_t cancelled_pin = job.capture_observer_epoch, queries = phys.query_calls;
        const uint32_t accepted_before_stop = adapter.rx_count;
        const uint32_t stops = phys.stops;
        failed += expect_bool("bounded RX STOP", ops->stop(&adapter), phase != 1u);
        failed += expect_u32("hardware stops before worker ACK", phys.stops, stops + 1u);
        if (phase == 1u) {
            failed += expect_bool("ARM refuses claimed old slot", ops->start(&adapter, &config), false);
            failed += expect_bool("repeat STOP stays pending", ops->stop(&adapter), false);
            failed += expect_bool("cancel cannot overwrite worker capture",
                memcmp(&cancelled_capture, &job.capture, sizeof(cancelled_capture)) == 0, true);
            assert(job.capture_observer_epoch == cancelled_pin);
            tdma_rx_prepare_core0_build_claimed(&job);
            failed += expect_bool("ACK completes STOP", ops->stop(&adapter), true);
        }
        failed += expect_u32("old slot retired", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_IDLE);
        failed += expect_u32("cancelled capture never committed", adapter.rx_count, accepted_before_stop);
        failed += expect_bool("new ARM", ops->start(&adapter, &config), true);
        assert(phys.query_calls == queries);
    }
    for (uint32_t boundary = 0u; boundary < 7u; ++boundary) {
        memcpy(phys.wire.echo_packet, packet, size); phys.wire.rx_pending = true;
        ops->service(&adapter, 300u, &status);
        tdma_rx_prepare_core0_service(&job);
        if (boundary == 0u) ++job.epoch;
        if (boundary == 1u) ++adapter.config.schedule_crc32;
        if (boundary == 2u) ++job.map_generation;
        if (boundary == 4u) ++adapter.config.ring_profile_crc32;
        if (boundary == 5u) ++job.capture_timestamp_flags;
        if (boundary == 6u) ++job.capture_timestamp_resolution_ns;
        const uint32_t queries = phys.query_calls;
        const uint32_t rx = adapter.rx_count, drops = adapter.rx_drop_count;
        ops->service(&adapter, boundary == 3u ? 5000u : 301u, &status);
        failed += expect_u32("foreign or aged RX cannot commit", adapter.rx_count, rx);
        failed += expect_u32("foreign or aged RX drop fact", adapter.rx_drop_count, drops + 1u);
        assert(phys.query_calls == queries);
        adapter.config = config;
    }
    for (uint32_t scenario = 0u; scenario < 6u; ++scenario) {
        tdma_transport_frame_build_t query_build = build;
        if (scenario == 1u) ++query_build.schedule_crc32;
        if (scenario == 2u) ++query_build.ring_profile_crc32;
        if (scenario == 3u) query_build.transport_sequence = 0u;
        if (scenario == 4u) query_build.flags |= TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE;
        assert(tdma_transport_frame_encode(&query_build, phys.wire.echo_packet,
            sizeof(phys.wire.echo_packet), &phys.wire.echo_packet_size, &result));
        if (scenario == 0u) phys.wire.echo_packet[TDMA_TRANSPORT_FRAME_SEQUENCE_OFFSET] ^= 1u;
        if (scenario == 4u) phys.wire.echo_packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE + 10u] ^= 1u;
        phys.wire.rx_pending = true;
        if (scenario == 5u) phys.pin_epoch = 0u;
        ops->service(&adapter, 6000u + scenario * 10u, &status);
        if (scenario == 5u) phys.pin_epoch = 99u;
        tdma_rx_prepare_core0_service(&job);
        assert(job.decoded == (scenario != 0u));
        if (scenario == 4u) assert(!job.origin_mailboxes_valid);
        const uint32_t queries = phys.query_calls;
        ops->service(&adapter, 6001u + scenario * 10u, &status);
        assert(phys.query_calls == queries + (scenario >= 3u ? 1u : 0u));
        if (scenario == 3u) assert(phys.queried_sequence == 0u);
        if (scenario == 5u) assert(phys.queried_epoch == 0u); /* No late pin repair. */
    }
    ops->stop(&adapter);

    /* Replacing the backend retires its optional extension. The legacy path
     * clears the reused slot even if the previous bytes looked valid. */
    tdma_pio_spi_ring_adapter_set_phys(&adapter, loopback_tx, loopback_rx, &phys);
    failed += expect_bool("backend replace clears extension", adapter.phys_rx_ex == NULL, true);
    assert(adapter.phys_rx_event_pin == NULL && adapter.phys_rx_event_query == NULL);
    failed += expect_bool("legacy station starts", ops->start(&adapter, &config), true);
    tdma_pio_spi_ring_adapter_set_phys_rx_ex(&adapter, rx_station_capture);
    tdma_pio_spi_ring_adapter_set_phys_rx_event(&adapter, rx_station_event_pin, rx_station_event_query);
    assert(adapter.phys_rx_event_pin == NULL && adapter.phys_rx_event_query == NULL);
    failed += expect_bool("RUN cannot change extended callback", adapter.phys_rx_ex == NULL, true);
    job.capture = first_capture;
    job.capture_observer_epoch = 999u;
    memcpy(phys.wire.echo_packet, packet, size); phys.wire.rx_pending = true;
    ops->service(&adapter, 1u, &status);
    failed += expect_u32("legacy copy has no provenance", job.capture.flags, 0u);
    assert(job.capture_observer_epoch == 0u);
    ops->stop(&adapter);

    /* Reference bad-frame diagnostics use a frozen TX copy. The newer RTT
     * and a reused evidence-ring entry must not alter the worker's result. */
    config.local_slot_id = 0u;
    tdma_pio_spi_ring_adapter_set_phys_feedback(&adapter, rx_station_feedback);
    failed += expect_bool("reference station starts", ops->start(&adapter, &config), true);
    const uint32_t index = build.transport_sequence % TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
    adapter.reference_tx_evidence[index].valid = true;
    adapter.reference_tx_evidence[index].sequence = build.transport_sequence;
    adapter.reference_tx_evidence[index].packet_size = size;
    memcpy(adapter.reference_tx_evidence[index].packet, packet, size);
    memcpy(phys.wire.echo_packet, packet, size);
    phys.wire.echo_packet[size - 1u] ^= 0x80u; phys.wire.rx_pending = true; phys.latch = 345u;
    ops->service(&adapter, 1u, &status);
    failed += expect_u32("RTT collected before decode", phys.feedback_calls, 1u);
    memset(adapter.reference_tx_evidence[index].packet, 0, size); phys.latch = 789u;
    tdma_rx_prepare_core0_service(&job);
    failed += expect_bool("bad CRC rejected on worker", job.decoded, false);
    failed += expect_u32("captured RTT immutable", job.round_trip_ns, 345u);
    failed += expect_u32("full bad-frame comparison on worker", job.diagnostic.packet_diff_count, 1u);
    ops->service(&adapter, 2u, &status);
    failed += expect_u32("no late RTT", phys.feedback_calls, 1u);
    failed += expect_u32("bad CRC fact committed", adapter.rx_transport_bad_count, 1u);
    failed += expect_u32("bad comparison copied", adapter.last_bad_packet_diff_count, 1u);
    ops->stop(&adapter);
    printf("RX station size: %zu bytes (host ABI)\n", sizeof(job));
    /* The runtime binds the station over the otherwise unused injection
     * pool. Injection cannot overwrite a claimed worker's union member. */
    tdma_pio_spi_ring_adapter_init(&adapter);
    failed += expect_bool("legacy queue accepts before binding",
        tdma_pio_spi_ring_adapter_inject_rx(&adapter, packet, size, 1u), true);
    failed += expect_bool("nonempty queue cannot become station",
        tdma_pio_spi_ring_adapter_enable_async_rx(&adapter), false);
    tdma_pio_spi_ring_adapter_init(&adapter);
    failed += expect_bool("runtime pool binding", tdma_pio_spi_ring_adapter_enable_async_rx(&adapter), true);
    failed += expect_bool("no repeat union initialization", tdma_pio_spi_ring_adapter_enable_async_rx(&adapter), false);
    memcpy(adapter.rx_station.packet, packet, size); adapter.rx_station.packet_size = size;
    tdma_rx_prepare_request(&adapter.rx_station);
    tdma_rx_prepare_core0_claim(&adapter.rx_station);
    failed += expect_bool("injection cannot overwrite worker",
        tdma_pio_spi_ring_adapter_inject_rx(&adapter, packet, size, 2u), false);
    failed += expect_u32("union worker lease retained", tdma_rx_prepare_state(&adapter.rx_station), TDMA_RX_PREPARE_BUILDING);
    failed += expect_bool("union input untouched", memcmp(adapter.rx_station.packet, packet, size) == 0, true);
    tdma_rx_prepare_cancel(&adapter.rx_station);
    tdma_rx_prepare_core0_build_claimed(&adapter.rx_station);
    return failed;
}

static int test_rx_prepare_origin(void)
{
    int failed = 0;
    const tdma_ring_adapter_ops_t *service = tdma_pio_spi_ring_adapter_ops();
    const tdma_pio_spi_ring_origin_ops_t origin_ops = {
        .admit = origin_adapter_admit, .begin = origin_adapter_start,
        .poll = origin_adapter_poll, .healthy = origin_adapter_healthy,
        .ready = origin_adapter_ready, .publish = origin_adapter_publish,
        .observe = origin_adapter_observe, .take_rx_observation = origin_adapter_take_pair,
    };
    tdma_pio_spi_ring_adapter_t adapter;
    tdma_flight_engine_t engine;
    tdma_flight_fifo_t fifo;
    tdma_rx_prepare_t job = {0};
    tdma_ring_adapter_status_t status;
    origin_adapter_phys_t p = {.bootstrap = {.tx_timestamp_ns = 100u,
        .rx_timestamp_ns = 600u, .advance_echo_to_feedback = true}, .adapter = &adapter,
        .admission = TDMA_ORIGIN_ADMISSION_READY};
    tdma_ring_runtime_config_t config = make_valid_config();
    config.node_count = 4u; config.feedback_timeout_ns = 4000u;
    config.flags |= TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE;
    const tdma_process_image_map_t map = make_eight_slot_flight_map();
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    fill_test_process_mailbox(mailbox, 0u, 1u, 0x31u);
    tdma_pio_spi_ring_adapter_init(&adapter);
    tdma_flight_engine_init(&engine); tdma_flight_engine_configure(&engine, &map);
    tdma_flight_fifo_init(&fifo); set_test_sequential_topology(&adapter, 4u);
    tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
    tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
    tdma_pio_spi_ring_adapter_set_phys(&adapter, origin_adapter_tx, origin_adapter_rx, &p);
    tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter, NULL, origin_adapter_stop, NULL, NULL, &p);
    tdma_pio_spi_ring_adapter_set_phys_tx_complete(&adapter, origin_adapter_complete);
    tdma_pio_spi_ring_adapter_set_phys_feedback(&adapter, origin_adapter_feedback);
    tdma_pio_spi_ring_adapter_set_phys_origin(&adapter, &origin_ops);
    tdma_pio_spi_ring_adapter_set_forwarding_mode(&adapter,
        TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE);
    adapter.rx_preparation = &job;
    failed += expect_bool("async bootstrap starts", service->start(&adapter, &config), true);
    tdma_flight_fifo_core0_publish_tx(&fifo, mailbox, sizeof(mailbox), 0x10001u, 1u, 1u);
    for (uint64_t tick = 1u; tick <= 12u; ++tick) {
        tdma_rx_prepare_core0_service(&job);
        failed += expect_bool("async bootstrap progresses", service->service(&adapter, tick, &status), true);
    }
    failed += expect_bool("async bootstrap enters autonomous graph", p.active, true);
    failed += expect_u32("station idle across persona handoff", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_IDLE);
    const uint32_t sequence = adapter.origin.returned.sequence + 5u;
    origin_adapter_enqueue(&p, sequence, 1u);
    const uint32_t accepted = adapter.origin.observed_return_count;
    service->service(&adapter, 50u, &status);
    failed += expect_u32("origin observation admitted", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_REQUESTED);
    failed += expect_u64("autonomous timestamp stays unqualified", job.rx_timestamp_ns, 0u);
    failed += expect_bool("origin owner bound before bank reuse", job.origin_owner_match, true);
    /* Both the hardware observation and publication bank can be newer while
     * the worker runs. The captured frame retains its own authorization. */
    p.observation.sequence += 20u;
    adapter.origin_shadow[0].generation = 99u;
    adapter.origin_shadow[0].owner_generation = 88u;
    memset(adapter.origin_shadow[0].mailbox, 0xCC, sizeof(mailbox));
    tdma_rx_prepare_core0_service(&job);
    failed += expect_bool("origin mailbox CRCs checked on worker", job.origin_mailboxes_valid, true);
    failed += expect_u32("worker cannot accept origin", adapter.origin.observed_return_count, accepted);
    service->service(&adapter, 51u, &status);
    failed += expect_u32("original paired sequence accepted", adapter.origin.returned.sequence, sequence);
    failed += expect_u32("origin accepted exactly once", adapter.origin.observed_return_count, accepted + 1u);
    failed += expect_u32("new bank owner not substituted", adapter.origin.matched_owner_generation, 0u);
    failed += expect_u32("paired RTT not newest boundary", adapter.origin.returned.rtt_remaining, 777u);
    /* A mutable remote body must still pass its owner's CRC. */
    adapter.origin_shadow[0].generation = 1u;
    memcpy(adapter.origin_shadow[0].mailbox, mailbox, sizeof(mailbox));
    origin_adapter_enqueue(&p, sequence + 1u, 1u);
    p.packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE + TDMA_FLIGHT_SHORT_SLOT_SIZE + 10u] ^= 1u;
    service->service(&adapter, 52u, &status);
    tdma_rx_prepare_core0_service(&job);
    failed += expect_bool("remote mutation passes bounded transport", job.decoded, true);
    failed += expect_bool("remote bad mailbox rejected on worker", job.origin_mailboxes_valid, false);
    service->service(&adapter, 53u, &status);
    failed += expect_u32("bad mailbox cannot advance origin", adapter.origin.returned.sequence, sequence);
    failed += expect_u32("bad mailbox rejection fact", adapter.origin.rejected_observation_count, 1u);
    /* Execute the real worker on ordinary and mixed raw-feedback frames.
     * Transport permits mutable remote bodies; each mailbox remains checked
     * for class, ownership, runtime target range and its independent CRC. */
    for (uint32_t kind = 0u; kind < 6u; ++kind) {
        for (uint32_t fault = 0u; fault < 4u; ++fault) {
            tdma_rx_prepare_t candidate = {0};
            memcpy(candidate.packet, p.seed, sizeof(p.seed));
            candidate.packet_size = sizeof(p.seed);
            candidate.node_count = config.node_count;
            candidate.origin_active = true;
            for (uint32_t slot = 1u; slot < config.node_count; ++slot) {
                uint8_t *remote = candidate.packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE;
                if (kind == 2u || (slot == 1u && kind == 1u)) remote[3] = TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS;
                if (slot == 1u && kind == 3u) remote[3] = TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS;
                if (slot == 1u && kind == 4u) remote[3] = 0x7fu;
                if (kind == 5u) remote[3] = TDMA_PROCESS_IMAGE_VDC_BOUNDARY_COMMAND_MESSAGE_CLASS;
                if (slot == 1u && fault == 2u) remote[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] = 2u;
                if (slot == 1u && fault == 3u) remote[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = (uint8_t)(1u << config.node_count);
                const uint16_t crc = tdma_process_image_crc16_ccitt(remote, TDMA_PROCESS_IMAGE_CRC_OFFSET);
                remote[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
                remote[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
                if (slot == 1u && fault == 1u) remote[10] ^= 1u;
            }
            failed += expect_bool("raw feedback RX worker request", tdma_rx_prepare_request(&candidate), true);
            tdma_rx_prepare_core0_service(&candidate);
            failed += expect_bool("raw feedback mutation retains transport validity", candidate.decoded, true);
            failed += expect_bool("raw feedback RX per-mailbox admission", candidate.origin_mailboxes_valid,
                (kind < 3u || kind == 5u) && fault == 0u);
            failed += expect_u32("raw feedback RX worker bounded completion", tdma_rx_prepare_state(&candidate), TDMA_RX_PREPARE_READY);
            tdma_rx_prepare_release(&candidate);
        }
    }
    failed += expect_bool("async origin STOP", service->stop(&adapter), true);
    return failed;
}
