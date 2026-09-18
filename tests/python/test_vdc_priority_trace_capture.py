"""Host orchestration: silence, STOP barrier, evidence and failure recovery."""
from contextlib import contextmanager
from types import SimpleNamespace
import copy
import pytest
from tools.vdc_priority_trace import vdc_priority_trace_capture as capture


def decoded(origin=False):
    return dict(status=dict(schema=8 if origin else 7, tick_hz=250000000, reason=1), records=[dict(
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


def setup_run(tmp_path, monkeypatch, fail=None, recovery=False):
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
        if ":SUMMary:" in cmd: return cmd.rsplit(" ",1)[1]
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
        return dict(schema=8 if ser.port == "COM1" else 7,
            capture_id=10 if ser.port == "COM1" else 11, state=1, request_seq=1, ack_seq=1)
    def ring(ser, timeout):
        active = ser.port in ring_armed and ser.port not in stopped
        if fail == "still_running" and ser.port == "COM2":
            active = bool(ring_armed)
        return dict(ring_enabled=int(active),
            ring_adapter_started=int(active), ring_up_running=0, ring_down_running=0)
    def sleep(seconds):
        nonlocal quiet
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
    monkeypatch.setattr(capture, "decode", lambda b,expected_capture_id: decoded(expected_capture_id==10))
    monkeypatch.setattr(capture.time, "sleep", sleep)
    args = SimpleNamespace(board=[capture.BoardSpec("NO1","COM1"),capture.BoardSpec("NO2","COM2")],
        duration_s=1, capture_id=10, origin=False, origin_board=["NO1"], start_ring=not recovery,
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
