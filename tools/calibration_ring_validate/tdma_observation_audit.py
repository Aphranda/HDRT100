"""Offline audit of every native TDMA sampling slot, including startup.

This is a diagnostic companion to TRN03, not a replacement acceptance gate.
The recorder does not contain lossless edge events or per-sample origin grant
epochs. A frozen handoff can explain persona 16 and its software TX plateau;
it cannot establish physical cadence, grant lifetime, or timestamp validity.
"""
from __future__ import annotations

import csv
import hashlib

try:
    from .tdma_board_record import decode_record
except ImportError:
    from tdma_board_record import decode_record

U32_MASK = 0xFFFFFFFF
MAX_FORWARD = 0x7FFFFFFF
# Wire diagnostics: tdma_pio_spi_phys.h / tdma_adapter_comm_fsm.h.
ORIGIN, FOLLOWER, AUTONOMOUS, INSTALLED = 11, 13, 16, 5


def handoff_context(raw, *, trial_epoch, config_seq, clock_hz):
    """Bind a STOP-frozen HANDoff response to independently captured controls."""
    fields = next(csv.reader([raw]))
    if len(fields) != 43 or fields[0] != "ORIGINHANDOFF":
        raise ValueError("handoff schema mismatch")
    values = list(map(int, fields[1:]))
    schema, epoch, config, hz, result, invalid, elapsed, begin, count = values[:9]
    if (schema != 1 or count != 11 or epoch != trial_epoch or epoch <= 0 or
            config != config_seq or config <= 0 or hz != clock_hz or hz <= 0 or
            result != 2 or invalid != 0 or elapsed <= 0 or begin <= 0):
        raise ValueError("handoff identity/result mismatch")
    stages = [values[i:i + 3] for i in range(9, len(values), 3)]
    if ([i for i, (_, _, calls) in enumerate(stages) if calls] != list(range(1, 9)) or
            any(min(stage) < 0 for stage in stages)):
        raise ValueError("handoff stages incomplete")
    for i in range(1, 9):
        first, work, _ = stages[i]
        end = stages[i + 1][0] if i < 8 else elapsed
        if not 0 <= first <= end <= elapsed or work > end - first:
            raise ValueError("handoff stage clock mismatch")
    return dict(trial_epoch=epoch, config_seq=config, clock_hz=hz,
                elapsed_ticks=elapsed, begin_ticks=begin,
                raw_sha256=hashlib.sha256(raw.encode("utf-8")).hexdigest())


def audit_record(data: bytes, *, expected_build, expected_board, expected_epoch,
                 config_seq, node_index, node_count, interval_us, sample_count,
                 handoff_raw=None, trial_epoch=None, clock_hz=None):
    """Audit bytes against a capture plan; never select a stable subwindow.

    Expectations must come from the run's controls/plan, not the decoded file.
    Cross-group identity comparisons only apply when sampled sequences match:
    the production recorder obtains the groups at different instants.
    """
    if (not 0 <= node_index < node_count or config_seq <= 0 or
            interval_us <= 0 or sample_count < 2):
        raise ValueError("invalid capture expectation")
    context = None
    if handoff_raw is not None:
        if node_index != 0 or trial_epoch is None or clock_hz is None:
            raise ValueError("handoff requires origin control identity")
        context = handoff_context(handoff_raw, trial_epoch=trial_epoch,
                                  config_seq=config_seq, clock_hz=clock_hz)
    decoded = decode_record(data, expected_build=expected_build,
                            expected_board=expected_board, expected_epoch=expected_epoch)
    issues = [dict(slot=None, reason=x) for x in decoded["errors"]]
    time_issues, comparisons, intervals = [], [], []

    def check(ok, reason, slot):
        if not ok:
            issues.append(dict(slot=slot, reason=reason))

    check(decoded["interval_us"] == interval_us, "capture_interval_mismatch", None)
    check(decoded["terminal"]["requested"] == sample_count and
          decoded["terminal"]["written"] == sample_count,
          "capture_window_incomplete", None)
    previous = decoded["baseline"]
    base_runtime = previous["snapshot"]["runtime"]
    base_process = previous["snapshot"]["flight"]["process"]
    check(base_runtime["ring_config_seq"] == config_seq and
          base_runtime["ring_applied_config_seq"] == config_seq,
          "baseline_config_mismatch", "baseline")
    autonomous_slots = []
    seen_autonomous = False
    for sample in decoded["samples"]:
        slot, snap, old = sample["slot"], sample["snapshot"], previous["snapshot"]
        runtime, phys = snap["runtime"], snap["physical"]
        process, fifo = snap["flight"]["process"], snap["flight"]["fifo"]
        persona = phys["program_persona"]
        autonomous = node_index == 0 and persona == AUTONOMOUS
        expected_persona = ORIGIN if node_index == 0 else FOLLOWER
        check(persona == expected_persona or (autonomous and context is not None),
              "persona_without_matching_context", slot)
        check(not seen_autonomous or autonomous, "autonomous_mode_retired_in_window", slot)
        if autonomous:
            autonomous_slots.append(slot)
            seen_autonomous = True
            check(process["comm_fsm_state"] == 5, "autonomous_fsm_mismatch", slot)
            if context:
                check(snap["schedule"]["sys_clock_hz"] == context["clock_hz"],
                      "handoff_clock_mismatch", slot)
        else:
            check(process["comm_fsm_state"] in ((1, 2, 3) if node_index == 0 else (1,)),
                  "ordinary_fsm_mismatch", slot)
        check(process["comm_fsm_last_error"] == 0, "comm_fsm_error", slot)
        check(all(process[key] == base_process[key] for key in
                  ("map_generation", "map_crc32", "payload_size")),
              "map_changed_in_frozen_config", slot)
        for key, expected in (("ring_enabled", 1), ("ring_adapter_started", 1),
                              ("ring_up_running", 1), ("ring_down_running", 1),
                              ("ring_node_count", node_count), ("ring_local_node", node_index),
                              ("ring_reference_node", 0), ("ring_config_seq", config_seq),
                              ("ring_applied_config_seq", config_seq), ("ring_adapter_last_error", 0)):
            check(runtime[key] == expected, key + "_mismatch", slot)
        for key, expected in (("armed", 1), ("program_lifecycle_state", INSTALLED),
                              ("program_target_persona", persona), ("program_lifecycle_error", 0),
                              ("last_error", 0)):
            check(phys[key] == expected, "physical_" + key + "_mismatch", slot)
        # TIMESTAMP_MISSING belongs to time-input readiness, not DMA transport.
        check(runtime["ring_last_error"] in (0, 5), "ring_transport_error", slot)
        if runtime["ring_last_error"] == 5 or runtime["ring_timestamp_resolution_ns"] == 0:
            time_issues.append(dict(slot=slot, reason="timestamp_input_unqualified"))
        for key, expected in (("configured", 1), ("active", 1), ("local_node", node_index),
                              ("receive_configured", 1), ("receive_state", 1),
                              ("receive_consecutive_failure_count", 0),
                              ("receive_last_reason", 0), ("receive_last_transport_result", 0)):
            check(process[key] == expected, "process_" + key + "_mismatch", slot)
        for accepted, expected in (("segment_mask", "receive_expected_segment_mask"),
                                   ("wkc", "receive_expected_wkc"),
                                   ("map_generation", "map_generation"),
                                   ("payload_size", "payload_size")):
            check(process["receive_accepted_" + accepted] == process[expected] and process[expected] > 0,
                  "receive_" + accepted + "_mismatch", slot)
        tx, rx = runtime["ring_up_tx_sequence"], runtime["ring_down_rx_sequence"]
        gap = (tx - rx) & U32_MASK
        check(gap == 0 if autonomous or node_index else gap <= 1,
              "feedback_sequence_mismatch", slot)
        if tx == rx:
            check(runtime["ring_up_tx_frame_crc32"] == runtime["ring_down_rx_frame_crc32"],
                  "feedback_identity_mismatch", slot)
        comparable = process["receive_accepted_sequence"] == rx
        comparisons.append(dict(slot=slot, comparable=comparable))
        if comparable:
            check(process["receive_accepted_identity_crc32"] == runtime["ring_down_rx_frame_crc32"],
                  "accepted_identity_mismatch", slot)

        delta = {}

        def counters(group, before, after, fields, policy):
            for field in fields:
                value = (after[field] - before[field]) & U32_MASK
                delta[group + "." + field] = value
                check(value <= MAX_FORWARD, group + "." + field + ":reset_or_backward", slot)
                if policy == "advance":
                    check(value > 0, group + "." + field + ":stalled", slot)
                elif policy == "zero":
                    check(value == 0, group + "." + field + ":grew", slot)

        counters("runtime", old["runtime"], runtime,
                 ("ring_adapter_service_count", "ring_up_tx_sequence", "ring_down_rx_sequence",
                  "ring_adapter_rx_count"), "advance")
        counters("runtime", old["runtime"], runtime, ("ring_adapter_tx_count",),
                 "monotonic" if autonomous else "advance")
        counters("runtime", old["runtime"], runtime,
                 ("ring_adapter_rx_bad_count", "ring_adapter_rx_transport_bad_count",
                  "ring_adapter_rx_schedule_bad_count", "ring_adapter_rx_profile_bad_count"), "zero")
        counters("physical", old["physical"], phys, ("rx_count",), "advance")
        counters("physical", old["physical"], phys,
                 ("rx_bad_count", "rx_stall_count", "tx_timeout_count", "rx_ring_overrun_count",
                  "program_switch_fail_count", "origin_clock_timeout_count", "origin_data_timeout_count",
                  "overlay_prepare_fail_count"), "zero")
        counters("process", old["flight"]["process"], process,
                 ("receive_accepted_count", "receive_image_generation", "receive_accepted_sequence"), "advance")
        counters("process", old["flight"]["process"], process,
                 ("receive_rejected_count", "receive_missing_count", "rx_bitmap_incomplete_count",
                  "map_reject_count", "length_reject_count"), "zero")
        counters("fifo", old["flight"]["fifo"], fifo,
                 ("rx_publish_count", "rx_acquire_count", "rx_release_count", "tx_acquire_count"), "advance")
        counters("fifo", old["flight"]["fifo"], fifo,
                 ("tx_publish_reject_count", "rx_mirror_drop_count", "rx_publish_drop_count"), "zero")
        check(sample["started_us"] >= previous["completed_us"], "snapshot_overlap_or_replay", slot)
        intervals.append(dict(slot=slot, previous_slot=previous["slot"], persona=persona,
                              started_us=sample["started_us"], completed_us=sample["completed_us"],
                              deltas=delta))
        previous = sample
    if context:
        check(len(autonomous_slots) >= 2, "autonomous_observation_window_missing", None)
    return dict(schema="HAOFV_TDMA_OBSERVATION_AUDIT_V1",
                record_sha256=hashlib.sha256(data).hexdigest(), build=decoded["build"],
                board=decoded["board"], epoch=decoded["epoch"], config_seq=config_seq,
                interval_us=decoded["interval_us"], terminal=decoded["terminal"],
                collection_passed=decoded["collection_passed"],
                observation_checks_passed=not issues, issues=issues, intervals=intervals,
                identity_comparisons=comparisons, autonomous_slots=autonomous_slots,
                handoff_context=context, time_input_issues=time_issues,
                physical_cadence_proven=False, per_sample_grant_proven=False,
                timestamp_input_proven=False, wcet_proven=False, formal_lock_accepted=False,
                scope="All native slots plus baseline counter deltas; Core0 groups are not simultaneous. "
                      "Advancing accepted sequences are observations, not lossless physical edge counts. "
                      "Frozen handoff context does not prove per-sample grant lifetime or raw identity.")
