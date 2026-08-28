#include "tdma_pio_spi_ring_adapter.h"

#include <string.h>

#include "tdma_process_image_layout.h"

static void tdma_pio_spi_ring_put_u32(uint8_t *dst, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        dst[i] = (uint8_t)(value >> (i * 8u));
    }
}

static uint32_t tdma_pio_spi_ring_get_u32(const uint8_t *src)
{
    uint32_t value = 0u;
    for (uint32_t i = 0u; i < 4u; i++) {
        value |= (uint32_t)src[i] << (i * 8u);
    }
    return value;
}

static bool tdma_pio_spi_ring_adapter_build_dpll_observation(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint8_t *payload,
    size_t payload_size)
{
    if (payload == NULL ||
        payload_size != TDMA_FLIGHT_SHORT_PAYLOAD_SIZE) {
        return false;
    }
    tdma_pio_spi_ring_put_u32(
        &payload[TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET], 0u);
    if (adapter == NULL || adapter->clock_evidence_enabled == 0u ||
        adapter->up_sequence == 0u ||
        adapter->timestamp_resolution_ns == 0u ||
        (adapter->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) == 0u ||
        (adapter->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u) {
        return false;
    }
    const uint32_t evidence_index = adapter->up_sequence %
        TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
    const uint32_t sequence =
        adapter->reference_tx_evidence[evidence_index].sequence;
    if (!adapter->reference_tx_evidence[evidence_index].valid ||
        sequence != adapter->up_sequence ||
        adapter->reference_tx_evidence[evidence_index].identity_crc32 == 0u ||
        adapter->reference_tx_evidence[evidence_index].timestamp_ns == 0ull) {
        return false;
    }

    const uint32_t encoded =
        tdma_process_image_dpll_observation_encode(
            adapter->reference_tx_evidence[evidence_index].timestamp_ns);
    tdma_pio_spi_ring_put_u32(
        &payload[TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET], encoded);
    return true;
}

static bool tdma_pio_spi_ring_adapter_correlate_dpll_observation(
    tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_transport_frame_view_t *view)
{
    if (adapter == NULL || view == NULL ||
        adapter->clock_evidence_enabled == 0u ||
        view->payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE ||
        (view->flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) == 0u ||
        view->payload_size != TDMA_FLIGHT_SHORT_PAYLOAD_SIZE ||
        view->payload == NULL ||
        view->transport_sequence <=
            TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SEQUENCE_LAG) {
        return false;
    }

    const uint32_t encoded = tdma_pio_spi_ring_get_u32(
        &view->payload[TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET]);
    if ((encoded & TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_VALID_MASK) == 0u) {
        return false;
    }
    const uint32_t sequence = view->transport_sequence -
        TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SEQUENCE_LAG;
    const uint32_t evidence_index = sequence %
        TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH;
    const bool local_eligible = adapter->timestamp_resolution_ns != 0u &&
        (adapter->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) != 0u &&
        (adapter->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u;
    if (!local_eligible ||
        !adapter->local_rx_evidence[evidence_index].valid ||
        adapter->local_rx_evidence[evidence_index].sequence != sequence ||
        adapter->local_rx_evidence[evidence_index].identity_crc32 == 0u ||
        adapter->local_rx_evidence[evidence_index].timestamp_ns == 0ull) {
        return false;
    }
    uint64_t reference_tx_timestamp_ns = 0ull;
    if (!tdma_process_image_dpll_observation_decode(
            encoded,
            adapter->local_rx_evidence[evidence_index].timestamp_ns,
            &reference_tx_timestamp_ns)) {
        return false;
    }

    tdma_ring_clock_observation_t observation;
    memset(&observation, 0, sizeof(observation));
    observation.valid = 1u;
    observation.node_count = adapter->config.node_count;
    observation.source_node = adapter->config.local_slot_id;
    observation.reference_node = view->origin_slot_id;
    observation.correlated_sequence = sequence;
    observation.frame_crc32 =
        adapter->local_rx_evidence[evidence_index].identity_crc32;
    observation.schedule_crc32 = view->schedule_crc32;
    observation.timestamp_resolution_ns =
        adapter->timestamp_resolution_ns >
                TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS
            ? adapter->timestamp_resolution_ns
            : TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS;
    observation.timestamp_flags =
        TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    observation.correlated_frame_evidence = 1u;
    observation.reference_tx_timestamp_ns = reference_tx_timestamp_ns;
    observation.local_rx_timestamp_ns =
        adapter->local_rx_evidence[evidence_index].timestamp_ns;
    adapter->clock_observation = observation;
    adapter->clock_observation_count++;
    adapter->local_rx_evidence[evidence_index].valid = false;
    return true;
}

static void tdma_pio_spi_ring_adapter_set_error(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t error)
{
    if (adapter != NULL) {
        adapter->last_error = error;
    }
}

static void tdma_pio_spi_ring_adapter_reset_bad_packet_diagnostic(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    adapter->last_bad_header_diff_count = 0u;
    adapter->last_bad_header_first_diff_offset = UINT32_MAX;
    adapter->last_bad_header_expected_byte = 0u;
    adapter->last_bad_header_observed_byte = 0u;
    adapter->last_bad_packet_diff_count = 0u;
    adapter->last_bad_packet_first_diff_offset = UINT32_MAX;
    adapter->last_bad_packet_expected_byte = 0u;
    adapter->last_bad_packet_observed_byte = 0u;
    adapter->last_bad_clock_evidence = 0u;
    adapter->last_bad_expected_transport_crc32 = 0u;
    adapter->last_bad_observed_transport_crc32 = 0u;
    adapter->last_bad_recomputed_transport_crc32 = 0u;
    adapter->last_bad_expected_payload_crc32 = 0u;
    adapter->last_bad_observed_payload_crc32 = 0u;
}

static bool tdma_pio_spi_ring_adapter_queue_push(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t timestamp_ns)
{
    if (adapter == NULL || packet == NULL || packet_size == 0u ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX) {
        return false;
    }
    if (adapter->rx_queue_count >= TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH) {
        adapter->rx_drop_count++;
        return false;
    }
    const uint32_t tail =
        (adapter->rx_queue_head + adapter->rx_queue_count) %
        TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH;
    memcpy(adapter->rx_queue[tail].packet, packet, packet_size);
    adapter->rx_queue[tail].packet_size = packet_size;
    adapter->rx_queue[tail].timestamp_ns = timestamp_ns;
    adapter->rx_queue[tail].valid = true;
    adapter->rx_queue_count++;
    return true;
}

static bool tdma_pio_spi_ring_adapter_queue_pop(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint8_t *packet,
    size_t packet_capacity,
    size_t *packet_size,
    uint64_t *timestamp_ns)
{
    if (packet_size != NULL) {
        *packet_size = 0u;
    }
    if (adapter == NULL || packet == NULL || packet_capacity == 0u ||
        packet_size == NULL || timestamp_ns == NULL ||
        adapter->rx_queue_count == 0u) {
        return false;
    }
    const uint32_t head = adapter->rx_queue_head;
    if (!adapter->rx_queue[head].valid ||
        adapter->rx_queue[head].packet_size > packet_capacity) {
        return false;
    }
    memcpy(packet,
           adapter->rx_queue[head].packet,
           adapter->rx_queue[head].packet_size);
    *packet_size = adapter->rx_queue[head].packet_size;
    *timestamp_ns = adapter->rx_queue[head].timestamp_ns;
    adapter->rx_queue[head].valid = false;
    adapter->rx_queue_head =
        (adapter->rx_queue_head + 1u) % TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH;
    adapter->rx_queue_count--;
    return true;
}

bool tdma_pio_spi_ring_adapter_init(tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter == NULL) {
        return false;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->clock_evidence_enabled = 1u;
    tdma_pio_spi_ring_adapter_reset_bad_packet_diagnostic(adapter);
    (void)tdma_receive_health_init(&adapter->receive_health);
    return true;
}

bool tdma_pio_spi_ring_adapter_set_calibration_topology(
    tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_ring_calibration_stage_t *stage)
{
    if (adapter == NULL || adapter->started != 0u || stage == NULL ||
        !tdma_ring_runtime_validate_calibration_stage(
            stage, stage->node_count, NULL)) {
        return false;
    }
    tdma_pio_spi_ring_topology_t topology;
    memset(&topology, 0, sizeof(topology));
    topology.valid = 1u;
    topology.node_count = stage->node_count;
    topology.topology_generation = stage->topology_generation;
    topology.topology_crc32 = stage->topology_crc32;
    for (uint32_t link_index = 0u; link_index < stage->node_count;
         link_index++) {
        const tdma_ring_calibration_link_t *link =
            &stage->links[link_index];
        topology.marker_next_node[link->marker_source_node] =
            link->marker_destination_node;
        topology.data_next_node[link->data_source_node] =
            link->data_destination_node;
    }
    adapter->topology = topology;
    adapter->topology_probe_mode = 0u;
    return true;
}

bool tdma_pio_spi_ring_adapter_set_topology_probe_mode(
    tdma_pio_spi_ring_adapter_t *adapter, bool enabled)
{
    if (adapter == NULL || adapter->started != 0u) {
        return false;
    }
    adapter->topology_probe_mode = enabled ? 1u : 0u;
    return true;
}

void tdma_pio_spi_ring_adapter_clear_calibration_topology(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter != NULL && adapter->started == 0u) {
        memset(&adapter->topology, 0, sizeof(adapter->topology));
    }
}

void tdma_pio_spi_ring_adapter_set_phys(tdma_pio_spi_ring_adapter_t *adapter,
                                        tdma_pio_spi_ring_tx_fn tx,
                                        tdma_pio_spi_ring_rx_fn rx,
                                        void *phys_context)
{
    if (adapter == NULL) {
        return;
    }
    adapter->phys_tx = tx;
    adapter->phys_rx = rx;
    adapter->phys_context = phys_context;
}

void tdma_pio_spi_ring_adapter_set_phys_feedback(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_feedback_fn feedback)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_feedback = feedback;
}

void tdma_pio_spi_ring_adapter_set_phys_ctrl(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_arm_fn arm,
    tdma_pio_spi_ring_phys_disarm_fn disarm,
    tdma_pio_spi_ring_phys_train_fn train,
    tdma_pio_spi_ring_phys_train_service_fn train_service,
    void *phys_ctrl_context)
{
    if (adapter == NULL) {
        return;
    }
    adapter->phys_arm = arm;
    adapter->phys_disarm = disarm;
    adapter->phys_train = train;
    adapter->phys_train_service = train_service;
    adapter->phys_ctrl_context = phys_ctrl_context;
}

void tdma_pio_spi_ring_adapter_set_phys_timestamp_ready(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_timestamp_ready_fn timestamp_ready)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_timestamp_ready = timestamp_ready;
}

void tdma_pio_spi_ring_adapter_set_phys_tx_complete(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_tx_complete_fn tx_complete)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_tx_complete = tx_complete;
}

void tdma_pio_spi_ring_adapter_set_phys_overlay(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_overlay_fn prepare_overlay,
    tdma_pio_spi_ring_phys_overlay_boundary_fn service_overlay_boundary)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_prepare_overlay = prepare_overlay;
    adapter->phys_service_overlay_boundary = service_overlay_boundary;
}

void tdma_pio_spi_ring_adapter_set_timestamp_metadata(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t resolution_ns,
    uint32_t flags)
{
    if (adapter == NULL) {
        return;
    }
    adapter->timestamp_resolution_ns = resolution_ns;
    adapter->timestamp_flags = flags;
}

static void tdma_pio_spi_ring_adapter_snapshot_write_begin(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    (void)__atomic_add_fetch(&adapter->snapshot_guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_pio_spi_ring_adapter_snapshot_write_end(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    (void)__atomic_add_fetch(&adapter->snapshot_guard, 1u, __ATOMIC_RELEASE);
}

void tdma_pio_spi_ring_adapter_set_flight_fifo(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_flight_fifo_t *fifo)
{
    if (adapter == NULL) {
        return;
    }
    adapter->flight_fifo = fifo;
}

void tdma_pio_spi_ring_adapter_set_flight_engine(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_flight_engine_t *engine)
{
    if (adapter == NULL) {
        return;
    }
    adapter->flight_engine = engine;
}

bool tdma_pio_spi_ring_adapter_set_forwarding_mode(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_forwarding_mode_t mode)
{
    if (adapter == NULL || adapter->started != 0u ||
        mode > TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE) {
        return false;
    }
    adapter->forwarding_mode = mode;
    return true;
}

bool tdma_pio_spi_ring_adapter_set_clock_evidence_enabled(
    tdma_pio_spi_ring_adapter_t *adapter, bool enabled)
{
    if (adapter == NULL || adapter->started != 0u) {
        return false;
    }
    adapter->clock_evidence_enabled = enabled ? 1u : 0u;
    return true;
}

bool tdma_pio_spi_ring_adapter_inject_rx(tdma_pio_spi_ring_adapter_t *adapter,
                                         const uint8_t *packet,
                                         size_t packet_size,
                                         uint64_t rx_timestamp_ns)
{
    return tdma_pio_spi_ring_adapter_queue_push(adapter,
                                                packet,
                                                packet_size,
                                                rx_timestamp_ns);
}

static uint32_t tdma_pio_spi_ring_adapter_expected_owner_mask(
    const tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_ring_runtime_config_t *config)
{
    if (adapter == NULL || config == NULL || config->node_count < 2u ||
        config->node_count > TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT ||
        config->local_slot_id >= config->node_count ||
        config->reference_slot_id >= config->node_count ||
        adapter->topology.valid == 0u ||
        adapter->topology.node_count != config->node_count) {
        return 0u;
    }
    const uint32_t active_mask = (1u << config->node_count) - 1u;
    if (config->local_slot_id == config->reference_slot_id) {
        return active_mask & ~(1u << config->local_slot_id);
    }
    uint32_t owner_mask = 0u;
    uint32_t node = config->reference_slot_id;
    for (uint32_t hop = 0u; hop < config->node_count; hop++) {
        if (node == config->local_slot_id) {
            break;
        }
        owner_mask |= 1u << node;
        node = adapter->topology.data_next_node[node];
        if (node >= config->node_count) {
            return 0u;
        }
    }
    return owner_mask;
}

static bool tdma_pio_spi_ring_adapter_start(
    void *context,
    const tdma_ring_runtime_config_t *config)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL) {
        return false;
    }
    tdma_pio_spi_ring_adapter_snapshot_write_begin(adapter);
    if (config == NULL || config->enabled == 0u ||
        config->node_count < 2u ||
        config->node_count > TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT ||
        config->local_slot_id >= config->node_count ||
        config->up_group_id == 0u || config->down_group_id == 0u ||
        config->up_group_id == config->down_group_id ||
        config->ring_profile_crc32 == 0u ||
        config->schedule_crc32 == 0u ||
        config->operating_profile_crc32 == 0u ||
        config->baud_hz < 1000000u || config->baud_hz > 50000000u ||
        config->cycle_period_ns == 0u ||
        config->feedback_timeout_ns == 0u) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT);
        tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
        return false;
    }
    adapter->config = *config;
    adapter->configured = true;
    adapter->role = (config->local_slot_id == config->reference_slot_id)
                        ? TDMA_PIO_SPI_RING_ROLE_REFERENCE
                        : TDMA_PIO_SPI_RING_ROLE_FORWARD;
    const bool flight_engine_enabled =
        adapter->forwarding_mode !=
        TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT;
    if (flight_engine_enabled && adapter->flight_engine != NULL &&
        tdma_flight_engine_is_configured(adapter->flight_engine) &&
        !tdma_flight_engine_activate(adapter->flight_engine,
                                     config->local_slot_id)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT);
        tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
        return false;
    }
    if (!flight_engine_enabled && adapter->flight_engine != NULL &&
        tdma_flight_engine_is_active(adapter->flight_engine)) {
        tdma_flight_engine_deactivate(adapter->flight_engine);
    }
    if (adapter->topology_probe_mode != 0u &&
        adapter->flight_engine != NULL &&
        tdma_flight_engine_is_active(adapter->flight_engine)) {
        tdma_flight_engine_deactivate(adapter->flight_engine);
        tdma_receive_health_reset_stopped(&adapter->receive_health);
    } else if (adapter->flight_engine != NULL &&
        tdma_flight_engine_is_active(adapter->flight_engine)) {
        tdma_flight_engine_snapshot_t engine_snapshot;
        uint32_t expected_segment_mask = 0u;
        const uint32_t expected_owner_mask =
            tdma_pio_spi_ring_adapter_expected_owner_mask(adapter, config);
        if (!tdma_flight_engine_get_snapshot(adapter->flight_engine,
                                              &engine_snapshot) ||
            !tdma_flight_engine_expected_input_mask(
                adapter->flight_engine,
                expected_owner_mask,
                &expected_segment_mask)) {
            tdma_flight_engine_deactivate(adapter->flight_engine);
            tdma_pio_spi_ring_adapter_set_error(
                adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
            tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
            return false;
        }
        const tdma_receive_health_config_t receive_config = {
            .schedule_crc32 = config->schedule_crc32,
            .ring_profile_crc32 = config->ring_profile_crc32,
            .map_generation = engine_snapshot.map_generation,
            .expected_payload_size = engine_snapshot.payload_size,
            .expected_segment_mask = expected_segment_mask,
            /* V1 is a diagnostic bring-up gate. Keep four complete cyclic
             * opportunities before declaring MISSING; product policy may
             * tighten this after the raw link and scheduler are frozen. */
            .stale_timeout_ns =
                (uint64_t)config->feedback_timeout_ns * 4ull,
        };
        if (!tdma_receive_health_configure_stopped(&adapter->receive_health,
                                                   &receive_config)) {
            tdma_flight_engine_deactivate(adapter->flight_engine);
            tdma_pio_spi_ring_adapter_set_error(
                adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
            tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
            return false;
        }
    } else {
        tdma_receive_health_reset_stopped(&adapter->receive_health);
    }
    /* A metadata declaration made during boot is not proof that the PIO
     * timestamp spine is armed.  Once a readiness probe is installed, keep
     * the adapter fail-closed until the physical arm has established the
     * latch.  Test/alternate physical layers without a probe retain their
     * explicitly supplied metadata for backwards compatibility. */
    if (adapter->phys_timestamp_ready != NULL) {
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            adapter, 0u, TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
    }
    if (adapter->phys_arm != NULL &&
        !adapter->phys_arm(adapter->phys_ctrl_context, config)) {
        if (adapter->phys_disarm != NULL) {
            adapter->phys_disarm(adapter->phys_ctrl_context);
        }
        tdma_flight_engine_deactivate(adapter->flight_engine);
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_MISSING);
        tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
        return false;
    }
    if (adapter->phys_timestamp_ready != NULL) {
        uint32_t resolution_ns = 0u;
        uint32_t flags = TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
        if (adapter->phys_timestamp_ready(adapter->phys_ctrl_context,
                                          &resolution_ns,
                                          &flags) &&
            resolution_ns != 0u &&
            (flags & TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) != 0u &&
            (flags & TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u) {
            tdma_pio_spi_ring_adapter_set_timestamp_metadata(
                adapter, resolution_ns, flags);
        }
    }
    adapter->started = 1u;
    /* These counters describe one armed ring session.  Keeping values from a
     * previous topology makes a follower that has not received any frame look
     * as if it is still forwarding traffic, and invalidates START readback. */
    adapter->forward_count = 0u;
    adapter->idle_beacon_tx_count = 0u;
    adapter->idle_beacon_rx_count = 0u;
    adapter->tx_count = 0u;
    adapter->rx_count = 0u;
    adapter->rx_bad_count = 0u;
    adapter->rx_transport_bad_count = 0u;
    adapter->rx_schedule_bad_count = 0u;
    adapter->rx_profile_bad_count = 0u;
    adapter->last_bad_transport_result = TDMA_TRANSPORT_OK;
    adapter->last_bad_sequence = 0u;
    adapter->last_bad_schedule_crc32 = 0u;
    adapter->last_bad_profile_crc32 = 0u;
    tdma_pio_spi_ring_adapter_reset_bad_packet_diagnostic(adapter);
    adapter->rx_drop_count = 0u;
    adapter->up_sequence = 0u;
    adapter->down_rx_sequence = 0u;
    adapter->up_tx_frame_crc32 = 0u;
    adapter->down_rx_frame_crc32 = 0u;
    adapter->feedback_reference_sequence = 0u;
    adapter->feedback_reference_frame_crc32 = 0u;
    adapter->feedback_timestamp_resolution_ns = 0u;
    adapter->feedback_timestamp_flags = 0u;
    adapter->reference_tx_timestamp_ns = 0ull;
    adapter->rx_ready_timestamp_ns = 0ull;
    adapter->pending_tx_evidence_sequence = 0u;
    adapter->pending_tx_evidence_identity_crc32 = 0u;
    adapter->pending_tx_evidence = false;
    adapter->feedback_rx_timestamp_ns = 0ull;
    adapter->last_rx_service_ns = 0ull;
    adapter->last_service_ns = 0ull;
    adapter->last_rx_packet_size = 0u;
    adapter->rx_queue_head = 0u;
    adapter->rx_queue_count = 0u;
    memset(adapter->reference_tx_evidence,
           0,
           sizeof(adapter->reference_tx_evidence));
    memset(adapter->local_rx_evidence, 0, sizeof(adapter->local_rx_evidence));
    memset(&adapter->clock_observation, 0, sizeof(adapter->clock_observation));
    adapter->clock_observation_count = 0u;
    adapter->clock_observation_reject_count = 0u;
    adapter->next_tx_deadline_ns = 0ull;
    adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
    return true;
}

static void tdma_pio_spi_ring_adapter_stop(void *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL) {
        return;
    }
    tdma_pio_spi_ring_adapter_snapshot_write_begin(adapter);
    if (adapter->started != 0u && adapter->phys_disarm != NULL) {
        adapter->phys_disarm(adapter->phys_ctrl_context);
    }
    if (adapter->phys_timestamp_ready != NULL) {
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            adapter, 0u, TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
    }
    tdma_flight_engine_deactivate(adapter->flight_engine);
    adapter->started = 0u;
    adapter->up_sequence = 0u;
    adapter->down_rx_sequence = 0u;
    adapter->up_tx_frame_crc32 = 0u;
    adapter->down_rx_frame_crc32 = 0u;
    adapter->feedback_reference_sequence = 0u;
    adapter->feedback_reference_frame_crc32 = 0u;
    adapter->feedback_timestamp_resolution_ns = 0u;
    adapter->feedback_timestamp_flags = 0u;
    adapter->reference_tx_timestamp_ns = 0ull;
    adapter->rx_ready_timestamp_ns = 0ull;
    adapter->pending_tx_evidence_sequence = 0u;
    adapter->pending_tx_evidence_identity_crc32 = 0u;
    adapter->pending_tx_evidence = false;
    adapter->feedback_rx_timestamp_ns = 0ull;
    adapter->last_rx_service_ns = 0ull;
    adapter->last_service_ns = 0ull;
    adapter->last_rx_packet_size = 0u;
    adapter->rx_queue_head = 0u;
    adapter->rx_queue_count = 0u;
    tdma_receive_health_reset_stopped(&adapter->receive_health);
    memset(adapter->reference_tx_evidence,
           0,
           sizeof(adapter->reference_tx_evidence));
    memset(adapter->local_rx_evidence, 0, sizeof(adapter->local_rx_evidence));
    memset(&adapter->clock_observation, 0, sizeof(adapter->clock_observation));
    tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
}

static bool tdma_pio_spi_ring_adapter_tx_beacon(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    tdma_flight_tx_view_t tx_view;
    memset(&tx_view, 0, sizeof(tx_view));
    const bool has_flight_tx =
        adapter->flight_fifo != NULL &&
        tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx_view);
    const uint32_t sequence = adapter->up_sequence + 1u;
    uint8_t empty_process_payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
    uint8_t process_payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
    uint8_t alignment_payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
    tdma_flight_engine_fill_alignment_symbols(alignment_payload,
                                               sizeof(alignment_payload));
    size_t fixed_flight_payload_size = sizeof(alignment_payload);
    if (adapter->flight_engine != NULL) {
        tdma_flight_engine_snapshot_t engine_snapshot;
        if (tdma_flight_engine_get_snapshot(adapter->flight_engine,
                                             &engine_snapshot) &&
            engine_snapshot.payload_size != 0u &&
            engine_snapshot.payload_size <= sizeof(alignment_payload)) {
            fixed_flight_payload_size = engine_snapshot.payload_size;
        }
    }
    const bool product_process_image =
        adapter->forwarding_mode ==
            TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE &&
        adapter->flight_engine != NULL &&
        tdma_flight_engine_is_active(adapter->flight_engine);
    /* Product RUN has one wire shape.  DPLL evidence is a fixed trailer in
     * that process image; it never replaces a cycle with an IDLE frame. */
    const bool emit_process_image = has_flight_tx || product_process_image;
    const uint8_t *wire_payload = emit_process_image ? tx_view.data : NULL;
    size_t wire_payload_size = emit_process_image ? tx_view.data_size : 0u;
    bool dpll_observation_ready = false;
    if (!emit_process_image &&
        (adapter->forwarding_mode ==
             TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT ||
         adapter->forwarding_mode ==
             TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE)) {
        /* Flight PIO consumes a fixed physical byte count configured at ARM.
         * An empty IDLE_BEACON would make every follower wait forever for
         * absent symbols after the first frame. Keep the frame full even
         * before clock evidence or a process-image mailbox is available. */
        wire_payload = alignment_payload;
        wire_payload_size = fixed_flight_payload_size;
    }
    if (product_process_image) {
        tdma_flight_engine_snapshot_t engine_snapshot;
        tdma_flight_engine_apply_t applied;
        tdma_flight_engine_result_t engine_result =
            TDMA_FLIGHT_ENGINE_BAD_ARGUMENT;
        if (!tdma_flight_engine_get_snapshot(adapter->flight_engine,
                                             &engine_snapshot) ||
            engine_snapshot.payload_size == 0u ||
            engine_snapshot.payload_size > sizeof(process_payload)) {
            tdma_pio_spi_ring_adapter_set_error(
                adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
            return false;
        }
        /* Seed every physical DATA symbol with the same transition-rich
         * alignment codeword. Each node then overlays only its owned mailbox.
         * A zero-filled image would keep the frame long enough for PIO, but
         * would provide no useful edges for raw-loop alignment analysis. */
        tdma_flight_engine_fill_alignment_symbols(
            empty_process_payload, engine_snapshot.payload_size);
        if (!tdma_flight_engine_apply_preclassified(
                adapter->flight_engine,
                empty_process_payload,
                engine_snapshot.payload_size,
                0u,
                has_flight_tx ? &tx_view : NULL,
                process_payload,
                sizeof(process_payload),
                &applied,
                &engine_result)) {
            tdma_pio_spi_ring_adapter_set_error(
                adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
            return false;
        }
        dpll_observation_ready =
            tdma_pio_spi_ring_adapter_build_dpll_observation(
                adapter, process_payload, engine_snapshot.payload_size);
        wire_payload = process_payload;
        wire_payload_size = engine_snapshot.payload_size;
    }
    const tdma_transport_frame_build_t build = {
        .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id = adapter->config.local_slot_id,
        .transport_sequence = sequence,
        .payload_class = emit_process_image
                             ? TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE
                             : TDMA_PAYLOAD_CLASS_IDLE_BEACON,
        .flags = emit_process_image
                     ? (TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK |
                        TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE)
                     : TDMA_TRANSPORT_FLAG_IDLE_BEACON,
        .schedule_crc32 = adapter->config.schedule_crc32,
        .ring_profile_crc32 = adapter->config.ring_profile_crc32,
        .hop_limit = adapter->config.node_count - 1u,
        .payload = wire_payload,
        .payload_size = wire_payload_size,
    };
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t packet_size = 0u;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_encode(&build,
                                     packet,
                                     sizeof(packet),
                                     &packet_size,
                                     &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    uint64_t tx_timestamp_ns = 0ull;
    if (!adapter->phys_tx(adapter->phys_context,
                          packet,
                          packet_size,
                          &tx_timestamp_ns)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    adapter->up_sequence = sequence;
    adapter->tx_count++;
    adapter->idle_beacon_tx_count++;
    if (tx_timestamp_ns != 0ull && adapter->config.loop_delay_ns != 0u) {
        const uint32_t tolerance = adapter->config.loop_delay_tolerance_ns;
        const uint32_t lower_bound =
            adapter->config.loop_delay_ns > tolerance
                ? adapter->config.loop_delay_ns - tolerance
                : 0u;
        adapter->rx_ready_timestamp_ns =
            UINT64_MAX - tx_timestamp_ns < (uint64_t)lower_bound
                ? UINT64_MAX
                : tx_timestamp_ns + (uint64_t)lower_bound;
    } else {
        adapter->rx_ready_timestamp_ns = 0ull;
    }

    tdma_transport_frame_view_t view;
    if (tdma_transport_frame_decode(packet,
                                    packet_size,
                                    &view,
                                    &result)) {
        adapter->up_tx_frame_crc32 = view.identity_crc32;
        const uint32_t evidence_index =
            sequence % TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
        adapter->reference_tx_evidence[evidence_index].sequence = sequence;
        adapter->reference_tx_evidence[evidence_index].identity_crc32 =
            view.identity_crc32;
        adapter->reference_tx_evidence[evidence_index].timestamp_ns =
            tx_timestamp_ns;
        memcpy(adapter->reference_tx_evidence[evidence_index].packet,
               packet,
               packet_size);
        adapter->reference_tx_evidence[evidence_index].packet_size = packet_size;
        adapter->reference_tx_evidence[evidence_index].clock_evidence =
            dpll_observation_ready;
        adapter->reference_tx_evidence[evidence_index].valid =
            tx_timestamp_ns != 0ull;
        if (tx_timestamp_ns == 0ull && adapter->phys_tx_complete != NULL) {
            adapter->pending_tx_evidence_sequence = sequence;
            adapter->pending_tx_evidence_identity_crc32 =
                view.identity_crc32;
            adapter->pending_tx_evidence = true;
        }
    }
    return true;
}

static bool tdma_pio_spi_ring_adapter_process_rx(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t rx_timestamp_ns)
{
    adapter->last_rx_gate_accepted = false;
    adapter->last_rx_new_segment_mask = 0u;
    /* Receive-health age belongs to the adapter service clock domain. The
     * physical RX timestamp is a separate hardware-latch domain used for
     * RTT/FIFO evidence and cannot be subtracted from service now_ns. */
    const uint64_t health_observation_ns = adapter->last_service_ns != 0ull
        ? adapter->last_service_ns
        : rx_timestamp_ns;
    uint32_t hardware_round_trip_ns = 0u;
    uint32_t hardware_resolution_ns = 0u;
    uint32_t hardware_flags = 0u;
    const bool hardware_round_trip_valid =
        adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
        adapter->phys_feedback != NULL &&
        adapter->phys_feedback(adapter->phys_context,
                               &hardware_round_trip_ns,
                               &hardware_resolution_ns,
                               &hardware_flags);
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    const bool decoded = tdma_transport_frame_decode(packet,
                                                     packet_size,
                                                     &view,
                                                     &result);
    const bool schedule_bad = decoded &&
        view.schedule_crc32 != adapter->config.schedule_crc32;
    const bool profile_bad = decoded && !schedule_bad &&
        view.ring_profile_crc32 != adapter->config.ring_profile_crc32;
    if (!decoded || schedule_bad || profile_bad) {
        if (adapter->receive_health.configured != 0u) {
            tdma_receive_reason_t receive_reason = TDMA_RECEIVE_REASON_NONE;
            (void)tdma_receive_health_evaluate(
                &adapter->receive_health,
                decoded ? &view : NULL,
                result,
                0u,
                health_observation_ns,
                &receive_reason);
        }
        adapter->rx_bad_count++;
        if (!decoded) {
            adapter->rx_transport_bad_count++;
        } else if (schedule_bad) {
            adapter->rx_schedule_bad_count++;
        } else {
            adapter->rx_profile_bad_count++;
        }
        /* Decode deliberately zeroes view before inspecting the packet, so
         * early header failures publish zero identity fields while CRC-stage
         * failures retain the observed sequence and frozen identities. */
        adapter->last_bad_transport_result = (uint32_t)result;
        adapter->last_bad_sequence = view.transport_sequence;
        adapter->last_bad_schedule_crc32 = view.schedule_crc32;
        adapter->last_bad_profile_crc32 = view.ring_profile_crc32;
        tdma_pio_spi_ring_adapter_reset_bad_packet_diagnostic(adapter);
        if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
            packet_size >= TDMA_TRANSPORT_FRAME_HEADER_SIZE &&
            view.transport_sequence != 0u) {
            const uint32_t evidence_index =
                view.transport_sequence %
                TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
            if (adapter->reference_tx_evidence[evidence_index].valid &&
                adapter->reference_tx_evidence[evidence_index].sequence ==
                    view.transport_sequence) {
                const size_t expected_size =
                    adapter->reference_tx_evidence[evidence_index].packet_size;
                const size_t common_size = expected_size < packet_size
                    ? expected_size : packet_size;
                for (size_t i = 0u; i < common_size; i++) {
                    const uint8_t expected =
                        adapter->reference_tx_evidence[evidence_index].packet[i];
                    const uint8_t observed = packet[i];
                    if (expected == observed) {
                        continue;
                    }
                    if (adapter->last_bad_packet_diff_count == 0u) {
                        adapter->last_bad_packet_first_diff_offset = (uint32_t)i;
                        adapter->last_bad_packet_expected_byte = expected;
                        adapter->last_bad_packet_observed_byte = observed;
                    }
                    adapter->last_bad_packet_diff_count++;
                    if (i < TDMA_TRANSPORT_FRAME_HEADER_SIZE) {
                        if (adapter->last_bad_header_diff_count == 0u) {
                            adapter->last_bad_header_first_diff_offset =
                                (uint32_t)i;
                            adapter->last_bad_header_expected_byte = expected;
                            adapter->last_bad_header_observed_byte = observed;
                        }
                        adapter->last_bad_header_diff_count++;
                    }
                }
                if (expected_size != packet_size) {
                    const size_t size_diff = expected_size > packet_size
                        ? expected_size - packet_size
                        : packet_size - expected_size;
                    if (adapter->last_bad_packet_diff_count == 0u) {
                        adapter->last_bad_packet_first_diff_offset =
                            (uint32_t)common_size;
                    }
                    adapter->last_bad_packet_diff_count += (uint32_t)size_diff;
                }
                adapter->last_bad_clock_evidence =
                    adapter->reference_tx_evidence[evidence_index].clock_evidence
                        ? 1u : 0u;
                tdma_transport_frame_view_t expected_view;
                tdma_transport_result_t expected_result = TDMA_TRANSPORT_OK;
                if (tdma_transport_frame_decode(
                        adapter->reference_tx_evidence[evidence_index].packet,
                        expected_size, &expected_view, &expected_result)) {
                    adapter->last_bad_expected_transport_crc32 =
                        expected_view.transport_crc32;
                    adapter->last_bad_expected_payload_crc32 =
                        tdma_transport_crc32_compute(expected_view.payload,
                                                     expected_view.payload_size);
                }
                adapter->last_bad_observed_transport_crc32 =
                    view.transport_crc32;
                adapter->last_bad_observed_payload_crc32 =
                    tdma_transport_crc32_compute(view.payload, view.payload_size);
                (void)tdma_transport_frame_calculate_transport_crc32(
                    packet, packet_size,
                    &adapter->last_bad_recomputed_transport_crc32);
            }
        }
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME);
        return false;
    }

    bool receive_health_rejected = false;
    uint32_t inspected_new_mask = 0u;
    if ((!receive_health_rejected ||
         adapter->receive_health.configured == 0u) &&
        view.payload_class == TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
        adapter->receive_health.configured != 0u) {
        uint32_t present_mask = 0u;
        uint32_t expected_mask = 0u;
        const uint32_t expected_owner_mask =
            tdma_pio_spi_ring_adapter_expected_owner_mask(
                adapter, &adapter->config);
        const bool inspected = tdma_flight_engine_inspect_input(
            adapter->flight_engine,
            view.payload,
            view.payload_size,
            expected_owner_mask,
            &present_mask,
            &inspected_new_mask,
            &expected_mask);
        tdma_receive_reason_t receive_reason = TDMA_RECEIVE_REASON_NONE;
        const bool receive_health_accepted = tdma_receive_health_evaluate(
            &adapter->receive_health,
            &view,
            result,
            present_mask,
            health_observation_ns,
            &receive_reason);
        adapter->last_rx_gate_accepted =
            inspected && receive_health_accepted;
        adapter->last_rx_new_segment_mask = inspected_new_mask;
        receive_health_rejected = !adapter->last_rx_gate_accepted;
        (void)expected_mask;
        if (receive_health_rejected) {
            tdma_pio_spi_ring_adapter_set_error(
                adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_GATE_REJECT);
        }
    }

    adapter->down_rx_sequence = view.transport_sequence;
    adapter->down_rx_frame_crc32 = view.identity_crc32;
    adapter->feedback_rx_timestamp_ns = rx_timestamp_ns;
    if (adapter->role == TDMA_PIO_SPI_RING_ROLE_FORWARD &&
        rx_timestamp_ns != 0ull && view.transport_sequence != 0u &&
        view.identity_crc32 != 0u) {
        const uint32_t evidence_index = view.transport_sequence %
            TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH;
        adapter->local_rx_evidence[evidence_index].sequence =
            view.transport_sequence;
        adapter->local_rx_evidence[evidence_index].identity_crc32 =
            view.identity_crc32;
        adapter->local_rx_evidence[evidence_index].timestamp_ns =
            rx_timestamp_ns;
        adapter->local_rx_evidence[evidence_index].valid = true;

        if (adapter->clock_evidence_enabled != 0u &&
            view.payload_class ==
                TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
            view.payload_size == TDMA_FLIGHT_SHORT_PAYLOAD_SIZE &&
            view.payload != NULL) {
            const uint32_t encoded = tdma_pio_spi_ring_get_u32(
                &view.payload[
                    TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET]);
            if ((encoded &
                 TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_VALID_MASK) != 0u &&
                !tdma_pio_spi_ring_adapter_correlate_dpll_observation(
                    adapter, &view)) {
                adapter->clock_observation_reject_count++;
            }
        }
    }
    if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) {
        /* The current RX commonly trails the latest TX by one frame in the
         * cut-through ring.  Select the physical TX latch by transport
         * sequence instead of pairing RX with the latest TX opportunistically. */
        adapter->feedback_reference_sequence = 0u;
        adapter->feedback_reference_frame_crc32 = 0u;
        adapter->feedback_timestamp_resolution_ns = 0u;
        adapter->feedback_timestamp_flags = 0u;
        adapter->reference_tx_timestamp_ns = 0ull;
        const uint32_t evidence_index =
            view.transport_sequence %
            TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
        if (adapter->reference_tx_evidence[evidence_index].valid &&
            adapter->reference_tx_evidence[evidence_index].sequence ==
                view.transport_sequence &&
            adapter->reference_tx_evidence[evidence_index].identity_crc32 ==
                view.identity_crc32) {
            const uint64_t matched_tx_timestamp_ns =
                adapter->reference_tx_evidence[evidence_index].timestamp_ns;
            if (hardware_round_trip_valid && hardware_round_trip_ns != 0u &&
                hardware_resolution_ns != 0u &&
                (hardware_flags &
                 TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) != 0u &&
                (hardware_flags &
                 TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u &&
                UINT64_MAX - matched_tx_timestamp_ns >=
                    (uint64_t)hardware_round_trip_ns) {
                adapter->feedback_reference_sequence =
                    view.transport_sequence;
                adapter->feedback_reference_frame_crc32 = view.identity_crc32;
                adapter->feedback_timestamp_resolution_ns =
                    hardware_resolution_ns;
                adapter->feedback_timestamp_flags = hardware_flags;
                adapter->reference_tx_timestamp_ns = matched_tx_timestamp_ns;
                adapter->feedback_rx_timestamp_ns = matched_tx_timestamp_ns +
                    (uint64_t)hardware_round_trip_ns;
            } else if (adapter->phys_feedback == NULL &&
                       adapter->feedback_rx_timestamp_ns >=
                           matched_tx_timestamp_ns &&
                       adapter->timestamp_resolution_ns != 0u &&
                       (adapter->timestamp_flags &
                        TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) != 0u &&
                       (adapter->timestamp_flags &
                        TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u) {
                /* Host/alternate physical layers may already return a pair
                 * of absolute hardware latches through phys_tx/phys_rx. */
                adapter->feedback_reference_sequence =
                    view.transport_sequence;
                adapter->feedback_reference_frame_crc32 = view.identity_crc32;
                adapter->feedback_timestamp_resolution_ns =
                    adapter->timestamp_resolution_ns;
                adapter->feedback_timestamp_flags = adapter->timestamp_flags;
                adapter->reference_tx_timestamp_ns = matched_tx_timestamp_ns;
            }
            /* Keep the reference TX record alive after feedback correlation.
             * The next clock-evidence beacon is intentionally one frame
             * behind the TX sequence (and is selected by exact sequence), so
             * clearing the record here can race the payload builder: on a
             * short ring the feedback for sequence N commonly arrives before
             * sequence N+1 is emitted.  The bounded evidence ring replaces a
             * record only when the same modulo index is reused; exact
             * sequence matching still makes an overwritten/stale record
             * fail closed. */
        }
    }
    adapter->rx_count++;
    if ((view.flags & TDMA_TRANSPORT_FLAG_IDLE_BEACON) != 0u) {
        adapter->idle_beacon_rx_count++;
    }
    if (view.payload_class == TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
        adapter->flight_fifo != NULL &&
        (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE ||
         adapter->forwarding_mode ==
             TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT ||
         adapter->forwarding_mode ==
             TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE ||
         adapter->flight_engine == NULL ||
         !tdma_flight_engine_is_active(adapter->flight_engine))) {
        uint32_t input_mask = inspected_new_mask;
        if (adapter->receive_health.configured == 0u &&
            adapter->flight_engine != NULL &&
            tdma_flight_engine_is_active(adapter->flight_engine)) {
            (void)tdma_flight_engine_classify_input(adapter->flight_engine,
                                                    view.payload,
                                                    view.payload_size,
                                                    &input_mask);
        }
        if ((!receive_health_rejected ||
             adapter->receive_health.configured == 0u) &&
            (input_mask != 0u ||
             adapter->flight_engine == NULL ||
             !tdma_flight_engine_is_active(adapter->flight_engine))) {
            const uint32_t rx_generation =
                adapter->receive_health.configured != 0u
                    ? adapter->receive_health.config.map_generation
                    : view.transport_sequence;
            const uint32_t rx_quality =
                adapter->timestamp_flags |
                (adapter->receive_health.configured != 0u
                     ? adapter->receive_health.quality_flags
                     : 0u);
            const bool published = tdma_flight_fifo_core1_publish_rx(
                adapter->flight_fifo,
                view.payload,
                view.payload_size,
                rx_generation,
                view.transport_sequence,
                input_mask,
                rx_timestamp_ns,
                rx_quality);
            if (published && input_mask != 0u &&
                adapter->flight_engine != NULL &&
                tdma_flight_engine_is_active(adapter->flight_engine)) {
                (void)tdma_flight_engine_commit_input(
                    adapter->flight_engine,
                    view.payload,
                    view.payload_size,
                    input_mask);
            }
        }
    }
    if (packet_size <= sizeof(adapter->last_rx_packet)) {
        memcpy(adapter->last_rx_packet, packet, packet_size);
        adapter->last_rx_packet_size = packet_size;
    }
    if (!receive_health_rejected) {
        adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    }
    return true;
}

/* FORWARD-node emission: re-emit the frame received from the previous board
 * toward the next board, keeping origin/sequence/identity CRC unchanged and
 * advancing hop/transport CRC. The feedback returns only to the reference
 * node, so this path proves ring service (up/down running) without
 * fabricating simultaneous_feedback_loop_evidence. */
static bool tdma_pio_spi_ring_adapter_tx_forward(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter->last_rx_packet_size == 0u) {
        return false;
    }
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    memcpy(packet, adapter->last_rx_packet, adapter->last_rx_packet_size);
    const size_t packet_size = adapter->last_rx_packet_size;
    bool flight_map_rejected = false;

    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    tdma_transport_frame_view_t incoming_view;
    if (!tdma_transport_frame_decode(adapter->last_rx_packet,
                                     packet_size,
                                     &incoming_view,
                                     &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME);
        return false;
    }
    if (adapter->flight_engine != NULL &&
        tdma_flight_engine_is_active(adapter->flight_engine) &&
        incoming_view.payload_class ==
            TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
        (incoming_view.flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) != 0u) {
        tdma_flight_tx_view_t tx_view;
        const bool has_tx = adapter->flight_fifo != NULL &&
            tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx_view);
        uint8_t processed_payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
        tdma_flight_engine_apply_t applied;
        tdma_flight_engine_result_t engine_result =
            TDMA_FLIGHT_ENGINE_BAD_ARGUMENT;
        if (tdma_flight_engine_apply_preclassified(
                adapter->flight_engine,
                incoming_view.payload,
                incoming_view.payload_size,
                adapter->last_rx_new_segment_mask,
                has_tx ? &tx_view : NULL,
                processed_payload,
                sizeof(processed_payload),
                &applied,
                &engine_result)) {
            if (!tdma_transport_frame_patch_flight_payload(
                    packet,
                    packet_size,
                    0u,
                    processed_payload,
                    incoming_view.payload_size,
                    &result)) {
                tdma_pio_spi_ring_adapter_set_error(
                    adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
                return false;
            }
            if (adapter->flight_fifo != NULL &&
                (adapter->receive_health.configured == 0u ||
                 adapter->last_rx_gate_accepted) &&
                applied.input_segment_mask != 0u) {
                const uint32_t rx_generation =
                    adapter->receive_health.configured != 0u
                        ? adapter->receive_health.config.map_generation
                        : (has_tx ? tx_view.generation
                                  : incoming_view.transport_sequence);
                const uint32_t rx_quality =
                    adapter->timestamp_flags |
                    (adapter->receive_health.configured != 0u
                         ? adapter->receive_health.quality_flags
                         : 0u);
                const bool published = tdma_flight_fifo_core1_publish_rx(
                    adapter->flight_fifo,
                    incoming_view.payload,
                    incoming_view.payload_size,
                    rx_generation,
                    incoming_view.transport_sequence,
                    applied.input_segment_mask,
                    adapter->feedback_rx_timestamp_ns,
                    rx_quality);
                if (published) {
                    (void)tdma_flight_engine_commit_input(
                        adapter->flight_engine,
                        incoming_view.payload,
                        incoming_view.payload_size,
                        applied.input_segment_mask);
                }
            }
        } else {
            /* Keep the transport path nonblocking, but expose the missed
             * local process-image exchange as adapter evidence. */
            flight_map_rejected = true;
            tdma_pio_spi_ring_adapter_set_error(
                adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
        }
    }
    if (!tdma_transport_frame_advance_hop(packet, packet_size, &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    uint64_t tx_timestamp_ns = 0ull;
    if (!adapter->phys_tx(adapter->phys_context,
                          packet,
                          packet_size,
                          &tx_timestamp_ns)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    tdma_transport_frame_view_t view;
    if (tdma_transport_frame_decode(packet,
                                    packet_size,
                                    &view,
                                    &result)) {
        adapter->up_sequence = view.transport_sequence;
        adapter->up_tx_frame_crc32 = view.identity_crc32;
    }
    adapter->forward_count++;
    adapter->tx_count++;
    adapter->last_error = flight_map_rejected
                              ? TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT
                              : TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    return true;
}

static bool tdma_pio_spi_ring_adapter_rx_once(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t packet_size = 0u;
    uint64_t rx_timestamp_ns = 0ull;

    if (adapter->rx_queue_count != 0u) {
        if (!tdma_pio_spi_ring_adapter_queue_pop(adapter,
                                                 packet,
                                                 sizeof(packet),
                                                 &packet_size,
                                                 &rx_timestamp_ns)) {
            return false;
        }
        return tdma_pio_spi_ring_adapter_process_rx(adapter,
                                                    packet,
                                                    packet_size,
                                                    rx_timestamp_ns);
    }

    if (adapter->phys_rx == NULL) {
        return false;
    }
    if (!adapter->phys_rx(adapter->phys_context,
                          packet,
                          sizeof(packet),
                          &packet_size,
                          &rx_timestamp_ns)) {
        return false;
    }
    return tdma_pio_spi_ring_adapter_process_rx(adapter,
                                                packet,
                                                packet_size,
                                                rx_timestamp_ns);
}

/* Poll the uplink capture up to RX_POLLS times per service: at 4x the frame
 * rate the DMA re-arm follows frame arrival within a fraction of a frame
 * interval, shrinking the inter-frame noise window and removing phase
 * sensitivity to the free-running core1 tick. */
#define TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS 4u

static bool tdma_pio_spi_ring_adapter_rx_poll(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint64_t now_ns)
{
    if (adapter == NULL) {
        return false;
    }
    /* The DMA/PIO capture remains armed continuously.  Only reference-node
     * feedback is held until the calibrated loop-delay lower bound; forward
     * nodes must consume RX immediately so cut-through forwarding is not
     * delayed by the full-ring measurement. */
    if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
        adapter->rx_ready_timestamp_ns != 0ull &&
        now_ns < adapter->rx_ready_timestamp_ns) {
        return false;
    }
    bool rx_ok = false;
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS; i++) {
        if (tdma_pio_spi_ring_adapter_rx_once(adapter)) {
            rx_ok = true;
        }
    }
    if (rx_ok && adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) {
        adapter->rx_ready_timestamp_ns = 0ull;
    }
    return rx_ok;
}

static bool tdma_pio_spi_ring_adapter_train_clock(void *context,
                                                  uint32_t cycles)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL || adapter->started == 0u ||
        adapter->phys_train == NULL || cycles == 0u) {
        return false;
    }
    return adapter->phys_train(adapter->phys_ctrl_context, cycles);
}

static void tdma_pio_spi_ring_adapter_train_clock_service(void *context,
                                                          uint64_t now_ns)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL || adapter->started == 0u ||
        adapter->phys_train_service == NULL) {
        return;
    }
    adapter->phys_train_service(adapter->phys_ctrl_context, now_ns);
}

static bool tdma_pio_spi_ring_adapter_forward_poll(
    tdma_pio_spi_ring_adapter_t *adapter,
    bool *rx_ok)
{
    bool any_rx = false;
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS; i++) {
        if (!tdma_pio_spi_ring_adapter_rx_once(adapter)) {
            continue;
        }
        any_rx = true;
        if (!tdma_pio_spi_ring_adapter_tx_forward(adapter)) {
            if (rx_ok != NULL) {
                *rx_ok = any_rx;
            }
            return false;
        }
    }
    if (rx_ok != NULL) {
        *rx_ok = any_rx;
    }
    return true;
}

static bool tdma_pio_spi_ring_adapter_prepare_process_overlay(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter == NULL || adapter->last_rx_packet_size == 0u ||
        adapter->phys_prepare_overlay == NULL) {
        return false;
    }
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_decode(adapter->last_rx_packet,
                                     adapter->last_rx_packet_size,
                                     &view,
                                     &result)) {
        return false;
    }

    uint8_t processed_packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    memcpy(processed_packet,
           adapter->last_rx_packet,
           adapter->last_rx_packet_size);
    bool applied_ok = false;
    if (adapter->flight_engine != NULL &&
        tdma_flight_engine_is_active(adapter->flight_engine) &&
        view.payload_class == TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
        (view.flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) != 0u) {
        tdma_flight_tx_view_t tx_view;
        const bool has_tx = adapter->flight_fifo != NULL &&
            tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx_view);
        uint8_t processed_payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
        tdma_flight_engine_apply_t applied;
        tdma_flight_engine_result_t engine_result =
            TDMA_FLIGHT_ENGINE_BAD_ARGUMENT;
        applied_ok = tdma_flight_engine_apply_preclassified(
            adapter->flight_engine,
            view.payload,
            view.payload_size,
            adapter->last_rx_new_segment_mask,
            has_tx ? &tx_view : NULL,
            processed_payload,
            sizeof(processed_payload),
            &applied,
            &engine_result);
        if (applied_ok) {
            memcpy(processed_packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
                   processed_payload,
                   view.payload_size);
        }
    }
    const bool prepared = adapter->phys_prepare_overlay(
        adapter->phys_ctrl_context,
        adapter->last_rx_packet,
        processed_packet,
        adapter->last_rx_packet_size);
    if (!prepared || !applied_ok) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
    }
    return prepared;
}

static bool tdma_pio_spi_ring_adapter_service_impl(
    void *context,
    uint64_t now_ns,
    tdma_ring_adapter_status_t *status)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL || status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    adapter->service_count++;
    adapter->last_service_ns = now_ns;

    /* A resident flight-origin submit returns at the PIO launch edge.  The
     * hardware latch is harvested on a later core1 pass and attached to the
     * exact sequence/identity entry that was published at launch. */
    if (adapter->phys_tx_complete != NULL) {
        uint64_t completed_timestamp_ns = 0ull;
        if (adapter->phys_tx_complete(adapter->phys_context,
                                      &completed_timestamp_ns)) {
            if (adapter->pending_tx_evidence) {
                const uint32_t evidence_index =
                    adapter->pending_tx_evidence_sequence %
                    TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
                if (adapter->reference_tx_evidence[evidence_index].sequence ==
                        adapter->pending_tx_evidence_sequence &&
                    adapter->reference_tx_evidence[evidence_index].identity_crc32 ==
                        adapter->pending_tx_evidence_identity_crc32) {
                    adapter->reference_tx_evidence[evidence_index].timestamp_ns =
                        completed_timestamp_ns;
                    adapter->reference_tx_evidence[evidence_index].valid =
                        completed_timestamp_ns != 0ull;
                    if (completed_timestamp_ns != 0ull) {
                        adapter->reference_tx_timestamp_ns =
                            completed_timestamp_ns;
                    }
                }
                adapter->pending_tx_evidence = false;
            }
        }
    }

    if (adapter->started == 0u || !adapter->configured) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT);
        return false;
    }

    status->up_configured = 1u;
    status->down_configured = 1u;
    status->schedule_crc32 = adapter->config.schedule_crc32;

    /* Bring-up honesty gate: without a physical TX path the adapter has no
     * UP leg. Returning false makes the ring runtime report EVIDENCE_MISSING
     * instead of a fabricated running state. RX may come from phys_rx or from
     * the injected test queue; with neither, down_running stays 0. */
    if (adapter->phys_tx == NULL) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_MISSING);
        return false;
    }

    bool tx_ok = false;
    bool rx_ok = false;
    if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) {
        /* Reference node is the ring origin: it emits one IDLE_BEACON per
         * TDMA cycle on the downlink TX leg and receives the same frame back
         * (hop advanced, identity preserved) on the uplink RX leg after it
         * has travelled once around the ring. Only this node may produce
         * simultaneous_feedback_loop_evidence. */
        /* A reference frame occupies one slot period per active node before
         * its feedback can return. The runtime freezes feedback_timeout_ns as
         * cycle_period_ns * node_count, so using it as the emission period
         * preserves the proven two-board 1 ms -> 500 Hz cadence and scales
         * deterministically for 3..8 nodes without a hard-coded divider. */
        const uint64_t emission_period_ns =
            adapter->config.feedback_timeout_ns != 0u
                ? adapter->config.feedback_timeout_ns
                : adapter->config.cycle_period_ns;
        bool emit_now = false;
        if (adapter->next_tx_deadline_ns == 0ull ||
            now_ns + emission_period_ns < now_ns) {
            /* Preserve the bring-up behavior where the first service only
             * establishes phase and the next service may emit. The backoff
             * models at most one current core1 tick; steady state below is
             * driven solely by absolute deadlines. */
            const uint64_t phase_backoff_ns =
                emission_period_ns < 1000000ull
                    ? emission_period_ns
                    : 1000000ull;
            adapter->next_tx_deadline_ns =
                now_ns + emission_period_ns - phase_backoff_ns;
        } else if (now_ns + emission_period_ns <
                   adapter->next_tx_deadline_ns) {
            /* Monotonic clock restarted or wrapped relative to the saved
             * deadline. Re-anchor without emitting a burst. */
            adapter->next_tx_deadline_ns = now_ns + emission_period_ns;
        } else {
            emit_now = now_ns >= adapter->next_tx_deadline_ns;
        }
        if (emit_now) {
            const bool first_emission = adapter->idle_beacon_tx_count == 0u;
            tx_ok = tdma_pio_spi_ring_adapter_tx_beacon(adapter);
            if (tx_ok) {
                /* Advance the absolute phase instead of restarting the
                 * period at the actual (jittered) service time. This avoids
                 * a nominal 2 ms cadence slipping to the third 1 ms RTOS
                 * tick after a slightly late service. Missed deadlines are
                 * coalesced: emit at most one frame per core1 service. */
                if (first_emission) {
                    adapter->next_tx_deadline_ns =
                        now_ns + emission_period_ns;
                } else {
                    do {
                        adapter->next_tx_deadline_ns += emission_period_ns;
                    } while (adapter->next_tx_deadline_ns <= now_ns &&
                             adapter->next_tx_deadline_ns >=
                                 emission_period_ns);
                }
            }
        } else {
            tx_ok = true; /* throttled round: keep the UP leg running. */
        }
        rx_ok = tdma_pio_spi_ring_adapter_rx_poll(adapter, now_ns);
    } else {
        /* Follower node (half-duplex ring): receives the frame from the
         * previous board on the uplink RX leg and re-emits it toward the next
         * board on the downlink TX leg, keeping origin/sequence/identity CRC
         * unchanged and advancing hop/transport CRC. When nothing arrived
         * this round there is nothing to forward and nothing is emitted: a
         * foreign placeholder beacon would race the reference frame around
         * the ring and corrupt the reference's feedback correlation. The TX
         * leg stays ready (up_running=1). */
        if (adapter->forwarding_mode ==
                TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT ||
            adapter->forwarding_mode ==
                TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE) {
            /* CS/SCK/DATA forwarding has already happened in PIO by the time
             * the complete captured frame reaches this parser. Re-emitting
             * here would create a second frame and destroy cut-through. */
            rx_ok = tdma_pio_spi_ring_adapter_rx_poll(adapter, now_ns);
            tx_ok = true;
            bool process_overlay_ok = true;
            if (rx_ok) {
                if (adapter->forwarding_mode ==
                        TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE &&
                    !tdma_pio_spi_ring_adapter_prepare_process_overlay(
                        adapter)) {
                    process_overlay_ok = false;
                }
                adapter->up_sequence = adapter->down_rx_sequence;
                adapter->up_tx_frame_crc32 = adapter->down_rx_frame_crc32;
                adapter->forward_count++;
                adapter->tx_count++;
            }
            if (adapter->forwarding_mode ==
                    TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE &&
                (adapter->phys_service_overlay_boundary == NULL ||
                 !adapter->phys_service_overlay_boundary(
                     adapter->phys_ctrl_context))) {
                process_overlay_ok = false;
            }
            if (!process_overlay_ok) {
                return false;
            }
        } else {
            tx_ok = tdma_pio_spi_ring_adapter_forward_poll(adapter, &rx_ok);
        }
    }
    if (!tx_ok) {
        return false;
    }
    if (rx_ok) {
        adapter->last_rx_service_ns = now_ns;
    } else if (adapter->receive_health.configured != 0u) {
        tdma_receive_health_observe_missing(&adapter->receive_health, now_ns);
    }

    status->up_running = 1u;
    if (rx_ok) {
        status->down_running = 1u;
    } else if (adapter->last_rx_service_ns != 0ull &&
               adapter->last_error !=
                   TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME) {
        const uint64_t freshness_ns =
            adapter->config.feedback_timeout_ns != 0u
                ? (uint64_t)adapter->config.feedback_timeout_ns * 4ull
                : 4000000ull;
        status->down_running =
            (now_ns >= adapter->last_rx_service_ns &&
             now_ns - adapter->last_rx_service_ns <= freshness_ns)
                ? 1u
                : 0u;
    } else {
        status->down_running = 0u;
    }
    status->up_tx_sequence = adapter->up_sequence;
    status->down_rx_sequence = adapter->down_rx_sequence;
    status->up_tx_frame_crc32 = adapter->up_tx_frame_crc32;
    status->down_rx_frame_crc32 = adapter->down_rx_frame_crc32;
    status->timestamp_resolution_ns =
        adapter->feedback_reference_sequence != 0u
            ? adapter->feedback_timestamp_resolution_ns
            : adapter->timestamp_resolution_ns;
    status->timestamp_flags =
        adapter->feedback_reference_sequence != 0u
            ? adapter->feedback_timestamp_flags
            : adapter->timestamp_flags;
    status->idle_beacon_tx_count = adapter->idle_beacon_tx_count;
    status->idle_beacon_rx_count = adapter->idle_beacon_rx_count;
    status->last_error = adapter->last_error;
    status->tx_count = adapter->tx_count;
    status->rx_count = adapter->rx_count;
    status->rx_bad_count = adapter->rx_bad_count;
    status->rx_transport_bad_count = adapter->rx_transport_bad_count;
    status->rx_schedule_bad_count = adapter->rx_schedule_bad_count;
    status->rx_profile_bad_count = adapter->rx_profile_bad_count;
    status->last_bad_transport_result = adapter->last_bad_transport_result;
    status->last_bad_sequence = adapter->last_bad_sequence;
    status->last_bad_schedule_crc32 = adapter->last_bad_schedule_crc32;
    status->last_bad_profile_crc32 = adapter->last_bad_profile_crc32;
    status->last_bad_header_diff_count =
        adapter->last_bad_header_diff_count;
    status->last_bad_header_first_diff_offset =
        adapter->last_bad_header_first_diff_offset;
    status->last_bad_header_expected_byte =
        adapter->last_bad_header_expected_byte;
    status->last_bad_header_observed_byte =
        adapter->last_bad_header_observed_byte;
    status->feedback_reference_sequence =
        adapter->feedback_reference_sequence;
    status->feedback_reference_frame_crc32 =
        adapter->feedback_reference_frame_crc32;
    status->reference_tx_timestamp_ns = adapter->reference_tx_timestamp_ns;
    status->feedback_rx_timestamp_ns = adapter->feedback_rx_timestamp_ns;
    status->clock_observation = adapter->clock_observation;
    return true;
}

static bool tdma_pio_spi_ring_adapter_service(
    void *context,
    uint64_t now_ns,
    tdma_ring_adapter_status_t *status)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL) {
        return false;
    }
    tdma_pio_spi_ring_adapter_snapshot_write_begin(adapter);
    const bool result = tdma_pio_spi_ring_adapter_service_impl(context,
                                                               now_ns,
                                                               status);
    tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
    return result;
}

static const tdma_ring_adapter_ops_t s_tdma_pio_spi_ring_adapter_ops = {
    .start = tdma_pio_spi_ring_adapter_start,
    .stop = tdma_pio_spi_ring_adapter_stop,
    .train_clock = tdma_pio_spi_ring_adapter_train_clock,
    .train_clock_service = tdma_pio_spi_ring_adapter_train_clock_service,
    .service = tdma_pio_spi_ring_adapter_service,
};

const tdma_ring_adapter_ops_t *tdma_pio_spi_ring_adapter_ops(void)
{
    return &s_tdma_pio_spi_ring_adapter_ops;
}

bool tdma_pio_spi_ring_adapter_get_snapshot(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_adapter_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL) {
        return false;
    }
    for (;;) {
        const uint32_t sequence_begin =
            __atomic_load_n(&adapter->snapshot_guard, __ATOMIC_ACQUIRE);
        if ((sequence_begin & 1u) != 0u) {
            continue;
        }
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->version = TDMA_PIO_SPI_RING_ADAPTER_VERSION;
        snapshot->started = adapter->started;
        snapshot->service_count = adapter->service_count;
        snapshot->role = (uint32_t)adapter->role;
        snapshot->forwarding_mode = (uint32_t)adapter->forwarding_mode;
        snapshot->forward_count = adapter->forward_count;
        snapshot->up_sequence = adapter->up_sequence;
        snapshot->down_rx_sequence = adapter->down_rx_sequence;
        snapshot->up_tx_frame_crc32 = adapter->up_tx_frame_crc32;
        snapshot->down_rx_frame_crc32 = adapter->down_rx_frame_crc32;
        snapshot->idle_beacon_tx_count = adapter->idle_beacon_tx_count;
        snapshot->idle_beacon_rx_count = adapter->idle_beacon_rx_count;
        snapshot->tx_count = adapter->tx_count;
        snapshot->rx_count = adapter->rx_count;
        snapshot->rx_bad_count = adapter->rx_bad_count;
        snapshot->rx_transport_bad_count = adapter->rx_transport_bad_count;
        snapshot->rx_schedule_bad_count = adapter->rx_schedule_bad_count;
        snapshot->rx_profile_bad_count = adapter->rx_profile_bad_count;
        snapshot->last_bad_transport_result =
            adapter->last_bad_transport_result;
        snapshot->last_bad_sequence = adapter->last_bad_sequence;
        snapshot->last_bad_schedule_crc32 =
            adapter->last_bad_schedule_crc32;
        snapshot->last_bad_profile_crc32 = adapter->last_bad_profile_crc32;
        snapshot->last_bad_header_diff_count =
            adapter->last_bad_header_diff_count;
        snapshot->last_bad_header_first_diff_offset =
            adapter->last_bad_header_first_diff_offset;
        snapshot->last_bad_header_expected_byte =
            adapter->last_bad_header_expected_byte;
        snapshot->last_bad_header_observed_byte =
            adapter->last_bad_header_observed_byte;
        snapshot->last_bad_packet_diff_count =
            adapter->last_bad_packet_diff_count;
        snapshot->last_bad_packet_first_diff_offset =
            adapter->last_bad_packet_first_diff_offset;
        snapshot->last_bad_packet_expected_byte =
            adapter->last_bad_packet_expected_byte;
        snapshot->last_bad_packet_observed_byte =
            adapter->last_bad_packet_observed_byte;
        snapshot->last_bad_clock_evidence = adapter->last_bad_clock_evidence;
        snapshot->last_bad_expected_transport_crc32 =
            adapter->last_bad_expected_transport_crc32;
        snapshot->last_bad_observed_transport_crc32 =
            adapter->last_bad_observed_transport_crc32;
        snapshot->last_bad_recomputed_transport_crc32 =
            adapter->last_bad_recomputed_transport_crc32;
        snapshot->last_bad_expected_payload_crc32 =
            adapter->last_bad_expected_payload_crc32;
        snapshot->last_bad_observed_payload_crc32 =
            adapter->last_bad_observed_payload_crc32;
        snapshot->clock_evidence_enabled = adapter->clock_evidence_enabled;
        snapshot->rx_drop_count = adapter->rx_drop_count;
        snapshot->timestamp_resolution_ns =
            adapter->feedback_reference_sequence != 0u
                ? adapter->feedback_timestamp_resolution_ns
                : adapter->timestamp_resolution_ns;
        snapshot->timestamp_flags =
            adapter->feedback_reference_sequence != 0u
                ? adapter->feedback_timestamp_flags
                : adapter->timestamp_flags;
        snapshot->feedback_reference_sequence =
            adapter->feedback_reference_sequence;
        snapshot->feedback_reference_frame_crc32 =
            adapter->feedback_reference_frame_crc32;
        snapshot->reference_tx_timestamp_ns = adapter->reference_tx_timestamp_ns;
        snapshot->feedback_rx_timestamp_ns = adapter->feedback_rx_timestamp_ns;
        snapshot->last_rx_service_ns = adapter->last_rx_service_ns;
        snapshot->last_service_ns = adapter->last_service_ns;
        snapshot->clock_observation = adapter->clock_observation;
        snapshot->clock_observation_count = adapter->clock_observation_count;
        snapshot->clock_observation_reject_count =
            adapter->clock_observation_reject_count;
        snapshot->last_error = adapter->last_error;
        snapshot->local_slot_id = adapter->config.local_slot_id;
        snapshot->schedule_crc32 = adapter->config.schedule_crc32;
        snapshot->ring_profile_crc32 = adapter->config.ring_profile_crc32;
        snapshot->feedback_timeout_ns = adapter->config.feedback_timeout_ns;
        snapshot->loop_delay_ns = adapter->config.loop_delay_ns;
        snapshot->loop_delay_tolerance_ns =
            adapter->config.loop_delay_tolerance_ns;
        snapshot->rx_ready_timestamp_ns = adapter->rx_ready_timestamp_ns;
        const uint32_t sequence_end =
            __atomic_load_n(&adapter->snapshot_guard, __ATOMIC_ACQUIRE);
        if (sequence_begin == sequence_end && (sequence_end & 1u) == 0u) {
            break;
        }
    }
    if (adapter->flight_engine != NULL) {
        tdma_flight_engine_snapshot_t engine_snapshot;
        if (tdma_flight_engine_get_snapshot(adapter->flight_engine,
                                             &engine_snapshot)) {
            snapshot->flight_map_configured = engine_snapshot.configured;
            snapshot->flight_map_active = engine_snapshot.active;
            snapshot->flight_map_crc32 = engine_snapshot.map_crc32;
            snapshot->flight_map_generation = engine_snapshot.map_generation;
            snapshot->flight_map_apply_count = engine_snapshot.map_apply_count;
            snapshot->flight_input_bytes = engine_snapshot.input_bytes;
            snapshot->flight_output_bytes = engine_snapshot.output_bytes;
            snapshot->flight_tx_stale_reuse_count =
                engine_snapshot.tx_stale_reuse_count;
            snapshot->flight_map_reject_count = engine_snapshot.map_reject_count;
            snapshot->flight_length_reject_count =
                engine_snapshot.length_reject_count;
            snapshot->flight_tx_unavailable_count =
                engine_snapshot.tx_unavailable_count;
        }
    }
    (void)tdma_receive_health_get_snapshot(&adapter->receive_health,
                                           adapter->last_service_ns,
                                           &snapshot->receive_health);
    return true;
}

bool tdma_pio_spi_ring_adapter_read_accepted_image(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint64_t now_ns,
    tdma_receive_image_t *image)
{
    if (adapter == NULL || image == NULL) {
        return false;
    }
    return tdma_receive_health_read_image(&adapter->receive_health,
                                          now_ns,
                                          image);
}
