from argparse import Namespace
from pathlib import Path

import pytest

from tools.calibration_ring_validate import calibration_link_p3 as p3
from tools.calibration_ring_validate.calibration_link_p3 import (
    P3_FLAGS_REQUIRED,
    P3_GROUP_CS_DATA,
    apply_frequency_policy,
    evaluate_pair,
    parse_p3_status,
    replay_saved_summary,
    timing_metrics,
    validation_frequency_ladder,
)

ROOT = Path(__file__).resolve().parents[2]


def test_p3_tool_uses_offline_session_without_realtime_load_mask() -> None:
    source = (ROOT / "tools" / "calibration_ring_validate" /
              "calibration_link_p3.py").read_text(encoding="utf-8")
    assert "TDMA_STOPPED_OFFLINE_CALIBRATION" in source
    assert "SYSTem:TDMA:RING:STOP" in source
    assert "SYSTem:TDMA:LOAD:MASK" not in source
    assert "CALIBRATION_LOAD_BIT" not in source
    assert "start_attempt_count" in source
    assert "next_submit = now + 0.10" in source


def make_snapshot(role: int, edge_mask: int, signal_group: int = 0) -> dict[str, int]:
    return {
        "state": 2,
        "role": role,
        "signal_group": signal_group,
        "flags": P3_FLAGS_REQUIRED,
        "reject_reason": 0,
        "baud_hz": 25_000_000,
        "epoch": 7,
        "sample_period_ns": 4,
        "pulse_count": 32,
        "requested_words": 256,
        "produced_words": 256,
        "edge_mask": edge_mask,
        "dma_overrun_count": 0,
        "pio_stall_count": 0,
        "clock_high_ns": 20,
        "clock_low_ns": 20,
        "data_high_ns": 20,
        "data_pulse_count": 32,
        "t1_ns": 100,
        "t2_ns": 200,
        "t3_ns": 204,
        "t4_ns": 144,
        "result_valid": 1,
    }


def test_parse_p3_status_reconstructs_u64() -> None:
    fields = [2, 1, 0, 15, 0, 10_000_000, 9, 4, 32, 256, 256, 9,
              0, 0, 50, 50, 48, 1, 2, 3, 4, 5, 6, 7, 8, 1, 31]
    parsed = parse_p3_status(",".join(str(value) for value in fields))
    assert parsed["t1_ns"] == 1 | (2 << 32)
    assert parsed["t4_ns"] == 7 | (8 << 32)
    assert parsed["data_pulse_count"] == 31


def test_timing_metrics_checks_frequency_and_duty() -> None:
    metrics = timing_metrics(
        {"clock_high_ns": 20, "clock_low_ns": 20, "data_high_ns": 20,
         "data_pulse_count": 32, "pulse_count": 32,
         "sample_period_ns": 4}, 25_000_000, 5.0, 10.0)
    assert metrics["actual_hz"] == 25_000_000
    assert metrics["duty_percent"] == 50.0
    assert metrics["frequency_ok"] and metrics["duty_ok"]
    assert metrics["data_high_ok"]
    assert metrics["data_burst_ok"]
    assert metrics["minimum_data_pulse_count"] == 29


def test_limited_rx_accepts_half_burst_for_statistical_width() -> None:
    metrics = timing_metrics(
        {"clock_high_ns": 16, "clock_low_ns": 17, "data_high_ns": 16,
         "data_pulse_count": 22, "pulse_count": 32,
         "sample_period_ns": 4}, 30_000_000, 5.0, 10.0)
    assert metrics["minimum_data_pulse_count"] == 16
    assert metrics["data_burst_ok"]


def test_timing_metrics_rejects_short_return_data_pulse() -> None:
    metrics = timing_metrics(
        {"clock_high_ns": 16, "clock_low_ns": 17, "data_high_ns": 8,
         "data_pulse_count": 32, "pulse_count": 32,
         "sample_period_ns": 4}, 30_000_000, 5.0, 10.0)
    assert metrics["frequency_ok"] and metrics["duty_ok"]
    assert not metrics["data_high_ok"]


def test_evaluate_pair_rejects_truncated_data_burst() -> None:
    initiator = make_snapshot(1, 0x09)
    responder = make_snapshot(2, 0x06)
    initiator["data_pulse_count"] = 1
    args = Namespace(frequency_tolerance_percent=5.0,
                     duty_tolerance_percent=10.0)
    result = evaluate_pair(initiator, responder, 25_000_000, args)
    assert "initiator_data_burst" in result["failures"]
    assert not result["passed"]


def test_evaluate_pair_subtracts_residence() -> None:
    initiator = make_snapshot(1, 0x09)
    responder = make_snapshot(2, 0x06)
    args = Namespace(frequency_tolerance_percent=5.0,
                     duty_tolerance_percent=10.0)
    result = evaluate_pair(initiator, responder, 25_000_000, args)
    assert result["source_rtt_ns"] == 44
    assert result["residence_ns"] == 4
    assert result["path_sum_ns"] == 40
    assert result["delay_estimate_ns"] == 20.0
    assert result["passed"]


def test_evaluate_pair_uses_local_return_width_as_link_reference() -> None:
    initiator = make_snapshot(1, 0x09)
    responder = make_snapshot(2, 0x06)
    initiator["data_high_ns"] = 24
    args = Namespace(frequency_tolerance_percent=5.0,
                     duty_tolerance_percent=10.0)
    result = evaluate_pair(initiator, responder, 25_000_000, args)
    assert result["initiator_timing"]["data_width_reference"] == \
        "responder_local_observed"
    assert result["initiator_timing"]["data_high_error_ns"] == 4
    assert result["passed"]
    initiator["data_high_ns"] = 28
    result = evaluate_pair(initiator, responder, 25_000_000, args)
    assert "initiator_data_width" in result["failures"]


def test_replay_saved_summary_reapplies_current_gate() -> None:
    initiator = make_snapshot(1, 0x09)
    responder = make_snapshot(2, 0x06)
    initiator["data_high_ns"] = 24
    trial = {
        "source": "node0", "destination": "node1",
        "frequency_hz": 25_000_000, "signal_group": 0,
        "initiator": initiator, "responder": responder,
        "passed": False, "failures": ["old_gate"],
    }
    source = {
        "measurement_domain": "calibration",
        "phase": "p3_per_link_bidirectional",
        "trials": [trial],
        "ladder": [{"link_index": 0, "source": "node0",
                    "destination": "node1", "signal_group": 0,
                    "frequency_mhz": 25}],
    }
    args = Namespace(frequency_tolerance_percent=5.0,
                     duty_tolerance_percent=10.0)
    replay = replay_saved_summary(source, args)
    assert replay["gate_replay"]
    assert replay["trials"][0]["passed"]
    assert replay["ladder"][0]["accepted_count"] == 1


def test_cs_data_group_keeps_logical_four_edge_gate() -> None:
    initiator = make_snapshot(1, 0x09, P3_GROUP_CS_DATA)
    responder = make_snapshot(2, 0x06, P3_GROUP_CS_DATA)
    args = Namespace(frequency_tolerance_percent=5.0,
                     duty_tolerance_percent=10.0)
    result = evaluate_pair(initiator, responder, 25_000_000, args,
                           P3_GROUP_CS_DATA)
    assert result["signal_group"] == P3_GROUP_CS_DATA
    assert result["passed"]


def test_cs_data_group_keeps_forward_timing_gate() -> None:
    snapshot = make_snapshot(1, 0x09, P3_GROUP_CS_DATA)
    metrics = timing_metrics(snapshot, 25_000_000, 5.0, 10.0,
                             P3_GROUP_CS_DATA)
    assert metrics["primary_timing_valid"]
    assert metrics["frequency_ok"]
    assert metrics["duty_ok"]


def test_30mhz_is_limited_rx_and_falls_back_without_failing_stable() -> None:
    ladder = [
        {"frequency_mhz": 10, "passed": True},
        {"frequency_mhz": 25, "passed": True},
        {"frequency_mhz": 30, "passed": False},
    ]
    policy = apply_frequency_policy(ladder)
    assert policy["stable_profiles_passed"]
    assert policy["highest_stable_frequency_mhz"] == 25
    assert not policy["limited_rx_all_trials_passed"]
    assert ladder[2]["operational_status"] == "FALLBACK_25MHZ"
    assert not ladder[2]["required_for_stable"]
    assert policy["limited_rx_executed"]
    assert policy["limited_rx_operational_status"] == "FALLBACK_25MHZ"


def test_every_validation_requires_30mhz_limited_rx() -> None:
    assert validation_frequency_ladder(None) == [10, 25, 30]
    assert validation_frequency_ladder([10, 25, 30]) == [10, 25, 30]
    try:
        validation_frequency_ladder([10, 25])
    except ValueError as exc:
        assert "exactly 10,25,30" in str(exc)
    else:
        raise AssertionError("validation without 30 MHz must be rejected")


def test_highest_stable_frequency_requires_all_links_to_pass() -> None:
    ladder = [
        {"frequency_mhz": 10, "passed": True},
        {"frequency_mhz": 25, "passed": True},
        {"frequency_mhz": 30, "passed": True},
        {"frequency_mhz": 10, "passed": True},
        {"frequency_mhz": 25, "passed": False},
        {"frequency_mhz": 30, "passed": True},
    ]
    policy = apply_frequency_policy(ladder)
    assert not policy["stable_profiles_passed"]
    assert policy["highest_stable_frequency_mhz"] == 10
    assert policy["limited_rx_all_trials_passed"]


def trial_args() -> Namespace:
    return Namespace(timeout=1.0, action_timeout=0.05, settle=0.0,
                     capture_timeout=0.1, gap=0.001, pulse_count=32,
                     capture_words=256, frequency_tolerance_percent=5.0,
                     duty_tolerance_percent=10.0)


def test_endpoint_query_selects_distinct_views(monkeypatch) -> None:
    commands = []
    fields = [2, 2, 0, 15, 0, 25_000_000, 7, 4, 32, 256, 256, 6,
              0, 0, 20, 20, 20, 0, 0, 200, 0, 204, 0, 0, 0, 1, 32]

    def command(board, text, args):
        commands.append(text)
        return ",".join(map(str, fields))

    monkeypatch.setattr(p3, "board_command", command)
    board = p3.Board("COM3", "uid", "idn", "build")
    snapshot = p3.p3_status(board, trial_args(), p3.P3_ROLE_RESPONDER)
    assert commands == ["READ:CALibration:P3? 2"]
    assert snapshot["role"] == 2
    assert snapshot["t2_ns"] == 200
    with pytest.raises(ValueError, match="endpoint"):
        p3.p3_status(board, trial_args(), p3.P3_ROLE_LOOPBACK)
    assert len(commands) == 1


@pytest.mark.parametrize("signal_group", [0, 1])
def test_single_board_starts_both_slots_once_and_reads_real_endpoints(
        monkeypatch, signal_group) -> None:
    board = p3.Board("COM3", "uid", "idn", "build")
    calls = []
    snapshots = {1: make_snapshot(1, 9, signal_group),
                 2: make_snapshot(2, 6, signal_group)}
    monkeypatch.setattr(p3, "stop_p3",
                        lambda boards, args: calls.append(("stop",)))

    def start(board, role, frequency, epoch, group, args):
        calls.append(("start", role, epoch, group))
        return {"role": role, "epoch": epoch}

    def status(board, args, endpoint=None):
        calls.append(("read", endpoint))
        return snapshots[endpoint]

    monkeypatch.setattr(p3, "start_p3", start)
    monkeypatch.setattr(p3, "p3_status", status)
    result = p3.run_trial(board, board, 25_000_000, 7, 1,
                          signal_group, trial_args())
    assert calls == [("stop",), ("start", 3, 7, signal_group),
                     ("read", 1), ("read", 2), ("stop",)]
    assert result["passed"]
    assert result["endpoint_mode"] == "SINGLE_BOARD_DUAL_ENDPOINT"
    assert result["initiator"] is not result["responder"]
    assert result["loopback_start"]["role"] == p3.P3_ROLE_LOOPBACK
    assert "responder_start" not in result


@pytest.mark.parametrize("fault", ["stale_epoch", "wrong_role", "idle"])
def test_single_board_rejects_stale_or_aliased_endpoint_and_stops(
        monkeypatch, fault) -> None:
    board = p3.Board("COM3", "uid", "idn", "build")
    stops = []
    monkeypatch.setattr(p3, "stop_p3",
                        lambda boards, args: stops.append(boards))
    monkeypatch.setattr(p3, "start_p3", lambda *args: {"role": 3})
    snapshots = {1: make_snapshot(1, 9), 2: make_snapshot(2, 6)}
    if fault == "stale_epoch":
        snapshots[2]["epoch"] = 6
    elif fault == "wrong_role":
        snapshots[2] = snapshots[1]
    else:
        snapshots[2]["state"] = p3.P3_STATE_IDLE
    monkeypatch.setattr(p3, "p3_status",
                        lambda board, args, endpoint: snapshots[endpoint])
    ticks = iter([0.0, 0.01, 0.2])
    monkeypatch.setattr(p3.time, "monotonic", lambda: next(ticks))
    monkeypatch.setattr(p3.time, "sleep", lambda _: None)
    with pytest.raises(RuntimeError, match="loopback completion timeout"):
        p3.run_trial(board, board, 25_000_000, 7, 1, 0, trial_args())
    assert len(stops) == 2


def test_single_board_missing_edges_remain_a_failure(monkeypatch) -> None:
    board = p3.Board("COM3", "uid", "idn", "build")
    monkeypatch.setattr(p3, "stop_p3", lambda *args: None)
    monkeypatch.setattr(p3, "start_p3", lambda *args: {"role": 3})
    snapshots = {1: make_snapshot(1, 9), 2: make_snapshot(2, 2)}
    snapshots[2]["state"] = p3.P3_STATE_ERROR
    snapshots[2]["result_valid"] = 0
    monkeypatch.setattr(p3, "p3_status",
                        lambda board, args, endpoint: snapshots[endpoint])
    result = p3.run_trial(board, board, 25_000_000, 7, 1, 0, trial_args())
    assert not result["passed"]
    assert "responder_edges" in result["failures"]
    assert "responder_result" in result["failures"]


def test_two_boards_keep_responder_first_handshake(monkeypatch) -> None:
    source = p3.Board("COM3", "uid1", "idn1", "build")
    destination = p3.Board("COM4", "uid2", "idn2", "build")
    starts = []
    monkeypatch.setattr(p3, "stop_p3", lambda *args: None)

    def start(board, role, *args):
        starts.append((board.address, role))
        return {"role": role}

    monkeypatch.setattr(p3, "start_p3", start)
    monkeypatch.setattr(p3, "wait_complete_pair", lambda *args: (
        make_snapshot(1, 9), make_snapshot(2, 6)))
    result = p3.run_trial(source, destination, 25_000_000, 7, 1, 0,
                          trial_args())
    assert starts == [("uid2", 2), ("uid1", 1)]
    assert result["endpoint_mode"] == "DISTINCT_BOARDS"
    assert "loopback_start" not in result
    assert result["passed"]


def test_loopback_query_error_still_stops_both_slots(monkeypatch) -> None:
    board = p3.Board("COM3", "uid", "idn", "build")
    stops = []
    monkeypatch.setattr(p3, "stop_p3",
                        lambda boards, args: stops.append(boards))
    monkeypatch.setattr(p3, "start_p3", lambda *args: {"role": 3})

    def status(*args):
        raise OSError("CDC disconnected")

    monkeypatch.setattr(p3, "p3_status", status)
    with pytest.raises(OSError, match="CDC disconnected"):
        p3.run_trial(board, board, 25_000_000, 7, 1, 0, trial_args())
    assert len(stops) == 2


def test_loopback_start_requires_requested_group_acknowledgement(
        monkeypatch) -> None:
    board = p3.Board("COM3", "uid", "idn", "build")
    commands = []
    stale = make_snapshot(3, 15, 1)
    current = make_snapshot(3, 15, 0)
    replies = iter([stale, current])
    monkeypatch.setattr(p3, "board_command", lambda board, command, args:
                        commands.append(command) or "OK")
    monkeypatch.setattr(p3, "p3_status", lambda *args: next(replies))
    monkeypatch.setattr(p3.time, "sleep", lambda _: None)
    ticks = iter([0.0, 0.01, 0.02, 0.03, 0.04])
    monkeypatch.setattr(p3.time, "monotonic", lambda: next(ticks))
    result = p3.start_p3(board, 3, 25_000_000, 7, 0, trial_args())
    assert result is current
    assert commands == ["CALibration:P3:STARt 3,25000000,32,256,7,0"]
    assert result["start_attempt_count"] == 1
