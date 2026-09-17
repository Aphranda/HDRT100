import json
from contextlib import contextmanager
from types import SimpleNamespace

import pytest

from tools.hardware_acceptance import sequence_position_validate as target
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError


def cli(tmp_path, *extra):
    return ["--port", "COM3", "--serial-number", "TEST_UID", "--build", "123",
            "--out", str(tmp_path / "evidence.json"), *extra]


def test_scheduler_evidence_preserves_overrun_without_claiming_realtime():
    values = [1, 250000000, 375000, 1, 0, 0, 100, 2,
              0, 217500, 212500, 0, 1000, 250000, 100, 0, 0, 2, 1]
    responses = iter(['0,"No error"', ",".join(map(str, values)), '0,"No error"'])
    bench = SimpleNamespace(command=lambda command: next(responses))
    report = {}
    target.capture_schedule(bench, report, "after")
    snapshot = report["scheduler"]["after"]
    assert snapshot["phases"][0]["overruns"] == 2
    assert snapshot["phases"][0]["max_runtime"] == 250000
    assert not snapshot["strict_realtime_verified"]


@pytest.mark.parametrize("raw", ['"UNAVAILABLE"', "1,250000000,375000,10,0,0,1,0",
                               "1,0,375000,0,0,0,1,0", "1,250000000,375000,0,0,0,4294967296,0"])
def test_scheduler_evidence_rejects_unavailable_or_partial_data(raw):
    responses = iter(['0,"No error"', raw, '0,"No error"'])
    report = {}
    with pytest.raises(AcceptanceError):
        target.capture_schedule(SimpleNamespace(command=lambda command: next(responses)), report, "before")
    assert report["scheduler"]["before"]["raw"] == raw


def wire(values, fields):
    return ",".join(str(values[k]) for k in fields)


def row(events=0, positions=0, phase=9, triggers=0, ready=0, completed=0, error=0):
    link = dict.fromkeys(target.LINK_FIELDS, 0)
    link.update(enabled=1, phase=phase, error=error, binding_epoch=1, model_epoch=2,
                run=11, generation=19, triggers=triggers, ready=ready, completed=completed, repeat=2)
    counter = dict.fromkeys(target.COUNTER_FIELDS, 0)
    counter.update(enabled=1, threshold=100, events=events, positions=positions, phase=phase,
                   error=error, fault_events=events if error else 0,
                   history_total=16 if phase == 8 else 0, history_retained=16 if phase == 8 else 0)
    return dict(link=link, counter=counter)


def history():
    return [dict(ordinal=i, run=11, generation=19, position=(i - 1) // 8 + 1,
                 sequence_index=(i - 1) % 8, threshold_pulses=((i - 1) // 8 + 1) * 100,
                 observed_pulses=((i - 1) // 8 + 1) * 100 + (i - 1) % 8, outcome_flags=7)
            for i in range(1, 17)]


def test_position_completion_does_not_mix_before_and_after_threshold():
    crossing = row(events=100, positions=1, phase=9)
    crossing["counter"]["phase"] = 10
    assert not target.position_finished_observed(crossing)
    completed = row(events=110, positions=1, phase=9, triggers=8, ready=8, completed=7)
    assert target.position_finished_observed(completed)


def profile_fixture(tmp_path, monkeypatch, rows):
    clock = [0.0]
    monkeypatch.setattr(target.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(target.time, "sleep", lambda amount: clock.__setitem__(0, clock[0] + amount))
    monkeypatch.setattr(target, "configure", lambda *args: None)
    observations = list(rows)
    monkeypatch.setattr(target, "sample", lambda bench: observations.pop(0) if len(observations) > 1 else observations[0])
    reads = [0]
    def ring_sample(*args):
        reads[0] += 1
        return {"tdma": [reads[0]] * 200}
    monkeypatch.setattr(target.ring, "sample", ring_sample)
    bench = SimpleNamespace(args=target.parse_args(cli(tmp_path, "--duration", "1")))
    bench.command = lambda command: "2,2,1" if command == "READ:SEQ:REPEAT?" else wire(history()[int(command.split()[1]) - 1], target.HISTORY_FIELDS)
    bench.status = lambda: dict(state="IDLE", run_id=11, generation=19, count=8,
        accepted=observations[-1]["link"]["completed"], completed=observations[-1]["link"]["completed"],
        error="NONE", faults=0, backend_fault=0)
    return bench, {}


def test_two_positions_full_history(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch,
        [row(4), row(211, 2, 8, 16, 16, 15)])
    target.run_profile(bench, object(), report)
    assert report["passed"] and report["no_premature_sample_observed"]
    assert len(report["history"]) == 16


def test_busy_boundary_withholds_ready(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch,
        [row(4), row(101, 1, 3, 1), row(201, 1, 7, 1, error=5)])
    target.run_profile(bench, object(), report, True)
    assert report["passed"] and report["expected_busy_fault"]["counter"]["fault_events"] == 201


def test_fault_between_separate_queries_is_resampled(tmp_path, monkeypatch):
    intermediate = row(201, 1, 3, 1)
    intermediate["counter"].update(phase=7, error=5, fault_events=201)
    bench, report = profile_fixture(tmp_path, monkeypatch,
        [row(4), intermediate, row(201, 1, 7, 1, error=5)])
    target.run_profile(bench, object(), report, True)
    assert report["passed"]


def test_done_does_not_hide_owner_finish_failure(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch, [row(4), row(211, 2, 8, 16, 16, 15)])
    bench.status = lambda: dict(state="FAULT", run_id=11, generation=19, count=8,
        accepted=15, completed=15, error="BACKEND_FAULT", faults=1, backend_fault=14)
    with pytest.raises(AcceptanceError, match="owner fault after LINK DONE"):
        target.run_profile(bench, object(), report)
    assert report["terminal_owner_samples"][-1]["backend_fault"] == 14 and not report["passed"]


def test_busy_driver_fault_is_attributed(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch,
        [row(4), row(101, 1, 3, 1), row(201, 1, 7, 1, error=5)])
    bench.status = lambda: dict(state="FAULT", run_id=11, generation=19, count=8,
        accepted=0, completed=0, error="BACKEND_FAULT", faults=1, backend_fault=14)
    target.run_profile(bench, object(), report, True)
    assert report["busy_fault_detection_layer"] == "Core1 counter driver"


@pytest.mark.parametrize("manual", [False, True])
def test_configure_uses_position_builder_and_readbacks(tmp_path, monkeypatch, manual):
    args = target.parse_args(cli(tmp_path))
    commands = []
    monkeypatch.setattr(target, "gui_batch", lambda b, r, stage, batch: commands.extend(batch))
    configured = row()
    configured["counter"].update(slot=1, input=1)
    configured["link"].update(phase=1, dutslot=2, vnaslot=3, input=0 if manual else 2, outputmask=8, repeat=0)
    monkeypatch.setattr(target, "sample", lambda b: configured)
    def command(text):
        if text == "SYST:TDMA:FLIGHT:MODE?": return "2"
        if text == "READ:SEQ:REPEAT?": return "2,0,0"
        if text == "READ:SEQ? SP8T": return '"SP8T",0,1,2,3,4,5,6,7'
        if text.startswith("READ:SEQ:CODE?"):
            code = text.split()[1]
            return f"{code},{code}"
        instance = int(text.split()[1])
        name = {2: "COUNTER", 5: "DUT", 7: "VNA"}[instance]
        return f'{instance},"{name}",1,1,152,11,5,152,11,5'
    target.configure(SimpleNamespace(args=args, command=command), {}, manual)
    expected = f"CONF:SEQ:LINK POSITION,1,2,3,IN1,100,{'MANUAL' if manual else 'IN2'},OUT4,1000,10000,RIS"
    assert expected in commands and "SYST:TDMA:RING:TRAIN 4096" in commands
    assert commands[-1] == "TRIG:START" and "TRIG:SEQ:NEXT" not in commands


@pytest.mark.parametrize("bad", [row(20, triggers=1), row(200, 1, 7, 1, error=4),
                                row(201, 2, 8, 15, 15, 14)])
def test_failure_keeps_offending_snapshot(tmp_path, monkeypatch, bad):
    bench, report = profile_fixture(tmp_path, monkeypatch, [row(4), bad])
    with pytest.raises(AcceptanceError):
        target.run_profile(bench, object(), report)
    assert report["samples"][-1] == bad and not report["passed"]


@pytest.mark.parametrize("field,value", [("position", 3), ("observed_pulses", 200),
    ("threshold_pulses", 99), ("outcome_flags", 3), ("run", 12), ("sequence_index", 7)])
def test_history_rejects_uncompleted_wrong_or_late_record(field, value):
    records = history()
    records[0][field] = value
    with pytest.raises(AcceptanceError):
        target.check_history(records, dict(run=11, generation=19), 100)


def transport_fixture(monkeypatch):
    @contextmanager
    def port(*args, **kwargs):
        yield object()
    monkeypatch.setattr(target, "discover", lambda: {})
    monkeypatch.setattr(target, "select_transport", lambda *a: None)
    monkeypatch.setattr(target, "open_serial_port", port)
    monkeypatch.setattr(target.ring, "EvidencePort", lambda *a: object())


def test_identity_failure_has_no_mutation_or_cleanup(tmp_path, monkeypatch):
    transport_fixture(monkeypatch)
    monkeypatch.setattr(target.Bench, "identity", lambda self: (_ for _ in ()).throw(AcceptanceError("unexpected build")))
    monkeypatch.setattr(target, "run_profile", lambda *a: pytest.fail("mutation before identity"))
    monkeypatch.setattr(target, "cleanup", lambda *a: pytest.fail("cleanup before identity"))
    assert target.main(cli(tmp_path)) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert "unexpected build" in report["failure"]


@pytest.mark.parametrize("failure", [RuntimeError("disconnect"), KeyboardInterrupt()])
def test_failed_profile_and_cleanup_are_preserved(tmp_path, monkeypatch, failure):
    transport_fixture(monkeypatch)
    monkeypatch.setattr(target.Bench, "identity", lambda self: None)
    def profile(bench, port, report, manual):
        report["offending"] = "kept"
        raise failure
    monkeypatch.setattr(target, "run_profile", profile)
    monkeypatch.setattr(target, "cleanup", lambda b, p, r: r["cleanup_failures"].append("stop failed"))
    assert target.main(cli(tmp_path)) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert report["two_positions"]["offending"] == "kept"
    assert type(failure).__name__ in report["failure"] and report["cleanup_failures"] == ["stop failed"]


def test_cleanup_failure_cannot_pass(tmp_path, monkeypatch):
    transport_fixture(monkeypatch)
    monkeypatch.setattr(target.Bench, "identity", lambda self: None)
    monkeypatch.setattr(target, "run_profile", lambda b, p, r, m: r.update(passed=True))
    monkeypatch.setattr(target, "cleanup", lambda b, p, r: r["cleanup_failures"].append("outputs high"))
    assert target.main(cli(tmp_path)) == 1


def test_report_cannot_overwrite_existing_evidence(tmp_path):
    path = tmp_path / "evidence.json"
    path.write_text("old evidence", encoding="utf-8")
    with pytest.raises(FileExistsError):
        target.main(cli(tmp_path))
    assert path.read_text(encoding="utf-8") == "old evidence"


@pytest.mark.parametrize("option,value", [("--threshold", "0"), ("--source-hz", "nan"),
                                        ("--duration", "0"), ("--threshold", str(0xffffffff))])
def test_invalid_settings_fail_before_hardware(tmp_path, option, value):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, option, value))


@pytest.mark.parametrize("fault", [None, "resume_reset", "duplicate_sample", "restart_old_run", "stop_owned", "wrong_state"])
def test_continuous_pause_restart_accounting(tmp_path, monkeypatch, fault):
    args = target.parse_args(cli(tmp_path, "--lifecycle"))
    commands = []
    monkeypatch.setattr(target, "configure", lambda *a, **kw: commands.append(kw["repeat"]))
    def observation(bench, report, name, predicate, **kwargs):
        events = {"waiting": 1, "paused": 2, "counting_while_paused": 5,
                  "resumed": 6, "position_complete": 110, "restarted": 1}[name]
        e = row(events=events, positions=int(name == "position_complete"),
                phase=6 if name in ("paused", "counting_while_paused") else 9)
        e["owner"] = dict(state="PAUSED" if e["link"]["phase"] == 6 else "READY", accepted=0, completed=0)
        e["link"]["repeat"] = 0
        if name == "position_complete":
            e["link"].update(triggers=8, ready=8)
            e["owner"].update(accepted=7, completed=7)
            e["owner"].update(current_index=7, current_state=7, completed_index=7, completed_state=7)
            if fault == "wrong_state": e["owner"]["completed_state"] = 3
            if fault == "duplicate_sample": e["link"]["triggers"] = 9
        if name == "resumed" and fault == "resume_reset": e["counter"]["events"] = 0
        if name == "restarted" and fault != "restart_old_run": e["link"]["run"] += 1
        assert predicate(e)
        return e
    monkeypatch.setattr(target, "lifecycle_observation", observation)
    monkeypatch.setattr(target, "read_io", lambda unused: dict(outputs=0, owned=int(fault == "stop_owned"), armed=0, busy=0))
    bench = SimpleNamespace(args=args, write=commands.append,
        command=lambda command: wire(history()[int(command.split()[1]) - 1], target.HISTORY_FIELDS),
        wait_state=lambda unused: dict(error="NONE", faults=0, backend_fault=0))
    report = {}
    if fault:
        with pytest.raises(AcceptanceError): target.run_lifecycle(bench, object(), report)
        assert not report["passed"]
    else:
        target.run_lifecycle(bench, object(), report)
        assert report["passed"] and commands == [0, "TRIG:PAUS", "TRIG:CONT", "TRIG:STOP", "TRIG:START", "TRIG:STOP"]


def test_lifecycle_rejects_consistent_but_changed_run(tmp_path, monkeypatch):
    e = row()
    monkeypatch.setattr(target, "sample", lambda unused: e)
    bench = SimpleNamespace(args=target.parse_args(cli(tmp_path)),
        status=lambda: dict(state="READY", run_id=e["link"]["run"], generation=19, error="NONE", faults=0, backend_fault=0))
    report = {}
    target.lifecycle_observation(bench, report, "first", lambda unused: True)
    e["link"]["run"] += 1
    with pytest.raises(AcceptanceError, match="lifecycle identity changed"):
        target.lifecycle_observation(bench, report, "resumed", lambda unused: True)


@pytest.mark.parametrize("never_starts", [False, True])
def test_restart_waits_for_async_new_run(tmp_path, monkeypatch, never_starts):
    e = row()
    clock = [0.0]
    monkeypatch.setattr(target.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(target.time, "sleep", lambda dt: clock.__setitem__(0, clock[0] + dt))
    def sample(unused):
        e["link"]["run"] = 11 if never_starts or clock[0] == 0 else 12
        return e
    monkeypatch.setattr(target, "sample", sample)
    bench = SimpleNamespace(args=target.parse_args(cli(tmp_path, "--duration", "1")),
        status=lambda: dict(state="IDLE" if e["link"]["run"] == 11 else "READY",
            run_id=e["link"]["run"], generation=19, error="NONE", faults=0, backend_fault=0))
    report = {"lifecycle_identity": {key: e["link"][key] for key in target.IDENTITY_FIELDS}}
    if never_starts:
        with pytest.raises(AcceptanceError, match="start acknowledgement timed out"):
            target.lifecycle_observation(bench, report, "restart", lambda unused: True, restarting=True)
    else:
        result = target.lifecycle_observation(bench, report, "restart", lambda unused: True, restarting=True)
        assert result["link"]["run"] == 12 and len(report["restart"]) == 2
