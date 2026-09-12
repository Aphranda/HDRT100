/* Included after the adapter fixtures. The fake physical owner models two
 * pools and explicit DMA selection, independently of RX mirror availability. */
typedef struct {
    tdma_overlay_prepare_t job;
    tdma_flight_overlay_plan_t plan[2];
    uint32_t active;
    uint32_t commits;
    uint32_t stops;
    bool pending;
} async_overlay_phys_t;

static bool async_overlay_grant(void *context, tdma_overlay_prepare_t *job)
{
    async_overlay_phys_t *phys = context;
    if (phys->pending || tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_IDLE)
        return false;
    job->buffer_index = phys->active ^ 1u;
    job->plan = &phys->plan[job->buffer_index];
    job->config = (tdma_flight_overlay_config_t){
        .physical_byte_count = TDMA_TRANSPORT_SHORT_PACKET_MAX + 12u,
        .outer_header_size = 4u, .local_slot_id = 3u,
        .header_write_mask = tdma_transport_frame_resident_overlay_header_mask(),
        .final_bit_pc = 16u,
    };
    return true;
}

static bool async_overlay_commit(void *context, tdma_overlay_prepare_t *job)
{
    async_overlay_phys_t *phys = context;
    if (phys->pending || job->request_epoch != job->epoch ||
        tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_READY ||
        job->buffer_index != (phys->active ^ 1u) ||
        !tdma_flight_overlay_plan_valid(job->plan, 16u)) return false;
    phys->commits++;
    phys->pending = true;
    return true;
}

static bool async_overlay_boundary(void *context)
{
    (void)context;
    return true; /* Observation alone cannot retire a DMA pool. */
}

static bool async_overlay_stop(void *context)
{
    async_overlay_phys_t *phys = context;
    phys->stops++;
    phys->pending = false;
    return tdma_overlay_prepare_cancel(&phys->job);
}

static int test_overlay_prepare_cases(void)
{
    int failed = 0;
    tdma_pio_spi_ring_adapter_t adapter;
    tdma_flight_fifo_t fifo;
    tdma_flight_engine_t engine;
    async_overlay_phys_t phys = {0};
    loopback_phys_t wire = {.suppress_echo = true};
    tdma_ring_adapter_status_t status;
    tdma_ring_runtime_config_t config = make_valid_config();
    config.node_count = 4u;
    config.local_slot_id = 3u;
    config.reference_slot_id = 0u;
    tdma_process_image_map_t map = make_eight_slot_flight_map();
    uint8_t payload[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE] = {0};
    for (uint32_t slot = 0u; slot < config.node_count; ++slot)
        fill_test_process_mailbox(payload + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE,
                                  slot, 1u, (uint8_t)slot);
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    fill_test_process_mailbox(mailbox, config.local_slot_id, 9u, 0xA5u);
    const tdma_transport_frame_build_t build = {
        .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT, .origin_slot_id = 0u,
        .transport_sequence = 41u,
        .payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
        .flags = TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK | TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE,
        .schedule_crc32 = config.schedule_crc32, .ring_profile_crc32 = config.ring_profile_crc32,
        .hop_limit = config.node_count - 1u, .payload = payload, .payload_size = sizeof(payload),
    };
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t packet_size;
    tdma_transport_result_t result;
    failed += expect_bool("async encode", tdma_transport_frame_encode(&build,
        packet, sizeof(packet), &packet_size, &result), true);
    tdma_pio_spi_ring_adapter_init(&adapter);
    set_test_sequential_topology(&adapter, config.node_count);
    tdma_flight_fifo_init(&fifo);
    tdma_flight_engine_init(&engine);
    tdma_flight_engine_configure(&engine, &map);
    tdma_pio_spi_ring_adapter_set_phys(&adapter, loopback_tx, loopback_rx, &wire);
    tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter, NULL, async_overlay_stop, NULL, NULL, &phys);
    tdma_pio_spi_ring_adapter_set_phys_overlay(&adapter, NULL, async_overlay_boundary, NULL);
    tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
    tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
    adapter.overlay_preparation = &phys.job;
    adapter.phys_grant_overlay = async_overlay_grant;
    adapter.phys_commit_overlay = async_overlay_commit;
    tdma_pio_spi_ring_adapter_set_forwarding_mode(&adapter,
        TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE);
    const tdma_ring_adapter_ops_t *ops = tdma_pio_spi_ring_adapter_ops();
    failed += expect_bool("async start", ops->start(&adapter, &config), true);
    failed += expect_bool("async publish", tdma_flight_fifo_core0_publish_tx(&fifo,
        mailbox, sizeof(mailbox), 1u, 9u, 1u << config.local_slot_id), true);
    tdma_pio_spi_ring_adapter_inject_rx(&adapter, packet, packet_size, 100u);
    failed += expect_bool("async request service", ops->service(&adapter, 100u, &status), true);
    failed += expect_u32("Core1 only requests", tdma_overlay_prepare_state(&phys.job),
        TDMA_OVERLAY_PREPARE_REQUESTED);
    failed += expect_u32("no premature commit", phys.commits, 0u);
    const uint32_t acquired = fifo.tx_acquire_count;
    for (uint32_t i = 0u; i < 12u; ++i) ops->service(&adapter, 101u + i, &status);
    failed += expect_u32("paused Core0 never reacquires source", fifo.tx_acquire_count, acquired);
    failed += expect_u32("paused Core0 never commits", phys.commits, 0u);

    /* Retire/reuse the source slot before Core0 even starts. Its frozen input
     * must survive producer reuse, as must the grant's map authorization. */
    tdma_flight_fifo_core1_release_tx(&fifo);
    uint8_t newer[sizeof(mailbox)];
    fill_test_process_mailbox(newer, config.local_slot_id, 10u, 0x5Au);
    uint8_t newer_image[sizeof(payload)];
    memcpy(newer_image, payload, sizeof(newer_image));
    memcpy(newer_image + config.local_slot_id * sizeof(newer), newer, sizeof(newer));
    failed += expect_bool("reuse input slot", tdma_flight_fifo_core0_publish_tx(&fifo,
        newer_image, sizeof(newer_image), 2u, 10u, 1u << config.local_slot_id), true);
    failed += expect_bool("claim worker", tdma_overlay_prepare_core0_claim(&phys.job), true);
    failed += expect_bool("only one claim", tdma_overlay_prepare_core0_claim(&phys.job), false);
    failed += expect_bool("frozen source", memcmp(phys.job.tx_data, mailbox, sizeof(mailbox)) == 0, true);
    tdma_overlay_prepare_core0_build_claimed(&phys.job);
    failed += expect_u32("worker ready", tdma_overlay_prepare_state(&phys.job), TDMA_OVERLAY_PREPARE_READY);
    failed += expect_u32("worker cannot publish DMA", phys.commits, 0u);
    failed += expect_u32("worker cannot commit engine facts", engine.map_apply_count, 0u);
    failed += expect_u32("owner-only output", phys.job.applied.output_bytes, sizeof(mailbox));
    /* TX acceptance progresses despite a corrupt observation on this phase. */
    packet[0] ^= 1u;
    tdma_pio_spi_ring_adapter_inject_rx(&adapter, packet, packet_size, 120u);
    ops->service(&adapter, 120u, &status);
    failed += expect_u32("bad RX does not block ready TX", phys.commits, 1u);
    failed += expect_u32("accepted generation", adapter.resident_overlay_tx_generation, 1u);
    failed += expect_u32("Core1 commits engine facts", engine.map_apply_count, 1u);
    failed += expect_u32("bad observation retained", adapter.rx_bad_count, 1u);
    const tdma_flight_overlay_plan_t committed = phys.plan[phys.active ^ 1u];
    for (uint32_t i = 0u; i < 12u; ++i) ops->service(&adapter, 121u + i, &status);
    failed += expect_u32("pending DMA blocks new job", tdma_overlay_prepare_state(&phys.job),
        TDMA_OVERLAY_PREPARE_IDLE);
    failed += expect_bool("pending pool immutable", memcmp(&committed,
        &phys.plan[phys.active ^ 1u], sizeof(committed)) == 0, true);
    phys.active ^= 1u; /* Only a simulated DMA selection retires the old pool. */
    phys.pending = false;
    ops->service(&adapter, 140u, &status);
    failed += expect_u32("new TX request without new RX", tdma_overlay_prepare_state(&phys.job),
        TDMA_OVERLAY_PREPARE_REQUESTED);
    failed += expect_u32("next generation independently acquired", phys.job.tx.generation, 2u);
    failed += expect_bool("full image normalized to local mailbox",
        phys.job.tx.data_size == sizeof(newer) && memcmp(phys.job.tx_data, newer, sizeof(newer)) == 0, true);
    tdma_overlay_prepare_core0_service(&phys.job);
    ops->service(&adapter, 141u, &status);
    failed += expect_u32("new TX committed without RX", phys.commits, 2u);
    failed += expect_bool("old DMA pool survived background build", memcmp(&committed,
        &phys.plan[phys.active], sizeof(committed)) == 0, true);

    /* A requested but unclaimed job can be cancelled immediately. */
    phys.active ^= 1u;
    phys.pending = false;
    tdma_flight_fifo_core0_publish_tx(&fifo, mailbox, sizeof(mailbox), 3u, 11u, 1u << 3u);
    ops->service(&adapter, 160u, &status);
    failed += expect_bool("unclaimed STOP", ops->stop(&adapter), true);
    failed += expect_bool("cancelled request cannot be claimed", tdma_overlay_prepare_core0_claim(&phys.job), false);
    failed += expect_bool("restart after retired request", ops->start(&adapter, &config), true);
    packet[0] ^= 1u;
    tdma_pio_spi_ring_adapter_inject_rx(&adapter, packet, packet_size, 200u);
    ops->service(&adapter, 200u, &status);
    failed += expect_bool("claim before STOP", tdma_overlay_prepare_core0_claim(&phys.job), true);
    const uint32_t old_epoch = phys.job.request_epoch;
    failed += expect_bool("claimed STOP waits for ACK", ops->stop(&adapter), false);
    failed += expect_bool("hardware stop ran before ACK", phys.stops >= 2u, true);
    failed += expect_u32("cancelled lease retained", tdma_overlay_prepare_state(&phys.job),
        TDMA_OVERLAY_PREPARE_CANCELLED);
    failed += expect_bool("cancelled lease cannot reissue", tdma_overlay_prepare_request(&phys.job), false);
    failed += expect_bool("ARM rejected before cancelled writer ACK", ops->start(&adapter, &config), false);
    failed += expect_bool("epoch invalidated", old_epoch != phys.job.epoch, true);
    tdma_overlay_prepare_core0_build_claimed(&phys.job);
    failed += expect_u32("late worker only ACKs", tdma_overlay_prepare_state(&phys.job), TDMA_OVERLAY_PREPARE_IDLE);
    failed += expect_bool("late result cannot commit", async_overlay_commit(&phys, &phys.job), false);
    failed += expect_bool("STOP completes after ACK", ops->stop(&adapter), true);
    failed += expect_u32("no cancelled publication", phys.commits, 2u);

    /* A READY result cancelled before consumption cannot enter the next ARM. */
    failed += expect_bool("restart after late worker", ops->start(&adapter, &config), true);
    tdma_pio_spi_ring_adapter_inject_rx(&adapter, packet, packet_size, 300u);
    ops->service(&adapter, 300u, &status);
    tdma_overlay_prepare_core0_service(&phys.job);
    failed += expect_u32("ready before stop", tdma_overlay_prepare_state(&phys.job), TDMA_OVERLAY_PREPARE_READY);
    failed += expect_bool("ready STOP", ops->stop(&adapter), true);
    failed += expect_bool("ready old epoch rejected", async_overlay_commit(&phys, &phys.job), false);
    failed += expect_u32("ready cancellation leaves no writer", tdma_overlay_prepare_state(&phys.job),
        TDMA_OVERLAY_PREPARE_IDLE);
    /* Compact authorization must reject maps that would grant another
     * mailbox or a partial span. It is not a copy of the whole mutable map. */
    map.segment[3].byte_length = TDMA_FLIGHT_SHORT_SLOT_SIZE / 2u;
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    failed += expect_bool("configure smaller valid map", tdma_flight_engine_configure(&engine, &map), true);
    failed += expect_bool("activate smaller valid map", tdma_flight_engine_activate(&engine, 3u), true);
    tdma_flight_tx_layout_t layout;
    failed += expect_bool("partial mailbox cannot authorize overlay", tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
    tdma_flight_engine_deactivate(&engine);
    map = make_eight_slot_flight_map();
    map.segment[4].owner_slot_id = 3u;
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    tdma_flight_engine_configure(&engine, &map);
    tdma_flight_engine_activate(&engine, 3u);
    failed += expect_bool("extra local span cannot authorize overlay", tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
    tdma_flight_engine_deactivate(&engine);
    return failed;
}
