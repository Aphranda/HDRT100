/* Included after the adapter fixture helpers. Exercise the real adapter,
 * engine, FIFO, receive gates and FSM across a fake asynchronous boundary. */
typedef struct {
    loopback_phys_t bootstrap;
    bool active, fault, pending, fail_start, fail_stop, rx_pending, paired;
    bool omit_pair, reject_publish, fail_poll;
    bool selection_seen;
    tdma_overlay_prepare_t *job;
    uint32_t starts, publications, completion_calls, feedback_calls, rx_calls;
    uint32_t generation;
    uint32_t prepare_polls;
    uint32_t ready_calls, grants, commit_attempts;
    tdma_pio_spi_ring_adapter_t *adapter;
    uint32_t admission_calls;
    tdma_origin_admission_result_t admission;
    bool begin_guarded, reader_deferred;
    uint8_t seed[TDMA_FLIGHT_SHORT_PACKET_SIZE];
    uint8_t packet[TDMA_FLIGHT_SHORT_PACKET_SIZE];
    uint8_t shadow[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    tdma_origin_observation_t observation;
} origin_adapter_phys_t;

static bool origin_adapter_tx(void *context, const uint8_t *packet, size_t size, uint64_t *timestamp)
{
    origin_adapter_phys_t *p = context;
    if (!loopback_tx(&p->bootstrap, packet, size, timestamp)) return false;
    for (uint32_t slot = 1u; slot < 4u; ++slot)
        fill_test_process_mailbox(p->bootstrap.echo_packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE +
            slot * TDMA_FLIGHT_SHORT_SLOT_SIZE, slot, 1u, (uint8_t)slot);
    return true;
}

static bool origin_adapter_rx(void *context, uint8_t *packet, size_t capacity,
                              size_t *size, uint64_t *timestamp)
{
    origin_adapter_phys_t *p = context;
    p->rx_calls++;
    if (!p->active) return loopback_rx(&p->bootstrap, packet, capacity, size, timestamp);
    p->paired = false;
    if (!p->rx_pending || capacity < sizeof(p->packet)) return false;
    memcpy(packet, p->packet, sizeof(p->packet));
    *size = sizeof(p->packet);
    *timestamp = UINT64_MAX; /* Must be discarded by the async consumer. */
    p->rx_pending = false;
    p->paired = !p->omit_pair;
    return true;
}

static bool origin_adapter_start(void *context, const tdma_ring_runtime_config_t *config,
    const uint8_t *packet, size_t size, uint32_t ticks, uint32_t polls)
{
    origin_adapter_phys_t *p = context;
    p->starts++;
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    p->begin_guarded = (p->adapter->snapshot_guard & 1u) != 0u;
    p->reader_deferred = !tdma_pio_spi_ring_adapter_try_get_snapshot(p->adapter, &snapshot);
    if (p->fail_start || config->node_count != 4u || size != sizeof(p->seed) ||
        ticks == 0u || polls == 0u) return false;
    memcpy(p->seed, packet, size);
    p->generation = 1u;
    return true;
}

static tdma_origin_admission_result_t origin_adapter_admit(void *context,
    const tdma_ring_runtime_config_t *config, uint32_t *ticks, uint32_t *polls)
{
    origin_adapter_phys_t *p = context;
    (void)config;
    p->admission_calls++;
    *ticks = 100u;
    *polls = 2u;
    return p->admission;
}

static tdma_origin_build_result_t origin_adapter_poll(void *context)
{
    origin_adapter_phys_t *p = context;
    p->prepare_polls++;
    if (p->prepare_polls < 3u) return TDMA_ORIGIN_BUILD_BUSY;
    if (p->fail_poll) return TDMA_ORIGIN_BUILD_FAILED;
    p->active = true;
    return TDMA_ORIGIN_BUILD_DONE;
}

static bool origin_adapter_stop(void *context)
{
    origin_adapter_phys_t *p = context;
    if (p->fail_stop) return false;
    p->active = false;
    return tdma_overlay_prepare_cancel(p->job);
}

static bool origin_adapter_healthy(const void *context)
{
    const origin_adapter_phys_t *p = context;
    return p->active && !p->fault;
}

static bool origin_adapter_ready(void *context)
{
    origin_adapter_phys_t *p = context;
    ++p->ready_calls;
    if (p->selection_seen) {
        p->pending = false;
        p->selection_seen = false;
    }
    return !p->pending;
}

static bool origin_adapter_publish(void *context, const uint8_t *mailbox, uint32_t *generation)
{
    origin_adapter_phys_t *p = context;
    if (p->pending || p->reject_publish) return false;
    memcpy(p->shadow, mailbox, sizeof(p->shadow));
    p->generation++;
    if (p->generation == 0u) p->generation = 1u;
    *generation = p->generation;
    p->publications++;
    p->pending = true;
    return true;
}

static bool origin_adapter_observe(void *context, tdma_origin_observation_t *observation)
{
    origin_adapter_phys_t *p = context;
    *observation = (tdma_origin_observation_t){.sequence = 900u, .identity = 123u,
        .local_generation = p->generation, .rtt_present = 1u, .rtt_remaining = 999u};
    return true; /* Deliberately newer and unrelated to the accepted bank. */
}

static bool origin_adapter_take_pair(void *context, tdma_origin_observation_t *observation)
{
    origin_adapter_phys_t *p = context;
    if (!p->paired) return false;
    *observation = p->observation;
    p->paired = false;
    return true;
}

static bool origin_adapter_complete(void *context, uint64_t *timestamp)
{
    ((origin_adapter_phys_t *)context)->completion_calls++;
    *timestamp = 1234ull;
    return false;
}

static bool origin_adapter_feedback(void *context, uint32_t *rtt, uint32_t *resolution, uint32_t *flags)
{
    ((origin_adapter_phys_t *)context)->feedback_calls++;
    *rtt = 500u; *resolution = 1u; *flags = TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    return true;
}

static void origin_adapter_enqueue(origin_adapter_phys_t *p, uint32_t sequence, uint32_t generation)
{
    tdma_transport_result_t result;
    tdma_transport_frame_view_t view;
    memcpy(p->packet, p->seed, sizeof(p->packet));
    if (generation != 1u) memcpy(p->packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE, p->shadow, sizeof(p->shadow));
    (void)tdma_transport_frame_prepare_resident_position(p->packet, sizeof(p->packet), sequence, 3u, &result);
    (void)tdma_transport_frame_decode(p->packet, sizeof(p->packet), &view, &result);
    p->observation = (tdma_origin_observation_t){.sequence = sequence, .identity = view.identity_crc32,
        .local_generation = generation, .rtt_present = 1u, .rtt_remaining = 777u};
    p->rx_pending = true;
}

static int test_origin_mailbox_selection(void)
{
    int failed = 0;
    for (uint32_t slot = 0u; slot < TDMA_FLIGHT_SHORT_SLOT_COUNT; ++slot) {
        for (uint32_t scenario = 0u; scenario < 12u; ++scenario) {
            tdma_pio_spi_ring_adapter_t adapter;
            tdma_flight_engine_t engine;
            tdma_flight_fifo_t fifo;
            tdma_ring_adapter_status_t status;
            origin_adapter_phys_t p = {.active = true, .generation = 1u, .adapter = &adapter};
            tdma_process_image_map_t map = make_eight_slot_flight_map();
            if (scenario == 8u) {
                map.segment[slot].byte_length--;
                map.map_crc32 = tdma_process_image_map_crc32(&map);
            }
            failed += expect_bool("mailbox selection fixture", tdma_pio_spi_ring_adapter_init(&adapter) &&
                tdma_flight_fifo_init(&fifo) && tdma_flight_engine_init(&engine) &&
                tdma_flight_engine_configure(&engine, &map) &&
                tdma_flight_engine_activate(&engine, slot), true);
            tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
            tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
            tdma_pio_spi_ring_adapter_set_phys(&adapter, origin_adapter_tx, origin_adapter_rx, &p);
            tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter, NULL, origin_adapter_stop, NULL, NULL, &p);
            const tdma_pio_spi_ring_origin_ops_t ops = {
                .begin = origin_adapter_start, .poll = origin_adapter_poll, .healthy = origin_adapter_healthy,
                .ready = origin_adapter_ready, .publish = origin_adapter_publish,
                .observe = origin_adapter_observe, .take_rx_observation = origin_adapter_take_pair};
            failed += expect_bool("mailbox selection callbacks", tdma_pio_spi_ring_adapter_set_phys_origin(&adapter, &ops), true);
            /* Isolate the running publication boundary. The tests below also
             * exercise the real bootstrap/prepare/STOP path end to end. */
            adapter.config = make_valid_config();
            adapter.config.node_count = TDMA_FLIGHT_SHORT_SLOT_COUNT;
            adapter.config.local_slot_id = adapter.config.reference_slot_id = slot;
            adapter.started = 1u;
            adapter.configured = true;
            adapter.origin.active = 1u;
            adapter.origin.published_generation = 1u;
            adapter.comm_fsm.state = TDMA_ADAPTER_COMM_STATE_AUTONOMOUS;
            memset(adapter.last_rx_packet, 0xa5, sizeof(adapter.last_rx_packet));
            uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
            fill_test_process_mailbox(mailbox, scenario == 3u ? (slot + 1u) % 8u : slot, 0x42u, (uint8_t)(slot + 0x20u));
            if (scenario == 2u) mailbox[12] ^= 1u;
            uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];
            memset(image, 0x69, sizeof(image));
            memcpy(image + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE, mailbox, sizeof(mailbox));
            const bool full = scenario == 1u || scenario == 11u;
            const size_t size = scenario == 4u ? sizeof(mailbox) - 1u :
                (full ? (scenario == 11u ? (slot + 1u) * sizeof(mailbox) : sizeof(image)) : sizeof(mailbox));
            const uint32_t mask = 1u << (scenario == 5u ? (slot + 1u) % 8u : slot);
            failed += expect_bool("mailbox immutable FIFO publication", tdma_flight_fifo_core0_publish_tx(
                &fifo, full ? image : mailbox, size, 0x10042u, 0x20042u, mask), true);
            if (scenario == 6u) engine.map_sequence |= 1u;
            if (scenario == 7u) tdma_flight_engine_deactivate(&engine);
            if (scenario == 9u) adapter.config.local_slot_id = (slot + 1u) % 8u;
            p.reject_publish = scenario == 10u;
            failed += expect_bool("mailbox rejection never stops autonomous wire",
                tdma_pio_spi_ring_adapter_ops()->service(&adapter, 1000u, &status), true);
            if (scenario == 10u) {
                failed += expect_u32("mailbox deferred no new hardware generation", p.publications, 0u);
                failed += expect_u32("mailbox deferred no owner acknowledgement", adapter.origin.published_owner_generation, 0u);
                p.reject_publish = false;
                memset(image, 0, sizeof(image)); /* Caller storage is not the active FIFO view. */
                failed += expect_bool("mailbox deferred retries complete active view",
                    tdma_pio_spi_ring_adapter_ops()->service(&adapter, 2000u, &status), true);
            }
            const bool valid = scenario < 2u || scenario >= 10u;
            failed += expect_u32("mailbox hardware publication authorization", p.publications, valid ? 1u : 0u);
            if (valid) {
                failed += expect_bool("mailbox selected bytes equal prepared local payload",
                    memcmp(p.shadow, mailbox, sizeof(mailbox)) == 0, true);
                failed += expect_u32("mailbox full owner generation preserved", adapter.origin.published_owner_generation, 0x10042u);
                failed += expect_u32("mailbox full owner sequence preserved", adapter.origin.published_owner_sequence, 0x20042u);
                p.pending = false;
                failed += expect_bool("mailbox unchanged generation service",
                    tdma_pio_spi_ring_adapter_ops()->service(&adapter, 3000u, &status), true);
                failed += expect_u32("mailbox unchanged generation not republished", p.publications, 1u);
            } else {
                failed += expect_u32("mailbox rejected leaves old hardware generation", adapter.origin.published_generation, 1u);
            }
        }
    }
    return failed;
}

static bool origin_adapter_grant_tx(void *context, tdma_overlay_prepare_t *job)
{
    origin_adapter_phys_t *p = context;
    if (job != p->job || tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_IDLE ||
        !origin_adapter_ready(p)) return false;
    ++p->grants;
    job->plan = NULL;
    job->config = (tdma_flight_overlay_config_t){
        .packet_size = TDMA_TRANSPORT_FRAME_HEADER_SIZE + p->adapter->flight_engine->map.payload_size,
        .local_slot_id = p->adapter->config.local_slot_id,
    };
    return true;
}

static bool origin_adapter_commit_tx(void *context, tdma_overlay_prepare_t *job, uint32_t *generation)
{
    origin_adapter_phys_t *p = context;
    ++p->commit_attempts;
    return job == p->job && tdma_overlay_prepare_origin_current(job,
        TDMA_TRANSPORT_FRAME_HEADER_SIZE + p->adapter->flight_engine->map.payload_size,
        p->adapter->config.local_slot_id) && origin_adapter_publish(p, job->tx_data, generation);
}

static int test_origin_async_mailbox(void)
{
    int failed = 0;
    tdma_pio_spi_ring_adapter_t adapter;
    tdma_flight_engine_t engine;
    tdma_flight_fifo_t fifo;
    tdma_overlay_prepare_t job = {0};
    tdma_ring_adapter_status_t status;
    origin_adapter_phys_t p = {.active = true, .generation = 1u, .adapter = &adapter, .job = &job};
    const tdma_process_image_map_t map = make_eight_slot_flight_map();
    failed += expect_bool("origin async fixture", tdma_pio_spi_ring_adapter_init(&adapter) &&
        tdma_flight_engine_init(&engine) && tdma_flight_fifo_init(&fifo) &&
        tdma_flight_engine_configure(&engine, &map) && tdma_flight_engine_activate(&engine, 0u), true);
    if (failed) return failed;
    tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
    tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
    tdma_pio_spi_ring_adapter_set_phys(&adapter, origin_adapter_tx, origin_adapter_rx, &p);
    tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter, NULL, origin_adapter_stop, NULL, NULL, &p);
    tdma_pio_spi_ring_origin_ops_t ops = {
        .begin = origin_adapter_start, .poll = origin_adapter_poll, .healthy = origin_adapter_healthy,
        .ready = origin_adapter_ready, .publish = origin_adapter_publish,
        .observe = origin_adapter_observe, .take_rx_observation = origin_adapter_take_pair,
        .grant_tx = origin_adapter_grant_tx};
    failed += expect_bool("origin async callback pair required", tdma_pio_spi_ring_adapter_set_phys_origin(&adapter, &ops), false);
    ops.commit_tx = origin_adapter_commit_tx;
    failed += expect_bool("origin async callbacks", tdma_pio_spi_ring_adapter_set_phys_origin(&adapter, &ops), true);
    adapter.overlay_preparation = &job;
    adapter.config = make_valid_config();
    adapter.config.node_count = TDMA_FLIGHT_SHORT_SLOT_COUNT;
    adapter.config.local_slot_id = adapter.config.reference_slot_id = 0u;
    adapter.started = 1u;
    adapter.configured = true;
    adapter.origin.active = adapter.origin.published_generation = 1u;
    adapter.comm_fsm.state = TDMA_ADAPTER_COMM_STATE_AUTONOMOUS;
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE], first[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    fill_test_process_mailbox(first, 0u, 1u, 0x31u);
    failed += expect_bool("origin async first FIFO version", tdma_flight_fifo_core0_publish_tx(&fifo,
        first, sizeof(first), 0x10001u, 0x10001u, 1u), true);
    failed += expect_bool("origin async request yields", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 1000u, &status), true);
    failed += expect_u32("origin async requested", tdma_overlay_prepare_state(&job), TDMA_OVERLAY_PREPARE_REQUESTED);
    failed += expect_bool("origin worker has private bytes", job.tx.data == job.tx_data && job.plan == NULL, true);
    failed += expect_u32("origin request leaves wire generation", p.publications, 0u);
    failed += expect_bool("origin worker paused after claim", tdma_overlay_prepare_core0_claim(&job), true);
    const uint32_t tail = fifo.tx_tail;
    fill_test_process_mailbox(mailbox, 0u, 1u, 0x62u);
    failed += expect_bool("origin concurrent next version", tdma_flight_fifo_core0_publish_tx(&fifo,
        mailbox, sizeof(mailbox), 0x20001u, 0x20001u, 1u), true);
    failed += expect_bool("origin slow worker retains live wire", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 2000u, &status), true);
    failed += expect_u32("origin slow worker keeps queued version", fifo.tx_tail, tail);
    failed += expect_u32("origin slow worker does not publish", p.publications, 0u);
    tdma_overlay_prepare_core0_build_claimed(&job);
    failed += expect_u32("origin worker READY", tdma_overlay_prepare_state(&job), TDMA_OVERLAY_PREPARE_READY);
    p.reject_publish = true;
    failed += expect_bool("origin deferred commit yields", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 3000u, &status), true);
    failed += expect_u32("origin deferred retains READY", tdma_overlay_prepare_state(&job), TDMA_OVERLAY_PREPARE_READY);
    failed += expect_u32("origin deferred does not account accepted output", engine.map_apply_count, 0u);
    p.reject_publish = false;
    failed += expect_bool("origin prepared commit", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 4000u, &status), true);
    failed += expect_u32("origin exact first complete generation", adapter.origin.published_owner_generation, 0x10001u);
    failed += expect_bool("origin first private bytes preserved", memcmp(p.shadow, first, sizeof(first)) == 0, true);
    failed += expect_u32("origin successful output accounted once", engine.map_apply_count, 1u);
    failed += expect_bool("origin pending selection yields", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 5000u, &status), true);
    failed += expect_u32("origin pending keeps next FIFO version", fifo.tx_tail, tail);
    p.selection_seen = true;
    failed += expect_bool("origin retires then requests queued version", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 6000u, &status), true);
    failed += expect_bool("origin selection consumed before grant", !p.pending && !p.selection_seen, true);
    failed += expect_u32("origin queued full generation accepted", job.tx.generation, 0x20001u);
    tdma_overlay_prepare_core0_service(&job);
    failed += expect_bool("origin second prepared commit", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 7000u, &status), true);
    failed += expect_u32("origin equal low bits do not alias", adapter.origin.published_owner_sequence, 0x20001u);
    failed += expect_bool("origin second private bytes selected", memcmp(p.shadow, mailbox, sizeof(mailbox)) == 0, true);
    p.selection_seen = true;
    const uint32_t reused = fifo.tx_reuse_count;
    failed += expect_bool("origin unchanged service", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 8000u, &status), true);
    failed += expect_u32("origin unchanged counts one reuse", fifo.tx_reuse_count, reused + 1u);
    failed += expect_u32("origin unchanged does not request worker", tdma_overlay_prepare_state(&job), TDMA_OVERLAY_PREPARE_IDLE);
    mailbox[20] ^= 1u;
    failed += expect_bool("origin invalid version FIFO", tdma_flight_fifo_core0_publish_tx(&fifo,
        mailbox, sizeof(mailbox), 0x30001u, 0x30001u, 1u), true);
    failed += expect_bool("origin invalid version awaits worker validation", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 9000u, &status), true);
    failed += expect_u32("origin CRC work is requested", tdma_overlay_prepare_state(&job), TDMA_OVERLAY_PREPARE_REQUESTED);
    tdma_overlay_prepare_core0_service(&job);
    failed += expect_u32("origin worker rejects invalid CRC", tdma_overlay_prepare_state(&job), TDMA_OVERLAY_PREPARE_FAILED);
    failed += expect_bool("origin failed update leaves wire running", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 10000u, &status), true);
    failed += expect_u32("origin invalid version not published", p.publications, 2u);
    failed += expect_u32("origin invalid version reports rejection", adapter.last_error, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
    fill_test_process_mailbox(mailbox, 0u, 2u, 0x73u);
    failed += expect_bool("origin stale map fixture", tdma_flight_fifo_core0_publish_tx(&fifo,
        mailbox, sizeof(mailbox), 0x40002u, 0x40002u, 1u), true);
    failed += expect_bool("origin stale map request", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 11000u, &status), true);
    tdma_overlay_prepare_core0_service(&job);
    const uint32_t attempts = p.commit_attempts, map_generation = engine.map_generation;
    ++engine.map_generation; /* Inject a foreign completion without changing its private input. */
    failed += expect_bool("origin stale map yields", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 12000u, &status), true);
    failed += expect_u32("origin stale map rejected before hardware", p.commit_attempts, attempts);
    engine.map_generation = map_generation;
    failed += expect_bool("origin active FIFO retries after stale result", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 13000u, &status), true);
    failed += expect_bool("origin cancel during worker", tdma_overlay_prepare_core0_claim(&job), true);
    failed += expect_bool("origin STOP waits for private writer ACK", tdma_pio_spi_ring_adapter_ops()->stop(&adapter), false);
    failed += expect_u32("origin cancelled worker retains lifetime", adapter.origin.active, 1u);
    failed += expect_u32("origin cancelled worker state", tdma_overlay_prepare_state(&job), TDMA_OVERLAY_PREPARE_CANCELLED);
    tdma_overlay_prepare_core0_build_claimed(&job);
    failed += expect_u32("origin cancelled result never READY", tdma_overlay_prepare_state(&job), TDMA_OVERLAY_PREPARE_IDLE);
    failed += expect_bool("origin STOP completes after ACK", tdma_pio_spi_ring_adapter_ops()->stop(&adapter), true);
    failed += expect_u32("origin successful STOP clears owner", adapter.origin.published_owner_generation, 0u);
    failed += expect_u32("origin cancellation never publishes", p.publications, 2u);
    return failed;
}

static int test_origin_adapter(void)
{
    int failed = test_origin_mailbox_selection() + test_origin_async_mailbox();
    const tdma_pio_spi_ring_origin_ops_t ops = {
        .admit = origin_adapter_admit,
        .begin = origin_adapter_start, .poll = origin_adapter_poll, .healthy = origin_adapter_healthy,
        .ready = origin_adapter_ready, .publish = origin_adapter_publish,
        .observe = origin_adapter_observe, .take_rx_observation = origin_adapter_take_pair,
    };
    /* A real completed bootstrap window is the sole handoff point. Repeat
     * with a hardware start refusal to prove no legacy resubmission. */
    for (uint32_t scenario = 0u; scenario < 10u; ++scenario) {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_flight_engine_t engine;
        tdma_flight_fifo_t fifo;
        tdma_ring_adapter_status_t status;
        origin_adapter_phys_t p = {.bootstrap = {.tx_timestamp_ns = 100u,
            .rx_timestamp_ns = 600u, .advance_echo_to_feedback = true}, .adapter = &adapter};
        tdma_ring_runtime_config_t config = make_valid_config();
        config.node_count = 4u;
        config.feedback_timeout_ns = 4000u;
        config.flags |= TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE;
        const tdma_process_image_map_t map = make_eight_slot_flight_map();
        uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
        fill_test_process_mailbox(mailbox, 0u, 1u, 0x31u);
        failed += expect_bool("origin init", tdma_pio_spi_ring_adapter_init(&adapter), true);
        failed += expect_bool("origin map", tdma_flight_engine_init(&engine) &&
            tdma_flight_engine_configure(&engine, &map) && tdma_flight_fifo_init(&fifo), true);
        set_test_sequential_topology(&adapter, 4u);
        tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
        tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
        tdma_pio_spi_ring_adapter_set_phys(&adapter, origin_adapter_tx, origin_adapter_rx, &p);
        tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter, NULL, origin_adapter_stop, NULL, NULL, &p);
        tdma_pio_spi_ring_adapter_set_phys_tx_complete(&adapter, origin_adapter_complete);
        tdma_pio_spi_ring_adapter_set_phys_feedback(&adapter, origin_adapter_feedback);
        failed += expect_bool("origin bind", tdma_pio_spi_ring_adapter_set_phys_origin(&adapter, &ops), true);
        failed += expect_bool("origin mode", tdma_pio_spi_ring_adapter_set_forwarding_mode(&adapter,
            TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE), true);
        failed += expect_bool("origin premature install", tdma_pio_spi_ring_adapter_start_origin(&adapter, 100u, 2u), false);
        failed += expect_bool("origin legacy start", tdma_pio_spi_ring_adapter_ops()->start(&adapter, &config), true);
        failed += expect_bool("origin no rebind live", tdma_pio_spi_ring_adapter_set_phys_origin(&adapter, &ops), false);
        failed += expect_bool("origin seed local", tdma_flight_fifo_core0_publish_tx(&fifo, mailbox,
            sizeof(mailbox), 0x10001u, 1u, 1u), true);
        failed += expect_bool("origin phase", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 1ull, &status), true);
        failed += expect_bool("origin seed", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 2ull, &status), true);
        failed += expect_u32("origin real bootstrap boundary", adapter.comm_fsm.state, TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY);
        failed += expect_bool("origin zero budget rejected", tdma_pio_spi_ring_adapter_start_origin(&adapter, 0u, 2u), false);
        const tdma_ring_adapter_status_t bootstrap_status = status;
        if (scenario >= 6u) {
            const uint32_t tx_before = p.bootstrap.tx_calls;
            p.admission = scenario == 7u ? TDMA_ORIGIN_ADMISSION_REJECTED : TDMA_ORIGIN_ADMISSION_READY;
            p.fail_start = scenario == 8u;
            if (scenario == 9u) adapter.last_rx_packet_size = 0u;
            failed += expect_bool("origin automatic boundary handoff",
                tdma_pio_spi_ring_adapter_ops()->service(&adapter, 3ull, &status), scenario == 6u);
            failed += expect_u32("origin automatic no second TX", p.bootstrap.tx_calls, tx_before);
            failed += expect_u32("origin automatic no same-call poll", p.prepare_polls, 0u);
            failed += expect_u32("origin automatic cleanup latch", adapter.origin.active, 1u);
            failed += expect_u32("origin automatic begin count", p.starts, scenario == 6u || scenario == 8u ? 1u : 0u);
            if (p.starts) {
                failed += expect_bool("origin automatic single writer", p.begin_guarded, true);
                failed += expect_bool("origin automatic reader defers", p.reader_deferred, true);
            }
            p.fail_stop = true;
            failed += expect_bool("origin automatic failed cleanup", tdma_pio_spi_ring_adapter_ops()->stop(&adapter), false);
            failed += expect_bool("origin automatic restart denied", tdma_pio_spi_ring_adapter_ops()->start(&adapter, &config), false);
            p.fail_stop = false;
            failed += expect_bool("origin automatic stopped", tdma_pio_spi_ring_adapter_ops()->stop(&adapter), true);
            continue;
        }
        failed += expect_bool("origin bootstrap has cumulative facts",
            bootstrap_status.rx_count != 0u && bootstrap_status.tx_count != 0u &&
            bootstrap_status.down_rx_sequence != 0u, true);
        p.fail_start = scenario == 1u;
        p.fail_poll = scenario == 2u;
        failed += expect_bool("origin begin", tdma_pio_spi_ring_adapter_start_origin(&adapter, 100u, 2u), scenario != 1u);
        const uint32_t tx_calls = p.bootstrap.tx_calls;
        const uint32_t completion_calls = p.completion_calls;
        const uint32_t feedback_calls = p.feedback_calls;
        const uint32_t completed = adapter.comm_fsm.completed_window_count;
        if (scenario != 1u) {
            const uint32_t rx_before = p.rx_calls;
            const uint32_t tail_before = fifo.tx_tail;
            failed += expect_u32("origin preparation state", adapter.comm_fsm.state, TDMA_ADAPTER_COMM_STATE_RESIDENT_PREPARING);
            failed += expect_u32("origin no published hardware yet", adapter.origin.published_generation, 0u);
            const uint32_t polls = scenario >= 3u ? scenario - 3u : 3u;
            for (uint32_t poll = 0u; poll < polls; ++poll) {
                failed += expect_bool("origin one preparation step", tdma_pio_spi_ring_adapter_ops()->service(
                    &adapter, 3ull + poll, &status), scenario != 2u || poll != 2u);
                failed += expect_u32("origin one poll per service", p.prepare_polls, poll + 1u);
                failed += expect_u32("origin prep no RX", p.rx_calls, rx_before);
                failed += expect_u32("origin prep no TX consume", fifo.tx_tail, tail_before);
                failed += expect_u32("origin prep no fake running", status.up_running | status.down_running, 0u);
                failed += expect_u32("origin prep retains RX total", status.rx_count, bootstrap_status.rx_count);
                failed += expect_u32("origin prep retains TX total", status.tx_count, bootstrap_status.tx_count);
                failed += expect_u32("origin prep retains sequence", status.down_rx_sequence, bootstrap_status.down_rx_sequence);
                failed += expect_u64("origin prep old latch remains invalid", status.feedback_rx_timestamp_ns, 0ull);
                failed += expect_u32("origin prep no time eligibility", status.timestamp_flags,
                    TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
                tdma_pio_spi_ring_adapter_snapshot_t snapshot;
                failed += expect_bool("origin prep snapshot guard closed", tdma_pio_spi_ring_adapter_get_snapshot(&adapter, &snapshot), true);
            }
            if (scenario >= 3u) {
                p.fail_stop = true;
                failed += expect_bool("origin prep STOP refusal", tdma_pio_spi_ring_adapter_ops()->stop(&adapter), false);
                failed += expect_u32("origin prep refusal retains lifetime", adapter.origin.active, 1u);
                failed += expect_bool("origin prep cannot rebind", tdma_pio_spi_ring_adapter_set_phys_origin(&adapter, &ops), false);
                failed += expect_bool("origin prep cannot rearm", tdma_pio_spi_ring_adapter_ops()->start(&adapter, &config), false);
                p.fail_stop = false;
                failed += expect_bool("origin prep STOP success", tdma_pio_spi_ring_adapter_ops()->stop(&adapter), true);
                failed += expect_u32("origin prep lifetime retired", adapter.origin.active, 0u);
                failed += expect_u32("origin prep no legacy TX", p.bootstrap.tx_calls, tx_calls);
                continue;
            }
        }
        if (scenario == 0u) {
            failed += expect_u32("origin autonomous state", adapter.comm_fsm.state, TDMA_ADAPTER_COMM_STATE_AUTONOMOUS);
            failed += expect_bool("origin one install", tdma_pio_spi_ring_adapter_start_origin(&adapter, 100u, 2u), false);
            origin_adapter_enqueue(&p, 9u, 1u);
            uint32_t rx_calls = p.rx_calls;
            failed += expect_bool("origin skips service cycles", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 1000000ull, &status), true);
            failed += expect_u32("origin one RX attempt", p.rx_calls - rx_calls, 1u);
            failed += expect_u32("origin accepted own bank", adapter.origin.returned.sequence, 9u);
            failed += expect_u32("origin independent latest boundary", adapter.origin.boundary.sequence, 900u);
            failed += expect_u32("origin gaps not fabricated completion", adapter.origin.unobserved_cycle_count, 7u);
            failed += expect_u32("origin no synthetic windows", adapter.comm_fsm.completed_window_count, completed);
            failed += expect_u32("origin local shadow publication", p.publications, 1u);
            failed += expect_u32("origin full owner generation", adapter.origin.published_owner_generation, 0x10001u);
            failed += expect_u64("origin no absolute RX", status.feedback_rx_timestamp_ns, 0ull);
            failed += expect_u32("origin no DPLL reference", status.feedback_reference_sequence, 0u);
            fill_test_process_mailbox(mailbox, 0u, 2u, 0x32u);
            failed += expect_bool("origin queue next owner", tdma_flight_fifo_core0_publish_tx(&fifo, mailbox,
                sizeof(mailbox), 0x20001u, 2u, 1u), true);
            const uint32_t tail = fifo.tx_tail;
            origin_adapter_enqueue(&p, 10u, 1u);
            failed += expect_bool("origin old generation return", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 1000001ull, &status), true);
            failed += expect_u32("origin pending retains FIFO", fifo.tx_tail, tail);
            failed += expect_u32("origin old seed has unknown owner generation", adapter.origin.matched_owner_generation, 0u);
            p.pending = false; /* Hardware selects the first published shadow. */
            origin_adapter_enqueue(&p, 11u, 2u);
            failed += expect_bool("origin selected return", tdma_pio_spi_ring_adapter_ops()->service(&adapter, 1000002ull, &status), true);
            failed += expect_u32("origin exact return owner generation", adapter.origin.matched_owner_generation, 0x10001u);
            failed += expect_u32("origin next published owner generation", adapter.origin.published_owner_generation, 0x20001u);
            failed += expect_u32("origin one subsequent publication", p.publications, 2u);
            const uint32_t accepted = adapter.origin.observed_return_count;
            const uint32_t rx_head = fifo.rx_head;
            for (uint32_t bad = 0u; bad < 8u; ++bad) {
                origin_adapter_enqueue(&p, 12u, 3u);
                if (bad == 0u) p.observation.sequence++;
                if (bad == 1u) p.observation.identity ^= 1u;
                if (bad == 2u) p.observation.local_generation = 99u;
                if (bad == 3u) p.observation.output_remaining = 1u;
                if (bad == 4u) p.omit_pair = true;
                if (bad == 5u) origin_adapter_enqueue(&p, 11u, 3u);
                if (bad == 6u) p.packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE + TDMA_FLIGHT_SHORT_SLOT_SIZE + 12u] ^= 1u;
                if (bad == 7u) fill_test_process_mailbox(p.packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE, 0u, 4u, 0x33u);
                failed += expect_bool("origin rejects pair without stopping wire", tdma_pio_spi_ring_adapter_ops()->service(
                    &adapter, 1000003ull + bad, &status), true);
                p.omit_pair = false;
            }
            failed += expect_u32("origin bad pair count", adapter.origin.rejected_observation_count, 8u);
            failed += expect_u32("origin bad pair no accepted progress", adapter.origin.observed_return_count, accepted);
            failed += expect_u32("origin bad pair not published", fifo.rx_head, rx_head);
            p.fault = true;
        }
        failed += expect_bool("origin hardware fault cannot restart legacy", tdma_pio_spi_ring_adapter_ops()->service(
            &adapter, UINT64_C(9000000000), &status), false);
        failed += expect_u32("origin no per-frame CPU TX", p.bootstrap.tx_calls, tx_calls);
        failed += expect_u32("origin no legacy completion reads", p.completion_calls, completion_calls);
        failed += expect_u32("origin no legacy RTT reads", p.feedback_calls, feedback_calls);
        p.fail_stop = true;
        failed += expect_bool("origin failed stop", tdma_pio_spi_ring_adapter_ops()->stop(&adapter), false);
        failed += expect_u32("origin failed stop retains latch", adapter.origin.active, 1u);
        failed += expect_bool("origin failed stop forbids fresh ARM", tdma_pio_spi_ring_adapter_ops()->start(&adapter, &config), false);
        p.fail_stop = false;
        failed += expect_bool("origin successful stop", tdma_pio_spi_ring_adapter_ops()->stop(&adapter), true);
        failed += expect_u32("origin stopped clears lifetime", adapter.origin.active, 0u);
    }
    return failed;
}
