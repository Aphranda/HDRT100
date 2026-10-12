"""Regression tests for the four-board P3 DPLL short observation."""

import json
from pathlib import Path
import zlib

import pytest

from tools.hardware_acceptance import p3_dpll_short as short
from tools.hardware_acceptance import p3_hardware_acceptance as p3
from tools.workflows.vdc_joint_capture import internal_seal


UIDS = ["no1", "no2", "no3", "no4"]
TICK_HZ = 250_000_000
TRACE_BASE_TICK = 1_000_000
HOST_OFFSET_NS = 1_000_000_000_000
HOST_CLOCK = {
    "schema": "HAOFV_HOST_EVIDENCE_CLOCK_V1",
    "name": "perf_counter",
    "implementation": "fixture-qpc",
    "monotonic": True,
    "adjustable": False,
    "resolution_ns": 100,
}


def anchor(uid, label, raw_before, host_start_ns):
    bridge = {name: 0 for name in internal_seal.BRIDGE_FIELDS}
    bridge.update(
        schema=1, platform_supported=1, configuration_supported=1,
        bridge_valid=1, clock_ready=1, expected_hz=TICK_HZ,
        cached_hz=TICK_HZ, sdk_sys_hz=TICK_HZ,
        timer0_sample_valid=1, timer1_sample_valid=1,
        timer0_sample_us=raw_before // 250,
        timer1_sample_ticks=raw_before + 1,
        raw_before=raw_before, local_ns=(raw_before // 250) * 1000,
        raw_after=raw_before + 2, tick_hz=TICK_HZ)
    return {
        "schema": "VDC_HOST_RAW_ANCHOR_V1", "label": label, "uid": uid,
        "host_start_ns": host_start_ns, "host_end_ns": host_start_ns + 100_000,
        "host_clock": HOST_CLOCK, "bridge": bridge,
    }


@pytest.fixture(autouse=True)
def decode_json_internal_trace(monkeypatch):
    monkeypatch.setattr(
        short, "_decode_internal_trace",
        lambda data, capture_id: json.loads(data.decode("utf-8")))


def phase_raw(slot, low, high):
    words = [0] * short.PHASE_WORD_COUNT
    words[0] = 1
    words[6] = 71
    words[7] = 72
    words[12] = 1
    words[18] = 0
    words[19] = slot
    for offset, value in ((30, low), (32, high)):
        encoded = value & ((1 << 64) - 1)
        words[offset] = encoded & 0xFFFFFFFF
        words[offset + 1] = encoded >> 32
    return ",".join(map(str, words))


def write_trace(folder, decoded):
    payload = json.dumps(decoded, sort_keys=True).encode("utf-8")
    (folder / "summary.ram.bin").write_bytes(payload)
    (folder / "decoded.json").write_text(
        json.dumps(decoded, sort_keys=True), encoding="utf-8")
    pages = []
    crc = zlib.crc32(payload)
    offset, size = 0, 4
    while offset < len(payload):
        chunk = payload[offset:offset + size]
        pages.append({
            "command": (
                f"SYSTem:VDC:PRIORity:TRACe:READ? "
                f"{decoded['status']['capture_id']},{offset},{size}"),
            "response": (
                f'{offset},{len(chunk)},{len(payload)},{crc},'
                f'"{chunk.hex()}"'),
        })
        offset += size
        size = min(128, len(payload) - offset)
    (folder / "read-pages.json").write_text(
        json.dumps(pages), encoding="utf-8")


def reports(tmp_path, *, scope, maximum=80.0, missing=False,
            trace_maximum=None, startup_maximum=10_000_000):
    joint = tmp_path / "report.json"
    capture = tmp_path / "capture"
    capture.mkdir(parents=True)
    input_report = capture / "input-probe.json"
    joint.write_text(json.dumps({
        "passed": True, "complete": True, "main_completed": True,
        "errors": [], "cleanup_errors": [], "restore_needed": [],
        "source_tree_sha256": "a" * 64, "receipt_build": "123",
        "workflow_inputs": {"board_uids": UIDS},
    }), encoding="utf-8")
    boards = {UIDS[0]: {"final_phase_raw": ",".join(["0"] * 52)}}
    trace_maximum = int(maximum if trace_maximum is None else trace_maximum)
    for slot, uid in enumerate(UIDS[1:], 1):
        capture_id = 100 + slot
        boards[uid] = {
            "final_phase_raw": phase_raw(slot, -int(maximum), int(maximum)),
            "trace_capture_id": capture_id,
        }
        folder = capture / f"NO{slot + 1}"
        folder.mkdir()
        records = []
        for index in range(6):
            bound = startup_maximum if index == 0 else trace_maximum
            records.append({
                "bin_index": index,
                "observed_start_raw": TRACE_BASE_TICK + index * 2_500_000_000,
                "observed_end_raw": TRACE_BASE_TICK + (index + 1) * 2_500_000_000,
                "success_count": 10,
                "first_success_offset_ticks": 1,
                "last_success_offset_ticks": 2_499_999_999,
                "max_success_gap_ticks": 1000,
                "max_service_gap_ticks": 1000,
                "residual_min_ns": -bound,
                "residual_max_ns": bound,
                "min_ppb": -1,
                "max_ppb": 1,
                "frequency_applied_count": 1,
                "phase_applied_count": 1,
                "residual_extrema_valid": True,
                "coverage_incomplete": False,
                "flag_names": (["PARTIAL", "TERMINAL", "STEADY_SUCCESS_GAP"]
                               if index == 5 else ["STEADY_SUCCESS_GAP"]),
            })
        decoded = {
            "schema": "VDC_SUMMARY_TRACE_DECODE_V9",
            "status": {"schema": 9, "capture_id": capture_id, "mode": 4,
                       "state": 3, "reason": 8, "session": 71,
                       "generation": 72, "local_slot": slot,
                       "reference_slot": 0, "node_count": 4,
                       "sample_interval_ms": 10000, "tick_hz": TICK_HZ},
            "records": records,
        }
        write_trace(folder, decoded)
    actions = []
    if scope:
        pre_raw = TRACE_BASE_TICK - 1_000_000
        post_raw = TRACE_BASE_TICK + 15_000_000_000 + 1_000_000
        for uid in UIDS:
            pre = anchor(uid, "pre", pre_raw, HOST_OFFSET_NS + pre_raw * 4)
            post = anchor(uid, "post", post_raw, HOST_OFFSET_NS + post_raw * 4)
            boards[uid]["host_raw_anchors"] = {"pre": pre, "post": post}
            pre_raw_text = ",".join(
                str(pre["bridge"][name]) for name in internal_seal.BRIDGE_FIELDS)
            post_raw_text = ",".join(
                str(post["bridge"][name]) for name in internal_seal.BRIDGE_FIELDS)
            actions.extend((
                {"uid": uid, "command": "SYSTem:VDC:FEEDback:BRIDge?",
                 "start_ns": pre["host_start_ns"] + 10,
                 "end_ns": pre["host_end_ns"] - 10, "raw": pre_raw_text},
                {"uid": uid, "command": "SYSTem:TDMA:RING:START",
                 "start_ns": pre["host_end_ns"] + 1_000_000,
                 "end_ns": pre["host_end_ns"] + 2_000_000, "raw": "OK"},
                {"uid": uid, "command": "SYSTem:TDMA:RING:STOP",
                 "start_ns": post["host_start_ns"] - 2_000_000,
                 "end_ns": post["host_start_ns"] - 1_000_000, "raw": "OK"},
                {"uid": uid, "command": "SYSTem:VDC:FEEDback:BRIDge?",
                 "start_ns": post["host_start_ns"] + 10,
                 "end_ns": post["host_end_ns"] - 10, "raw": post_raw_text},
            ))
    samples = []
    if scope:
        for due in range(5, 61, 5):
            if missing and due == 60:
                continue
            folder = capture / f"sample-{due:04d}"
            folder.mkdir()
            sample_raw = TRACE_BASE_TICK + int((due + 0.375) * TICK_HZ)
            sample_host = HOST_OFFSET_NS + sample_raw * 4
            for channel in range(1, 5):
                (folder / f"ch{channel}.block").write_bytes(
                    f"{due}:{channel}".encode("ascii"))
            (folder / "scope.json").write_text(json.dumps({
                "capture_complete": True,
                "host_clock": HOST_CLOCK,
                "fresh_wait_ns": sample_host - 100,
                "trigger_admitted_ns": sample_host,
                "trigger_complete_observed_ns": sample_host + 100_000,
                "commands": [
                    {"command": ":TRIG:STAT?", "raw": "WAIT",
                     "start_ns": sample_host - 300, "end_ns": sample_host - 200},
                    {"command": ":TRIG:EDGE:LEV 1.5",
                     "start_ns": sample_host, "end_ns": sample_host},
                    {"command": ":TRIG:STAT?", "raw": "STOP",
                     "start_ns": sample_host + 90_000, "end_ns": sample_host + 100_000}],
                "channels": [{"channel": channel,
                              "preamble": "0,2,650000,1,2e-9,-.0002,0,.1,0,128",
                              "blocks": [{"path": f"ch{channel}.block",
                                          "start": 1, "stop": 650000}]}
                             for channel in range(1, 5)]}), encoding="utf-8")
            samples.append({"due_s": due, "capture_complete": True,
                            "acquisition_host_ns": [sample_host, sample_host + 100_000],
                            "phase": {"relative_ns": [[maximum], [0.0], [0.0]]}})
    payload = {
        "passed": True, "joint_internal_health_passed": True,
        "quiet_elapsed_s": 62.0, "errors": [], "boards": boards,
        "host_clock": HOST_CLOCK,
        "actions": actions, "queries_during_acquisition": 0,
        "plan": {"runtime_anchor_queries": 0,
                 "core1_probe_work_added": False,
                 "realtime_resource_seat_added": False},
        "joint_verdict": {"internal_health_passed": True,
                          "external_status": "PASS" if scope else "SKIPPED"},
    }
    if scope:
        payload["scope_samples"] = samples
    input_report.write_text(json.dumps(payload), encoding="utf-8")
    return joint, input_report


def mutate_trace(input_report, board_no, mutation):
    folder = input_report.parent / f"NO{board_no}"
    decoded = json.loads((folder / "decoded.json").read_text(encoding="utf-8"))
    mutation(decoded)
    write_trace(folder, decoded)


@pytest.mark.parametrize("value,severity", [
    (100.0, "INFO"), (100.001, "WARN"), (1000.0, "WARN"), (1000.001, "ERROR")])
def test_phase_severity_boundaries(value, severity):
    assert short.phase_severity(value) == severity


def test_internal_only_uses_steady_native_envelope_and_skips_external(tmp_path):
    joint, inner = reports(tmp_path, scope=False, maximum=101)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is True
    assert summary["phase_severity"] == "WARN"
    assert summary["phase_max_abs_ns"] == 101
    assert summary["external_status"] == "SKIPPED"
    assert summary["phase_domain"] == "internal_steady_summary_residual_envelope"
    assert summary["internal_summary_interval_ms"] == 10000
    assert summary["internal_settling_seconds_excluded"] == 10
    assert len(summary["internal_phase_traces"]) == 3
    assert all(row["settling_bins_excluded"] == 1 and row["steady_bins"] == 5 and
               row["endpoint_within_last_bin"]
               for row in summary["internal_phase_traces"])
    short.validate_summary(tmp_path, summary)


def test_internal_startup_transient_is_recorded_but_not_used_for_severity(tmp_path):
    joint, inner = reports(
        tmp_path, scope=False, maximum=80, startup_maximum=5_000_000)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["phase_max_abs_ns"] == 80
    assert summary["phase_severity"] == "INFO"
    assert all(row["startup_residual_min_ns"] == -5_000_000
               for row in summary["internal_phase_traces"])


def test_internal_endpoint_must_match_last_steady_bin(tmp_path):
    joint, inner = reports(
        tmp_path, scope=False, maximum=101, trace_maximum=80)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("outside the last steady trace bin" in error
               for error in summary["errors"])


def test_internal_decoded_json_must_match_native_replay(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    path = inner.parent / "NO2" / "decoded.json"
    decoded = json.loads(path.read_text(encoding="utf-8"))
    decoded["records"][1]["residual_max_ns"] += 1
    path.write_text(json.dumps(decoded), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("differs from native bytes" in error for error in summary["errors"])


def test_internal_read_pages_must_reconstruct_native_bytes(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    path = inner.parent / "NO2" / "read-pages.json"
    pages = json.loads(path.read_text(encoding="utf-8"))
    response = pages[1]["response"]
    payload_hex = response.split('"')[1]
    changed = ("0" if payload_hex[0] != "0" else "1") + payload_hex[1:]
    pages[1]["response"] = response.replace(
        f'"{payload_hex}"', f'"{changed}"')
    path.write_text(json.dumps(pages), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("read page" in error for error in summary["errors"])


def test_internal_read_pages_must_use_canonical_first_page(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    path = inner.parent / "NO2" / "read-pages.json"
    pages = json.loads(path.read_text(encoding="utf-8"))
    pages[0]["command"] = pages[0]["command"].rsplit(",", 1)[0] + ",128"
    path.write_text(json.dumps(pages), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("command or offset" in error for error in summary["errors"])


@pytest.mark.parametrize("mutation", [
    lambda decoded: decoded["status"].update(sample_interval_ms=5000),
    lambda decoded: decoded["records"][-1].update(flag_names=["STEADY_SUCCESS_GAP"]),
    lambda decoded: decoded["records"][2].update(max_success_gap_ticks=300_000_000),
])
def test_internal_trace_requires_fixed_interval_terminal_and_success_gap(
        tmp_path, mutation):
    joint, inner = reports(tmp_path, scope=False)
    mutate_trace(inner, 2, mutation)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("coverage or success-gap" in error or "bin count" in error
               for error in summary["errors"])


def boundary_tail(decoded, *, duration=250_000, flags=None, prior_terminal=False):
    """STOP just after the last boundary: bin 5 is full, a short terminal bin 6 follows."""
    records = decoded["records"]
    last = records[-1]
    last["flag_names"] = (["PARTIAL", "TERMINAL", "STEADY_SUCCESS_GAP"] if prior_terminal
                          else ["STEADY_SUCCESS_GAP"])
    tail = dict(last)
    tail.update(bin_index=last["bin_index"] + 1,
                observed_start_raw=last["observed_end_raw"],
                observed_end_raw=last["observed_end_raw"] + duration,
                success_count=1, first_success_offset_ticks=1,
                last_success_offset_ticks=1, max_success_gap_ticks=1000,
                residual_min_ns=last["residual_min_ns"] // 2,
                residual_max_ns=last["residual_max_ns"] // 2,
                flag_names=(["PARTIAL", "TERMINAL", "STEADY_SUCCESS_GAP"]
                            if flags is None else flags))
    records.append(tail)
    decoded["status"]["record_count"] = len(records)


def test_internal_trace_accepts_a_stop_boundary_tail_bin(tmp_path):
    # p3-132149: STOP ~1 ms after the 60 s boundary committed a 7th bin, and the
    # endpoint envelope spans the last full bin, not the one-decision tail.
    joint, inner = reports(tmp_path, scope=False)
    mutate_trace(inner, 2, boundary_tail)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is True, summary["errors"]
    row = next(r for r in summary["internal_phase_traces"] if r["board_id"] == UIDS[1])
    assert row["total_bins"] == 7 and row["endpoint_within_last_bin"]
    short.validate_summary(tmp_path, summary)


@pytest.mark.parametrize("variant", [
    dict(duration=2_500_000_000),                        # tail is a whole interval
    dict(flags=["STEADY_SUCCESS_GAP"]),                  # tail is not terminal
    dict(flags=["TERMINAL", "STEADY_SUCCESS_GAP"]),      # terminal but not partial
    dict(prior_terminal=True),                           # two terminal bins
])
def test_internal_trace_refuses_other_extra_bins(tmp_path, variant):
    joint, inner = reports(tmp_path, scope=False)
    mutate_trace(inner, 2, lambda decoded: boundary_tail(decoded, **variant))
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False


def test_internal_trace_refuses_two_extra_bins(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    def twice(decoded):
        boundary_tail(decoded)
        decoded["records"][-1]["flag_names"] = ["STEADY_SUCCESS_GAP"]
        boundary_tail(decoded)
    mutate_trace(inner, 2, twice)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("bin count" in error for error in summary["errors"])


def test_internal_trace_binds_endpoint_session_and_board_slot(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    mutate_trace(inner, 2, lambda decoded: decoded["status"].update(local_slot=2))
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("identity" in error for error in summary["errors"])


def test_internal_traces_share_one_session_generation(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    mutate_trace(inner, 2, lambda decoded: decoded["status"].update(
        session=73, generation=74))
    payload = json.loads(inner.read_text(encoding="utf-8"))
    words = [int(value) for value in payload["boards"][UIDS[1]]["final_phase_raw"].split(",")]
    words[6], words[7] = 73, 74
    payload["boards"][UIDS[1]]["final_phase_raw"] = ",".join(map(str, words))
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    assert summary["passed"] is False
    assert any("do not share one session" in error for error in summary["errors"])


def test_summary_identity_must_match_acceptance_receipt(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    with pytest.raises(ValueError, match="source identity differs"):
        short.validate_summary(
            tmp_path, summary,
            expected_source_tree_sha256="b" * 64,
            expected_build_id="123",
            expected_board_uids=UIDS)


@pytest.mark.parametrize("magnitude,severity,passed", [
    (100, "INFO", True), (101, "WARN", True),
    (1000, "WARN", True), (1001, "ERROR", False),
])
@pytest.mark.parametrize("sign", [-1, 1])
def test_external_phase_thresholds_are_symmetric(tmp_path, magnitude, severity, passed, sign):
    joint, inner = reports(tmp_path, scope=True)
    payload = json.loads(inner.read_text(encoding="utf-8"))
    # All-negative windows must work too, with no zero or positive sentinel.
    for sample in payload["scope_samples"]:
        sample["phase"]["relative_ns"] = [[sign * magnitude], [-2], [-1]]
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(tmp_path, joint, inner, scope_enabled=True, seconds=60)
    assert summary["phase_max_abs_ns"] == magnitude
    assert summary["phase_severity"] == severity
    assert summary["passed"] is passed


def test_scope_path_uses_relative_ns_and_binds_every_raw_block(tmp_path, monkeypatch):
    joint, inner = reports(tmp_path, scope=True, maximum=999)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    assert summary["passed"] is True
    assert summary["phase_severity"] == "WARN"
    assert summary["phase_max_abs_ns"] == 999
    assert summary["phase_domain"] == "external_physical_sampled_windows"
    assert len(summary["scope_raw_blocks"]) == 48
    assert len(summary["scope_records"]) == 12
    correlation = summary["time_correlation"]
    assert correlation["evidence_valid"] is True
    assert correlation["probe_mode"] == "PASSIVE_SCOPE_PLUS_STOP_ONLY_DOUBLE_BRIDGE"
    assert correlation["runtime_board_queries"] == 0
    assert correlation["core1_probe_work_added"] is False
    assert correlation["realtime_resource_seat_added"] is False
    assert correlation["exact_event_correlation_qualified"] is False
    assert correlation["numeric_error_qualified"] is False
    assert all(value > 0 for value in
               correlation["unique_steady_associations_by_board"].values())
    assert correlation["outside_capture_associations_by_board"] == {
        uid: 1 for uid in UIDS[1:]}
    assert all(row["association"] == "OUTSIDE_CAPTURE" for row in
               correlation["associations"] if row["due_s"] == 60)
    monkeypatch.setattr(short, "_analyze_scope_memory", lambda report, blocks: {
        "relative_ns": [[999], [0.0], [0.0]]})
    short.validate_summary(tmp_path, summary)


def mutate_anchor_rate(payload):
    post = payload["boards"][UIDS[1]]["host_raw_anchors"]["post"]
    post.update(host_start_ns=HOST_OFFSET_NS + 90_000_000_000,
                host_end_ns=HOST_OFFSET_NS + 90_000_100_000)
    actions = [row for row in payload["actions"] if row["uid"] == UIDS[1] and
               row["command"].upper() == "SYSTEM:VDC:FEEDBACK:BRIDGE?"]
    actions[1].update(start_ns=post["host_start_ns"] + 10,
                      end_ns=post["host_end_ns"] - 10)


def mutate_bridge_clock(payload):
    post = payload["boards"][UIDS[1]]["host_raw_anchors"]["post"]
    post["bridge"]["cached_hz"] = 249_000_000
    actions = [row for row in payload["actions"] if row["uid"] == UIDS[1] and
               row["command"].upper() == "SYSTEM:VDC:FEEDBACK:BRIDGE?"]
    actions[1]["raw"] = ",".join(
        str(post["bridge"][name]) for name in internal_seal.BRIDGE_FIELDS)


def mutate_anchor_width(payload):
    uid = UIDS[1]
    anchors = payload["boards"][uid]["host_raw_anchors"]
    actions = [row for row in payload["actions"] if row["uid"] == uid]
    bridge_actions = sorted(
        (row for row in actions if
         row["command"].upper() == "SYSTEM:VDC:FEEDBACK:BRIDGE?"),
        key=lambda row: row["start_ns"])
    for label, action in zip(("pre", "post"), bridge_actions):
        item = anchors[label]
        center = (item["host_start_ns"] + item["host_end_ns"]) // 2
        item.update(host_start_ns=center - 200_000_000,
                    host_end_ns=center + 200_000_000)
        action.update(start_ns=item["host_start_ns"] + 10,
                      end_ns=item["host_end_ns"] - 10)
    for row in payload["actions"]:
        command = row["command"].upper()
        if command == "SYSTEM:TDMA:RING:START":
            row["start_ns"] += 500_000_000
            row["end_ns"] += 500_000_000
        elif command == "SYSTEM:TDMA:RING:STOP":
            row["start_ns"] -= 500_000_000
            row["end_ns"] -= 500_000_000


def mutate_cross_board_pre_anchor(payload):
    uid = UIDS[3]
    anchor_row = payload["boards"][uid]["host_raw_anchors"]["pre"]
    anchor_row["host_start_ns"] += 5_000_000
    anchor_row["host_end_ns"] += 5_000_000
    actions = [row for row in payload["actions"] if row["uid"] == uid]
    pre = min((row for row in actions if
               row["command"].upper() == "SYSTEM:VDC:FEEDBACK:BRIDGE?"),
              key=lambda row: row["start_ns"])
    pre["start_ns"] += 5_000_000
    pre["end_ns"] += 5_000_000
    start = next(row for row in actions if
                 row["command"].upper() == "SYSTEM:TDMA:RING:START")
    start["start_ns"] += 10_000_000
    start["end_ns"] += 10_000_000


def insert_runtime_query(payload):
    starts = [row["start_ns"] for row in payload["actions"] if
              row["command"].upper() == "SYSTEM:TDMA:RING:START"]
    stops = [row["end_ns"] for row in payload["actions"] if
             row["command"].upper() == "SYSTEM:TDMA:RING:STOP"]
    middle = (min(starts) + max(stops)) // 2
    payload["actions"].append({
        "uid": UIDS[1], "command": "SYSTem:VDC:UNRELATED?",
        "start_ns": middle, "end_ns": middle + 1, "raw": "0"})


def insert_malformed_query(payload, start_ns, end_ns):
    payload["actions"].append({
        "uid": UIDS[1], "command": "SYSTem:VDC:UNRELATED?",
        "start_ns": start_ns, "end_ns": end_ns, "raw": "0"})


def insert_query_at_stop_boundary(payload):
    stop = max(row["end_ns"] for row in payload["actions"] if
               row["command"].upper() == "SYSTEM:TDMA:RING:STOP")
    payload["actions"].append({
        "uid": UIDS[1], "command": "SYSTem:TDMA:RING:STATus?",
        "start_ns": stop, "end_ns": stop + 1, "raw": "0"})


def move_most_scope_windows_after_capture(payload):
    for sample in payload["scope_samples"]:
        if sample["due_s"] != 15:
            sample["acquisition_host_ns"] = [
                HOST_OFFSET_NS + 100_000_000_000 + sample["due_s"],
                HOST_OFFSET_NS + 100_000_000_001 + sample["due_s"]]


def move_first_scope_before_capture(payload):
    payload["scope_samples"][0]["acquisition_host_ns"] = [
        HOST_OFFSET_NS - 1000, HOST_OFFSET_NS - 999]


def remove_host_clock(payload):
    payload.pop("host_clock")


def mix_anchor_host_clock(payload):
    payload["boards"][UIDS[1]]["host_raw_anchors"]["pre"]["host_clock"] = {
        **HOST_CLOCK, "implementation": "other-clock"}


def degrade_host_clock_resolution(payload):
    payload["host_clock"]["resolution_ns"] = 15_625_000


@pytest.mark.parametrize("mutation,error", [
    (lambda payload: payload["boards"][UIDS[1]].pop("host_raw_anchors"),
     "action set is incomplete"),
    (mutate_bridge_clock, "bridge action raw response is invalid"),
    (mutate_anchor_rate, "corner rate differs"),
    (mutate_anchor_width, "corner rate differs"),
    (lambda payload: payload["boards"][UIDS[1]]["host_raw_anchors"]["post"]
     ["bridge"].update(raw_after=payload["boards"][UIDS[1]]["host_raw_anchors"]
                       ["post"]["bridge"]["raw_after"] + 1),
     "structured bridge anchor differs"),
    (mutate_cross_board_pre_anchor, "all-board RUN interval"),
    (insert_runtime_query, "raw actions contain a query"),
    (lambda payload: insert_malformed_query(payload, "inside", "inside"),
     "timed raw action identity or interval is invalid"),
    (lambda payload: insert_malformed_query(payload, True, False),
     "timed raw action identity or interval is invalid"),
    (lambda payload: insert_malformed_query(payload, 2, 1),
     "timed raw action identity or interval is invalid"),
    (lambda payload: payload["actions"][1].update(raw="NOT_OK"),
     "START/STOP action did not complete"),
    (move_most_scope_windows_after_capture,
     "scope host-time interval differs"),
    (move_first_scope_before_capture,
     "scope host-time interval differs"),
    (remove_host_clock, "clock identity"),
    (mix_anchor_host_clock, "anchor identity"),
    (degrade_host_clock_resolution, "clock identity"),
    (lambda payload: payload["scope_samples"][0].update(due_s=60),
     "due sequence differs"),
])
def test_scope_time_correlation_fails_closed(tmp_path, mutation, error):
    joint, inner = reports(tmp_path, scope=True)
    payload = json.loads(inner.read_text(encoding="utf-8"))
    mutation(payload)
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    assert summary["passed"] is True
    assert summary["errors"] == []
    assert summary["time_correlation"]["evidence_valid"] is False
    assert any(error in value for value in summary["time_correlation"]["reasons"])


@pytest.mark.parametrize("field,value", [
    ("queries_during_acquisition", 1),
    ("runtime_anchor_queries", 1),
    ("core1_probe_work_added", True),
    ("realtime_resource_seat_added", True),
])
def test_scope_time_correlation_requires_nonintrusive_probe_boundary(
        tmp_path, field, value):
    joint, inner = reports(tmp_path, scope=True)
    payload = json.loads(inner.read_text(encoding="utf-8"))
    if field == "queries_during_acquisition":
        payload[field] = value
    else:
        payload["plan"][field] = value
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    assert summary["passed"] is True
    assert summary["time_correlation"]["evidence_valid"] is False
    assert any("nonintrusive probe boundary" in error for error in
               summary["time_correlation"]["reasons"])


def test_scope_time_correlation_binds_native_trace_tick_rate(tmp_path):
    joint, inner = reports(tmp_path, scope=True)
    mutate_trace(inner, 2, lambda decoded: decoded["status"].update(tick_hz=150_000_000))
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    assert summary["passed"] is True
    assert summary["time_correlation"]["evidence_valid"] is False
    assert any("trace tick rate differs" in error for error in
               summary["time_correlation"]["reasons"])


def test_scope_interval_on_conservative_bin_boundary_is_ambiguous(tmp_path):
    joint, inner = reports(tmp_path, scope=True)
    first = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    boundary = first["time_correlation"]["models"][0]["bins"][0]["host_end_ns"][0]
    payload = json.loads(inner.read_text(encoding="utf-8"))
    payload["scope_samples"][0]["acquisition_host_ns"] = [boundary, boundary]
    inner.write_text(json.dumps(payload), encoding="utf-8")
    scope_path = inner.parent / "sample-0005/scope.json"
    record = json.loads(scope_path.read_text(encoding="utf-8"))
    record.update(fresh_wait_ns=boundary - 1, trigger_admitted_ns=boundary,
                  trigger_complete_observed_ns=boundary)
    record["commands"] = [
        {"command": ":TRIG:STAT?", "raw": "WAIT",
         "start_ns": boundary - 2, "end_ns": boundary - 1},
        {"command": ":TRIG:EDGE:LEV 1.5", "start_ns": boundary, "end_ns": boundary},
        {"command": ":TRIG:STAT?", "raw": "STOP",
         "start_ns": boundary, "end_ns": boundary}]
    scope_path.write_text(json.dumps(record), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    row = next(item for item in summary["time_correlation"]["associations"]
               if item["due_s"] == 5 and item["board_id"] == UIDS[1])
    assert row["candidate_bins"] == [0, 1]
    assert row["unique_bin"] is None
    assert row["association"] == "AFFINE_AMBIGUOUS"
    assert row["internal_residual_ns"] is None


def test_query_starting_at_global_stop_boundary_is_not_runtime_intrusion(tmp_path):
    joint, inner = reports(tmp_path, scope=True)
    payload = json.loads(inner.read_text(encoding="utf-8"))
    insert_query_at_stop_boundary(payload)
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    assert summary["time_correlation"]["evidence_valid"] is True


def test_scope_record_interval_includes_write_latency_and_rounds_pretrigger_outward(tmp_path):
    _, inner = reports(tmp_path, scope=True)
    payload = json.loads(inner.read_text(encoding="utf-8"))
    sample = payload["scope_samples"][0]
    record = json.loads((inner.parent / "sample-0005/scope.json").read_text(encoding="utf-8"))
    admitted, complete = sample["acquisition_host_ns"]
    record["commands"][1]["start_ns"] -= 50
    for channel in record["channels"]:
        channel["preamble"] = "0,2,650000,1,2e-9,-.0002000000001,0,.1,0,128"
    assert short._scope_record_interval(record, sample) == [admitted - 50 - 200001, complete]


@pytest.mark.parametrize("fault", [
    "missing_commands", "duplicate_admission", "write_error", "bool_timestamp",
    "reversed_write", "no_wait", "late_wait", "no_stop", "early_completion",
    "missing_channel", "nan_axis", "zero_denominator", "axis_mismatch", "invalid_block",
])
def test_scope_record_interval_rejects_unproven_window(tmp_path, fault):
    _, inner = reports(tmp_path, scope=True)
    sample = json.loads(inner.read_text(encoding="utf-8"))["scope_samples"][0]
    record = json.loads((inner.parent / "sample-0005/scope.json").read_text(encoding="utf-8"))
    commands = record["commands"]
    if fault == "missing_commands":
        record.pop("commands")
    elif fault == "duplicate_admission":
        commands.append(dict(commands[1]))
    elif fault == "write_error":
        commands[1]["error"] = "timeout"
    elif fault == "bool_timestamp":
        commands[1]["start_ns"] = True
    elif fault == "reversed_write":
        commands[1]["end_ns"] -= 1
    elif fault == "no_wait":
        commands[0]["raw"] = "STOP"
    elif fault == "late_wait":
        record["fresh_wait_ns"] = commands[1]["end_ns"] + 1
    elif fault == "no_stop":
        commands[2]["raw"] = "WAIT"
    elif fault == "early_completion":
        record["trigger_complete_observed_ns"] -= 1
    elif fault == "missing_channel":
        record["channels"].pop()
    elif fault in ("nan_axis", "zero_denominator", "axis_mismatch"):
        value = {"nan_axis": "nan", "zero_denominator": "1/0",
                 "axis_mismatch": "-.0003"}[fault]
        record["channels"][1]["preamble"] = f"0,2,650000,1,2e-9,{value},0,.1,0,128"
    elif fault == "invalid_block":
        record["channels"][0]["blocks"][0]["start"] = 0
    with pytest.raises(ValueError):
        short._scope_record_interval(record, sample)


def test_raw_pretrigger_prevents_false_unique_bin_association(tmp_path):
    joint, inner = reports(tmp_path, scope=True)
    baseline = short.summarize_joint(tmp_path, joint, inner, scope_enabled=True, seconds=60)
    bins = baseline["time_correlation"]["models"][0]["bins"]
    admitted = bins[0]["host_end_ns"][1] + 1000
    complete = admitted + 100_000
    assert admitted >= bins[1]["host_start_ns"][1]
    assert complete <= bins[1]["host_end_ns"][0]
    payload = json.loads(inner.read_text(encoding="utf-8"))
    payload["scope_samples"][0]["acquisition_host_ns"] = [admitted, complete]
    inner.write_text(json.dumps(payload), encoding="utf-8")
    path = inner.parent / "sample-0005/scope.json"
    record = json.loads(path.read_text(encoding="utf-8"))
    record.update(fresh_wait_ns=admitted - 100, trigger_admitted_ns=admitted,
                  trigger_complete_observed_ns=complete)
    for row, bounds in zip(record["commands"], [
            (admitted - 300, admitted - 200), (admitted - 50, admitted),
            (complete - 100, complete)]):
        row.update(start_ns=bounds[0], end_ns=bounds[1])
    path.write_text(json.dumps(record), encoding="utf-8")
    corrected = short.summarize_joint(tmp_path, joint, inner, scope_enabled=True, seconds=60)
    row = corrected["time_correlation"]["associations"][0]
    assert row["association"] == "AFFINE_AMBIGUOUS"
    assert row["candidate_bins"] == [0, 1]
    assert row["scope_host_ns"] == [admitted - 50 - 200000, complete]


def test_over_one_microsecond_and_missing_sample_are_errors(tmp_path, monkeypatch):
    joint, inner = reports(tmp_path / "phase", scope=True, maximum=1001)
    phase = short.summarize_joint(
        tmp_path / "phase", joint, inner, scope_enabled=True, seconds=60)
    assert phase["passed"] is False and phase["phase_severity"] == "ERROR"
    monkeypatch.setattr(short, "_analyze_scope_memory", lambda report, blocks: {
        "relative_ns": [[1001], [0.0], [0.0]]})
    with pytest.raises(ValueError, match="blocking"):
        short.validate_summary(tmp_path / "phase", phase)

    joint, inner = reports(tmp_path / "missing", scope=True, missing=True)
    missing = short.summarize_joint(
        tmp_path / "missing", joint, inner, scope_enabled=True, seconds=60)
    assert missing["passed"] is False and missing["evidence_status"] == "ERROR"
    assert missing["phase_severity"] == "INFO" and missing["phase_max_abs_ns"] == 0.0
    assert not any("phase exceeds" in error for error in missing["errors"])
    assert any("sample count" in error for error in missing["errors"])


def test_bound_raw_change_is_rejected(tmp_path):
    joint, inner = reports(tmp_path, scope=True)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    raw = tmp_path / summary["scope_raw_blocks"][0]["path"]
    raw.write_bytes(b"changed")
    with pytest.raises(ValueError, match="missing or changed"):
        short.validate_summary(tmp_path, summary)


def test_bound_internal_native_change_is_rejected(tmp_path):
    joint, inner = reports(tmp_path, scope=False)
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=False, seconds=60)
    raw = tmp_path / summary["internal_phase_traces"][0]["native_trace"]["path"]
    raw.write_bytes(b"changed")
    with pytest.raises(ValueError, match="missing or changed"):
        short.validate_summary(tmp_path, summary)


def test_rehashed_json_phase_must_still_match_offline_raw_decode(tmp_path, monkeypatch):
    joint, inner = reports(tmp_path, scope=True, maximum=80)
    payload = json.loads(inner.read_text(encoding="utf-8"))
    payload["scope_samples"][0]["phase"]["relative_ns"] = [[90], [0.0], [0.0]]
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    monkeypatch.setattr(short, "_analyze_scope_memory", lambda report, blocks: {
        "relative_ns": [[80], [0.0], [0.0]]})
    with pytest.raises(ValueError, match="offline RAW decode"):
        short.validate_summary(tmp_path, summary)


def test_rehashed_scope_host_interval_must_match_bound_scope_record(tmp_path, monkeypatch):
    joint, inner = reports(tmp_path, scope=True, maximum=80)
    payload = json.loads(inner.read_text(encoding="utf-8"))
    payload["scope_samples"][0]["acquisition_host_ns"][0] += 1
    inner.write_text(json.dumps(payload), encoding="utf-8")
    summary = short.summarize_joint(
        tmp_path, joint, inner, scope_enabled=True, seconds=60)
    monkeypatch.setattr(short, "_analyze_scope_memory", lambda report, blocks: {
        "relative_ns": [[80], [0.0], [0.0]]})
    with pytest.raises(ValueError, match="host-time interval differs"):
        short.validate_summary(tmp_path, summary)


def test_provisional_receipt_is_source_bound_and_requires_stopped_handoff(
        tmp_path, monkeypatch):
    monkeypatch.setattr(p3, "working_source_fingerprint", lambda root: ("source", 7))
    monkeypatch.setattr(p3, "_read_package_build_id", lambda path: "123")
    monkeypatch.setattr(p3, "validate_ota", lambda value, ids, build: None)
    monkeypatch.setattr(p3, "training_inputs_read_back", lambda matrix, tdma, ids: True)
    expected_proof = {"mode": "STOPPED_RECORDS", "passed": True, "records": [1, 2, 3, 4]}
    monkeypatch.setattr(p3, "validate_tdma_stopped_handoff",
                        lambda root, tdma, ids, build: expected_proof)
    receipt = {"schema": short.PROVISIONAL_SCHEMA, "joint_capture_only": True,
               "passed": True, "build_id": "123", "source_tree_sha256": "source",
               "source_file_count": 7, "tdma_board_ids": list(UIDS),
               "ota_board_ids": list(UIDS), "calibration_generation": 71}
    for name in ("firmware_package", "ota_summary", "tdma_summary"):
        path = tmp_path / f"{name}.json"
        path.write_text("{}", encoding="utf-8")
        receipt[name] = short.evidence_entry(tmp_path, path)
    matrix = tmp_path / "trn03_matrix.json"
    matrix.write_text(json.dumps({"calibration_generation": 71}), encoding="utf-8")
    receipt["trn03_matrix"] = short.evidence_entry(tmp_path, matrix)
    handoff = tmp_path / "handoff.json"
    handoff.write_text(json.dumps(expected_proof),
                       encoding="utf-8")
    receipt["tdma_stopped_handoff"] = short.evidence_entry(tmp_path, handoff)
    short.validate_provisional_receipt(tmp_path, receipt, list(UIDS))
    handoff.write_text(json.dumps({"mode": "STOPPED_RECORDS", "passed": False}),
                       encoding="utf-8")
    receipt["tdma_stopped_handoff"] = short.evidence_entry(tmp_path, handoff)
    with pytest.raises(ValueError, match="differs from raw records"):
        short.validate_provisional_receipt(tmp_path, receipt, list(UIDS))


def test_scope_discovery_absence_is_not_an_error(monkeypatch):
    class Manager:
        def list_resources(self):
            return ("USB::OTHER",)

        def close(self):
            pass

    fake = type("Visa", (), {"ResourceManager": lambda: Manager()})
    monkeypatch.setitem(__import__("sys").modules, "pyvisa", fake)
    result = p3.discover_optional_scope("USB::HDO", "SERIAL")
    assert result["present"] is False
    assert result["reason"] == "CONFIGURED_RESOURCE_ABSENT"
