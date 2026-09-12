/* Core1 admission and acceptance. No worker sees this adapter. */
static bool tdma_pio_spi_ring_rx_accept(tdma_pio_spi_ring_adapter_t *adapter,
                                       tdma_rx_prepare_t *job)
{
    if (tdma_rx_prepare_state(job) != TDMA_RX_PREPARE_READY) return false;
    const uint64_t now = adapter->last_service_ns;
    const uint64_t age_limit = adapter->receive_health.configured != 0u
        ? adapter->receive_health.config.stale_timeout_ns : adapter->config.feedback_timeout_ns;
    const uint32_t map_generation = adapter->flight_engine != NULL
        ? adapter->flight_engine->map_generation : 0u;
    const bool current = adapter->started != 0u && job->request_epoch == job->epoch &&
        job->schedule_crc32 == adapter->config.schedule_crc32 &&
        job->profile_crc32 == adapter->config.ring_profile_crc32 &&
        job->map_generation == map_generation && job->origin_active == (adapter->origin.active != 0u) &&
        job->capture_timestamp_flags == adapter->timestamp_flags &&
        job->capture_timestamp_resolution_ns == adapter->timestamp_resolution_ns &&
        now >= job->capture_service_ns && now - job->capture_service_ns <= age_limit;
    bool accepted = false;
    if (current) {
        const uint64_t started = tdma_service_timing_now();
        accepted = tdma_pio_spi_ring_adapter_process_rx_impl(adapter,
            job->packet, job->packet_size, job->rx_timestamp_ns,
            job->origin_paired ? &job->origin : NULL, job);
        tdma_service_timing_record(TDMA_TIMING_RX_PARSE, started);
    } else {
        /* A stale/foreign copy is neither a new receive nor a wire error. */
        adapter->rx_drop_count++;
    }
    tdma_rx_prepare_release(job);
    return accepted;
}

static bool tdma_pio_spi_ring_rx_request(tdma_pio_spi_ring_adapter_t *adapter,
    tdma_rx_prepare_t *job, size_t packet_size, uint64_t timestamp,
    const tdma_origin_observation_t *observation)
{
    if (packet_size == 0u || packet_size > sizeof(job->packet)) return false;
    job->packet_size = packet_size;
    job->capture_service_ns = adapter->last_service_ns;
    job->rx_timestamp_ns = adapter->origin.active != 0u ? 0ull : timestamp;
    job->schedule_crc32 = adapter->config.schedule_crc32;
    job->profile_crc32 = adapter->config.ring_profile_crc32;
    job->map_generation = adapter->flight_engine != NULL ? adapter->flight_engine->map_generation : 0u;
    job->node_count = adapter->config.node_count;
    job->capture_timestamp_flags = adapter->timestamp_flags;
    job->capture_timestamp_resolution_ns = adapter->timestamp_resolution_ns;
    job->origin_active = adapter->origin.active != 0u;
    job->origin_paired = observation != NULL;
    job->origin_owner_match = false;
    job->origin_owner_generation = job->origin_owner_sequence = 0u;
    job->origin = observation != NULL ? *observation : (tdma_origin_observation_t){0};
    if (observation != NULL && packet_size == TDMA_TRANSPORT_SHORT_PACKET_MAX) {
        for (uint32_t i = 0u; i < TDMA_ORIGIN_PLAN_BANK_COUNT; ++i) {
            if (adapter->origin_shadow[i].generation == observation->local_generation &&
                memcmp(adapter->origin_shadow[i].mailbox,
                    job->packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE +
                        adapter->config.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE,
                    TDMA_FLIGHT_SHORT_SLOT_SIZE) == 0) {
                job->origin_owner_match = true;
                job->origin_owner_generation = adapter->origin_shadow[i].owner_generation;
                job->origin_owner_sequence = adapter->origin_shadow[i].owner_sequence;
                break;
            }
        }
    }
    job->round_trip_ns = job->resolution_ns = job->flags = 0u;
    job->round_trip_valid = !job->origin_active &&
        adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE && adapter->phys_feedback != NULL &&
        adapter->phys_feedback(adapter->phys_context, &job->round_trip_ns,
            &job->resolution_ns, &job->flags);
    uint32_t sequence = 0u, identity = 0u;
    (void)tdma_transport_frame_capture_hint(job->packet, packet_size, &sequence, &identity);
    job->expected_size = 0u;
    job->expected_clock = false;
    const uint32_t index = sequence % TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
    if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE && sequence != 0u &&
        adapter->reference_tx_evidence[index].valid &&
        adapter->reference_tx_evidence[index].sequence == sequence &&
        adapter->reference_tx_evidence[index].packet_size <= sizeof(job->expected)) {
        job->expected_size = adapter->reference_tx_evidence[index].packet_size;
        job->expected_clock = adapter->reference_tx_evidence[index].clock_evidence;
        memcpy(job->expected, adapter->reference_tx_evidence[index].packet, job->expected_size);
    }
    /* Drain the captured frame's TX latch now. Binding is only a hint until
     * Core0 validates this same packet; no evidence record is published here. */
    job->local_tx_captured = false;
    memset(&job->local_tx, 0, sizeof(job->local_tx));
    if (!job->origin_active && adapter->role != TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
        (adapter->forwarding_mode == TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT ||
         adapter->forwarding_mode == TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE)) {
        if (adapter->phys_local_tx_edge_ex != NULL) {
            job->local_tx_captured = adapter->phys_local_tx_edge_ex(adapter->phys_context,
                sequence, identity, &job->local_tx);
        } else if (adapter->phys_local_tx_edge != NULL) {
            job->local_tx_captured = adapter->phys_local_tx_edge(adapter->phys_context,
                &job->local_tx.timestamp_ns);
            if (job->local_tx_captured && job->local_tx.timestamp_ns != 0ull)
                job->local_tx.flags = TDMA_RING_LOCAL_TX_EDGE_FLAG_TIMESTAMP_VALID;
        }
    }
    return tdma_rx_prepare_request(job);
}
