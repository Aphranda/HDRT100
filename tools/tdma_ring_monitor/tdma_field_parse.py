#!/usr/bin/env python3
"""Parse the 110-field SYSTem:REFMEM:SYNC:TDMA:STATus? response into named fields."""
import sys

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
]

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
