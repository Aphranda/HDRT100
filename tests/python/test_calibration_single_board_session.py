from copy import deepcopy
from argparse import Namespace
from dataclasses import dataclass
import json
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from calibration_ring_validate import calibration_single_board_session as session


def trials(delay=52.):
    return [dict(passed=True, signal_group=group, delay_estimate_ns=delay)
            for group in (0, 1) for _ in range(3)]


def test_measured_fixture_baseline_preserves_quantization():
    result = session.measured_baseline(trials())
    assert result["requested_base_ns"] == 26
    assert result["base_samples"] == 7
    assert result["quantized_base_ns"] == 28
    assert result["quantization_error_ns"] == 2
    # Do not hard-code the short-fixture delay into the derivation.
    assert session.measured_baseline(trials(80.))["requested_base_ns"] == 40


@pytest.mark.parametrize("mutation", ["missing", "failed", "group", "unstable", "nan", "odd"])
def test_incomplete_or_inconsistent_latency_cannot_seed_phase(mutation):
    values = trials()
    if mutation == "missing":
        values.pop()
    elif mutation == "failed":
        values[0]["passed"] = False
    elif mutation == "group":
        values[0]["signal_group"] = 1
    elif mutation == "unstable":
        values[0]["delay_estimate_ns"] = 56.
    elif mutation == "nan":
        values[0]["delay_estimate_ns"] = float("nan")
    else:
        values = trials(53.)
    with pytest.raises(ValueError):
        session.measured_baseline(values)


def fixture_identity():
    return [1, 2, 0, 0, 123, 234, 345, 456, 10, 10, 0, 4]


def test_identity_reads_staged_config_and_allows_untrained_stage():
    responses = iter(["NO.1,DHRT100,UID,0.1.0", '"BUILD"',
                      ",".join(map(str, fixture_identity()))])
    result = session.identity(lambda _: next(responses), "UID", "BUILD")
    assert result["config"]["calibration_generation"] == 0
    assert result["config"]["topology_generation"] == 123


@pytest.mark.parametrize("index,value", [(0, 2), (1, 4), (2, 1), (3, 1), (4, 0),
                                        (5, 0), (6, 0), (7, 0), (9, 9), (11, 8)])
def test_identity_refuses_invalid_unsettled_or_wrong_topology(index, value):
    values = fixture_identity()
    values[index] = value
    responses = iter(["NO.1,DHRT100,UID,0.1.0", '"BUILD"', ",".join(map(str, values))])
    with pytest.raises(ValueError):
        session.identity(lambda _: next(responses), "UID", "BUILD")


@pytest.mark.parametrize("field", session.CONFIG_FIELDS)
def test_any_configuration_change_invalidates_predecessor(field):
    baseline = dict(config=dict(zip(session.CONFIG_FIELDS, fixture_identity())), build="A")
    changed = deepcopy(baseline)
    changed["config"][field] += 1
    with pytest.raises(ValueError, match="stale"):
        session.same_identity(baseline, changed)
    assert session.digest(baseline) != session.digest(changed)


@pytest.fixture
def session_harness(tmp_path, monkeypatch):
    """Pure host orchestration fakes; these tests make no hardware claim."""
    from calibration_ring_validate import calibration_rxphase as rx
    from calibration_ring_validate import calibration_data_loopback as data

    @dataclass
    class Board:
        address: str = "UID"
        build: str = "BUILD"
        idn: str = "NO.1,DHRT100,UID,0.1.0"
        port: str = "MOCK_ONLY"

    state = dict(config_read=0, change_at=None, change_field="profile_crc", calls=[],
                 latency_calls=0, marker_calls=0, rx_calls=0, mark_phase_calls=0, data_calls=0,
                 failure=None, closed=False,
                 error_at=None, error_reads=0)
    board = Board()
    args = Namespace(board_id="UID", expected_build="BUILD", out_dir=tmp_path / "session",
                     epoch=10, timeout=1., capture_timeout=1., settle=0.)
    monkeypatch.setattr(session.marker, "discover", lambda args: {"UID": board})
    monkeypatch.setattr(session.marker, "close_persistent_connections",
                        lambda: state.update(closed=True))

    def command(board, text, args):
        state["calls"].append(text)
        if text == "*IDN?":
            return board.idn
        if text == "SYST:FW:BUILD?":
            return '"BUILD"'
        if text == "SYST:ERR?":
            state["error_reads"] += 1
            if state["error_reads"] == state["error_at"]:
                return '-200,"injected device error"'
            return '0,"No error"'
        if text == session.CONFIG_QUERY:
            state["config_read"] += 1
            values = fixture_identity()
            if state["change_at"] is not None and state["config_read"] >= state["change_at"]:
                values[session.CONFIG_FIELDS.index(state["change_field"])] += 1
            return ",".join(map(str, values))
        if text == "SYST:REFMEM:SYNC:TDMA:STAT?":
            from tdma_field_parse import FIELDS
            return ",".join("0" for _ in FIELDS)
        return "OK"

    monkeypatch.setattr(session.marker, "board_command", command)

    def latency_trial(exchange, args, epoch, group):
        state["latency_calls"] += 1
        exchange(f"CAL:P3:START MOCK,{epoch},{group}")
        try:
            if state["failure"] == "latency_exception" and state["latency_calls"] == 2:
                raise RuntimeError("injected latency transport failure")
            return dict(passed=state["failure"] != "latency", epoch=epoch, signal_group=group,
                        delay_estimate_ns=56. if state["failure"] == "unstable" and
                            state["latency_calls"] == 6 else 52., cleanup={"state": 0})
        finally:
            exchange("CAL:P3:STOP")

    def marker_trial(exchange, args, epoch):
        state["marker_calls"] += 1
        exchange(f"CAL:MARK:LOOP:START MOCK,{epoch}")
        try:
            if state["failure"] == "marker_exception" and state["marker_calls"] == 2:
                raise RuntimeError("injected marker transport failure")
            return dict(passed=state["failure"] != "marker", epoch=epoch, cleanup={"state": 0})
        finally:
            exchange("CAL:MARK:LOOP:STOP")

    def rx_trial(exchange, args, epoch, *, protocol=rx.SCK_PROTOCOL):
        counter = "mark_phase_calls" if protocol.signal == "MARK" else "rx_calls"
        state[counter] += 1
        exchange(f"{protocol.prefix}:START MOCK,{epoch},{args.direction},{args.offset_samples}")
        try:
            if state["failure"] == "rx_exception" and counter == "rx_calls" and state[counter] == 2:
                raise RuntimeError("injected receiver transport failure")
            return dict(passed=True, phase_loaded=True, epoch=epoch, direction=args.direction,
                offset_samples=args.offset_samples, best_lag_sample=121 - args.offset_samples,
                nominal_lag_sample=121, residual_offset_sample_count=-args.offset_samples,
                recommended_offset_sample_count=0, cleanup={"state": 0},
                status=dict(generation=epoch),
                codeword=dict(passed=True, lag_samples=121 - args.offset_samples))
        finally:
            exchange(f"{protocol.prefix}:STOP")

    monkeypatch.setattr(session, "acquire_latency", latency_trial)
    monkeypatch.setattr(session.marker, "run_trial", marker_trial)
    monkeypatch.setattr(rx, "run_trial", rx_trial)
    monkeypatch.setattr(rx, "evaluate_scan", lambda *args, protocol=rx.SCK_PROTOCOL: dict(
        passed=state["failure"] != ("mark_slope" if protocol.signal == "MARK" else "slope")))
    monkeypatch.setattr(rx, "select_offsets", lambda trials, scan, protocol=rx.SCK_PROTOCOL: dict(
        passed=state["failure"] != ("mark_selection" if protocol.signal == "MARK" else "selection"),
        selected_by_direction={str(i): 0 for i in range(len(protocol.pins))},
        scan_generation_identities=[[trial["epoch"], trial["status"]["generation"]] for trial in trials],
        configuration=[1, 26, 4]))

    def residual(trials, selected, repeats, *, scan_generation_identities, expected_configuration,
                 protocol=rx.SCK_PROTOCOL):
        assert selected == {str(i): 0 for i in range(len(protocol.pins))}
        assert repeats == 3
        assert expected_configuration == [1, 26, 4]
        assert len(trials) == len(protocol.pins) * repeats
        assert not ({trial["epoch"] for trial in trials} & {item[0] for item in scan_generation_identities})
        assert all(trial["offset_samples"] == selected[str(trial["direction"])] for trial in trials)
        passed = state["failure"] != ("mark_residual" if protocol.signal == "MARK" else "residual")
        return dict(passed=passed, zero_residual_verified=passed)

    monkeypatch.setattr(rx, "evaluate_residual", residual)

    def data_trial(exchange, args, epoch):
        state["data_calls"] += 1
        assert args.base_ns == 26 and args.mark_offset == 0 and args.gap_samples == 1000
        exchange(f"CAL:DATA:LOOP:START MOCK,{epoch},{args.data_offset}")
        try:
            if state["failure"] == "data_exception" and state["data_calls"] == 2:
                raise RuntimeError("injected DATA failure")
            result = dict(passed=state["failure"] != "data_trial", epoch=epoch,
                          data_offset=args.data_offset, status=dict(generation=epoch),
                          configuration=dict(zip(session.CONFIG_FIELDS, fixture_identity())))
            if state["data_calls"] >= state.get("trial_drift_at", 100):
                field = state["trial_drift_field"]
                if field == "missing":
                    result.pop("configuration")
                else:
                    result["configuration"][field] += 1
            return result
        finally:
            exchange("CAL:DATA:LOOP:STOP")

    monkeypatch.setattr(data, "run_trial", data_trial)
    monkeypatch.setattr(data, "evaluate_scan", lambda *a: dict(passed=state["failure"] != "data_slope"))
    monkeypatch.setattr(data, "select_offset", lambda trials, scan, *, diagnostic_window: dict(
        passed=state["failure"] != "data_selection", selected_offset=-2,
        scan_generation_identities=[[trial["epoch"], trial["epoch"]] for trial in trials],
        configuration=[1, 26, 0, 1000], residual_policy={"diagnostic_window": diagnostic_window}))

    def data_residual(trials, selected, repeats, *, scan_generation_identities, expected_configuration,
                      diagnostic_window, residual_policy, scan_trials, scan):
        assert selected == -2 and repeats == 3 and len(trials) == 3
        assert diagnostic_window is True and residual_policy == {"diagnostic_window": True}
        assert len(scan_trials) == 15 and scan["passed"]
        assert [[trial["epoch"], trial["epoch"]] for trial in scan_trials] == scan_generation_identities
        assert all(trial["data_offset"] == -2 for trial in trials)
        assert expected_configuration == [1, 26, 0, 1000]
        assert not ({trial["epoch"] for trial in trials} & {item[0] for item in scan_generation_identities})
        return dict(passed=state["failure"] != "data_residual", zero_residual_verified=True)

    monkeypatch.setattr(data, "evaluate_residual", data_residual)
    return args, state


@pytest.mark.parametrize("config_read,allowed", [(2, (0, 0, 0)), (3, (6, 0, 0)),
    (4, (6, 0, 0)), (5, (6, 3, 0)), (6, (6, 3, 0)), (7, (6, 3, 0)),
    (8, (6, 3, 0)), (9, (6, 3, 0)), (10, (6, 3, 0)), (11, (6, 3, 18)),
    (12, (6, 3, 18)), (13, (6, 3, 24)), (14, (6, 3, 24)), (15, (6, 3, 24)),
    (16, (6, 3, 24)), (17, (6, 3, 24))])
@pytest.mark.parametrize("field", session.CONFIG_FIELDS)
def test_configuration_drift_at_stage_boundary_blocks_dependent_start(session_harness, config_read, allowed, field):
    args, state = session_harness
    state.update(change_at=config_read, change_field=field)
    result = session.run(args)
    assert not result["passed"]
    assert result["error"]
    assert tuple(state[name] for name in ("latency_calls", "marker_calls", "rx_calls")) == allowed
    assert state["data_calls"] == (0 if config_read <= 14 else 15 if config_read <= 16 else 18)
    assert state["closed"]
    assert (args.out_dir / "summary.json").is_file()
    assert result["transcript"]
    assert not result["parameter_flash_saved"]


@pytest.mark.parametrize("failure,counts", [("latency", (1, 0, 0)), ("unstable", (6, 0, 0)),
    ("marker", (6, 1, 0)), ("slope", (6, 3, 18)), ("residual", (6, 3, 24))])
def test_predecessor_failure_blocks_dependent_stages_and_keeps_trials(session_harness, failure, counts):
    args, state = session_harness
    state["failure"] = failure
    result = session.run(args)
    assert not result["passed"]
    assert tuple(state[name] for name in ("latency_calls", "marker_calls", "rx_calls")) == counts
    failed = result["stages"][-1]
    assert not failed["passed"]
    assert failed["result"]["trials"], "already acquired evidence must survive baseline reduction failure"
    assert failed["transcript_sha256"]
    assert state["closed"]
    saved = json.loads((args.out_dir / "summary.json").read_text(encoding="utf-8"))
    assert saved == result
    assert not result["trn_training_complete"]


@pytest.mark.parametrize("failure", ["latency_exception", "marker_exception", "rx_exception"])
def test_transport_exception_retains_completed_trials_transcript_and_cleanup(session_harness, failure):
    args, state = session_harness
    state["failure"] = failure
    result = session.run(args)
    assert not result["passed"]
    assert "injected" in result["error"]
    failed = result["stages"][-1]
    assert len(failed["result"]["trials"]) >= 1
    assert failed["result"]["trials"][0]["cleanup"]["state"] == 0
    assert result["transcript"][-1]["command"].endswith(":STOP")
    assert state["closed"]
    assert (args.out_dir / "summary.json").is_file()
    assert not any("SAVE" in item or "COMMIT" in item for item in state["calls"])


@pytest.mark.parametrize("error_at,counts", [(1, (6, 0, 0)), (2, (6, 3, 0)), (3, (6, 3, 0)),
    (4, (6, 3, 0)), (5, (6, 3, 18)), (6, (6, 3, 24)),
    (7, (6, 3, 24)), (8, (6, 3, 24))])
def test_stage_device_error_blocks_successors(session_harness, error_at, counts):
    args, state = session_harness
    state["error_at"] = error_at
    result = session.run(args)
    assert not result["passed"]
    assert tuple(state[name] for name in ("latency_calls", "marker_calls", "rx_calls")) == counts
    assert "injected device error" in str(result)
    assert state["closed"]


def test_selection_failure_never_starts_residual_trials(session_harness):
    args, state = session_harness
    state["failure"] = "selection"
    result = session.run(args)
    assert not result["passed"]
    assert not result["selection"]["passed"]
    assert state["rx_calls"] == 18
    assert len(result["stages"]) == 5


def test_full_host_chain_reloads_selected_offset_with_fresh_epochs(session_harness):
    args, state = session_harness
    result = session.run(args)
    assert result["passed"], result.get("error")
    assert [stage["name"] for stage in result["stages"]] == [
        "latency", "marker", "marker-rxphase", "marker-rxphase-residual", "rxphase", "rxphase-residual",
        "data-window", "data-window-residual"]
    assert state["mark_phase_calls"] == 12
    assert state["rx_calls"] == 24
    assert state["data_calls"] == 18
    assert result["stages"][-1]["result"]["evaluation"]["zero_residual_verified"]
    assert not result["trn_training_complete"]


@pytest.mark.parametrize("fault,count", [("mark_slope", 9), ("mark_selection", 9),
                                       ("mark_residual", 12)])
def test_mark_phase_failure_blocks_independent_sck(session_harness, fault, count):
    args, state = session_harness
    state["failure"] = fault
    result = session.run(args)
    assert not result["passed"]
    assert state["mark_phase_calls"] == count
    assert state["rx_calls"] == 0
    assert not any(command.startswith("CAL:SCK:RXPHASE:START") for command in state["calls"])
    assert not result["trn_training_complete"]


def test_phase_stages_have_distinct_epochs_and_chain_to_predecessor(session_harness):
    args, state = session_harness
    result = session.run(args)
    assert result["passed"]
    epochs = [trial["epoch"] for stage in result["stages"] for trial in stage["result"]["trials"]]
    assert len(epochs) == len(set(epochs)) == 63
    previous = session.digest(result["identity"])
    for stage in result["stages"]:
        assert stage["predecessor_sha256"] == previous
        unsigned = {k: v for k, v in stage.items() if k != "sha256"}
        assert session.digest(unsigned) == stage["sha256"]
        previous = stage["sha256"]


@pytest.mark.parametrize("fault,count", [("data_trial", 1), ("data_slope", 15),
    ("data_selection", 15), ("data_residual", 18), ("data_exception", 2)])
def test_data_stage_failure_retains_evidence_and_blocks_completion(session_harness, fault, count):
    args, state = session_harness
    state["failure"] = fault
    result = session.run(args)
    assert not result["passed"]
    assert state["data_calls"] == count
    assert result["stages"][-1]["result"]["trials"]
    assert not result["trn_training_complete"]
    assert not result.get("prerequisites_acquired")
    assert not any("SAVE" in command or "COMMIT" in command for command in state["calls"])


@pytest.mark.parametrize("field", ["missing", *session.CONFIG_FIELDS])
@pytest.mark.parametrize("drift_at", [1, 16])
def test_data_acquisition_must_bind_predecessor_even_with_unchanged_stage_boundaries(
        session_harness, field, drift_at):
    args, state = session_harness
    state.update(trial_drift_at=drift_at, trial_drift_field=field)
    result = session.run(args)
    assert not result["passed"]
    assert state["data_calls"] == drift_at
    failed_trial = result["stages"][-1]["result"]["trials"][-1]
    assert not failed_trial["passed"]
    assert "differs from predecessor" in failed_trial["error"]
    assert not result.get("prerequisites_acquired")
    assert result["stages"][-1]["transcript_sha256"]
    assert state["closed"]


@pytest.mark.parametrize("fault", [None, "start_error", "start_wrong_epoch", "start_short",
    "initiator_reject", "responder_reject", "initiator_edges", "responder_edges"])
def test_latency_start_echo_and_distinct_endpoint_identity_are_required(fault, monkeypatch):
    from test_calibration_link_p3 import make_snapshot
    monkeypatch.setattr(session.time, "sleep", lambda _: None)
    snapshots = {1: make_snapshot(1, 9), 2: make_snapshot(2, 6)}
    for snapshot in snapshots.values():
        snapshot.update(baud_hz=10_000_000, clock_high_ns=50, clock_low_ns=50, data_high_ns=48)
        for endpoint in range(1, 5):
            value = snapshot[f"t{endpoint}_ns"]
            snapshot[f"t{endpoint}_lo"] = value & 0xFFFFFFFF
            snapshot[f"t{endpoint}_hi"] = value >> 32
    if fault and fault.endswith("reject"):
        snapshots[1 if fault.startswith("initiator") else 2]["reject_reason"] = 4
    elif fault and fault.endswith("edges"):
        snapshots[1 if fault.startswith("initiator") else 2]["edge_mask"] = 15
    idle = dict.fromkeys(session.latency.P3_FIELDS, 0)
    calls = []

    def exchange(command):
        calls.append(command)
        if command == "CAL:P3:STOP":
            return "OK"
        if command.startswith("CAL:P3:START"):
            return {"start_error": "ERROR", "start_wrong_epoch": "3,10000000,32,256,8,0",
                    "start_short": "3,10000000,32,256,7"}.get(fault, "3,10000000,32,256,7,0")
        snapshot = snapshots[int(command[-1])] if command.endswith((" 1", " 2")) else idle
        return ",".join(str(snapshot[field]) for field in session.latency.P3_FIELDS)

    args = Namespace(capture_timeout=1., frequency_tolerance_percent=5., duty_tolerance_percent=10.)
    result = session.acquire_latency(exchange, args, 7, 0)
    assert result["passed"] is (fault is None), result
    assert result["cleanup"]["state"] == 0
    assert calls.count("CAL:P3:STOP") == 2
    if fault and fault.startswith("start"):
        assert not any(command.endswith((" 1", " 2")) for command in calls)
