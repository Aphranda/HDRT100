/* Reuse the existing physical stub and run the complete adapter regression
 * before exercising the opt-in path through the same public RX boundary. */
#define main tdma_existing_adapter_tests
#include "test_tdma_pio_spi_ring_adapter.c"
#undef main
#include <assert.h>
#include <stdlib.h>

static void mailbox_crc(uint8_t *mailbox)
{
    const uint16_t crc = tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET);
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
}

static void alter_return(loopback_phys_t *phys, uint32_t scenario)
{
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result;
    assert(tdma_transport_frame_decode(phys->echo_packet, phys->echo_packet_size, &view, &result));
    uint8_t payload[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];
    memcpy(payload, view.payload, view.payload_size);
    if (scenario == 2u) payload[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] = 1u;
    if (scenario == 3u) payload[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 2u;
    if (scenario == 4u) payload[TDMA_PROCESS_IMAGE_CRC_OFFSET] ^= 1u;
    if (scenario == 5u) payload[8] ^= 1u;
    if (scenario == 13u || scenario == 16u) {
        uint8_t *remote = payload + TDMA_FLIGHT_SHORT_SLOT_SIZE;
        fill_test_process_mailbox(remote, 1u, 10u, 0x64u);
        remote[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 3u;
        mailbox_crc(remote);
    }
    if (scenario == 16u) {
        payload[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 2u;
        mailbox_crc(payload);
    }
    if (scenario == 2u || scenario == 3u || scenario == 5u) mailbox_crc(payload);
    tdma_transport_frame_build_t build = {
        .frame_class = view.frame_class,
        .payload_class = view.payload_class, .origin_slot_id = view.origin_slot_id,
        .hop_limit = view.hop_limit, .flags = view.flags,
        .transport_sequence = view.transport_sequence +
            (scenario == 6u ? 1u : scenario == 15u ? 100000u : 0u),
        .schedule_crc32 = view.schedule_crc32 + (scenario == 8u ? 1u : 0u),
        .ring_profile_crc32 = view.ring_profile_crc32,
        .payload = payload, .payload_size = view.payload_size,
    };
    assert(tdma_transport_frame_encode(&build, phys->echo_packet, sizeof(phys->echo_packet),
        &phys->echo_packet_size, &result));
    if (scenario == 7u) assert(tdma_transport_frame_advance_hop(
        phys->echo_packet, phys->echo_packet_size, &result));
}

static void run_local_return(uint32_t scenario, uint32_t forwarding_mode)
{
    tdma_pio_spi_ring_adapter_t adapter;
    tdma_flight_engine_t engine;
    tdma_flight_fifo_t fifo;
    tdma_ring_runtime_config_t config = make_valid_config();
    tdma_process_image_map_t map = make_eight_slot_flight_map();
    loopback_phys_t phys = {0};
    tdma_ring_adapter_status_t status;
    tdma_flight_rx_view_t received;
    const tdma_ring_adapter_ops_t *ops = tdma_pio_spi_ring_adapter_ops();
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    fill_test_process_mailbox(mailbox, 0u, scenario == 10u ? UINT16_MAX : 10u, 0x73u);
    mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 3u;
    mailbox_crc(mailbox);
    assert(tdma_pio_spi_ring_adapter_init(&adapter));
    assert(tdma_flight_engine_init(&engine));
    assert(tdma_flight_engine_configure(&engine, &map));
    assert(tdma_flight_fifo_init(&fifo));
    set_test_sequential_topology(&adapter, config.node_count);
    phys.tx_timestamp_ns = 100u;
    phys.rx_timestamp_ns = 150u;
    /* A direct RJ45 cable returns the TX bytes without inventing a hop. */
    phys.advance_echo_to_feedback = false;
    phys.suppress_echo = true;
    tdma_pio_spi_ring_adapter_set_phys(&adapter, loopback_tx, loopback_rx, &phys);
    tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
    tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
    assert(tdma_pio_spi_ring_adapter_set_forwarding_mode(&adapter,
        TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT));
    assert(!tdma_pio_spi_ring_adapter_set_local_return_delivery(&adapter, true));
    assert(tdma_pio_spi_ring_adapter_set_local_return_delivery(&adapter, false));
    assert(tdma_pio_spi_ring_adapter_set_forwarding_mode(&adapter, forwarding_mode));
    if (scenario != 0u) assert(tdma_pio_spi_ring_adapter_set_local_return_delivery(&adapter, true));
    if (scenario != 0u) {
        assert(!tdma_pio_spi_ring_adapter_set_forwarding_mode(
            &adapter, TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT));
        assert(adapter.forwarding_mode == TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE);
        assert(tdma_pio_spi_ring_adapter_set_forwarding_mode(
            &adapter, TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE));
    }
    assert(ops->start(&adapter, &config));
    uint32_t facts[6];
    assert(tdma_pio_spi_ring_adapter_get_local_return_status(&adapter, facts));
    assert(facts[0] == (scenario != 0u) && facts[1] == 0u && facts[2] == 0u &&
        facts[3] == 0u && facts[4] == 0u && facts[5] == 0u);
    assert(!tdma_pio_spi_ring_adapter_set_local_return_delivery(&adapter, false));
    assert(tdma_flight_fifo_core0_publish_tx(&fifo, mailbox, sizeof(mailbox), 10u, 10u, 1u));
    assert(ops->service(&adapter, 1u, &status));
    assert(ops->service(&adapter, 2u, &status));
    assert(phys.tx_calls == 1u);
    assert(!tdma_flight_fifo_core0_acquire_rx(&fifo, &received)); /* TX is not RX. */
    if ((scenario >= 2u && scenario <= 8u) || scenario == 13u) alter_return(&phys, scenario);
    if (scenario == 13u) {
        /* A valid remote segment in a health-rejected frame must not become
         * deliverable merely because its local segment has return proof. */
        adapter.receive_health.image_generation = 1u;
        adapter.receive_health.accepted_sequence = adapter.up_sequence;
    }
    if (scenario == 14u) {
        /* More than a complete six-fragment message traverses the real RX
         * callback. Each new TX stays invisible until physically returned. */
        for (uint32_t frame = 0u; frame < 12u; ++frame) {
            if (frame != 0u) {
                mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET] = (uint8_t)(10u + frame);
                mailbox[14] = (uint8_t)frame;
                mailbox_crc(mailbox);
                assert(tdma_flight_fifo_core0_publish_tx(&fifo, mailbox,
                    sizeof(mailbox), 10u + frame, 10u + frame, 1u));
                assert(ops->service(&adapter, 2u + frame * 2000u, &status));
            }
            assert(!tdma_flight_fifo_core0_acquire_rx(&fifo, &received));
            phys.rx_pending = true;
            (void)ops->service(&adapter, 3u + frame * 2000u, &status);
            assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &received));
            assert(received.segment_mask == 1u);
            assert(memcmp(received.data, mailbox, sizeof(mailbox)) == 0);
            assert(tdma_flight_fifo_core0_release_rx(&fifo, received.slot_index));
            assert(engine.rx_seen_segment_mask == 0u);
            assert(!adapter.last_rx_gate_accepted);
            assert(adapter.last_rx_hop_count == 0u);
            assert(adapter.origin.active == 0u && !adapter.resident_seeded);
        }
        assert(ops->stop(&adapter));
        return;
    }
    if (scenario == 11u || scenario == 12u) {
        /* The real wire can lag several emitted frames. A retained exact TX
         * is sufficient; an overwritten evidence slot must fail closed. */
        uint8_t delayed[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        const size_t delayed_size = phys.echo_packet_size;
        memcpy(delayed, phys.echo_packet, delayed_size);
        const uint32_t lag = TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH -
            (scenario == 11u ? 1u : 0u);
        for (uint32_t frame = 1u; frame <= lag; ++frame)
            assert(ops->service(&adapter, 2u + frame * 2000u, &status));
        assert(phys.tx_calls == 1u + lag);
        assert(tdma_pio_spi_ring_adapter_inject_rx(&adapter, delayed, delayed_size, 150u));
        (void)ops->service(&adapter, 3u + lag * 2000u, &status);
        assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &received) == (scenario == 11u));
        if (scenario == 11u) {
            assert(received.segment_mask == 1u);
            assert(memcmp(received.data, mailbox, sizeof(mailbox)) == 0);
            assert(tdma_flight_fifo_core0_release_rx(&fifo, received.slot_index));
        }
        assert(engine.rx_seen_segment_mask == 0u);
        assert(!adapter.last_rx_gate_accepted);
        assert(tdma_pio_spi_ring_adapter_get_local_return_status(&adapter, facts));
        assert(facts[3] == (scenario == 11u ? 0u : 5u));
        assert(facts[4] == (scenario == 11u ? 1u : 0u) && facts[5] == facts[4]);
        assert(ops->stop(&adapter));
        return;
    }
    phys.rx_pending = true;
    uint32_t filled = 0u;
    if (scenario == 9u) {
        while (tdma_flight_fifo_core1_publish_rx(&fifo, mailbox, sizeof(mailbox),
                1u, 1u, 2u, 0u, 0u)) ++filled;
        assert(filled != 0u);
    }
    (void)ops->service(&adapter, 3u, &status);
    assert(tdma_pio_spi_ring_adapter_get_local_return_status(&adapter, facts));
    if (scenario == 0u) assert(facts[3] == 1u);
    if (scenario >= 2u && scenario <= 4u) assert(facts[3] == 4u);
    if (scenario == 5u) assert(facts[3] == 6u);
    if (scenario == 6u) assert(facts[3] == 5u);
    if (scenario == 9u) {
        assert(!adapter.local_return_seen);
        assert(facts[3] == 0u && facts[4] == 1u && facts[5] == 0u);
        while (tdma_flight_fifo_core0_acquire_rx(&fifo, &received))
            assert(tdma_flight_fifo_core0_release_rx(&fifo, received.slot_index));
        assert(tdma_pio_spi_ring_adapter_inject_rx(&adapter, phys.echo_packet, phys.echo_packet_size, 160u));
        (void)ops->service(&adapter, 4u, &status);
    }
    const bool expect_delivery = scenario == 1u || scenario == 7u || scenario == 9u ||
        scenario == 10u || scenario == 13u;
    const bool delivered = tdma_flight_fifo_core0_acquire_rx(&fifo, &received);
    if (delivered != expect_delivery) {
        fprintf(stderr, "local return scenario=%u mode=%u delivered=%u expected=%u error=%u rx=%u\n",
            scenario, forwarding_mode, delivered, expect_delivery, adapter.last_error, adapter.rx_count);
        exit(1);
    }
    if (expect_delivery) {
        assert(received.segment_mask == 1u);
        assert(memcmp(received.data, mailbox, sizeof(mailbox)) == 0);
        assert(tdma_flight_fifo_core0_release_rx(&fifo, received.slot_index));
        /* Delivery did not invent any remote mailbox/WKC evidence. */
        assert(engine.rx_seen_segment_mask == 0u);
        assert(!adapter.last_rx_gate_accepted);
        assert(adapter.local_return_seen);
        assert(tdma_pio_spi_ring_adapter_inject_rx(&adapter, phys.echo_packet, phys.echo_packet_size, 170u));
        (void)ops->service(&adapter, 5u, &status);
        assert(!tdma_flight_fifo_core0_acquire_rx(&fifo, &received));
        assert(tdma_pio_spi_ring_adapter_get_local_return_status(&adapter, facts));
        assert(facts[3] == 9u && facts[4] == (scenario == 9u ? 2u : 1u) && facts[5] == 1u);
        /* A newly emitted cycle carrying a stale mailbox must also fail.
         * Conversely UINT16_MAX -> 0 is a valid wrap, once per mailbox. */
        mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET] = scenario == 10u ? 0u : 9u;
        mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET + 1u] = 0u;
        mailbox_crc(mailbox);
        assert(tdma_flight_fifo_core0_publish_tx(&fifo, mailbox, sizeof(mailbox), 11u, 11u, 1u));
        assert(ops->service(&adapter, 2002u, &status));
        assert(phys.tx_calls == 2u);
        phys.rx_pending = true;
        (void)ops->service(&adapter, 2003u, &status);
        assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &received) == (scenario == 10u));
        if (scenario == 10u) {
            assert(received.segment_mask == 1u);
            assert(memcmp(received.data, mailbox, sizeof(mailbox)) == 0);
            assert(tdma_flight_fifo_core0_release_rx(&fifo, received.slot_index));
        }
    } else assert(!adapter.local_return_seen);
    assert(ops->stop(&adapter));
    if (expect_delivery) {
        const uint16_t retained = adapter.local_return_seq16;
        assert(tdma_pio_spi_ring_adapter_set_local_return_delivery(&adapter, false));
        assert(adapter.local_return_seen && adapter.local_return_seq16 == retained);
        assert(tdma_pio_spi_ring_adapter_set_local_return_delivery(&adapter, true));
        assert(adapter.local_return_seen && adapter.local_return_seq16 == retained);
        assert(ops->start(&adapter, &config));
        assert(adapter.local_return_delivery == 1u && !adapter.local_return_seen);
        assert(tdma_pio_spi_ring_adapter_get_local_return_status(&adapter, facts));
        assert(facts[3] == 0u && facts[4] == 0u && facts[5] == 0u);
        assert(ops->stop(&adapter));
    }
    assert(tdma_pio_spi_ring_adapter_set_local_return_delivery(&adapter, false));
    assert(tdma_pio_spi_ring_adapter_set_forwarding_mode(
        &adapter, TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT));
}

static void run_async_local_return(uint32_t scenario)
{
    tdma_pio_spi_ring_adapter_t adapter;
    tdma_flight_engine_t engine;
    tdma_flight_fifo_t fifo;
    tdma_ring_runtime_config_t config = make_valid_config();
    tdma_process_image_map_t map = make_eight_slot_flight_map();
    if (scenario >= 9u) {
        /* Production builds map exactly the admitted topology, independently
         * of the compile-time storage capacity (two 32-byte slots + trailer). */
        map.payload_size = tdma_flight_payload_size(config.node_count);
        map.segment_count = config.node_count;
        for (uint32_t slot = config.node_count; slot < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; ++slot)
            memset(&map.segment[slot], 0, sizeof(map.segment[slot]));
        map.map_crc32 = tdma_process_image_map_crc32(&map);
    }
    loopback_phys_t phys = {.tx_timestamp_ns = 100u, .rx_timestamp_ns = 150u,
        .suppress_echo = true};
    tdma_ring_adapter_status_t status;
    tdma_flight_rx_view_t received;
    const tdma_ring_adapter_ops_t *ops = tdma_pio_spi_ring_adapter_ops();
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    if (scenario == 8u) phys.tx_timestamp_ns = 0u; /* No clock latch required. */
    fill_test_process_mailbox(mailbox, 0u, 10u, 0x73u);
    mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = 3u;
    mailbox_crc(mailbox);
    assert(tdma_pio_spi_ring_adapter_init(&adapter));
    assert(tdma_pio_spi_ring_adapter_enable_async_rx(&adapter));
    assert(tdma_flight_engine_init(&engine));
    assert(tdma_flight_engine_configure(&engine, &map));
    assert(tdma_flight_fifo_init(&fifo));
    set_test_sequential_topology(&adapter, config.node_count);
    if (scenario >= 12u) memset(&adapter.topology, 0, sizeof(adapter.topology));
    tdma_pio_spi_ring_adapter_set_phys(&adapter, loopback_tx, loopback_rx, &phys);
    tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
    tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
    assert(tdma_pio_spi_ring_adapter_set_forwarding_mode(&adapter,
        TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE));
    assert(tdma_pio_spi_ring_adapter_set_local_return_delivery(&adapter, scenario != 11u));
    if (scenario >= 10u && scenario != 13u)
        assert(tdma_pio_spi_ring_adapter_set_topology_probe_mode(&adapter, true));
    if (scenario == 13u) {
        /* Product RUN still requires the discovered topology. */
        assert(!ops->start(&adapter, &config));
        assert(adapter.last_error == TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
        assert(!tdma_flight_engine_is_active(&engine));
        return;
    }
    assert(ops->start(&adapter, &config));
    if (scenario == 11u) {
        /* Ordinary topology diagnostics still disable the process engine. */
        assert(!tdma_flight_engine_is_active(&engine));
        assert(ops->stop(&adapter));
        return;
    }
    uint8_t tx_image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];
    tdma_flight_engine_fill_alignment_symbols(tx_image, sizeof(tx_image));
    memcpy(tx_image, mailbox, sizeof(mailbox));
    assert(tdma_flight_fifo_core0_publish_tx(&fifo, tx_image,
        scenario >= 9u ? map.payload_size : sizeof(mailbox), 10u, 10u, 1u));
    assert(ops->service(&adapter, 1u, &status));
    assert(ops->service(&adapter, 2u, &status));
    assert(phys.tx_calls == 1u);
    if (scenario == 2u) alter_return(&phys, 5u); /* Valid CRC, changed local bytes. */
    if (scenario == 3u) alter_return(&phys, 4u); /* Bad mailbox CRC. */
    if (scenario == 4u) alter_return(&phys, 15u); /* Never-transmitted sequence. */
    if (scenario == 5u) alter_return(&phys, 8u); /* Wrong schedule identity. */
    if (scenario == 14u) alter_return(&phys, 13u); /* Valid foreign mailbox. */
    if (scenario == 15u) alter_return(&phys, 16u); /* Remote only; no local proof. */
    uint8_t returned[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    const size_t size = phys.echo_packet_size;
    if (scenario >= 9u) assert(size == TDMA_TRANSPORT_FRAME_HEADER_SIZE +
        tdma_flight_payload_size(config.node_count));
    memcpy(returned, phys.echo_packet, size);
    /* Capture near the oldest retained TX, but still within evidence life.
     * Scenario 1 arrives after expiry and must never manufacture proof. */
    const uint32_t lag = TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH -
        (scenario == 1u ? 0u : 1u);
    for (uint32_t i = 1u; i <= lag; ++i)
        assert(ops->service(&adapter, 2u + i * 2000u, &status));
    uint64_t now = 3u + lag * 2000u;
    memcpy(phys.echo_packet, returned, size);
    phys.echo_packet_size = size;
    phys.rx_pending = true;
    (void)ops->service(&adapter, now, &status);
    tdma_rx_prepare_t *job = adapter.rx_preparation;
    assert(tdma_rx_prepare_state(job) == TDMA_RX_PREPARE_REQUESTED);
    if (scenario == 0u || scenario == 8u || (scenario >= 9u && scenario < 14u)) {
        assert(job->expected_size == size);
        assert(memcmp(job->expected, returned, size) == 0);
    }
    if (scenario == 1u || scenario == 4u) assert(job->expected_size == 0u);
    assert(!tdma_flight_fifo_core0_acquire_rx(&fifo, &received));
    assert(tdma_rx_prepare_core0_claim(job));
    /* One real TX reuses this slot while the worker holds its private copy.
     * Acceptance remains inside the existing RX job freshness deadline. */
    assert(ops->service(&adapter, now + 1999u, &status));
    assert(adapter.reference_tx_evidence[1u].sequence != 1u);
    assert(memcmp(job->packet, returned, size) == 0);
    tdma_rx_prepare_core0_build_claimed(job);
    assert(tdma_rx_prepare_state(job) == TDMA_RX_PREPARE_READY);
    if (scenario == 6u) ++job->epoch;
    now += 2000u;
    if (scenario == 7u) now += adapter.receive_health.config.stale_timeout_ns;
    (void)ops->service(&adapter, now, &status);
    const bool expected = scenario == 0u || scenario == 8u || (scenario >= 9u && scenario < 15u);
    const bool delivered = tdma_flight_fifo_core0_acquire_rx(&fifo, &received);
    if (delivered != expected || (expected && adapter.local_return_last_reject != 0u)) {
        fprintf(stderr, "async local return scenario=%u delivered=%u expected=%u reject=%u\n",
            scenario, delivered, expected, adapter.local_return_last_reject);
        exit(1);
    }
    if (delivered) {
        assert(received.segment_mask == 1u);
        assert(memcmp(received.data, mailbox, sizeof(mailbox)) == 0);
        assert(tdma_flight_fifo_core0_release_rx(&fifo, received.slot_index));
        assert(adapter.local_return_published == 1u);
        /* A newer, genuinely transmitted frame carrying the same mailbox
         * must not redeliver the message through the async path. */
        phys.rx_pending = true;
        (void)ops->service(&adapter, now + 1u, &status);
        assert(tdma_rx_prepare_state(job) == TDMA_RX_PREPARE_REQUESTED);
        tdma_rx_prepare_core0_service(job);
        (void)ops->service(&adapter, now + 2u, &status);
        assert(!tdma_flight_fifo_core0_acquire_rx(&fifo, &received));
        assert(adapter.local_return_last_reject == 9u);
        assert(adapter.local_return_published == 1u);
    }
    assert(engine.rx_seen_segment_mask == 0u);
    if (scenario >= 10u) {
        assert(adapter.receive_health.configured == 0u);
        assert(adapter.receive_health.accepted_wkc == 0u);
        assert(adapter.receive_health.accepted_count == 0u);
    }
    assert(!adapter.last_rx_gate_accepted);
    assert(adapter.last_rx_hop_count == 0u);
    assert(adapter.origin.active == 0u && !adapter.resident_seeded);
    assert(ops->stop(&adapter));
}

int main(void)
{
    tdma_pio_spi_ring_adapter_t snapshot_adapter;
    uint32_t facts[6];
    assert(tdma_pio_spi_ring_adapter_init(&snapshot_adapter));
    assert(!tdma_pio_spi_ring_adapter_get_local_return_status(NULL, facts));
    assert(!tdma_pio_spi_ring_adapter_get_local_return_status(&snapshot_adapter, NULL));
    snapshot_adapter.snapshot_guard = 1u;
    assert(!tdma_pio_spi_ring_adapter_get_local_return_status(&snapshot_adapter, facts));
    for (uint32_t i = 0u; i < 6u; ++i) assert(facts[i] == 0u);
#if PROJECT_NODE_CAPACITY == 8
    assert(tdma_existing_adapter_tests() == 0);
#endif
    for (uint32_t scenario = 0u; scenario <= 14u; ++scenario) {
        run_local_return(scenario, TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE);
    }
    for (uint32_t scenario = 0u; scenario <= 15u; ++scenario) run_async_local_return(scenario);
    puts("TDMA physical local-return delivery tests passed");
    return 0;
}
