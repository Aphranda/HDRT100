#include "distributed_refmem.h"
#include "distributed_refmem_internal.h"

#include <string.h>

#include "drv_flash.h"
#include "osal.h"
#include "board_config.h"
#include "board_identity.h"
#include "project_config.h"

#include "distributed_refmem_vdc_bridge.h"
#include "diagnostics.h"
#include "ota_ao.h"
#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_cmd_load.h"
#include "refmem_lease_table.h"
#include "refmem_control_route.h"
#include "refmem_node_control.h"
#include "refmem_param_pool.h"
#include "refmem_cmd_frame.h"
#include "refmem_cmd_reply.h"
#include "tdma_maintenance_runtime.h"
#include "refmem_node_load_sync.h"
#include "refmem_quality.h"
#include "refmem_realtime_tdma.h"
#include "tdma_runtime_owner.h"
#include "refmem_slot_claim.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_sync.h"
#include "refmem_sync_vdc_feedback.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "tdma_process_image_layout.h"
#include "tdma_process_image_map.h"
#include "tdma_profile.h"
#include "trigger_position_mailbox.h"
#include "vdc_dpll_manager.h"

_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE ==
                   TDMA_PROCESS_IMAGE_REFMEM_SIZE + TDMA_PROCESS_IMAGE_VDC_SIZE,
               "A T2 position record spans the freed VDC observation area plus the RefMem region");

/* RAM placement macros live in a header: the split command units need them. */
#include "distributed_refmem_time_critical.h"

#include "distributed_refmem_split_internal.h"
void distributed_refmem_tdma_flight_parse_mailbox(
    const uint8_t *mailbox,
    size_t mailbox_size)
{
    if (mailbox == NULL ||
        mailbox_size < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE) {
        s_tdma_flight_sync.rx_bad_mailbox_count++;
        return;
    }
    if (distributed_refmem_get_le16(&mailbox[0]) !=
        DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC) {
        s_tdma_flight_sync.rx_empty_count++;
        return;
    }
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
    if (mailbox[3] == TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS) {
        distributed_refmem_tdma_flight_parse_vdc_fragment(mailbox, mailbox_size);
        return;
    }
#endif
    if (!tdma_process_image_transport_class_valid(mailbox[3])) {
        s_tdma_flight_sync.rx_bad_mailbox_count++;
        s_tdma_flight_sync.last_error = 14u;
        return;
    }

    uint8_t frame[REFMEM_SYNC_FRAME_HEADER_SIZE +
                  sizeof(refmem_sync_delta_header_t) + sizeof(uint32_t)];
    size_t frame_size = 0u;
    if (!distributed_refmem_tdma_flight_expand_compact_delta(mailbox,
                                                             mailbox_size,
                                                             frame,
                                                             sizeof(frame),
                                                             &frame_size)) {
        s_tdma_flight_sync.rx_bad_mailbox_count++;
        s_tdma_flight_sync.last_error = 2u;
        return;
    }

    refmem_sync_frame_header_t header;
    if (refmem_sync_frame_decode_header(frame,
                                        frame_size,
                                        &header) != REFMEM_SYNC_FRAME_OK ||
        header.source_slot >= REFMEM_SYNC_NODE_COUNT) {
        s_tdma_flight_sync.rx_bad_mailbox_count++;
        s_tdma_flight_sync.last_error = 7u;
        return;
    }
    const uint32_t source_bit = 1u << header.source_slot;
    refmem_sync_rx_snapshot_t rx;
    const refmem_sync_rx_result_t result =
        refmem_sync_delta_receive_frame(&s_tdma_flight_sync.context,
                                  frame,
                                  frame_size,
                                  &rx);
    s_tdma_flight_sync.rx_seen_mask |= source_bit;
    s_tdma_flight_sync.rx_last_seq_by_source[header.source_slot] = header.seq32;
    s_tdma_flight_sync.last_rx_result = result;
    s_tdma_flight_sync.last_frame_type = rx.header.frame_type;
    s_tdma_flight_sync.last_source_slot = rx.source_slot;
    s_tdma_flight_sync.last_seq32 = rx.header.seq32;
    if (mailbox[3] == TDMA_PROCESS_IMAGE_MESSAGE_CLASS) {
    s_tdma_flight_sync.last_vdc_phase_offset_ns =
        tdma_process_image_expand_i16(
            distributed_refmem_get_i16(
                &mailbox[TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET]),
            TDMA_PROCESS_IMAGE_VDC_PHASE_QUANTUM_NS);
    s_tdma_flight_sync.last_vdc_rate_adjust_ppb =
        tdma_process_image_expand_i16(
            distributed_refmem_get_i16(
                &mailbox[TDMA_PROCESS_IMAGE_VDC_RATE_OFFSET]),
            TDMA_PROCESS_IMAGE_VDC_RATE_QUANTUM_PPB);
    s_tdma_flight_sync.last_vdc_lock_state =
        mailbox[TDMA_PROCESS_IMAGE_VDC_LOCK_OFFSET];
    s_tdma_flight_sync.last_vdc_quality =
        mailbox[TDMA_PROCESS_IMAGE_VDC_QUALITY_OFFSET];
    }
    s_tdma_flight_sync.last_ack_seq16 = distributed_refmem_get_le16(
        &mailbox[TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET]);
    s_tdma_flight_sync.last_ack_flags =
        mailbox[TDMA_PROCESS_IMAGE_ACK_FLAGS_OFFSET];
    s_tdma_flight_sync.last_control_opcode =
        mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET];
    s_tdma_flight_sync.last_control_seq8 =
        mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET];
    s_tdma_flight_sync.last_optional_diagnostic =
        mailbox[TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET];
    s_tdma_flight_sync.last_mailbox_crc16 = distributed_refmem_get_le16(
        &mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET]);
    const uint8_t ack_diag =
        (uint8_t)s_tdma_flight_sync.last_optional_diagnostic;
    if ((s_tdma_flight_sync.last_ack_flags &
         TDMA_PROCESS_IMAGE_ACK_FLAG_VALID) != 0u &&
        (s_tdma_flight_sync.last_ack_flags &
         TDMA_PROCESS_IMAGE_ACK_FLAG_COMMAND) == 0u &&
        (ack_diag & 0xF0u) == 0xA0u) {
        /* N3.2.4 maintenance-frame ACK: seq16 is stream_id:generation and the
         * diagnostic low bit is the result.  It is not a command reply. */
        const uint8_t ack_stream_id =
            (uint8_t)(s_tdma_flight_sync.last_ack_seq16 >> 8u);
        const uint8_t ack_generation =
            (uint8_t)(s_tdma_flight_sync.last_ack_seq16 & 0xFFu);
        (void)tdma_maintenance_runtime_ack(ack_stream_id, ack_generation,
                                           header.source_slot,
                                           (ack_diag & 1u) == 0u);
    } else {
        distributed_refmem_cmd_reply_store(
            header.source_slot, s_tdma_flight_sync.last_ack_seq16,
            s_tdma_flight_sync.last_ack_flags, ack_diag);
    }
    if (result == REFMEM_SYNC_RX_ACCEPTED) {
        s_tdma_flight_sync.rx_accept_count++;
        s_tdma_flight_sync.last_error = 0u;
        if (rx.header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_DELTA &&
            rx.payload != NULL &&
            rx.payload_size >=
                (uint16_t)(sizeof(refmem_sync_delta_header_t) + sizeof(uint32_t))) {
            const uint8_t *value =
                &rx.payload[sizeof(refmem_sync_delta_header_t)];
            s_tdma_flight_sync.last_value_u32 =
                ((uint32_t)value[0]) |
                ((uint32_t)value[1] << 8u) |
                ((uint32_t)value[2] << 16u) |
                ((uint32_t)value[3] << 24u);
        }
    } else if (result == REFMEM_SYNC_RX_DUPLICATE_SEQ) {
        s_tdma_flight_sync.rx_duplicate_skip_count++;
        s_tdma_flight_sync.last_error = 0u;
    } else {
        s_tdma_flight_sync.rx_reject_count++;
        s_tdma_flight_sync.last_error = 3u;
    }
}

void distributed_refmem_tdma_flight_sync_publish(
    tdma_service_service_t *owner,
    const tdma_ring_runtime_snapshot_t *ring)
{
    if (owner == NULL || ring == NULL ||
        ring->enabled == 0u ||
        ring->adapter_started == 0u ||
        ring->data_enabled == 0u ||
        ring->node_count == 0u ||
        ring->node_count > DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT ||
        ring->local_slot_id >= ring->node_count) {
        return;
    }

    const uint32_t now_ms = osal_tick_ms();
    uint32_t publish_interval_ms = s_tdma_flight_sync.publish_interval_ms;
    int feedback = -2;
    if (distributed_refmem_feedback_binding()->rate_mode == VDC_BOUNDARY_MODE_REFERENCE) {
        /* Binding refresh/STOP cancellation already ran in the service.
         * Revalidate an active group's ARM before any pacing/backpressure
         * return; do not freeze a new event until publication is due. */
        if (distributed_refmem_feedback_tx_active()) {
            feedback = distributed_refmem_feedback_prepare(owner);
            if (feedback < 0) {
                if (s_tdma_flight_sync.position_tx_active) {
                    s_tdma_flight_sync.position_tx_phase =
                        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_FEEDBACK;
                    s_tdma_flight_sync.position_tx_wait_reason =
                        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FEEDBACK;
                    s_tdma_flight_sync.position_tx_wait_count++;
                }
                return;
            }
        }
        if (publish_interval_ms < DISTRIBUTED_REFMEM_REFERENCE_PUBLISH_INTERVAL_MS)
            publish_interval_ms = DISTRIBUTED_REFMEM_REFERENCE_PUBLISH_INTERVAL_MS;
    }
    const uint32_t ring_interval_ms =
        (ring->feedback_timeout_ns + 999999u) / 1000000u;
    if (ring_interval_ms > publish_interval_ms) {
        publish_interval_ms = ring_interval_ms;
    }
    const bool normal_due =
        now_ms - s_tdma_flight_sync.last_publish_ms >= publish_interval_ms;
    /* The transmit side pulls: taking a value here already required a free seat, so the
     * boarding below cannot be refused. */
    (void)distributed_refmem_position_tx_prepare(owner);
    if (!normal_due && !s_tdma_flight_sync.position_tx_active) {
        return;
    }

    /* TX is a latest-value process image, not an event log. Keep at most one
     * prepared image behind the core1-active image and coalesce while that
     * descriptor is pending. Calling publish against the occupied two-buffer
     * state turns normal bounded backpressure into a reject counter and can
     * also create artificial sequence gaps. */
    tdma_flight_fifo_snapshot_t fifo;
    if (!tdma_service_get_flight_fifo_snapshot(owner, &fifo)) {
        if (s_tdma_flight_sync.position_tx_active) {
            s_tdma_flight_sync.position_tx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_RESOURCE;
            s_tdma_flight_sync.position_tx_wait_reason =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FIFO_SNAPSHOT;
            s_tdma_flight_sync.position_tx_wait_count++;
        }
        return;
    }
    /* One ready slot may contain the higher-priority VDC image. The second
     * slot is the bounded FIFO handoff for a lower-priority T2 image; FIFO
     * order keeps VDC ahead without starving T2. Only a full two-slot queue
     * is resource backpressure. */
    if (fifo.tx_ready_count >= TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT) {
        if (s_tdma_flight_sync.position_tx_active) {
            s_tdma_flight_sync.position_tx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_RESOURCE;
            s_tdma_flight_sync.position_tx_wait_reason =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FIFO_READY;
            s_tdma_flight_sync.position_tx_wait_count++;
        }
        return;
    }
    if (feedback == -2) feedback = distributed_refmem_feedback_prepare(owner);
    if (feedback < 0) {
        if (s_tdma_flight_sync.position_tx_active) {
            s_tdma_flight_sync.position_tx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_FEEDBACK;
            s_tdma_flight_sync.position_tx_wait_reason =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FEEDBACK;
            s_tdma_flight_sync.position_tx_wait_count++;
        }
        return; /* Busy snapshot: do not splice an ordinary frame. */
    }
    /* A position-only idle-cycle publication must not move the VDC cadence;
     * the next normal frame remains due at the original deadline. */

    uint8_t frame[DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE];
    size_t frame_size = 0u;
    const uint32_t seq32 = s_tdma_flight_sync.next_seq32;
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
    vdc_dpll_manager_refmem_snapshot_t refmem_snapshot;
    const bool resident_master =
        vdc_dpll_manager_get_refmem_snapshot(&refmem_snapshot) &&
        refmem_snapshot.control_profile.valid == 1u &&
        refmem_snapshot.control_profile.mode ==
            VDC_DPLL_CONTROL_MODE_MASTER &&
        distributed_refmem_tdma_prepare_vdc_command_record(ring);
    if (!resident_master &&
        (!vdc_dpll_manager_get_refmem_snapshot(&refmem_snapshot) ||
         refmem_snapshot.control_profile.mode != VDC_DPLL_CONTROL_MODE_MASTER)) {
        s_tdma_flight_sync.vdc_command_record_active = 0u;
        s_tdma_flight_sync.vdc_command_record_fragment_index = 0u;
    }
#else
    const bool resident_master = false;
#endif
    /* VDC command/feedback owns the frame. T2 is admitted only when the
     * higher-priority VDC path has no pending record.
     *
     * REVERTED RULE (2026-10-17): "T2 fills idle cycles only" (`&& !normal_due`) deadlocks.
     * `last_publish_ms` advances only when a frame is published; if a queued image gets
     * stuck (Core1 not boarding, FIFO ready stays 1) the ordinary path stops, `normal_due`
     * then stays true for ever, and T2 - the only path that bypasses the FIFO - stops with
     * it. Measured: two full runs with tx_publish 0 for 313-568 s and NO DELIVERY, while
     * the position table kept merging (22,788 coalesced). The VDC-cadence cost of letting
     * T2 take a due cycle is bounded by T_drain instead, and the reason recorded below is
     * now explicit about which priority blocked the record. */
    const bool position_publication =
        s_tdma_flight_sync.position_tx_active && !resident_master && feedback <= 0;
    if (s_tdma_flight_sync.position_tx_active && !position_publication) {
        s_tdma_flight_sync.position_tx_phase =
            resident_master || feedback > 0
                ? DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_VDC
                : DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_FEEDBACK;
        /* Distinguish "a VDC frame owns this cycle" from "the ordinary flight FIFO is
         * holding an image that Core1 has not boarded": the two have different owners and
         * the old single FAILURE_FEEDBACK label made the bench log point at the wrong one. */
        s_tdma_flight_sync.position_tx_wait_reason =
            resident_master || feedback > 0
                ? DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FEEDBACK
                : DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FIFO_READY;
        s_tdma_flight_sync.position_tx_wait_count++;
    }
    const bool reply_publication =
        distributed_refmem_cmd_reply_pending() &&
        !position_publication && !resident_master && feedback <= 0;
    const bool maint_ack_publication =
        !reply_publication &&
        refmem_cmd_load_peek_maintenance_ack(NULL, NULL, NULL) &&
        !position_publication && !resident_master && feedback <= 0;
    const uint8_t message_class =
        (reply_publication || maint_ack_publication)
        ? TDMA_PROCESS_IMAGE_MESSAGE_CLASS
        : position_publication
        ? TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS
        : resident_master
        ? TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS
        : (feedback == 2 ? TDMA_PROCESS_IMAGE_VDC_BOUNDARY_COMMAND_MESSAGE_CLASS
            : (feedback == 1 ? TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS
                             : TDMA_PROCESS_IMAGE_MESSAGE_CLASS));
    const uint32_t target_mask = maint_ack_publication
        ? (1u << s_tdma_flight_sync.reference_slot)
        : reply_publication
        ? distributed_refmem_cmd_reply_target_mask()
        : position_publication
        ? s_tdma_flight_sync.position_tx_target_mask
        : resident_master
        ? (s_tdma_flight_sync.active_mask &
           ~(1u << ring->local_slot_id))
        : s_tdma_flight_sync.active_mask;

    /* TDMA-SEAT-01: while the origin fast path is the only route for local
     * mailboxes, a T2 record is a first-class seat: stage one complete mailbox
     * for the Core1 origin and never touch a queued VDC image. Elsewhere the
     * record rides an ordinary flight FIFO image below. */
    if (position_publication && tdma_service_first_class_route(owner)) {
        uint8_t position_frame[DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE];
        if (!distributed_refmem_tdma_flight_build_compact_mailbox(
                (uint8_t)ring->local_slot_id,
                (uint8_t)target_mask,
                s_tdma_flight_sync.position_tx_sequence,
                s_service_count,
                message_class,
                position_frame,
                sizeof(position_frame))) {
            s_tdma_flight_sync.position_tx_reject_count++;
            s_tdma_flight_sync.position_tx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED;
            s_tdma_flight_sync.position_tx_wait_reason =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_BAD_DESCRIPTOR;
            return;
        }
        /* The value was taken only because the seat was free (see
         * distributed_refmem_position_tx_prepare), and Core0 is the only writer of the
         * slot, so this boarding cannot be refused. A false here is an invariant break,
         * not backpressure: count it and keep the record for the next pass. */
        if (tdma_service_stage_first_class(owner, position_frame)) {
            s_tdma_flight_sync.position_tx_publish_count++;
            s_tdma_flight_sync.position_tx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_SUBMITTED;
            s_tdma_flight_sync.position_tx_submitted_sequence =
                s_tdma_flight_sync.position_tx_sequence;
            s_tdma_flight_sync.position_tx_wait_reason =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
            /* Each record owns its sequence: followers deduplicate a slot by
             * seq16, so a repeated sequence would hide the next record. */
            s_tdma_flight_sync.next_seq32 =
                s_tdma_flight_sync.position_tx_sequence == UINT32_MAX ? 1u :
                s_tdma_flight_sync.position_tx_sequence + 1u;
            /* One record, one seat: the transport is idle again. */
            __atomic_store_n(&s_tdma_flight_sync.position_tx_active, 0u,
                             __ATOMIC_RELEASE);
            s_tdma_flight_sync.position_tx_fragment_index = 0u;
        } else {
            /* Invariant break: the seat was free when this value was taken, so the
             * boarding must succeed. Keep the record and make the defect visible instead
             * of pretending it is backpressure. */
            s_tdma_flight_sync.position_tx_reject_count++;
            s_tdma_flight_sync.position_tx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED;
            s_tdma_flight_sync.position_tx_wait_reason =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_SEAT_BUSY;
        }
        return;
    }
    if (!distributed_refmem_tdma_flight_build_compact_mailbox(
            (uint8_t)ring->local_slot_id,
            (uint8_t)target_mask,
            seq32,
            s_service_count,
            message_class,
            frame,
            sizeof(frame))) {
        s_tdma_flight_sync.tx_reject_count++;
        s_tdma_flight_sync.last_error = 4u;
        return;
    }
    frame_size = sizeof(frame);

    distributed_refmem_tdma_flight_sync_store_mailbox(ring->local_slot_id,
                                                      frame,
                                                      frame_size);

    /* Same cadence rule the position path applies above: never attempt a
     * second TX image while one is already prepared and not yet retired.
     * The futile attempt is rejected as no_slot and consumes a pacing turn,
     * and the soak gate rejects a growing TX reject counter outright, which
     * is exactly what this guard removes. */
    if (fifo.tx_ready_count != 0u) {
        return;
    }

    if (tdma_service_publish_flight_tx(
            owner,
            s_tdma_flight_sync.tx_image,
            tdma_flight_payload_size(ring->node_count),
            seq32,
            seq32,
            distributed_refmem_flight_publish_mask_for_slot(
                ring->local_slot_id))) {
        s_tdma_flight_sync.tx_publish_count++;
        if (normal_due) s_tdma_flight_sync.last_publish_ms = now_ms;
        if (reply_publication) {
            distributed_refmem_cmd_reply_note_published();
        } else if (maint_ack_publication) {
            refmem_cmd_load_consume_maintenance_ack();
        } else if (position_publication) {
            s_tdma_flight_sync.position_tx_publish_count++;
            s_tdma_flight_sync.position_tx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_SUBMITTED;
            s_tdma_flight_sync.position_tx_submitted_sequence = seq32;
            s_tdma_flight_sync.position_tx_wait_reason =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
        } else {
            action_mailbox_published(frame);
        }
        s_tdma_flight_sync.next_seq32 = seq32 == UINT32_MAX ? 1u : seq32 + 1u;
        distributed_refmem_feedback_published(feedback > 0, seq32);
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
        if (resident_master) {
            s_tdma_flight_sync.vdc_command_record_fragment_index =
                (uint8_t)(s_tdma_flight_sync.vdc_command_record_fragment_index +
                          1u);
            if (s_tdma_flight_sync.vdc_command_record_fragment_index >=
                REFMEM_SYNC_VDC_FRAGMENT_COUNT) {
                s_tdma_flight_sync.vdc_command_record_fragment_index = 0u;
            }
        }
#endif
        if (position_publication) {
            const bool freeze_publication =
                s_tdma_flight_sync.position_tx_fragment_index == UINT32_MAX;
            if (freeze_publication) {
                __atomic_store_n(&s_tdma_flight_sync.position_tx_active, 0u,
                                 __ATOMIC_RELEASE);
                s_tdma_flight_sync.position_tx_fragment_index = 0u;
            } else {
                s_tdma_flight_sync.position_tx_fragment_index++;
            }
            if (!freeze_publication &&
                s_tdma_flight_sync.position_tx_fragment_index >=
                    TRIGGER_POSITION_DESCRIPTOR_FRAGMENT_COUNT) {
                __atomic_store_n(&s_tdma_flight_sync.position_tx_active, 0u,
                                 __ATOMIC_RELEASE);
                s_tdma_flight_sync.position_tx_fragment_index = 0u;
            }
        }
    } else {
        s_tdma_flight_sync.tx_reject_count++;
        s_tdma_flight_sync.last_error = 5u;
        if (position_publication) {
            s_tdma_flight_sync.position_tx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED;
            s_tdma_flight_sync.position_tx_wait_reason =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_PUBLISH;
        }
    }
}

void distributed_refmem_tdma_flight_sync_receive(
    tdma_service_service_t *owner,
    const tdma_ring_runtime_snapshot_t *ring)
{
    if (owner == NULL || ring == NULL ||
        ring->local_slot_id >=
            DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT) {
        return;
    }

    for (uint32_t retired = 0u;; ++retired) {
        if (s_tdma_flight_sync.position_retiring &&
            retired >= TDMA_FLIGHT_RX_FRAME_SLOT_COUNT) break;
        tdma_flight_rx_view_t view;
        if (!tdma_service_acquire_flight_rx(owner, &view)) {
            break;
        }
        s_tdma_flight_sync.rx_acquire_count++;
        if (view.data != NULL &&
            view.data_size == tdma_flight_payload_size(ring->node_count)) {
            uint32_t scan_mask = view.segment_mask;
            scan_mask &= s_tdma_flight_sync.active_mask;
            scan_mask &= ~(1u << ring->local_slot_id);
            for (uint32_t slot = 0u;
                 slot < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT;
                 slot++) {
                if ((scan_mask & (1u << slot)) == 0u) {
                    continue;
                }
                const uint32_t offset =
                    distributed_refmem_flight_input_offset_for_slot(slot);
                const uint8_t *mailbox = &view.data[offset];
                if (distributed_refmem_get_le16(mailbox) !=
                        DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC ||
                    mailbox[2] !=
                        DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION) {
                    s_tdma_flight_sync.rx_empty_count++;
                    continue;
                }
                if (mailbox[4] == ring->local_slot_id ||
                    mailbox[4] >= s_tdma_flight_sync.node_count ||
                    (mailbox[5] & (1u << ring->local_slot_id)) == 0u) {
                    continue;
                }
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
                /* Role/session refresh cancels command fragments which began
                 * FIFO publication under an older admission. Ordinary RefMem
                 * mailboxes in this same view must still be consumed. */
                if (mailbox[3] == TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS &&
                    (s_tdma_flight_sync.vdc_command_rx_admission_epoch == 0u ||
                     view.admission_epoch !=
                         s_tdma_flight_sync.vdc_command_rx_admission_epoch)) {
                    s_tdma_flight_sync.vdc_command_fragment_reject_count++;
                    refmem_sync_vdc_fragment_reset(
                        &s_tdma_flight_sync.vdc_command_fragments);
                    continue;
                }
#endif
                distributed_refmem_position_ack_receive(slot, mailbox);
                if (mailbox[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS) {
                    if (!s_tdma_flight_sync.position_retiring &&
                        !distributed_refmem_position_priority_owned())
                        distributed_refmem_tdma_parse_position_fragment(slot, mailbox);
                } else if (!action_receive_mailbox(slot, mailbox)) {
                    distributed_refmem_tdma_flight_parse_mailbox(
                        mailbox,
                        DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE);
                }
                distributed_refmem_feedback_receive(owner, slot, mailbox, &view);
                distributed_refmem_vdc_flight_rx_accept(slot, mailbox, &view);
            }
        } else {
            s_tdma_flight_sync.rx_bad_mailbox_count++;
            s_tdma_flight_sync.last_error = 6u;
        }
        (void)tdma_service_release_flight_rx(owner, view.slot_index);
    }
}

bool distributed_refmem_reset_position_stopped(void)
{
    tdma_service_service_t *owner=tdma_runtime_owner_get();
    tdma_ring_runtime_snapshot_t stopped;
    tdma_flight_fifo_snapshot_t fifo;
    if (!owner || !tdma_ring_runtime_get_snapshot(&owner->ring_runtime,&stopped) ||
        stopped.enabled || stopped.adapter_started ||
        stopped.config_seq!=stopped.applied_config_seq ||
        !tdma_service_get_flight_fifo_snapshot(owner,&fifo) ||
        fifo.tx_ready_count || fifo.tx_active_generation ||
        fifo.rx_queued_count>TDMA_FLIGHT_RX_FRAME_SLOT_COUNT ||
        fifo.rx_parse_count!=fifo.rx_queued_count) return false;
    /* No consumer may hold a private mailbox across this stopped boundary. The TX side has
     * no mailbox any more: the pending table is Core0-only and is dropped below. */
    for (uint32_t slot=0u;slot<DISTRIBUTED_REFMEM_NODE_COUNT;++slot) {
        const uint32_t state=__atomic_load_n(&s_position_rx_mailboxes[slot].state,__ATOMIC_ACQUIRE);
        if (state==TRIGGER_POSITION_MAILBOX_WRITING || state==TRIGGER_POSITION_MAILBOX_READING)
            return false;
    }
    if (fifo.rx_queued_count) {
        /* STOP may clear the public topology. Queued data retains the last
         * RefMem topology; never reinterpret it using the next RUN geometry. */
        if (!s_tdma_flight_sync.node_count ||
            s_tdma_flight_sync.node_count>DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT ||
            s_tdma_flight_sync.local_slot>=s_tdma_flight_sync.node_count)return false;
        stopped.node_count=s_tdma_flight_sync.node_count;
        stopped.local_slot_id=s_tdma_flight_sync.local_slot;
        s_tdma_flight_sync.position_retiring=1u;
        distributed_refmem_tdma_flight_sync_receive(owner,&stopped);
        s_tdma_flight_sync.position_retiring=0u;
        if (!tdma_service_get_flight_fifo_snapshot(owner,&fifo) ||
            fifo.rx_queued_count || fifo.rx_parse_count)return false;
    }
    memset(s_position_pending, 0, sizeof(s_position_pending));
    s_position_wait_counted = 0u;
    s_position_last_drain_ms = 0u;
    __atomic_store_n(&s_tdma_flight_sync.position_tx_active,0u,__ATOMIC_RELEASE);
    s_tdma_flight_sync.position_tx_fragment_index=0u;
    s_tdma_flight_sync.position_tx_phase =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_IDLE;
    s_tdma_flight_sync.position_tx_wait_reason =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
    s_tdma_flight_sync.position_tx_wait_count = 0u;
    s_tdma_flight_sync.position_tx_submitted_sequence = 0u;
    s_tdma_flight_sync.position_tx_target_mask=0u;
    s_tdma_flight_sync.position_tx_position_id=0u;
    s_tdma_flight_sync.position_tx_sequence=0u;
    memset(s_tdma_flight_sync.position_tx_wire,0,sizeof(s_tdma_flight_sync.position_tx_wire));
    for (uint32_t slot=0u;slot<DISTRIBUTED_REFMEM_NODE_COUNT;++slot) {
        __atomic_store_n(&s_position_rx_mailboxes[slot].state,TRIGGER_POSITION_MAILBOX_EMPTY,__ATOMIC_RELEASE);
        __atomic_store_n(&s_position_freeze_latches[slot].mask,0u,__ATOMIC_RELEASE);
        trigger_position_fragment_reset(&s_tdma_flight_sync.position_rx_fragments[slot]);
    }
    s_tdma_flight_sync.position_rx_valid_mask=0u;
    distributed_refmem_position_ack_reset();
    s_tdma_flight_sync.position_rx_phase =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_IDLE;
    s_tdma_flight_sync.position_rx_last_failure =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
    /* Retained private images must not publish the old FREEZE again. Only the compact
     * slice is cleared; the header and the ACK/control/diagnostic tail are kept as they
     * were (a zero-count T2 record has no action meaning). */
    for (size_t offset=0u;offset+TDMA_FLIGHT_SHORT_SLOT_SIZE<=sizeof(s_tdma_flight_sync.tx_image);
         offset+=TDMA_FLIGHT_SHORT_SLOT_SIZE) {
        uint8_t *mailbox=s_tdma_flight_sync.tx_image+offset;
        if (distributed_refmem_get_le16(mailbox)!=DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC ||
            mailbox[3]!=TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS)continue;
        /* Clear the WHOLE compact slice (16 B from VDC_PHASE_OFFSET, not the legacy 10 B
         * at REFMEM_OFFSET): with the extended layout a partial clear would leave a
         * decodable slice behind and the retained image could still publish the old FREEZE. */
        memset(mailbox + TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET, 0,
               TRIGGER_POSITION_SLICE_WIRE_SIZE);
        distributed_refmem_put_le16(mailbox+TDMA_PROCESS_IMAGE_CRC_OFFSET,
            tdma_process_image_crc16_ccitt(mailbox,TDMA_PROCESS_IMAGE_CRC_OFFSET));
    }
    __atomic_store_n(&s_position_cancel_requested,0u,__ATOMIC_RELEASE);
    return true;
}

/* N8.5: TX-side clear only.  RX mailboxes/fragments are owned by this same
 * service context and are not rewritten here.  The caller runs inside the
 * refmem service (unified-command dispatch), so no other task can prepare
 * position TX while this clear executes. */
static void distributed_refmem_position_clear_running_tx(void)
{
    memset(s_position_pending, 0, sizeof(s_position_pending));
    s_position_wait_counted = 0u;
    s_position_last_drain_ms = 0u;
    s_position_drain_cursor = 0u;
    __atomic_store_n(&s_tdma_flight_sync.position_tx_active, 0u, __ATOMIC_RELEASE);
    s_tdma_flight_sync.position_tx_fragment_index = 0u;
    s_tdma_flight_sync.position_tx_phase =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_IDLE;
    s_tdma_flight_sync.position_tx_wait_reason =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
    s_tdma_flight_sync.position_tx_wait_count = 0u;
    s_tdma_flight_sync.position_tx_submitted_sequence = 0u;
    s_tdma_flight_sync.position_tx_target_mask = 0u;
    s_tdma_flight_sync.position_tx_position_id = 0u;
    s_tdma_flight_sync.position_tx_sequence = 0u;
    memset(s_tdma_flight_sync.position_tx_wire, 0,
           sizeof(s_tdma_flight_sync.position_tx_wire));
}

refmem_position_reset_result_t distributed_refmem_reset_position_running(void)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    tdma_ring_runtime_snapshot_t ring;
    if (owner == NULL ||
        !tdma_ring_runtime_get_snapshot(&owner->ring_runtime, &ring) ||
        !refmem_position_reset_admits(ring.enabled, ring.adapter_started,
                                      ring.config_seq,
                                      ring.applied_config_seq)) {
        return REFMEM_POS_RESET_UNSAFE;
    }
    /* The unified-command dispatch path runs inside the sole Core0 refmem
     * service, i.e. the same context that prepares position TX.  Clearing the
     * TX/pending state here is therefore single-writer; no cross-task
     * handshake is required, and the producer stays parked until the caller
     * returns to the service loop. */
    distributed_refmem_position_clear_running_tx();
    return REFMEM_POS_RESET_OK;
}

/* Updated by the core0 refmem service only; read lock-free by submit/SCPI. */
static volatile uint32_t s_ring_transport_ready_cache;

bool distributed_refmem_ring_transport_ready(void)
{
    /* Cached by the core0 refmem service from the ring snapshot: the SCPI /
     * submit path must not take the TDMA runtime lock (it can be held by a
     * parked ring service while the ring is stopped). */
    return __atomic_load_n(&s_ring_transport_ready_cache, __ATOMIC_ACQUIRE) != 0u;
}

void distributed_refmem_tdma_flight_sync_service(void)
{
    distributed_refmem_position_cancel_service();
    if (board_identity_get_no() == 5u || s_tdma_flight_sync.enabled == 0u) {
        __atomic_store_n(&s_ring_transport_ready_cache, 0u, __ATOMIC_RELEASE);
        return;
    }
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    /* Run both refreshes even when one is unknown: expiry and cancellation
     * still belong to this service invocation. No RX lease is held while busy. */
    const bool ordinary_binding_known = distributed_refmem_vdc_flight_rx_refresh(owner);
    const bool feedback_binding_known = distributed_refmem_feedback_refresh(owner);
    if (owner == NULL) return;
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_ring_runtime_get_snapshot(&owner->ring_runtime, &ring)) {
        __atomic_store_n(&s_ring_transport_ready_cache, 0u, __ATOMIC_RELEASE);
        s_tdma_flight_sync.last_error = 1u;
        return;
    }
    __atomic_store_n(
        &s_ring_transport_ready_cache,
        refmem_ring_transport_admits(ring.enabled, ring.adapter_started,
                                     ring.config_seq, ring.applied_config_seq)
            ? 1u : 0u,
        __ATOMIC_RELEASE);
    action_observe(&ring);
    if (ring.enabled == 0u) return;
    distributed_refmem_tdma_flight_sync_update_ring(&ring);
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
    vdc_dpll_manager_refmem_snapshot_t refmem_snapshot;
    if (vdc_dpll_manager_get_refmem_snapshot(&refmem_snapshot)) {
        distributed_refmem_tdma_sync_resident_command_identity(&refmem_snapshot);
    }
#endif
    if (ordinary_binding_known && feedback_binding_known) {
        distributed_refmem_tdma_flight_sync_receive(owner, &ring);
    }
    distributed_refmem_tdma_flight_sync_publish(owner, &ring);
}

void distributed_refmem_node_load_auto_init(void)
{
    memset(&s_node_load_auto_sync, 0, sizeof(s_node_load_auto_sync));
    s_node_load_auto_sync.enabled = 0u;
    s_node_load_auto_sync.local_slot = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    s_node_load_auto_sync.target_mask = 0xFFu;
    s_node_load_auto_sync.epoch_id = DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH;
    s_node_load_auto_sync.run_id = DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN;
    s_node_load_auto_sync.baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    s_node_load_auto_sync.deadline_us = 1000000u;
    s_node_load_auto_sync.uplink_duplex_mode =
        DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF;
    s_node_load_auto_sync.downlink_duplex_mode =
        DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF;
    s_node_load_auto_sync.uplink_adapter_pins.rx_pin = BOARD_REFMEM_SPI_RX_PIN;
    s_node_load_auto_sync.uplink_adapter_pins.csn_pin =
        REFMEM_SPI_PHYSICAL_PIN_UNUSED;
    s_node_load_auto_sync.uplink_adapter_pins.sck_pin = BOARD_REFMEM_SPI_SCK_PIN;
    s_node_load_auto_sync.uplink_adapter_pins.tx_pin = BOARD_REFMEM_SPI_TX_PIN;
    s_node_load_auto_sync.downlink_adapter_pins.rx_pin = BOARD_REFMEM_SPI_RX_PIN;
    s_node_load_auto_sync.downlink_adapter_pins.csn_pin =
        REFMEM_SPI_PHYSICAL_PIN_UNUSED;
    s_node_load_auto_sync.downlink_adapter_pins.sck_pin = BOARD_REFMEM_SPI_SCK_PIN;
    s_node_load_auto_sync.downlink_adapter_pins.tx_pin = BOARD_REFMEM_SPI_TX_PIN;
    s_node_load_auto_sync.next_seq32 = 1u;
    (void)refmem_sync_init(&s_refmem_sync_context,
                           s_node_load_auto_sync.local_slot,
                           s_node_load_auto_sync.epoch_id,
                           s_node_load_auto_sync.run_id);
    refmem_sync_set_command_handler(&s_refmem_sync_context,
                                    distributed_refmem_sync_command_handler,
                                    NULL);
}

bool distributed_refmem_node_load_auto_enqueue(uint32_t instance_id)
{
    if (instance_id == 0u ||
        instance_id >= REFMEM_APP_MODEL_INSTANCE_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_node_load_auto_sync.pending_count; i++) {
        if (s_node_load_auto_sync.pending_instance[i] == instance_id) {
            return true;
        }
    }
    if (s_node_load_auto_sync.pending_count >=
        DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_QUEUE_COUNT) {
        s_node_load_auto_sync.dropped_pending_count++;
        s_node_load_auto_sync.last_error = 1u;
        return false;
    }

    s_node_load_auto_sync.pending_instance[s_node_load_auto_sync.pending_count] =
        instance_id;
    s_node_load_auto_sync.pending_count++;
    return true;
}

bool distributed_refmem_fill_vdc_data_window_plan(
    refmem_realtime_tdma_intent_config_t *config)
{
    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;
    if (config == NULL) {
        return false;
    }
    if (!vdc_dpll_manager_plan_tdma_window(VDC_DOMAIN_WINDOW_REFMEM_DATA,
                                           VDC_DPLL_MANAGER_PLAN_NOW_NS,
                                           &plan,
                                           &gate)) {
        (void)gate;
        return false;
    }
    config->vdc_window_plan_valid = plan.valid;
    config->vdc_window_class = plan.window_class;
    config->vdc_schedule_crc32 = plan.schedule_crc32;
    config->vdc_window_start_ns = plan.window_start_ns;
    config->vdc_window_end_ns = plan.window_end_ns;
    config->vdc_guard_start_ns = plan.guard_start_ns;
    config->vdc_guard_end_ns = plan.guard_end_ns;
    return true;
}

void distributed_refmem_node_load_auto_pop_front(void)
{
    if (s_node_load_auto_sync.pending_count == 0u) {
        return;
    }
    for (uint32_t i = 1u; i < s_node_load_auto_sync.pending_count; i++) {
        s_node_load_auto_sync.pending_instance[i - 1u] =
            s_node_load_auto_sync.pending_instance[i];
    }
    s_node_load_auto_sync.pending_count--;
    s_node_load_auto_sync.pending_instance[s_node_load_auto_sync.pending_count] = 0u;
}

bool distributed_refmem_tdma_busy(const refmem_realtime_tdma_snapshot_t *snapshot)
{
    /* intent_seq/completed_seq are now scoped to the selected traffic class
     * by refmem_realtime_tdma_get_snapshot().  Comparing those values is
     * therefore safe even while another class is carrying extra load. */
    return snapshot != NULL && snapshot->intent_seq != 0u &&
           (int32_t)(snapshot->intent_seq - snapshot->completed_seq) > 0;
}

uint32_t distributed_refmem_u32_payload_crc32(const uint32_t *fields,
                                                     uint32_t field_count)
{
    if (fields == NULL || field_count == 0u) {
        return 0u;
    }

    uint32_t crc = 2166136261u;
    const uint8_t *bytes = (const uint8_t *)fields;
    for (uint32_t i = 0u; i < field_count * (uint32_t)sizeof(uint32_t); i++) {
        crc ^= bytes[i];
        crc *= 16777619u;
    }
    return crc;
}

uint32_t distributed_refmem_model_payload_crc32(uint32_t slot_id,
                                                       uint32_t output_index)
{
    const uint32_t fields[] = {slot_id, output_index};
    return distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
}

bool distributed_refmem_command_state_is_complete(uint32_t state)
{
    return state == REFMEM_COMMAND_STATE_ACKED ||
           state == REFMEM_COMMAND_STATE_NACKED ||
           state == REFMEM_COMMAND_STATE_TIMED_OUT;
}

uint32_t distributed_refmem_next_command_seq(
    const refmem_command_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return 1u;
    }

    uint32_t seq = snapshot->last_completed_seq;
    if (snapshot->command_seq > seq) {
        seq = snapshot->command_seq;
    }
    seq++;
    return seq == 0u ? 1u : seq;
}

bool distributed_refmem_post_command_replacing_complete(
    refmem_command_request_t *request,
    uint32_t issue_tick32)
{
    if (request == NULL) {
        return false;
    }

    osal_critical_enter();
    refmem_command_snapshot_t snapshot;
    if (!refmem_command_get_snapshot(&s_refmem_command_slot, &snapshot)) {
        osal_critical_exit();
        return false;
    }
    if (snapshot.command_seq != 0u) {
        if (!distributed_refmem_command_state_is_complete(snapshot.state) ||
            !refmem_command_clear(&s_refmem_command_slot, snapshot.command_seq)) {
            osal_critical_exit();
            return false;
        }
    }
    request->command_seq = distributed_refmem_next_command_seq(&snapshot);
    const bool ok = refmem_command_try_post(&s_refmem_command_slot,
                                            request,
                                            issue_tick32);
    osal_critical_exit();
    return ok;
}

static distributed_refmem_node_load_owner_entry_t *
distributed_refmem_find_node_load_owner(uint32_t instance_id)
{
    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT; i++) {
        if (s_node_load_owners[i].owner != NULL &&
            s_node_load_owners[i].instance_id == instance_id) {
            return &s_node_load_owners[i];
        }
    }
    return NULL;
}

bool distributed_refmem_execute_node_load_owner(uint32_t instance_id,
                                                       uint32_t slot_id,
                                                       uint32_t payload_ref)
{
    distributed_refmem_node_load_owner_entry_t *entry =
        distributed_refmem_find_node_load_owner(instance_id);
    if (entry == NULL) {
        return false;
    }
    return entry->owner(instance_id, slot_id, payload_ref, entry->context);
}

bool distributed_refmem_slot_claim_gate_ready(void)
{
    refmem_slot_claim_map_t claim_map;
    refmem_slot_claim_gate_status_t claim_gate;
    if (!refmem_slot_claim_derive_map(refmem_application_model_get_generic_node_table(),
                                      refmem_application_model_get_board_capability_table(),
                                      refmem_application_model_get_node_load_table(),
                                      refmem_application_model_get_fb_instance_table(),
                                      &claim_map)) {
        return false;
    }
    uint32_t control_owner = 0u;
    return refmem_slot_claim_gate_evaluate(&claim_map, &claim_gate) &&
           claim_gate.ready != 0u &&
           refmem_slot_claim_lowest_claimed(&claim_map, &control_owner);
}

bool distributed_refmem_control_owner_slot(uint32_t *owner_slot)
{
    return refmem_application_model_control_owner_slot(owner_slot);
}

bool distributed_refmem_flash_activation_safe(void)
{
    distributed_refmem_runtime_protection_snapshot_t protection;
    distributed_refmem_get_runtime_protection(&protection);
    const bool ram_entry_ok =
        protection.ram_resident_required == 0u ||
        (protection.flags & DISTRIBUTED_REFMEM_PROT_RAM_RESIDENT_REQUIRED) != 0u;
    const bool flash_lockout_ok =
        protection.flash_lockout_supported == 0u ||
        protection.flash_lockout_online != 0u;
    const bool entry_owner_ok =
        protection.entry_table_owner == DISTRIBUTED_REFMEM_OWNER_SHARED;
    return ram_entry_ok && flash_lockout_ok && entry_owner_ok;
}

bool distributed_refmem_tdma_profile_activation_ready(
    const tdma_foundation_profile_t *profile,
    uint32_t *schedule_crc32)
{
    tdma_profile_result_t profile_result = TDMA_PROFILE_BAD_ARGUMENT;
    vdc_tdma_ring_plan_t plan;
    if (profile == NULL || schedule_crc32 == NULL ||
        !tdma_foundation_profile_validate(profile, &profile_result) ||
        profile->resource.short_frame_capacity > TDMA_SERVICE_SHORT_FRAME_MAX ||
        profile->resource.long_frame_capacity > TDMA_SERVICE_LONG_FRAME_MAX ||
        !vdc_dpll_manager_plan_tdma_ring(&plan) || plan.valid == 0u ||
        plan.schedule_crc32 == 0u ||
        profile->ring.node_count != plan.ring_node_count ||
        profile->ring.local_index != plan.local_slot_id ||
        profile->ring.reference_index != plan.reference_slot_id ||
        profile->ring.upstream_slot_id != plan.upstream_slot_id ||
        profile->ring.downstream_slot_id != plan.downstream_slot_id ||
        profile->ring.feedback_slot_id != plan.feedback_slot_id ||
        profile->ring.flags != plan.ring_flags ||
        profile->ring.profile_crc32 != plan.ring_profile_crc32 ||
        profile->resource.cycle_period_ns != plan.cycle_period_ns) {
        return false;
    }

    *schedule_crc32 = plan.schedule_crc32;
    return true;
}

bool distributed_refmem_apply_tdma_foundation_profile(
    const tdma_foundation_profile_t *profile,
    uint32_t schedule_crc32)
{
    return refmem_realtime_tdma_configure_foundation_profile(
        &s_refmem_realtime_tdma,
        profile,
        schedule_crc32);
}

refmem_command_reason_t distributed_refmem_activation_nack_reason(uint32_t result)
{
    switch ((refmem_table_activation_result_t)result) {
    case REFMEM_TABLE_ACTIVATE_ERR_GATE:
        return REFMEM_COMMAND_REASON_RUN_STATE_DENIED;
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_BUSY:
        return REFMEM_COMMAND_REASON_RESOURCE_BUSY;
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_TOO_LARGE:
        return REFMEM_COMMAND_REASON_PAYLOAD_CRC_MISMATCH;
    case REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID:
    case REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE:
        return REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
    case REFMEM_TABLE_ACTIVATE_ERR_BAD_ARGUMENT:
    case REFMEM_TABLE_ACTIVATE_ERR_NO_VALID_STAGING:
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED:
    case REFMEM_TABLE_ACTIVATE_OK:
    default:
        return REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH;
    }
}

refmem_vector_header_region_t *distributed_refmem_header(void)
{
    return refmem_vector_table_header(&s_distributed_refmem_table);
}

refmem_vector_node_region_t *distributed_refmem_node_region(uint32_t node_id)
{
    return refmem_vector_table_node(&s_distributed_refmem_table, node_id);
}

void distributed_refmem_publish_status_locked(void)
{
    const refmem_vector_header_region_t *header = distributed_refmem_header();
    const refmem_vector_node_region_t *local_node =
        distributed_refmem_node_region(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);

    s_status.table_size = header->table_size;
    s_status.layout_version = header->layout_version;
    s_status.table_seq = header->table_seq;
    s_status.local_node_id = header->local_node_id;
    s_status.node_count = header->node_count;
    s_status.local_heartbeat = local_node->heartbeat;
    s_status.service_count = s_service_count;
    s_status.flags = header->flags;
    s_status.initialized = s_refmem_core_initialized ? 1u : 0u;
}

uint32_t distributed_refmem_header_crc_locked(void)
{
    return refmem_vector_header_crc(&s_distributed_refmem_table);
}

refmem_vdc_vector_region_t *DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_vdc_vector_region)(void)
{
    return &s_distributed_refmem_table.vdc;
}

refmem_dpll_vector_region_t *DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_dpll_vector_region)(void)
{
    return &s_distributed_refmem_table.dpll;
}

void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_copy_to_volatile)(volatile uint8_t *destination,
                                         const void *source,
                                         size_t size)
{
    const uint8_t *bytes = (const uint8_t *)source;
    for (size_t i = 0u; i < size; i++) {
        destination[i] = bytes[i];
    }
}

void distributed_refmem_copy_from_volatile(void *destination,
                                                  const volatile uint8_t *source,
                                                  size_t size)
{
    uint8_t *bytes = (uint8_t *)destination;
    for (size_t i = 0u; i < size; i++) {
        bytes[i] = source[i];
    }
}

uint32_t DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_next_publish_sequence)(uint32_t *sequence)
{
    if (sequence == NULL) {
        return 1u;
    }
    (*sequence)++;
    if (*sequence == 0u) {
        *sequence = 1u;
    }
    return *sequence;
}

uint32_t DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_begin_vector_write)(volatile uint32_t *seqlock,
                                           uint32_t *stable_sequence)
{
    uint32_t current = __atomic_load_n(seqlock, __ATOMIC_ACQUIRE);
    current &= ~1u;
    if (current > (UINT32_MAX - 2u)) {
        current = 0u;
    }
    const uint32_t odd = current + 1u;
    const uint32_t even = current + 2u;
    __atomic_store_n(seqlock, odd, __ATOMIC_RELEASE);
    if (stable_sequence != NULL) {
        *stable_sequence = even;
    }
    return even;
}

void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_publish_vdc_vector_payload)(
    refmem_vdc_vector_region_t *region,
    refmem_vdc_vector_payload_t *payload)
{
    if (region == NULL || payload == NULL) {
        return;
    }
    uint32_t stable_sequence = 0u;
    (void)distributed_refmem_begin_vector_write(&region->seqlock,
                                                &stable_sequence);
    payload->stable_sequence = stable_sequence;
    payload->payload_crc32 = 0u;
    payload->payload_crc32 = refmem_vdc_vector_payload_crc(payload);
    distributed_refmem_copy_to_volatile((volatile uint8_t *)&region->payload,
                                        payload,
                                        sizeof(*payload));
    __atomic_store_n(&region->seqlock, stable_sequence, __ATOMIC_RELEASE);
}

void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_publish_dpll_vector_payload)(
    refmem_dpll_vector_region_t *region,
    refmem_dpll_vector_payload_t *payload)
{
    if (region == NULL || payload == NULL) {
        return;
    }
    uint32_t stable_sequence = 0u;
    (void)distributed_refmem_begin_vector_write(&region->seqlock,
                                                &stable_sequence);
    payload->stable_sequence = stable_sequence;
    payload->payload_crc32 = 0u;
    payload->payload_crc32 = refmem_dpll_vector_payload_crc(payload);
    distributed_refmem_copy_to_volatile((volatile uint8_t *)&region->payload,
                                        payload,
                                        sizeof(*payload));
    __atomic_store_n(&region->seqlock, stable_sequence, __ATOMIC_RELEASE);
}

bool DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_vector_hardware_evidence_valid)(
    const vdc_dpll_manager_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    const uint32_t flags = snapshot->quality.last_timestamp_flags;
    return snapshot->quality.last_timestamp_source ==
               VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK &&
           snapshot->quality.last_timestamp_resolution_ns != 0u &&
           snapshot->quality.last_timestamp_resolution_ns <=
               VDC_DOMAIN_DPLL_ADMISSION_TIMESTAMP_RESOLUTION_LIMIT_NS &&
           (flags & VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0u &&
           (flags & VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u;
}

uint32_t __attribute__((noinline)) DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_vector_flags)(
    const vdc_dpll_manager_vector_snapshot_t *snapshot)
{
    uint32_t flags = REFMEM_VECTOR_FLAG_VALID;
    const bool provisional =
        (snapshot->path_delay.flags &
         VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) != 0u;
    if (provisional) {
        flags |= REFMEM_VECTOR_FLAG_PROVISIONAL;
    }
    if (snapshot->schedule.enabled != 0u &&
        snapshot->schedule.schedule_crc32 != 0u) {
        flags |= REFMEM_VECTOR_FLAG_SCHEDULE_VALID;
    }
    if (snapshot->path_delay.valid != 0u &&
        (snapshot->path_delay.flags &
         (VDC_PATH_DELAY_FLAG_ACCEPTED |
          VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
          VDC_PATH_DELAY_FLAG_BIAS_VALID |
          VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH)) ==
            (VDC_PATH_DELAY_FLAG_ACCEPTED |
             VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
             VDC_PATH_DELAY_FLAG_BIAS_VALID |
             VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH)) {
        flags |= REFMEM_VECTOR_FLAG_CALIBRATION_VALID;
    }
    if (distributed_refmem_vector_hardware_evidence_valid(snapshot)) {
        flags |= REFMEM_VECTOR_FLAG_HARDWARE_EVIDENCE;
    }
    /* The published lock state is ONE bit, declared by the VDC owner and only
     * projected here (HAOFV: VdcVector is the fact source, RefMem carries it).
     * The owner commits it under the same snapshot guard, so a changed
     * declaration moves the publication revision this writer dedups on. The
     * provisional (diagnostic path-delay) mode has its own bit and does not
     * suppress this one. */
    if (snapshot->lock.locked != 0u) {
        flags |= REFMEM_VECTOR_FLAG_LOCKED;
    }

    return flags;
}

void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_fill_vdc_vector_payload)(
    refmem_vdc_vector_payload_t *payload,
    const vdc_dpll_manager_vector_snapshot_t *snapshot,
    uint32_t publish_sequence,
    bool snapshot_valid)
{
    if (payload == NULL) {
        return;
    }
    memset(payload, 0, sizeof(*payload));
    payload->layout_version = REFMEM_VDC_VECTOR_LAYOUT_VERSION;
    payload->writer = REFMEM_VECTOR_WRITER_CORE1;
    payload->publish_sequence = publish_sequence;
    if (!snapshot_valid || snapshot == NULL) {
        payload->flags = REFMEM_VECTOR_FLAG_STALE;
        return;
    }

    payload->flags = distributed_refmem_vector_flags(snapshot);
    payload->source_update_seq = snapshot->dpll.update_seq;
    payload->source_service_count = (uint32_t)snapshot->service_count;
    payload->schedule_epoch = snapshot->schedule.schedule_epoch;
    payload->local_node_id = snapshot->schedule.local_slot_id;
    payload->reference_node_id = snapshot->schedule.reference_slot_id;
    payload->node_count = snapshot->schedule.ring_binding.node_count;
    payload->schedule_crc32 = snapshot->schedule.schedule_crc32;
    payload->servo_profile_crc32 = snapshot->servo.servo_profile_crc32;
    payload->path_delay_table_crc32 = snapshot->path_delay.table_crc32;
    payload->path_delay_generation = snapshot->path_delay.calibration_generation;
    payload->path_delay_freshness_us = snapshot->path_delay.freshness_us;

    payload->dpll_state = snapshot->dpll.state;
    payload->dpll_update_seq = snapshot->dpll.update_seq;
    payload->dpll_accepted_sample_count = snapshot->dpll.accepted_sample_count;
    payload->dpll_rejected_sample_count = snapshot->dpll.rejected_sample_count;
    payload->dpll_last_sample_seq = snapshot->dpll.last_sample_seq;
    payload->dpll_last_phase_error_ns = snapshot->dpll.last_phase_error_ns;
    payload->dpll_last_frequency_error_ppb = snapshot->dpll.last_frequency_error_ppb;
    payload->dpll_last_offset_ns = snapshot->dpll.last_offset_ns;
    payload->dpll_rms_offset_ns = snapshot->dpll.rms_offset_ns;
    payload->dpll_max_abs_offset_ns = snapshot->dpll.max_abs_offset_ns;
    payload->dpll_jitter_pk_ns = snapshot->dpll.jitter_pk_ns;
    payload->dpll_holdover_age_us = snapshot->dpll.holdover_age_us;

    payload->quality_health_state = snapshot->quality.health_state;
    payload->quality_lock_quality_tier = snapshot->quality.lock_quality_tier;
    payload->quality_flags = snapshot->quality.quality_flags;
    payload->quality_last_reject_code = snapshot->quality.last_reject_code;
    payload->quality_last_timestamp_source = snapshot->quality.last_timestamp_source;
    payload->quality_last_timestamp_resolution_ns =
        snapshot->quality.last_timestamp_resolution_ns;
    payload->quality_last_timestamp_flags = snapshot->quality.last_timestamp_flags;
    payload->quality_last_sample_age_us = snapshot->quality.last_sample_age_us;
    payload->quality_freshness_limit_us = snapshot->quality.freshness_limit_us;

    payload->gate_passed = snapshot->gate.passed;
    payload->gate_reject_code = snapshot->gate.reject_code;
    payload->gate_reject_slot = snapshot->gate.reject_slot;
    payload->gate_reject_evidence = snapshot->gate.reject_evidence;

    payload->clock_base_local_tick64 = snapshot->clock.base_local_tick64;
    payload->clock_base_vdc_time64_ns = snapshot->clock.base_vdc_time64_ns;
    payload->clock_phase_offset_ns = snapshot->clock.phase_offset_ns;
    payload->clock_period_adjust_ppb = snapshot->clock.period_adjust_ppb;
    payload->clock_nominal_period_ns = snapshot->clock.nominal_period_ns;
    payload->clock_model_seq = snapshot->clock.model_seq;
    payload->clock_slew_limit_ppb = snapshot->clock.slew_limit_ppb;
    payload->last_sample_time_ns = snapshot->quality.last_sample_time_ns;
}

void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_fill_dpll_vector_payload)(
    refmem_dpll_vector_payload_t *payload,
    const vdc_dpll_manager_vector_snapshot_t *snapshot,
    uint32_t publish_sequence,
    bool snapshot_valid)
{
    if (payload == NULL) {
        return;
    }
    memset(payload, 0, sizeof(*payload));
    payload->layout_version = REFMEM_DPLL_VECTOR_LAYOUT_VERSION;
    payload->writer = REFMEM_VECTOR_WRITER_CORE1;
    payload->publish_sequence = publish_sequence;
    if (!snapshot_valid || snapshot == NULL) {
        payload->flags = REFMEM_VECTOR_FLAG_STALE;
        return;
    }

    payload->flags = distributed_refmem_vector_flags(snapshot);
    payload->source_update_seq = snapshot->dpll.update_seq;
    payload->source_service_count = (uint32_t)snapshot->service_count;
    payload->ready = snapshot->ready;
    payload->schedule_epoch = snapshot->schedule.schedule_epoch;
    payload->local_node_id = snapshot->schedule.local_slot_id;
    payload->reference_node_id = snapshot->schedule.reference_slot_id;
    payload->node_count = snapshot->schedule.ring_binding.node_count;
    payload->schedule_crc32 = snapshot->schedule.schedule_crc32;
    payload->servo_profile_crc32 = snapshot->servo.servo_profile_crc32;

    payload->state = snapshot->dpll.state;
    payload->dpll_update_seq = snapshot->dpll.update_seq;
    payload->dpll_accepted_sample_count = snapshot->dpll.accepted_sample_count;
    payload->dpll_rejected_sample_count = snapshot->dpll.rejected_sample_count;
    payload->last_sample_seq = snapshot->dpll.last_sample_seq;
    payload->last_phase_error_ns = snapshot->dpll.last_phase_error_ns;
    payload->last_frequency_error_ppb = snapshot->dpll.last_frequency_error_ppb;
    payload->last_offset_ns = snapshot->dpll.last_offset_ns;
    payload->rms_offset_ns = snapshot->dpll.rms_offset_ns;
    payload->max_abs_offset_ns = snapshot->dpll.max_abs_offset_ns;
    payload->jitter_pk_ns = snapshot->dpll.jitter_pk_ns;
    payload->holdover_age_us = snapshot->dpll.holdover_age_us;

    payload->dco_valid = snapshot->dco.valid;
    payload->dco_update_seq = snapshot->dco.dco_update_seq;
    payload->dco_source_model_seq = snapshot->dco.source_model_seq;
    payload->dco_lock_state = snapshot->dco.lock_state;
    payload->dco_phase_offset_ns = snapshot->dco.phase_offset_ns;
    payload->dco_period_adjust_ppb = snapshot->dco.period_adjust_ppb;
    payload->dco_base_local_tick64 = snapshot->dco.base_local_tick64;
    payload->dco_base_vdc_time64_ns = snapshot->dco.base_vdc_time64_ns;
    payload->dco_nominal_period_ns = snapshot->dco.nominal_period_ns;
    payload->dco_slew_limit_ppb = snapshot->dco.slew_limit_ppb;

    payload->quality_health_state = snapshot->quality.health_state;
    payload->quality_lock_quality_tier = snapshot->quality.lock_quality_tier;
    payload->quality_last_reject_code = snapshot->quality.last_reject_code;
    payload->quality_last_timestamp_source = snapshot->quality.last_timestamp_source;
    payload->quality_last_timestamp_resolution_ns =
        snapshot->quality.last_timestamp_resolution_ns;
    payload->quality_last_timestamp_flags = snapshot->quality.last_timestamp_flags;
    payload->quality_last_sample_age_us = snapshot->quality.last_sample_age_us;
    payload->quality_freshness_limit_us = snapshot->quality.freshness_limit_us;
    payload->gate_passed = snapshot->gate.passed;
    payload->gate_reject_code = snapshot->gate.reject_code;
    payload->gate_reject_slot = snapshot->gate.reject_slot;
    payload->gate_reject_evidence = snapshot->gate.reject_evidence;
    payload->path_delay_table_crc32 = snapshot->path_delay.table_crc32;
    payload->path_delay_generation = snapshot->path_delay.calibration_generation;
    payload->path_delay_freshness_us = snapshot->path_delay.freshness_us;
}

bool distributed_refmem_read_vdc_vector_snapshot(
    distributed_refmem_vdc_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    refmem_vdc_vector_region_t *region = distributed_refmem_vdc_vector_region();
    for (uint32_t attempt = 0u; attempt < 8u; attempt++) {
        const uint32_t begin = __atomic_load_n(&region->seqlock,
                                               __ATOMIC_ACQUIRE);
        if (begin == 0u || (begin & 1u) != 0u) {
            continue;
        }
        refmem_vdc_vector_payload_t copy;
        distributed_refmem_copy_from_volatile(
            &copy,
            (const volatile uint8_t *)&region->payload,
            sizeof(copy));
        const uint32_t end = __atomic_load_n(&region->seqlock,
                                             __ATOMIC_ACQUIRE);
        if (begin != end || (end & 1u) != 0u || copy.stable_sequence != end ||
            !refmem_vdc_vector_payload_validate(&copy)) {
            continue;
        }
        *snapshot = copy;
        return true;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    return false;
}

