"""Admission/result checks and failure-preserving independent repeat procedure."""
from contextlib import nullcontext
import json

import pytest

from tools.hardware_acceptance import sequence_repeat_validate as tool


def args(tmp_path, *extra):
    return tool.parse_args(["--port", "COM3", "--serial-number", "UID", "--build", "BUILD",
                            "--out", str(tmp_path / "evidence.json"), *extra])


def row(count=0, state="READY", run=1):
    return {"state": state, "run_id": run, "generation": run, "count": 8,
            "accepted": count, "completed": count, "current_index": count % 8,
            "current_state": count % 8, "completed_index": count % 8, "completed_state": count % 8,
            "error": "NONE", "faults": 0, "backend_fault": 0, "cancelled": 0}


class FakeBench:
    def __init__(self, settings):
        self.args = settings
        self.commands = []
        self.current = row(0, "IDLE", 0)
        self.started = False
        self.initial = False
        self.repeat = 1
        self.finished = False
        self.forge_finished = False

    def identity(self):
        self.commands.append("IDENTITY")

    def write(self, command):
        self.commands.append(command)
        if command.startswith("CONF:SEQ:REP "):
            self.repeat = int(command.split()[-1])
        elif command == "TRIG:START":
            self.started = self.initial = True
            self.current = row(0)
        elif command == "TRIG:STOP":
            self.started = False
            self.current["state"] = "IDLE"
        elif command == "TRIG:SEQ:NEXT":
            self.advance()

    def advance(self):
        count = self.current["completed"] + 1
        done = self.repeat != 0 and count == self.repeat * 8 - 1
        self.current = row(count, "IDLE" if done else "READY")
        self.finished = done
        if done:
            self.started = False

    def command(self, command):
        self.commands.append(command)
        if command == "READ:SEQ:REP?":
            return f"{self.repeat},{self.repeat},{int(self.finished and not self.forge_finished)}"
        if command == "READ:IO:STAT?":
            return "1,0,0,0,0"  # source remains high after stop
        if command == "SYST:ERR?":
            return '0,"No error"'
        raise AssertionError(command)

    def status(self):
        if self.started and self.args.source == "IN1" and not self.initial:
            self.advance()
        self.initial = False
        return self.current.copy()

    def wait_state(self, expected):
        assert self.current["state"] == expected
        return self.current.copy()

    def configure(self):
        self.commands.append("CONFIGURE")


@pytest.fixture
def no_hardware(monkeypatch):
    monkeypatch.setattr(tool, "stop_ring", lambda *unused: None)
    monkeypatch.setattr(tool.time, "sleep", lambda *unused: None)


@pytest.mark.parametrize("source", ["BUS", "IN1"])
@pytest.mark.parametrize("repeat", [1, 10, 100])
def test_complete_finite_rounds_without_source_stop(tmp_path, no_hardware, source, repeat):
    bench = FakeBench(args(tmp_path, "--source", source, "--repeat", str(repeat)))
    report = {}
    tool.execute(bench, object(), report)
    assert report["functional_execution_verified"]
    assert report["repeat_result"]["finished"] == 1
    assert report["idle_after_quiet"]["sequence"]["completed"] == repeat * 8 - 1
    assert report["idle_after_quiet"]["io"]["inputs"] == 1
    assert bench.commands.count("TRIG:SEQ:NEXT") == (repeat * 8 - 1 if source == "BUS" else 0)
    assert bench.commands.count("TRIG:STOP") == 1  # no host stop caused finite completion
    assert bench.commands.index("CONF:SEQ:LINK OFF") < bench.commands.index("TRIG:START")


def test_continuous_requires_explicit_stop(tmp_path, no_hardware):
    bench = FakeBench(args(tmp_path, "--source", "BUS", "--repeat", "0"))
    report = {}
    tool.execute(bench, object(), report)
    assert report["continuous_observation"]["completed"] == 9
    assert bench.commands.count("TRIG:STOP") == 2
    assert not bench.finished


def test_large_configuration_does_not_claim_execution(tmp_path, no_hardware):
    bench = FakeBench(args(tmp_path, "--repeat", "10000", "--configure-only"))
    report = {}
    tool.execute(bench, object(), report)
    assert report["configuration_verified"]
    assert not report.get("functional_execution_verified")
    assert "TRIG:START" not in bench.commands
    assert report["configured_repeat"]["configured"] == 10000


def test_idle_without_finished_receipt_is_failure(tmp_path, no_hardware):
    bench = FakeBench(args(tmp_path, "--source", "BUS"))
    bench.forge_finished = True
    report = {}
    with pytest.raises(RuntimeError, match="finished receipt"):
        tool.execute(bench, object(), report)
    assert report["repeat_result"]["finished"] == 0
    assert not report.get("functional_execution_verified")


@pytest.mark.parametrize("mutation,match", [
    ({"accepted": 8, "completed": 8}, "quota"),
    ({"run_id": 2}, "run changed"),
    ({"backend_fault": 3}, "fault"),
    ({"current_state": 1}, "selected"),
    ({"accepted": 3, "completed": 0}, "accounting"),
])
def test_invalid_hardware_result_never_passes(mutation, match):
    previous = row(0)
    current = row(0)
    current.update(mutation)
    with pytest.raises(RuntimeError, match=match):
        tool.check_status(current, previous, 7)


@pytest.mark.parametrize("value", ["1,1", "-1,1,0", "1,1,2", "4294967296,0,0", "１,1,0"])
def test_repeat_parser_rejects_malformed(value):
    with pytest.raises(RuntimeError):
        tool.parse_repeat(value)


def test_idle_quiet_rejects_extra_step(tmp_path, no_hardware):
    bench = FakeBench(args(tmp_path))
    calls = iter([row(7, "IDLE"), row(8, "IDLE")])
    bench.status = lambda: next(calls)
    with pytest.raises(RuntimeError, match="continuing source"):
        tool.verify_quiet(bench, {})


def test_identity_transport_failure_saved_and_no_cleanup_writes(tmp_path, monkeypatch):
    settings = args(tmp_path)
    monkeypatch.setattr(tool, "discover", lambda: {"serial": [], "visa": []})
    monkeypatch.setattr(tool, "open_serial_port", lambda *a, **k: nullcontext(object()))
    attempts = []
    def fail_query(port, command, timeout):
        attempts.append(command)
        raise OSError("disconnected")
    monkeypatch.setattr(tool.ring, "transport_query", fail_query)
    result = tool.main(["--port", "COM3", "--serial-number", "UID", "--build", "BUILD",
                        "--out", str(settings.out)])
    report = json.loads(settings.out.read_text(encoding="utf-8"))
    assert result == 1 and not report["passed"]
    assert attempts == ["*IDN?"]
    assert "disconnected" in report["transport_transcript"][0]["exception"]
    assert not report["functional_execution_verified"]


def test_existing_evidence_refused_before_discovery(tmp_path, monkeypatch):
    settings = args(tmp_path)
    settings.out.write_text("original failed evidence", encoding="utf-8")
    monkeypatch.setattr(tool, "discover", lambda: pytest.fail("must not touch hardware"))
    with pytest.raises(FileExistsError):
        tool.main(["--serial-number", "UID", "--build", "BUILD", "--out", str(settings.out)])
    assert settings.out.read_text(encoding="utf-8") == "original failed evidence"
