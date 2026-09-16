"""Keep missing input, wrong wiring, stale runs and cleanup failures visible."""
from argparse import Namespace
from contextlib import contextmanager
import json

import pytest

from tools.hardware_acceptance import sequence_feedback_validate as target
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError


def row(count=0):
    return dict(state="PAUSED", run_id=1, generation=2, count=8,
                accepted=count, completed=count, current_index=count % 8,
                current_state=count % 8, next_index=(count + 1) % 8,
                cycles=count // 8, completed_index=count % 8, completed_state=count % 8,
                faults=0, backend_fault=0, cancelled=0, error="NONE", busy_rejected=0, notready_rejected=4)


def test_continuous_observation_can_skip_host_samples():
    target.check_settled(row(157), row(0), 9)


@pytest.mark.parametrize("broken", [None, "pads", "runtime", "lease", "readback"])
def test_standalone_sp8t_checks_pads_without_starting_sequence(broken):
    class FakeBench:
        position = 1

        def write(self, command):
            if command == "TRIG:STOP":
                return
            assert command.startswith("CONF:SWITCH1 ")
            self.position = int(command.split()[1])

        def wait_state(self, state):
            assert state == "IDLE"
            return dict(row(0), state="IDLE")

        def status(self):
            return dict(row(1 if broken == "runtime" else 0), state="IDLE")

        def command(self, command):
            if command == "SYST:ERR?":
                return '0,"No error"'
            if command == "READ:SWITCH1?":
                return f'1,{0 if broken == "readback" else self.position},0,0,0'
            assert command == "READ:IO:STAT?"
            code = (self.position - 1) ^ (1 if broken == "pads" else 0)
            return f'0,{code},{7 if broken == "lease" else 0},0,0'

    phase = {}
    if broken:
        with pytest.raises(AcceptanceError):
            target.standalone_sp8t(FakeBench(), phase)
        assert not phase.get("passed", False)
    else:
        target.standalone_sp8t(FakeBench(), phase)
        assert phase["passed"]
        assert [p["position"] for p in phase["positions"]] == [*range(1, 9), 1]


@pytest.mark.parametrize("response", ["5,DUT,0,1", "7,VNA,0,1,152,11,5,152,11,5",
    "5,DUT,0,2,152,11,5,152,11,5", "5,DUT,0,1,-1,11,5,152,11,5"])
def test_role_identity_and_enable_are_strict(response):
    with pytest.raises(AcceptanceError):
        target.parse_role(response, 5, "DUT")


def test_role_activation_rejection_remains_failure():
    class FakeBench:
        args = Namespace(dut_slot=2, dut_instance=5, vna_slot=3, vna_instance=7)
        staged = False

        def write(self, command):
            assert command == "TRIG:STOP"

        def wait_state(self, state):
            assert state == "IDLE"

        def command(self, command):
            if command == "SYST:ERR?":
                return '0,"No error"'
            if command.startswith("CONF:SEQ:NODE:ROLE"):
                self.staged = True
                return '"STAGED"'
            if command == "READ:SEQ:NODE:ROLE? 5":
                return f'5,"DUT",0,{int(self.staged)},152,12,5,152,11,5'
            if command == "READ:SEQ:NODE:ROLE? 7":
                return f'7,"VNA",0,{int(self.staged)},64,36,16,152,3,3'
            if command == "READ:SEQ:NODE:LOAD?":
                return "load evidence"
            if command == "CONF:SEQ:NODE:ACT":
                return '"REJECTED",1,10,1023,1023,123,3'
            raise AssertionError(command)
    phase = {}
    with pytest.raises(AcceptanceError, match="activation rejected"):
        target.role_config(FakeBench(), phase)
    assert phase["activation"].startswith('"REJECTED"')
    assert phase["after"]["DUT"]["active_enabled"] == 0
    assert not phase.get("passed", False)


@pytest.mark.parametrize("update", [
    {"completed": 0, "accepted": 0}, {"run_id": 2}, {"generation": 3},
    {"accepted": 158}, {"current_state": 6}, {"next_index": 0},
    {"cycles": 0}, {"completed_state": 0}, {"faults": 1}, {"state": "READY"},
])
def test_invalid_or_no_input_cannot_pass(update):
    snapshot = row(157)
    snapshot.update(update)
    with pytest.raises(AcceptanceError):
        target.check_settled(snapshot, row(0), 9)


def test_resume_requires_new_pulses():
    with pytest.raises(AcceptanceError, match="insufficient"):
        target.check_settled(row(157), row(157), 9)


@pytest.mark.parametrize("high", [True, False])
def test_loopback_checks_real_input_and_output(high):
    io = target.parse_io(f"{3 if high else 1},{13 if high else 5},15,1,0")
    target.check_loopback(io, 5, high)
    io["inputs"] ^= 2
    with pytest.raises(AcceptanceError, match="cable"):
        target.check_loopback(io, 5, high)


@pytest.mark.parametrize("value", ["0,0", "0,16,0,0,0", "0,0,0,2,0", "-1,0,0,0,0"])
def test_invalid_io_snapshot(value):
    with pytest.raises(AcceptanceError):
        target.parse_io(value)


def cli(tmp_path):
    return ["--port", "TEST", "--serial-number", "UID", "--out", str(tmp_path / "evidence.json")]


def test_wrong_identity_does_not_send_writes(tmp_path, monkeypatch):
    @contextmanager
    def port(*a, **k):
        yield object()
    commands = []
    monkeypatch.setattr(target, "discover", lambda: {})
    monkeypatch.setattr(target, "open_serial_port", port)
    def command(self, text):
        commands.append(text)
        return 'NO.1,DHRT100,OTHER,0.1' if text == '*IDN?' else '"BUILD"'
    monkeypatch.setattr(target.Bench, "command", command)
    assert target.main(cli(tmp_path)) == 1
    assert commands == ["*IDN?", "SYST:FW:BUILD?"]


def test_phase_failure_always_stops_and_preserves_both_errors(monkeypatch):
    class Bench:
        args = Namespace(mode="bus-loopback")
        stopped = False
        def stop(self):
            self.stopped = True
            raise RuntimeError("stop lost connection")
    def fail(*args):
        raise RuntimeError("loopback disconnected")
    monkeypatch.setattr(target, "bus_loopback", fail)
    bench = Bench()
    report = {"phases": {}}
    with pytest.raises(AcceptanceError):
        target.run_phases(bench, report)
    phase = report["phases"]["bus-loopback"]
    assert bench.stopped and not phase["passed"]
    assert phase["failure"] == "loopback disconnected"
    assert phase["cleanup_failure"] == "stop lost connection"


@pytest.mark.parametrize("reset_fails", [False, True])
def test_manual_failure_attempts_zero_even_when_sequence_already_idle(monkeypatch, reset_fails):
    class FakeBench:
        args = Namespace(mode="standalone-sp8t")
        commands = []
        stopped = False

        def write(self, command):
            self.commands.append(command)
            if reset_fails:
                raise RuntimeError("manual reset lost connection")

        def command(self, command):
            self.commands.append(command)
            return "1,0,0,0,0"

        def stop(self):
            self.stopped = True
            raise RuntimeError("stop readback lost connection")

    def fail(*args):
        raise RuntimeError("manual position 4 readback failed")
    monkeypatch.setattr(target, "standalone_sp8t", fail)
    bench = FakeBench()
    report = {"phases": {}}
    with pytest.raises(AcceptanceError):
        target.run_phases(bench, report)
    phase = report["phases"]["standalone-sp8t"]
    assert bench.stopped and bench.commands[0] == "CONF:SWITCH1 1"
    assert phase["failure"] == "manual position 4 readback failed"
    assert "stop readback lost connection" in phase["cleanup_failure"]
    if reset_fails:
        assert "manual reset lost connection" in phase["cleanup_failure"]
    else:
        assert phase["manual_cleanup_io"]["outputs"] == 0


def test_earlier_evidence_never_overwritten(tmp_path):
    (tmp_path / "evidence.json").write_text('{"failure": "original"}', encoding="utf-8")
    with pytest.raises(FileExistsError):
        target.main(cli(tmp_path))
    assert json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))["failure"] == "original"


def test_discovery_selects_uid_not_first_port():
    args = Namespace(port=None, visa_resource=None, serial_number="UID")
    target.select_transport(args, {"serial": [
        {"port": "COM1", "vid": 0xCAFE, "serial_number": "OTHER"},
        {"port": "COM3", "vid": 0xCAFE, "serial_number": "UID"}], "visa": []})
    assert args.port == "COM3"


def test_notready_rejections_can_grow_while_paused():
    assert target.check_rejections("1,2,0,7,0", row()) == [1, 2, 0, 7, 0]
    for response in ("1,2,0,3,0", "1,2,1,7,0", "1,2,0,7,1", "2,2,0,7,0"):
        with pytest.raises(AcceptanceError):
            target.check_rejections(response, row())


def test_resume_waits_for_async_owner(monkeypatch):
    monkeypatch.setattr(target.time, "sleep", lambda _: None)
    snapshots = iter([row(30), row(30), dict(row(31), state="READY")])
    bench = Namespace(args=Namespace(timeout=1, poll=.01), status=lambda: next(snapshots))
    assert target.wait_running(bench, row(30))["accepted"] == 31


def test_resume_does_not_wait_through_device_reset():
    bench = Namespace(args=Namespace(timeout=1, poll=.01),
                      status=lambda: dict(row(0), run_id=2, state="READY"))
    with pytest.raises(AcceptanceError, match="run changed"):
        target.wait_running(bench, row(30))


def test_stop_allows_paused_input_rejections_but_rejects_faults_and_execution():
    before = row(30)
    stopped = dict(before, state="IDLE", notready_rejected=50)
    target.check_stop(stopped, before)
    for update in ({"faults": 1}, {"backend_fault": 1}, {"error": "RESOURCE_BUSY"},
                   {"accepted": 31}, {"cancelled": 1}, {"run_id": 2}):
        with pytest.raises(AcceptanceError):
            target.check_stop(dict(stopped, **update), before)
