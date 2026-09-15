"""Decode versioned whole-phase TDMA profiles; nested stages are inclusive."""
import csv


STAGES_V1 = (
    "phys_service", "owner_service", "refmem_publish", "training_gate", "analyzer",
    "accounting", "adapter", "rx_capture", "rx_parse", "overlay_prepare", "overlay_boundary",
)
STAGES_V2 = STAGES_V1 + ("rx_acquire", "rx_packet_copy", "rx_clock", "rx_latch")
STAGES_V3 = STAGES_V2 + ("rx_dma_observe", "rx_locate", "rx_header_check", "rx_ring_copy")
STAGES_V4 = STAGES_V3 + ("ring_runtime", "ring_publish", "intent_dispatch", "adapter_prologue", "rx_handoff", "adapter_status")
STAGES_BY_VERSION = {
    1: STAGES_V1,
    2: STAGES_V2,
    3: STAGES_V3,
    4: STAGES_V4,
    5: STAGES_V4 + ("rx_inspect", "rx_health", "rx_evidence", "rx_fifo_publish", "rx_commit", "rx_complete"),
}
STAGES_BY_VERSION[6] = STAGES_BY_VERSION[5]
STAGES_BY_VERSION[7] = STAGES_BY_VERSION[6] + (
    "rx_request", "rx_request_hint", "rx_local_tx_edge", "tx_latch_read",
    "tx_latch_rearm", "rx_request_publish", "intent_clock", "select_empty",
    "select_blocked", "select_busy", "select_dispatch", "select_refresh", "intent_bind",
)
STAGES_BY_VERSION[8] = STAGES_BY_VERSION[7] + (
    "rx_dma_initial", "rx_dma_frame_recheck", "rx_dma_discovery_recheck",
    "rx_latch_read", "rx_latch_rearm",
)
STAGES_BY_VERSION[9] = STAGES_BY_VERSION[8] + ("origin_observe", "origin_publish")
STAGES_BY_VERSION[10] = STAGES_BY_VERSION[9] + ("origin_admit", "origin_calibration_crc", "origin_begin")
STAGES_BY_VERSION[11] = STAGES_BY_VERSION[10] + (
    "event_entry", "event_harvest", "event_convert", "event_feed", "event_final_check",
    "event_start_cut", "event_retain", "event_publish", "reference_tx", "reference_submit",
)
FIELDS = (
    "version", "clock_hz", "reset_generation", "phase_count", "stage_count",
    "peak", "sequence", "start_ticks", "total_ticks", "invalid_count",
)
FIELDS_V6 = FIELDS + (
    "full_phase_ticks", "entry_state", "entry_config_generation", "entry_trial_epoch",
    "entry_return_sequence", "exit_state", "exit_config_generation", "exit_trial_epoch",
    "exit_return_sequence", "autonomous_phase_count", "other_phase_count",
)


def parse_service_timing(raw: str) -> dict | None:
    """Reject unknown schemas instead of silently assigning the wrong labels."""
    if raw.strip().strip('"') == "UNAVAILABLE":
        return None
    values = list(map(int, next(csv.reader([raw]))))
    if len(values) < len(FIELDS):
        raise ValueError("Truncated TDMA profile header")
    stages = STAGES_BY_VERSION.get(values[0])
    if stages is None:
        raise ValueError(f"Unsupported TDMA profile version: {values[0]}")
    fields = FIELDS_V6 if values[0] >= 6 else FIELDS
    if values[4] != len(stages) or len(values) != len(fields) + 2 * len(stages):
        raise ValueError("TDMA profile stage count or length mismatch")
    result = dict(zip(fields, values[:len(fields)]))
    result["stages"] = {
        name: {"ticks": values[len(fields) + 2*i], "calls": values[len(fields) + 2*i + 1]}
        for i, name in enumerate(stages)
    }
    return result


RX_FIELDS = (
    "version", "clock_hz", "reset_generation", "phase_count", "invalid_count",
    "station_state_count", "drop_cause_count", "initial_observation_count",
    "initial_gap_count", "initial_gap_max_ticks", "initial_backlog_max_words",
    "clamp_skipped_words",
)
RX_STATES = ("idle", "requested", "building", "ready", "cancelled")
RX_DROP_CAUSES = ("epoch", "clamp", "stale_hint", "frame_copy", "discovery_copy")


def parse_rx_timing(raw: str) -> dict | None:
    """Decode STOP-read aggregates; ns ages and clk_sys gaps have distinct units."""
    if raw.strip().strip('"') == "UNAVAILABLE":
        return None
    values = list(map(int, next(csv.reader([raw]))))
    base = len(RX_FIELDS)
    if (len(values) != base + 2 * len(RX_STATES) + len(RX_DROP_CAUSES) or
            values[0] != 1 or values[5:7] != [len(RX_STATES), len(RX_DROP_CAUSES)]):
        raise ValueError("Unknown or truncated TDMA RX profile schema")
    wide = {9, 11, *(base + 2*i + 1 for i in range(len(RX_STATES)))}
    if any(v < 0 or v >= 1 << (64 if i in wide else 32) for i, v in enumerate(values)):
        raise ValueError("TDMA RX profile field width mismatch")
    result = dict(zip(RX_FIELDS, values[:base]))
    result['station'] = {name:dict(polls=values[base+2*i], age_max_ns=values[base+2*i+1])
        for i, name in enumerate(RX_STATES)}
    result['drops'] = dict(zip(RX_DROP_CAUSES, values[base+2*len(RX_STATES):]))
    return result
