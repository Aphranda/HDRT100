"""Keep RJ45 cycle evidence honest without accessing serial/VISA hardware."""
from contextlib import contextmanager
import json

import pytest

from tools.hardware_acceptance import sequence_tdma_cycle_validate as target
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError


def cli(tmp_path, *extra):
    return ["--port", "TEST", "--serial-number", "UID", "--build", "BUILD",
            "--out", str(tmp_path / "evidence.json"), *extra]


def snapshot(completed=0, phase=3, repeat=1, **changes):
    row = dict.fromkeys(target.LINK_FIELDS, 0)
    row.update(enabled=1, phase=phase, binding_epoch=2, model_epoch=7, run=11,
               generation=19, step=completed, txfragments=6 * (completed + 1),
               rxmessages=2 * completed + 1, triggers=completed + 1,
               ready=completed, completed=completed, dutslot=2, vnaslot=3,
               input=1, outputmask=8, pulseus=1000, timeoutms=5000, repeat=repeat,
               exchange_id=completed + 1)
    row.update(changes)
    return row


def wire(row):
    return ",".join(str(row[name]) for name in target.LINK_FIELDS)


@pytest.mark.parametrize("bad", ["", "1,2", ",".join(["0"] * 22),
    ",".join(["0"] * 24), wire(snapshot()).replace("1,", "-1,", 1),
    wire(snapshot()).replace("1,", "１,", 1), wire(snapshot(phase=9)),
    wire(snapshot(enabled=2)), wire(snapshot(run=2**32)), wire(snapshot(input=5)),
    wire(snapshot(outputmask=16)), wire(snapshot(falling=2))])
def test_link_parser_rejects_invalid_or_truncated(bad):
    with pytest.raises(AcceptanceError):
        target.parse_link(bad)


def test_link_parser_retains_all_23_fields():
    row = snapshot(repeat=2)
    assert target.parse_link(wire(row)) == row


@pytest.mark.parametrize("reason,name", [(0, "NONE"), (5, "TX_SEQUENCE_IDENTITY_OR_SIZE"),
    (6, "MAILBOX_BYTES"), (9, "STALE_MAILBOX_SEQUENCE"), (10, "UNKNOWN")])
def test_transport_diagnostics_preserve_facts_without_claiming_root_cause(reason, name):
    row = target.parse_transport(f"1,1,65535,{reason},20,18,1")
    assert row == dict(enabled=1, seen=1, mailbox_seq16=65535, last_reject=reason,
                      matches=20, published=18, snapshot_quality=1,
                      last_reject_name=name, snapshot_quality_name="FRESH")


@pytest.mark.parametrize("scenario", ["recover", "expired", "scpi_error", "malformed"])
def test_ring_snapshot_retries_only_explicit_busy_with_deadline(monkeypatch, scenario):
    args = type("Args", (), {"timeout": 1.0, "poll": 0.01})()
    clock = iter([0.0, 2.0 if scenario == "expired" else 0.0])
    monkeypatch.setattr(target.time, "monotonic", lambda: next(clock))
    monkeypatch.setattr(target.time, "sleep", lambda _: None)
    error = '-200,"Execution error"' if scenario == "scpi_error" else '0,"No error"'
    monkeypatch.setattr(target.ring, "query", lambda *unused: error)
    calls = []

    def sample(*unused):
        calls.append(1)
        if scenario == "malformed":
            raise AssertionError("malformed TDMA status")
        if len(calls) == 1:
            raise target.ring.SnapshotBusy('"BUSY"')
        return {"tdma": [0]}

    monkeypatch.setattr(target.ring, "sample", sample)
    report = {}
    if scenario == "recover":
        assert target.ring_snapshot(object(), args, report, "test") == {"tdma": [0]}
        assert len(calls) == 2
    else:
        match = {"expired": "snapshot is BUSY", "scpi_error": "SCPI error", "malformed": "malformed"}
        with pytest.raises((RuntimeError, AssertionError), match=match[scenario]):
            target.ring_snapshot(object(), args, report, "test")
        assert len(calls) == 1
    if scenario != "malformed":
        assert report["ring_snapshot_busy"] == [{"label": "test", "response": '"BUSY"', "error": error}]


@pytest.mark.parametrize("bad", ["", "1,0,0,5,0,0", "1,0,0,5,0,0,0,0",
    "1,0,0,-1,0,0,1", "１,0,0,0,0,0,1", "2,0,0,0,0,0,1", "1,2,0,0,0,0,1",
    "1,0,65536,0,0,0,1", "1,0,0,4294967296,0,0,1", "1,0,0,0,0,0,3"])
def test_transport_diagnostics_reject_malformed_values(bad):
    with pytest.raises(AcceptanceError):
        target.parse_transport(bad)


@contextmanager
def fake_port(*args, **kwargs):
    yield object()


def main_transport(monkeypatch):
    monkeypatch.setattr(target, "discover", lambda: {})
    monkeypatch.setattr(target, "open_serial_port", fake_port)


def test_wrong_uid_never_mutates_or_cleans_up(tmp_path, monkeypatch):
    main_transport(monkeypatch)
    commands = []

    def command(self, text):
        commands.append(text)
        return "NO.1,DHRT100,OTHER,0.1" if text == "*IDN?" else '"BUILD"'

    monkeypatch.setattr(target.Bench, "command", command)
    monkeypatch.setattr(target, "execute", lambda *a: pytest.fail("executed wrong device"))
    monkeypatch.setattr(target, "cleanup", lambda *a: pytest.fail("cleanup wrote wrong device"))
    assert target.main(cli(tmp_path)) == 1
    assert commands == ["*IDN?", "SYST:FW:BUILD?"]
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert not report["passed"] and not report["functional_cycle_verified"]
    assert report["failure"] and not report["cleanup_failures"]


class FakeBench:
    def __init__(self, args, rows):
        self.args, self.rows = args, list(rows)
        self.current = None
        self.commands = []
        self.identities = 0

    def write(self, text):
        self.commands.append(text)

    def command(self, text):
        self.commands.append(text)
        if text == "READ:SEQ:LINK?":
            if self.rows:
                self.current = self.rows.pop(0)
            return wire(self.current)
        if text == "READ:SEQ:REP?":
            return f"{self.args.repeat},{self.args.repeat},1"
        if text.startswith("TRIG:SEQ:INJECT READY,"):
            return "1"
        raise AssertionError(text)

    def status(self):
        return dict(state="IDLE" if self.current["phase"] == 8 else "READY",
                    error="NONE", faults=0, backend_fault=0,
                    run_id=self.current["run"], generation=self.current["generation"])

    def wait_state(self, state):
        assert state == "IDLE"
        return self.status()

    def identity(self):
        self.identities += 1


def execute_fixture(tmp_path, monkeypatch, rows, repeat=1):
    args = target.parse_args(cli(tmp_path, "--repeat", str(repeat), "--duration", "1"))
    clock = [0.0]
    monkeypatch.setattr(target.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(target.time, "sleep", lambda duration: clock.__setitem__(0, clock[0] + duration))
    monkeypatch.setattr(target, "configure", lambda *a: None)
    calls = [0]

    def ring_sample(*a):
        calls[0] += 1
        return {"tdma": [calls[0]] * 200}

    monkeypatch.setattr(target.ring, "sample", ring_sample)
    bench = FakeBench(args, rows)
    return bench, {}


def test_scpi_ready_is_one_attributed_batch(tmp_path):
    args = target.parse_args(cli(tmp_path, "--scpi-next"))
    bench = FakeBench(args, [])
    report = {}
    first = snapshot(exchange_id=7)
    target.inject_ready_batch(bench, report, first)
    target.inject_ready_batch(bench, report, first)

    assert bench.commands == ["TRIG:SEQ:INJECT READY,8"]
    assert report["ready_injection"] == {
        "identity": [11, 19, 2], "command": "TRIG:SEQ:INJECT READY,8",
        "count": 8, "response": "1"}


def test_scpi_ready_rejection_is_preserved_and_fails(tmp_path):
    args = target.parse_args(cli(tmp_path, "--scpi-next"))
    bench = FakeBench(args, [])
    bench.command = lambda command: "0"
    report = {}

    with pytest.raises(AcceptanceError, match="was not accepted"):
        target.inject_ready_batch(bench, report, snapshot(exchange_id=7))

    assert report["ready_injection"] == {
        "identity": [11, 19, 2], "command": "TRIG:SEQ:INJECT READY,8",
        "count": 8, "response": "0"}


@pytest.mark.parametrize("repeat", [1, 2])
def test_finite_counts_include_first_measurement_without_step(tmp_path, monkeypatch, repeat):
    total = 8 * repeat
    rows = [snapshot(repeat=repeat), snapshot(total - 1, phase=8, repeat=repeat, ready=total)]
    bench, report = execute_fixture(tmp_path, monkeypatch, rows, repeat)
    target.execute(bench, object(), report)
    assert report["functional_cycle_verified"]
    assert report["samples"][-1]["link"]["triggers"] == total
    assert report["samples"][-1]["link"]["completed"] == total - 1
    assert report["repeat_result"] == f"{repeat},{repeat},1"
    assert bench.identities == 1


def test_finite_wrong_counts_preserve_failed_sample(tmp_path, monkeypatch):
    bench, report = execute_fixture(tmp_path, monkeypatch, [snapshot(6, phase=8, ready=7)])
    with pytest.raises(AcceptanceError, match="finite measurement count mismatch"):
        target.execute(bench, object(), report)
    assert report["samples"][-1]["link"]["completed"] == 6
    assert not report.get("functional_cycle_verified", False)


def test_continuous_requires_more_than_first_finite_round(tmp_path, monkeypatch):
    bench, report = execute_fixture(tmp_path, monkeypatch, [snapshot(7, repeat=0), snapshot(9, repeat=0)], 0)
    target.execute(bench, object(), report)
    assert len(report["samples"]) == 2
    assert report["samples"][-1]["link"]["completed"] == 9
    assert "READ:SEQ:REP?" not in bench.commands


def test_continuous_stall_cannot_pass(tmp_path, monkeypatch):
    bench, report = execute_fixture(tmp_path, monkeypatch, [snapshot(7, repeat=0)], 0)
    with pytest.raises(AcceptanceError, match="insufficient"):
        target.execute(bench, object(), report)
    assert not report.get("functional_cycle_verified", False)


@pytest.mark.parametrize("row", [snapshot(9, phase=8, repeat=0, ready=10), snapshot(9, repeat=2)])
def test_continuous_rejects_finished_or_wrong_repeat(tmp_path, monkeypatch, row):
    bench, report = execute_fixture(tmp_path, monkeypatch, [row], 0)
    with pytest.raises(AcceptanceError):
        target.execute(bench, object(), report)
    assert not report.get("functional_cycle_verified", False)


@pytest.mark.parametrize("error", [RuntimeError("link disconnected"), KeyboardInterrupt()])
def test_execute_exception_preserves_cleanup_failures(tmp_path, monkeypatch, error):
    main_transport(monkeypatch)
    monkeypatch.setattr(target.Bench, "identity", lambda self: None)

    def execute(*a):
        a[2]["raw_failed_snapshot"] = "kept"
        raise error

    def broken_write(self, text):
        raise RuntimeError("sequence stop lost port")

    actions = []

    def checked(port, command, timeout):
        actions.append(command)
        raise RuntimeError("ring cleanup lost port")

    monkeypatch.setattr(target, "execute", execute)
    monkeypatch.setattr(target.Bench, "write", broken_write)
    monkeypatch.setattr(target.ring, "checked_action", checked)
    assert target.main(cli(tmp_path)) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert report["raw_failed_snapshot"] == "kept"
    assert type(error).__name__ in report["failure"]
    assert len(report["cleanup_failures"]) == 5
    assert report["cleanup_failures"][0].startswith("ring diagnostic snapshot:")
    assert report["cleanup_failures"][1].startswith("ring diagnostic error query:")
    assert "sequence stop lost port" in report["cleanup_failures"][2]
    assert actions == ["SYSTem:TDMA:RING:STOP", "CALibration:TOPology:PROBe 0"]
    assert not report["passed"]


def test_successful_operation_with_failed_cleanup_remains_failed(tmp_path, monkeypatch):
    main_transport(monkeypatch)
    monkeypatch.setattr(target.Bench, "identity", lambda self: None)
    monkeypatch.setattr(target, "execute", lambda b, p, r: r.update(functional_cycle_verified=True))
    monkeypatch.setattr(target, "cleanup", lambda b, p, r: r["cleanup_failures"].append("outputs not zero"))
    assert target.main(cli(tmp_path)) == 1
    report = json.loads((tmp_path / "evidence.json").read_text(encoding="utf-8"))
    assert report["functional_cycle_verified"] and not report["passed"]
    assert "cleanup verification failed" in report["failure"]


@pytest.mark.parametrize("option,value", [("--dut-slot", "4"), ("--vna-slot", "4"),
    ("--dut-instance", "6"), ("--vna-instance", "8"), ("--node-count", "3"),
    ("--operating-level", "6"), ("--train-cycles", "2048")])
def test_gui_rejects_cli_settings_not_represented_by_builders(tmp_path, option, value):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, "--gui-control", option, value))


def gui_bench(tmp_path, fail_command=None, fail_response="0"):
    from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as gui
    args = target.parse_args(cli(
        tmp_path, "--gui-control", "--scpi-next", "--repeat", "2"))
    report = {"transcript": []}
    commands = []

    def exchange(command, timeout):
        commands.append(command)
        if command == fail_command:
            return fail_response
        if command == "SYST:ERR?":
            return '0,"No error"'
        if command == gui.RING_STATUS_QUERY:
            fields = dict.fromkeys(gui.RUNTIME_FIELDS, 0)
            return ",".join(str(fields[k]) for k in gui.RUNTIME_FIELDS)
        if command == "TRIG:SEQ:NEXT?":
            return '"IDLE"'
        if command == "READ:SEQ:LINK?":
            return wire(snapshot(phase=1, repeat=2, input=0))
        if command == "SYST:TDMA:FLIGHT:MODE?":
            return "2"
        if command == "READ:SEQ? SP8T":
            return '"SP8T",0,1,2,3,4,5,6,7'
        if command.startswith("READ:SEQ:CODE?"):
            code = command.split()[1]
            return f"{code},{code}"
        if command == "READ:SEQ:SOUR?":
            return '"MANUAL","RISING"'
        if command == "READ:SEQ:OUTPUT?":
            return '7,0,"NONE",10,0,1'
        if command == "READ:SEQ:NODE:ROLE? 5":
            return '5,"DUT",1,1,152,11,5,152,11,5'
        if command == "READ:SEQ:NODE:ROLE? 7":
            return '7,"VNA",1,1,152,3,3,152,3,3'
        if command.startswith("CONF:SEQ:NODE:ROLE "):
            return '"STAGED"'
        if command == "CONF:SEQ:NODE:ACT":
            return '"ACTIVE",1'
        return "1"

    return target.Bench(object(), args, report, exchange), report, commands


def test_gui_configure_runs_real_builders_executor_and_readbacks(tmp_path, monkeypatch):
    from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as gui
    bench, report, commands = gui_bench(tmp_path)
    monkeypatch.setattr(target, "prepare_ring", lambda *a, **k: pytest.fail("non-GUI ring setup"))
    target.configure(bench, object(), report)
    expected = gui.build_mode_configuration(gui.MODE_RJ45, "SP8T", list(range(8)),
        "MANUAL", "RIS", 10, 1000, 7, 0, "NONE", "MANUAL", 5000, 2)
    record = report["gui_control"]["configure"]
    assert record["commands"] == expected and record["completed"]
    assert record["exchanges"] and len(report["gui_control"]["module_sha256"]) == 64
    assert "SYST:ERR?" in commands and "READ:SEQ:NODE:ROLE? 7" in commands
    assert report["link_configuration"]["repeat"] == 2
    assert report["roles"]["after"]["DUT"]["active_io"] == 11


@pytest.mark.parametrize("command,response", [("CONF:SEQ:NODE:ROLE 2,5,DUT", '"REJECTED"'),
    ("CONF:SEQ:NODE:ACT", '"STAGED"'), ("CONF:SEQ:CODE 3,3", "0"),
    ("CONF:SEQ:CODE 3,3", "<timeout>"), ("SYST:ERR?", '-100,"Error"')])
def test_gui_typed_rejection_stops_dependent_commands(tmp_path, command, response):
    bench, report, commands = gui_bench(tmp_path, command, response)
    with pytest.raises((AcceptanceError, RuntimeError)):
        target.configure(bench, object(), report)
    assert not report["gui_control"]["configure"]["completed"]
    assert not any(c.startswith("CONF:SEQ:LINK LOOPBACK") for c in commands)


def test_gui_configure_readback_mismatch_fails(tmp_path):
    bench, report, _ = gui_bench(tmp_path, "READ:SEQ:CODE? 5", "5,4")
    with pytest.raises(AcceptanceError, match="code readback mismatch"):
        target.configure(bench, object(), report)


def test_gui_ring_ack_timeout_uses_real_status_verification(tmp_path):
    bench, report, commands = gui_bench(tmp_path, "SYST:TDMA:RING:STOP", "<timeout>")
    target.configure(bench, object(), report)
    record = report["gui_control"]["configure"]
    assert record["completed"]
    assert {"command": "SYST:TDMA:RING:STOP", "response": "<timeout>"} in record["exchanges"]
    assert "SYST:TDMA:RING:STAT?" in commands


def test_gui_start_uses_real_batch_and_same_cycle_checks(tmp_path, monkeypatch):
    from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as gui
    bench, report = execute_fixture(tmp_path, monkeypatch,
        [snapshot(), snapshot(7, phase=8, ready=8)])
    bench.args.gui_control = True
    original = bench.command

    def exchange(command):
        if command == "SYST:ERR?":
            return '0,"No error"'
        if command == gui.RING_STATUS_QUERY:
            fields = dict.fromkeys(gui.RUNTIME_FIELDS, 0)
            running = "SYST:TDMA:RING:ARM" in bench.commands
            fields.update(ring_enabled=int(running), ring_adapter_started=int(running),
                          ring_up_running=int(running))
            return ",".join(str(fields[k]) for k in gui.RUNTIME_FIELDS)
        if command.startswith("SYST:TDMA:RING:") or command == "TRIG:START":
            bench.commands.append(command)
            return "1"
        return original(command)

    bench.command = exchange
    target.execute(bench, object(), report)
    record = report["gui_control"]["start"]
    assert record["commands"] == gui.build_start_commands(gui.MODE_RJ45)
    assert record["completed"] and report["functional_cycle_verified"]
    assert bench.commands.count("TRIG:START") == 1


@pytest.mark.parametrize("repeat", [1, 2])
def test_pause_resume_requires_explicit_continuous_mode(tmp_path, repeat):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, "--pause-resume", "--repeat", str(repeat)))
    assert not target.parse_args(cli(tmp_path)).pause_resume


class PauseBench(FakeBench):
    def __init__(self, args, before, paused, resumed):
        super().__init__(args, [before])
        self.paused, self.resumed = paused, resumed
        self.mode = "running"
        self.paused_reads = 0
        self.quiet_change = None
        self.output_extra = 0

    def write(self, text):
        super().write(text)
        if text == "TRIG:PAUS":
            self.mode, self.current, self.rows = "paused", self.paused.copy(), []
        if text == "TRIG:CONT":
            self.mode, self.rows = "resuming", list(self.resumed)

    def command(self, text):
        if text == "READ:SEQ:LINK?" and self.mode == "paused":
            self.paused_reads += 1
            if self.paused_reads >= 2 and self.quiet_change:
                self.current.update(self.quiet_change)
        if text == "READ:IO:STAT?":
            self.commands.append(text)
            return f"1,{self.current['completed'] % 8 | self.output_extra},15,1,0"
        return super().command(text)

    def status(self):
        row = super().status()
        row.update(state="PAUSED" if self.current["phase"] == 6 else "READY",
            current_index=self.current["completed"] % 8, current_state=self.current["completed"] % 8,
            accepted=self.current["completed"], completed=self.current["completed"])
        return row


def pause_fixture(tmp_path, monkeypatch):
    base, report = execute_fixture(tmp_path, monkeypatch, [], repeat=0)
    base.args.pause_resume = True
    base.args.duration = 5
    before = snapshot(9, repeat=0)
    # A switch can settle between the PAUSE request and PAUSED. An outstanding
    # trigger is cancelled, so lifetime triggers need not equal step count + 1.
    paused = snapshot(10, phase=6, repeat=0, triggers=11, ready=10)
    resumed = [snapshot(11, repeat=0, triggers=13, ready=11),
               snapshot(19, repeat=0, triggers=21, ready=19)]
    return PauseBench(base.args, before, paused, resumed), report


def test_pause_resume_accepts_cancelled_measurement_and_verifies_new_cycles(tmp_path, monkeypatch):
    bench, report = pause_fixture(tmp_path, monkeypatch)
    target.execute(bench, object(), report)
    evidence = report["pause_resume"]
    assert evidence["passed"] and evidence["exchange_rotated"]
    assert report["functional_cycle_verified"]
    assert evidence["paused"]["link"]["completed"] == 10
    assert evidence["quiet_samples"] and evidence["quiet_elapsed_s"] >= bench.args.quiet
    assert all(sample["io"]["outputs"] & 8 == 0 for sample in evidence["quiet_samples"])
    assert bench.commands.count("TRIG:PAUS") == bench.commands.count("TRIG:CONT") == 1
    assert "TRIG:RESUME" not in bench.commands
    assert report["samples"][-1]["link"]["completed"] == 19
    assert report["samples"][-1]["link"]["triggers"] == 21  # Cancellation is retained.


@pytest.mark.parametrize("change", [dict(step=11), dict(triggers=12), dict(ready=11),
    dict(completed=11), dict(txfragments=1000), dict(phase=3), dict(run=12), dict(error=2)])
def test_pause_activity_or_identity_change_fails_before_resume(tmp_path, monkeypatch, change):
    bench, report = pause_fixture(tmp_path, monkeypatch)
    bench.quiet_change = change
    with pytest.raises(AcceptanceError):
        target.execute(bench, object(), report)
    assert not report["pause_resume"]["passed"]
    assert report["pause_resume"]["quiet_samples"]
    assert "TRIG:CONT" not in bench.commands


def test_paused_gateway_high_pad_fails(tmp_path, monkeypatch):
    bench, report = pause_fixture(tmp_path, monkeypatch)
    bench.output_extra = 8
    with pytest.raises(AcceptanceError, match="gateway output remained high"):
        target.execute(bench, object(), report)
    assert "TRIG:CONT" not in bench.commands


@pytest.mark.parametrize("resumed", [snapshot(10, repeat=0, triggers=12, ready=10),
    snapshot(11, repeat=0, triggers=13, ready=11, run=12),
    snapshot(11, repeat=0, triggers=14, ready=11)])
def test_resume_requires_same_run_and_completed_measurement(tmp_path, monkeypatch, resumed):
    bench, report = pause_fixture(tmp_path, monkeypatch)
    bench.resumed = [resumed]
    with pytest.raises(AcceptanceError):
        target.execute(bench, object(), report)
    assert not report["pause_resume"]["passed"]
    assert report["pause_resume"]["resume_samples"]
    assert "TRIG:CONT" in bench.commands


@pytest.mark.parametrize("stopped", [True, False])
def test_cleanup_requires_actual_ring_stopped_readback(tmp_path, monkeypatch, stopped):
    bench, report = execute_fixture(tmp_path, monkeypatch, [snapshot(9, phase=1, repeat=0)], repeat=0)
    bench.current = bench.rows[0]
    monkeypatch.setattr(target, "read_io", lambda b: dict(inputs=1, outputs=0, owned=0, armed=0, busy=0))
    actions = []
    monkeypatch.setattr(target.ring, "checked_action",
        lambda p, c, t: actions.append(c) or {"response": '"OK"'})
    reads = [0]

    def ring_sample(*args):
        reads[0] += 1
        values = [0] * 200
        values[target.ring.RING_ENABLED] = int(not stopped or reads[0] < 3)
        values[target.ring.RING_ADAPTER_STARTED] = int(not stopped or reads[0] < 3)
        return {"tdma": values}

    monkeypatch.setattr(target.ring, "sample", ring_sample)
    target.cleanup(bench, object(), report)
    assert len(report["ring_stop_readbacks"]) >= 2
    failures = [failure for failure in report["cleanup_failures"] if failure.startswith("TDMA stop:")]
    assert bool(failures) is not stopped
    if not stopped:
        assert "readback did not confirm" in failures[0]
    assert actions[-1] == "CALibration:TOPology:PROBe 0"


def test_cleanup_attributes_scpi_error_to_transport_query(tmp_path, monkeypatch):
    bench, report = execute_fixture(tmp_path, monkeypatch, [snapshot(phase=1)], repeat=0)
    bench.current = bench.rows.pop(0)
    original = bench.command
    error_queries = [0]

    def command(text):
        if text == "SYST:ERR?":
            error_queries[0] += 1
            return '-200,"transport snapshot"' if error_queries[0] == 2 else '0,"No error"'
        if text == "READ:SEQ:LINK:TRANSPORT?":
            return "1,0,0,0,0,0,1"
        if text.startswith("SYST:"):
            return "0"
        return original(text)

    bench.command = command
    monkeypatch.setattr(target, "read_io",
        lambda b: dict(inputs=0, outputs=0, owned=0, armed=0, busy=0))
    monkeypatch.setattr(target.ring, "checked_action", lambda *a: {"response": "OK"})
    target.cleanup(bench, object(), report)
    record = report["transport_before_cleanup"]["READ:SEQ:LINK:TRANSPORT?"]
    assert record["error_before"].startswith("0,")
    assert record["error_after"].startswith("-200,")
    assert "SCPI error from READ:SEQ:LINK:TRANSPORT?" in report["cleanup_failures"][0]


@pytest.mark.parametrize("error_reply", ['-200,"Execution error"', '0,"No error"', None])
def test_cleanup_preserves_ring_diagnostic_failure_before_stop(tmp_path, monkeypatch, error_reply):
    bench, report = execute_fixture(tmp_path, monkeypatch, [snapshot(phase=1)], repeat=0)
    bench.current = bench.rows.pop(0)
    original = bench.command
    pending_diagnostic = [False]
    commands = []
    def command(text):
        commands.append(text)
        if text == "SYST:ERR?":
            if pending_diagnostic[0]:
                pending_diagnostic[0] = False
                if error_reply is None:
                    raise TimeoutError("error queue unavailable")
                return error_reply
            return '0,"No error"'
        if text == "READ:SEQ:LINK:TRANSPORT?":
            return "1,0,0,0,0,0,1"
        if text.startswith("SYST:"):
            return "0"
        return original(text)
    bench.command = command
    def ring_sample(*args):
        if "diagnostic_failure" not in report:
            pending_diagnostic[0] = True
            commands.append("failed ring snapshot")
            raise TimeoutError("TDMA snapshot timeout")
        return {"tdma": [0] * 200}
    def checked_action(port, command, timeout):
        assert not pending_diagnostic[0]
        commands.append(command)
        return {"response": "OK"}
    monkeypatch.setattr(target.ring, "sample", ring_sample)
    monkeypatch.setattr(target.ring, "checked_action", checked_action)
    monkeypatch.setattr(target, "read_io",
        lambda b: dict(inputs=0, outputs=0, owned=0, armed=0, busy=0))
    target.cleanup(bench, object(), report)
    assert commands[commands.index("failed ring snapshot") + 1] == "SYST:ERR?"
    assert report["diagnostic_failure"] == "TimeoutError: TDMA snapshot timeout"
    assert report["cleanup_failures"][0].startswith("ring diagnostic snapshot:")
    assert all(not failure.startswith("TDMA stop:") for failure in report["cleanup_failures"])
    assert report["ring_stop_readbacks"] and report["stopped"]["io"]["owned"] == 0
    if error_reply is None:
        assert "error queue unavailable" in report["diagnostic_error_read_failure"]
        assert len(report["cleanup_failures"]) == 2
    else:
        assert report["diagnostic_error_after"] == error_reply
        assert len(report["cleanup_failures"]) == 1
