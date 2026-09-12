typedef struct {
    loopback_phys_t wire;
    uint32_t latch_calls, feedback_calls, stops;
    uint64_t latch;
} rx_station_phys_t;

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

static int test_rx_prepare_cases(void)
{
    int failed = 0;
    const tdma_ring_adapter_ops_t *ops = tdma_pio_spi_ring_adapter_ops();
    tdma_pio_spi_ring_adapter_t adapter;
    tdma_rx_prepare_t job = {0};
    rx_station_phys_t phys = {.wire = {.rx_timestamp_ns = 123u, .suppress_echo = true}, .latch = 456u};
    tdma_ring_adapter_status_t status;
    tdma_ring_runtime_config_t config = make_valid_config();
    config.node_count = 4u; config.local_slot_id = 1u;
    config.feedback_timeout_ns = 4000u;
    tdma_pio_spi_ring_adapter_init(&adapter);
    adapter.rx_preparation = &job;
    set_test_sequential_topology(&adapter, 4u);
    tdma_pio_spi_ring_adapter_set_phys(&adapter, loopback_tx, loopback_rx, &phys);
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
    ops->service(&adapter, 100u, &status);
    failed += expect_u32("RX admission not acceptance", adapter.rx_count, 0u);
    failed += expect_u32("RX admitted", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_REQUESTED);
    failed += expect_u32("latch drained at admission", phys.latch_calls, 1u);
    const uint32_t captures = phys.wire.rx_calls;
    memset(phys.wire.echo_packet, 0xA5, size); phys.latch = 999u;
    for (uint32_t i = 0u; i < 20u; ++i) ops->service(&adapter, 101u + i, &status);
    failed += expect_u32("paused worker never reacquires RX", phys.wire.rx_calls, captures);
    failed += expect_bool("DMA source reusable", memcmp(job.packet, packet, size) == 0, true);
    tdma_rx_prepare_core0_service(&job);
    failed += expect_u32("worker cannot commit adapter", adapter.rx_count, 0u);
    failed += expect_u32("worker ready", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_READY);
    ops->service(&adapter, 150u, &status);
    failed += expect_u32("Core1 accepts RX", adapter.rx_count, 1u);
    failed += expect_u32("no late latch read", phys.latch_calls, 1u);
    failed += expect_u64("original RX time", adapter.feedback_rx_timestamp_ns, 123u);
    failed += expect_u64("original TX time", adapter.local_tx_edge_timestamp_ns, 456u);

    /* STOP at REQUESTED, BUILDING, READY; only a claimed worker delays ACK.
     * No continuation may touch the new ARM's slot after ACK. */
    for (uint32_t phase = 0u; phase < 3u; ++phase) {
        memcpy(phys.wire.echo_packet, packet, size); phys.wire.rx_pending = true;
        ops->service(&adapter, 200u, &status);
        if (phase == 1u) failed += expect_bool("RX claim", tdma_rx_prepare_core0_claim(&job), true);
        if (phase == 2u) tdma_rx_prepare_core0_service(&job);
        const uint32_t stops = phys.stops;
        failed += expect_bool("bounded RX STOP", ops->stop(&adapter), phase != 1u);
        failed += expect_u32("hardware stops before worker ACK", phys.stops, stops + 1u);
        if (phase == 1u) {
            failed += expect_bool("ARM refuses claimed old slot", ops->start(&adapter, &config), false);
            failed += expect_bool("repeat STOP stays pending", ops->stop(&adapter), false);
            tdma_rx_prepare_core0_build_claimed(&job);
            failed += expect_bool("ACK completes STOP", ops->stop(&adapter), true);
        }
        failed += expect_u32("old slot retired", tdma_rx_prepare_state(&job), TDMA_RX_PREPARE_IDLE);
        failed += expect_bool("new ARM", ops->start(&adapter, &config), true);
    }
    for (uint32_t boundary = 0u; boundary < 4u; ++boundary) {
        memcpy(phys.wire.echo_packet, packet, size); phys.wire.rx_pending = true;
        ops->service(&adapter, 300u, &status);
        tdma_rx_prepare_core0_service(&job);
        if (boundary == 0u) ++job.epoch;
        if (boundary == 1u) ++adapter.config.schedule_crc32;
        if (boundary == 2u) ++job.map_generation;
        const uint32_t rx = adapter.rx_count, drops = adapter.rx_drop_count;
        ops->service(&adapter, boundary == 3u ? 5000u : 301u, &status);
        failed += expect_u32("foreign or aged RX cannot commit", adapter.rx_count, rx);
        failed += expect_u32("foreign or aged RX drop fact", adapter.rx_drop_count, drops + 1u);
        adapter.config = config;
    }
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
    failed += expect_bool("async origin STOP", service->stop(&adapter), true);
    return failed;
}
