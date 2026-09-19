from contextlib import contextmanager
import json
from types import SimpleNamespace

import pytest

from tools.hardware_acceptance import sequence_config_validate as target
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError


def cli(tmp_path, *extra):
    return ["--port", "TEST", "--serial-number", "UID", "--build", "BUILD",
            "--out", str(tmp_path / "config.json"), *extra]


def fixture(tmp_path, monkeypatch, fault=None):
    args = target.parse_args(cli(tmp_path, "--attempts", "3"))
    commands, active = [], [0]
    pending = [False]
    def command(text):
        commands.append(text)
        if text == "SYST:ERR?":
            if pending[0]:
                pending[0] = False
                return '-200,"Execution error"'
            return '0,"No error"'
        if text == "TRIG:STOP":
            return "1"
        if text == "SYST:TDMA:RING:STOP":
            return '"OK"'
        if text == "CONF:SEQ:LINK OFF":
            if fault == "link":
                pending[0] = True
                return '"REJECTED",9,1,0,0'
            return "1"
        if text.startswith("CONF:SEQ:NODE:ROLE "):
            if fault == "stage":
                return '"REJECTED"'
            return '"STAGED"'
        if text == "CONF:SEQ:NODE:ACT":
            active[0] += 1
            if fault == "timeout":
                pending[0] = True
                raise TimeoutError("ACT response lost")
            return '"REJECTED",7' if fault == "activation" else '"ACTIVE",1,2,3'
        if text.startswith("READ:SEQ:NODE:ROLE?"):
            instance = int(text.split()[1])
            _, _, name, resource, io, ip = next(role for role in target.ROLES if role[1] == instance)
            if fault == "role" and active[0]:
                io = 15
            return f'{instance},"{name}",1,1,{resource},{io},{ip},{resource},{io},{ip}'
        if text == "SYST:REFMEM:LOAD:ACT:STAT?":
            if fault == "malformed":
                return '"BUSY"'
            values = dict.fromkeys(target.ACTIVATION_FIELDS, 0)
            values.update(attempt_seq=active[0], evaluated_mask=255, quality_state=2,
                          staging_crc32=123, staging_seq=8)
            if fault == "stale":
                values["attempt_seq"] = 0
            if fault == "activation":
                values.update(result=7, quality_state=1, unavailable_mask=64)
            if fault == "gate":
                values["failed_mask"] = 4
            return ",".join(str(values[key]) for key in target.ACTIVATION_FIELDS)
        raise AssertionError(text)
    bench = SimpleNamespace(args=args, command=command, wait_state=lambda state: {"state": state})
    monkeypatch.setattr(target, "read_io", lambda b: dict(inputs=0, outputs=0, owned=0, armed=0, busy=0))
    monkeypatch.setattr(target.ring, "sample", lambda *args: {"tdma": [0] * 200,
                                                               "raw_tdma": "raw", "raw_phys": "physical"})
    return bench, commands, {}


def test_repeated_idle_role_activation_uses_new_diagnostics(tmp_path, monkeypatch):
    bench, commands, report = fixture(tmp_path, monkeypatch)
    target.run_attempts(bench, object(), report)
    assert len(report["attempts"]) == 3 and all(row["passed"] for row in report["attempts"])
    assert commands.count("CONF:SEQ:NODE:ACT") == 3
    for slot, instance, name, *_ in target.ROLES:
        assert commands.count(f"CONF:SEQ:NODE:ROLE {slot},{instance},{name}") == 3
    assert "TRIG:START" not in commands
    for ordinal, attempt in enumerate(report["attempts"], 1):
        assert attempt["activation_after"]["attempt_seq"] == ordinal
        assert attempt["activation_after"]["evaluated_mask"] == 255
        assert attempt["ring_stop_samples"][0]["raw"]["raw_phys"] == "physical"
        assert set(attempt["active_roles"]) == {"COUNTER", "DUT", "VNA"}
        assert all("error_before" in action and "error_after" in action for action in attempt["actions"])


@pytest.mark.parametrize("fault", ["link", "stage", "activation", "timeout", "stale", "role", "gate", "malformed"])
def test_rejection_stops_attempts_and_preserves_diagnostic_without_mutation_retry(tmp_path, monkeypatch, fault):
    bench, commands, report = fixture(tmp_path, monkeypatch, fault)
    with pytest.raises((AcceptanceError, TimeoutError)):
        target.run_attempts(bench, object(), report)
    assert len(report["attempts"]) == 1
    attempt = report["attempts"][0]
    assert not attempt["passed"] and attempt["failure"]
    mutations = [command for command in commands if "?" not in command]
    assert len(mutations) == len(set(mutations))
    assert "activation_at_failure" in attempt
    if fault == "link":
        action = next(action for action in attempt["actions"] if action["command"] == "CONF:SEQ:LINK OFF")
        assert action["response"].startswith('"REJECTED"') and action["error_after"].startswith("-200,")
    if fault == "timeout":
        action = next(action for action in attempt["actions"] if action["command"] == "CONF:SEQ:NODE:ACT")
        assert action["error_after_failure"].startswith("-200,")
        assert attempt["activation_at_failure"]["attempt_seq"] == 1


@pytest.mark.parametrize("reply", ["1,2", ",".join(["-1"] * 14), ",".join([str(2**32)] * 14)])
def test_bad_diagnostic_preserves_raw_reply(reply):
    bench = SimpleNamespace(command=lambda text: '0,"No error"' if text == "SYST:ERR?" else reply)
    report = {}
    with pytest.raises(AcceptanceError):
        target.read_activation(bench, report, "diagnostic")
    assert report["diagnostic"]["raw"] == reply


def test_off_stress_stops_at_first_rejection_without_activation(tmp_path, monkeypatch):
    bench, commands, report = fixture(tmp_path, monkeypatch)
    bench.args.link_off_probes = 20
    original = bench.command
    calls = 0
    def command(text):
        nonlocal calls
        if text == "CONF:SEQ:LINK OFF":
            calls += 1
            if calls == 4:
                commands.append(text)
                return '\"REJECTED\",8,0,0,0'
        return original(text)
    bench.command = command
    with pytest.raises(AcceptanceError, match="rejected"):
        target.run_attempts(bench, object(), report)
    assert calls == 4 and len(report["attempts"]) == 1
    attempt = report["attempts"][0]
    assert attempt["link_off_completed"] == 3 and attempt["link_off_probe"] == 4
    assert "CONF:SEQ:NODE:ACT" not in commands
    assert any(action.get("response") == '\"REJECTED\",8,0,0,0' for action in attempt["actions"])


def test_off_stress_count_is_bounded_per_transaction(tmp_path, monkeypatch):
    bench, commands, report = fixture(tmp_path, monkeypatch)
    bench.args.link_off_probes = 7
    target.run_attempts(bench, object(), report)
    assert commands.count("CONF:SEQ:LINK OFF") == 21
    assert commands.count("CONF:SEQ:NODE:ACT") == 3
    assert all(row["link_off_completed"] == 7 for row in report["attempts"])


def transport_fixture(monkeypatch):
    @contextmanager
    def opened(*args, **kwargs):
        yield object()
    monkeypatch.setattr(target, "discover", lambda: {})
    monkeypatch.setattr(target, "select_transport", lambda *args: None)
    monkeypatch.setattr(target, "open_serial_port", opened)


def test_identity_failure_does_not_mutate_or_cleanup(tmp_path, monkeypatch):
    transport_fixture(monkeypatch)
    def identity(self):
        raise AcceptanceError("wrong firmware")
    monkeypatch.setattr(target.Bench, "identity", identity)
    monkeypatch.setattr(target, "run_attempts", lambda *a: pytest.fail("mutated wrong hardware"))
    monkeypatch.setattr(target, "cleanup", lambda *a: pytest.fail("cleaned wrong hardware"))
    assert target.main(cli(tmp_path)) == 1
    report = json.loads((tmp_path / "config.json").read_text(encoding="utf-8"))
    assert "wrong firmware" in report["failure"]


def test_failed_attempt_is_persisted_and_cleanup_runs(tmp_path, monkeypatch):
    bench, commands, unused = fixture(tmp_path, monkeypatch, "timeout")
    actual = target.run_attempts
    transport_fixture(monkeypatch)
    monkeypatch.setattr(target.Bench, "identity", lambda self: None)
    monkeypatch.setattr(target, "run_attempts", lambda b, p, r: actual(bench, p, r))
    cleaned = []
    monkeypatch.setattr(target, "cleanup", lambda b, p, r: cleaned.append(True))
    assert target.main(cli(tmp_path)) == 1
    report = json.loads((tmp_path / "config.json").read_text(encoding="utf-8"))
    assert cleaned == [True] and not report["passed"]
    assert len(report["attempts"]) == 1
    assert "ACT response lost" in report["failure"]


def test_cleanup_failures_stay_failed_and_both_stops_are_attempted(tmp_path, monkeypatch):
    bench, commands, report = fixture(tmp_path, monkeypatch)
    report["cleanup_failures"] = []
    def stop(*args):
        raise TimeoutError("sequence stop timeout")
    monkeypatch.setattr(target, "stop_sequence", stop)
    target.cleanup(bench, object(), report)
    assert report["cleanup_failures"] == ["sequence stop: TimeoutError: sequence stop timeout"]
    assert commands.count("SYST:TDMA:RING:STOP") == 1


def test_cleanup_pending_error_does_not_suppress_stop_command():
    replies = iter(['-200,"previous failure"', "1", '0,"No error"'])
    commands = []
    def command(text):
        commands.append(text)
        return next(replies)
    report = {}
    with pytest.raises(AcceptanceError, match="pending SCPI error before cleanup"):
        target.command_once(SimpleNamespace(command=command), report, "TRIG:STOP", "1", cleanup=True)
    assert commands == ["SYST:ERR?", "TRIG:STOP", "SYST:ERR?"]
    assert report["actions"][0]["response"] == "1"


@pytest.mark.parametrize("extra", [("--attempts", "0"), ("--attempts", "101"),
                                  ("--link-off-probes", "0"), ("--link-off-probes", "1001"),
                                  ("--timeout", "nan"), ("--poll", "0")])
def test_settings_rejected_before_hardware(tmp_path, extra):
    with pytest.raises(SystemExit):
        target.parse_args(cli(tmp_path, *extra))


def test_report_never_overwrites_existing_evidence(tmp_path):
    path = tmp_path / "config.json"
    path.write_text("original", encoding="utf-8")
    with pytest.raises(FileExistsError):
        target.main(cli(tmp_path))
    assert path.read_text(encoding="utf-8") == "original"
