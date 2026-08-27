#!/usr/bin/env python3
"""Canonical field schemas for TDMA status and physical snapshots.

The field order is the wire order emitted by
``scpi_cmd_refmem_sync_tdma_status_q``.  Keep this module as the single host
side source of truth: the SCPI response is positional and silently accepting
an older field count turns valid hardware evidence into a false failure (or,
worse, associates a counter with the wrong field).
"""
import csv
import sys


class TdmaStatusParseError(ValueError):
    """Raised when a TDMA status response is not the current schema."""


def parse_status_fields(response: str) -> list[int]:
    """Parse one complete ``REFMEM:SYNC:TDMA:STATus?`` response.

    The response is deliberately required to contain exactly ``len(FIELDS)``
    values.  A short/old response must be reported to the caller instead of
    being padded, because padding would make the ring and timestamp gates use
    unrelated positions.
    """
    try:
        fields = next(csv.reader([response], skipinitialspace=True))
    except csv.Error as exc:
        raise TdmaStatusParseError(f"invalid CSV TDMA status: {response!r}") from exc
    if len(fields) != len(FIELDS):
        raise TdmaStatusParseError(
            f"field count {len(fields)} != {len(FIELDS)}")
    values: list[int] = []
    for field in fields:
        try:
            values.append(int(field.strip().strip('"'), 0))
        except ValueError as exc:
            raise TdmaStatusParseError(
                f"non-integer TDMA status field: {field!r}") from exc
    return values


def parse_status_named(response: str) -> dict[str, int]:
    """Return a validated TDMA status response keyed by field name."""
    values = parse_status_fields(response)
    return dict(zip(FIELDS, values, strict=True))

FIELDS = [
    "state", "owner_core", "armed", "service_count", "intent_seq",
    "completed_seq", "dropped_seq", "window_epoch", "window_index",
    "intent_type", "role", "baud_hz", "rx_pin", "csn_pin", "sck_pin",
    "tx_pin", "deadline_us", "frame_size", "ready_count", "timeout_count",
    "overrun_count", "reject_count", "last_result", "last_error",
    "timestamp_source", "timestamp_resolution_ns", "timestamp_flags",
    "vdc_window_plan_valid", "vdc_window_class", "vdc_schedule_crc32",
    "vdc_window_miss_count", "vdc_window_wait_ns", "vdc_window_late_ns",
    "vdc_window_start_ns_lo", "vdc_window_start_ns_hi",
    "vdc_window_end_ns_lo", "vdc_window_end_ns_hi",
    "vdc_guard_start_ns_lo", "vdc_guard_start_ns_hi",
    "vdc_guard_end_ns_lo", "vdc_guard_end_ns_hi",
    "submit_time_ns_lo", "submit_time_ns_hi",
    "core1_arm_time_ns_lo", "core1_arm_time_ns_hi",
    "core1_start_time_ns_lo", "core1_start_time_ns_hi",
    "core1_done_time_ns_lo", "core1_done_time_ns_hi", "core1_elapsed_ns",
    "foundation_profile_crc32", "foundation_owner_instance_id",
    "adapter_type", "payload_whitelist_mask", "ring_enabled",
    "ring_config_seq", "ring_node_count", "ring_local_slot_id",
    "ring_reference_slot_id", "ring_up_group_id", "ring_down_group_id",
    "ring_profile_crc32", "ring_schedule_crc32", "ring_up_running",
    "ring_down_running", "ring_seq", "ring_last_error",
    "simultaneous_feedback_loop_evidence", "payload_registry_config_seq",
    "payload_registry_registration_seq", "payload_registry_used_count",
    "payload_registry_admitted_count", "payload_registry_reject_count",
    "payload_registry_last_result", "payload_registry_last_payload_class",
    "ring_config_reject_count", "traffic_scheduler_configured",
    "traffic_scheduler_enqueue_seq", "traffic_scheduler_dispatch_seq",
    "traffic_scheduler_queued_count", "traffic_scheduler_fault_latched",
    "traffic_scheduler_last_result", "traffic_scheduler_last_class",
    "tc_completed_seq_0", "tc_completed_seq_1", "tc_completed_seq_2",
    "tc_completed_seq_3", "tc_completed_seq_4",
    "ring_feedback_timeout_ns", "ring_adapter_started",
    "ring_adapter_start_count", "ring_adapter_stop_count",
    "ring_adapter_service_count", "ring_up_tx_sequence",
    "ring_down_rx_sequence", "ring_up_tx_frame_crc32",
    "ring_down_rx_frame_crc32", "ring_timestamp_resolution_ns",
    "ring_timestamp_flags", "ring_idle_beacon_tx_count",
    "ring_idle_beacon_rx_count", "ring_feedback_round_trip_ns",
    "ring_reference_tx_timestamp_ns_lo", "ring_reference_tx_timestamp_ns_hi",
    "ring_feedback_rx_timestamp_ns_lo", "ring_feedback_rx_timestamp_ns_hi",
    "ring_adapter_last_error", "ring_adapter_tx_count",
    "ring_adapter_rx_count", "ring_adapter_rx_bad_count",
    "ring_adapter_rx_transport_bad_count",
    "ring_adapter_rx_schedule_bad_count",
    "ring_adapter_rx_profile_bad_count",
    "ring_adapter_last_bad_transport_result",
    "ring_adapter_last_bad_sequence",
    "ring_adapter_last_bad_schedule_crc32",
    "ring_adapter_last_bad_profile_crc32",
    "ring_adapter_last_bad_header_diff_count",
    "ring_adapter_last_bad_header_first_diff_offset",
    "ring_adapter_last_bad_header_expected_byte",
    "ring_adapter_last_bad_header_observed_byte",
    "ring_clock_observation_valid",
    "ring_clock_observation_node_count",
    "ring_clock_observation_source_node",
    "ring_clock_observation_reference_node",
    "ring_clock_observation_sequence",
    "ring_clock_observation_frame_crc32",
    "ring_clock_observation_schedule_crc32",
    "ring_clock_observation_resolution_ns",
    "ring_clock_observation_flags",
    "ring_clock_observation_correlated",
    "ring_clock_reference_tx_timestamp_ns_lo",
    "ring_clock_reference_tx_timestamp_ns_hi",
    "ring_clock_local_rx_timestamp_ns_lo",
    "ring_clock_local_rx_timestamp_ns_hi",
]

PHYS_FIELDS = (
    "armed", "role", "baud_hz", "tx_count", "rx_count", "rx_bad_count",
    "tx_busy_count", "rx_partial_count", "rx_stall_count",
    "tx_timeout_count", "last_error", "last_rx_size", "tx_sck_pin",
    "tx_pin", "rx_sck_pin", "rx_pin", "last_bad_header0",
    "last_bad_header1", "last_bad_header2", "last_bad_header3",
    "last_bad_words", "rx_busy_count", "rx_magic_fail_count",
    "rx_busy_word0", "rx_busy_word1", "rx_busy_word2", "rx_busy_word3",
    "rx_busy_moved", "rx_magic_at_zero", "rx_magic_at_shift",
    "tx_csn_pin", "rx_csn_pin", "rx_ring_overrun_count",
    "rx_dma_produced_words", "rx_scan_produced_words", "rx_dma_write_index",
    "rx_dma_channel", "tx_edge_count", "rx_edge_count",
    "last_tx_edge_timestamp_ns_lo", "last_tx_edge_timestamp_ns_hi",
    "last_tx_done_timestamp_ns_lo", "last_tx_done_timestamp_ns_hi",
    "last_rx_edge_timestamp_ns_lo", "last_rx_edge_timestamp_ns_hi",
    "last_rx_extract_timestamp_ns_lo", "last_rx_extract_timestamp_ns_hi",
    "program_persona", "program_switch_count", "program_switch_fail_count",
    "flight_marker_offset_sample_count", "flight_sck_offset_sample_count",
    "flight_data_offset_sample_count", "flight_marker_phase_delay_cycles",
    "flight_sck_phase_delay_cycles", "flight_data_phase_delay_cycles",
    "pio_irq_flags", "pio_fdebug", "tx_sm_pc", "rx_sm_pc",
    "tx_sm_tx_fifo_level", "tx_sm_rx_fifo_level",
    "rx_sm_tx_fifo_level", "rx_sm_rx_fifo_level", "gpio_input_levels",
    "origin_done_irq_count", "origin_done_txstall_count",
    "origin_clock_timeout_count", "origin_data_timeout_count",
    "origin_recovery_count",
    "overlay_prepare_count", "overlay_prepare_fail_count",
    "overlay_replacement_byte_count", "overlay_alignment_byte_shift",
    "overlay_alignment_bit_shift", "overlay_physical_byte_count",
    "overlay_last_error", "overlay_tx_dma_remaining",
    "overlay_tx_dma_busy", "overlay_tx_fifo_level_at_fail",
    "overlay_prepare_wait_us",
    "overlay_program_offset", "overlay_tx_dma_read_index",
    "overlay_tx_dma_ctrl", "overlay_sm_shiftctrl", "overlay_sm_execctrl",
    "overlay_sm_pc_at_fail", "overlay_pio_ctrl_at_fail",
    "overlay_pio_fstat_at_fail", "overlay_pio_fdebug_at_fail",
    "overlay_frame_boundary_count", "overlay_pass_recovery_count",
    "overlay_late_coalesce_count",
    "clock_latch_resolution_ns", "clock_latch_count",
    "clock_latch_miss_count",
)

KEY = [
    "service_count", "ring_enabled", "ring_config_seq", "ring_node_count",
    "ring_local_slot_id", "ring_reference_slot_id", "ring_up_group_id",
    "ring_down_group_id", "ring_up_running", "ring_down_running",
    "ring_seq", "ring_adapter_started", "ring_adapter_start_count",
    "ring_adapter_stop_count", "ring_adapter_service_count",
    "ring_up_tx_sequence", "ring_down_rx_sequence",
    "ring_idle_beacon_tx_count", "ring_idle_beacon_rx_count",
    "ring_adapter_last_error", "ring_adapter_tx_count",
    "ring_adapter_rx_count", "ring_adapter_rx_bad_count",
    "ring_adapter_rx_transport_bad_count",
    "ring_adapter_rx_schedule_bad_count",
    "ring_adapter_rx_profile_bad_count",
    "ring_adapter_last_bad_transport_result",
    "ring_adapter_last_bad_sequence",
    "ring_adapter_last_bad_schedule_crc32",
    "ring_adapter_last_bad_profile_crc32",
    "ring_adapter_last_bad_header_diff_count",
    "ring_adapter_last_bad_header_first_diff_offset",
    "ring_adapter_last_bad_header_expected_byte",
    "ring_adapter_last_bad_header_observed_byte",
    "simultaneous_feedback_loop_evidence",
]


def main() -> int:
    data = sys.stdin.read().strip()
    values = [int(v.strip().strip('"'), 0) for v in data.split(",")]
    if len(values) != len(FIELDS):
        print(f"field count mismatch: got {len(values)} expected {len(FIELDS)}",
              file=sys.stderr)
        return 1
    for name in KEY:
        idx = FIELDS.index(name)
        print(f"{name:36s} = {values[idx]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
