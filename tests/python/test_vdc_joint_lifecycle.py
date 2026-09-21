"""Device-free checks for the tracked VDC joint outer lifecycle."""

from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import lifecycle


UIDS = (
    "0010071E65B5CB38",
    "FB276192BEF9CCE1",
    "2BD5090FE009FA2A",
    "A1E549202D18ED6A",
)
INITIAL_TIMING = (12000, 16000, 6000)
TRIAL_TIMING = (24000, 32000, 16000)
INITIAL_DELAYS = (0, 4, -8, 12)
TRIAL_DELAYS = (0, -28, -88, -116)


def receipt():
    return {
        "tdma_board_ids": list(UIDS),
        "build_id": "123",
        "source_tree_sha256": "source",
    }


class Backend:
    def __init__(self, *, fail_restore_uid=None, drift_after_restore_uid=None,
                 delay_drift_after_restore_uid=None, fail_open_uid=None,
                 fail_close_once_uid=None):
        self.opened = set()
        self.events = []
        self.timing = {uid: INITIAL_TIMING for uid in UIDS}
        self.delays = dict(zip(UIDS, INITIAL_DELAYS))
        self.child_called = False
        self.fail_restore_uid = fail_restore_uid
        self.drift_after_restore_uid = drift_after_restore_uid
        self.restored_timing_queries = set()
        self.delay_drift_after_restore_uid = delay_drift_after_restore_uid
        self.restored_delay_queries = set()
        self.fail_open_uid = fail_open_uid
        self.fail_close_once_uid = fail_close_once_uid
        self.close_failed = set()

    def open(self, uid):
        self.events.append(("open", uid))
        if uid == self.fail_open_uid:
            raise IOError("open failed")
        self.opened.add(uid)
        return f"COM{UIDS.index(uid) + 3}"

    def close(self, uid):
        self.events.append(("close", uid))
        self.opened.discard(uid)
        if uid == self.fail_close_once_uid and uid not in self.close_failed:
            self.close_failed.add(uid)
            raise IOError("close failed")

    def sleep(self, seconds):
        self.events.append(("sleep", seconds))

    def execute(self, uid, command):
        assert uid in self.opened
        self.events.append(("command", uid, command))
        if command in (lifecycle.OUTPUT + ":STOP", lifecycle.STOP):
            return "OK"
        if command == lifecycle.STATUS:
            values = dict.fromkeys(lifecycle.RUNTIME_FIELDS, 0)
            values["ring_config_seq"] = 3
            values["ring_applied_config_seq"] = 3
            return ",".join(str(values[name]) for name in lifecycle.RUNTIME_FIELDS)
        if command == lifecycle.OUTPUT + "?":
            return "12,0,0"
        if command == "*IDN?":
            return f"NO.{UIDS.index(uid) + 1},DHRT100,{uid},0.1.0"
        if command == "SYSTem:FW:BUILD?":
            return '"123"'
        if command == "SYSTem:BOARD:NO?":
            return str(UIDS.index(uid) + 1)
        if command == lifecycle.TIMING + "?":
            if (self.child_called and uid == self.drift_after_restore_uid
                    and self.timing[uid] == INITIAL_TIMING):
                if uid in self.restored_timing_queries:
                    return ",".join(map(str, TRIAL_TIMING))
                self.restored_timing_queries.add(uid)
            return ",".join(map(str, self.timing[uid]))
        if command.startswith(lifecycle.TIMING + " "):
            value = tuple(map(int, command.split(" ", 1)[1].split(",")))
            if self.child_called and uid == self.fail_restore_uid and value == INITIAL_TIMING:
                raise IOError("restore timing failed")
            self.timing[uid] = value
            return ",".join(map(str, value))
        if command == lifecycle.DELAY + "?":
            expected = INITIAL_DELAYS[UIDS.index(uid)]
            if (self.child_called and uid == self.delay_drift_after_restore_uid
                    and self.delays[uid] == expected):
                if uid in self.restored_delay_queries:
                    return str(TRIAL_DELAYS[UIDS.index(uid)])
                self.restored_delay_queries.add(uid)
            return str(self.delays[uid])
        if command.startswith(lifecycle.DELAY + " "):
            value = int(command.rsplit(" ", 1)[1])
            self.delays[uid] = value
            return str(value)
        if command == lifecycle.BASELINE:
            return "2,250000000"
        if command == lifecycle.PI:
            return "16384,256,1000,10000,10000,1,0,0,0"
        if command == lifecycle.ROLE:
            return f"{0 if uid == UIDS[0] else 1},0,1,1,0"
        if command == lifecycle.ERROR:
            return '0,"No error"'
        raise AssertionError(f"unexpected command: {command}")


def run_trial(tmp_path, *, child_code=0, fail_restore_uid=None,
              drift_after_restore_uid=None, delay_drift_after_restore_uid=None,
              fail_open_uid=None, fail_close_once_uid=None):
    backend = Backend(fail_restore_uid=fail_restore_uid,
                      drift_after_restore_uid=drift_after_restore_uid,
                      delay_drift_after_restore_uid=delay_drift_after_restore_uid,
                      fail_open_uid=fail_open_uid,
                      fail_close_once_uid=fail_close_once_uid)
    child_observation = {}

    def child(command, timeout, output):
        backend.child_called = True
        child_observation.update(
            command=list(command), timeout=timeout, opened=set(backend.opened),
            timing=dict(backend.timing), delays=dict(backend.delays), output=output)
        return child_code

    trial = lifecycle.ResumeTrial(
        backend, receipt(), tmp_path / "run", TRIAL_TIMING,
        ["python", "capture.py"], board_uids=UIDS,
        output_delays=TRIAL_DELAYS, child=child, capture_timeout=60)
    trial.report["workflow_inputs"] = {"schema": "TEST_INPUTS"}
    trial.save()
    return trial.run(), backend, child_observation


def test_tracked_lifecycle_stops_runs_restores_and_closes(tmp_path):
    report, backend, child = run_trial(tmp_path)
    assert report["passed"] and report["complete"] and report["main_completed"]
    assert report["schema"] == "DPLL_RAM_TIMING_TRIAL_V1"
    assert report["lifecycle_version"] == "VDC_JOINT_OUTER_LIFECYCLE_V1"
    assert report["basis_script_sha256"] == report["lifecycle_source"]["sha256"]
    assert not report["errors"] and not report["cleanup_errors"]
    assert report["restore_needed"] == [] and not backend.opened
    assert report["restore_needed_detail"] == {"timing": [], "delay": []}
    assert child["opened"] == set()
    assert set(child["timing"].values()) == {TRIAL_TIMING}
    assert tuple(child["delays"][uid] for uid in UIDS) == TRIAL_DELAYS
    assert set(backend.timing.values()) == {INITIAL_TIMING}
    assert tuple(backend.delays[uid] for uid in UIDS) == INITIAL_DELAYS
    assert report["workflow_inputs"] == {"schema": "TEST_INPUTS"}
    assert (tmp_path / "run/report.json").is_file()
    names = [row["name"] for row in report["stages"]]
    assert names == [
        "initial_output_and_tdma_stop", "original_ram_and_invariants",
        "apply_ram_timing", "close_before_capture", "child_capture",
        "final_output_and_tdma_stop", "restore_original_ram",
        "final_readbacks", "close_final_connections",
    ]
    commands = [event for event in backend.events if event[0] == "command"]
    first_query = next(i for i, event in enumerate(commands) if "?" in event[2])
    assert [event[2] for event in commands[:first_query]] == (
        [lifecycle.OUTPUT + ":STOP"] * len(UIDS) + [lifecycle.STOP] * len(UIDS))


def test_child_failure_is_retained_but_cleanup_restores_everything(tmp_path):
    report, backend, child = run_trial(tmp_path, child_code=2)
    assert child["opened"] == set()
    assert not report["passed"] and report["complete"]
    assert report["errors"] == [
        {"stage": "child_capture", "error": "RuntimeError('Capture exited 2')"}
    ]
    assert not report["cleanup_errors"] and report["restore_needed"] == []
    assert set(backend.timing.values()) == {INITIAL_TIMING}
    assert tuple(backend.delays[uid] for uid in UIDS) == INITIAL_DELAYS
    assert not backend.opened


def test_restore_failure_remains_cleanup_error_and_dirty_state(tmp_path):
    report, backend, _ = run_trial(tmp_path, fail_restore_uid=UIDS[2])
    assert not report["passed"] and report["restore_needed"] == [UIDS[2]]
    assert any(row["stage"] == "restore_original_ram" for row in report["cleanup_errors"])
    assert any(row["stage"] == "final_readbacks" for row in report["cleanup_errors"])
    assert backend.timing[UIDS[2]] == TRIAL_TIMING
    assert not backend.opened


def test_final_readback_drift_remains_restore_needed(tmp_path):
    report, backend, _ = run_trial(tmp_path, drift_after_restore_uid=UIDS[1])
    assert not report["passed"] and report["restore_needed"] == [UIDS[1]]
    assert report["restore_needed_detail"] == {
        "timing": [UIDS[1]], "delay": [UIDS[1]]}
    assert any(row["stage"] == "final_readbacks" for row in report["cleanup_errors"])
    assert not backend.opened


def test_final_delay_drift_remains_restore_needed(tmp_path):
    report, backend, _ = run_trial(
        tmp_path, delay_drift_after_restore_uid=UIDS[3])
    assert not report["passed"] and report["restore_needed"] == [UIDS[3]]
    assert report["restore_needed_detail"] == {
        "timing": [UIDS[3]], "delay": [UIDS[3]]}
    assert any(row["stage"] == "final_readbacks" for row in report["cleanup_errors"])
    assert not backend.opened


def test_run_level_open_failure_still_attempts_every_close(tmp_path):
    report, backend, child = run_trial(tmp_path, fail_open_uid=UIDS[1])
    assert not child and not report["passed"] and report["complete"]
    assert any(row["stage"] == "initial_output_and_tdma_stop" for row in report["errors"])
    assert any(row["stage"] == "final_stop" for row in report["cleanup_errors"])
    closed = [event[1] for event in backend.events if event[0] == "close"]
    assert set(closed) == set(UIDS) and not backend.opened


def test_run_level_close_failure_is_retained_then_cleanup_recovers(tmp_path):
    report, backend, child = run_trial(tmp_path, fail_close_once_uid=UIDS[2])
    assert not child and not report["passed"] and report["complete"]
    assert len(report["errors"]) == 1
    assert report["errors"][0]["stage"] == "close_before_capture"
    assert UIDS[2] in report["errors"][0]["error"]
    assert "close failed" in report["errors"][0]["error"]
    assert not report["cleanup_errors"] and report["restore_needed"] == []
    assert not backend.opened


def test_real_child_timeout_is_killed_and_logs_are_retained(tmp_path):
    output = tmp_path / "child"
    output.mkdir()
    with pytest.raises(subprocess.TimeoutExpired):
        lifecycle.child_capture(
            [sys.executable, "-c", "import time; time.sleep(2)"], .05, output)
    assert (output / "child.stdout.log").is_file()
    assert (output / "child.stderr.log").is_file()


def test_instance_response_matcher_handles_timing_and_signed_delay():
    assert lifecycle.response_matches(lifecycle.TIMING + "?", "12000,16000,6000")
    assert lifecycle.response_matches(lifecycle.TIMING + " 1,2,3", "1,2,3")
    assert lifecycle.response_matches(lifecycle.DELAY + "?", "-116")
    assert not lifecycle.response_matches(lifecycle.DELAY + "?", "2147483648")
    assert lifecycle.response_matches("SYSTem:ERR?", '0,"No error"')


def test_serial_session_uses_instance_response_matcher(monkeypatch):
    from tools.common.scpi import scpi_serial

    class MemorySerial:
        def __init__(self):
            self.lines = [b"stale\n", b"-116\n"]

        def reset_input_buffer(self): pass
        def reset_output_buffer(self): pass
        def write(self, raw): self.written = raw
        def flush(self): pass
        def close(self): pass

        def read(self, count):
            if not self.lines:
                return b""
            line = self.lines[0]
            value, self.lines[0] = line[:1], line[1:]
            if not self.lines[0]:
                self.lines.pop(0)
            return value

    peer = MemorySerial()
    monkeypatch.setattr(scpi_serial.serial, "Serial", lambda *args, **kwargs: peer)
    session = scpi_serial.SerialSession(
        "COM1", 115200, .1, 0, read_timeout_s=.01,
        response_matcher=lifecycle.response_matches).open()
    assert session.execute(lifecycle.DELAY + "?") == "-116"
    assert peer.written == (lifecycle.DELAY + "?\n").encode("ascii")
    session.close()


def test_serial_backend_admits_uid_mapping_and_cleans_partial_open():
    ports = [SimpleNamespace(serial_number=uid.lower(), device=f"COM{index}")
             for index, uid in enumerate(UIDS, 3)]
    sessions = {}

    class Session:
        def __init__(self, port):
            self.port = port
            self.closed = False

        def open(self):
            if self.port == "COM4":
                raise IOError("open failed")
            return self

        def close(self):
            self.closed = True

        def execute(self, command):
            return "OK"

    def factory(port):
        sessions[port] = Session(port)
        return sessions[port]

    backend = lifecycle.SerialBackend(
        UIDS, ports_provider=lambda: ports, session_factory=factory)
    assert backend.open(UIDS[0]) == "COM3"
    assert backend.execute(UIDS[0], "STOP") == "OK"
    backend.close(UIDS[0])
    assert sessions["COM3"].closed and not backend.sessions
    with pytest.raises(IOError, match="open failed"):
        backend.open(UIDS[1])
    assert sessions["COM4"].closed and UIDS[1] not in backend.sessions
    with pytest.raises(RuntimeError, match="Unauthorized UID"):
        backend.open("OTHER")


def test_serial_backend_rejects_duplicate_uid_ports():
    ports = [SimpleNamespace(serial_number=UIDS[0], device="COM3"),
             SimpleNamespace(serial_number=UIDS[0], device="COM9")]
    backend = lifecycle.SerialBackend(UIDS, ports_provider=lambda: ports)
    with pytest.raises(RuntimeError, match="Duplicate USB UID"):
        backend.open(UIDS[0])
