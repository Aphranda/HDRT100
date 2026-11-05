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

scpi_result_t scpi_cmd_refmem_sync_spi_line_drive(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t line_index = 0u;
    uint32_t level = 0u;
    if (!scpi_port_read_u32(context, &line_index)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &level, FALSE);
    if (line_index > 3u || level > 1u) {
        return SCPI_RES_ERR;
    }

#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
    refmem_bootstrap_transport_disarm();
#endif
    if (!refmem_spi_physical_line_drive(line_index, level != 0u)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_SPI_LINE_DRIVE");
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, line_index);
    SCPI_ResultUInt32(context, level);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_line_status_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    const uint32_t level_mask = refmem_spi_physical_line_sample();
    SCPI_ResultUInt32(context, level_mask);
    SCPI_ResultUInt32(context, BOARD_REFMEM_SPI_RX_PIN);
    SCPI_ResultUInt32(context, BOARD_REFMEM_SPI_CSN_PIN);
    SCPI_ResultUInt32(context, BOARD_REFMEM_SPI_SCK_PIN);
    SCPI_ResultUInt32(context, BOARD_REFMEM_SPI_TX_PIN);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_raw_tx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t byte_count = 0u;
    uint32_t seed = 0xA5u;
    if (!scpi_port_read_u32(context, &byte_count)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &seed, FALSE);
    if (byte_count == 0u || byte_count > SCPI_REFMEM_SYNC_SPI_RAW_MAX || seed > 0xFFu) {
        return SCPI_RES_ERR;
    }

    uint32_t checksum = 0u;
    for (uint32_t i = 0u; i < byte_count; i++) {
        checksum += (uint8_t)(seed + i);
    }

#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
    if (!refmem_spi_physical_adapter_transmit_raw(
            refmem_bootstrap_transport_adapter(), (uint8_t)seed,
            byte_count)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_SPI_RAW_TX");
        return SCPI_RES_ERR;
    }
#else
    scpi_port_push_exec_error(context, "REFMEM_BOOTSTRAP_DISABLED");
    return SCPI_RES_ERR;
#endif

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, byte_count);
    SCPI_ResultUInt32(context, checksum);
    SCPI_ResultUInt32(context, (uint8_t)seed);
    SCPI_ResultUInt32(context, (uint8_t)(seed + byte_count - 1u));
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_spi_raw_rx_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t expected_size = 0u;
    uint32_t seed = 0xA5u;
    uint32_t timeout_ms = 1000u;
    if (!scpi_port_read_u32(context, &expected_size)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &seed, FALSE);
    (void)SCPI_ParamUInt32(context, &timeout_ms, FALSE);
    if (expected_size == 0u ||
        expected_size > SCPI_REFMEM_SYNC_SPI_RAW_MAX ||
        seed > 0xFFu) {
        return SCPI_RES_ERR;
    }

    uint8_t buffer[SCPI_REFMEM_SYNC_SPI_RAW_MAX];
    size_t received_size = 0u;
#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
    if (!refmem_spi_physical_adapter_receive_raw(
            refmem_bootstrap_transport_adapter(), buffer, expected_size,
            &received_size, timeout_ms)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_SPI_RAW_RX");
        return SCPI_RES_ERR;
    }
#else
    scpi_port_push_exec_error(context, "REFMEM_BOOTSTRAP_DISABLED");
    return SCPI_RES_ERR;
#endif

    uint32_t checksum = 0u;
    uint32_t mismatch_count = 0u;
    for (uint32_t i = 0u; i < received_size; i++) {
        checksum += buffer[i];
        if (buffer[i] != (uint8_t)(seed + i)) {
            mismatch_count++;
        }
    }

    SCPI_ResultText(context, "RAW");
    SCPI_ResultUInt32(context, (uint32_t)received_size);
    SCPI_ResultUInt32(context, checksum);
    SCPI_ResultUInt32(context, buffer[0]);
    SCPI_ResultUInt32(context, buffer[received_size - 1u]);
    SCPI_ResultUInt32(context, mismatch_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_adapter_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    refmem_pio_spi_adapter_snapshot_t snapshot;
    if (!refmem_pio_spi_adapter_get_snapshot(&s_refmem_sync.adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.adapter_id);
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.capability_mask);
    SCPI_ResultUInt32(context, snapshot.max_payload_size);
    SCPI_ResultUInt32(context, snapshot.preferred_mtu);
    SCPI_ResultUInt32(context, snapshot.latency_class_us);
    SCPI_ResultUInt32(context, snapshot.tx_count);
    SCPI_ResultUInt32(context, snapshot.rx_count);
    SCPI_ResultUInt32(context, snapshot.tx_reject_count);
    SCPI_ResultUInt32(context, snapshot.rx_empty_count);
    SCPI_ResultUInt32(context, snapshot.bad_frame_count);
    SCPI_ResultUInt32(context, snapshot.drop_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.last_tx_size);
    SCPI_ResultUInt32(context, snapshot.last_rx_size);
    SCPI_ResultUInt32(context, snapshot.last_rx_timestamp);
    SCPI_ResultUInt32(context, snapshot.rx_pending);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_auto(scpi_t *context)
{
    uint32_t enabled = 0u;
    uint32_t local_slot = 0u;
    uint32_t target_mask = 0xFFu;
    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    uint32_t deadline_us = 1000000u;
    uint32_t uplink_duplex_mode = DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF;
    uint32_t downlink_duplex_mode = DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF;
    refmem_spi_physical_pin_config_t uplink_adapter_pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = REFMEM_SPI_PHYSICAL_PIN_UNUSED,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    refmem_spi_physical_pin_config_t downlink_adapter_pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = REFMEM_SPI_PHYSICAL_PIN_UNUSED,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };

    if (!scpi_port_read_u32(context, &enabled)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &local_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &deadline_us, FALSE);
    (void)SCPI_ParamUInt32(context, &uplink_duplex_mode, FALSE);
    (void)SCPI_ParamUInt32(context, &uplink_adapter_pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &uplink_adapter_pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &uplink_adapter_pins.tx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &downlink_duplex_mode, FALSE);
    (void)SCPI_ParamUInt32(context, &downlink_adapter_pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &downlink_adapter_pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &downlink_adapter_pins.tx_pin, FALSE);

    if (!distributed_refmem_configure_node_load_auto_sync(enabled,
                                                          local_slot,
                                                          target_mask,
                                                          baud_hz,
                                                          deadline_us,
                                                          uplink_duplex_mode,
                                                          &uplink_adapter_pins,
                                                          downlink_duplex_mode,
                                                          &downlink_adapter_pins)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_AUTO");
        return SCPI_RES_ERR;
    }

    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_refmem_sync_auto_q(scpi_t *context)
{
    distributed_refmem_node_load_auto_sync_snapshot_t snapshot;
    distributed_refmem_get_node_load_auto_sync(&snapshot);

    SCPI_ResultUInt32(context, snapshot.enabled);
    SCPI_ResultUInt32(context, snapshot.local_slot);
    SCPI_ResultUInt32(context, snapshot.target_mask);
    SCPI_ResultUInt32(context, snapshot.baud_hz);
    SCPI_ResultUInt32(context, snapshot.deadline_us);
    SCPI_ResultUInt32(context, snapshot.uplink_duplex_mode);
    SCPI_ResultUInt32(context, snapshot.uplink_rx_pin);
    SCPI_ResultUInt32(context, snapshot.uplink_sck_pin);
    SCPI_ResultUInt32(context, snapshot.uplink_tx_pin);
    SCPI_ResultUInt32(context, snapshot.downlink_duplex_mode);
    SCPI_ResultUInt32(context, snapshot.downlink_rx_pin);
    SCPI_ResultUInt32(context, snapshot.downlink_sck_pin);
    SCPI_ResultUInt32(context, snapshot.downlink_tx_pin);
    SCPI_ResultUInt32(context, snapshot.pending_count);
    SCPI_ResultUInt32(context, snapshot.active_intent);
    SCPI_ResultUInt32(context, snapshot.active_instance_id);
    SCPI_ResultUInt32(context, snapshot.next_seq32);
    SCPI_ResultUInt32(context, snapshot.submitted_tx_count);
    SCPI_ResultUInt32(context, snapshot.submitted_rx_count);
    SCPI_ResultUInt32(context, snapshot.applied_rx_count);
    SCPI_ResultUInt32(context, snapshot.failed_apply_count);
    SCPI_ResultUInt32(context, snapshot.dropped_pending_count);
    SCPI_ResultUInt32(context, snapshot.last_rx_result);
    SCPI_ResultUInt32(context, snapshot.last_frame_type);
    SCPI_ResultUInt32(context, snapshot.last_source_slot);
    SCPI_ResultUInt32(context, snapshot.last_error);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_status_q(scpi_t *context)
{
    refmem_realtime_tdma_snapshot_t snapshot;
    if (!distributed_refmem_get_realtime_tdma(&snapshot)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.owner_core);
    SCPI_ResultUInt32(context, snapshot.armed);
    SCPI_ResultUInt32(context, snapshot.service_count);
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, snapshot.completed_seq);
    SCPI_ResultUInt32(context, snapshot.dropped_seq);
    SCPI_ResultUInt32(context, snapshot.window_epoch);
    SCPI_ResultUInt32(context, snapshot.window_index);
    SCPI_ResultUInt32(context, snapshot.intent_type);
    SCPI_ResultUInt32(context, snapshot.role);
    SCPI_ResultUInt32(context, snapshot.baud_hz);
    SCPI_ResultUInt32(context, snapshot.rx_pin);
    SCPI_ResultUInt32(context, snapshot.csn_pin);
    SCPI_ResultUInt32(context, snapshot.sck_pin);
    SCPI_ResultUInt32(context, snapshot.tx_pin);
    SCPI_ResultUInt32(context, snapshot.deadline_us);
    SCPI_ResultUInt32(context, snapshot.frame_size);
    SCPI_ResultUInt32(context, snapshot.ready_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    SCPI_ResultUInt32(context, snapshot.overrun_count);
    SCPI_ResultUInt32(context, snapshot.reject_count);
    SCPI_ResultUInt32(context, snapshot.last_result);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.timestamp_source);
    SCPI_ResultUInt32(context, snapshot.timestamp_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.timestamp_flags);
    SCPI_ResultUInt32(context, snapshot.vdc_window_plan_valid);
    SCPI_ResultUInt32(context, snapshot.vdc_window_class);
    SCPI_ResultUInt32(context, snapshot.vdc_schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.vdc_window_miss_count);
    SCPI_ResultUInt32(context, snapshot.vdc_window_wait_ns);
    SCPI_ResultUInt32(context, snapshot.vdc_window_late_ns);
    SCPI_ResultUInt32(context, snapshot.vdc_window_start_ns_lo);
    SCPI_ResultUInt32(context, snapshot.vdc_window_start_ns_hi);
    SCPI_ResultUInt32(context, snapshot.vdc_window_end_ns_lo);
    SCPI_ResultUInt32(context, snapshot.vdc_window_end_ns_hi);
    SCPI_ResultUInt32(context, snapshot.vdc_guard_start_ns_lo);
    SCPI_ResultUInt32(context, snapshot.vdc_guard_start_ns_hi);
    SCPI_ResultUInt32(context, snapshot.vdc_guard_end_ns_lo);
    SCPI_ResultUInt32(context, snapshot.vdc_guard_end_ns_hi);
    SCPI_ResultUInt32(context, snapshot.submit_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.submit_time_ns_hi);
    SCPI_ResultUInt32(context, snapshot.core1_arm_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.core1_arm_time_ns_hi);
    SCPI_ResultUInt32(context, snapshot.core1_start_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.core1_start_time_ns_hi);
    SCPI_ResultUInt32(context, snapshot.core1_done_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.core1_done_time_ns_hi);
    SCPI_ResultUInt32(context, snapshot.core1_elapsed_ns);
    SCPI_ResultUInt32(context, snapshot.foundation_profile_crc32);
    SCPI_ResultUInt32(context, snapshot.foundation_owner_instance_id);
    SCPI_ResultUInt32(context, snapshot.adapter_type);
    SCPI_ResultUInt32(context, snapshot.payload_whitelist_mask);
    SCPI_ResultUInt32(context, snapshot.ring_enabled);
    SCPI_ResultUInt32(context, snapshot.ring_config_seq);
    SCPI_ResultUInt32(context, snapshot.ring_node_count);
    SCPI_ResultUInt32(context, snapshot.ring_local_slot_id);
    SCPI_ResultUInt32(context, snapshot.ring_reference_slot_id);
    SCPI_ResultUInt32(context, snapshot.ring_up_group_id);
    SCPI_ResultUInt32(context, snapshot.ring_down_group_id);
    SCPI_ResultUInt32(context, snapshot.ring_profile_crc32);
    SCPI_ResultUInt32(context, snapshot.ring_schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.ring_up_running);
    SCPI_ResultUInt32(context, snapshot.ring_down_running);
    SCPI_ResultUInt32(context, snapshot.ring_seq);
    SCPI_ResultUInt32(context, snapshot.ring_last_error);
    SCPI_ResultUInt32(context, snapshot.simultaneous_feedback_loop_evidence);
    SCPI_ResultUInt32(context, snapshot.payload_registry_config_seq);
    SCPI_ResultUInt32(context, snapshot.payload_registry_registration_seq);
    SCPI_ResultUInt32(context, snapshot.payload_registry_used_count);
    SCPI_ResultUInt32(context, snapshot.payload_registry_admitted_count);
    SCPI_ResultUInt32(context, snapshot.payload_registry_reject_count);
    SCPI_ResultUInt32(context, snapshot.payload_registry_last_result);
    SCPI_ResultUInt32(context, snapshot.payload_registry_last_payload_class);
    SCPI_ResultUInt32(context, snapshot.ring_config_reject_count);
    SCPI_ResultUInt32(context, snapshot.traffic_scheduler_configured);
    SCPI_ResultUInt32(context, snapshot.traffic_scheduler_enqueue_seq);
    SCPI_ResultUInt32(context, snapshot.traffic_scheduler_dispatch_seq);
    SCPI_ResultUInt32(context, snapshot.traffic_scheduler_queued_count);
    SCPI_ResultUInt32(context, snapshot.traffic_scheduler_fault_latched);
    SCPI_ResultUInt32(context, snapshot.traffic_scheduler_last_result);
    SCPI_ResultUInt32(context, snapshot.traffic_scheduler_last_class);
    for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
        SCPI_ResultUInt32(context,
                          snapshot.traffic_scheduler_completed_seq[i]);
    }
    SCPI_ResultUInt32(context, snapshot.ring_feedback_timeout_ns);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_started);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_start_count);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_stop_count);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_service_count);
    SCPI_ResultUInt32(context, snapshot.ring_up_tx_sequence);
    SCPI_ResultUInt32(context, snapshot.ring_down_rx_sequence);
    SCPI_ResultUInt32(context, snapshot.ring_up_tx_frame_crc32);
    SCPI_ResultUInt32(context, snapshot.ring_down_rx_frame_crc32);
    SCPI_ResultUInt32(context, snapshot.ring_timestamp_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.ring_timestamp_flags);
    SCPI_ResultUInt32(context, snapshot.ring_idle_beacon_tx_count);
    SCPI_ResultUInt32(context, snapshot.ring_idle_beacon_rx_count);
    SCPI_ResultUInt32(context, snapshot.ring_feedback_round_trip_ns);
    SCPI_ResultUInt32(context, snapshot.ring_reference_tx_timestamp_ns_lo);
    SCPI_ResultUInt32(context, snapshot.ring_reference_tx_timestamp_ns_hi);
    SCPI_ResultUInt32(context, snapshot.ring_feedback_rx_timestamp_ns_lo);
    SCPI_ResultUInt32(context, snapshot.ring_feedback_rx_timestamp_ns_hi);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_last_error);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_tx_count);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_rx_count);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_rx_bad_count);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_rx_transport_bad_count);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_rx_schedule_bad_count);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_rx_profile_bad_count);
    SCPI_ResultUInt32(context,
                      snapshot.ring_adapter_last_bad_transport_result);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_last_bad_sequence);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_last_bad_schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.ring_adapter_last_bad_profile_crc32);
    SCPI_ResultUInt32(
        context, snapshot.ring_adapter_last_bad_header_diff_count);
    SCPI_ResultUInt32(
        context, snapshot.ring_adapter_last_bad_header_first_diff_offset);
    SCPI_ResultUInt32(
        context, snapshot.ring_adapter_last_bad_header_expected_byte);
    SCPI_ResultUInt32(
        context, snapshot.ring_adapter_last_bad_header_observed_byte);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_valid);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_node_count);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_source_node);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_reference_node);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_sequence);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_frame_crc32);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_flags);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_correlated);
    SCPI_ResultUInt32(context, snapshot.ring_clock_observation_correlation_flags);
    SCPI_ResultUInt32(context, snapshot.ring_clock_reference_tx_phase_ns);
    SCPI_ResultUInt32(context, snapshot.ring_clock_local_rx_phase_ns);
    SCPI_ResultUInt32(
        context, snapshot.ring_clock_common_effective_time_ns_lo);
    SCPI_ResultUInt32(
        context, snapshot.ring_clock_common_effective_time_ns_hi);
    SCPI_ResultUInt32(
        context, snapshot.ring_clock_reference_tx_timestamp_ns_lo);
    SCPI_ResultUInt32(
        context, snapshot.ring_clock_reference_tx_timestamp_ns_hi);
    SCPI_ResultUInt32(context, snapshot.ring_clock_local_rx_timestamp_ns_lo);
    SCPI_ResultUInt32(context, snapshot.ring_clock_local_rx_timestamp_ns_hi);
    SCPI_ResultUInt32(context, snapshot.recovery_reserved_bytes_per_cycle);
    SCPI_ResultUInt32(context, snapshot.recovery_buffer_count);
    SCPI_ResultUInt32(context, snapshot.recovery_max_frames_per_cycle);
    SCPI_ResultUInt32(context, snapshot.recovery_current_depth);
    SCPI_ResultUInt32(context, snapshot.recovery_queue_high_watermark);
    SCPI_ResultUInt32(context, snapshot.recovery_queued_count);
    SCPI_ResultUInt32(context, snapshot.recovery_dispatched_count);
    SCPI_ResultUInt32(context, snapshot.recovery_sent_count);
    SCPI_ResultUInt32(context, snapshot.recovery_retry_count);
    SCPI_ResultUInt32(context, snapshot.recovery_exhausted_count);
    SCPI_ResultUInt32(context, snapshot.recovery_backpressure_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_node_tx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t instance_id = 0u;
    uint32_t target_mask = 0xFFu;
    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    uint32_t deadline_us = 1000u;
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = BOARD_REFMEM_SPI_CSN_PIN,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    if (!scpi_port_read_u32(context, &instance_id)) {
        return SCPI_RES_ERR;
    }
    (void)SCPI_ParamUInt32(context, &target_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &deadline_us, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.csn_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.tx_pin, FALSE);
    if (target_mask > 0xFFu) {
        return SCPI_RES_ERR;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    const uint32_t seq32 = s_refmem_sync.tx_seq32++;
    if (!distributed_refmem_build_node_load_sync_frame(instance_id,
                                                       s_refmem_sync.context.local_slot,
                                                       (uint8_t)target_mask,
                                                       s_refmem_sync.context.active_epoch_id,
                                                       s_refmem_sync.context.active_run_id,
                                                       seq32,
                                                       osal_tick_ms(),
                                                       frame,
                                                       sizeof(frame),
                                                       &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_NODE_FRAME");
        return SCPI_RES_ERR;
    }

    const refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_refmem_sync.context.active_epoch_id,
        .window_index = seq32,
        .deadline_us = deadline_us,
        .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
        .baud_hz = baud_hz,
        .pins = pins,
        .frame = frame,
        .frame_size = frame_size,
    };
    if (!distributed_refmem_submit_realtime_tdma_tx(&config)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_NODE_TX_BUSY");
        return SCPI_RES_ERR;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)distributed_refmem_get_realtime_tdma(&snapshot);
    SCPI_ResultText(context, "ACCEPTED");
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, instance_id);
    SCPI_ResultUInt32(context, (uint32_t)frame_size);
    SCPI_ResultUInt32(context, seq32);
    SCPI_ResultUInt32(context, target_mask);
    SCPI_ResultUInt32(context, baud_hz);
    SCPI_ResultUInt32(context, deadline_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_tx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    const char *hex = NULL;
    size_t hex_len = 0u;
    if (SCPI_ParamCharacters(context, &hex, &hex_len, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    uint32_t deadline_us = 1000u;
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = BOARD_REFMEM_SPI_CSN_PIN,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &deadline_us, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.csn_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.tx_pin, FALSE);
    if (!scpi_refmem_sync_hex_decode(hex,
                                     hex_len,
                                     frame,
                                     sizeof(frame),
                                     &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_TX_HEX");
        return SCPI_RES_ERR;
    }

    const refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_refmem_sync.context.active_epoch_id,
        .window_index = s_refmem_sync.tx_seq32,
        .deadline_us = deadline_us,
        .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
        .baud_hz = baud_hz,
        .pins = pins,
        .frame = frame,
        .frame_size = frame_size,
    };
    if (!distributed_refmem_submit_realtime_tdma_tx(&config)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_TX_BUSY");
        return SCPI_RES_ERR;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)distributed_refmem_get_realtime_tdma(&snapshot);
    SCPI_ResultText(context, "ACCEPTED");
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, (uint32_t)frame_size);
    SCPI_ResultUInt32(context, baud_hz);
    SCPI_ResultUInt32(context, deadline_us);
    SCPI_ResultUInt32(context, pins.rx_pin);
    SCPI_ResultUInt32(context, pins.csn_pin);
    SCPI_ResultUInt32(context, pins.sck_pin);
    SCPI_ResultUInt32(context, pins.tx_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_rx(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    uint32_t deadline_us = 1000000u;
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = BOARD_REFMEM_SPI_CSN_PIN,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    (void)SCPI_ParamUInt32(context, &deadline_us, FALSE);
    (void)SCPI_ParamUInt32(context, &baud_hz, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.rx_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.csn_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.sck_pin, FALSE);
    (void)SCPI_ParamUInt32(context, &pins.tx_pin, FALSE);

    const refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_refmem_sync.context.active_epoch_id,
        .window_index = s_refmem_sync.tx_seq32,
        .deadline_us = deadline_us,
        .role = REFMEM_SPI_PHYSICAL_ROLE_SLAVE,
        .baud_hz = baud_hz,
        .pins = pins,
        .frame = NULL,
        .frame_size = 0u,
    };
    if (!distributed_refmem_submit_realtime_tdma_rx(&config)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_RX_BUSY");
        return SCPI_RES_ERR;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)distributed_refmem_get_realtime_tdma(&snapshot);
    SCPI_ResultText(context, "ACCEPTED");
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, deadline_us);
    SCPI_ResultUInt32(context, baud_hz);
    SCPI_ResultUInt32(context, pins.rx_pin);
    SCPI_ResultUInt32(context, pins.csn_pin);
    SCPI_ResultUInt32(context, pins.sck_pin);
    SCPI_ResultUInt32(context, pins.tx_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_frame_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint8_t frame[SCPI_REFMEM_SYNC_FRAME_MAX];
    size_t frame_size = 0u;
    if (!distributed_refmem_get_realtime_tdma_frame(frame, sizeof(frame), &frame_size)) {
        scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_NO_FRAME");
        return SCPI_RES_ERR;
    }

    const uint32_t timestamp = osal_tick_ms();
    if (refmem_pio_spi_adapter_inject_rx_frame(&s_refmem_sync.adapter,
                                               frame,
                                               frame_size,
                                               timestamp)) {
        uint8_t staged_frame[SCPI_REFMEM_SYNC_FRAME_MAX];
        size_t staged_size = 0u;
        if (!refmem_pio_spi_adapter_poll(&s_refmem_sync.adapter,
                                         staged_frame,
                                         sizeof(staged_frame),
                                         &staged_size)) {
            scpi_port_push_exec_error(context, "REFMEM_SYNC_TDMA_FRAME_POLL");
            return SCPI_RES_ERR;
        }
        (void)refmem_sync_receive_frame(scpi_refmem_sync_context(),
                                        staged_frame,
                                        staged_size,
                                        &s_refmem_sync.last_rx);
    } else {
        (void)refmem_sync_receive_frame(scpi_refmem_sync_context(),
                                        frame,
                                        frame_size,
                                        &s_refmem_sync.last_rx);
    }
    scpi_refmem_sync_apply_node_load_delta(&s_refmem_sync.last_rx);

    char hex[SCPI_REFMEM_SYNC_HEX_MAX];
    scpi_refmem_sync_hex_encode(frame, frame_size, hex, sizeof(hex));
    SCPI_ResultText(context, s_refmem_sync.last_rx.accepted != 0u ? "ACCEPTED" : "REJECTED");
    scpi_refmem_sync_result_rx_snapshot(context, &s_refmem_sync.last_rx);
    SCPI_ResultUInt32(context, (uint32_t)frame_size);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_vdc_q(scpi_t *context)
{
    if (!scpi_refmem_sync_ensure_initialized()) {
        return SCPI_RES_ERR;
    }

    uint32_t local_slot = s_refmem_sync.context.local_slot;
    uint32_t reference_slot = 0u;
    (void)SCPI_ParamUInt32(context, &local_slot, FALSE);
    (void)SCPI_ParamUInt32(context, &reference_slot, FALSE);

    vdc_tdma_schedule_profile_t schedule;
    vdc_domain_default_schedule(&schedule, local_slot, reference_slot);

    vdc_tdma_frame_envelope_t envelope;
    refmem_vdc_bridge_status_t status;
    if (!distributed_refmem_build_realtime_tdma_vdc_envelope(&schedule,
                                                             &envelope,
                                                             &status)) {
        SCPI_ResultText(context, "REJECTED");
        SCPI_ResultUInt32(context, status.result);
        SCPI_ResultUInt32(context, status.frame_type);
        SCPI_ResultUInt32(context, status.source_slot);
        SCPI_ResultUInt32(context, status.payload_class);
        SCPI_ResultUInt32(context, status.frame_crc32);
        SCPI_ResultUInt32(context, status.payload_crc32);
        SCPI_ResultUInt32(context, status.gate.passed);
        SCPI_ResultUInt32(context, status.gate.reject_code);
        SCPI_ResultUInt32(context, status.gate.reject_slot);
        SCPI_ResultUInt32(context, status.gate.reject_evidence);
        return SCPI_RES_OK;
    }

    SCPI_ResultText(context, "ACCEPTED");
    SCPI_ResultUInt32(context, status.result);
    SCPI_ResultUInt32(context, status.frame_type);
    SCPI_ResultUInt32(context, status.source_slot);
    SCPI_ResultUInt32(context, status.payload_class);
    SCPI_ResultUInt32(context, status.frame_crc32);
    SCPI_ResultUInt32(context, status.payload_crc32);
    SCPI_ResultUInt32(context, status.gate.passed);
    SCPI_ResultUInt32(context, status.gate.reject_code);
    SCPI_ResultUInt32(context, status.gate.reject_slot);
    SCPI_ResultUInt32(context, status.gate.reject_evidence);
    SCPI_ResultUInt32(context, envelope.frame_version);
    SCPI_ResultUInt32(context, envelope.frame_seq);
    SCPI_ResultUInt32(context, envelope.schedule_epoch);
    SCPI_ResultUInt32(context, envelope.slot_index);
    SCPI_ResultUInt32(context, envelope.source_slot_id);
    SCPI_ResultUInt32(context, envelope.reference_slot_id);
    SCPI_ResultUInt32(context, envelope.window_class);
    SCPI_ResultUInt32(context, envelope.payload_class);
    SCPI_ResultUInt32(context, (uint32_t)(envelope.window_start_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(envelope.window_start_ns >> 32u));
    SCPI_ResultUInt32(context, envelope.schedule_crc32);
    SCPI_ResultUInt32(context, envelope.frame_crc32);
    SCPI_ResultUInt32(context, envelope.payload_crc32);
    SCPI_ResultUInt32(context, envelope.timestamp.timestamp_source);
    SCPI_ResultUInt32(context, envelope.timestamp.timestamp_resolution_ns);
    SCPI_ResultUInt32(context, envelope.timestamp.timestamp_flags);
    SCPI_ResultUInt32(context, envelope.timestamp.late_ns);
    SCPI_ResultUInt32(context, envelope.timestamp.jitter_ns);
    SCPI_ResultUInt32(context, envelope.timestamp.delay_ns);
    SCPI_ResultUInt32(context,
                      (uint32_t)(envelope.timestamp.observed_time_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(envelope.timestamp.observed_time_ns >> 32u));
    SCPI_ResultUInt32(context,
                      (uint32_t)(envelope.timestamp.done_time_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(envelope.timestamp.done_time_ns >> 32u));
    return SCPI_RES_OK;
}

/* Receive-side lifecycle telemetry for the dedicated follower command path.
 * Unlike TDMA:VDC?, this does not synthesize a transmit envelope. */
scpi_result_t scpi_cmd_refmem_sync_tdma_vdc_rx_q(scpi_t *context)
{
    distributed_refmem_vdc_follower_rx_snapshot_t snapshot;
    distributed_refmem_get_vdc_follower_rx(&snapshot);

    SCPI_ResultUInt32(context, snapshot.active);
    SCPI_ResultUInt32(context, snapshot.active_intent_seq);
    SCPI_ResultUInt32(context, snapshot.next_window_seq);
    SCPI_ResultUInt32(context, snapshot.last_processed_completed_seq);
    SCPI_ResultUInt32(context, snapshot.submitted_count);
    SCPI_ResultUInt32(context, snapshot.frame_ready_count);
    SCPI_ResultUInt32(context, snapshot.accepted_count);
    SCPI_ResultUInt32(context, snapshot.invalid_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    SCPI_ResultUInt32(context, snapshot.window_miss_count);
    SCPI_ResultUInt32(context, snapshot.submit_reject_count);
    SCPI_ResultUInt32(context, snapshot.last_result);
    SCPI_ResultUInt32(context, snapshot.last_rx_result);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.last_source_slot);
    SCPI_ResultUInt32(context, snapshot.last_command_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_tdma_abort(scpi_t *context)
{
    distributed_refmem_abort_realtime_tdma();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_refmem_sync_tdma_vdc_flight_q(scpi_t *context)
{
    distributed_refmem_vdc_flight_rx_snapshot_t snapshot;
    if (!distributed_refmem_get_vdc_flight_rx(&snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }
    SCPI_ResultUInt32(context, snapshot.retained);
    SCPI_ResultUInt32(context, snapshot.active);
    SCPI_ResultUInt32(context, snapshot.source_slot);
    SCPI_ResultUInt32(context, snapshot.local_slot);
    SCPI_ResultUInt32(context, snapshot.control_generation);
    SCPI_ResultUInt32(context, snapshot.ring_config_seq);
    SCPI_ResultUInt32(context, snapshot.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.mailbox_seq);
    SCPI_ResultUInt32(context, snapshot.receive_count);
    SCPI_ResultInt32(context, snapshot.phase_offset_ns);
    SCPI_ResultInt32(context, snapshot.period_adjust_ppb);
    SCPI_ResultUInt32(context, snapshot.lock_state);
    SCPI_ResultUInt32(context, snapshot.quality);
    SCPI_ResultUInt32(context, snapshot.mailbox_crc16);
    SCPI_ResultUInt32(context, snapshot.rx_admission_epoch);
    SCPI_ResultUInt32(context, snapshot.transport_sequence);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_flight_q(scpi_t *context)
{
    distributed_refmem_tdma_flight_sync_snapshot_t snapshot;
    distributed_refmem_get_tdma_flight_sync(&snapshot);

    SCPI_ResultUInt32(context, snapshot.enabled);
    SCPI_ResultUInt32(context, snapshot.local_slot);
    SCPI_ResultUInt32(context, snapshot.node_count);
    SCPI_ResultUInt32(context, snapshot.active_mask);
    SCPI_ResultUInt32(context, snapshot.reference_slot);
    SCPI_ResultUInt32(context, snapshot.remote_slot);
    SCPI_ResultUInt32(context, snapshot.payload_size);
    SCPI_ResultUInt32(context, snapshot.mailbox_size);
    SCPI_ResultUInt32(context, snapshot.publish_interval_ms);
    SCPI_ResultUInt32(context, snapshot.next_seq32);
    SCPI_ResultUInt32(context, snapshot.tx_publish_count);
    SCPI_ResultUInt32(context, snapshot.tx_reject_count);
    SCPI_ResultUInt32(context, snapshot.rx_acquire_count);
    SCPI_ResultUInt32(context, snapshot.rx_empty_count);
    SCPI_ResultUInt32(context, snapshot.rx_accept_count);
    SCPI_ResultUInt32(context, snapshot.rx_reject_count);
    SCPI_ResultUInt32(context, snapshot.rx_duplicate_skip_count);
    SCPI_ResultUInt32(context, snapshot.rx_bad_mailbox_count);
    SCPI_ResultUInt32(context, snapshot.last_rx_result);
    SCPI_ResultUInt32(context, snapshot.last_frame_type);
    SCPI_ResultUInt32(context, snapshot.last_source_slot);
    SCPI_ResultUInt32(context, snapshot.last_seq32);
    SCPI_ResultUInt32(context, snapshot.last_value_u32);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.wire_layout_version);
    SCPI_ResultInt32(context, snapshot.last_vdc_phase_offset_ns);
    SCPI_ResultInt32(context, snapshot.last_vdc_rate_adjust_ppb);
    SCPI_ResultUInt32(context, snapshot.last_vdc_lock_state);
    SCPI_ResultUInt32(context, snapshot.last_vdc_quality);
    SCPI_ResultUInt32(context, snapshot.last_ack_seq16);
    SCPI_ResultUInt32(context, snapshot.last_ack_flags);
    SCPI_ResultUInt32(context, snapshot.last_control_opcode);
    SCPI_ResultUInt32(context, snapshot.last_control_seq8);
    SCPI_ResultUInt32(context, snapshot.last_optional_diagnostic);
    SCPI_ResultUInt32(context, snapshot.last_mailbox_crc16);
    SCPI_ResultUInt32(context, snapshot.vdc_command_fragment_rx_count);
    SCPI_ResultUInt32(context, snapshot.vdc_command_fragment_complete_count);
    SCPI_ResultUInt32(context, snapshot.vdc_command_fragment_reject_count);
    SCPI_ResultUInt32(context, snapshot.vdc_command_accept_count);
    SCPI_ResultUInt32(context, snapshot.vdc_command_last_seq);
    SCPI_ResultUInt32(context, snapshot.vdc_command_prepare_attempt_count);
    SCPI_ResultUInt32(context, snapshot.vdc_command_prepare_reject_count);
    SCPI_ResultUInt32(context, snapshot.vdc_command_prepare_last_reason);
    SCPI_ResultUInt32(context, snapshot.vdc_command_prepare_last_gate_code);
    SCPI_ResultUInt32(context, snapshot.vdc_command_prepare_last_gate_slot);
    SCPI_ResultUInt32(context, snapshot.vdc_command_prepare_last_gate_evidence);
    SCPI_ResultUInt32(context, snapshot.vdc_command_record_active);
    SCPI_ResultUInt32(context, snapshot.vdc_command_record_fragment_index);
    SCPI_ResultUInt32(context, snapshot.vdc_command_record_source_update_seq);
    SCPI_ResultUInt32(context, snapshot.vdc_command_last_common_time_ns_lo);
    SCPI_ResultUInt32(context, snapshot.vdc_command_last_common_time_ns_hi);

    /* Appended: WHY the VDC feedback path last refused to let a frame out. A refusal
     * stops the whole flight publish path (no VDC and no T2 frame), so the branch is
     * exposed instead of guessed. Buckets: 1 not-ready/tx-active, 2 not-ready/offer,
     * 3 snapshot busy, 4 binding changed. */
    uint32_t feedback_reason[8];
    const uint32_t feedback_last =
        distributed_refmem_feedback_prepare_reason(feedback_reason, 8u);
    SCPI_ResultUInt32(context, feedback_last);
    for (uint32_t i = 0u; i < 8u; ++i) SCPI_ResultUInt32(context, feedback_reason[i]);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_sync_flight_position_q(scpi_t *context)
{
    distributed_refmem_position_transport_snapshot_t snapshot;
    if (!distributed_refmem_get_position_transport(&snapshot)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, snapshot.tx_pending);
    SCPI_ResultUInt32(context, snapshot.tx_active);
    SCPI_ResultUInt32(context, snapshot.tx_fragment_index);
    SCPI_ResultUInt32(context, snapshot.tx_publish_count);
    SCPI_ResultUInt32(context, snapshot.tx_reject_count);
    SCPI_ResultUInt32(context, snapshot.tx_position_id);
    SCPI_ResultUInt32(context, snapshot.tx_phase);
    SCPI_ResultUInt32(context, snapshot.tx_wait_reason);
    SCPI_ResultUInt32(context, snapshot.tx_wait_count);
    SCPI_ResultUInt32(context, snapshot.tx_submitted_sequence);
    SCPI_ResultUInt32(context, snapshot.rx_complete_count);
    SCPI_ResultUInt32(context, snapshot.rx_reject_count);
    SCPI_ResultUInt32(context, snapshot.rx_pending_mask);
    SCPI_ResultUInt32(context, snapshot.rx_source_slot);
    SCPI_ResultUInt32(context, snapshot.rx_position_id);
    SCPI_ResultUInt32(context, snapshot.cancel_requested);
    SCPI_ResultUInt32(context, snapshot.rx_phase);
    SCPI_ResultUInt32(context, snapshot.rx_last_failure);
    SCPI_ResultUInt32(context, snapshot.lane_accept_count);
    SCPI_ResultUInt32(context, snapshot.lane_duplicate_count);
    SCPI_ResultUInt32(context, snapshot.lane_reject_count);
    SCPI_ResultUInt32(context, snapshot.lane_owned);
    SCPI_ResultUInt32(context, snapshot.lane_hold_count);
    SCPI_ResultUInt32(context, snapshot.lane_drain_count);
    SCPI_ResultUInt32(context, snapshot.lane_drain_fail_count);
    SCPI_ResultUInt32(context, snapshot.lane_ack_override_count);
    SCPI_ResultUInt32(context, snapshot.lane_hold_expired_count);
    SCPI_ResultUInt32(context, snapshot.lane_hold_dropped_count);
    SCPI_ResultUInt32(context, snapshot.lane_diag_refused_count);
    SCPI_ResultUInt32(context, snapshot.lane_decode_reject_count);
    /* Appended last: positional readers keep their existing indices. */
    SCPI_ResultUInt32(context, snapshot.lane_hold_delivered_count);
    SCPI_ResultUInt32(context, snapshot.lane_freeze_latched_count);
    SCPI_ResultUInt32(context, snapshot.lane_freeze_kept_count);
    SCPI_ResultUInt32(context, snapshot.lane_freeze_taken_count);
    /* Appended last: how many staged updates were MERGED into a newer value before the
     * wire (latest-value TX table). Non-zero proves the merge; it is not a loss. */
    SCPI_ResultUInt32(context, snapshot.tx_coalesced_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_fifo_q(scpi_t *context)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    tdma_flight_fifo_snapshot_t snapshot;
    if (owner == NULL ||
        !tdma_service_get_flight_fifo_snapshot(owner, &snapshot)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, snapshot.version);
    SCPI_ResultUInt32(context, snapshot.tx_publish_count);
    SCPI_ResultUInt32(context, snapshot.tx_publish_reject_count);
    SCPI_ResultUInt32(context, snapshot.tx_acquire_count);
    SCPI_ResultUInt32(context, snapshot.tx_image_stale_count);
    SCPI_ResultUInt32(context, snapshot.tx_reuse_count);
    SCPI_ResultUInt32(context, snapshot.tx_release_count);
    SCPI_ResultUInt32(context, snapshot.tx_ready_count);
    SCPI_ResultUInt32(context, snapshot.tx_active_slot);
    SCPI_ResultUInt32(context, snapshot.tx_active_generation);
    SCPI_ResultUInt32(context, snapshot.rx_publish_count);
    SCPI_ResultUInt32(context, snapshot.rx_mirror_drop_count);
    SCPI_ResultUInt32(context, snapshot.rx_publish_drop_count);
    SCPI_ResultUInt32(context, snapshot.rx_acquire_count);
    SCPI_ResultUInt32(context, snapshot.rx_release_count);
    SCPI_ResultUInt32(context, snapshot.rx_queued_count);
    SCPI_ResultUInt32(context, snapshot.rx_parse_count);
    /* Appended attribution of tx_publish_reject_count; the 17 primary fields
     * above keep their existing order and index. */
    SCPI_ResultUInt32(context, snapshot.tx_reject_bad_payload_count);
    SCPI_ResultUInt32(context, snapshot.tx_reject_ring_full_count);
    SCPI_ResultUInt32(context, snapshot.tx_reject_no_slot_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_fifo_reset(scpi_t *context)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    for (uint32_t attempt = 0u; attempt < SCPI_TDMA_FIFO_RESET_WAIT_LOOPS; ++attempt) {
        const tdma_service_flight_fifo_reset_result_t result =
            tdma_service_reset_flight_fifo_checked(owner);
        if (result == TDMA_SERVICE_FLIGHT_FIFO_RESET_OK) {
            SCPI_ResultText(context, "OK");
            return SCPI_RES_OK;
        }
        if (result != TDMA_SERVICE_FLIGHT_FIFO_RESET_BUSY) {
            scpi_port_push_exec_error(context, "TDMA_FLIGHT_FIFO_RESET");
            return SCPI_RES_ERR;
        }
        /* The RefMem task must finish its copy/view before reset can reclaim
         * storage. Only Core0 SCPI yields here; Core1 never takes this lock. */
        osal_task_delay_ms(1u);
    }
    scpi_port_push_exec_error(context, "TDMA_FLIGHT_FIFO_RESET_BUSY");
    return SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_system_tdma_seat_rotation(scpi_t *context)
{
    uint32_t priority_share = 0u, first_class_share = 0u, second_class_share = 0u;
    if (!scpi_port_read_u32(context, &priority_share) ||
        !scpi_port_read_u32(context, &first_class_share)) {
        scpi_port_push_exec_error(context, "TDMA_SEAT_ROTATION");
        return SCPI_RES_ERR;
    }
    /* Optional third field (owner decision 2026-10-16): slots of the cycle reserved for
     * the path that does not use the fast channel. Absent => 0 => the two-share
     * behaviour, so an existing caller keeps working unchanged. */
    uint32_t third = 0u;
    const bool has_third = SCPI_ParamUInt32(context, &third, FALSE) == TRUE;
    if (has_third) second_class_share = third;
    if (!tdma_seat_rotation_valid3(priority_share, first_class_share,
                                  second_class_share) ||
        !tdma_runtime_owner_set_seat_rotation3(priority_share, first_class_share,
                                              second_class_share)) {
        scpi_port_push_exec_error(context, "TDMA_SEAT_ROTATION");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, priority_share);
    SCPI_ResultUInt32(context, first_class_share);
    SCPI_ResultUInt32(context, second_class_share);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_seat_rotation_q(scpi_t *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    if (adapter == NULL ||
        !tdma_pio_spi_ring_adapter_get_snapshot(adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }
    /* Configured radio first, then the DECLARED limits it is bounded by. A
     * capture has to be readable without the firmware source: the seat's
     * counters are only evidence against the budget they were measured
     * against, and the starvation counters stay zero unless the seat-window
     * schedule is compiled in at all. */
    SCPI_ResultUInt32(context, snapshot.seat_rotation_priority_share);
    SCPI_ResultUInt32(context, snapshot.seat_rotation_first_class_share);
    const uint32_t priority_share = tdma_seat_rotation_priority(
        snapshot.seat_rotation_priority_share);
    const uint32_t first_class_share = tdma_seat_rotation_first_class(
        snapshot.seat_rotation_first_class_share);
    SCPI_ResultUInt32(context, priority_share);
    SCPI_ResultUInt32(context, first_class_share);
    /* Cycle length actually walked: the two fast seats plus the slots reserved for the
     * path that does not use the fast channel (0 => the pre-three-share cycle). */
    const uint32_t second_class_share = snapshot.seat_rotation_second_class_share;
    SCPI_ResultUInt32(context, priority_share + first_class_share + second_class_share);
    SCPI_ResultUInt32(context, TDMA_SEAT_ROTATION_SHARE_MAX);
    SCPI_ResultUInt32(context, TDMA_SEAT_ROTATION_CYCLE_MAX);
    SCPI_ResultUInt32(context, TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
    SCPI_ResultUInt32(context, DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH);
    SCPI_ResultUInt32(context, DISTRIBUTED_REFMEM_POSITION_HOLD_MAX_ATTEMPTS);
    SCPI_ResultUInt32(context, TDMA_LOOP_SCHEDULE_ENABLED);
    /* Appended for the three-share form: reserved second-class slots per cycle. Older
     * readers take the first 11 fields and see 0 here only when nothing is reserved. */
    SCPI_ResultUInt32(context, second_class_share);
    return SCPI_RES_OK;
}

/* Origin fast-path seat evidence: opportunity/boarding counters that the M:N
 * ratio is verified against. Read-only, valid while stopped as well. */
scpi_result_t scpi_cmd_system_tdma_seat_counters_q(scpi_t *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    if (adapter == NULL ||
        !tdma_pio_spi_ring_adapter_get_snapshot(adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.origin.seat_opportunity_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_priority_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_priority_yield_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_reject_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_wait_max);
    SCPI_ResultUInt32(context, snapshot.first_class_stage_refused_count);
    SCPI_ResultUInt32(context, snapshot.first_class_retired_waiting_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_wait_violation_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_starved_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_starved_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_deferred_count);
    return SCPI_RES_OK;
}

/* TDMA-SEAT-03 seat health verdict. Folds the origin seat counters and the
 * follower's first-class lane counters into `ok,reason_mask` followed by the raw
 * evidence, so one read answers "is the first-class seat behaving like the
 * priority seat, and if not, why". Read-only; valid while stopped. */
scpi_result_t scpi_cmd_system_tdma_seat_health_q(scpi_t *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    distributed_refmem_position_transport_snapshot_t lane;
    tdma_priority_rx_snapshot_t rx;
    trigger_t2_runtime_snapshot_t consumer;
    /* The per-class reject split lives in the priority-lane snapshot, not in the
     * position transport snapshot; read each fact from its own owner. The lane
     * snapshot is only readable while the adapter is started, so this page
     * requires it: no data beats a zero that looks healthy. The consumer
     * snapshot is what proves a first-class position was lost end to end, so a
     * failed read must fail the page rather than report healthy. */
    if (adapter == NULL ||
        !tdma_pio_spi_ring_adapter_get_snapshot(adapter, &snapshot) ||
        !tdma_runtime_owner_get_priority_rx_snapshot(&rx) ||
        !distributed_refmem_get_position_transport(&lane) ||
        !trigger_t2_runtime_snapshot(&consumer)) {
        scpi_port_push_exec_error(context, "TDMA_SEAT_HEALTH_REQUIRES_STARTED_LANE");
        return SCPI_RES_ERR;
    }
    /* Both are losses, not by-design backpressure: the origin gave up completing
     * a position's freeze, or a follower was asked to freeze a position it never
     * had. Saturating, like every other seat counter. */
    uint32_t missed = consumer.freeze_abandoned_count;
    if (UINT32_MAX - missed < consumer.freeze_missing_local_count)
        missed = UINT32_MAX;
    else
        missed += consumer.freeze_missing_local_count;
    const tdma_seat_health_facts_t facts = {
        .lane_hold_dropped_count = lane.lane_hold_dropped_count,
        .lane_hold_expired_count = lane.lane_hold_expired_count,
        .lane_ack_override_count = lane.lane_ack_override_count,
        .wait_violation_count = snapshot.origin.seat_first_class_wait_violation_count,
        .lane_reject_first_class_count = rx.first_class_reject_count,
        .retired_waiting_count = snapshot.first_class_retired_waiting_count,
        .first_class_starved_count = snapshot.origin.seat_first_class_starved_count,
        .lane_decode_reject_count = lane.lane_decode_reject_count,
        .first_class_missed_count = missed,
    };
    const uint32_t reason = tdma_seat_health_reason(&facts);
    SCPI_ResultUInt32(context, reason == 0u ? 1u : 0u);
    SCPI_ResultUInt32(context, reason);
    SCPI_ResultUInt32(context, snapshot.origin.seat_opportunity_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_priority_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_wait_max);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_wait_violation_count);
    SCPI_ResultUInt32(context, snapshot.first_class_stage_refused_count);
    SCPI_ResultUInt32(context, snapshot.first_class_retired_waiting_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_starved_count);
    SCPI_ResultUInt32(context, snapshot.origin.seat_first_class_deferred_count);
    SCPI_ResultUInt32(context, rx.first_class_reject_count);
    SCPI_ResultUInt32(context, rx.other_class_reject_count);
    SCPI_ResultUInt32(context, rx.unknown_class_reject_count);
    SCPI_ResultUInt32(context, lane.lane_hold_count);
    SCPI_ResultUInt32(context, lane.lane_drain_count);
    SCPI_ResultUInt32(context, lane.lane_drain_fail_count);
    SCPI_ResultUInt32(context, lane.lane_hold_expired_count);
    SCPI_ResultUInt32(context, lane.lane_hold_dropped_count);
    SCPI_ResultUInt32(context, lane.lane_ack_override_count);
    SCPI_ResultUInt32(context, lane.lane_diag_refused_count);
    SCPI_ResultUInt32(context, lane.lane_decode_reject_count);
    /* Appended: the two consumer-side loss facts behind reason bit 8. */
    SCPI_ResultUInt32(context, consumer.freeze_abandoned_count);
    SCPI_ResultUInt32(context, consumer.freeze_missing_local_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_mode(scpi_t *context)
{
    uint32_t process_image = 0u;
    if (!scpi_port_read_u32(context, &process_image) || process_image > 1u ||
        !tdma_runtime_owner_set_flight_process_image_mode(
            process_image != 0u)) {
        scpi_port_push_exec_error(context, "TDMA_FLIGHT_MODE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, process_image);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_mode_q(scpi_t *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        tdma_runtime_owner_get_ring_adapter();
    tdma_pio_spi_ring_adapter_snapshot_t snapshot;
    if (adapter == NULL ||
        !tdma_pio_spi_ring_adapter_get_snapshot(adapter, &snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.forwarding_mode);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_flight_clock_evidence(scpi_t *context)
{
    uint32_t enabled = 0u;
    if (!scpi_port_read_u32(context, &enabled) || enabled > 1u ||
        !tdma_runtime_owner_set_clock_evidence_enabled(enabled != 0u)) {
        scpi_port_push_exec_error(context, "TDMA_FLIGHT_CLOCK_EVIDENCE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}
