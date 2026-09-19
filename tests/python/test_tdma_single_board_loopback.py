"""Failure evidence must survive setup, transport and cleanup errors."""
from contextlib import contextmanager
import json

import pytest

from tools.tdma_ring_monitor import tdma_single_board_loopback as target

PREPARE_RING = target.prepare_single_board_ring


@pytest.mark.parametrize("response,busy", [('"BUSY"', True), ("broken", False)])
def test_snapshot_busy_is_explicit_and_never_a_partial_sample(monkeypatch, response, busy):
    commands = []

    def query(port, command, timeout):
        commands.append(command)
        return response

    monkeypatch.setattr(target, "query", query)
    with pytest.raises(target.SnapshotBusy if busy else AssertionError) as caught:
        target.sample(object(), 1)
    assert commands == ["SYSTem:REFMEM:SYNC:TDMA:STATus?"]
    if busy:
        assert caught.value.response == response


@pytest.fixture
def bench(monkeypatch, tmp_path):
    monkeypatch.setattr("sys.argv", ["loopback", "COM_TEST", "--out-dir",
        str(tmp_path / "evidence"), "--duration-s", "0.004",
        "--poll-interval-s", "0.001", "--expected-build", "test-build"])
    args = target.parse_args()
    monkeypatch.setattr(target, "parse_args", lambda: args)
    calls = []
    count = 0
    running = False
    now = [0.0]
    monkeypatch.setattr(target.time, "monotonic", lambda: now[0])
    def advance(seconds):
        now[0] += seconds
    monkeypatch.setattr(target.time, "sleep", advance)

    @contextmanager
    def port(_args):
        class Port:
            def write(self, data):
                calls.append(data.decode("ascii").strip())
                return len(data)
            def flush(self):
                pass
        yield Port()

    def transport(_port, command, _timeout):
        nonlocal count, running
        calls.append(command)
        if command == "*IDN?":
            return "HAOFV,HDRT100,test-uid,1"
        if command == "SYSTem:FW:BUILD?":
            return '"test-build"'
        if command == "SYSTem:ERR?":
            return '0,"No error"'
        if command == "SYSTem:TDMA:RING:START":
            running = True
        if command == "SYSTem:TDMA:RING:STOP":
            running = False
        if command == "SYSTem:REFMEM:SYNC:TDMA:STATus?":
            count += 1
            fields = [0] * len(target.TDMA_FIELDS)
            for index in (target.RING_ENABLED, target.RING_ADAPTER_STARTED):
                fields[index] = 1
            for index in (target.RING_UP_RUNNING, target.RING_DOWN_RUNNING):
                fields[index] = int(running)
            fields[target.RING_LAST_ERROR] = 5
            for index in (target.RING_SEQ, target.RING_UP_TX_SEQUENCE,
                          target.RING_DOWN_RX_SEQUENCE, target.RING_ADAPTER_TX_COUNT,
                          target.RING_ADAPTER_RX_COUNT):
                fields[index] = count
            return ",".join(map(str, fields))
        if command == "SYSTem:SYNC:VDC:TDMA:PHYS?":
            values = dict.fromkeys(target.PHYS_FIELDS, 0)
            values.update(baud_hz=10000000, tx_csn_pin=26, tx_sck_pin=25,
                          tx_pin=29, rx_csn_pin=27, rx_sck_pin=28, rx_pin=24)
            return ",".join(str(values[key]) for key in target.PHYS_FIELDS)
        return "1"

    def prepare(ser, setup_args, result):
        result["enabled"] = True
        result["steps"] = [target.checked_action(
            ser, "SYSTem:TDMA:RING:START", setup_args.timeout)]
        return result

    monkeypatch.setattr(target, "open_loopback_port", port)
    monkeypatch.setattr(target, "transport_query", transport)
    monkeypatch.setattr(target, "prepare_single_board_ring", prepare)
    return args, calls, transport


def report(args):
    return json.loads((args.out_dir / "summary.json").read_text(encoding="utf-8"))


def test_positive_keeps_raw_samples_and_does_not_claim_formal_timestamps(bench):
    args, calls, _ = bench
    assert target.main() == 0
    evidence = report(args)
    assert evidence["electrical_data_loopback_passed"]
    assert not evidence["formal_feedback_evidence"]
    assert evidence["samples"][0]["raw_tdma"]
    assert evidence["samples"][0]["raw_phys"]
    assert evidence["transcript"][0]["response"].startswith("HAOFV")
    assert "CALibration:TOPology:PROBe 0" in calls


@pytest.mark.parametrize("name,value", [
    ("ring_enabled", 0), ("ring_adapter_started", 0),
    ("ring_up_running", 0), ("ring_down_running", 0),
    ("ring_adapter_last_error", 8), ("ring_last_error", 8),
    ("ring_seq", 0), ("ring_up_tx_sequence", 0),
    ("ring_down_rx_sequence", 0), ("ring_adapter_tx_count", 0),
    ("ring_adapter_rx_count", 0), ("ring_adapter_rx_bad_count", 1),
])
def test_middle_tdma_failure_cannot_be_hidden_by_recovery(bench, monkeypatch, name, value):
    args, _, transport = bench
    sample_count = 0
    def middle_failure(port, command, timeout):
        nonlocal sample_count
        response = transport(port, command, timeout)
        if command == "SYSTem:REFMEM:SYNC:TDMA:STATus?":
            sample_count += 1
            if sample_count == 2:
                values = response.split(",")
                values[target.TDMA_FIELDS.index(name)] = str(value)
                return ",".join(values)
        return response
    monkeypatch.setattr(target, "transport_query", middle_failure)
    assert target.main() == 1
    evidence = report(args)
    assert evidence["valid_sample_count"] == 4
    assert evidence["samples"][1]["tdma"][target.TDMA_FIELDS.index(name)] == value
    assert any("sample[" in failure for failure in evidence["failures"])
    assert all(value > 0 for value in evidence["counter_deltas"].values())
    assert all(value == 0 for value in evidence["bad_deltas"].values())


@pytest.mark.parametrize("name,value", [
    ("baud_hz", 100), ("rx_pin", 1), ("rx_bad_count", 1),
    ("rx_ring_overrun_count", 1),
])
def test_middle_physical_failure_cannot_be_hidden_by_recovery(bench, monkeypatch, name, value):
    args, _, transport = bench
    sample_count = 0
    def middle_failure(port, command, timeout):
        nonlocal sample_count
        response = transport(port, command, timeout)
        if command == "SYSTem:SYNC:VDC:TDMA:PHYS?":
            sample_count += 1
            if sample_count == 2:
                values = response.split(",")
                values[target.PHYS_FIELDS.index(name)] = str(value)
                return ",".join(values)
        return response
    monkeypatch.setattr(target, "transport_query", middle_failure)
    assert target.main() == 1
    evidence = report(args)
    assert evidence["samples"][1]["phys"][name] == value
    assert any("sample[" in failure and name in failure for failure in evidence["failures"])
    assert all(value == 0 for value in evidence["bad_deltas"].values())


def test_bad_counter_reset_is_failure_even_before_later_growth(bench, monkeypatch):
    args, _, transport = bench
    sample_count = 0
    def reset(port, command, timeout):
        nonlocal sample_count
        response = transport(port, command, timeout)
        if command == "SYSTem:REFMEM:SYNC:TDMA:STATus?":
            sample_count += 1
            values = response.split(",")
            values[target.RING_ADAPTER_RX_BAD_COUNT] = "0" if sample_count == 2 else "7"
            return ",".join(values)
        return response
    monkeypatch.setattr(target, "transport_query", reset)
    assert target.main() == 1
    evidence = report(args)
    assert evidence["bad_deltas"]["adapter_rx_bad_count"] == 0
    assert any("adapter_rx_bad_count changed by -7" in failure for failure in evidence["failures"])


@pytest.mark.parametrize("phase", ["setup", "sampling", "cleanup"])
def test_exception_writes_evidence_and_fails(bench, monkeypatch, phase):
    args, calls, transport = bench
    args.timeout = 0.001
    def failing(port, command, timeout):
        if ((phase == "setup" and command == "SYSTem:TDMA:RING:START") or
            (phase == "sampling" and command == "SYSTem:REFMEM:SYNC:TDMA:STATus?") or
            (phase == "cleanup" and command == "SYSTem:TDMA:RING:STOP")):
            raise OSError(f"{phase} cable disconnect")
        return transport(port, command, timeout)
    monkeypatch.setattr(target, "transport_query", failing)
    assert target.main() == 1
    evidence = report(args)
    assert not evidence["passed"]
    assert any(phase in failure for failure in evidence["failures"])
    assert any("exception" in entry for entry in evidence["transcript"])
    assert "CALibration:TOPology:PROBe 0" in calls


def test_setup_rejection_retains_raw_reply(bench, monkeypatch):
    args, _, transport = bench
    def reject(port, command, timeout):
        if command == "SYSTem:ERR?":
            return '-221,"Settings conflict"'
        return transport(port, command, timeout)
    monkeypatch.setattr(target, "transport_query", reject)
    assert target.main() == 1
    assert any(entry.get("response") == '-221,"Settings conflict"'
               for entry in report(args)["transcript"])


def test_build_mismatch_has_evidence_and_no_mutation(bench):
    args, calls, _ = bench
    args.expected_build = "other-build"
    assert target.main() == 1
    assert calls == ["*IDN?", "SYSTem:FW:BUILD?"]
    assert "build mismatch" in report(args)["failures"][0]


def test_uid_mismatch_has_evidence_and_no_mutation(bench):
    args, calls, _ = bench
    args.expected_serial_number = "839E1AE79EA20F31"
    assert target.main() == 1
    assert calls == ["*IDN?"]
    assert "device UID mismatch" in report(args)["failures"][0]


@pytest.mark.parametrize("name", ["duration_s", "poll_interval_s", "timeout",
                                   "arm_wait", "owner_wait", "start_wait", "settle"])
@pytest.mark.parametrize("value", [float("nan"), float("inf"), -1.0])
def test_invalid_timing_rejected_before_hardware(bench, name, value):
    args, calls, _ = bench
    setattr(args, name, value)
    with pytest.raises(SystemExit, match="finite"):
        target.main()
    assert not calls


def test_build_query_timeout_cannot_pass_without_expected_build(bench, monkeypatch):
    args, _, transport = bench
    args.expected_build = None
    def timeout_build(port, command, timeout):
        if command == "SYSTem:FW:BUILD?":
            return "<timeout>"
        return transport(port, command, timeout)
    monkeypatch.setattr(target, "transport_query", timeout_build)
    assert target.main() == 1
    assert any("FW:BUILD?" in failure for failure in report(args)["failures"])


def test_open_failure_has_summary(bench, monkeypatch):
    args, _, _ = bench
    @contextmanager
    def fail(_args):
        raise OSError("port unavailable")
        yield
    monkeypatch.setattr(target, "open_loopback_port", fail)
    assert target.main() == 1
    assert "port unavailable" in report(args)["failures"][0]


def test_malformed_sample_is_not_skipped(bench, monkeypatch):
    args, _, transport = bench
    def malformed(port, command, timeout):
        if command == "SYSTem:REFMEM:SYNC:TDMA:STATus?":
            return "broken-status"
        return transport(port, command, timeout)
    monkeypatch.setattr(target, "transport_query", malformed)
    assert target.main() == 1
    evidence = report(args)
    assert any("broken-status" in item for item in evidence["failures"])
    assert any(item.get("response") == "broken-status" for item in evidence["transcript"])


def test_existing_evidence_is_never_overwritten(bench):
    args, calls, _ = bench
    args.out_dir.mkdir()
    with pytest.raises(FileExistsError):
        target.main()
    assert not calls


def test_real_setup_failure_keeps_completed_steps(bench, monkeypatch):
    args, _, transport = bench
    monkeypatch.setattr(target, "prepare_single_board_ring", PREPARE_RING)
    def fail_stage(port, command, timeout):
        if command.startswith("SYSTem:TDMA:OPMode:STAGe"):
            return "<timeout>"
        return transport(port, command, timeout)
    monkeypatch.setattr(target, "transport_query", fail_stage)
    assert target.main() == 1
    evidence = report(args)
    assert evidence["ring_setup"]["steps"][0]["command"] == "SYSTem:TDMA:RING:STOP"
    assert any("response=<timeout>" in failure for failure in evidence["failures"])


def test_binding_hook_runs_after_topology_before_arm_and_failure_never_starts(bench, monkeypatch):
    args, _, _ = bench
    commands = []
    owner_waits = []
    monkeypatch.setattr(target, "write_only", lambda *a: {})
    monkeypatch.setattr(target, "query", lambda *a: '0,"No error"')
    def action(port, command, timeout):
        commands.append(command)
        response = "QUEUED,2,0,0,17" if "RING:TOPology" in command else "OK"
        return {"command": command, "response": response, "error": '0,"No error"'}
    monkeypatch.setattr(target, "checked_action", action)
    def wait(query, sequence, timeout, evidence):
        owner_waits.append((sequence, timeout))
        evidence.append({"command": "SYSTem:SYNC:VDC:DPLL:OWNer?",
                         "response": "17,17,1,2,0"})
        return {"sequence": sequence, "opcode": 1, "applied": True}
    monkeypatch.setattr(target, "wait_owner_applied", wait)
    monkeypatch.setattr(target.time, "sleep", lambda _: None)

    def bind():
        assert commands[-1].startswith("CALibration:TOPology:PROBe")
        assert any("TOPology " in command for command in commands)
        assert not any(command.endswith((":ARM", ":START")) for command in commands)
        raise RuntimeError("binding rejected")

    with pytest.raises(RuntimeError, match="binding rejected"):
        PREPARE_RING(object(), args, {}, before_arm=bind)
    assert owner_waits == [(17, args.owner_wait)]
    assert not any(command.endswith((":ARM", ":START")) for command in commands)


def test_topology_requires_queued_identity_and_token_before_probe_or_arm(bench, monkeypatch):
    args, _, _ = bench
    commands = []
    monkeypatch.setattr(target, "write_only", lambda *a: {})
    monkeypatch.setattr(target, "query", lambda *a: '0,"No error"')
    def action(port, command, timeout):
        commands.append(command)
        return {"command": command, "response": "1", "error": '0,"No error"'}
    monkeypatch.setattr(target, "checked_action", action)
    monkeypatch.setattr(target.time, "sleep", lambda _: None)

    with pytest.raises(RuntimeError, match="queued topology acknowledgement"):
        PREPARE_RING(object(), args, {})

    assert any("RING:TOPology" in command for command in commands)
    assert not any("TOPology:PROBe" in command or command.endswith((":ARM", ":START"))
                   for command in commands)


def test_owner_wait_failure_retains_raw_observation_before_probe(bench, monkeypatch):
    args, _, _ = bench
    result = {}
    commands = []
    monkeypatch.setattr(target, "write_only", lambda *a: {})
    monkeypatch.setattr(target, "query", lambda *a: '0,"No error"')
    def action(port, command, timeout):
        commands.append(command)
        response = "QUEUED,2,0,0,23" if "RING:TOPology" in command else "OK"
        return {"command": command, "response": response, "error": '0,"No error"'}
    monkeypatch.setattr(target, "checked_action", action)
    def timeout(query, sequence, timeout_s, evidence):
        evidence.append({"command": "SYSTem:SYNC:VDC:DPLL:OWNer?",
                         "response": "23,0,1,1,1", "host_monotonic": 1.0})
        raise TimeoutError("owner application timed out")
    monkeypatch.setattr(target, "wait_owner_applied", timeout)
    monkeypatch.setattr(target.time, "sleep", lambda _: None)

    with pytest.raises(TimeoutError, match="owner application timed out"):
        PREPARE_RING(object(), args, result)

    assert result["topology_owner"] == {
        "queued_response": "QUEUED,2,0,0,23",
        "sequence": 23,
        "wait_result": None,
        "observations": [{"command": "SYSTem:SYNC:VDC:DPLL:OWNer?",
                          "response": "23,0,1,1,1", "host_monotonic": 1.0}],
    }
    assert not any("TOPology:PROBe" in command or command.endswith((":ARM", ":START"))
                   for command in commands)


def test_real_owner_wait_reaches_applied_pending_zero_before_probe(bench, monkeypatch):
    args, _, _ = bench
    result = {}
    commands = []
    owner_replies = iter(("31,0,1,1,1", "31,31,1,2,0"))
    monkeypatch.setattr(target, "write_only", lambda *a: {})
    monkeypatch.setattr(target, "query", lambda *a: '0,"No error"')
    def action(port, command, timeout):
        commands.append(command)
        response = "QUEUED,2,0,0,31" if "RING:TOPology" in command else "OK"
        return {"command": command, "response": response, "error": '0,"No error"'}
    monkeypatch.setattr(target, "checked_action", action)
    def owner_query(port, command, timeout):
        assert command == "SYSTem:SYNC:VDC:DPLL:OWNer?"
        return next(owner_replies)
    monkeypatch.setattr(target, "retryable_query", owner_query)

    def stop_before_arm():
        assert commands[-1].startswith("CALibration:TOPology:PROBe")
        raise RuntimeError("stop after owner proof")

    with pytest.raises(RuntimeError, match="stop after owner proof"):
        PREPARE_RING(object(), args, result, before_arm=stop_before_arm)

    owner = result["topology_owner"]
    assert owner["wait_result"] == {"sequence": 31, "opcode": 1, "applied": True}
    assert [row["response"] for row in owner["observations"]] == [
        "31,0,1,1,1", "31,31,1,2,0"]
    assert commands.index("SYSTem:TDMA:RING:TOPology 2,0,0") < commands.index(
        "CALibration:TOPology:PROBe 1,10")
    assert not any(command.endswith((":ARM", ":START")) for command in commands)


def test_wrong_owner_opcode_blocks_probe_and_arm(bench, monkeypatch):
    args, _, _ = bench
    commands = []
    monkeypatch.setattr(target, "write_only", lambda *a: {})
    monkeypatch.setattr(target, "query", lambda *a: '0,"No error"')
    def action(port, command, timeout):
        commands.append(command)
        response = "QUEUED,2,0,0,41" if "RING:TOPology" in command else "OK"
        return {"command": command, "response": response, "error": '0,"No error"'}
    monkeypatch.setattr(target, "checked_action", action)
    monkeypatch.setattr(target, "wait_owner_applied", lambda *a, **k: {
        "sequence": 41, "opcode": 2, "applied": True})
    monkeypatch.setattr(target.time, "sleep", lambda _: None)

    with pytest.raises(RuntimeError, match="opcode mismatch"):
        PREPARE_RING(object(), args, {})

    assert not any("TOPology:PROBe" in command or command.endswith((":ARM", ":START"))
                   for command in commands)


def test_recovered_query_exception_still_fails_evidence(bench, monkeypatch):
    args, _, transport = bench
    failures_left = 1
    def recover(port, command, timeout):
        nonlocal failures_left
        if command == "*IDN?" and failures_left:
            failures_left -= 1
            raise OSError("transient read failure")
        return transport(port, command, timeout)
    monkeypatch.setattr(target, "transport_query", recover)
    assert target.main() == 1
    evidence = report(args)
    assert evidence["valid_sample_count"] >= 2
    assert any("transient read failure" in failure for failure in evidence["failures"])


def test_write_is_not_retried_after_reply_loss(monkeypatch):
    calls = []
    def fail(*args):
        calls.append(args)
        raise OSError("reply lost")
    monkeypatch.setattr(target, "query", fail)
    monkeypatch.setattr(target.time, "sleep", lambda _: None)
    with pytest.raises(OSError):
        target.retryable_query(object(), "SYSTem:TDMA:RING:START", 1)
    assert len(calls) == 1


def test_port_retries_enter_but_never_yielded_body(monkeypatch):
    from argparse import Namespace
    opens = []
    closes = []
    @contextmanager
    def port(*_args):
        opens.append(1)
        if len(opens) == 1:
            raise OSError("initial enumerate")
        try:
            yield object()
        finally:
            closes.append(1)
    monkeypatch.setattr(target, "open_serial_port", port)
    monkeypatch.setattr(target.time, "sleep", lambda _: None)
    args = Namespace(port="COM_TEST", baud=115200, timeout=1, settle=0)
    with pytest.raises(OSError, match="body disconnect"):
        with target.open_loopback_port(args):
            raise OSError("body disconnect")
    assert len(opens) == 2
    assert len(closes) == 1
