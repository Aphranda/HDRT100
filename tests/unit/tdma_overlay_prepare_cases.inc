/* Included after the adapter fixtures. The fake physical owner models two
 * pools and explicit DMA selection, independently of RX mirror availability. */
typedef struct {
    tdma_overlay_prepare_t job;
    tdma_flight_overlay_plan_t plan[2];
    uint32_t active;
    uint32_t commits;
    uint32_t stops;
    uint32_t grants;
    bool pending;
    bool selected;
    bool blocked;
    uint32_t retirements;
} async_overlay_phys_t;

static void async_overlay_retire(async_overlay_phys_t *phys)
{
    if (phys->pending && phys->selected) {
        phys->active ^= 1u;
        phys->pending = phys->selected = false;
        phys->retirements++;
    }
}

static bool async_overlay_ready(void *context)
{
    async_overlay_phys_t *phys = context;
    async_overlay_retire(phys);
    return !phys->pending && !phys->blocked;
}

static bool async_overlay_grant(void *context, tdma_overlay_prepare_t *job)
{
    async_overlay_phys_t *phys = context;
    phys->grants++;
    if (!async_overlay_ready(phys) || tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_IDLE)
        return false;
    job->buffer_index = phys->active ^ 1u;
    job->plan = &phys->plan[job->buffer_index];
    job->binding = (tdma_flight_overlay_binding_t){
        .pass_control = 0x100u, .token_control = 0x110u,
        .selection_control = 0x200u, .restart_control = 0x300u,
        .tx_fifo_address = 0x50000010u, .live_word_address = 0x20001000u,
        .selected_generation_address = 0x20001004u,
        .loader_trigger_address = 0x50000100u, .next_address_address = 0x20001008u,
    };
    job->config = (tdma_flight_overlay_config_t){
        .packet_size = TDMA_FLIGHT_SHORT_PACKET_SIZE,
        .physical_byte_count = TDMA_FLIGHT_SHORT_PACKET_SIZE + 12u,
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
        job->plan->generation != 0u ||
        job->plan->run_count <= TDMA_FLIGHT_OVERLAY_CONTROL_RUNS ||
        job->plan->run[0].write_address != job->binding.selected_generation_address ||
        job->plan->run[job->plan->run_count - 1u].write_address !=
            job->binding.loader_trigger_address) return false;
    phys->commits++;
    phys->pending = true;
    return true;
}

static bool async_overlay_boundary(void *context)
{
    async_overlay_retire(context);
    return true; /* Observation alone cannot retire a DMA pool. */
}

static bool async_overlay_stop(void *context)
{
    async_overlay_phys_t *phys = context;
    phys->stops++;
    phys->pending = false;
    return tdma_overlay_prepare_cancel(&phys->job);
}

static int test_overlay_layout_activation(void)
{
    int failed = 0;
    tdma_flight_engine_t engine;
    tdma_flight_tx_layout_t layout;
    tdma_process_image_map_t map = make_eight_slot_flight_map();
    tdma_flight_engine_init(&engine);
    failed += expect_bool("unconfigured layout rejected",
        tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
    failed += expect_bool("configure layout", tdma_flight_engine_configure(&engine, &map), true);
    failed += expect_bool("inactive layout rejected",
        tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
    for (uint32_t slot = 0u; slot < TDMA_FLIGHT_SHORT_SLOT_COUNT; ++slot) {
        failed += expect_bool("activate each owner", tdma_flight_engine_activate(&engine, slot), true);
        failed += expect_bool("activated layout available",
            tdma_flight_engine_copy_tx_layout(&engine, &layout), true);
        failed += expect_u32("exact activated owner", layout.local_slot_id, slot);
        failed += expect_u32("exact activated span", layout.output_segment_mask, 1u << slot);
        failed += expect_u32("stable configured generation", layout.map_generation, 1u);
        failed += expect_bool("active map cannot change",
            tdma_flight_engine_configure(&engine, &map), false);
        failed += expect_bool("failed configure preserves authorization",
            tdma_flight_engine_copy_tx_layout(&engine, &layout), true);
        failed += expect_u32("failed configure preserves span", layout.output_segment_mask, 1u << slot);
        const uint32_t sequence = engine.map_sequence;
        engine.map_sequence = sequence | 1u; /* Paused publication must not spin. */
        failed += expect_bool("in-progress map snapshot rejected",
            tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
        engine.map_sequence = sequence;
        tdma_flight_engine_deactivate(&engine);
        failed += expect_bool("deactivated authorization unavailable",
            tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
    }
    /* A valid general map still activates, but an earlier full mailbox
     * authorization must not survive a partial or foreign replacement. */
    map.segment[3].byte_length = TDMA_FLIGHT_SHORT_SLOT_SIZE / 2u;
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    failed += expect_bool("reconfigure partial map", tdma_flight_engine_configure(&engine, &map), true);
    failed += expect_bool("activate general partial map", tdma_flight_engine_activate(&engine, 3u), true);
    failed += expect_bool("old complete span not reused",
        tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
    tdma_flight_engine_deactivate(&engine);
    failed += expect_bool("other full owner still activates", tdma_flight_engine_activate(&engine, 4u), true);
    failed += expect_bool("other full owner still authorized",
        tdma_flight_engine_copy_tx_layout(&engine, &layout), true);
    failed += expect_u32("reconfigured generation", layout.map_generation, 2u);
    failed += expect_u32("new owner span", layout.output_segment_mask, 1u << 4u);
    tdma_flight_engine_deactivate(&engine);
    map = make_eight_slot_flight_map();
    map.segment[3].flags &= ~TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE;
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    tdma_flight_engine_configure(&engine, &map);
    tdma_flight_engine_activate(&engine, 3u);
    failed += expect_bool("missing write permission cannot reuse old span",
        tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
    tdma_flight_engine_deactivate(&engine);
    map = make_eight_slot_flight_map();
    map.segment[4].owner_slot_id = 3u;
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    tdma_flight_engine_configure(&engine, &map);
    tdma_flight_engine_activate(&engine, 3u);
    failed += expect_bool("multiple owned spans never authorize overlay",
        tdma_flight_engine_copy_tx_layout(&engine, &layout), false);
    tdma_flight_engine_deactivate(&engine);
    return failed;
}

static int test_overlay_runtime_sizes(void)
{
    int failed = 0;
    tdma_flight_engine_t engine;
    tdma_flight_engine_init(&engine);
    tdma_flight_tx_layout_t old_layout = {0};
    const uint32_t counts[] = {4u, 5u, 6u, 4u};
    for (uint32_t turn = 0u; turn < sizeof(counts) / sizeof(counts[0]); ++turn) {
        const uint32_t nodes = counts[turn], payload_size = nodes * 32u + 4u;
        tdma_process_image_map_t map = make_eight_slot_flight_map();
        map.segment_count = nodes;
        map.payload_size = payload_size;
        for (uint32_t slot = nodes; slot < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; ++slot)
            memset(&map.segment[slot], 0, sizeof(map.segment[slot]));
        map.map_crc32 = tdma_process_image_map_crc32(&map);
        failed += expect_bool("runtime map configure stopped", tdma_flight_engine_configure(&engine, &map), true);
        failed += expect_bool("runtime map activate", tdma_flight_engine_activate(&engine, nodes - 1u), true);
        async_overlay_phys_t phys = {0};
        tdma_overlay_prepare_t *job = &phys.job;
        failed += expect_bool("runtime immutable layout", tdma_flight_engine_copy_tx_layout(&engine, &job->layout), true);
        failed += expect_u32("runtime exact payload grant", job->layout.payload_size, payload_size);
        failed += expect_bool("runtime grant", async_overlay_grant(&phys, job), true);
        job->config.local_slot_id = nodes - 1u;
        job->config.packet_size = payload_size + TDMA_TRANSPORT_FRAME_HEADER_SIZE;
        job->config.physical_byte_count = job->config.packet_size + 12u;
        uint8_t payload[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE] = {0};
        tdma_transport_frame_build_t frame = {
            .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = 0u, .transport_sequence = 7u,
            .payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
            .flags = TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK | TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE,
            .schedule_crc32 = 0x123u, .ring_profile_crc32 = 0x456u,
            .hop_limit = nodes - 1u, .payload = payload, .payload_size = payload_size};
        size_t packet_size = 0u;
        tdma_transport_result_t result;
        failed += expect_bool("runtime packet encode", tdma_transport_frame_encode(
            &frame, job->packet, sizeof(job->packet), &packet_size, &result), true);
        failed += expect_u32("runtime exact packet", packet_size, job->config.packet_size);
        job->target_sequence = 7u;
        job->egress_hop = 1u;
        fill_test_process_mailbox(job->tx_data, nodes - 1u, turn + 1u, 0x36u);
        job->tx.data_size = sizeof(job->tx_data);
        failed += expect_bool("runtime active reconfigure rejected", tdma_flight_engine_configure(&engine, &map), false);
        ++job->config.packet_size;
        failed += expect_bool("runtime worker wrong length rejected", tdma_overlay_prepare_request(job), false);
        --job->config.packet_size;
        failed += expect_bool("runtime worker publish", tdma_overlay_prepare_request(job), true);
        tdma_overlay_prepare_core0_service(job);
        failed += expect_u32("runtime worker ready", tdma_overlay_prepare_state(job), TDMA_OVERLAY_PREPARE_READY);
        failed += expect_u32("runtime physical plan extent", job->plan->command_word_count,
                             job->config.physical_byte_count * 4u);
        failed += expect_u32("runtime only local mailbox loaded", job->applied.output_bytes, 32u);
        failed += expect_bool("runtime owner accepts current layout",
            tdma_flight_engine_accept_tx(&engine, &job->layout, &job->applied, false), true);
        if (turn != 0u)
            failed += expect_bool("runtime stale layout rejected after rearm",
                tdma_flight_engine_accept_tx(&engine, &old_layout, &job->applied, false), false);
        old_layout = job->layout;
        tdma_overlay_prepare_release(job);
        failed += expect_bool("runtime cancellation publish", tdma_overlay_prepare_request(job), true);
        failed += expect_bool("runtime cancellation worker claims", tdma_overlay_prepare_core0_claim(job), true);
        failed += expect_bool("runtime STOP retains building lease", tdma_overlay_prepare_cancel(job), false);
        failed += expect_bool("runtime cannot reuse cancelled lease", tdma_overlay_prepare_request(job), false);
        tdma_overlay_prepare_core0_build_claimed(job);
        failed += expect_u32("runtime STOP worker acknowledgement", tdma_overlay_prepare_state(job), TDMA_OVERLAY_PREPARE_IDLE);
        tdma_flight_engine_deactivate(&engine);
    }
    return failed;
}

static int test_origin_prepare_worker(void)
{
    int failed = 0;
    for (uint32_t nodes = 4u; nodes <= 6u; ++nodes) {
        for (uint32_t fault = 0u; fault <= 8u; ++fault) {
            tdma_overlay_prepare_t job = {0};
            const uint32_t slot = nodes - 1u, packet_size = TDMA_TRANSPORT_FRAME_HEADER_SIZE + nodes * 32u + 4u;
            job.layout = (tdma_flight_tx_layout_t){.local_slot_id = slot, .map_generation = 3u,
                .output_segment_mask = 1u << slot, .payload_size = nodes * 32u + 4u};
            job.config.packet_size = packet_size;
            job.config.local_slot_id = slot;
            job.tx = (tdma_flight_tx_view_t){.generation = 0x10001u, .sequence = 0x20001u,
                .segment_mask = 1u << slot, .data_size = sizeof(job.tx_data)};
            fill_test_process_mailbox(job.tx_data, slot, 1u, 0x38u);
            if (fault == 1u) job.tx_data[20] ^= 1u;
            if (fault == 2u || fault == 3u) {
                job.tx_data[fault == 2u ? TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET : TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] =
                    (uint8_t)(fault == 2u ? 0u : 1u << nodes);
                const uint16_t crc = tdma_process_image_crc16_ccitt(job.tx_data, TDMA_PROCESS_IMAGE_CRC_OFFSET);
                job.tx_data[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
                job.tx_data[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(crc >> 8u);
            }
            if (fault == 4u) ++job.config.local_slot_id;
            if (fault == 5u) job.tx.generation = 0u;
            if (fault == 6u) job.tx.segment_mask = 0u;
            if (fault == 7u) ++job.layout.payload_size;
            failed += expect_bool("origin worker private request", tdma_overlay_prepare_origin_request(&job), true);
            if (fault == 8u) job.kind = UINT32_MAX;
            tdma_overlay_prepare_core0_service(&job);
            failed += expect_u32("origin worker valid or rejected", tdma_overlay_prepare_state(&job),
                fault == 0u ? TDMA_OVERLAY_PREPARE_READY : TDMA_OVERLAY_PREPARE_FAILED);
            failed += expect_bool("origin admission requires valid worker result",
                tdma_overlay_prepare_origin_current(&job, packet_size, slot), fault == 0u);
            if (fault == 0u) {
                failed += expect_u32("origin worker bounded accounting", job.applied.output_bytes, TDMA_FLIGHT_SHORT_SLOT_SIZE);
                failed += expect_bool("origin foreign physical slot rejected", tdma_overlay_prepare_origin_current(&job, packet_size, 0u), false);
                failed += expect_bool("origin foreign physical size rejected", tdma_overlay_prepare_origin_current(&job, packet_size + 32u, slot), false);
                ++job.epoch;
                failed += expect_bool("origin cancelled epoch cannot commit", tdma_overlay_prepare_origin_current(&job, packet_size, slot), false);
            }
            tdma_overlay_prepare_release(&job);
            failed += expect_bool("origin cancellation before claim request", tdma_overlay_prepare_origin_request(&job), true);
            failed += expect_bool("origin cancellation before claim completes", tdma_overlay_prepare_cancel(&job), true);
            failed += expect_bool("origin cancelled request cannot be claimed", tdma_overlay_prepare_core0_claim(&job), false);
        }
    }
    return failed;
}

static int test_overlay_prepare_cases(void)
{
    int failed = test_overlay_layout_activation() + test_overlay_runtime_sizes() + test_origin_prepare_worker();
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
    tdma_pio_spi_ring_adapter_set_phys_overlay(&adapter, NULL, async_overlay_boundary, async_overlay_ready);
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
    failed += expect_u32("worker leaves generation unpublished", phys.job.plan->generation, 0u);
    failed += expect_u32("worker already binds DMA selection", phys.job.plan->run[0].write_address,
        phys.job.binding.selected_generation_address);
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
    phys.selected = true; /* Only a DMA selection retires the old pool. */
    ops->service(&adapter, 140u, &status);
    failed += expect_u32("readiness retires selected pool before new grant", phys.retirements, 1u);
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
    const uint32_t reuse_before = fifo.tx_reuse_count;
    const uint32_t grant_before = phys.grants;
    const uint32_t acquire_before = fifo.tx_acquire_count;
    const uint32_t release_before = fifo.tx_release_count;
    ops->service(&adapter, 142u, &status);
    failed += expect_bool("boundary alone leaves selection pending", phys.pending, true);
    failed += expect_u32("pending selection cannot account reuse", fifo.tx_reuse_count, reuse_before);
    failed += expect_u32("pending selection cannot grant", phys.grants, grant_before);
    phys.selected = true;
    ops->service(&adapter, 143u, &status);
    failed += expect_u32("reuse readiness retires selected pool", phys.retirements, 2u);
    failed += expect_u32("selected unchanged TX reuses once", fifo.tx_reuse_count, reuse_before + 1u);
    phys.blocked = true;
    ops->service(&adapter, 144u, &status);
    failed += expect_u32("physical gate prevents early reuse", fifo.tx_reuse_count, reuse_before + 1u);
    phys.blocked = false;
    const tdma_flight_overlay_plan_t active_plan = phys.plan[phys.active];
    for (uint32_t i = 0; i < 12u; ++i) ops->service(&adapter, 145u + i, &status);
    failed += expect_u32("unchanged TX skips physical grant", phys.grants, grant_before);
    failed += expect_u32("unchanged TX keeps exact reuse evidence", fifo.tx_reuse_count, reuse_before + 13u);
    failed += expect_u32("unchanged TX does not acquire", fifo.tx_acquire_count, acquire_before);
    failed += expect_u32("unchanged TX does not release", fifo.tx_release_count, release_before);
    failed += expect_u32("unchanged TX leaves worker idle", tdma_overlay_prepare_state(&phys.job), TDMA_OVERLAY_PREPARE_IDLE);
    failed += expect_u32("unchanged TX never recommits DMA", phys.commits, 2u);
    failed += expect_bool("unchanged TX preserves active plan",
        memcmp(&active_plan, &phys.plan[phys.active], sizeof(active_plan)) == 0, true);
    const uint32_t map_sequence = engine.map_sequence;
    engine.map_sequence |= 1u; /* Interrupted map publisher cannot authorize reuse. */
    ops->service(&adapter, 158u, &status);
    failed += expect_u32("in-progress map cannot account reuse", fifo.tx_reuse_count, reuse_before + 13u);
    failed += expect_u32("in-progress map cannot consume TX", fifo.tx_acquire_count, acquire_before);
    engine.map_sequence = map_sequence;
    adapter.phys_overlay_ready = NULL;
    const uint32_t fallback_grants = phys.grants;
    ops->service(&adapter, 159u, &status);
    failed += expect_u32("legacy backend still grants before reuse", phys.grants, fallback_grants + 1u);
    failed += expect_u32("legacy backend keeps reuse evidence", fifo.tx_reuse_count, reuse_before + 14u);
    adapter.phys_overlay_ready = async_overlay_ready;
    tdma_flight_fifo_core0_publish_tx(&fifo, mailbox, sizeof(mailbox), 3u, 11u, 1u << 3u);
    phys.blocked = true;
    ops->service(&adapter, 159u, &status);
    failed += expect_u32("blocked new TX stays queued", fifo.tx_acquire_count, acquire_before);
    phys.blocked = false;
    ops->service(&adapter, 160u, &status);
    failed += expect_u32("new descriptor defeats early reuse", phys.job.tx.generation, 3u);
    failed += expect_u32("new descriptor requests worker", tdma_overlay_prepare_state(&phys.job), TDMA_OVERLAY_PREPARE_REQUESTED);
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
