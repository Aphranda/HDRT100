"""Host orchestration: silence, STOP barrier, evidence and failure recovery."""
from contextlib import contextmanager
from types import SimpleNamespace
import copy
import sys
import pytest
from tools.vdc_priority_trace import vdc_priority_trace_capture as capture


def decoded(origin=False, interval_ms=None):
    return dict(status=dict(schema=(8 if origin else 7) if interval_ms is None else (10 if origin else 9),
        sample_interval_ms=interval_ms or 1000, tick_hz=250000000, reason=1), records=[dict(
        bin_index=0, observed_start_raw=100, observed_end_raw=250000100,
        flag_names=["TERMINAL", "PARTIAL"], coverage_incomplete=False, success_count=10,
        frequency_applied_count=0, phase_applied_count=0, residual_min_ns=-96,
        residual_max_ns=96, min_ppb=-50, max_ppb=-50, max_service_gap_ticks=375000,
        first_success_offset_ticks=1000, last_success_offset_ticks=249999000,
        max_success_gap_ticks=500000)])


@pytest.mark.parametrize("value", [0, -1, 61, 600, float("nan"), float("inf")])
def test_duration_rejects_uncovered_windows(value):
    with pytest.raises(ValueError): capture.validate_duration(value)


def test_duration_cli_and_arm_identity():
    assert capture.validate_duration("60") == 60
    assert capture.arm_command(8, origin=True).endswith("ORIGin 8")
    with pytest.raises(ValueError): capture.arm_command(0, origin=False)


def test_assessment_keeps_no_success_separate_from_coverage():
    data = decoded()
    data["records"][0].update(success_count=0, flag_names=["TERMINAL", "PARTIAL", "NO_SUCCESS"])
    result = capture.assess_capture(data, 1, False)
    assert result["coverage_complete"]
    assert not result["all_bins_have_success"]
    assert result["residual_range_ns"] is None
    assert not result["physical_lock_qualified"]


@pytest.mark.parametrize("change", ["short", "full", "gap", "schema"])
def test_assessment_rejects_incomplete_evidence(change):
    data = decoded()
    if change == "short": data["records"][0]["observed_end_raw"] = 200
    if change == "full": data["status"]["reason"] = 2
    if change == "schema": data["status"]["schema"] = 8
    if change == "gap": data["records"][0]["coverage_incomplete"] = True
    assert not capture.assess_capture(data, 1, False)["coverage_complete"]


def test_zero_adjustment_is_valid():
    result = capture.assess_capture(decoded(), 1, False)
    assert result["coverage_complete"] and result["all_bins_have_success"]
    assert result["frequency_applied_count"] == 0


@pytest.mark.parametrize("origin", [False, True])
@pytest.mark.parametrize("seconds", [60, 600])
def test_target_complete_frozen_window_proves_coverage_only(origin, seconds):
    data = decoded(origin, 10000)
    data["status"].update(reason=8, state=3)
    data["records"][0]["observed_end_raw"] = 100 + seconds * 250000000
    result = capture.assess_capture(data, seconds, origin, summary_interval_ms=10000)
    assert result["coverage_complete"] and result["terminal"] and result["freeze_reason"] == 8
    assert not result["requested_window_proven"] and not result["physical_lock_qualified"]
    assert "guard_passed" not in result  # Sealed records cannot invent a GUARD verdict.


@pytest.mark.parametrize("origin", [False, True])
@pytest.mark.parametrize("change", ["old_schema", "not_frozen", "short", "gap",
                                     "no_terminal", "interval", "binding"])
def test_target_complete_still_rejects_incomplete_coverage(origin, change):
    data = decoded(origin, 10000)
    data["status"].update(reason=8, state=3)
    data["records"][0]["observed_end_raw"] = 100 + 60 * 250000000
    interval = 10000
    if change == "old_schema":
        data["status"].update(schema=8 if origin else 7, sample_interval_ms=1000)
        interval = None
    elif change == "not_frozen": data["status"]["state"] = 2
    elif change == "short": data["records"][0]["observed_end_raw"] -= 1
    elif change == "gap":
        data["records"][0]["coverage_incomplete"] = True
        data["records"][0]["flag_names"].append("SERVICE_GAP")
    elif change == "no_terminal": data["records"][0]["flag_names"].remove("TERMINAL")
    elif change == "interval": data["status"]["sample_interval_ms"] = 2000
    elif change == "binding": data["status"]["reason"] = 3
    result = capture.assess_capture(data, 60, origin, summary_interval_ms=interval)
    assert not result["coverage_complete"]
    assert not result["requested_window_proven"] and not result["physical_lock_qualified"]


def setup_run(tmp_path, monkeypatch, fail=None, recovery=False, interval_ms=None, duration_s=1):
    calls, closed, stopped, released, ring_armed = [], [], set(), set(), set()
    quiet = False
    @contextmanager
    def opener(port, *unused):
        try: yield SimpleNamespace(port=port)
        finally: closed.append(port)
    def query(ser, cmd, timeout):
        assert not quiet, "serial traffic during quiet run"
        calls.append((ser.port, cmd))
        if cmd == "SYST:FW:BUILD?": return '"test-build"'
        if cmd == "SYSTem:TDMA:RING:STOP":
            stopped.add(ser.port)
            return "<timeout>" if fail == "stop" and ser.port == "COM2" else "OK"
        if cmd == "SYSTem:TDMA:RING:START":
            return "<timeout>" if fail == "start" else "OK"
        if cmd == "SYSTem:TDMA:RING:ARM":
            ring_armed.add(ser.port)
            return "OK"
        if cmd.startswith("CALibration:ORIGin:TRIAL"): return "2"
        if ":SUMMary:" in cmd: return cmd.rsplit(" ",1)[1].split(",")[0]
        if cmd.endswith(":STOP"):
            assert len(stopped) == 2
            return "1"
        if cmd.endswith(":RELease"):
            released.add(ser.port)
            return "1"
        raise AssertionError(cmd)
    def status(ser, *unused):
        if fail == "arm" and ser.port == "COM2" and not stopped:
            raise TimeoutError("ARM readback lost")
        schema = (8 if ser.port == "COM1" else 7) if interval_ms is None else (10 if ser.port == "COM1" else 9)
        return dict(schema=schema if fail != "arm_schema" else 7,
            sample_interval_ms=(interval_ms or 1000) if fail != "arm_interval" else 2000,
            capture_id=10 if ser.port == "COM1" else 11, state=1, request_seq=1, ack_seq=1)
    def ring(ser, timeout):
        active = ser.port in ring_armed and ser.port not in stopped
        if fail == "still_running" and ser.port == "COM2":
            active = bool(ring_armed)
        return dict(ring_enabled=int(active),
            ring_adapter_started=int(active), ring_up_running=0, ring_down_running=0)
    def sleep(seconds):
        nonlocal quiet
        assert 0 < seconds <= 60
        quiet = True
        calls.append(("host", "quiet"))
        quiet = False
    def download(q, identity):
        assert len(stopped) == 2
        assert sum(cmd == capture.TRACE_ROOT+":STOP" for _,cmd in calls) == 2
        if fail == "read" and identity == 11: raise ValueError("CRC mismatch")
        calls.append((str(identity), "download"))
        return b"native", []
    monkeypatch.setattr(capture, "query", query)
    monkeypatch.setattr(capture, "ring_status", ring)
    monkeypatch.setattr(capture, "wait_armed", status)
    monkeypatch.setattr(capture, "wait_frozen", lambda *a: dict(reason=1))
    monkeypatch.setattr(capture, "wait_released", lambda *a: dict(state=0, reason=6))
    monkeypatch.setattr(capture, "download_capture", download)
    def decode(b, expected_capture_id):
        result = decoded(expected_capture_id == 10, interval_ms)
        template = result["records"][0]
        result["records"] = []
        remaining, start = duration_s, 100
        while remaining > 0:
            seconds = min(remaining, (interval_ms or 1000)/1000)
            ticks = int(seconds*250000000)
            row = dict(template, bin_index=len(result["records"]),
                       observed_start_raw=start, observed_end_raw=start+ticks,
                       last_success_offset_ticks=ticks-1000, flag_names=[])
            result["records"].append(row)
            remaining -= seconds
            start += ticks
        row["flag_names"] = ["PARTIAL", "TERMINAL"]
        if fail == "decoded_interval": result["status"]["sample_interval_ms"] = 2000
        if fail == "decoded_schema": result["status"]["schema"] = 7
        if fail == "saturated": row.update(coverage_incomplete=True, flag_names=["PARTIAL", "TERMINAL", "FIELD_SATURATED"])
        return result
    monkeypatch.setattr(capture, "decode", decode)
    monkeypatch.setattr(capture.time, "sleep", sleep)
    args = SimpleNamespace(board=[capture.BoardSpec("NO1","COM1"),capture.BoardSpec("NO2","COM2")],
        duration_s=duration_s, summary_interval_ms=interval_ms,
        capture_id=10, origin=False, origin_board=["NO1"], start_ring=not recovery,
        skip_arm=recovery, trial_epoch=2, timeout=.01, stop_timeout=.01, poll_interval_s=.001,
        settle=0, baud=115200, expected_build="test-build", out_dir=tmp_path)
    result = capture.run(args, opener=opener)
    return result, calls, closed, released


def test_run_orders_arm_start_quiet_stop_freeze_read_release(tmp_path, monkeypatch):
    result, calls, closed, released = setup_run(tmp_path, monkeypatch)
    assert result["passed"] and result["runtime_scpi_queries"] == 0
    assert len(closed) == len(released) == 2
    assert calls.index(("host","quiet")) < calls.index(("COM1","SYSTem:TDMA:RING:STOP"))
    trial = next(i for i,(_,cmd) in enumerate(calls) if cmd.startswith("CALibration:ORIGin:TRIAL"))
    stop = calls.index(("COM1","SYSTem:TDMA:RING:STOP"))
    assert calls[trial+1:stop] == [("host","quiet")]
    assert (tmp_path / "summary.json").exists()


@pytest.mark.parametrize("failure", ["arm", "start", "stop", "read", "still_running"])
def test_failure_preserves_summary_and_stops_all(tmp_path, monkeypatch, failure):
    result, calls, closed, released = setup_run(tmp_path, monkeypatch, fail=failure)
    assert not result["passed"] and result["errors"]
    assert len(closed) == 2
    assert sum(cmd == "SYSTem:TDMA:RING:STOP" for _,cmd in calls) == 2
    if failure in ("stop", "still_running"):
        assert not released and not any(cmd == "download" for _,cmd in calls)
    if failure == "read": assert "COM2" not in released
    if failure in ("arm","start"): assert ("host","quiet") not in calls
    assert (tmp_path / "summary.json").exists()


def test_recovery_does_not_claim_runtime_silence(tmp_path, monkeypatch):
    result, calls, _, _ = setup_run(tmp_path, monkeypatch, recovery=True)
    assert result["recovery_only"] and not result["passed"]
    assert result["runtime_scpi_queries"] is None
    assert calls[0][1] == "SYSTem:TDMA:RING:STOP"


def test_query_accepts_bare_ok(monkeypatch):
    ser=SimpleNamespace(reset_input_buffer=lambda: None,write=lambda b: None,flush=lambda: None)
    monkeypatch.setattr(capture,"read_serial_line_idle",lambda *a: '\"OK\"')
    assert capture.query(ser,"STOP",1)=="OK"


@pytest.mark.parametrize("interval_ms", [1000, 2000, 10000])
@pytest.mark.parametrize("origin", [False, True])
def test_explicit_interval_selects_new_arm_protocol(interval_ms, origin):
    command = capture.arm_command(12, origin=origin, summary_interval_ms=interval_ms)
    assert command.endswith(f"{'ORIGin' if origin else 'PHASe'} 12,{interval_ms}")
    assert capture.summary_schema(origin, interval_ms) == (10 if origin else 9)
    assert capture.summary_schema(origin, None) == (8 if origin else 7)


@pytest.mark.parametrize("interval_ms", [0, 999, 1001, 1500, 11000, 1000.0, True])
def test_invalid_interval_rejected_before_arm(interval_ms):
    with pytest.raises(ValueError, match="interval"):
        capture.arm_command(12, origin=False, summary_interval_ms=interval_ms)
    with pytest.raises(ValueError, match="interval"):
        capture.validate_duration(1, interval_ms)


@pytest.mark.parametrize("interval_ms", [1000, 2000, 10000])
def test_duration_capacity_scales_only_with_explicit_interval(interval_ms):
    limit = 60*interval_ms/1000
    assert capture.validate_duration(str(limit), interval_ms) == limit
    with pytest.raises(ValueError):
        capture.validate_duration(limit+0.001, interval_ms)
    with pytest.raises(ValueError):
        capture.validate_duration(601, interval_ms)


@pytest.mark.parametrize("interval_ms", [None, 1000, 10000])
def test_cli_interval_and_string_duration(tmp_path, monkeypatch, interval_ms):
    duration = "60" if interval_ms is None else str(60*interval_ms//1000)
    argv = ["capture", "--board", "NO1=COM1", "--duration-s", duration,
            "--skip-arm", "--out-dir", str(tmp_path)]
    if interval_ms is not None:
        argv += ["--summary-interval-ms", str(interval_ms)]
    monkeypatch.setattr(sys, "argv", argv)
    args = capture.parse_args()
    assert args.duration_s == float(duration) and args.summary_interval_ms == interval_ms


@pytest.mark.parametrize("extra", [[], ["--summary-interval-ms", "1000"],
                                   ["--summary-interval-ms", "1500"]])
def test_cli_rejects_duration_or_interval_outside_capacity(tmp_path, monkeypatch, extra):
    monkeypatch.setattr(sys, "argv", ["capture", "--board", "NO1=COM1", "--duration-s", "600",
                                     "--skip-arm", "--out-dir", str(tmp_path), *extra])
    with pytest.raises(SystemExit) as exc:
        capture.parse_args()
    assert exc.value.code == 2


@pytest.mark.parametrize("interval_ms,duration_s", [(1000, 60), (10000, 600)])
def test_extended_run_binds_schema_and_interval_and_stays_silent(tmp_path, monkeypatch, interval_ms, duration_s):
    result, calls, _, _ = setup_run(tmp_path, monkeypatch, interval_ms=interval_ms, duration_s=duration_s)
    assert result["passed"] and result["summary_interval_ms"] == interval_ms
    assert ("COM1", capture.TRACE_ROOT+f":SUMMary:ORIGin 10,{interval_ms}") in calls
    assert ("COM2", capture.TRACE_ROOT+f":SUMMary:PHASe 11,{interval_ms}") in calls
    trial = next(i for i, (_, cmd) in enumerate(calls) if cmd.startswith("CALibration:ORIGin:TRIAL"))
    stop = calls.index(("COM1", "SYSTem:TDMA:RING:STOP"))
    assert calls[trial+1:stop] == [("host", "quiet")]*(duration_s//60)
    for board in result["boards"]:
        assert board["assessment"]["interval_ok"]
        assert not board["assessment"]["physical_lock_qualified"]


@pytest.mark.parametrize("failure", ["arm_schema", "arm_interval", "decoded_schema", "decoded_interval", "saturated"])
def test_extended_capture_identity_and_saturation_cannot_pass(tmp_path, monkeypatch, failure):
    result, calls, _, _ = setup_run(tmp_path, monkeypatch, fail=failure, interval_ms=10000)
    assert not result["passed"]
    if failure.startswith("arm_"):
        assert result["boards"][0]["arm_attempted"]
        assert not any(cmd == "SYSTem:TDMA:RING:START" for _, cmd in calls)
    else:
        assert any(not b["passed"] and b["binary"] for b in result["boards"])


def test_extended_recovery_still_cannot_claim_pass(tmp_path, monkeypatch):
    result, _, _, _ = setup_run(tmp_path, monkeypatch, recovery=True, interval_ms=10000)
    assert result["recovery_only"] and not result["passed"]
    assert result["runtime_scpi_queries"] is None


def test_quiet_wait_splits_final_fraction_without_device_calls(monkeypatch):
    waits = []
    monkeypatch.setattr(capture.time, "sleep", waits.append)
    monkeypatch.setattr(capture, "query", lambda *a: pytest.fail("query during quiet wait"))
    capture.quiet_wait(125.5)
    assert waits == [60, 60, 5.5]
