#include "distributed_refmem_internal.h"
#include "tdma_process_image_layout.h"

#include <string.h>

void distributed_refmem_get_tdma_flight_sync(
    distributed_refmem_tdma_flight_sync_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    const distributed_refmem_tdma_flight_sync_t *s =
        distributed_refmem_tdma_flight_sync();
    snapshot->enabled = s->enabled;
    snapshot->local_slot = s->local_slot;
    snapshot->node_count = s->node_count;
    snapshot->active_mask = s->active_mask;
    snapshot->reference_slot = s->reference_slot;
    snapshot->remote_slot = s->remote_slot;
    snapshot->payload_size = tdma_flight_payload_size(s->node_count);
    snapshot->mailbox_size = DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE;
    snapshot->publish_interval_ms = s->publish_interval_ms;
    snapshot->next_seq32 = s->next_seq32;
    snapshot->tx_publish_count = s->tx_publish_count;
    snapshot->tx_reject_count = s->tx_reject_count;
    snapshot->rx_acquire_count = s->rx_acquire_count;
    snapshot->rx_empty_count = s->rx_empty_count;
    snapshot->rx_accept_count = s->rx_accept_count;
    snapshot->rx_reject_count = s->rx_reject_count;
    snapshot->rx_duplicate_skip_count =
        s->rx_duplicate_skip_count;
    snapshot->rx_bad_mailbox_count = s->rx_bad_mailbox_count;
    snapshot->last_rx_result = s->last_rx_result;
    snapshot->last_frame_type = s->last_frame_type;
    snapshot->last_source_slot = s->last_source_slot;
    snapshot->last_seq32 = s->last_seq32;
    snapshot->last_value_u32 = s->last_value_u32;
    snapshot->last_error = s->last_error;
    snapshot->wire_layout_version = TDMA_PROCESS_IMAGE_LAYOUT_VERSION;
    snapshot->last_vdc_phase_offset_ns =
        s->last_vdc_phase_offset_ns;
    snapshot->last_vdc_rate_adjust_ppb =
        s->last_vdc_rate_adjust_ppb;
    snapshot->last_vdc_lock_state = s->last_vdc_lock_state;
    snapshot->last_vdc_quality = s->last_vdc_quality;
    snapshot->last_ack_seq16 = s->last_ack_seq16;
    snapshot->last_ack_flags = s->last_ack_flags;
    snapshot->last_control_opcode = s->last_control_opcode;
    snapshot->last_control_seq8 = s->last_control_seq8;
    snapshot->last_optional_diagnostic =
        s->last_optional_diagnostic;
    snapshot->last_mailbox_crc16 = s->last_mailbox_crc16;
    snapshot->vdc_command_fragment_rx_count =
        s->vdc_command_fragment_rx_count;
    snapshot->vdc_command_fragment_complete_count =
        s->vdc_command_fragment_complete_count;
    snapshot->vdc_command_fragment_reject_count =
        s->vdc_command_fragment_reject_count;
    snapshot->vdc_command_accept_count =
        s->vdc_command_accept_count;
    snapshot->vdc_command_last_seq = s->vdc_command_last_seq;
    snapshot->vdc_command_prepare_attempt_count =
        s->vdc_command_prepare_attempt_count;
    snapshot->vdc_command_prepare_reject_count =
        s->vdc_command_prepare_reject_count;
    snapshot->vdc_command_prepare_last_reason =
        s->vdc_command_prepare_last_reason;
    snapshot->vdc_command_prepare_last_gate_code =
        s->vdc_command_prepare_last_gate_code;
    snapshot->vdc_command_prepare_last_gate_slot =
        s->vdc_command_prepare_last_gate_slot;
    snapshot->vdc_command_prepare_last_gate_evidence =
        s->vdc_command_prepare_last_gate_evidence;
    snapshot->vdc_command_record_active =
        s->vdc_command_record_active;
    snapshot->vdc_command_record_fragment_index =
        s->vdc_command_record_fragment_index;
    snapshot->vdc_command_record_source_update_seq =
        s->vdc_command_record_source_update_seq;
    snapshot->vdc_command_last_common_time_ns_lo =
        (uint32_t)s->vdc_command_last_common_time_ns;
    snapshot->vdc_command_last_common_time_ns_hi =
        (uint32_t)(s->vdc_command_last_common_time_ns >> 32u);
}

bool distributed_refmem_get_tdma_flight_sync_peer(
    uint32_t source_slot,
    refmem_sync_peer_state_t *snapshot)
{
    if (snapshot == NULL || source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }
    const refmem_sync_peer_state_t *peer =
        refmem_sync_delta_get_peer(&distributed_refmem_tdma_flight_sync()->context,
                             (uint8_t)source_slot);
    if (peer == NULL) {
        return false;
    }
    *snapshot = *peer;
    return true;
}

bool distributed_refmem_get_tdma_flight_sync_mirror(
    uint32_t source_slot,
    refmem_sync_mirror_snapshot_t *snapshot)
{
    if (snapshot == NULL || source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }
    const refmem_sync_mirror_snapshot_t *mirror =
        refmem_sync_delta_get_mirror(&distributed_refmem_tdma_flight_sync()->context,
                               (uint8_t)source_slot);
    if (mirror == NULL) {
        return false;
    }
    *snapshot = *mirror;
    return true;
}

/* Keep the dedicated VDC receiver context alive across node-load control
 * changes.  Reinitialization is reserved for an identity change; ordinary
 * service calls never memset retained peer commands. */
void distributed_refmem_get_tdma_flight_sync_quality(
    refmem_sync_quality_counters_t *snapshot)
{
    refmem_sync_delta_get_quality(&distributed_refmem_tdma_flight_sync()->context, snapshot);
}
