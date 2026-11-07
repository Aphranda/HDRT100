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

bool tdma_pio_spi_ring_adapter_tx_stale_resident_cycle(
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
    if (adapter == NULL || !adapter->resident_seeded ||
        adapter->resident_return_ready ||
        adapter->reference_tx_completion_pending ||
        adapter->resident_packet_size == 0u ||
        (adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_RUNNING &&
         adapter->comm_fsm.state !=
             TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY) ||
        (adapter->comm_fsm.state == TDMA_ADAPTER_COMM_STATE_RUNNING &&
         (adapter->comm_fsm.clock_tx_active ||
          !adapter->comm_fsm.clock_tx_complete ||
          !adapter->comm_fsm.data_rx_active ||
          adapter->comm_fsm.data_rx_complete ||
          adapter->comm_fsm.bootstrap_tx_active))) {
        return false;
    }
    return tdma_pio_spi_ring_adapter_tx_resident_cycle_from(
        adapter,
        adapter->resident_packet,
        adapter->resident_packet_size,
        true,
        launched);
}

bool tdma_pio_spi_ring_adapter_tx_bootstrap_retry(
    tdma_pio_spi_ring_adapter_t *adapter,
    bool *launched)
{
    if (!tdma_pio_spi_ring_adapter_resident_process_image(adapter) ||
        adapter->resident_packet_size == 0u ||
        adapter->resident_packet_size > sizeof(adapter->resident_packet) ||
        adapter->resident_bootstrap_retry_count >=
            TDMA_PIO_SPI_RING_RESIDENT_BOOTSTRAP_RETRY_MAX ||
        adapter->resident_return_ready ||
        adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_RUNNING ||
        !adapter->comm_fsm.clock_tx_complete ||
        !adapter->comm_fsm.data_rx_active ||
        adapter->comm_fsm.bootstrap_tx_active) {
        return false;
    }

    tdma_transport_frame_view_t view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_decode(adapter->resident_packet,
                                     adapter->resident_packet_size,
                                     &view,
                                     &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME);
        return false;
    }
    return tdma_pio_spi_ring_adapter_launch_reference(
        adapter,
        adapter->resident_packet,
        adapter->resident_packet_size,
        &view,
        false,
        true,
        launched);
}

bool tdma_pio_spi_ring_adapter_tx_beacon(
    tdma_pio_spi_ring_adapter_t *adapter,
    bool *launched)
{
    tdma_flight_tx_view_t tx_view;
    memset(&tx_view, 0, sizeof(tx_view));
    uint8_t load_payload[TDMA_TRANSPORT_LOAD_MAX_BYTES];
    uint32_t load_size = 0u;
    const bool load_frame = adapter->transport_load != NULL &&
        tdma_transport_load_window_open(adapter->transport_load) &&
        tdma_transport_load_take(adapter->transport_load,
                                 load_payload,
                                 sizeof(load_payload),
                                 &load_size);
    if (load_frame) {
        tdma_transport_load_advance(adapter->transport_load);
    }
    const bool has_flight_tx = !load_frame &&
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
    bool emit_process_image = has_flight_tx || product_process_image;
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
    if (!load_frame && product_process_image) {
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
        if (!tdma_flight_engine_tx_load(
                adapter->flight_engine,
                empty_process_payload,
                engine_snapshot.payload_size,
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
    uint8_t maintenance_payload[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    bool maintenance_frame = false;
    if (!has_flight_tx && adapter->maintenance_mailbox != NULL &&
        adapter->maintenance_mailbox(adapter->maintenance_mailbox_context,
                                     maintenance_payload)) {
        wire_payload = maintenance_payload;
        wire_payload_size = sizeof(maintenance_payload);
        emit_process_image = true;
        maintenance_frame = true;
        adapter->maintenance_beacon_frames++;
        dpll_observation_ready = false;
    }
    if (load_frame) {
        static uint8_t load_padded[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX];
        memset(load_padded, 0, sizeof(load_padded));
        memcpy(load_padded, load_payload,
               load_size < sizeof(load_padded) ? load_size : sizeof(load_padded));
        size_t target_size = fixed_flight_payload_size;
        const uint8_t *packets[2] = {
            adapter->resident_packet, adapter->last_rx_packet};
        const size_t packet_sizes[2] = {
            adapter->resident_packet_size, adapter->last_rx_packet_size};
        for (uint32_t i = 0u; i < 2u; i++) {
            if (packet_sizes[i] == 0u) {
                continue;
            }
            tdma_transport_frame_view_t resident_view;
            tdma_transport_result_t resident_result = TDMA_TRANSPORT_OK;
            if (tdma_transport_frame_decode(packets[i], packet_sizes[i],
                                            &resident_view,
                                            &resident_result) &&
                resident_view.payload_size <= sizeof(load_padded) &&
                resident_view.payload_size > target_size) {
                target_size = resident_view.payload_size;
            }
        }
        wire_payload = load_padded;
        wire_payload_size = target_size;
        emit_process_image = true;
        dpll_observation_ready = false;
    }
    const tdma_transport_frame_build_t build = {
        .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id = adapter->config.local_slot_id,
        .transport_sequence = sequence,
        .payload_class = (maintenance_frame || load_frame)
                             ? TDMA_PAYLOAD_CLASS_CONFIG_CONTROL
                             : (emit_process_image
                                    ? TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE
                                    : TDMA_PAYLOAD_CLASS_IDLE_BEACON),
        .flags = (maintenance_frame || load_frame)
                     ? 0u
                     : (emit_process_image
                            ? (TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK |
                               TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE)
                            : TDMA_TRANSPORT_FLAG_IDLE_BEACON),
        .schedule_crc32 = adapter->config.schedule_crc32,
        .ring_profile_crc32 = adapter->config.ring_profile_crc32,
        .hop_limit = adapter->config.node_count - 1u,
        .payload = wire_payload,
        .payload_size = wire_payload_size,
    };
    adapter->transport_load_build_class = build.payload_class;
    adapter->transport_load_build_size = (uint32_t)build.payload_size;
    adapter->transport_load_build_load = load_frame ? 1u : 0u;
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t packet_size = 0u;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_encode(&build,
                                     packet,
                                     sizeof(packet),
                                     &packet_size,
                                     &result)) {
        adapter->transport_load_tx_stage = 1u;
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    tdma_transport_frame_view_t view;
    if (!tdma_transport_frame_decode(packet,
                                     packet_size,
                                     &view,
                                     &result)) {
        adapter->transport_load_tx_stage = 2u;
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }
    const bool launch_ok = tdma_pio_spi_ring_adapter_launch_reference(
        adapter,
        packet,
        packet_size,
        &view,
        dpll_observation_ready,
        false,
        launched);
    adapter->transport_load_tx_stage = launch_ok
        ? (adapter->transport_load_tx_stage == 9u
               ? 10u : adapter->transport_load_tx_stage)
        : (adapter->transport_load_tx_stage == 0u ? 3u :
           adapter->transport_load_tx_stage);
    return launch_ok;
}

#include "tdma_pio_spi_ring_origin.h"

bool tdma_pio_spi_ring_adapter_process_rx_impl(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t rx_timestamp_ns,
    const tdma_origin_observation_t *origin_observation,
    const tdma_rx_prepare_t *prepared)
{
    adapter->last_rx_gate_accepted = false;
    adapter->last_rx_new_segment_mask = 0u;
    /* Receive-health age belongs to the adapter service clock domain. The
     * physical RX timestamp is a separate hardware-latch domain used for
     * RTT/FIFO evidence and cannot be subtracted from service now_ns. */
    const uint64_t health_observation_ns = prepared != NULL ? prepared->capture_service_ns :
        adapter->last_service_ns != 0ull
        ? adapter->last_service_ns
        : rx_timestamp_ns;
    uint32_t hardware_round_trip_ns = prepared != NULL ? prepared->round_trip_ns : 0u;
    uint32_t hardware_resolution_ns = prepared != NULL ? prepared->resolution_ns : 0u;
    uint32_t hardware_flags = prepared != NULL ? prepared->flags : 0u;
    const bool hardware_round_trip_valid = prepared != NULL ? prepared->round_trip_valid :
        adapter->origin.active == 0u &&
        adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
        adapter->phys_feedback != NULL &&
        adapter->phys_feedback(adapter->phys_context,
                               &hardware_round_trip_ns,
                               &hardware_resolution_ns,
                               &hardware_flags);
    bool dpll_correlation_pending = false;
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    const bool decoded = prepared != NULL ? prepared->decoded : tdma_transport_frame_decode(packet,
                                                     packet_size,
                                                     &view,
                                                     &result);
    if (prepared != NULL) {
        view = prepared->view;
        result = prepared->result;
    }
    if (decoded) {
        /* Classification is a diagnostic projection of the frame already
         * decoded above. It must not add a second decode to the realtime RX
         * path: the per-packet CRC/parse cost there starved the ring service
         * and the VDC run-output refill on the four-board 60 s observation. */
        if (view.payload_class == TDMA_PAYLOAD_CLASS_CONFIG_CONTROL) {
            adapter->rx_class_config_control++;
        } else if (view.payload_class == TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE) {
            adapter->rx_class_process_image++;
        } else {
            adapter->rx_class_other++;
        }
    }
    if (decoded && view.payload_class == TDMA_PAYLOAD_CLASS_CONFIG_CONTROL) {
        adapter->transport_load_rx_seen++;
        if (adapter->transport_load != NULL && view.payload != NULL &&
            view.payload_size >=
                adapter->transport_load->descriptor.max_frame_size &&
            adapter->transport_load_rx != NULL) {
            adapter->transport_load_rx_delivered++;
            adapter->transport_load_rx(adapter->transport_load_rx_context,
                                       view.payload,
                                       view.payload_size);
        }
    }
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
        if (prepared != NULL) {
            const tdma_rx_diagnostic_t *d = &prepared->diagnostic;
            adapter->last_bad_header_diff_count = d->header_diff_count;
            adapter->last_bad_header_first_diff_offset = d->header_first_diff_offset;
            adapter->last_bad_header_expected_byte = d->header_expected_byte;
            adapter->last_bad_header_observed_byte = d->header_observed_byte;
            adapter->last_bad_packet_diff_count = d->packet_diff_count;
            adapter->last_bad_packet_first_diff_offset = d->packet_first_diff_offset;
            adapter->last_bad_packet_expected_byte = d->packet_expected_byte;
            adapter->last_bad_packet_observed_byte = d->packet_observed_byte;
            adapter->last_bad_clock_evidence = d->clock_evidence;
            adapter->last_bad_expected_transport_crc32 = d->expected_transport_crc32;
            adapter->last_bad_observed_transport_crc32 = d->observed_transport_crc32;
            adapter->last_bad_recomputed_transport_crc32 = d->recomputed_transport_crc32;
            adapter->last_bad_expected_payload_crc32 = d->expected_payload_crc32;
            adapter->last_bad_observed_payload_crc32 = d->observed_payload_crc32;
        } else if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
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

    if (adapter->origin.active != 0u &&
        !tdma_pio_spi_ring_origin_pair_valid(adapter, &view, origin_observation, prepared)) {
        adapter->origin.rejected_observation_count++;
        tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_GATE_REJECT);
        return false;
    }
    bool receive_health_rejected = false;
    uint32_t inspected_present_mask = 0u;
    uint32_t inspected_new_mask = 0u;
    if ((!receive_health_rejected ||
         adapter->receive_health.configured == 0u) &&
        view.payload_class == TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
        adapter->receive_health.configured != 0u) {
        const uint64_t rx_inspect_start = tdma_service_timing_now();
        tdma_flight_engine_unload_t unloaded;
        const uint32_t expected_owner_mask =
            tdma_pio_spi_ring_adapter_expected_owner_mask(
                adapter, &adapter->config);
        const bool inspected = tdma_flight_engine_rx_unload(
            adapter->flight_engine,
            view.payload,
            view.payload_size,
            expected_owner_mask,
            &unloaded);
        inspected_present_mask = unloaded.present_segment_mask;
        inspected_new_mask = unloaded.new_segment_mask;
        tdma_service_timing_record(TDMA_TIMING_RX_INSPECT, rx_inspect_start);
        const uint64_t rx_health_start = tdma_service_timing_now();
        tdma_receive_reason_t receive_reason = TDMA_RECEIVE_REASON_NONE;
        const bool receive_health_accepted = tdma_receive_health_evaluate(
            &adapter->receive_health,
            &view,
            result,
            inspected_present_mask,
            health_observation_ns,
            &receive_reason);
        tdma_service_timing_record(TDMA_TIMING_RX_HEALTH, rx_health_start);
        adapter->last_rx_gate_accepted =
            inspected && receive_health_accepted;
        adapter->last_rx_new_segment_mask = inspected_new_mask;
        receive_health_rejected = !adapter->last_rx_gate_accepted;
        if (receive_health_rejected) {
            tdma_pio_spi_ring_adapter_set_error(
                adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_GATE_REJECT);
        }
    }

    const uint64_t rx_evidence_start = tdma_service_timing_now();
    if (adapter->origin.active != 0u) {
        if (receive_health_rejected) {
            adapter->origin.rejected_observation_count++;
            tdma_service_timing_record(TDMA_TIMING_RX_EVIDENCE, rx_evidence_start);
            return false;
        }
        tdma_pio_spi_ring_origin_accept(adapter, origin_observation);
        /* One accepted own-frame return is exactly one ring revolution: the
         * origin emits its next frame from this return. */
        tdma_pio_spi_ring_adapter_note_origin_return(adapter);
        if (prepared != NULL) {
            adapter->origin.matched_owner_generation = prepared->origin_owner_generation;
            adapter->origin.matched_owner_sequence = prepared->origin_owner_sequence;
        }
    }
    uint32_t resident_feedback_conditions = 0u;
    if (tdma_pio_spi_ring_adapter_resident_process_image(adapter)) {
        resident_feedback_conditions |=
            TDMA_PIO_SPI_RESIDENT_FEEDBACK_RESIDENT;
    }
    if (adapter->resident_seeded) {
        resident_feedback_conditions |= TDMA_PIO_SPI_RESIDENT_FEEDBACK_SEEDED;
    }
    if (view.origin_slot_id == adapter->config.local_slot_id) {
        resident_feedback_conditions |= TDMA_PIO_SPI_RESIDENT_FEEDBACK_ORIGIN;
    }
    if (view.hop_count == view.hop_limit) {
        resident_feedback_conditions |= TDMA_PIO_SPI_RESIDENT_FEEDBACK_HOP;
    }
    if ((view.flags & TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK) != 0u) {
        resident_feedback_conditions |= TDMA_PIO_SPI_RESIDENT_FEEDBACK_REQUIRED;
    }
    if (view.transport_sequence == adapter->up_sequence) {
        resident_feedback_conditions |= TDMA_PIO_SPI_RESIDENT_FEEDBACK_SEQUENCE;
    }
    if (view.identity_crc32 == adapter->up_tx_frame_crc32) {
        resident_feedback_conditions |= TDMA_PIO_SPI_RESIDENT_FEEDBACK_IDENTITY;
    }
    adapter->last_rx_origin_slot_id = view.origin_slot_id;
    adapter->last_rx_hop_count = view.hop_count;
    adapter->last_rx_hop_limit = view.hop_limit;
    adapter->last_rx_flags = view.flags;
    adapter->last_rx_sequence = view.transport_sequence;
    adapter->last_rx_identity_crc32 = view.identity_crc32;
    adapter->resident_feedback_condition_mask = resident_feedback_conditions;
    const bool resident_feedback = resident_feedback_conditions ==
        TDMA_PIO_SPI_RESIDENT_FEEDBACK_ALL;

    adapter->down_rx_sequence = view.transport_sequence;
    adapter->down_rx_frame_crc32 = view.identity_crc32;
    adapter->feedback_rx_timestamp_ns = rx_timestamp_ns;
    if (rx_timestamp_ns != 0ull && view.transport_sequence != 0u &&
        view.payload_class == TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
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

        /* In physical cut-through mode the local forwarding edge occurred
         * before the completed frame reached this parser. Drain every frame's
         * latch here, prior to optional DPLL correlation, so a frame without
         * a DPLL trailer cannot leave stale provenance for the next one. */
        if (adapter->role != TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
            (adapter->forwarding_mode ==
                 TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT ||
             adapter->forwarding_mode ==
                 TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE)) {
            if (prepared == NULL) {
                tdma_pio_spi_ring_adapter_sample_local_tx_edge(adapter, true);
            } else {
                const tdma_ring_local_tx_edge_evidence_t *e = &prepared->local_tx;
                if (!prepared->local_tx_captured || e->timestamp_ns == 0ull) {
                    adapter->local_tx_edge_miss_count++;
                } else {
                    adapter->local_tx_edge_count++;
                    adapter->local_tx_edge_timestamp_ns = e->timestamp_ns;
                    adapter->local_tx_edge_sequence = e->sequence;
                    adapter->local_tx_edge_identity_crc32 = e->identity_crc32;
                    adapter->local_tx_edge_capture_generation = e->capture_generation;
                    adapter->local_tx_edge_flags = e->flags;
                    if ((e->flags & TDMA_RING_LOCAL_TX_EDGE_FLAG_SEQUENCE_BOUND) == 0u ||
                        (e->flags & TDMA_RING_LOCAL_TX_EDGE_FLAG_IDENTITY_BOUND) == 0u)
                        adapter->local_tx_edge_unpaired_count++;
                    adapter->local_tx_edge_evidence[evidence_index] = *e;
                }
            }
        }

        if (adapter->clock_evidence_enabled != 0u &&
            tdma_pio_spi_ring_product_payload(adapter, view.payload_size) &&
            view.payload != NULL) {
            const uint32_t encoded = tdma_pio_spi_ring_get_u32(
                &view.payload[
                    view.payload_size - TDMA_FLIGHT_DPLL_OBSERVATION_SIZE]);
            adapter->clock_evidence_last_rx_encoded = encoded;
            if ((encoded &
                 TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_VALID_MASK) != 0u) {
                if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) {
                    dpll_correlation_pending = true;
                } else {
                    if (!tdma_pio_spi_ring_adapter_correlate_dpll_observation(
                            adapter, &view)) {
                        adapter->clock_observation_reject_count++;
                    }
                    dpll_correlation_pending = false;
                }
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
                if (view.transport_sequence != 0u) {
                    const uint32_t rx_index = view.transport_sequence %
                        TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH;
                    adapter->local_rx_evidence[rx_index].timestamp_ns =
                        adapter->feedback_rx_timestamp_ns;
                    adapter->local_rx_evidence[rx_index].valid = true;
                }
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
    /* The autonomous origin owns its own pair: it has no phys_tx/phys_rx
     * latch, so the reference block above deliberately leaves it empty.  The
     * accepted own-frame return just handled supplies both endpoints from the
     * shared timestamp clock; an unpaired or non-advancing return publishes
     * nothing and the runtime keeps reporting TIMESTAMP_MISSING. */
    if (adapter->origin.active != 0u && origin_observation != NULL) {
        tdma_pio_spi_ring_origin_correlate_feedback(adapter, &view,
            origin_observation->local_generation);
    }
    if (dpll_correlation_pending &&
        !tdma_pio_spi_ring_adapter_correlate_dpll_observation(adapter, &view)) {
        adapter->clock_observation_reject_count++;
    }
    tdma_service_timing_record(TDMA_TIMING_RX_EVIDENCE, rx_evidence_start);
    adapter->rx_count++;
    adapter->total_rx_ok++;
    if (view.origin_slot_id < TDMA_PIO_SPI_RING_ADAPTER_SLOT_MAX) {
        const uint32_t slot = view.origin_slot_id;
        const uint32_t hop = view.hop_count > 0xffu ? 0xffu : view.hop_count;
        adapter->rx_slot_count[slot]++;
        adapter->rx_slot_mask |= (uint32_t)1u << slot;
        if (adapter->rx_slot_hop_min[slot] == 0xffu ||
            hop < adapter->rx_slot_hop_min[slot]) {
            adapter->rx_slot_hop_min[slot] = (uint8_t)hop;
        }
        if (hop > adapter->rx_slot_hop_max[slot]) {
            adapter->rx_slot_hop_max[slot] = (uint8_t)hop;
        }
    }
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
    /* On-cadence publish policy: when the reference is actively emitting and
     * the received frame carried no new segment, publish what the frame DOES
     * carry (present/output mask) so the consumer can see it.  Committing mask
     * 0 would be a no-op for both the refmem consumer and rx_commit. */
    const uint32_t cadence_mask =
        (input_mask == 0u && adapter->config.origin_cadence_enabled != 0u &&
         adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
         adapter->comm_fsm.state == TDMA_ADAPTER_COMM_STATE_AUTONOMOUS)
            ? inspected_present_mask : 0u;
    const uint32_t publish_mask = input_mask != 0u ? input_mask : cadence_mask;
        if (adapter->receive_health.configured == 0u &&
            adapter->flight_engine != NULL &&
            tdma_flight_engine_is_active(adapter->flight_engine)) {
            (void)tdma_flight_engine_classify_input(adapter->flight_engine,
                                                    view.payload,
                                                    view.payload_size,
                                                    &input_mask);
        }
        if ((!receive_health_rejected ||
             adapter->receive_health.configured == 0u ||
             resident_feedback) &&
            (publish_mask != 0u ||
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
            const uint64_t rx_publish_start = tdma_service_timing_now();
            const bool published = tdma_flight_fifo_core1_publish_rx(
                adapter->flight_fifo,
                view.payload,
                view.payload_size,
                rx_generation,
                view.transport_sequence,
                publish_mask,
                rx_timestamp_ns,
                rx_quality);
            tdma_service_timing_record(TDMA_TIMING_RX_FIFO_PUBLISH, rx_publish_start);
            if (published && input_mask != 0u &&
                adapter->flight_engine != NULL &&
                tdma_flight_engine_is_active(adapter->flight_engine)) {
                const uint64_t rx_commit_start = tdma_service_timing_now();
                (void)tdma_flight_engine_rx_commit(
                    adapter->flight_engine,
                    view.payload,
                    view.payload_size,
                    input_mask);
                tdma_service_timing_record(TDMA_TIMING_RX_COMMIT, rx_commit_start);
            }
        }
    }
    const uint64_t rx_complete_start = tdma_service_timing_now();
    if (packet_size <= sizeof(adapter->last_rx_packet) &&
        (!tdma_pio_spi_ring_adapter_resident_process_image(adapter) ||
         resident_feedback)) {
        memcpy(adapter->last_rx_packet, packet, packet_size);
        adapter->last_rx_packet_size = packet_size;
    }
    if (resident_feedback && adapter->origin.active == 0u && !adapter->resident_return_ready) {
        adapter->resident_return_new_segment_mask = inspected_new_mask;
        adapter->resident_return_ready = true;
        if (!tdma_adapter_comm_fsm_dispatch(
                &adapter->comm_fsm,
                TDMA_ADAPTER_COMM_EVENT_DATA_RX_COMPLETED,
                0u)) {
            adapter->resident_return_ready = false;
            tdma_pio_spi_ring_adapter_set_error(
                adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
            tdma_service_timing_record(TDMA_TIMING_RX_COMPLETE, rx_complete_start);
            return false;
        }
        adapter->resident_pending_completed_cycle = view.transport_sequence;
        adapter->resident_pending_completed_segment_mask =
            inspected_present_mask;
        adapter->resident_completion_pending = true;
        tdma_pio_spi_ring_adapter_commit_resident_completion(adapter);
    }
    if (!receive_health_rejected) {
        adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    }
    adapter->last_rx_service_ns = health_observation_ns;
    tdma_service_timing_record(TDMA_TIMING_RX_COMPLETE, rx_complete_start);
    return true;
}

static bool tdma_pio_spi_ring_adapter_process_rx(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t rx_timestamp_ns,
    const tdma_origin_observation_t *origin_observation)
{
    const uint64_t started = tdma_service_timing_now();
    const bool result = tdma_pio_spi_ring_adapter_process_rx_impl(
        adapter, packet, packet_size, rx_timestamp_ns, origin_observation, NULL);
    tdma_service_timing_record(TDMA_TIMING_RX_PARSE, started);
    return result;
}

/* FORWARD-node emission: re-emit the frame received from the previous board
 * toward the next board, keeping origin/sequence/identity CRC unchanged and
 * advancing hop/transport CRC. The feedback returns only to the reference
 * node, so this path proves ring service (up/down running) without
 * fabricating simultaneous_feedback_loop_evidence. */
bool tdma_pio_spi_ring_adapter_tx_forward(
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
        if (tdma_flight_engine_tx_load(
                adapter->flight_engine,
                incoming_view.payload,
                incoming_view.payload_size,
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
                 adapter->last_rx_new_segment_mask != 0u) {
    /* NOTE: the on-cadence publish policy must be decided inside the
     * reference-node branch (the origin service path); this forwarding path is
     * only reached when the origin is inactive and role != REFERENCE, so a
     * cadence exemption here would be dead code. */
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
                    adapter->last_rx_new_segment_mask,
                    adapter->feedback_rx_timestamp_ns,
                    rx_quality);
                if (published) {
                    (void)tdma_flight_engine_rx_commit(
                        adapter->flight_engine,
                        incoming_view.payload,
                        incoming_view.payload_size,
                        adapter->last_rx_new_segment_mask);
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
    adapter->total_tx_ok++;
    adapter->last_error = flight_map_rejected
                              ? TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT
                              : TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    return true;
}

#include "tdma_pio_spi_ring_rx_prepare.h"

_Static_assert(TDMA_RX_PREPARE_IDLE == 0u && TDMA_RX_PREPARE_REQUESTED == 1u &&
    TDMA_RX_PREPARE_BUILDING == 2u && TDMA_RX_PREPARE_READY == 3u &&
    TDMA_RX_PREPARE_CANCELLED + 1u == TDMA_RX_TIMING_STATION_STATES,
    "RX profile state indices must match station ownership states");

/* Keep capture/request state separate from accepting an already prepared RX.
 * These locals must not remain live across the process-image inspection. */
__attribute__((noinline)) bool tdma_pio_spi_ring_adapter_rx_capture(
    tdma_pio_spi_ring_adapter_t *adapter, uint8_t *packet, size_t packet_capacity)
{
    size_t packet_size = 0u;
    uint64_t rx_timestamp_ns = 0ull;
    tdma_rx_prepare_t *job = adapter->rx_preparation;

    if (adapter->rx_queue_count != 0u) {
        if (!tdma_pio_spi_ring_adapter_queue_pop(adapter,
                                                 packet,
                                                 packet_capacity,
                                                 &packet_size,
                                                 &rx_timestamp_ns)) {
            return false;
        }
        return tdma_pio_spi_ring_adapter_process_rx(adapter,
                                                    packet,
                                                    packet_size,
                                                    rx_timestamp_ns, NULL);
    }

    if (job != NULL) {
        job->capture = (tdma_rx_capture_t){0};
        job->capture_observer_epoch = 0u;
    }
    if (adapter->phys_rx == NULL && adapter->phys_rx_ex == NULL) {
        return false;
    }
    const uint64_t capture_start = tdma_service_timing_now();
    const bool captured = adapter->phys_rx_ex != NULL
        ? adapter->phys_rx_ex(adapter->phys_context,
                             packet, packet_capacity,
                             &packet_size, &rx_timestamp_ns,
                             job != NULL ? &job->capture : NULL)
        : adapter->phys_rx(adapter->phys_context,
                                           packet,
                                           packet_capacity,
                                           &packet_size,
                                           &rx_timestamp_ns);
    tdma_service_timing_record(TDMA_TIMING_RX_CAPTURE, capture_start);
    if (!captured) {
        if (job != NULL) job->capture = (tdma_rx_capture_t){0};
        return false;
    }
    tdma_origin_observation_t observation;
    const bool paired = adapter->origin.active != 0u &&
        adapter->phys_origin.take_rx_observation(adapter->phys_ctrl_context, &observation);
    if (job != NULL) {
        const uint64_t request_start = tdma_service_timing_now();
        if (adapter->phys_rx_event_pin != NULL &&
            job->capture.flags == TDMA_RX_CAPTURE_PRIVATE_COPY)
            job->capture_observer_epoch = adapter->phys_rx_event_pin(
                adapter->phys_context, &job->capture);
        if (!tdma_pio_spi_ring_rx_request(adapter, job, packet_size, rx_timestamp_ns,
            paired ? &observation : NULL)) {
            job->capture = (tdma_rx_capture_t){0};
            job->capture_observer_epoch = 0u;
        }
        tdma_service_timing_record(TDMA_TIMING_RX_REQUEST, request_start);
        return false; /* Admission is not an accepted receive. */
    }
    return tdma_pio_spi_ring_adapter_process_rx(adapter,
                                                packet,
                                                packet_size,
                                                adapter->origin.active != 0u ? 0ull : rx_timestamp_ns,
                                                paired ? &observation : NULL);
}
