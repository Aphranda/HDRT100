#include "tdma_seat_admission.h"
#include "tdma_seat_mount_store.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_pio_spi_ring_adapter_internal.h"
#include "tdma_long_frame.h"

#include <string.h>

#include "tdma_process_image_layout.h"
#include "tdma_service_timing.h"

#if TDMA_SERVICE_TIMING_ENABLED
/* Only the device build owns a real timebase (see tdma_service_timing.h). */
#include "vdc_timestamp_clock.h"
#endif

#define TDMA_PIO_SPI_RING_RESIDENT_BOOTSTRAP_RETRY_MAX 1u

#include "tdma_pio_spi_ring_origin.h"
#include "tdma_pio_spi_ring_rx_prepare.h"
#include "tdma_pio_spi_ring_adapter_api.h"

bool tdma_pio_spi_ring_adapter_set_seat_rotation3(
    tdma_pio_spi_ring_adapter_t *adapter, uint32_t priority_share,
    uint32_t first_class_share, uint32_t second_class_share)
{
    if (adapter == NULL || adapter->started != 0u ||
        !tdma_seat_rotation_valid3(priority_share, first_class_share,
                                   second_class_share)) {
        return false;
    }
    adapter->seat_rotation_priority_share = priority_share;
    adapter->seat_rotation_first_class_share = first_class_share;
    /* Reserved slots of the rotation cycle for the path that does not use the fast
     * channel; 0 keeps the whole cycle with the two fast seats. */
    adapter->seat_rotation_second_class_share = second_class_share;
    return true;
}

uint32_t tdma_pio_spi_ring_adapter_seat_rotation_second_class(
    const tdma_pio_spi_ring_adapter_t *adapter)
{
    return adapter != NULL ? adapter->seat_rotation_second_class_share : 0u;
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

bool tdma_pio_spi_ring_adapter_read_maintenance(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *payload_size,
    uint32_t *transport_sequence)
{
    if (adapter == NULL || payload == NULL || payload_size == NULL ||
        !adapter->last_rx_gate_accepted ||
        adapter->last_rx_packet_size == 0u ||
        adapter->last_rx_packet_size > sizeof(adapter->last_rx_packet)) {
        return false;
    }
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_decode(adapter->last_rx_packet,
                                     adapter->last_rx_packet_size,
                                     &view,
                                     &result) ||
        view.payload_class != TDMA_PAYLOAD_CLASS_CONFIG_CONTROL ||
        view.payload == NULL ||
        view.payload_size > payload_capacity ||
        view.origin_slot_id == adapter->config.local_slot_id) {
        return false;
    }
    memcpy(payload, view.payload, view.payload_size);
    *payload_size = view.payload_size;
    if (transport_sequence != NULL) {
        *transport_sequence = view.transport_sequence;
    }
    adapter->last_rx_gate_accepted = false;
    return true;
}

bool tdma_pio_spi_ring_adapter_take_maintenance_chunk(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE],
    uint32_t *source_slot)
{
    if (adapter == NULL || chunk == NULL ||
        adapter->last_rx_packet_size == 0u ||
        adapter->last_rx_packet_size > sizeof(adapter->last_rx_packet)) {
        return false;
    }
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_decode(adapter->last_rx_packet,
                                     adapter->last_rx_packet_size,
                                     &view,
                                     &result) ||
        (view.payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
         view.payload_class != TDMA_PAYLOAD_CLASS_CONFIG_CONTROL) ||
        view.payload == NULL) {
        return false;
    }
    adapter->maintenance_rx_polls++;
    if ((view.transport_sequence + 1u) ==
        adapter->maintenance_rx_sequence) {
        if (adapter->maintenance_rx_last_poll_sequence !=
            view.transport_sequence) {
            adapter->maintenance_rx_dedup_sequence++;
            adapter->maintenance_rx_last_poll_sequence =
                view.transport_sequence;
        }
        return false;
    }
    if (view.payload_class == TDMA_PAYLOAD_CLASS_CONFIG_CONTROL) {
        const uint8_t *mailbox = view.payload;
        if (view.payload_size < TDMA_FLIGHT_SHORT_SLOT_SIZE ||
            (uint32_t)mailbox[0] != (TDMA_FLIGHT_MAILBOX_MAGIC & 0xFFu) ||
            (uint32_t)mailbox[1] != (TDMA_FLIGHT_MAILBOX_MAGIC >> 8u) ||
            mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] !=
                TDMA_FLIGHT_MAILBOX_VERSION ||
            mailbox[3] != TDMA_PROCESS_IMAGE_MESSAGE_CLASS ||
            mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] ==
                (uint8_t)adapter->config.local_slot_id) {
            adapter->maintenance_rx_bad_mailbox++;
            return false;
        }
        memcpy(chunk,
               mailbox + TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE,
               TDMA_MAINTENANCE_CHUNK_SIZE);
        if (source_slot != NULL) {
            *source_slot = mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET];
        }
        adapter->maintenance_rx_sequence = view.transport_sequence + 1u;
        adapter->maintenance_rx_last_sequence = view.transport_sequence;
        adapter->maintenance_rx_seen_chunks++;
        adapter->maintenance_rx_accepted_chunks++;
        return true;
    }
    const uint32_t mask = (1u << adapter->config.node_count) - 1u;
    for (uint32_t slot = 0u; slot < adapter->config.node_count; slot++) {
        if ((mask & (1u << slot)) == 0u) {
            continue;
        }
        const uint8_t *mailbox =
            view.payload + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE;
        if ((uint32_t)mailbox[0] != (TDMA_FLIGHT_MAILBOX_MAGIC & 0xFFu) ||
            (uint32_t)mailbox[1] != (TDMA_FLIGHT_MAILBOX_MAGIC >> 8u) ||
            mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] !=
                TDMA_FLIGHT_MAILBOX_VERSION ||
            mailbox[3] != TDMA_PROCESS_IMAGE_MESSAGE_CLASS ||
            mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] ==
                (uint8_t)adapter->config.local_slot_id) {
            continue;
        }
        memcpy(chunk,
               mailbox + TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE,
               TDMA_MAINTENANCE_CHUNK_SIZE);
        if (source_slot != NULL) {
            *source_slot = mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET];
        }
        adapter->maintenance_rx_sequence = view.transport_sequence + 1u;
        adapter->maintenance_rx_last_sequence = view.transport_sequence;
        adapter->maintenance_rx_seen_chunks++;
        adapter->maintenance_rx_accepted_chunks++;
        return true;
    }
    return false;
}

void tdma_pio_spi_ring_adapter_get_maintenance_counters(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *priority_calls,
    uint32_t *yield_slots,
    uint32_t *attempts,
    uint32_t *publish_ok)
{
    if (adapter == NULL) {
        return;
    }
    if (priority_calls != NULL) {
        *priority_calls = adapter->maintenance_priority_calls;
    }
    if (yield_slots != NULL) {
        *yield_slots = adapter->maintenance_yield_slots;
    }
    if (attempts != NULL) {
        *attempts = adapter->maintenance_attempts;
    }
    if (publish_ok != NULL) {
        *publish_ok = adapter->maintenance_publish_ok;
    }
}

uint32_t tdma_pio_spi_ring_adapter_expected_owner_mask(
    const tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_ring_runtime_config_t *config)
{
    if (adapter == NULL || config == NULL || config->node_count < 2u ||
        config->node_count > TDMA_RING_NODE_MAX ||
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

bool tdma_pio_spi_ring_adapter_resident_hop_position(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *ingress_hop,
    uint32_t *egress_hop)
{
    if (adapter == NULL || ingress_hop == NULL || egress_hop == NULL ||
        adapter->topology.valid == 0u ||
        adapter->topology.node_count != adapter->config.node_count ||
        adapter->config.local_slot_id == adapter->config.reference_slot_id) {
        return false;
    }
    uint32_t node = adapter->config.reference_slot_id;
    for (uint32_t hop = 1u; hop < adapter->config.node_count; hop++) {
        node = adapter->topology.data_next_node[node];
        if (node >= adapter->config.node_count) {
            return false;
        }
        if (node == adapter->config.local_slot_id) {
            *ingress_hop = hop - 1u;
            *egress_hop = hop;
            return true;
        }
    }
    return false;
}

bool tdma_pio_spi_ring_adapter_start(
    void *context,
    const tdma_ring_runtime_config_t *config)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL) {
        return false;
    }
    tdma_pio_spi_ring_adapter_snapshot_write_begin(adapter);
    /* Consume geometry before any new-ARM early exit. Runtime failure
     * cleanup retires the pending request through the physical STOP. */
    if (adapter->phys_arm_request != NULL) {
        const uint64_t arm_request_start = tdma_service_timing_now();
        const bool arm_request_ok = adapter->phys_arm_request(
            adapter->phys_ctrl_context, config);
        tdma_service_timing_record(TDMA_TIMING_ARM_REQUEST, arm_request_start);
        if (!arm_request_ok) {
            const uint32_t error = adapter->phys_last_error != NULL
                ? adapter->phys_last_error(adapter->phys_ctrl_context) : 0u;
            tdma_pio_spi_ring_adapter_set_error(adapter,
                TDMA_PIO_SPI_RING_ADAPTER_PHYS_ARM_ERROR(error));
            tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
            return false;
        }
    }
    if ((adapter->started != 0u &&
         tdma_ring_diagnostic_burst_limit(adapter->config.flags) != 0u) ||
        adapter->origin.active != 0u ||
        tdma_overlay_prepare_state(adapter->overlay_preparation) != TDMA_OVERLAY_PREPARE_IDLE ||
        tdma_rx_prepare_state(adapter->rx_preparation) != TDMA_RX_PREPARE_IDLE ||
        config == NULL || config->enabled == 0u ||
        config->node_count < 2u ||
        config->node_count > TDMA_RING_NODE_MAX ||
        config->local_slot_id >= config->node_count ||
        config->reference_slot_id >= config->node_count ||
        config->up_group_id == 0u || config->down_group_id == 0u ||
        config->up_group_id == config->down_group_id ||
        config->ring_profile_crc32 == 0u ||
        config->schedule_crc32 == 0u ||
        config->operating_profile_crc32 == 0u ||
        config->baud_hz < 1000000u || config->baud_hz > 50000000u ||
        config->cycle_period_ns == 0u ||
        config->feedback_timeout_ns == 0u ||
        (tdma_ring_diagnostic_burst_limit(config->flags) != 0u &&
         (adapter->started != 0u ||
          tdma_ring_diagnostic_burst_limit(config->flags) > 2u ||
          (config->flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) == 0u ||
          config->local_slot_id != config->reference_slot_id ||
          adapter->forwarding_mode !=
              TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE))) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT);
        tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
        return false;
    }
    /* A failed new ARM must not pair its new quota with the prior session's
     * retained launch count. Only a completely armed session is readable. */
    adapter->diagnostic_burst_valid = 0u;
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
        const uint32_t phys_error = adapter->phys_last_error != NULL
            ? adapter->phys_last_error(adapter->phys_ctrl_context)
            : 0u;
        if (adapter->phys_disarm != NULL) {
            adapter->phys_disarm(adapter->phys_ctrl_context);
        }
        tdma_flight_engine_deactivate(adapter->flight_engine);
        tdma_pio_spi_ring_adapter_set_error(
            adapter,
            phys_error != 0u
                ? TDMA_PIO_SPI_RING_ADAPTER_PHYS_ARM_ERROR(phys_error)
                : TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_MISSING);
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
    tdma_adapter_comm_fsm_init(&adapter->comm_fsm);
    if (!tdma_adapter_comm_fsm_dispatch(
            &adapter->comm_fsm, TDMA_ADAPTER_COMM_EVENT_ARM, 0u)) {
        if (adapter->phys_disarm != NULL) {
            adapter->phys_disarm(adapter->phys_ctrl_context);
        }
        tdma_flight_engine_deactivate(adapter->flight_engine);
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT);
        tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
        return false;
    }
    adapter->started = 1u;
    memset(&adapter->origin, 0, sizeof(adapter->origin));
    memset(adapter->origin_shadow, 0, sizeof(adapter->origin_shadow));
    /* These counters describe one armed ring session.  Keeping values from a
     * previous topology makes a follower that has not received any frame look
     * as if it is still forwarding traffic, and invalidates START readback. */
    adapter->forward_count = 0u;
    adapter->idle_beacon_tx_count = 0u;
    adapter->idle_beacon_rx_count = 0u;
    adapter->tx_count = 0u;
    adapter->diagnostic_burst_launched = 0u;
    adapter->diagnostic_burst_valid = 1u;
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
    /* A loop/beat measurement belongs to one armed session; a re-arm must
     * never let the previous session's revolution look current. */
    tdma_pio_spi_ring_adapter_reset_loop_time(adapter);
    memset(&adapter->origin_feedback_tx, 0, sizeof(adapter->origin_feedback_tx));
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
    adapter->reference_tx_completion_pending = false;
    adapter->pending_tx_evidence_sequence = 0u;
    adapter->pending_tx_evidence_identity_crc32 = 0u;
    adapter->pending_tx_evidence = false;
    adapter->feedback_rx_timestamp_ns = 0ull;
    adapter->last_rx_service_ns = 0ull;
    adapter->last_service_ns = 0ull;
    adapter->last_rx_packet_size = 0u;
    adapter->last_rx_origin_slot_id = 0u;
    adapter->last_rx_hop_count = 0u;
    adapter->last_rx_hop_limit = 0u;
    adapter->last_rx_flags = 0u;
    adapter->last_rx_sequence = 0u;
    adapter->last_rx_identity_crc32 = 0u;
    adapter->local_tx_edge_timestamp_ns = 0ull;
    adapter->local_tx_edge_count = 0u;
    adapter->local_tx_edge_miss_count = 0u;
    adapter->local_tx_edge_unpaired_count = 0u;
    adapter->local_tx_edge_sequence = 0u;
    adapter->local_tx_edge_identity_crc32 = 0u;
    adapter->local_tx_edge_capture_generation = 0u;
    adapter->local_tx_edge_flags = 0u;
    adapter->resident_feedback_condition_mask = 0u;
    adapter->rx_queue_head = 0u;
    adapter->rx_queue_count = 0u;
    memset(adapter->reference_tx_evidence,
           0,
           sizeof(adapter->reference_tx_evidence));
    memset(adapter->local_rx_evidence, 0, sizeof(adapter->local_rx_evidence));
    memset(adapter->local_tx_edge_evidence,
           0,
           sizeof(adapter->local_tx_edge_evidence));
    memset(&adapter->clock_observation, 0, sizeof(adapter->clock_observation));
    adapter->clock_observation_count = 0u;
    adapter->clock_observation_reject_count = 0u;
    adapter->clock_observation_last_reject_reason = 0u;
    adapter->clock_evidence_build_count = 0u;
    adapter->clock_evidence_build_reject_count = 0u;
    adapter->clock_evidence_build_last_reason = 0u;
    adapter->clock_evidence_last_tx_encoded = 0u;
    adapter->clock_evidence_last_rx_encoded = 0u;
    adapter->next_tx_deadline_ns = 0ull;
    adapter->resident_seeded = false;
    adapter->resident_return_ready = false;
    adapter->resident_return_new_segment_mask = 0u;
    adapter->resident_completion_pending = false;
    adapter->resident_pending_completed_cycle = 0u;
    adapter->resident_pending_completed_segment_mask = 0u;
    adapter->resident_bootstrap_tx_pending = false;
    adapter->resident_bootstrap_retry_count = 0u;
    adapter->resident_packet_size = 0u;
    adapter->resident_overlay_bootstrap_prepared = false;
    adapter->resident_overlay_target_sequence = 0u;
    adapter->resident_overlay_tx_generation = 0u;
    adapter->resident_overlay_tx_sequence = 0u;
    adapter->resident_stale_cycle_count = 0u;
    adapter->resident_last_completed_cycle = 0u;
    adapter->resident_last_completed_segment_mask = 0u;
    adapter->resident_reseed_count = 0u;
    adapter->resident_last_reseed_reason =
        TDMA_PIO_SPI_RESIDENT_RESEED_NONE;
    adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
    return true;
}

static bool tdma_pio_spi_ring_adapter_stop_locked(void *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL) {
        return false;
    }
    tdma_pio_spi_ring_adapter_snapshot_write_begin(adapter);
    const bool rx_retired = tdma_rx_prepare_cancel(adapter->rx_preparation);
    if (adapter->origin.active && adapter->priority_tx_provider != NULL)
        (void)adapter->priority_tx_provider(NULL, NULL);
    tdma_first_class_retire(adapter->first_class, false);
    const uint64_t disarm_start = tdma_service_timing_now();
    const bool disarmed = adapter->phys_disarm == NULL ||
        adapter->phys_disarm(adapter->phys_ctrl_context);
    tdma_service_timing_record(TDMA_TIMING_ADAPTER_DISARM, disarm_start);
    if (!disarmed) {
        const uint32_t error = adapter->phys_last_error != NULL
            ? adapter->phys_last_error(adapter->phys_ctrl_context) : 0u;
        tdma_pio_spi_ring_adapter_set_error(adapter,
            TDMA_PIO_SPI_RING_ADAPTER_PHYS_ARM_ERROR(error));
        tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
        return false;
    }
    if (adapter->phys_timestamp_ready != NULL) {
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            adapter, 0u, TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
    }
    /* Hardware has stopped; only the static station lease awaits an ACK. */
    if (!rx_retired) {
        tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
        return false;
    }
    tdma_flight_engine_deactivate(adapter->flight_engine);
    if (adapter->comm_fsm.state == TDMA_ADAPTER_COMM_STATE_FAULT) {
        (void)tdma_adapter_comm_fsm_dispatch(
            &adapter->comm_fsm, TDMA_ADAPTER_COMM_EVENT_RESET, 0u);
    } else if (adapter->comm_fsm.state !=
               TDMA_ADAPTER_COMM_STATE_STOPPED) {
        (void)tdma_adapter_comm_fsm_dispatch(
            &adapter->comm_fsm, TDMA_ADAPTER_COMM_EVENT_STOP, 0u);
    }
    adapter->started = 0u;
    memset(&adapter->origin, 0, sizeof(adapter->origin));
    memset(adapter->origin_shadow, 0, sizeof(adapter->origin_shadow));
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
    adapter->reference_tx_completion_pending = false;
    adapter->pending_tx_evidence_sequence = 0u;
    adapter->pending_tx_evidence_identity_crc32 = 0u;
    adapter->pending_tx_evidence = false;
    adapter->feedback_rx_timestamp_ns = 0ull;
    adapter->last_rx_service_ns = 0ull;
    adapter->last_service_ns = 0ull;
    adapter->last_rx_packet_size = 0u;
    adapter->last_rx_origin_slot_id = 0u;
    adapter->last_rx_hop_count = 0u;
    adapter->last_rx_hop_limit = 0u;
    adapter->last_rx_flags = 0u;
    adapter->last_rx_sequence = 0u;
    adapter->last_rx_identity_crc32 = 0u;
    adapter->local_tx_edge_timestamp_ns = 0ull;
    adapter->local_tx_edge_count = 0u;
    adapter->local_tx_edge_miss_count = 0u;
    adapter->local_tx_edge_unpaired_count = 0u;
    adapter->local_tx_edge_sequence = 0u;
    adapter->local_tx_edge_identity_crc32 = 0u;
    adapter->local_tx_edge_capture_generation = 0u;
    adapter->local_tx_edge_flags = 0u;
    adapter->resident_feedback_condition_mask = 0u;
    adapter->rx_queue_head = 0u;
    adapter->rx_queue_count = 0u;
    adapter->resident_seeded = false;
    adapter->resident_return_ready = false;
    adapter->resident_return_new_segment_mask = 0u;
    adapter->resident_completion_pending = false;
    adapter->resident_pending_completed_cycle = 0u;
    adapter->resident_pending_completed_segment_mask = 0u;
    adapter->resident_bootstrap_tx_pending = false;
    adapter->resident_bootstrap_retry_count = 0u;
    adapter->resident_packet_size = 0u;
    adapter->resident_overlay_bootstrap_prepared = false;
    adapter->resident_overlay_target_sequence = 0u;
    adapter->resident_overlay_tx_generation = 0u;
    adapter->resident_overlay_tx_sequence = 0u;
    adapter->resident_stale_cycle_count = 0u;
    adapter->resident_last_completed_cycle = 0u;
    adapter->resident_last_completed_segment_mask = 0u;
    adapter->resident_reseed_count = 0u;
    adapter->resident_last_reseed_reason =
        TDMA_PIO_SPI_RESIDENT_RESEED_NONE;
    tdma_receive_health_reset_stopped(&adapter->receive_health);
    memset(adapter->reference_tx_evidence,
           0,
           sizeof(adapter->reference_tx_evidence));
    memset(adapter->local_rx_evidence, 0, sizeof(adapter->local_rx_evidence));
    memset(adapter->local_tx_edge_evidence,
           0,
           sizeof(adapter->local_tx_edge_evidence));
    memset(&adapter->origin_feedback_tx, 0, sizeof(adapter->origin_feedback_tx));
    memset(&adapter->clock_observation, 0, sizeof(adapter->clock_observation));
    if (adapter->phys_stopped != NULL)
        adapter->phys_stopped(adapter->phys_ctrl_context);
    tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
    return true;
}

bool tdma_pio_spi_ring_adapter_stop(void *context)
{
    /* STOP is the ARM rollback path too, so attribute it: a transition phase
     * otherwise reports only the surrounding ring_runtime interval. */
    const uint64_t started = tdma_service_timing_now();
    const bool stopped = tdma_pio_spi_ring_adapter_stop_locked(context);
    tdma_service_timing_record(TDMA_TIMING_ADAPTER_STOP, started);
    return stopped;
}

bool tdma_pio_spi_ring_adapter_resident_process_image(
    const tdma_pio_spi_ring_adapter_t *adapter)
{
    return adapter != NULL &&
           adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
           adapter->forwarding_mode ==
               TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE &&
           adapter->flight_engine != NULL &&
           tdma_flight_engine_is_active(adapter->flight_engine);
}

void tdma_pio_spi_ring_adapter_commit_resident_completion(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter == NULL || !adapter->resident_completion_pending ||
        adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY) {
        return;
    }
    adapter->resident_last_completed_cycle =
        adapter->resident_pending_completed_cycle;
    adapter->resident_last_completed_segment_mask =
        adapter->resident_pending_completed_segment_mask;
    adapter->resident_completion_pending = false;
}

bool tdma_pio_spi_ring_adapter_launch_reference(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *packet,
    size_t packet_size,
    const tdma_transport_frame_view_t *view,
    bool clock_evidence,
    bool bootstrap_retry,
    bool *launched)
{
    if (launched == NULL) {
        return false;
    }
    *launched = false;
    const bool resident =
        tdma_pio_spi_ring_adapter_resident_process_image(adapter);
    uint64_t tx_timestamp_ns = 0ull;
    if (tdma_pio_spi_ring_adapter_burst_exhausted(adapter)) {
        adapter->transport_load_tx_stage = 6u;
        return true;
    }
    const uint64_t submit_start = tdma_service_timing_now();
    const bool submitted = adapter->phys_tx(adapter->phys_context,
                                           packet,
                                           packet_size,
                                           &tx_timestamp_ns);
    tdma_service_timing_record(TDMA_TIMING_REFERENCE_SUBMIT, submit_start);
    if (!submitted) {
        const void *error_context = adapter->phys_ctrl_context != NULL
            ? adapter->phys_ctrl_context : adapter->phys_context;
        if (adapter->phys_tx_retryable != NULL &&
            adapter->phys_tx_retryable(error_context)) {
            /* Physical backpressure is an observed deferred submission, not
             * a communication-window failure. No FSM event has been emitted,
             * so the same resident image remains eligible on the next core1
             * service pass. */
            return true;
        }
        if (resident) {
            (void)tdma_adapter_comm_fsm_dispatch(
                &adapter->comm_fsm,
                TDMA_ADAPTER_COMM_EVENT_ERROR,
                TDMA_ADAPTER_COMM_ERROR_RUNTIME);
        }
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        adapter->transport_load_tx_stage = 4u;
        return false;
    }

    /* An accepted submission may already be on the wire. Count it even if
     * a later FSM/evidence step fails, and never refund an aborted frame. */
    adapter->transport_load_tx_stage = 8u;
    if (tdma_ring_diagnostic_burst_limit(adapter->config.flags) != 0u) {
        adapter->diagnostic_burst_launched++;
    }
    bool comm_started = true;
    if (resident && !bootstrap_retry &&
        adapter->comm_fsm.state ==
            TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY) {
        comm_started = tdma_adapter_comm_fsm_dispatch(
            &adapter->comm_fsm,
            TDMA_ADAPTER_COMM_EVENT_BEGIN_NEXT_CYCLE,
            0u);
    }
    if (resident && comm_started) {
        comm_started = bootstrap_retry
            ? tdma_adapter_comm_fsm_dispatch(
                  &adapter->comm_fsm,
                  TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_STARTED,
                  0u)
            : (tdma_adapter_comm_fsm_dispatch(
                   &adapter->comm_fsm,
                   TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED,
                   0u) &&
               tdma_adapter_comm_fsm_dispatch(
                   &adapter->comm_fsm,
                   TDMA_ADAPTER_COMM_EVENT_DATA_RX_STARTED,
                   0u));
    }
    if (!comm_started) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
        adapter->transport_load_tx_stage = 5u;
        return false;
    }

    *launched = true;
    adapter->transport_load_tx_stage = 9u;
    adapter->reference_tx_completion_pending =
        tx_timestamp_ns == 0ull && adapter->phys_tx_complete != NULL;
    adapter->resident_bootstrap_tx_pending =
        adapter->reference_tx_completion_pending && bootstrap_retry;
    adapter->up_sequence = view->transport_sequence;
    adapter->up_tx_frame_crc32 = view->identity_crc32;
    adapter->tx_count++;
    adapter->idle_beacon_tx_count++;
    adapter->resident_seeded = resident;
    if (resident && !bootstrap_retry) {
        memcpy(adapter->resident_packet, packet, packet_size);
        adapter->resident_packet_size = packet_size;
    } else if (resident) {
        adapter->resident_bootstrap_retry_count++;
    }

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

    const uint32_t evidence_index =
        view->transport_sequence %
        TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
    adapter->reference_tx_evidence[evidence_index].sequence =
        view->transport_sequence;
    adapter->reference_tx_evidence[evidence_index].identity_crc32 =
        view->identity_crc32;
    adapter->reference_tx_evidence[evidence_index].timestamp_ns =
        tx_timestamp_ns;
    memcpy(adapter->reference_tx_evidence[evidence_index].packet,
           packet,
           packet_size);
    adapter->reference_tx_evidence[evidence_index].packet_size = packet_size;
    adapter->reference_tx_evidence[evidence_index].clock_evidence =
        clock_evidence;
    adapter->reference_tx_evidence[evidence_index].valid =
        tx_timestamp_ns != 0ull;
    tdma_pio_spi_ring_adapter_bind_reference_local_tx_edge(
        adapter,
        view->transport_sequence,
        view->identity_crc32,
        tx_timestamp_ns);
    if (adapter->reference_tx_completion_pending) {
        adapter->pending_tx_evidence_sequence = view->transport_sequence;
        adapter->pending_tx_evidence_identity_crc32 = view->identity_crc32;
        adapter->pending_tx_evidence = true;
    }
    const tdma_adapter_comm_event_t completion_event = bootstrap_retry
        ? TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_COMPLETED
        : TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED;
    if (resident && !adapter->reference_tx_completion_pending &&
        !tdma_adapter_comm_fsm_dispatch(
            &adapter->comm_fsm,
            completion_event,
            0u)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }
    return true;
}

bool tdma_pio_spi_ring_adapter_tx_resident_cycle_from(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *source_packet,
    size_t source_packet_size,
    bool data_rx_timed_out,
    bool *launched)
{
    if (launched == NULL) {
        return false;
    }
    *launched = false;
    if (!tdma_pio_spi_ring_adapter_resident_process_image(adapter) ||
        source_packet == NULL || source_packet_size == 0u ||
        source_packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX) {
        return false;
    }

    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    memcpy(packet, source_packet, source_packet_size);
    const size_t packet_size = source_packet_size;
    tdma_transport_frame_view_t returned_view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_decode(packet,
                                     packet_size,
                                     &returned_view,
                                     &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME);
        return false;
    }

    /* A timed-out cycle has no physical return packet. The resident cache is
     * the frame as launched at hop zero, so move only the local working copy
     * to the logical return position before using the normal next-cycle
     * transport transition. This preserves the last process image without
     * manufacturing RX evidence. */
    if (data_rx_timed_out &&
        !tdma_transport_frame_prepare_resident_position(
            packet,
            packet_size,
            returned_view.transport_sequence,
            returned_view.hop_limit,
            &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    tdma_flight_tx_view_t tx_view;
    const bool has_tx = adapter->flight_fifo != NULL &&
        tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx_view);
    uint8_t processed_payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
    tdma_flight_engine_apply_t applied;
    tdma_flight_engine_result_t engine_result =
        TDMA_FLIGHT_ENGINE_BAD_ARGUMENT;
    if (!tdma_flight_engine_tx_load(
            adapter->flight_engine,
            returned_view.payload,
            returned_view.payload_size,
            has_tx ? &tx_view : NULL,
            processed_payload,
            sizeof(processed_payload),
            &applied,
            &engine_result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
        return false;
    }

    bool maintenance_injected = false;
    if (adapter->maintenance_mailbox != NULL &&
        returned_view.payload_size >=
            (adapter->config.local_slot_id + 1u) *
                TDMA_FLIGHT_SHORT_SLOT_SIZE) {
        uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
        adapter->maintenance_inject_attempts++;
        if (adapter->maintenance_mailbox(adapter->maintenance_mailbox_context,
                                         mailbox)) {
            adapter->maintenance_inject_ok++;
            maintenance_injected = true;
            uint8_t *slot = processed_payload +
                adapter->config.local_slot_id *
                    TDMA_FLIGHT_SHORT_SLOT_SIZE;
            memcpy(slot, mailbox, TDMA_FLIGHT_SHORT_SLOT_SIZE);
            const uint16_t crc = tdma_process_image_crc16_ccitt(
                slot, TDMA_PROCESS_IMAGE_CRC_OFFSET);
            slot[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)crc;
            slot[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] =
                (uint8_t)(crc >> 8u);
        }
    }

    const bool dpll_observation_ready =
        tdma_pio_spi_ring_adapter_build_dpll_observation(
            adapter, processed_payload, returned_view.payload_size);
    if (!tdma_transport_frame_begin_next_cycle(
            packet,
            packet_size,
            adapter->config.local_slot_id,
            0u,
            processed_payload,
            returned_view.payload_size,
            &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    tdma_transport_frame_view_t next_view;
    if (!tdma_transport_frame_decode(packet,
                                     packet_size,
                                     &next_view,
                                     &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }
    if (maintenance_injected) {
        const uint32_t emitted_sequence = next_view.transport_sequence;
        if (emitted_sequence == adapter->maintenance_inject_last_sequence) {
            adapter->maintenance_inject_repeated_sequence++;
        } else {
            adapter->maintenance_inject_new_sequence++;
        }
        adapter->maintenance_inject_last_sequence = emitted_sequence;
    }
    if (data_rx_timed_out &&
        adapter->comm_fsm.state == TDMA_ADAPTER_COMM_STATE_RUNNING &&
        !tdma_adapter_comm_fsm_dispatch(
            &adapter->comm_fsm,
            TDMA_ADAPTER_COMM_EVENT_DATA_RX_TIMED_OUT,
            0u)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
        return false;
    }
    if (adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
        return false;
    }
    bool reference_launched = false;
    if (!tdma_pio_spi_ring_adapter_launch_reference(
            adapter,
            packet,
            packet_size,
            &next_view,
            dpll_observation_ready,
            false,
            &reference_launched)) {
        return false;
    }
    if (maintenance_injected) {
        if (reference_launched) {
            adapter->maintenance_inject_launched++;
        } else {
            adapter->maintenance_inject_dropped_unlaunched++;
        }
    }
    if (!reference_launched) {
        return true;
    }
    *launched = true;
    adapter->resident_return_ready = false;
    adapter->resident_return_new_segment_mask = 0u;
    if (data_rx_timed_out) {
        adapter->resident_stale_cycle_count++;
        adapter->resident_reseed_count++;
        adapter->resident_last_reseed_reason =
            adapter->comm_fsm.completed_window_count == 0u
                ? TDMA_PIO_SPI_RESIDENT_RESEED_BOOTSTRAP_EXHAUSTED_LAST_VALID
                : TDMA_PIO_SPI_RESIDENT_RESEED_FEEDBACK_TIMEOUT_LAST_VALID;
    }
    return true;
}

bool tdma_pio_spi_ring_adapter_tx_beacon(
    tdma_pio_spi_ring_adapter_t *adapter,
    bool *launched);

bool tdma_pio_spi_ring_adapter_tx_next_resident_cycle(
    tdma_pio_spi_ring_adapter_t *adapter,
    bool *launched)
{
    if (adapter != NULL && adapter->transport_load != NULL &&
        tdma_transport_load_pending(adapter->transport_load) &&
        tdma_transport_load_window_open(adapter->transport_load)) {
        const bool ok = tdma_pio_spi_ring_adapter_tx_beacon(adapter, launched);
        if (ok) {
            adapter->transport_load_tx_ok++;
        } else {
            adapter->transport_load_tx_fail++;
        }
        return ok;
    }
    if (adapter == NULL || !adapter->resident_return_ready ||
        adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY ||
        adapter->last_rx_packet_size == 0u) {
        return false;
    }
    return tdma_pio_spi_ring_adapter_tx_resident_cycle_from(
        adapter,
        adapter->last_rx_packet,
        adapter->last_rx_packet_size,
        false,
        launched);
}
