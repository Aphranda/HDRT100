"""Reject incomplete, stale and contradictory sequence acceptance evidence."""
import csv
import io
import json
from contextlib import contextmanager

import pytest

from tools.hardware_acceptance import sequence_trigger_acceptance as target


def sample(completed=1):
    row = dict.fromkeys(target.STATUS_FIELDS, 0)
    code = (completed - 1) % 8
    row.update(state="READY", error="NONE", run_id=5, generation=3, count=8,
               current_index=code, current_state=code, executed_index=code, executed_state=code,
               completed_index=code, completed_state=code, next_index=completed % 8,
               cycles=(completed - 1) // 8, accepted=completed, completed=completed,
               written_us=0, rise_us=0, completed_us=0)
    return row


def encoded(row):
    stream = io.StringIO()
    csv.writer(stream).writerow([row[key] for key in target.STATUS_FIELDS])
    return stream.getvalue().strip()


@pytest.mark.parametrize("count", [1, 8, 9, 16, 17])
def test_valid_sequence_wrap(count):
    row = sample(count)
    assert target.parse_status(encoded(row)) == row
    assert target.validate_sample(row, sample(), count - 1) == count
    target.validate_totals(row, count, 0, count)


@pytest.mark.parametrize("field,value", [
    ("run_id", 0), ("generation", 4), ("count", 2), ("state", "FAULT"),
    ("accepted", 3), ("completed_index", 1), ("completed_state", 1),
    ("current_index", 1), ("next_index", 2), ("cycles", 1),
    ("faults", 1), ("backend_fault", 1), ("cancelled", 1), ("error", "RESOURCE_BUSY"),
])
def test_inconsistent_state_is_not_accepted(field, value):
    row = sample()
    row[field] = value
    with pytest.raises(target.AcceptanceError):
        target.validate_sample(row, sample(), 0)


@pytest.mark.parametrize("previous,current", [(0, 2), (3, 1)])
def test_cannot_skip_or_regress_completions(previous, current):
    with pytest.raises(target.AcceptanceError):
        target.validate_sample(sample(current), sample(), previous)


@pytest.mark.parametrize("response", ["<timeout>", '"READY",1', '"READY', "", "," * 24])
def test_truncated_status_rejected(response):
    with pytest.raises(target.AcceptanceError):
        target.parse_status(response)


@pytest.mark.parametrize("value", ["-1", "1.2", "1e2", "4294967296", "1" * 100, "\u0661"])
def test_invalid_unsigned_status_rejected(value):
    row = sample()
    row["accepted"] = value
    with pytest.raises(target.AcceptanceError):
        target.parse_status(encoded(row))


def test_external_input_accounting():
    row = sample(9)
    row["busy_rejected"] = 3
    target.validate_totals(row, 9, 3, 12)
    for sent in (0, 9, 13):
        with pytest.raises(target.AcceptanceError):
            target.validate_totals(row, 9, 3, sent)
    row["notready_rejected"] = 1
    with pytest.raises(target.AcceptanceError):
        target.validate_totals(row, 9, 3, 12)


def cli(tmp_path, *extra):
    return ["--port", "TEST", "--serial-number", "UID", "--build", "BUILD",
            "--out", str(tmp_path / "evidence.json"), *extra]


@pytest.mark.parametrize("extra", [
    ["--source", "IN1"], ["--source", "IN2", "--input-events", "8"],
    ["--input-events", "9"], ["--steps", "8"], ["--busy", "1"],
    ["--duration", "nan"], ["--poll", "0"], ["--pulse-us", "0"],
    ["--timeout", "0.2"],
    ["--settle-us", str(target.TIME_MAX_US + 1)],
    ["--pulse-us", str(target.TIME_MAX_US + 1)],
])
def test_invalid_cli_fails_before_hardware(tmp_path, extra):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, *extra))


def test_pad_read_race_cannot_certify_wrong_step():
    args = type("Args", (), {"settle_us": 100, "pulse_us": 100})()
    report = {"steps": [], "timing": {"backend": "PIO0"}}
    bench = target.Bench(None, args, report)
    bench.command = lambda cmd: "0"
    bench.status = lambda: sample(2)
    with pytest.raises(target.AcceptanceError, match="state changed"):
        bench.sample_output(sample())
    assert report["steps"] == []


def test_next_query_reads_without_advance():
    bench = target.Bench(None, object(), {})
    commands = []
    def command(text):
        commands.append(text)
        return encoded(sample())
    bench.command = command
    assert bench.status() == sample()
    assert commands == ["READ:SEQ:NEXT?"]


def test_primary_failure_and_cleanup_failure_both_preserved(tmp_path, monkeypatch):
    @contextmanager
    def port(*args, **kwargs):
        yield object()

    class FailedBench:
        def __init__(self, *args):
            pass

        def identity(self):
            pass

        def configure(self):
            raise target.AcceptanceError("configuration failed")

        def stop(self):
            raise target.AcceptanceError("STOP failed")

    monkeypatch.setattr(target, "open_serial_port", port)
    monkeypatch.setattr(target, "Bench", FailedBench)
    assert target.main(cli(tmp_path)) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert report["passed"] is False
    assert report["failure"] == "configuration failed"
    assert report["cleanup_failure"] == "STOP failed"
    assert report["external_waveform_verified"] is False
    assert report["rf_path_verified"] is False
    assert report["completion_high_observed"] is False


def test_existing_evidence_cannot_be_overwritten(tmp_path, monkeypatch):
    path = tmp_path / "evidence.json"
    path.write_text("original failure", encoding="utf-8")
    monkeypatch.setattr(target, "open_serial_port", lambda *args, **kwargs: pytest.fail("opened hardware"))
    with pytest.raises(FileExistsError):
        target.main(cli(tmp_path))
    assert path.read_text(encoding="utf-8") == "original failure"


def test_external_mode_never_sends_software_step(tmp_path, monkeypatch):
    args = target.parse_args(cli(tmp_path, "--source", "IN3", "--input-events", "9", "--duration", "9"))
    report = {"steps": []}
    bench = target.Bench(None, args, report)
    writes = []
    bench.write = writes.append
    baseline = sample()
    baseline.update(accepted=0, completed=0, cycles=0, next_index=0)
    def wait_state(state):
        if state == "READY":
            return baseline
        row = sample(9)
        row["state"] = "PAUSED"
        return row
    bench.wait_state = wait_state
    rows = iter(sample(n) for n in range(1, 10))
    bench.status = lambda: next(rows)
    bench.sample_output = lambda row: report["steps"].append(row)
    bench.identity = lambda: None
    bench.command = lambda command: {
        "READ:SEQ:TIM?": "PIO0,100,0", "READ:SEQ:REJ?": "5,3,0,0,0",
    }.get(command, '0,"No error"')
    clock = iter([0, *range(10)])
    monkeypatch.setattr(target.time, "monotonic", lambda: next(clock))
    monkeypatch.setattr(target.time, "sleep", lambda duration: None)
    bench.execute()
    assert writes == ["TRIG:START", "TRIG:PAUS"]
    assert report["final_running_status"]["completed"] == 9


@pytest.mark.parametrize("response", ["NONE,0,0", "PIO0,100,1", "PIO0,0,0", "PIO0,-1,0",
                                     "PIO0,4294967296,0", "PIO0,100", "PIO2,100,0"])
def test_invalid_timing_metadata_rejected(response):
    with pytest.raises(target.AcceptanceError):
        target.parse_timing(response)


def test_pio_receipts_do_not_certify_physical_time():
    metadata = target.parse_timing('"PIO0",100,0')
    assert metadata == {"backend": "PIO0", "tick_ns": 100, "physical_timestamps_valid": False}
    report = {"steps": [], "timing": metadata}
    bench = target.Bench(None, object(), report)
    row = sample()
    row["written_us"] = 100
    with pytest.raises(target.AcceptanceError, match="physical timestamps"):
        bench.sample_output(row)
    assert report["steps"] == []


@pytest.mark.parametrize("response", ["5,3,0,0,1", "5,4,0,0,0", "5,3,1,0,0", "<timeout>"])
def test_pending_or_mismatched_rejections_cannot_certify_totals(response):
    report = {}
    bench = target.Bench(None, object(), report)
    bench.command = lambda command: response
    with pytest.raises(target.AcceptanceError, match="pending or changed"):
        bench.settled_rejections(sample())
    assert "rejection_counts_settled" not in report


def test_wrong_identity_never_stops_or_configures_board(tmp_path, monkeypatch):
    @contextmanager
    def port(*args, **kwargs):
        yield object()

    commands = []

    def send(serial_port, command, timeout):
        commands.append(command)
        return "NO.1,DHRT100,OTHER,0.1.0" if command == "*IDN?" else '"BUILD"'

    monkeypatch.setattr(target, "open_serial_port", port)
    monkeypatch.setattr(target, "send_command", send)
    assert target.main(cli(tmp_path)) == 1
    assert commands == ["*IDN?", "SYST:FW:BUILD?"]
