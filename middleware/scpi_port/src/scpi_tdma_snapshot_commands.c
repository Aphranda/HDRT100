#include "scpi_system_snapshot_commands.h"
#include "vdc_run_output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "diagnostics_tdma_record.h"
#include "pico/time.h"
#include "board_config.h"
#include "board_identity.h"
#include "refmem_control_route.h"
#include "refmem_cmd_load.h"
#include "refmem_cmd_codec.h"
#include "refmem_cmd_frame.h"
#include "refmem_command_catalog.h"
#include "refmem_node_control.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "distributed_refmem_vdc_bridge.h"
#include "osal.h"
#include "project_build_info.h"
#include "refmem_application_model.h"
#include "refmem_pio_spi_adapter.h"
#include "refmem_quality.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_slot_claim.h"
#include "refmem_sync.h"
#include "refmem_sync_hello.h"
#include "refmem_table_registry.h"
#include "refmem_lease_table.h"
#include "tdma_maintenance_runtime.h"
#include "scpi_port_internal.h"
#include "storage_manager.h"
#include "system_manager.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"
#include "tdma_rx_start_cut.h"
#include "tdma_service_timing.h"
#include "tdma_long_frame.h"
#include "tdma_maintenance.h"
#include "tdma_maintenance_stream.h"
#include "tdma_maintenance_runtime.h"
#include "tdma_process_image_layout.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "vdc_dpll_manager.h"
#include "vdc_timestamp_clock.h"
#include "scpi_system_snapshot_internal.h"

#include "trigger_sequence_pilot.h"
/* The T2 runtime snapshot type used by the reservation page below used to
 * arrive through scpi_trigger_t2_runtime_commands.inc. That page handler is a
 * separate translation unit now, so name the dependency here. */
#include "trigger_t2_runtime.h"
#include "tdma_seat_admission.h"
#include "tdma_seat_mount_store.h"
#include "scpi_snapshot_commands_internal.h"

scpi_result_t scpi_cmd_system_tdma_flight_clock_evidence_q(scpi_t *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    if (adapter == NULL ||
        !tdma_pio_spi_ring_adapter_get_snapshot(adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.clock_evidence_enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_clock_evidence_status_q(
    scpi_t *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    if (adapter == NULL ||
        !tdma_pio_spi_ring_adapter_get_snapshot(adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.clock_evidence_enabled);
    SCPI_ResultUInt32(context, snapshot.clock_evidence_build_count);
    SCPI_ResultUInt32(context, snapshot.clock_evidence_build_reject_count);
    SCPI_ResultUInt32(context, snapshot.clock_evidence_build_last_reason);
    SCPI_ResultUInt32(context, snapshot.clock_evidence_last_tx_encoded);
    SCPI_ResultUInt32(context, snapshot.clock_evidence_last_rx_encoded);
    SCPI_ResultUInt32(context, snapshot.pending_tx_evidence);
    SCPI_ResultUInt32(context, snapshot.pending_tx_evidence_sequence);
    SCPI_ResultUInt32(context, snapshot.clock_observation_count);
    SCPI_ResultUInt32(context, snapshot.clock_observation_reject_count);
    SCPI_ResultUInt32(context,
                      snapshot.clock_observation_last_reject_reason);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_load_mask(scpi_t *context)
{
    uint32_t enabled_mask = 0u;
    if (!scpi_port_read_u32(context, &enabled_mask) ||
        !app_realtime_set_load_mask(enabled_mask)) {
        scpi_port_push_exec_error(context, "TDMA_LOAD_MASK");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, enabled_mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_load_mask_q(scpi_t *context)
{
    app_realtime_schedule_snapshot_t snapshot;
    if (!app_realtime_get_schedule_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.enabled_mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_period(scpi_t *context)
{
    uint32_t period_us, generation;
    if (!scpi_port_read_u32(context, &period_us) ||
        !app_realtime_request_period_us(period_us, &generation)) {
        scpi_port_push_exec_error(context, "TDMA_PERIOD_STOP_REQUIRED_OR_BUSY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "PENDING");
    SCPI_ResultUInt32(context, generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_period_q(scpi_t *context)
{
    app_realtime_period_snapshot_t snapshot;
    if (!app_realtime_get_period_snapshot(&snapshot)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, snapshot.active_cycles);
    SCPI_ResultUInt32(context, snapshot.applied_generation);
    SCPI_ResultUInt32(context, snapshot.pending_cycles);
    SCPI_ResultUInt32(context, snapshot.requested_generation);
    SCPI_ResultUInt32(context, snapshot.applying);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_schedule_q(scpi_t *context)
{
    app_realtime_schedule_snapshot_t snapshot;
    if (!app_realtime_get_schedule_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.sys_clock_hz);
    SCPI_ResultUInt32(context, snapshot.cycle_cycles);
    SCPI_ResultUInt32(context, snapshot.phase_count);
    SCPI_ResultUInt32(context, snapshot.enabled_mask);
    SCPI_ResultUInt32(context, snapshot.quarantined_mask);
    SCPI_ResultUInt32(context, snapshot.cycle_count);
    SCPI_ResultUInt32(context, snapshot.schedule_miss_count);
    for (uint32_t phase = 0u; phase < APP_REALTIME_PHASE_COUNT; phase++) {
        SCPI_ResultUInt32(context, snapshot.phase_start_cycle[phase]);
        SCPI_ResultUInt32(context, snapshot.phase_end_cycle[phase]);
        SCPI_ResultUInt32(context, snapshot.phase_wcet_cycles[phase]);
        SCPI_ResultUInt32(context, snapshot.phase_last_start_cycle[phase]);
        SCPI_ResultUInt32(context,
                          snapshot.phase_last_runtime_cycles[phase]);
        SCPI_ResultUInt32(context,
                          snapshot.phase_max_runtime_cycles[phase]);
        SCPI_ResultUInt32(context, snapshot.phase_run_count[phase]);
        SCPI_ResultUInt32(context, snapshot.phase_skip_count[phase]);
        SCPI_ResultUInt32(context,
                          snapshot.phase_start_miss_count[phase]);
        SCPI_ResultUInt32(context, snapshot.phase_overrun_count[phase]);
        SCPI_ResultUInt32(context,
                          snapshot.phase_deadline_miss_count[phase]);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_tdma_profile_result(scpi_t *context, uint32_t selection)
{
    tdma_service_timing_snapshot_t snapshot;
    if (!tdma_service_timing_try_snapshot(&snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    const tdma_service_timing_record_t *record = selection == 2u ? &snapshot.autonomous_peak :
        selection == 3u ? &snapshot.other_peak : selection == 4u ? &snapshot.steady_peak :
        selection == 1u ? &snapshot.peak : &snapshot.last;
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.clock_hz);
    SCPI_ResultUInt32(context, snapshot.reset_generation);
    SCPI_ResultUInt32(context, snapshot.phase_count);
    SCPI_ResultUInt32(context, TDMA_TIMING_STAGE_COUNT);
    SCPI_ResultUInt32(context, selection);
    SCPI_ResultUInt32(context, record->sequence);
    SCPI_ResultUInt64(context, record->start_ticks);
    SCPI_ResultUInt32(context, record->total_ticks);
    SCPI_ResultUInt32(context, record->invalid_count);
    SCPI_ResultUInt32(context, record->full_phase_ticks);
    SCPI_ResultUInt32(context, record->entry.state);
    SCPI_ResultUInt32(context, record->entry.config_generation);
    SCPI_ResultUInt32(context, record->entry.trial_epoch);
    SCPI_ResultUInt32(context, record->entry.return_sequence);
    SCPI_ResultUInt32(context, record->exit.state);
    SCPI_ResultUInt32(context, record->exit.config_generation);
    SCPI_ResultUInt32(context, record->exit.trial_epoch);
    SCPI_ResultUInt32(context, record->exit.return_sequence);
    SCPI_ResultUInt32(context, snapshot.autonomous_phase_count);
    SCPI_ResultUInt32(context, snapshot.other_phase_count);
    for (uint32_t stage = 0u; stage < TDMA_TIMING_STAGE_COUNT; stage++) {
        SCPI_ResultUInt32(context, record->elapsed_ticks[stage]);
        SCPI_ResultUInt32(context, record->calls[stage]);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_profile_q(scpi_t *context)
{
    return scpi_tdma_profile_result(context, false);
}

scpi_result_t scpi_cmd_system_tdma_profile_peak_q(scpi_t *context)
{
    return scpi_tdma_profile_result(context, true);
}

scpi_result_t scpi_cmd_system_tdma_profile_run_q(scpi_t *context)
{
    return scpi_tdma_profile_result(context, 2u);
}

scpi_result_t scpi_cmd_system_tdma_profile_other_q(scpi_t *context)
{
    return scpi_tdma_profile_result(context, 3u);
}

/* The steady record excludes STOP/ARM/config transitions, so a per-cycle
 * WCET budget is compared against a running cycle instead of the one-off ARM. */
scpi_result_t scpi_cmd_system_tdma_profile_steady_q(scpi_t *context)
{
    return scpi_tdma_profile_result(context, 4u);
}

scpi_result_t scpi_cmd_system_tdma_profile_reset(scpi_t *context)
{
    SCPI_ResultUInt32(context, tdma_service_timing_request_reset());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_profile_rx_q(scpi_t *context)
{
    tdma_rx_timing_snapshot_t snapshot;
    if (!tdma_service_timing_rx_try_snapshot(&snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.clock_hz);
    SCPI_ResultUInt32(context, snapshot.reset_generation);
    SCPI_ResultUInt32(context, snapshot.phase_count);
    SCPI_ResultUInt32(context, snapshot.invalid_count);
    SCPI_ResultUInt32(context, TDMA_RX_TIMING_STATION_STATES);
    SCPI_ResultUInt32(context, TDMA_RX_DROP_CAUSE_COUNT);
    SCPI_ResultUInt32(context, snapshot.initial_observation_count);
    SCPI_ResultUInt32(context, snapshot.initial_gap_count);
    SCPI_ResultUInt64(context, snapshot.initial_gap_max_ticks);
    SCPI_ResultUInt32(context, snapshot.initial_backlog_max_words);
    SCPI_ResultUInt64(context, snapshot.clamp_skipped_words);
    for (uint32_t state = 0u; state < TDMA_RX_TIMING_STATION_STATES; ++state) {
        SCPI_ResultUInt32(context, snapshot.station_polls[state]);
        SCPI_ResultUInt64(context, state == 0u ? 0u : snapshot.station_age_max_ns[state - 1u]);
    }
    for (uint32_t cause = 0u; cause < TDMA_RX_DROP_CAUSE_COUNT; ++cause)
        SCPI_ResultUInt32(context, snapshot.drop_count[cause]);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_rx_start_cut_q(scpi_t *context)
{
    /* A frozen, owner-published startup record. This STOP-only export does
     * not read live PIO/DMA capture state or expand the owner snapshot. */
    tdma_rx_start_cut_t cut;
    if (!tdma_pio_spi_phys_get_rx_start_cut(&cut)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultText(context, "RXSTARTCUT");
    SCPI_ResultUInt32(context, cut.schema);
    SCPI_ResultUInt32(context, cut.flags);
    SCPI_ResultUInt32(context, cut.retire_reasons);
    SCPI_ResultUInt32(context, cut.observer_epoch);
    SCPI_ResultUInt32(context, cut.pio_hz);
    SCPI_ResultUInt32(context, cut.physical_frame_words);
    SCPI_ResultUInt32(context, cut.alignment_byte_shift);
    SCPI_ResultUInt32(context, cut.alignment_bit_shift);
    SCPI_ResultUInt64(context, cut.arm_epoch);
    SCPI_ResultUInt64(context, cut.observation_epoch_before);
    SCPI_ResultUInt64(context, cut.observation_epoch_after);
    SCPI_ResultUInt64(context, cut.sample_before_ticks);
    SCPI_ResultUInt64(context, cut.sample_after_ticks);
    SCPI_ResultUInt64(context, cut.produced_before);
    SCPI_ResultUInt64(context, cut.produced_after);
    SCPI_ResultUInt32(context, cut.dma_count_before);
    SCPI_ResultUInt32(context, cut.dma_count_after);
    SCPI_ResultUInt32(context, cut.dma_ctrl_before);
    SCPI_ResultUInt32(context, cut.dma_ctrl_after);
    SCPI_ResultUInt32(context, cut.capture_fdebug_before);
    SCPI_ResultUInt32(context, cut.capture_fdebug_after);
    SCPI_ResultUInt32(context, cut.pads_before);
    SCPI_ResultUInt32(context, cut.pads_after);
    SCPI_ResultUInt32(context, cut.dma_channel);
    SCPI_ResultUInt32(context, cut.capture_fifo_before);
    SCPI_ResultUInt32(context, cut.capture_fifo_after);
    SCPI_ResultUInt32(context, cut.capture_pc_before);
    SCPI_ResultUInt32(context, cut.capture_pc_after);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_crc_diagnostic_q(scpi_t *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    if (adapter == NULL ||
        !tdma_pio_spi_ring_adapter_get_snapshot(adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.clock_evidence_enabled);
    SCPI_ResultUInt32(context, snapshot.last_bad_sequence);
    SCPI_ResultUInt32(context, snapshot.last_bad_transport_result);
    SCPI_ResultUInt32(context, snapshot.last_bad_clock_evidence);
    SCPI_ResultUInt32(context, snapshot.last_bad_packet_diff_count);
    SCPI_ResultUInt32(context, snapshot.last_bad_packet_first_diff_offset);
    SCPI_ResultUInt32(context, snapshot.last_bad_packet_expected_byte);
    SCPI_ResultUInt32(context, snapshot.last_bad_packet_observed_byte);
    SCPI_ResultUInt32(context, snapshot.last_bad_expected_transport_crc32);
    SCPI_ResultUInt32(context, snapshot.last_bad_observed_transport_crc32);
    SCPI_ResultUInt32(context, snapshot.last_bad_recomputed_transport_crc32);
    SCPI_ResultUInt32(context, snapshot.last_bad_expected_payload_crc32);
    SCPI_ResultUInt32(context, snapshot.last_bad_observed_payload_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_process_q(scpi_t *context)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_flight_engine_snapshot_t snapshot;
    tdma_pio_spi_ring_adapter_snapshot_t adapter_snapshot;
    if (owner == NULL ||
        adapter == NULL ||
        !tdma_service_get_flight_engine_snapshot(owner, &snapshot) ||
        !tdma_pio_spi_ring_adapter_get_snapshot(adapter, &adapter_snapshot)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.configured);
    SCPI_ResultUInt32(context, snapshot.active);
    SCPI_ResultUInt32(context, snapshot.local_slot_id);
    SCPI_ResultUInt32(context, snapshot.map_crc32);
    SCPI_ResultUInt32(context, snapshot.map_generation);
    SCPI_ResultUInt32(context, snapshot.payload_size);
    SCPI_ResultUInt32(context, snapshot.local_segment_count);
    SCPI_ResultUInt32(context, snapshot.map_apply_count);
    SCPI_ResultUInt32(context, snapshot.input_bytes);
    SCPI_ResultUInt32(context, snapshot.output_bytes);
    SCPI_ResultUInt32(context, snapshot.tx_stale_reuse_count);
    SCPI_ResultUInt32(context, snapshot.map_reject_count);
    SCPI_ResultUInt32(context, snapshot.length_reject_count);
    SCPI_ResultUInt32(context, snapshot.tx_unavailable_count);
    SCPI_ResultUInt32(context, snapshot.rx_bitmap_scan_count);
    SCPI_ResultUInt32(context, snapshot.rx_bitmap_hit_count);
    SCPI_ResultUInt32(context, snapshot.rx_bitmap_duplicate_count);
    SCPI_ResultUInt32(context, snapshot.rx_bitmap_present_count);
    SCPI_ResultUInt32(context, snapshot.rx_bitmap_incomplete_count);
    const tdma_receive_health_snapshot_t *health =
        &adapter_snapshot.receive_health;
    SCPI_ResultUInt32(context, health->version);
    SCPI_ResultUInt32(context, health->configured);
    SCPI_ResultUInt32(context, health->state);
    SCPI_ResultUInt32(context, health->last_reason);
    SCPI_ResultUInt32(context, health->last_transport_result);
    SCPI_ResultUInt32(context, health->quality_flags);
    SCPI_ResultUInt32(context, health->accepted_count);
    SCPI_ResultUInt32(context, health->rejected_count);
    SCPI_ResultUInt32(context, health->missing_count);
    SCPI_ResultUInt32(context, health->consecutive_failure_count);
    SCPI_ResultUInt32(context, health->image_generation);
    SCPI_ResultUInt32(context, health->accepted_sequence);
    SCPI_ResultUInt32(context, health->accepted_identity_crc32);
    SCPI_ResultUInt32(context, health->accepted_schedule_crc32);
    SCPI_ResultUInt32(context, health->accepted_profile_crc32);
    SCPI_ResultUInt32(context, health->accepted_map_generation);
    SCPI_ResultUInt32(context, health->accepted_segment_mask);
    SCPI_ResultUInt32(context, health->expected_segment_mask);
    SCPI_ResultUInt32(context, health->accepted_wkc);
    SCPI_ResultUInt32(context, health->expected_wkc);
    SCPI_ResultUInt32(context, health->accepted_payload_size);
    SCPI_ResultUInt64(context, health->last_accept_timestamp_ns);
    SCPI_ResultUInt64(context, health->last_observation_timestamp_ns);
    SCPI_ResultUInt64(context, health->stale_age_ns);
    SCPI_ResultUInt32(context, health->last_rejected_reason);
    SCPI_ResultUInt32(context, health->last_rejected_transport_result);
    SCPI_ResultUInt32(context, health->last_rejected_sequence);
    SCPI_ResultUInt32(context,
                      health->last_rejected_observed_segment_mask);
    SCPI_ResultUInt32(context,
                      health->last_rejected_expected_segment_mask);
    SCPI_ResultUInt32(context, health->last_rejected_quality_flags);
    SCPI_ResultUInt64(context, health->last_rejected_timestamp_ns);
    SCPI_ResultUInt32(context, adapter_snapshot.last_rx_origin_slot_id);
    SCPI_ResultUInt32(context, adapter_snapshot.last_rx_hop_count);
    SCPI_ResultUInt32(context, adapter_snapshot.last_rx_hop_limit);
    SCPI_ResultUInt32(context, adapter_snapshot.last_rx_flags);
    SCPI_ResultUInt32(context, adapter_snapshot.last_rx_sequence);
    SCPI_ResultUInt32(context, adapter_snapshot.last_rx_identity_crc32);
    SCPI_ResultUInt32(
        context, adapter_snapshot.resident_feedback_condition_mask);
    SCPI_ResultUInt32(context, adapter_snapshot.resident_feedback_all_mask);
    SCPI_ResultUInt32(context, adapter_snapshot.comm_fsm_state);
    SCPI_ResultUInt32(context, adapter_snapshot.resident_seeded);
    SCPI_ResultUInt32(context, adapter_snapshot.resident_return_ready);
    SCPI_ResultUInt32(
        context, adapter_snapshot.resident_bootstrap_retry_count);
    SCPI_ResultUInt32(
        context, adapter_snapshot.resident_overlay_bootstrap_prepared);
    SCPI_ResultUInt32(
        context, adapter_snapshot.resident_overlay_target_sequence);
    SCPI_ResultUInt32(
        context, adapter_snapshot.comm_fsm_timed_out_window_count);
    SCPI_ResultUInt32(context, adapter_snapshot.resident_stale_cycle_count);
    SCPI_ResultUInt32(context, adapter_snapshot.comm_fsm_window_sequence);
    SCPI_ResultUInt32(
        context, adapter_snapshot.comm_fsm_completed_window_count);
    SCPI_ResultUInt32(
        context, adapter_snapshot.resident_last_completed_cycle);
    SCPI_ResultUInt32(
        context, adapter_snapshot.resident_last_completed_segment_mask);
    SCPI_ResultUInt32(context, adapter_snapshot.comm_fsm_last_error);
    SCPI_ResultUInt32(context, adapter_snapshot.resident_reseed_count);
    SCPI_ResultUInt32(
        context, adapter_snapshot.resident_last_reseed_reason);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_tx(scpi_t *context)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    uint32_t byte_count = 32u;
    uint32_t seed = 0xA5u;
    uint32_t generation = 1u;
    uint32_t sequence = 1u;
    uint32_t segment_mask = 1u;
    const char *hex_text = NULL;
    size_t hex_len = 0u;
    uint8_t payload[TDMA_FLIGHT_PAYLOAD_CAPACITY];
    bool hex_mode = false;
    if (owner == NULL) {
        return SCPI_RES_ERR;
    }

    if (SCPI_ParamUInt32(context, &byte_count, TRUE) == TRUE) {
        (void)SCPI_ParamUInt32(context, &seed, FALSE);
        (void)SCPI_ParamUInt32(context, &generation, FALSE);
        (void)SCPI_ParamUInt32(context, &sequence, FALSE);
        (void)SCPI_ParamUInt32(context, &segment_mask, FALSE);
    } else {
        size_t decoded = 0u;
        if (SCPI_ParamCharacters(context, &hex_text, &hex_len, TRUE) != TRUE ||
            !scpi_refmem_sync_hex_decode(hex_text,
                                         hex_len,
                                         payload,
                                         sizeof(payload),
                                         &decoded) ||
            decoded == 0u) {
            scpi_port_push_exec_error(context, "TDMA_FLIGHT_TX_HEX");
            return SCPI_RES_ERR;
        }
        byte_count = (uint32_t)decoded;
        hex_mode = true;
        (void)SCPI_ParamUInt32(context, &generation, FALSE);
        (void)SCPI_ParamUInt32(context, &sequence, FALSE);
        (void)SCPI_ParamUInt32(context, &segment_mask, FALSE);
    }

    if (byte_count > TDMA_FLIGHT_PAYLOAD_CAPACITY ||
        seed > UINT8_MAX || generation == 0u) {
        return SCPI_RES_ERR;
    }

    uint32_t checksum = 0u;
    if (!hex_mode) {
        for (uint32_t i = 0u; i < byte_count; i++) {
            payload[i] = (uint8_t)(seed + i);
            checksum += payload[i];
        }
    } else {
        for (uint32_t i = 0u; i < byte_count; i++) {
            checksum += payload[i];
        }
    }

    if (!tdma_service_publish_flight_tx(owner,
                                        payload,
                                        byte_count,
                                        generation,
                                        sequence,
                                        segment_mask)) {
        scpi_port_push_exec_error(context, "TDMA_FLIGHT_TX");
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, byte_count);
    SCPI_ResultUInt32(context, checksum);
    SCPI_ResultUInt32(context, seed);
    SCPI_ResultUInt32(context, generation);
    SCPI_ResultUInt32(context, sequence);
    SCPI_ResultUInt32(context, segment_mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_rx_q(scpi_t *context)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    tdma_flight_rx_view_t view;
    const char *mode = NULL;
    size_t mode_len = 0u;
    const bool hex_mode =
        SCPI_ParamCharacters(context, &mode, &mode_len, FALSE) == TRUE &&
        mode_len == 3u &&
        (mode[0] == 'H' || mode[0] == 'h') &&
        (mode[1] == 'E' || mode[1] == 'e') &&
        (mode[2] == 'X' || mode[2] == 'x');
    if (owner == NULL ||
        !tdma_service_acquire_flight_rx(owner, &view)) {
        SCPI_ResultText(context, "EMPTY");
        return SCPI_RES_OK;
    }

    uint32_t checksum = 0u;
    for (uint32_t i = 0u; i < view.data_size; i++) {
        checksum += view.data[i];
    }
    const uint32_t first_byte = view.data_size != 0u ? view.data[0] : 0u;
    const uint32_t last_byte =
        view.data_size != 0u ? view.data[view.data_size - 1u] : 0u;
    const uint32_t data_size = view.data_size;
    const uint32_t generation = view.generation;
    const uint32_t sequence = view.sequence;
    const uint32_t segment_mask = view.segment_mask;
    const uint32_t timestamp_ns_lo =
        (uint32_t)(view.timestamp_ns & 0xFFFFFFFFull);
    const uint32_t timestamp_ns_hi = (uint32_t)(view.timestamp_ns >> 32u);
    const uint32_t quality_flags = view.quality_flags;
    if (hex_mode && view.data_size <= TDMA_FLIGHT_PAYLOAD_CAPACITY) {
        char hex[(TDMA_FLIGHT_PAYLOAD_CAPACITY * 2u) + 1u];
        static const char digits[] = "0123456789ABCDEF";
        for (uint32_t i = 0u; i < view.data_size; i++) {
            hex[i * 2u] = digits[view.data[i] >> 4u];
            hex[(i * 2u) + 1u] = digits[view.data[i] & 0x0Fu];
        }
        hex[view.data_size * 2u] = '\0';
        (void)tdma_service_release_flight_rx(owner, view.slot_index);
        SCPI_ResultText(context, "RXHEX");
        SCPI_ResultText(context, hex);
    } else {
        (void)tdma_service_release_flight_rx(owner, view.slot_index);
        SCPI_ResultText(context, "RX");
    }
    SCPI_ResultUInt32(context, data_size);
    SCPI_ResultUInt32(context, checksum);
    SCPI_ResultUInt32(context, first_byte);
    SCPI_ResultUInt32(context, last_byte);
    SCPI_ResultUInt32(context, generation);
    SCPI_ResultUInt32(context, sequence);
    SCPI_ResultUInt32(context, segment_mask);
    SCPI_ResultUInt32(context, timestamp_ns_lo);
    SCPI_ResultUInt32(context, timestamp_ns_hi);
    SCPI_ResultUInt32(context, quality_flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_local(scpi_t *context)
{
    uint32_t local_slot = 0u;
    if (!scpi_port_read_u32(context, &local_slot)) {
        return SCPI_RES_ERR;
    }
    if (!distributed_refmem_set_tdma_ring_local_slot(local_slot)) {
        scpi_port_push_exec_error(context, "TDMA_RING_LOCAL");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, local_slot);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_topology(scpi_t *context)
{
    uint32_t node_count = 0u;
    uint32_t local_slot = 0u;
    uint32_t reference_slot = 0u;
    if (!scpi_port_read_u32(context, &node_count) ||
        !scpi_port_read_u32(context, &local_slot) ||
        !scpi_port_read_u32(context, &reference_slot)) {
        return SCPI_RES_ERR;
    }
    /* A stopped-state refusal here is TRANSIENT: the ring snapshot, the VDC manager
     * plan and the RefMem application model can still be settling right after the
     * RING:STOP the caller issued moments earlier. The caller cannot see that - a
     * refused SCPI_RES_ERR sends no reply at all, so the host observes a plain
     * timeout plus -200 in its queue (measured 2026-10-14: an intentionally invalid
     * topology reproduces exactly that host-visible signature, and about 3% of the
     * TRN-00 preparation writes timed out that way, each one costing the row).
     * Retry inside this ONE command so a single host write either succeeds or
     * reports a genuine refusal. */
    for (uint32_t attempt = 0u; attempt < 50u; ++attempt) {
        if (distributed_refmem_set_tdma_ring_topology(local_slot,
                                                      reference_slot,
                                                      node_count)) {
            SCPI_ResultUInt32(context, node_count);
            SCPI_ResultUInt32(context, local_slot);
            SCPI_ResultUInt32(context, reference_slot);
            return SCPI_RES_OK;
        }
        sleep_ms(20u);
    }
    scpi_port_push_exec_error(context, "TDMA_RING_TOPOLOGY");
    return SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_system_tdma_ring_arm(scpi_t *context)
{
    if (!distributed_refmem_tdma_ring_arm()) {
        scpi_port_push_exec_error(context, "TDMA_RING_ARM");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_system_tdma_ring_arm_status_q(scpi_t *context)
{
    SCPI_ResultUInt32(
        context, (uint32_t)distributed_refmem_tdma_ring_arm_last_result());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_status_q(scpi_t *context)
{
    tdma_ring_runtime_snapshot_t snapshot;
    if (!tdma_runtime_owner_get_ring_snapshot(&snapshot)) {
        scpi_port_push_exec_error(context, "TDMA_RING_STATUS");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.enabled);
    SCPI_ResultUInt32(context, snapshot.node_count);
    SCPI_ResultUInt32(context, snapshot.local_slot_id);
    SCPI_ResultUInt32(context, snapshot.reference_slot_id);
    SCPI_ResultUInt32(context, snapshot.up_running);
    SCPI_ResultUInt32(context, snapshot.down_running);
    SCPI_ResultUInt32(context, snapshot.ring_seq);
    SCPI_ResultUInt32(context, snapshot.last_reason);
    SCPI_ResultUInt32(context, snapshot.adapter_started);
    SCPI_ResultUInt32(context, snapshot.adapter_service_count);
    SCPI_ResultUInt32(context, snapshot.up_tx_sequence);
    SCPI_ResultUInt32(context, snapshot.down_rx_sequence);
    SCPI_ResultUInt32(context, snapshot.up_tx_frame_crc32);
    SCPI_ResultUInt32(context, snapshot.down_rx_frame_crc32);
    SCPI_ResultUInt32(context, snapshot.idle_beacon_tx_count);
    SCPI_ResultUInt32(context, snapshot.idle_beacon_rx_count);
    SCPI_ResultUInt32(context, snapshot.feedback_round_trip_ns);
    SCPI_ResultUInt32(context, snapshot.timestamp_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.timestamp_flags);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.reference_tx_timestamp_ns);
    SCPI_ResultUInt32(context,
                      (uint32_t)(snapshot.reference_tx_timestamp_ns >> 32u));
    SCPI_ResultUInt32(context, (uint32_t)snapshot.feedback_rx_timestamp_ns);
    SCPI_ResultUInt32(context,
                      (uint32_t)(snapshot.feedback_rx_timestamp_ns >> 32u));
    SCPI_ResultUInt32(context, snapshot.adapter_last_error);
    SCPI_ResultUInt32(context, snapshot.adapter_tx_count);
    SCPI_ResultUInt32(context, snapshot.adapter_rx_count);
    SCPI_ResultUInt32(context, snapshot.adapter_rx_bad_count);
    SCPI_ResultUInt32(context, snapshot.adapter_rx_transport_bad_count);
    SCPI_ResultUInt32(context, snapshot.adapter_rx_schedule_bad_count);
    SCPI_ResultUInt32(context, snapshot.adapter_rx_profile_bad_count);
    SCPI_ResultUInt32(context, snapshot.adapter_last_bad_transport_result);
    SCPI_ResultUInt32(context, snapshot.adapter_last_bad_sequence);
    SCPI_ResultUInt32(context, snapshot.adapter_last_bad_schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.adapter_last_bad_profile_crc32);
    SCPI_ResultUInt32(context, snapshot.adapter_last_bad_header_diff_count);
    SCPI_ResultUInt32(
        context, snapshot.adapter_last_bad_header_first_diff_offset);
    SCPI_ResultUInt32(context, snapshot.adapter_last_bad_header_expected_byte);
    SCPI_ResultUInt32(context, snapshot.adapter_last_bad_header_observed_byte);
    SCPI_ResultUInt32(context, snapshot.config_seq);
    SCPI_ResultUInt32(context, snapshot.applied_config_seq);
    /* Appended loop/beat measurement: the interval between successive arrivals
     * of the reference's own frame at the reference (one ring revolution).
     * Existing positions are unchanged. */
    SCPI_ResultUInt32(context, snapshot.loop_time.flags);
    SCPI_ResultUInt32(context, snapshot.loop_time.resolution_ns);
    SCPI_ResultUInt32(context, snapshot.loop_time.last_period_ns);
    SCPI_ResultUInt32(context, snapshot.loop_time.min_period_ns);
    SCPI_ResultUInt32(context, snapshot.loop_time.max_period_ns);
    SCPI_ResultUInt32(context, snapshot.loop_time.mean_period_ns);
    SCPI_ResultUInt32(context, snapshot.loop_time.sample_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_config_q(scpi_t *context)
{
    tdma_service_ring_runtime_config_t config;
    if (!tdma_runtime_owner_get_staged_ring_config(&config)) {
        scpi_port_push_exec_error(context, "TDMA_RING_CONFIG");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, config.enabled);
    SCPI_ResultUInt32(context, config.node_count);
    SCPI_ResultUInt32(context, config.local_slot_id);
    SCPI_ResultUInt32(context, config.reference_slot_id);
    SCPI_ResultUInt32(context, config.schedule_crc32);
    SCPI_ResultUInt32(context, config.ring_profile_crc32);
    SCPI_ResultUInt32(context, config.operating_profile_crc32);
    SCPI_ResultUInt32(context, config.cycle_period_ns);
    SCPI_ResultUInt32(context, config.owner_config_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_train(scpi_t *context)
{
    uint32_t cycles = 0u;
    if (!scpi_port_read_u32(context, &cycles)) {
        return SCPI_RES_ERR;
    }
    if (!distributed_refmem_tdma_ring_train(cycles)) {
        scpi_port_push_exec_error(context, "TDMA_RING_TRAIN");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, cycles);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_loop_delay(scpi_t *context)
{
    uint32_t loop_delay_ns = 0u;
    uint32_t tolerance_ns = 0u;
    if (!scpi_port_read_u32(context, &loop_delay_ns)) {
        return SCPI_RES_ERR;
    }
    (void)scpi_port_read_u32(context, &tolerance_ns);
    if (!tdma_runtime_owner_set_loop_delay_ns(loop_delay_ns,
                                              tolerance_ns)) {
        scpi_port_push_exec_error(context, "TDMA_RING_LOOP_DELAY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, loop_delay_ns);
    SCPI_ResultUInt32(context, tolerance_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_loop_delay_q(scpi_t *context)
{
    tdma_service_ring_runtime_config_t config;
    if (!tdma_runtime_owner_get_staged_ring_config(&config)) {
        scpi_port_push_exec_error(context, "TDMA_RING_LOOP_DELAY_QUERY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, config.loop_delay_ns);
    SCPI_ResultUInt32(context, config.loop_delay_tolerance_ns);
    return SCPI_RES_OK;
}

/* Read-only witness for the on-cadence gate policy.  Reports the staged copy
 * (the value ARM will hand to the adapter), so a host can prove the policy it
 * requested is the policy in force.  Not part of the DIAGnostic command on
 * purpose: that command's response arity must stay unchanged. */
scpi_result_t scpi_cmd_system_tdma_ring_origin_cadence_q(scpi_t *context)
{
    tdma_service_ring_runtime_config_t config;
    if (!tdma_runtime_owner_get_staged_ring_config(&config)) {
        scpi_port_push_exec_error(context, "TDMA_RING_ORIGIN_CADENCE_QUERY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, config.origin_cadence_enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_diagnostic(scpi_t *context)
{
    uint32_t enabled = 0u;
    /* Optional second parameter: the gate policy switch only, not the physical
     * emission cadence budget (tdma_origin_cadence_t).  Presence is decided by
     * the reader's return value so an omitted argument leaves the current
     * policy untouched instead of being mistaken for an explicit 0. */
    uint32_t origin_cadence = 0u;
    if (!scpi_port_read_u32(context, &enabled) || enabled > 1u) {
        scpi_port_push_exec_error(context, "TDMA_RING_DIAGNOSTIC");
        return SCPI_RES_ERR;
    }
    /* FALSE = optional: the mandatory variant would push
     * SCPI_ERROR_MISSING_PARAMETER for the legacy single-argument form. */
    const bool has_origin_cadence =
        SCPI_ParamUInt32(context, &origin_cadence, FALSE);
    if (has_origin_cadence && origin_cadence > 1u) {
        scpi_port_push_exec_error(context, "TDMA_RING_DIAGNOSTIC");
        return SCPI_RES_ERR;
    }
    if (!tdma_runtime_owner_set_ring_diagnostic_mode(enabled != 0u)) {
        scpi_port_push_exec_error(context, "TDMA_RING_DIAGNOSTIC");
        return SCPI_RES_ERR;
    }
    if (has_origin_cadence &&
        !tdma_runtime_owner_set_origin_cadence(origin_cadence != 0u)) {
        scpi_port_push_exec_error(context, "TDMA_RING_DIAGNOSTIC");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_diagnostic_q(scpi_t *context)
{
    tdma_service_ring_runtime_config_t config;
    if (!tdma_runtime_owner_get_staged_ring_config(&config)) {
        scpi_port_push_exec_error(context, "TDMA_RING_DIAGNOSTIC_QUERY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(
        context,
        (config.flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) != 0u ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_bringup(scpi_t *context)
{
    uint32_t enabled = 0u;
    if (!scpi_port_read_u32(context, &enabled) || enabled > 1u ||
        !tdma_runtime_owner_set_bringup_no_cal(enabled != 0u)) {
        scpi_port_push_exec_error(context, "TDMA_RING_BRINGUP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_bringup_q(scpi_t *context)
{
    tdma_service_ring_runtime_config_t config;
    if (!tdma_runtime_owner_get_staged_ring_config(&config)) {
        scpi_port_push_exec_error(context, "TDMA_RING_BRINGUP_QUERY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(
        context,
        (config.flags & TDMA_RING_FLAG_BRINGUP_NO_CAL) != 0u ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_burst(scpi_t *context)
{
    uint32_t limit = 0u;
    if (!scpi_port_read_u32(context, &limit) || limit > 2u ||
        !tdma_runtime_owner_set_ring_diagnostic_burst(limit)) {
        scpi_port_push_exec_error(context, "TDMA_RING_BURST");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, limit);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_geometry(scpi_t *context)
{
    uint32_t generation = 0u;
    if (!scpi_port_read_u32(context, &generation) ||
        !tdma_runtime_owner_set_ring_geometry_generation(generation)) {
        scpi_port_push_exec_error(context, "TDMA_RING_GEOMETRY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_geometry_q(scpi_t *context)
{
    tdma_ring_runtime_snapshot_t before, after;
    tdma_frozen_geometry_snapshot_t g;
    /* Lifecycle readback at a stable config ACK. Also supports armed,
     * non-emitting readiness; no real-time SCPI sampling is required. */
    if (!tdma_runtime_owner_get_ring_snapshot(&before) ||
        before.config_seq != before.applied_config_seq ||
        !tdma_pio_spi_phys_get_frozen_geometry(&g) ||
        !tdma_runtime_owner_get_ring_snapshot(&after) ||
        after.config_seq != before.config_seq ||
        after.applied_config_seq != before.applied_config_seq ||
        after.enabled != before.enabled || after.adapter_started != before.adapter_started) {
        scpi_port_push_exec_error(context, "TDMA_RING_GEOMETRY_QUERY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, g.version);
    SCPI_ResultUInt32(context, g.state);
    SCPI_ResultUInt32(context, g.reason);
    SCPI_ResultUInt32(context, g.generation);
    SCPI_ResultUInt32(context, g.requested_generation);
    SCPI_ResultUInt32(context, g.source_config_seq);
    SCPI_ResultUInt32(context, g.bound_config_seq);
    SCPI_ResultUInt32(context, (uint32_t)g.source_arm_epoch);
    SCPI_ResultUInt32(context, (uint32_t)(g.source_arm_epoch >> 32u));
    SCPI_ResultUInt32(context, (uint32_t)g.source_observation_epoch);
    SCPI_ResultUInt32(context, (uint32_t)(g.source_observation_epoch >> 32u));
    SCPI_ResultUInt32(context, (uint32_t)g.bound_arm_epoch);
    SCPI_ResultUInt32(context, (uint32_t)(g.bound_arm_epoch >> 32u));
    SCPI_ResultUInt32(context, (uint32_t)g.bound_observation_epoch);
    SCPI_ResultUInt32(context, (uint32_t)(g.bound_observation_epoch >> 32u));
    SCPI_ResultUInt32(context, g.physical_bytes);
    SCPI_ResultUInt32(context, g.dma_byte_shift);
    SCPI_ResultUInt32(context, g.dma_bit_shift);
    SCPI_ResultUInt32(context, g.training_samples);
    SCPI_ResultUInt32(context, g.clk_sys_hz);
    SCPI_ResultUInt32(context, g.persona);
    SCPI_ResultUInt32(context, g.node_count);
    SCPI_ResultUInt32(context, g.local_slot);
    SCPI_ResultUInt32(context, g.reference_slot);
    SCPI_ResultUInt32(context, g.topology_generation);
    SCPI_ResultUInt32(context, g.topology_crc32);
    SCPI_ResultUInt32(context, g.calibration_generation);
    SCPI_ResultUInt32(context, g.schedule_crc32);
    SCPI_ResultUInt32(context, g.operating_profile_crc32);
    SCPI_ResultUInt32(context, g.source_map_generation);
    SCPI_ResultUInt32(context, g.bound_map_generation);
    SCPI_ResultUInt32(context, after.config_seq);
    SCPI_ResultUInt32(context, g.observer_epoch);
    SCPI_ResultUInt32(context, g.observer_state);
    SCPI_ResultUInt32(context, g.observer_reason);
    SCPI_ResultUInt32(context, g.observer_prefix_bits);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_burst_q(scpi_t *context)
{
    tdma_service_ring_runtime_config_t config;
    if (!tdma_runtime_owner_get_staged_ring_config(&config)) {
        scpi_port_push_exec_error(context, "TDMA_RING_BURST_QUERY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, tdma_ring_diagnostic_burst_limit(config.flags));
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_burst_status_q(scpi_t *context)
{
    tdma_ring_runtime_snapshot_t before, after;
    tdma_pio_spi_ring_adapter_snapshot_t adapter;
    tdma_pio_spi_phys_snapshot_t phys;
    /* Post-STOP facts only. Bracket the independent owner snapshots with the
     * same applied STOP generation; none of these counts grants identity. */
    if (!tdma_runtime_owner_get_ring_snapshot(&before) ||
        before.enabled != 0u || before.adapter_started != 0u ||
        before.config_seq != before.applied_config_seq ||
        !tdma_pio_spi_ring_adapter_get_snapshot(
            tdma_runtime_owner_get_ring_adapter(), &adapter) ||
        adapter.diagnostic_burst_valid == 0u ||
        !tdma_runtime_owner_get_phys_snapshot(&phys) ||
        !tdma_runtime_owner_get_ring_snapshot(&after) ||
        after.enabled != 0u || after.adapter_started != 0u ||
        after.config_seq != before.config_seq ||
        after.applied_config_seq != before.applied_config_seq) {
        scpi_port_push_exec_error(context, "TDMA_RING_BURST_STATUS");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, adapter.diagnostic_burst_limit);
    SCPI_ResultUInt32(context, adapter.diagnostic_burst_launched);
    SCPI_ResultUInt32(context, phys.tx_count);
    SCPI_ResultUInt32(context, phys.tx_timeout_count);
    SCPI_ResultUInt32(context, phys.last_error);
    SCPI_ResultUInt32(context, adapter.last_error);
    SCPI_ResultUInt32(context, after.applied_config_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_train_status_q(scpi_t *context)
{
    tdma_pio_spi_clk_train_snapshot_t train;
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_clk_train_snapshot(&train) ||
        !tdma_runtime_owner_get_ring_snapshot(&ring)) {
        scpi_port_push_exec_error(context, "TDMA_RING_TRAIN_STATUS");
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, "CLKTRAIN");
    SCPI_ResultUInt32(context, train.version);
    SCPI_ResultUInt32(context, train.state);
    SCPI_ResultUInt32(context, train.result);
    SCPI_ResultUInt32(context, train.role);
    SCPI_ResultUInt32(context, train.request_seq);
    SCPI_ResultUInt32(context, train.service_count);
    SCPI_ResultUInt32(context, train.baud_hz);
    SCPI_ResultUInt32(context, train.requested_cycles);
    SCPI_ResultUInt32(context, train.return_seen);
    SCPI_ResultUInt32(context, train.return_before_tx_done);
    SCPI_ResultUInt32(context, train.tx_sck_pin);
    SCPI_ResultUInt32(context, train.rx_sck_pin);
    SCPI_ResultUInt32(context, train.timestamp_resolution_ns);
    SCPI_ResultUInt32(context, train.timestamp_flags);
    SCPI_ResultUInt32(context,
                      (uint32_t)(train.tx_start_timestamp_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context,
                      (uint32_t)(train.tx_start_timestamp_ns >> 32u));
    SCPI_ResultUInt32(
        context,
        (uint32_t)(train.tx_done_observed_timestamp_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(
        context,
        (uint32_t)(train.tx_done_observed_timestamp_ns >> 32u));
    SCPI_ResultUInt32(
        context,
        (uint32_t)(train.return_observed_timestamp_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(
        context,
        (uint32_t)(train.return_observed_timestamp_ns >> 32u));
    SCPI_ResultUInt32(context,
                      (uint32_t)(train.burst_duration_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context,
                      (uint32_t)(train.burst_duration_ns >> 32u));
    SCPI_ResultUInt32(context, ring.train_request_seq);
    SCPI_ResultUInt32(context, ring.train_accepted_seq);
    SCPI_ResultUInt32(context, ring.train_start_count);
    SCPI_ResultUInt32(context, ring.train_reject_count);
    SCPI_ResultUInt32(context, ring.training_dirty);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_start(scpi_t *context)
{
    const uint64_t requested_us = time_us_64();
    if (!distributed_refmem_tdma_ring_start()) {
        scpi_port_push_exec_error(context, "TDMA_RING_START");
        return SCPI_RES_ERR;
    }
    diagnostics_tdma_record_start(requested_us);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_system_tdma_record_arm(scpi_t *context)
{
    uint32_t epoch, interval_us, samples;
    if (!scpi_port_read_u32(context, &epoch) ||
        !scpi_port_read_u32(context, &interval_us) ||
        !scpi_port_read_u32(context, &samples) ||
        !diagnostics_tdma_record_arm(epoch, interval_us, samples)) {
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_system_tdma_record_status_q(scpi_t *context)
{
    diagnostics_tdma_record_status_t status;
    if (!diagnostics_tdma_record_status(&status)) {
        SCPI_ResultText(context, "BUSY");
        return SCPI_RES_OK;
    }
    SCPI_ResultUInt32(context, status.state);
    SCPI_ResultUInt32(context, status.epoch);
    SCPI_ResultUInt32(context, status.interval_us);
    SCPI_ResultUInt32(context, status.requested);
    SCPI_ResultUInt32(context, status.written);
    SCPI_ResultUInt32(context, status.missed);
    SCPI_ResultUInt32(context, status.reason);
    SCPI_ResultUInt32(context, status.bytes);
    SCPI_ResultUInt32(context, status.job_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_record_cancel(scpi_t *context)
{
    diagnostics_tdma_record_cancel();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_system_tdma_record_save(scpi_t *context)
{
    if (!diagnostics_tdma_record_save()) {
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_system_tdma_record_read_q(scpi_t *context)
{
    uint32_t offset, size;
    uint8_t data[256];
    char hex[sizeof(data) * 2u + 1u];
    static const char digits[] = "0123456789abcdef";
    if (!scpi_port_read_u32(context, &offset) ||
        !scpi_port_read_u32(context, &size) || size == 0u || size > sizeof(data) ||
        !app_tdma_record_copy(offset, data, size)) {
        return SCPI_RES_ERR;
    }
    for (uint32_t i = 0u; i < size; ++i) {
        hex[i * 2u] = digits[data[i] >> 4u];
        hex[i * 2u + 1u] = digits[data[i] & 15u];
    }
    hex[size * 2u] = '\0';
    SCPI_ResultUInt32(context, offset);
    SCPI_ResultUInt32(context, size);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_stop(scpi_t *context)
{
    if (!distributed_refmem_tdma_ring_stop()) {
        scpi_port_push_exec_error(context, "TDMA_RING_STOP");
        return SCPI_RES_ERR;
    }
    diagnostics_tdma_record_cancel();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_system_tdma_ring_log(scpi_t *context)
{
    scpi_bool_t enabled = FALSE;
    if (SCPI_ParamBool(context, &enabled, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }
    if (!distributed_refmem_set_tdma_ring_log_enabled(enabled ? true : false)) {
        scpi_port_push_exec_error(context, "TDMA_RING_LOG");
        return SCPI_RES_ERR;
    }
    SCPI_ResultBool(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_ring_log_q(scpi_t *context)
{
    SCPI_ResultBool(context,
                    distributed_refmem_get_tdma_ring_log_enabled() ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_long_frame_tx(scpi_t *context)
{
    const char *hex = NULL;
    size_t hex_len = 0u;
    uint8_t frame[TDMA_LONG_FRAME_MAX];
    size_t frame_size = 0u;
    uint32_t generation = 1u;
    uint32_t sequence = 1u;
    uint32_t segment_mask = 1u;
    if (SCPI_ParamCharacters(context, &hex, &hex_len, TRUE) != TRUE ||
        !scpi_refmem_sync_hex_decode(hex, hex_len, frame, sizeof(frame),
                                     &frame_size)) {
        scpi_port_push_exec_error(context, "TDMA_LONGFRAME_TX_HEX");
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &generation, FALSE);
    (void)SCPI_ParamUInt32(context, &sequence, FALSE);
    (void)SCPI_ParamUInt32(context, &segment_mask, FALSE);
    tdma_long_frame_ensure_ready();
    if (!tdma_maintenance_runtime_submit(
            frame, frame_size,
            (uint8_t)(board_identity_get_no() - 1u),
            (uint8_t)generation, 0x0Fu)) {
        scpi_port_push_exec_error(context, "TDMA_LONGFRAME_TX");
        return SCPI_RES_ERR;
    }
    uint32_t checksum = 0u;
    for (size_t i = 0u; i < frame_size; i++) {
        checksum += frame[i];
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, (uint32_t)frame_size);
    SCPI_ResultUInt32(context, checksum);
    SCPI_ResultUInt32(context, generation);
    SCPI_ResultUInt32(context, sequence);
    SCPI_ResultUInt32(context, segment_mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_long_frame_rx_q(scpi_t *context)
{
    const char *mode = NULL;
    size_t mode_len = 0u;
    const bool hex_mode =
        SCPI_ParamCharacters(context, &mode, &mode_len, FALSE) == TRUE &&
        mode_len == 3u &&
        (mode[0] == 'H' || mode[0] == 'h') &&
        (mode[1] == 'E' || mode[1] == 'e') &&
        (mode[2] == 'X' || mode[2] == 'x');
    tdma_long_frame_ensure_ready();
    uint8_t received[TDMA_LONG_FRAME_MAX];
    size_t received_size = 0u;
    uint8_t received_stream = 0u;
    uint8_t received_generation = 0u;
    while (tdma_maintenance_runtime_take(
               received, sizeof(received), &received_size,
               &received_stream, &received_generation)) {
        (void)tdma_long_frame_publish_rx(
            &s_tdma_long_frame_rx,
            received, received_size,
            received_generation, received_stream, 1u);
    }
    tdma_long_frame_view_t view;
    if (!tdma_long_frame_acquire_rx(&s_tdma_long_frame_rx, &view)) {
        SCPI_ResultText(context, "EMPTY");
        return SCPI_RES_OK;
    }
    if (hex_mode) {
        char hex[(TDMA_LONG_FRAME_MAX * 2u) + 1u];
        static const char digits[] = "0123456789ABCDEF";
        for (uint32_t i = 0u; i < view.data_size; i++) {
            hex[i * 2u] = digits[view.data[i] >> 4u];
            hex[(i * 2u) + 1u] = digits[view.data[i] & 0x0Fu];
        }
        hex[view.data_size * 2u] = '\0';
        SCPI_ResultText(context, "LONGHEX");
        SCPI_ResultText(context, hex);
    } else {
        SCPI_ResultText(context, "LONG");
    }
    const uint32_t data_size = view.data_size;
    const uint32_t generation = view.generation;
    const uint32_t sequence = view.sequence;
    const uint32_t segment_mask = view.segment_mask;
    uint32_t checksum = 0u;
    for (uint32_t i = 0u; i < view.data_size; i++) {
        checksum += view.data[i];
    }
    tdma_long_frame_release_rx(&s_tdma_long_frame_rx, view.slot_index);
    SCPI_ResultUInt32(context, data_size);
    SCPI_ResultUInt32(context, checksum);
    SCPI_ResultUInt32(context, generation);
    SCPI_ResultUInt32(context, sequence);
    SCPI_ResultUInt32(context, segment_mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_boot_plan_q(scpi_t *context)
{
    tdma_boot_plan_t plan;
    if (!app_guide_boot_plan_get(&plan)) {
        scpi_port_push_exec_error(context, "TDMA_BOOT_PLAN");
        return SCPI_RES_ERR;
    }
    tdma_boot_profile_t profile;
    const bool have_profile = app_guide_boot_profile_get(&profile);
    tdma_boot_step_t steps[TDMA_BOOT_SEQUENCE_MAX];
    const size_t step_count =
        app_guide_boot_steps_get(steps, TDMA_BOOT_SEQUENCE_MAX);
    SCPI_ResultUInt32(context, (uint32_t)plan.action);
    SCPI_ResultUInt32(context, (uint32_t)plan.reason);
    SCPI_ResultUInt32(context, (uint32_t)step_count);
    SCPI_ResultUInt32(context, app_guide_boot_fail_step());
    SCPI_ResultUInt32(context, app_guide_boot_listen_pending() ? 1u : 0u);
    SCPI_ResultUInt32(context, app_guide_boot_join_timeout() ? 1u : 0u);
    SCPI_ResultUInt32(context, have_profile ? 1u : 0u);
    SCPI_ResultUInt32(context, have_profile ? profile.local_slot : 0u);
    SCPI_ResultUInt32(context, have_profile ? profile.node_count : 0u);
    SCPI_ResultUInt32(context, have_profile ? profile.profile_level : 0u);
    SCPI_ResultUInt32(context, have_profile ? profile.flags : 0u);
    SCPI_ResultUInt32(context, have_profile ? profile.generation : 0u);
    SCPI_ResultUInt32(context, have_profile ? profile.topology_crc32 : 0u);
    SCPI_ResultUInt32(context, app_guide_boot_topology_fail());
    SCPI_ResultUInt32(context, app_guide_boot_apply_fail());
    SCPI_ResultUInt32(context, app_guide_boot_join_frame_seen() ? 1u : 0u);
    SCPI_ResultUInt32(context, app_guide_boot_half_ring() ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_guide_pair_q(scpi_t *context)
{
    uint32_t index = 0u;
    if (!scpi_port_read_u32(context, &index)) {
        return SCPI_RES_ERR;
    }
    uint32_t role = 0u;
    uint32_t frames = 0u;
    uint32_t words = 0u;
    uint32_t tx = 0u;
    uint32_t state = 0u;
    uint32_t offset = 0u;
    uint32_t service = 0u;
    uint32_t error = 0u;
    if (!app_guide_scan_get_pair(index, &role, &frames, &words, &tx,
                                 &state, &offset, &service, &error)) {
        scpi_port_push_exec_error(context, "TDMA_GUIDE_PAIR");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, role);
    SCPI_ResultUInt32(context, frames);
    SCPI_ResultUInt32(context, words);
    SCPI_ResultUInt32(context, tx);
    SCPI_ResultUInt32(context, state);
    SCPI_ResultUInt32(context, offset);
    SCPI_ResultUInt32(context, service);
    SCPI_ResultUInt32(context, error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_boot_topology_q(scpi_t *context)
{
    uint32_t reason = 0u;
    const bool accepted = app_guide_topology_accepted(&reason);
    SCPI_ResultUInt32(context, accepted ? 1u : 0u);
    SCPI_ResultUInt32(context, reason);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_guide_scan_q(scpi_t *context)
{
    app_guide_scan_status_t status;
    app_guide_scan_get_status(&status);
    SCPI_ResultUInt32(context, status.state);
    SCPI_ResultUInt32(context, status.pair_index);
    SCPI_ResultUInt32(context, status.pair_count);
    SCPI_ResultUInt32(context, status.role);
    SCPI_ResultUInt32(context, status.node_count);
    SCPI_ResultUInt32(context, status.local_slot);
    SCPI_ResultUInt32(context, status.observed_low);
    SCPI_ResultUInt32(context, status.observed_high);
    SCPI_ResultUInt32(context, status.observed_count);
    SCPI_ResultUInt32(context, status.skipped);
    SCPI_ResultUInt32(context, status.failures);
    SCPI_ResultUInt32(context, status.fail_phase);
    SCPI_ResultUInt32(context, status.arm_result);
    SCPI_ResultUInt32(context, status.arm_attempts);
    SCPI_ResultUInt32(context, status.arm_gap);
    SCPI_ResultUInt32(context, status.staged_enabled);
    SCPI_ResultUInt32(context, status.trials_started);
    SCPI_ResultUInt32(context, status.trials_armed);
    SCPI_ResultUInt32(context, status.max_late_ms);
    SCPI_ResultUInt32(context, status.stall_ms);
    SCPI_ResultUInt32(context, status.frames_delta);
    SCPI_ResultUInt32(context, status.words_delta);
    SCPI_ResultUInt32(context, status.tx_delta);
    SCPI_ResultUInt32(context, status.sample_started);
    return SCPI_RES_OK;
}
