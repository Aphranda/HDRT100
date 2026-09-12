/* Included after the adapter fixture helpers. Exercise the real adapter,
 * engine, FIFO, receive gates and FSM across a fake asynchronous boundary. */
typedef struct {
    loopback_phys_t bootstrap;
    bool active, fault, pending, fail_start, fail_stop, rx_pending, paired;
    bool omit_pair, reject_publish, fail_poll;
    uint32_t starts, publications, completion_calls, feedback_calls, rx_calls;
    uint32_t generation;
    uint32_t prepare_polls;
    tdma_pio_spi_ring_adapter_t *adapter;
    uint32_t admission_calls;
    tdma_origin_admission_result_t admission;
    bool begin_guarded, reader_deferred;
    uint8_t seed[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
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
    return true;
}

static bool origin_adapter_healthy(const void *context)
{
    const origin_adapter_phys_t *p = context;
    return p->active && !p->fault;
}

static bool origin_adapter_ready(void *context)
{
    return !((origin_adapter_phys_t *)context)->pending;
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

static int test_origin_adapter(void)
{
    int failed = 0;
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
