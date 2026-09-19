import json
from contextlib import contextmanager
from types import SimpleNamespace

import pytest

from tools.hardware_acceptance import sequence_position_validate as target
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError


def cli(tmp_path, *extra):
    return ["--port", "COM3", "--serial-number", "TEST_UID", "--build", "123",
            "--out", str(tmp_path / "evidence.json"), *extra]


def angle_bench(overrides=None):
    replies = {
        "READ:ANGLE:SWEEP?": "0,1,1,1,2,1",
        "READ:ANGLE:INPUT?": '"IN1",50,50,50,1,1,1',
        "READ:ANGLE:SPEED?": "1",
        "READ:ANGLE:POSITION?": '"POSITION",0,2,0,0,0,0,0,1,0,1',
    }
    replies.update(overrides or {})
    return SimpleNamespace(args=SimpleNamespace(threshold=50, source_hz=50),
                           command=lambda cmd: replies[cmd])


def test_angle_readback_and_terminal_are_real():
    assert target.verify_angle_configuration(angle_bench(), 1)["SWEEP"][-2:] == ["2", "1"]
    bench = angle_bench({"READ:ANGLE:POSITION?": '"POSITION",2,2,1,0,104,4,1,0,0,1'})
    assert target.verify_angle_terminal(bench)[1] == "2"


@pytest.mark.parametrize("command,response", [
    ("READ:ANGLE:SWEEP?", "0,1,1,1,2,0"),
    ("READ:ANGLE:SWEEP?", "0,1,1,2,2,1"),
    ("READ:ANGLE:INPUT?", '"IN1",50,50,100,1,1,1'),
    ("READ:ANGLE:INPUT?", '"IN2",50,50,50,1,1,1'),
    ("READ:ANGLE:POSITION?", '"POSITION",0,2,0,0,0,0,1,1,0,1'),
])
def test_angle_readback_rejects_stale_or_wrong_configuration(command, response):
    with pytest.raises(AcceptanceError):
        target.verify_angle_configuration(angle_bench({command: response}), 1)


def test_angle_validation_requires_external_profile(tmp_path):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, "--angle-scan"))
    assert target.parse_args(cli(tmp_path, "--angle-scan", "--external-input")).angle_scan


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


def completed_profile_rows():
    rows = [row(), row(99)]
    for ordinal in range(1, 9):
        rows.append(row(100, 1, 3, ordinal, ordinal - 1, max(0, ordinal - 2)))
    rows.append(row(100, 1, 9, 8, 8, 7))
    for ordinal in range(9, 17):
        rows.append(row(200, 2, 3, ordinal, ordinal - 1, ordinal - 2))
    rows.append(row(200, 2, 8, 16, 16, 15))
    return rows


def history():
    records = []
    for i in range(1, 17):
        position, index = (i - 1) // 8 + 1, (i - 1) % 8
        admitted = position * 1000
        elapsed = (index + 1) * 20
        records.append(dict(ordinal=i, run=11, generation=19, binding_epoch=1,
            exchange_id=i, position=position, sequence_index=index, sequence_state=index,
            output_code=index, threshold_pulses=position * 100,
            observed_pulses=position * 100 + index, trigger_ordinal=i, ready_ordinal=i,
            position_admitted_tick_ms=admitted, sample_done_tick_ms=admitted + elapsed,
            cycle_elapsed_ms=elapsed, outcome_flags=7))
    return records


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
    configured_modes = []
    monkeypatch.setattr(target, "configure",
                        lambda bench, report, manual_ready=False: configured_modes.append(manual_ready))
    observations = list(rows)
    monkeypatch.setattr(target, "sample", lambda bench: observations.pop(0) if len(observations) > 1 else observations[0])
    reads = [0]
    def ring_sample(*args):
        reads[0] += 1
        return {"tdma": [reads[0]] * 200}
    monkeypatch.setattr(target.ring, "sample", ring_sample)
    bench = SimpleNamespace(args=target.parse_args(cli(tmp_path, "--duration", "1")))
    def command(text):
        if text == "READ:SEQ:REPEAT?":
            return "2,2,1"
        if text.startswith("TRIG:SEQ:"):
            return "1"
        return wire(history()[int(text.split()[1]) - 1], target.HISTORY_FIELDS)
    bench.command = command
    bench.status = lambda: dict(state="IDLE", run_id=11, generation=19, count=8,
        accepted=observations[-1]["link"]["completed"], completed=observations[-1]["link"]["completed"],
        error="NONE", faults=0, backend_fault=0)
    return bench, {"configured_modes": configured_modes}


def test_two_positions_full_history(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch, completed_profile_rows())
    target.run_profile(bench, object(), report)
    assert report["passed"] and report["no_premature_sample_observed"]
    assert report["configured_modes"] == [False]
    assert len(report["history"]) == 16
    assert report["input_simulation"]["physical_ready_verified"] is True
    assert "ready_injection" not in report


def test_external_positions_never_inject_edges(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch, completed_profile_rows())
    monkeypatch.setattr(target, "configure", lambda *args, **kwargs: None)
    original = bench.command
    commands = []
    def query(command):
        commands.append(command)
        assert "INJECT" not in command
        return original(command)
    bench.command = query
    target.run_external_profile(bench, object(), report)
    assert report["passed"] and len(report["history"]) == 16
    assert report["input_simulation"]["method"] == "physical IN1"
    assert all("?" in command for command in commands)


def test_diagnostic_stress_requires_external_profile(tmp_path):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, "--diagnostic-stress"))
    assert target.parse_args(cli(tmp_path, "--external-input", "--diagnostic-stress")).diagnostic_stress


def test_external_diagnostic_stress_queries_after_every_poll(tmp_path, monkeypatch):
    rows = completed_profile_rows()
    rows.insert(0, row(phase=1))
    bench, report = profile_fixture(tmp_path, monkeypatch, rows)
    bench.args.diagnostic_stress = True
    monkeypatch.setattr(target, "configure", lambda *args, **kwargs: None)
    query = bench.command
    errors = []
    port = object()
    snapshots = []
    def ring_sample(actual_port, timeout):
        assert actual_port is port and timeout == bench.args.timeout
        snapshot = {"tdma": [len(snapshots) + 1] * 200,
                    "raw_tdma": f"tdma-{len(snapshots)}", "raw_phys": "physical-raw"}
        snapshots.append(snapshot)
        return snapshot
    def command(text):
        if text == "SYST:ERR?":
            errors.append(text)
            return '0,"No error"'
        return query(text)
    bench.command = command
    monkeypatch.setattr(target.ring, "sample", ring_sample)
    target.run_external_profile(bench, port, report)
    diagnostics = report["diagnostic_stress"]
    assert report["passed"] and len(diagnostics) == len(rows)
    assert [record["poll_ordinal"] for record in diagnostics] == list(range(1, len(rows) + 1))
    assert [record["snapshot"] for record in diagnostics] == snapshots[1:-1]
    assert len(errors) == 2 * len(rows)
    assert all(record["error_before"] == record["error_after"] == '0,"No error"'
               for record in diagnostics)


@pytest.mark.parametrize("failure_at", ["before", "sample", "after"])
def test_diagnostic_stress_failure_is_retained_without_retry(tmp_path, monkeypatch, failure_at):
    bench, report = profile_fixture(tmp_path, monkeypatch, completed_profile_rows())
    bench.args.diagnostic_stress = True
    monkeypatch.setattr(target, "configure", lambda *args, **kwargs: None)
    error_calls = []
    snapshots = []
    def command(text):
        assert text == "SYST:ERR?"
        error_calls.append(text)
        if (failure_at == "before" and len(error_calls) == 1 or
                failure_at == "after" and len(error_calls) == 2):
            return '-200,"Execution error"'
        return '0,"No error"'
    def ring_sample(*args):
        snapshots.append({"raw_tdma": "tdma", "raw_phys": "phys"})
        if failure_at == "sample" and len(snapshots) == 2:
            raise TimeoutError("diagnostic query timed out")
        return snapshots[-1]
    bench.command = command
    monkeypatch.setattr(target.ring, "sample", ring_sample)
    with pytest.raises((AcceptanceError, TimeoutError)):
        target.run_external_profile(bench, object(), report)
    assert not report["passed"] and len(report["samples"]) == 1
    assert len(report["diagnostic_stress"]) == 1
    record = report["diagnostic_stress"][0]
    assert "failure" in record
    assert len(snapshots) == (1 if failure_at == "before" else 2)
    assert len(error_calls) == (2 if failure_at == "after" else 1)
    if failure_at == "after":
        assert record["snapshot"] == snapshots[-1]
        assert record["error_after"] == '-200,"Execution error"'


def test_diagnostic_stress_busy_reply_keeps_raw_transport_evidence(monkeypatch):
    transcript = []
    port = target.ring.EvidencePort(object(), transcript)
    def exchange(physical, command, timeout):
        return '0,"No error"' if command == "SYST:ERR?" else '"BUSY"'
    monkeypatch.setattr(target.ring, "transport_query", exchange)
    bench = SimpleNamespace(args=SimpleNamespace(timeout=1),
        command=lambda command: target.ring.query(port, command, 1))
    report = {"samples": [row()]}
    with pytest.raises(target.ring.SnapshotBusy, match="snapshot is BUSY"):
        target.capture_diagnostic_stress(bench, port, report)
    assert [entry["command"] for entry in transcript] == [
        "SYST:ERR?", "SYSTem:REFMEM:SYNC:TDMA:STATus?"]
    assert transcript[-1]["response"] == '"BUSY"'
    assert "failure" in report["diagnostic_stress"][0]


def test_external_positions_require_real_progress(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch, [row()])
    monkeypatch.setattr(target, "configure", lambda *args, **kwargs: None)
    with pytest.raises(AcceptanceError, match="before timeout"):
        target.run_external_profile(bench, object(), report)
    assert not report["passed"]


def test_external_profile_rejects_injected_lifecycle(tmp_path):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, "--external-input", "--lifecycle"))


def test_external_positions_reject_extra_history(tmp_path, monkeypatch):
    rows = completed_profile_rows()
    rows[-1]["counter"]["history_total"] = 17
    bench, report = profile_fixture(tmp_path, monkeypatch, rows)
    monkeypatch.setattr(target, "configure", lambda *args, **kwargs: None)
    with pytest.raises(AcceptanceError, match="history count|history overwritten"):
        target.run_external_profile(bench, object(), report)


def test_external_sixty_positions_stream_history_without_loss(tmp_path, monkeypatch):
    records = []
    template = history()
    rows = [row()]
    for position in range(1, 61):
        for index in range(8):
            item = dict(template[index])
            ordinal = len(records) + 1
            item.update(ordinal=ordinal, position=position, exchange_id=ordinal,
                        threshold_pulses=position * 100, observed_pulses=position * 100 + index,
                        trigger_ordinal=ordinal, ready_ordinal=ordinal,
                        position_admitted_tick_ms=position * 1000,
                        sample_done_tick_ms=position * 1000 + item["cycle_elapsed_ms"])
            records.append(item)
        entry = row(position * 100 + 8, position, 8 if position == 60 else 9,
                    position * 8, position * 8, position * 8 - 1)
        entry["link"]["repeat"] = 60
        entry["counter"].update(history_total=position * 8,
                                history_retained=min(position * 8, target.HISTORY_CAPACITY))
        rows.append(entry)
    bench, report = profile_fixture(tmp_path, monkeypatch, rows)
    bench.args.positions = 60
    bench.args.duration = 2
    monkeypatch.setattr(target, "configure", lambda *args, **kwargs: None)
    def query(command):
        if command == "READ:SEQ:REPEAT?":
            return "60,60,1"
        return wire(records[int(command.split()[1]) - 1], target.HISTORY_FIELDS)
    bench.command = query
    target.run_external_profile(bench, object(), report)
    assert report["passed"] and len(report["history"]) == 480
    assert len(report["position_cycles"]) == 60


def test_history_overwrite_is_not_silently_accepted():
    with pytest.raises(AcceptanceError, match="overwritten"):
        target.collect_completed_history(SimpleNamespace(), [], {"ready": target.HISTORY_CAPACITY + 1},
            {"history_total": target.HISTORY_CAPACITY + 1, "history_retained": target.HISTORY_CAPACITY})


def test_history_collection_waits_for_completion():
    entry = dict(history()[0], outcome_flags=3)
    bench = SimpleNamespace(command=lambda _: wire(entry, target.HISTORY_FIELDS))
    records = []
    target.collect_completed_history(bench, records, row()["link"] | {"ready": 1},
                                     {"history_total": 1, "history_retained": 1})
    assert records == []


def timing_snapshot(record):
    identity = {k: record[k] for k in target.TIMING_FIELDS[2:9]}
    end = record["cycle_elapsed_ms"] * 250000
    return dict(identity, version=1, clock_hz=250000000, flags=63,
                request_ticks=0, applied_ticks=1, offered_ticks=2,
                returned_ticks=3, fire_queued_ticks=4, done_ticks=end)


def quiet_fixture(tmp_path, monkeypatch, fault=None, positions=2):
    args = target.parse_args(cli(tmp_path, "--external-input", "--quiet-capture", "--duration", "100",
                                "--positions", str(positions)))
    clock, started, sleeps = [0.0], [False], []
    transcript = []
    port = target.ring.EvidencePort(object(), transcript)
    root_report = {"transcript": []}
    bench = target.Bench(object(), args, root_report,
                         lambda command, timeout: target.ring.query(port, command, timeout))
    measurements = positions * 8
    terminal = row(positions * 100 + 10, positions, 8, measurements, measurements, measurements - 1)
    terminal["link"]["repeat"] = positions
    terminal["counter"].update(history_total=measurements, history_retained=measurements)
    if fault == "terminal":
        terminal["link"]["phase"] = 7
        terminal["link"]["error"] = 5
    records = []
    for ordinal in range(1, measurements + 1):
        position, index = (ordinal - 1) // 8 + 1, (ordinal - 1) % 8
        record = dict(history()[index], ordinal=ordinal, position=position, exchange_id=ordinal,
            threshold_pulses=position * 100, observed_pulses=position * 100 + index,
            trigger_ordinal=ordinal, ready_ordinal=ordinal,
            position_admitted_tick_ms=position * 1000,
            sample_done_tick_ms=position * 1000 + (index + 1) * 20)
        records.append(record)
    if fault == "early_history":
        records[0]["observed_pulses"] = 99
    def exchange(physical, command, timeout):
        if command == "TRIG:START":
            assert not started[0]
            started[0] = True
            return "1"
        if command == "READ:SEQ:HIST:STAT?":
            if fault == "busy_capability" and not started[0]:
                return '"BUSY"'
            total = measurements if started[0] else 0
            if fault == "lost_history" and started[0]:
                total = 257
            vector = dict(version=1, clock_hz=250000000,
                capacity=8 if fault == "capacity" else target.HISTORY_CAPACITY,
                run=11 if started[0] or fault == "stale_run" else 10,
                generation=19, binding_epoch=1, threshold=100,
                total=total, retained=min(total, target.HISTORY_CAPACITY),
                overwritten=max(0, total - target.HISTORY_CAPACITY))
            return wire(vector, target.HISTORY_STATUS_FIELDS)
        if command == "READ:SEQ:LINK?":
            return wire(terminal["link"], target.LINK_FIELDS)
        if command == "READ:SEQ:COUNTER?":
            return wire(terminal["counter"], target.COUNTER_FIELDS)
        if command == "READ:SEQ:REPEAT?":
            return f"{positions},{positions},1"
        if command == "READ:SEQ:TIM?":
            return '"PIO0",4,0'
        if command.startswith("READ:SEQ:HIST:TIM?"):
            return wire(timing_snapshot(records[int(command.split()[1]) - 1]), target.TIMING_FIELDS)
        if command.startswith("READ:SEQ:HIST?"):
            return wire(records[int(command.split()[1]) - 1], target.HISTORY_FIELDS)
        if command == "SYST:ERR?":
            return '0,"No error"'
        return "1"
    def sleep(seconds):
        assert 0 < seconds <= 1
        assert transcript[-1]["command"] == "TRIG:START"
        sleeps.append(seconds)
        clock[0] += seconds
    def configure(bench, report, repeat, start):
        assert repeat == positions and start is False
    def ring_sample(port, timeout):
        target.ring.query(port, "TEST:RING?", timeout)
        return {"tdma": [2 if started[0] else 1] * 200}
    def status():
        bench.command("TEST:OWNER?")
        return dict(state="READY" if fault == "owner" else "IDLE", run_id=11,
            generation=19, count=8, accepted=measurements - 1, completed=measurements - 1,
            error="NONE", faults=0, backend_fault=0)
    bench.status = status
    monkeypatch.setattr(target.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(target.time, "sleep", sleep)
    monkeypatch.setattr(target.ring, "transport_query", exchange)
    monkeypatch.setattr(target.ring, "sample", ring_sample)
    monkeypatch.setattr(target, "configure", configure)
    monkeypatch.setattr(target, "gui_batch", lambda b, r, stage, commands: [b.command(c) for c in commands])
    return bench, port, {}, sleeps


def test_quiet_capture_is_silent_until_natural_terminal_then_drains_vector(tmp_path, monkeypatch):
    bench, port, report, sleeps = quiet_fixture(tmp_path, monkeypatch)
    target.run_external_profile(bench, port, report)
    commands = [entry["command"] for entry in port.transcript]
    assert commands.count("TRIG:START") == 1
    assert commands[commands.index("TRIG:START") + 1:][:2] == ["READ:SEQ:LINK?", "READ:SEQ:COUNTER?"]
    assert all("INJECT" not in command and command != "TRIG:STOP" for command in commands)
    assert commands.index("TEST:OWNER?") < commands.index("READ:SEQ:HIST? 1")
    assert len(report["history"]) == 16 and all("timing" in record for record in report["history"])
    silence = report["quiet_capture"]
    assert sum(sleeps) == pytest.approx(5.2)
    assert silence["elapsed_s"] >= silence["planned_wait_s"]
    assert silence["commands_during_wait"] == 0
    assert silence["first_query_ordinal"] == silence["start_command_ordinal"] + 1
    assert report["passed"] and report["probe_mode"] == "post_run_vector"
    assert report["history_thresholds_verified"]
    assert not report["no_premature_sample_observed"]
    assert not report["observation_limits"]["live_prethreshold_observation"]


def test_quiet_capture_thirty_positions_drains_all_240_records(tmp_path, monkeypatch):
    bench, port, report, sleeps = quiet_fixture(tmp_path, monkeypatch, positions=30)
    target.run_external_profile(bench, port, report)
    assert report["passed"] and len(report["history"]) == 240
    assert len(report["position_cycles"]) == 30
    assert report["history_vector_after"]["overwritten"] == 0
    commands = [entry["command"] for entry in port.transcript]
    assert commands.count("READ:SEQ:HIST? 240") == commands.count("READ:SEQ:HIST:TIM? 240") == 1


def test_quiet_capture_rejects_command_inserted_during_wait(tmp_path, monkeypatch):
    bench, port, report, sleeps = quiet_fixture(tmp_path, monkeypatch)
    original_sleep = target.time.sleep
    def sleep(seconds):
        original_sleep(seconds)
        if sum(sleeps) >= target.quiet_wait_seconds(bench.args):
            bench.command("SYST:ERR?")
    monkeypatch.setattr(target.time, "sleep", sleep)
    with pytest.raises(AcceptanceError, match="command issued during quiet capture"):
        target.run_external_profile(bench, port, report)
    assert report["quiet_capture"]["commands_during_wait"] == 1
    assert not report["passed"] and not report["samples"]


@pytest.mark.parametrize("fault", ["capacity", "busy_capability"])
def test_quiet_capacity_failure_precedes_start(tmp_path, monkeypatch, fault):
    bench, port, report, sleeps = quiet_fixture(tmp_path, monkeypatch, fault)
    with pytest.raises(AcceptanceError):
        target.run_external_profile(bench, port, report)
    assert "TRIG:START" not in [entry["command"] for entry in port.transcript]
    assert not sleeps and not report["passed"]


@pytest.mark.parametrize("fault", ["terminal", "owner", "lost_history", "early_history", "stale_run"])
def test_quiet_capture_rejects_fault_incomplete_owner_or_history(tmp_path, monkeypatch, fault):
    bench, port, report, sleeps = quiet_fixture(tmp_path, monkeypatch, fault)
    with pytest.raises(AcceptanceError):
        target.run_external_profile(bench, port, report)
    assert sleeps and not report["passed"]
    assert report["samples"]
    assert "TRIG:STOP" not in [entry["command"] for entry in port.transcript]


@pytest.mark.parametrize("extra", [(), ("--external-input", "--diagnostic-stress"),
    ("--external-input", "--duration", "1")])
def test_quiet_capture_cli_rejects_unsupported_mode_or_short_duration(tmp_path, extra):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, "--quiet-capture", *extra))


@pytest.mark.parametrize("change", [None, "identity", "incomplete", "backward", "overflow", "duration"])
def test_timing_collection_validates_full_identity_and_raw_boundaries(change):
    record = history()[0]
    timing = timing_snapshot(record)
    if change == "identity": timing["exchange_id"] += 1
    elif change == "incomplete": timing["flags"] = 31
    elif change == "backward": timing["offered_ticks"] = 0
    elif change == "overflow": timing["done_ticks"] = 1 << 64
    elif change == "duration": timing["done_ticks"] += 250000
    bench = SimpleNamespace(command=lambda _: wire(timing, target.TIMING_FIELDS))
    if change:
        with pytest.raises(AcceptanceError): target.read_timing(bench, record)
    else:
        assert target.read_timing(bench, record) == timing


def test_completed_record_attaches_timing_only_when_requested():
    record = history()[0]
    timing = timing_snapshot(record)
    def query(command):
        return wire(timing, target.TIMING_FIELDS) if "TIM?" in command else wire(record, target.HISTORY_FIELDS)
    bench = SimpleNamespace(command=query, args=SimpleNamespace(timing_evidence=True))
    records = []
    target.collect_completed_history(bench, records, row()["link"] | {"ready": 1},
                                     {"history_total": 1, "history_retained": 1})
    assert records[0]["timing"] == timing


@pytest.mark.parametrize("extra", [
    ("--positions", "60"),
    ("--external-input", "--positions", "0"),
    ("--external-input", "--positions", "536870912"),
    ("--external-input", "--positions", "60", "--threshold", "100000000"),
])
def test_position_cli_rejects_unsupported_or_overflowing_runs(tmp_path, extra):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, *extra))


def test_external_positions_require_physical_tdma_growth(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch, completed_profile_rows())
    monkeypatch.setattr(target, "configure", lambda *args, **kwargs: None)
    monkeypatch.setattr(target.ring, "sample", lambda *args: {"tdma": [1] * 200})
    with pytest.raises(AcceptanceError, match="TDMA counter growth"):
        target.run_external_profile(bench, object(), report)


def test_busy_boundary_withholds_ready(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch,
        [row(), row(99), row(100, 1, 3, 1), row(200, 1, 7, 1, error=5)])
    target.run_profile(bench, object(), report, True)
    assert report["passed"] and report["expected_busy_fault"]["counter"]["fault_events"] == 200
    assert report["configured_modes"] == [True]


def test_fault_between_separate_queries_is_resampled(tmp_path, monkeypatch):
    intermediate = row(200, 1, 3, 1)
    intermediate["counter"].update(phase=7, error=5, fault_events=200)
    bench, report = profile_fixture(tmp_path, monkeypatch,
        [row(), row(99), row(100, 1, 3, 1), intermediate, row(200, 1, 7, 1, error=5)])
    target.run_profile(bench, object(), report, True)
    assert report["passed"]


def test_done_does_not_hide_owner_finish_failure(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch, completed_profile_rows())
    bench.status = lambda: dict(state="FAULT", run_id=11, generation=19, count=8,
        accepted=15, completed=15, error="BACKEND_FAULT", faults=1, backend_fault=14)
    with pytest.raises(AcceptanceError, match="owner fault after LINK DONE"):
        target.run_profile(bench, object(), report)
    assert report["terminal_owner_samples"][-1]["backend_fault"] == 14 and not report["passed"]


def test_busy_driver_fault_is_attributed(tmp_path, monkeypatch):
    bench, report = profile_fixture(tmp_path, monkeypatch,
        [row(), row(99), row(100, 1, 3, 1), row(200, 1, 7, 1, error=5)])
    bench.status = lambda: dict(state="FAULT", run_id=11, generation=19, count=8,
        accepted=0, completed=0, error="BACKEND_FAULT", faults=1, backend_fault=14)
    target.run_profile(bench, object(), report, True)
    assert report["busy_fault_detection_layer"] == "Core1 counter driver"


@pytest.mark.parametrize("manual", [False, True])
@pytest.mark.parametrize("start", [False, True])
@pytest.mark.parametrize("counter_input", ["IN1", "IN3", "IN4"])
def test_configure_uses_position_builder_and_readbacks(tmp_path, monkeypatch, manual, start, counter_input):
    args = target.parse_args(cli(tmp_path, "--counter-input", counter_input))
    commands = []
    monkeypatch.setattr(target, "gui_batch", lambda b, r, stage, batch: commands.extend(batch))
    configured = row()
    configured["counter"].update(slot=1, input=int(counter_input[-1]))
    configured["link"].update(phase=1, dutslot=2, vnaslot=3,
                              input=0 if manual else 2, outputmask=8, repeat=0)
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
    target.configure(SimpleNamespace(args=args, command=command), {}, manual, start=start)
    ready = "MANUAL" if manual else "IN2"
    expected = f"CONF:SEQ:LINK POSITION,1,2,3,{counter_input},100,{ready},OUT4,1000,10000,RIS"
    assert expected in commands and "TRIG:SEQ:NEXT" not in commands
    if start:
        assert "SYST:TDMA:RING:TRAIN 4096" in commands and commands[-1] == "TRIG:START"
    else:
        assert "TRIG:START" not in commands and "SYST:TDMA:RING:TRAIN 4096" not in commands


def test_counter_injection_uses_selected_unused_input(tmp_path):
    args = target.parse_args(cli(tmp_path, "--counter-input", "IN3"))
    commands = []
    bench = SimpleNamespace(args=args, command=lambda cmd: commands.append(cmd) or "1")
    report = {}
    target.inject_counter(bench, report, 999, "prethreshold")
    assert commands == ["TRIG:SEQ:INJECT IN3,999"]
    assert report["injections"][0]["command"] == commands[0]


def test_external_profile_cannot_claim_in1_with_another_input(tmp_path):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, "--counter-input", "IN3", "--external-input"))


@pytest.mark.parametrize("bad", [row(20, triggers=1), row(200, 1, 7, 1, error=4),
                                row(201, 2, 8, 15, 15, 14)])
def test_failure_keeps_offending_snapshot(tmp_path, monkeypatch, bad):
    bench, report = profile_fixture(tmp_path, monkeypatch, [row(), row(99), bad])
    with pytest.raises(AcceptanceError):
        target.run_profile(bench, object(), report)
    assert report["samples"][-1] == bad and not report["passed"]


@pytest.mark.parametrize("field,value", [("position", 3), ("observed_pulses", 200),
    ("threshold_pulses", 99), ("outcome_flags", 3), ("run", 12), ("sequence_index", 7),
    ("binding_epoch", 2), ("exchange_id", 3), ("sequence_state", 2), ("output_code", 2),
    ("trigger_ordinal", 2), ("ready_ordinal", 2), ("cycle_elapsed_ms", 201)])
def test_history_rejects_uncompleted_wrong_or_late_record(field, value):
    records = history()
    records[0][field] = value
    with pytest.raises(AcceptanceError):
        target.check_history(records, dict(run=11, generation=19, binding_epoch=1), 100)


@pytest.mark.parametrize("difference,accepted", [(0, True), (1, True), (-1, False), (2, False)])
def test_timer1_history_millisecond_quantization(difference, accepted):
    records = history()
    records[0]["sample_done_tick_ms"] += difference
    if accepted:
        target.check_history(records, dict(run=11, generation=19, binding_epoch=1), 100)
    else:
        with pytest.raises(AcceptanceError, match="timing"):
            target.check_history(records, dict(run=11, generation=19, binding_epoch=1), 100)


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


def test_diagnostic_stress_failure_still_runs_cleanup(tmp_path, monkeypatch):
    transport_fixture(monkeypatch)
    monkeypatch.setattr(target.Bench, "identity", lambda self: None)
    monkeypatch.setattr(target.Bench, "command", lambda self, text: '0,"No error"')
    def fail_snapshot(*args):
        raise TimeoutError("TDMA snapshot timeout")
    monkeypatch.setattr(target.ring, "sample", fail_snapshot)
    def profile(bench, port, report):
        report["samples"] = [row()]
        target.capture_diagnostic_stress(bench, port, report)
    monkeypatch.setattr(target, "run_external_profile", profile)
    cleanup_calls = []
    monkeypatch.setattr(target, "cleanup", lambda *args: cleanup_calls.append(True))
    assert target.main(cli(tmp_path, "--external-input", "--diagnostic-stress")) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert cleanup_calls == [True] and not report["passed"]
    assert "TDMA snapshot timeout" in report["failure"]
    diagnostic = report["two_positions"]["diagnostic_stress"][0]
    assert diagnostic["error_before"] == '0,"No error"'
    assert diagnostic["failure"] == "TimeoutError: TDMA snapshot timeout"


def test_quiet_terminal_failure_still_runs_main_cleanup(tmp_path, monkeypatch):
    bench, port, profile_report, sleeps = quiet_fixture(tmp_path, monkeypatch, fault="terminal")
    external = target.run_external_profile
    def profile(unused_bench, unused_port, report):
        external(bench, port, report)
    transport_fixture(monkeypatch)
    monkeypatch.setattr(target.Bench, "identity", lambda self: None)
    monkeypatch.setattr(target.ring, "EvidencePort", type(port))
    monkeypatch.setattr(target, "run_external_profile", profile)
    cleanup_calls = []
    monkeypatch.setattr(target, "cleanup", lambda *args: cleanup_calls.append(True))
    assert target.main(cli(tmp_path, "--external-input", "--quiet-capture", "--duration", "10")) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert cleanup_calls == [True] and not report["passed"]
    assert "did not naturally finish" in report["failure"]
    assert report["two_positions"]["samples"][0]["link"]["error"] == 5


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
                                        ("--duration", "0"), ("--threshold", str(0xffffffff)),
                                        ("--position-cycle-target-ms", "0")])
def test_invalid_settings_fail_before_hardware(tmp_path, option, value):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, option, value))


@pytest.mark.parametrize("fault", [None, "resume_reset", "duplicate_sample", "restart_old_run", "stop_owned", "wrong_state"])
def test_continuous_pause_restart_accounting(tmp_path, monkeypatch, fault):
    args = target.parse_args(cli(tmp_path, "--lifecycle"))
    commands = []
    monkeypatch.setattr(target, "configure", lambda *a, **kw: commands.append(kw["repeat"]))
    def observation(bench, report, name, predicate, **kwargs):
        events = {"waiting": 0, "initial_partial": 1, "paused": 1,
                  "counting_while_paused": 4, "resumed": 4,
                  "position_complete": 100, "restarted": 0}[name]
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
    def command(text):
        commands.append(text)
        if text.startswith("TRIG:SEQ:INJECT") or text == "TRIG:SEQ:NEXT":
            return "1"
        return wire(history()[int(text.split()[1]) - 1], target.HISTORY_FIELDS)
    bench = SimpleNamespace(args=args, write=commands.append, command=command,
        wait_state=lambda unused: dict(error="NONE", faults=0, backend_fault=0))
    report = {}
    if fault:
        with pytest.raises(AcceptanceError): target.run_lifecycle(bench, object(), report)
        assert not report["passed"]
    else:
        target.run_lifecycle(bench, object(), report)
        assert report["passed"]
        assert commands[:6] == [0, "TRIG:SEQ:INJECT IN1,1", "TRIG:PAUS",
                                "TRIG:SEQ:INJECT IN1,3", "TRIG:CONT",
                                "TRIG:SEQ:INJECT IN1,96"]
        assert commands[-3:] == ["TRIG:STOP", "TRIG:START", "TRIG:STOP"]


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
