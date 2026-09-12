/* Core1 adapter boundary for an owner-admitted autonomous origin. Physical
 * callbacks own the graph; the adapter owns software views and provenance.
 * No polling action here launches, rearms or waits for a physical cycle. */
static bool tdma_pio_spi_ring_adapter_rx_once(tdma_pio_spi_ring_adapter_t *adapter);

static bool tdma_pio_spi_ring_origin_mailbox_valid(const uint8_t *mailbox,
                                                  uint32_t slot, uint32_t mask)
{
    return mailbox != NULL &&
        ((uint32_t)mailbox[0] | ((uint32_t)mailbox[1] << 8u)) == TDMA_FLIGHT_MAILBOX_MAGIC &&
        mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] == TDMA_FLIGHT_MAILBOX_VERSION &&
        mailbox[3] == TDMA_PROCESS_IMAGE_MESSAGE_CLASS &&
        mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] == slot &&
        (mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] & ~mask) == 0u &&
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET) ==
            ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] |
             ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] << 8u));
}

static bool tdma_pio_spi_ring_origin_header_valid(
    const tdma_pio_spi_ring_adapter_t *adapter, const tdma_transport_frame_view_t *view)
{
    if (view->payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE ||
        view->payload_size != TDMA_FLIGHT_SHORT_PAYLOAD_SIZE ||
        view->origin_slot_id != adapter->config.local_slot_id ||
        view->hop_count != adapter->config.node_count - 1u ||
        view->hop_limit != view->hop_count ||
        view->schedule_crc32 != adapter->config.schedule_crc32 ||
        view->ring_profile_crc32 != adapter->config.ring_profile_crc32 ||
        view->flags != (TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK | TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE))
        return false;
    return true;
}

static bool tdma_pio_spi_ring_origin_packet_valid(
    const tdma_pio_spi_ring_adapter_t *adapter, const tdma_transport_frame_view_t *view)
{
    if (!tdma_pio_spi_ring_origin_header_valid(adapter, view)) return false;
    const uint32_t mask = (1u << adapter->config.node_count) - 1u;
    for (uint32_t slot = 0u; slot < adapter->config.node_count; ++slot) {
        if (!tdma_pio_spi_ring_origin_mailbox_valid(
                view->payload + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE, slot, mask)) return false;
    }
    return true;
}

bool tdma_pio_spi_ring_adapter_set_phys_origin(tdma_pio_spi_ring_adapter_t *adapter,
                                              const tdma_pio_spi_ring_origin_ops_t *ops)
{
    if (adapter == NULL || adapter->started != 0u || adapter->origin.active != 0u ||
        (ops != NULL && (ops->begin == NULL || ops->poll == NULL || ops->healthy == NULL ||
         ops->ready == NULL || ops->publish == NULL || ops->observe == NULL ||
         ops->take_rx_observation == NULL))) return false;
    adapter->phys_origin = ops != NULL ? *ops : (tdma_pio_spi_ring_origin_ops_t){0};
    return true;
}

static void tdma_pio_spi_ring_origin_invalidate_time(tdma_pio_spi_ring_adapter_t *adapter)
{
    adapter->timestamp_resolution_ns = 0u;
    adapter->timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    adapter->feedback_reference_sequence = 0u;
    adapter->feedback_reference_frame_crc32 = 0u;
    adapter->feedback_timestamp_resolution_ns = 0u;
    adapter->feedback_timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    adapter->reference_tx_timestamp_ns = 0ull;
    adapter->feedback_rx_timestamp_ns = 0ull;
    adapter->rx_ready_timestamp_ns = 0ull;
    adapter->local_tx_edge_timestamp_ns = 0ull;
    adapter->local_tx_edge_flags = 0u;
    memset(adapter->reference_tx_evidence, 0, sizeof(adapter->reference_tx_evidence));
    memset(adapter->local_rx_evidence, 0, sizeof(adapter->local_rx_evidence));
    memset(adapter->local_tx_edge_evidence, 0, sizeof(adapter->local_tx_edge_evidence));
    memset(&adapter->clock_observation, 0, sizeof(adapter->clock_observation));
}

/* Caller already owns the sole snapshot writer guard. */
static bool tdma_pio_spi_ring_adapter_start_origin_impl(tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count)
{
    if (adapter == NULL || adapter->started == 0u || !adapter->configured ||
        adapter->origin.active != 0u || adapter->phys_origin.begin == NULL ||
        adapter->phys_disarm == NULL || adapter->phys_rx == NULL ||
        !tdma_pio_spi_ring_adapter_resident_process_image(adapter) ||
        (adapter->config.flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) == 0u ||
        adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY ||
        !adapter->resident_seeded || !adapter->resident_return_ready ||
        !adapter->last_rx_gate_accepted || adapter->reference_tx_completion_pending ||
        adapter->pending_tx_evidence || adapter->rx_queue_count != 0u ||
        adapter->last_rx_packet_size != TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        adapter->receive_health.configured == 0u || rearm_budget_ticks == 0u ||
        abort_poll_count == 0u || abort_poll_count > UINT16_MAX) return false;
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result;
    if (!tdma_transport_frame_decode(adapter->last_rx_packet, adapter->last_rx_packet_size,
            &view, &result) || !tdma_pio_spi_ring_origin_header_valid(adapter, &view) ||
        view.transport_sequence != adapter->up_sequence || view.identity_crc32 != adapter->up_tx_frame_crc32)
        return false;

    memset(&adapter->origin, 0, sizeof(adapter->origin));
    memset(adapter->origin_shadow, 0, sizeof(adapter->origin_shadow));
    adapter->origin.active = 1u;
    /* Once a handoff touches hardware, a failure requires common STOP.
     * Keep this latch even if the physical backend is no longer armed. */
    const bool started = adapter->phys_origin.begin(adapter->phys_ctrl_context, &adapter->config,
        adapter->last_rx_packet, adapter->last_rx_packet_size, rearm_budget_ticks, abort_poll_count);
    tdma_pio_spi_ring_origin_invalidate_time(adapter);
    if (!started || !tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
            TDMA_ADAPTER_COMM_EVENT_RESIDENT_PREPARE, 0u)) {
        (void)tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
            TDMA_ADAPTER_COMM_EVENT_ERROR, TDMA_ADAPTER_COMM_ERROR_RUNTIME);
        tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
        return false;
    }
    adapter->origin.returned.sequence = view.transport_sequence;
    adapter->origin.returned.identity = view.identity_crc32;
    adapter->origin_shadow[0].generation = 1u;
    memcpy(adapter->origin_shadow[0].mailbox,
        view.payload + adapter->config.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE,
        TDMA_FLIGHT_SHORT_SLOT_SIZE);
    /* Seed content has no known full owner generation. Do not recover it
     * from the mailbox's truncated wire sequence. */
    adapter->resident_return_ready = false;
    adapter->resident_completion_pending = false;
    adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    return true;
}

bool tdma_pio_spi_ring_adapter_start_origin(tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count)
{
    if (adapter == NULL) return false;
    tdma_pio_spi_ring_adapter_snapshot_write_begin(adapter);
    const bool result = tdma_pio_spi_ring_adapter_start_origin_impl(
        adapter, rearm_budget_ticks, abort_poll_count);
    tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
    return result;
}

static bool tdma_pio_spi_ring_origin_prepare_service(tdma_pio_spi_ring_adapter_t *adapter)
{
    const tdma_origin_build_result_t result = adapter->phys_origin.poll(adapter->phys_ctrl_context);
    if (result == TDMA_ORIGIN_BUILD_BUSY) return true;
    if (result == TDMA_ORIGIN_BUILD_DONE &&
        tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
                                      TDMA_ADAPTER_COMM_EVENT_RESIDENT_INSTALLED, 0u)) {
        adapter->origin.published_generation = 1u;
        return true;
    }
    (void)tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
        TDMA_ADAPTER_COMM_EVENT_ERROR, TDMA_ADAPTER_COMM_ERROR_RUNTIME);
    tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
    return false;
}

static bool tdma_pio_spi_ring_origin_pair_valid(tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_transport_frame_view_t *view, const tdma_origin_observation_t *observation)
{
    if (observation == NULL || !tdma_pio_spi_ring_origin_packet_valid(adapter, view) ||
        observation->sequence != view->transport_sequence || observation->identity != view->identity_crc32 ||
        observation->local_generation == 0u || observation->output_remaining != 0u) return false;
    const uint32_t advance = view->transport_sequence - adapter->origin.returned.sequence;
    if (advance == 0u || advance > INT32_MAX) return false;
    for (uint32_t i = 0u; i < TDMA_ORIGIN_PLAN_BANK_COUNT; ++i) {
        if (adapter->origin_shadow[i].generation == observation->local_generation &&
            memcmp(adapter->origin_shadow[i].mailbox,
                view->payload + adapter->config.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE,
                TDMA_FLIGHT_SHORT_SLOT_SIZE) == 0) return true;
    }
    return false;
}

static void tdma_pio_spi_ring_origin_accept(tdma_pio_spi_ring_adapter_t *adapter,
                                           const tdma_origin_observation_t *observation)
{
    const uint32_t gap = observation->sequence - adapter->origin.returned.sequence - 1u;
    adapter->origin.unobserved_cycle_count = gap > UINT32_MAX - adapter->origin.unobserved_cycle_count
        ? UINT32_MAX : adapter->origin.unobserved_cycle_count + gap;
    adapter->origin.observed_return_count++;
    adapter->origin.returned = *observation;
    for (uint32_t i = 0u; i < TDMA_ORIGIN_PLAN_BANK_COUNT; ++i) {
        if (adapter->origin_shadow[i].generation == observation->local_generation) {
            adapter->origin.matched_owner_generation = adapter->origin_shadow[i].owner_generation;
            adapter->origin.matched_owner_sequence = adapter->origin_shadow[i].owner_sequence;
            break;
        }
    }
    /* This pair has a real decoded return and its bank's local identity.
     * It supplies transport progress, not SENT/ACK, a latch or a VDC epoch. */
    adapter->up_sequence = observation->sequence;
    adapter->up_tx_frame_crc32 = observation->identity;
}

static bool tdma_pio_spi_ring_origin_publish(tdma_pio_spi_ring_adapter_t *adapter)
{
    if (!adapter->phys_origin.ready(adapter->phys_ctrl_context) || adapter->flight_fifo == NULL)
        return true;
    tdma_flight_tx_view_t tx;
    if (!tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx) ||
        (adapter->origin.published_owner_generation == tx.generation &&
         adapter->origin.published_owner_sequence == tx.sequence)) return true;
    if (tx.generation == 0u || tx.segment_mask != (1u << adapter->config.local_slot_id)) return false;
    uint8_t payload[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];
    tdma_flight_engine_apply_t applied;
    tdma_flight_engine_result_t result;
    if (!tdma_flight_engine_tx_load(adapter->flight_engine,
            adapter->last_rx_packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE, sizeof(payload),
            &tx, payload, sizeof(payload), &applied, &result)) return false;
    /* The graph can replace exactly one fixed local mailbox. A changed map
     * must be rejected, never silently truncated into a smaller grant. */
    for (uint32_t word = 0u; word < TDMA_FLIGHT_OUTPUT_BITMAP_WORDS; ++word) {
        if (applied.output_byte_bitmap[word] !=
            (word == adapter->config.local_slot_id ? UINT32_MAX : 0u)) return false;
    }
    const uint8_t *mailbox = payload + adapter->config.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE;
    if (!tdma_pio_spi_ring_origin_mailbox_valid(mailbox, adapter->config.local_slot_id,
            (1u << adapter->config.node_count) - 1u)) return false;
    uint32_t generation = 0u;
    if (!adapter->phys_origin.publish(adapter->phys_ctrl_context, mailbox, &generation))
        return true; /* deferred: active FIFO view remains available for retry */
    if (generation == 0u || generation == adapter->origin.published_generation) return false;
    const uint32_t bank = adapter->origin_shadow[0].generation == adapter->origin.published_generation ? 1u : 0u;
    adapter->origin_shadow[bank].generation = generation;
    adapter->origin_shadow[bank].owner_generation = tx.generation;
    adapter->origin_shadow[bank].owner_sequence = tx.sequence;
    memcpy(adapter->origin_shadow[bank].mailbox, mailbox, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    adapter->origin.published_generation = generation;
    adapter->origin.published_owner_generation = tx.generation;
    adapter->origin.published_owner_sequence = tx.sequence;
    return true;
}

static bool tdma_pio_spi_ring_origin_service(tdma_pio_spi_ring_adapter_t *adapter, bool *rx_ok)
{
    if (adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_AUTONOMOUS ||
        !adapter->phys_origin.healthy(adapter->phys_ctrl_context)) {
        (void)tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
            TDMA_ADAPTER_COMM_EVENT_ERROR, TDMA_ADAPTER_COMM_ERROR_RUNTIME);
        tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
        return false;
    }
    *rx_ok = tdma_pio_spi_ring_adapter_rx_once(adapter);
    tdma_origin_observation_t observation;
    if (adapter->phys_origin.observe(adapter->phys_ctrl_context, &observation))
        adapter->origin.boundary = observation;
    if (!tdma_pio_spi_ring_origin_publish(adapter)) {
        tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
        /* Invalid/late software publication keeps the old hardware shadow. */
    }
    return true;
}
