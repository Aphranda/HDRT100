"""Failure-sensitive checks for the persisted START pad observation procedure."""
from contextlib import nullcontext
import json

import pytest

from tools.hardware_acceptance import sequence_start_validate as tool


def arguments(tmp_path, *extra):
    return ["--port", "COM3", "--serial-number", "UID", "--build", "BUILD",
            "--out", str(tmp_path / "evidence.json"), *extra]


class Clock:
    now = 0

    def monotonic(self):
        self.now += .001
        return self.now

    def sleep(self, duration):
        self.now += duration


class FakeBench:
    def __init__(self, args, clock):
        self.args, self.clock = args, clock
        self.commands = []
        self.plan = tool.PLAN
        self.output = ["7", "8", "PULSE", "300000", "300000"]
        self.started = False
        self.steps = self.run = self.repeat = 0
        self.state_count = 0
        self.codes = {}
        self.finished = False
        self.start_time = 0
        self.omit_start_pulse = self.count_start = self.ignore_settle = False
        self.ignore_stop = self.wrong_loopback = False

    def identity(self):
        self.commands.append("IDENTITY")

    def write(self, command):
        self.commands.append(command)
        if command.startswith("CONF:TRIG "):
            channels, pol, frequencies, waves = map(int, command.split(" ")[1].split(","))
            assert pol == 0
            self.state_count = channels * frequencies * waves
            self.codes.clear()
        elif command.startswith("CONF:SEQ SP8T,"):
            plan = tuple(map(int, command.split(",")[1:]))
            if any(state < 0 or state >= self.state_count for state in plan):
                raise RuntimeError("STATE_RANGE")
            self.plan = plan
        elif command.startswith("CONF:SEQ:CODE "):
            state, code = map(int, command.split(" ")[1].split(","))
            if not 0 <= state < self.state_count or code & ~int(self.output[0]):
                raise RuntimeError("INVALID")
            self.codes[state] = code
        elif command.startswith("CONF:SEQ:OUTPUT "):
            self.output = command.split(" ")[1].split(",")
        elif command.startswith("CONF:SEQ:REP "):
            self.repeat = int(command.split(" ")[1])
        elif command == "TRIG:START":
            self.started = True
            self.finished = False
            self.steps = int(self.count_start)
            self.run += 1
            self.start_time = self.clock.now
        elif command == "TRIG:STOP" and not self.ignore_stop:
            self.started = False
        elif command == "TRIG:SEQ:NEXT":
            assert self.state() == "READY"
            self.steps += 1
            self.start_time = self.clock.now

    def state(self):
        if not self.started:
            return "IDLE"
        elapsed = self.clock.now - self.start_time
        duration = (int(self.output[3]) + int(self.output[4])) / 1e6
        if elapsed < duration:
            return "STARTING" if not self.steps else "BUSY"
        if len(self.plan) == self.repeat == 1:
            self.finished = True
            self.started = False
            return "IDLE"
        return "READY"

    def status(self):
        state = self.state()
        completed = self.steps if state in ("READY", "IDLE") else max(0, self.steps - 1)
        return {"state": state, "run_id": self.run, "generation": self.run, "count": len(self.plan),
                "current_index": self.steps % len(self.plan), "current_state": self.plan[self.steps % len(self.plan)],
                "completed_index": completed % len(self.plan), "completed_state": self.plan[completed % len(self.plan)],
                "accepted": self.steps, "completed": completed, "error": "NONE",
                "faults": 0, "backend_fault": 0, "cancelled": 0}

    def wait_state(self, expected):
        deadline = self.clock.now + self.args.timeout
        while self.clock.monotonic() < deadline:
            row = self.status()
            if row["state"] == expected:
                return row
            self.clock.sleep(.01)
        raise RuntimeError("wait state timeout")

    def command(self, command):
        self.commands.append(command)
        if command == "READ:SEQ? SP8T":
            # scpi_config_sequence_q: id,index,crc,count,has_index,state_ids.
            return f'"SP8T",4294967295,123,{len(self.plan)},0,' + ",".join(map(str, self.plan))
        if command == "READ:SEQ:OUTPUT?":
            # scpi_sequence_output_config_q adds generation and valid.
            return ",".join(self.output) + ",17,1"
        if command.startswith("READ:SEQ:CODE? "):
            state = int(command.split(" ")[1])
            return f"{state},{self.codes[state]}"
        if command == "READ:SEQ:SOUR?":
            return '"MANUAL","RISING"'
        if command == "READ:SEQ:REP?":
            return f"{self.repeat},{self.repeat},{int(self.finished)}"
        if command == "SYST:ERR?":
            return '0,"No error"'
        if command == "READ:IO:STAT?":
            self.state()
            if not self.started:
                return "0,0,0,0,0"
            elapsed = self.clock.now - self.start_time
            settled = self.ignore_settle or elapsed >= int(self.output[3]) / 1e6
            high = settled and (self.output[2] == "LEVEL" or
                               (self.output[2] == "PULSE" and elapsed <
                                (int(self.output[3]) + int(self.output[4])) / 1e6))
            if self.omit_start_pulse and self.steps == 0:
                high = False
            output = self.plan[self.steps % len(self.plan)] | (8 if high else 0)
            inputs = 2 if high and not self.wrong_loopback else 0
            return f"{inputs},{output},{7 if self.output[2] == 'NONE' else 15},1,0"
        raise AssertionError(command)


@pytest.fixture
def bench(tmp_path, monkeypatch):
    clock = Clock()
    monkeypatch.setattr(tool.time, "monotonic", clock.monotonic)
    monkeypatch.setattr(tool.time, "sleep", clock.sleep)
    monkeypatch.setattr(tool, "stop_ring", lambda *unused: None)
    return FakeBench(tool.parse_args(arguments(tmp_path)), clock)


def test_all_modes_singleton_stop_and_hot_reload(bench):
    report = {"phases": {}}
    tool.execute(bench, object(), report)
    assert len(report["phases"]) == 7
    assert all(phase["passed"] for phase in report["phases"].values())
    assert report["phases"]["singleton"]["repeat_result"]["finished"] == 1
    assert report["phases"]["stop_startup"]["stopped"]["accepted"] == 0
    assert bench.commands.count("TRIG:SEQ:NEXT") == 5
    assert bench.commands.index("CONF:SEQ:LINK OFF") < bench.commands.index("TRIG:START")


@pytest.mark.parametrize("fault,match", [
    ("omit_start_pulse", "startup_high"), ("count_start", "settling_low"),
    ("ignore_settle", "startup_high"), ("wrong_loopback", "cable"),
])
def test_missing_or_incorrect_start_is_failure(bench, fault, match):
    setattr(bench, fault, True)
    with pytest.raises(RuntimeError, match=match):
        tool.run_phase(bench, {}, "PULSE")


def test_stop_not_releasing_pads_fails(bench):
    bench.ignore_stop = True
    with pytest.raises(RuntimeError, match="timeout"):
        tool.run_phase(bench, {}, "PULSE", abort=True)


def test_split_gpio_read_retains_edge_sample(bench, monkeypatch):
    rows = iter([dict(inputs=0, outputs=13, owned=15),
                 dict(inputs=2, outputs=13, owned=15)])
    monkeypatch.setattr(tool, "read_io", lambda unused: next(rows))
    phase = {}
    tool.sample_level(bench, phase, "high", 5, True, 15)
    assert len(phase["high"]) == 2 and phase["high"][0]["inputs"] == 0


def test_split_gpio_read_cannot_pass_later_pulse(bench, monkeypatch):
    rows = iter([dict(inputs=0, outputs=13, owned=15)])
    monkeypatch.setattr(tool, "read_io", lambda unused: next(rows, dict(inputs=0, outputs=5, owned=15)))
    with pytest.raises(RuntimeError, match="cable"):
        tool.sample_level(bench, {}, "high", 5, True, 15)


def test_slow_matching_read_cannot_pass_confirmation_deadline(bench, monkeypatch):
    calls = []
    def read(unused):
        calls.append(True)
        if len(calls) > 1:
            bench.clock.sleep(.2)
        return dict(inputs=2 if len(calls) > 1 else 0, outputs=13, owned=15)
    monkeypatch.setattr(tool, "read_io", read)
    with pytest.raises(RuntimeError, match="confirmation exceeded"):
        tool.sample_level(bench, {}, "high", 5, True, 15)


def test_output_only_does_not_claim_input_loopback(bench):
    bench.args.output_only = True
    bench.wrong_loopback = True
    report = {"phases": {}}
    tool.execute(bench, object(), report)
    assert report["output_pad_sequence_verified"]
    assert not report["io_loopback_verified"]
    assert all(phase["passed"] for phase in report["phases"].values())


def test_output_only_still_requires_start_pulse(bench):
    bench.args.output_only = True
    bench.omit_start_pulse = True
    with pytest.raises(RuntimeError, match="startup_high"):
        tool.run_phase(bench, {}, "PULSE")


def test_singleton_keeps_eight_state_space(bench):
    phase = {}
    tool.run_phase(bench, phase, "PULSE", singleton=True)
    assert bench.state_count == 8
    assert bench.plan == (5,)
    assert bench.codes == {code: code for code in range(8)}


def test_fake_rejects_out_of_range_plan_and_code(bench):
    bench.write("CONF:TRIG 1,0,1,1")
    with pytest.raises(RuntimeError, match="STATE_RANGE"):
        bench.write("CONF:SEQ SP8T,5")
    with pytest.raises(RuntimeError, match="INVALID"):
        bench.write("CONF:SEQ:CODE 5,5")


@pytest.mark.parametrize("query,bad_reply", [
    ("READ:SEQ? SP8T", '"SP8T",5,2,7,0,1,3,4,6'),
    ("READ:SEQ:OUTPUT?", '7,8,"PULSE",300000,300000,1'),
    ("READ:SEQ:SOUR?", '"BUS","RISING"'),
    ("READ:SEQ:CODE? 5", "5,0"),
])
def test_malformed_or_wrong_configuration_readback_fails(bench, query, bad_reply):
    command = bench.command
    bench.command = lambda value: bad_reply if value == query else command(value)
    with pytest.raises(RuntimeError, match="readback mismatch"):
        tool.configure(bench, {}, "PULSE")


def test_rejects_counter_reset_and_wrong_code():
    good = {"error": "NONE", "faults": 0, "backend_fault": 0, "cancelled": 0,
            "accepted": 0, "completed": 0, "count": 8, "current_index": 0,
            "current_state": 5, "run_id": 1, "generation": 1}
    with pytest.raises(RuntimeError, match="position"):
        tool.check_result({**good, "current_state": 0}, tool.PLAN, 0)
    with pytest.raises(RuntimeError, match="advancement"):
        tool.check_result({**good, "accepted": 1, "completed": 1}, tool.PLAN, 0)
    with pytest.raises(RuntimeError, match="run changed"):
        tool.check_result({**good, "run_id": 2}, tool.PLAN, 0, good)


@pytest.mark.parametrize("extra", [["--settle-us", "0"], ["--pulse-us", "429496730"],
                                  ["--timeout", "nan"], ["--timeout", "1"]])
def test_invalid_timing_rejected(tmp_path, extra):
    with pytest.raises(SystemExit):
        tool.parse_args(arguments(tmp_path, *extra))


def test_existing_report_refused_before_device_access(tmp_path, monkeypatch):
    path = tmp_path / "evidence.json"
    path.write_text("prior failure", encoding="utf-8")
    monkeypatch.setattr(tool, "discover", lambda: pytest.fail("hardware access"))
    with pytest.raises(FileExistsError):
        tool.main(arguments(tmp_path))
    assert path.read_text(encoding="utf-8") == "prior failure"


def test_failure_and_cleanup_are_both_preserved(tmp_path, monkeypatch):
    monkeypatch.setattr(tool, "discover", lambda: {"serial": [], "visa": []})
    monkeypatch.setattr(tool, "open_serial_port", lambda *a, **k: nullcontext(object()))
    monkeypatch.setattr(tool.Bench, "identity", lambda self: None)
    def fail(bench, port, report):
        report["transcript"].append({"command": "TRIG:START", "response": "<timeout>"})
        raise RuntimeError("startup failed")
    monkeypatch.setattr(tool, "execute", fail)
    monkeypatch.setattr(tool, "cleanup", lambda b, p, r: r["cleanup_failures"].append("STOP failed"))
    assert tool.main(arguments(tmp_path)) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert "startup failed" in report["failure"]
    assert report["cleanup_failures"] == ["STOP failed"]
    assert not report["passed"] and not report["external_waveform_verified"]


def test_identity_failure_must_not_mutate_board(tmp_path, monkeypatch):
    monkeypatch.setattr(tool, "discover", lambda: {"serial": [], "visa": []})
    monkeypatch.setattr(tool, "open_serial_port", lambda *a, **k: nullcontext(object()))
    monkeypatch.setattr(tool.ring, "transport_query", lambda *a: (_ for _ in ()).throw(OSError("offline")))
    monkeypatch.setattr(tool, "cleanup", lambda *a: pytest.fail("no cleanup before identity"))
    assert tool.main(arguments(tmp_path)) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert "offline" in report["failure"]
    assert report["transport_transcript"][0]["command"] == "*IDN?"
