import json
from contextlib import contextmanager
from pathlib import Path

import pytest

from tools.calibration_loopback_validate import calibration_loopback_validate as target


SCHEDULE = "1,250000000,250000,8,11,3,99,0"


def snapshot(epoch: int, *, complete: int = 1, flags: int = 5,
             reject_reason: int = 5, result_valid: int = 0,
             active_eligible: int = 0) -> str:
    return ",".join(map(str, (
        0, complete, 250000000, 4, 128, 15, flags, reject_reason, epoch,
        92, 144, 164, 216, result_valid, 124, 104, 52, active_eligible,
    )))


class FakeTransport:
    def __init__(self, *, serial="board-1", build="build-1",
                 terminals=None, fail_command=None):
        self.serial = serial
        self.build = build
        self.terminals = list(terminals or [snapshot(2)])
        self.fail_command = fail_command
        self.commands = []
        self.read_index = 0
        self.started = False

    def __call__(self, command: str) -> str:
        self.commands.append(command)
        if command == self.fail_command:
            raise RuntimeError("injected command failure")
        if command == "*IDN?":
            return f"HAOFV,DHRT100,{self.serial},1"
        if command == "SYST:FW:BUILD?":
            return f'"{self.build}"'
        if command == "SYST:TDMA:SCHEDULE?":
            return SCHEDULE
        if command == "READ:CAL:LOOP?":
            if not self.started:
                return snapshot(self.read_index)
            result = self.terminals[self.read_index]
            self.read_index += 1
            return result
        if command.startswith("CAL:LOOP:START"):
            self.started = True
            return "OK"
        if command == "CAL:LOOP:STOP":
            self.started = False
            return "OK"
        if command == "SYST:ERR?":
            return '0,"No error"'
        return "OK"


def arguments(tmp_path: Path, **overrides):
    values = dict(
        port=None, visa_resource="USB::FAKE", serial_number="board-1",
        expected_build="build-1", words=128, runs=1, duration_s=0.01,
        poll_s=0.001, inter_run_s=0.0, quiet_capture=True, baud=115200,
        timeout_s=1.0, settle_s=0.0, out_dir=str(tmp_path),
    )
    values.update(overrides)
    return target.argparse.Namespace(**values)


def install_transport(monkeypatch: pytest.MonkeyPatch, fake: FakeTransport) -> None:
    @contextmanager
    def opener(_args):
        yield fake

    monkeypatch.setattr(target, "open_transport", opener)


def read_summary(path: Path) -> dict:
    return json.loads((path / "summary.json").read_text(encoding="utf-8"))


@pytest.mark.parametrize("field,value", [
    ("serial", "another-board"),
    ("build", "another-build"),
])
def test_identity_mismatch_is_rejected_before_state_changing_writes(
        tmp_path, monkeypatch, field, value):
    fake = FakeTransport(**{field: value})
    install_transport(monkeypatch, fake)

    assert target.run(arguments(tmp_path)) == 1

    assert fake.commands == ["*IDN?", "SYST:FW:BUILD?"]
    report = read_summary(tmp_path)
    assert "mismatch" in report["failure"]
    assert report["records"][-1]["command"] == "SYST:FW:BUILD?"


def test_quiet_capture_has_no_query_during_fixed_wait(tmp_path, monkeypatch):
    fake = FakeTransport()
    install_transport(monkeypatch, fake)
    observed = []

    def quiet_wait(seconds):
        observed.append((seconds, list(fake.commands)))
        assert fake.commands[-1] == "CAL:LOOP:START 128"
        assert fake.commands.count("READ:CAL:LOOP?") == 1

    monkeypatch.setattr(target.time, "sleep", quiet_wait)

    assert target.run(arguments(tmp_path)) == 0
    assert observed == [(0.01, observed[0][1])]
    assert fake.commands.count("READ:CAL:LOOP?") == 2
    report = read_summary(tmp_path)
    assert report["settings"]["quiet_capture"] is True
    window = report["quiet_windows"][0]
    assert window["records_before"] == window["records_after"]
    assert window["host_monotonic_end_s"] >= window["host_monotonic_start_s"]
    assert all(record["host_monotonic_end_s"] >= record["host_monotonic_start_s"]
               for record in report["records"])


def test_poll_timeout_preserves_clock_and_raw_command_evidence(tmp_path, monkeypatch):
    fake = FakeTransport(terminals=[snapshot(0, complete=0)] * 4)
    install_transport(monkeypatch, fake)
    ticks = iter((10.0, 10.0, 10.02))
    monkeypatch.setattr(target.time, "monotonic", lambda: next(ticks))
    monkeypatch.setattr(target.time, "sleep", lambda _seconds: None)

    result = target.run(arguments(tmp_path, quiet_capture=False, duration_s=0.01))

    assert result == 1
    report = read_summary(tmp_path)
    assert "TimeoutError" in report["failure"]
    assert any(row["command"] == "CAL:LOOP:STOP" and row["phase"] == "cleanup"
               for row in report["records"])
    assert report["passed"] is False


def test_raw_physical_success_is_not_promoted_to_formal_calibration(
        tmp_path, monkeypatch):
    fake = FakeTransport(terminals=[snapshot(7), snapshot(8), snapshot(9)])
    install_transport(monkeypatch, fake)
    monkeypatch.setattr(target.time, "sleep", lambda _seconds: None)

    assert target.run(arguments(tmp_path, runs=3)) == 0

    report = read_summary(tmp_path)
    assert report["raw_measurement_passed"] is True
    assert report["runs_raw_passed"] == 3
    assert report["firmware_calibration_passed"] is False
    assert report["active_eligible"] is False
    assert report["formal_calibration_claimed"] is False
    assert report["load_guard"]["unchanged"] is True
    phases = [record.get("phase") for record in report["records"]]
    assert phases.index("load_guard_after") < phases.index("final_error")
    for row in report["run_results"]:
        measured = row["snapshot"]
        assert measured["flags"] == 5
        assert measured["clk_path_ns"] == 52
        assert measured["data_path_ns"] == 52
        assert measured["computed_residence_ns"] == 20
        assert measured["computed_raw_path_sum_ns"] == 104
        assert measured["raw_fields"][8:13] == [
            measured["epoch"], 92, 144, 164, 216]


def test_command_failure_preserves_original_response_and_cleanup(tmp_path, monkeypatch):
    fake = FakeTransport(fail_command="CAL:LOOP:START 128")
    install_transport(monkeypatch, fake)

    assert target.run(arguments(tmp_path)) == 1

    report = read_summary(tmp_path)
    failed = next(row for row in report["records"]
                  if row["command"] == "CAL:LOOP:START 128")
    assert failed["error"] == "RuntimeError: injected command failure"
    assert any(row["phase"] == "baseline" and "response" in row
               for row in report["records"])
    assert any(row["command"] == "CAL:LOOP:STOP" for row in report["records"])
    assert report["failure"] == "RuntimeError: injected command failure"


def test_usbtmc_transport_uses_shared_visa_helpers(monkeypatch):
    calls = []
    instrument = object()

    @contextmanager
    def open_visa(resource, timeout_s):
        calls.append(("open", resource, timeout_s))
        yield instrument

    def command(session, text, timeout_s):
        calls.append(("query", session, text, timeout_s))
        return "response"

    monkeypatch.setattr(target, "open_visa_resource", open_visa)
    monkeypatch.setattr(target, "visa_command", command)
    args = target.argparse.Namespace(visa_resource="USB::1", port=None,
                                     timeout_s=2.5, baud=115200, settle_s=0)
    with target.open_transport(args) as transact:
        assert transact("*IDN?") == "response"
    assert calls == [
        ("open", "USB::1", 2.5),
        ("query", instrument, "*IDN?", 2.5),
    ]


def test_cdc_transport_remains_supported(monkeypatch):
    calls = []
    serial = object()

    @contextmanager
    def open_serial(port, baud, timeout_s, settle_s):
        calls.append(("open", port, baud, timeout_s, settle_s))
        yield serial

    def command(session, text, timeout_s):
        calls.append(("query", session, text, timeout_s))
        return "response"

    monkeypatch.setattr(target, "open_serial_port", open_serial)
    monkeypatch.setattr(target, "query", command)
    args = target.argparse.Namespace(visa_resource=None, port="COM8",
                                     timeout_s=2.5, baud=230400, settle_s=0.5)
    with target.open_transport(args) as transact:
        assert transact("*IDN?") == "response"
    assert calls == [
        ("open", "COM8", 230400, 2.5, 0.5),
        ("query", serial, "*IDN?", 2.5),
    ]


def test_cli_requires_explicit_identity_and_exactly_one_transport():
    args = target.parse_args([
        "COM8", "--serial-number", "uid", "--expected-build", "build"])
    assert args.port == "COM8" and args.visa_resource is None
    with pytest.raises(SystemExit):
        target.parse_args([
            "COM8", "--visa-resource", "USB::1", "--serial-number", "uid",
            "--expected-build", "build"])


@pytest.mark.parametrize("option,value", [
    ("--duration-s", "nan"), ("--poll-s", "inf"),
    ("--timeout-s", "-inf"), ("--settle-s", "nan"),
])
def test_cli_rejects_non_finite_times(option, value):
    with pytest.raises(SystemExit):
        target.parse_args([
            "COM8", "--serial-number", "uid", "--expected-build", "build",
            option, value])
