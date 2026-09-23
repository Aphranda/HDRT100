from __future__ import annotations

import sys
from pathlib import Path
from argparse import Namespace

import pytest
from tools.calibration_ring_validate import calibration_clk_coded as coded
from tools.tdma_ring_monitor.tdma_start_ring import Board

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = ROOT / "tools" / "calibration_ring_validate"
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

from calibration_clk_coded import (  # noqa: E402
    CODEBOOK_HALF_CHIP_NS,
    CODED_FIELDS,
    metric_statistics,
    parse_csv_u32,
    reject_reason_name,
    summarize_phase_matrix,
    summarize_trials,
    trial_integrity_category,
)


def test_parse_coded_snapshot_fields() -> None:
    raw = ",".join(str(index) for index in range(len(CODED_FIELDS)))
    parsed = parse_csv_u32(raw, len(CODED_FIELDS))
    assert parsed["version"] == 0
    assert parsed["state"] == 1
    assert parsed["pio_stall_count"] == len(CODED_FIELDS) - 1
    assert CODEBOOK_HALF_CHIP_NS == {0: 20, 1: 40, 2: 24, 3: 32}


def test_parse_coded_snapshot_rejects_wrong_field_count() -> None:
    try:
        parse_csv_u32("1,2,3", len(CODED_FIELDS))
    except RuntimeError as exc:
        assert "field count" in str(exc)
    else:
        raise AssertionError("wrong coded snapshot field count accepted")


def make_trial(sequence: int, lag: int, distance: int, margin: int,
               *, state: int = 3, reason: int = 0,
               phase: int = 10, reference_node: int = 0) -> dict[str, object]:
    board_id = int("0010071E65B5CB38", 16)
    build_id = 20260901021308
    return {
        "reference_node": reference_node,
        "reference_node_id": "0010071E65B5CB38",
        "reference_build_id": str(build_id),
        "probe_phase_cycles": phase,
        "snapshot": {
            "state": state,
            "reject_reason": reason,
            "board_id_lo": board_id & 0xFFFFFFFF,
            "board_id_hi": board_id >> 32,
            "build_id_lo": build_id & 0xFFFFFFFF,
            "build_id_hi": build_id >> 32,
            "local_node": reference_node,
            "marker_flags": 0x3F,
            "dma_overrun_count": 0,
            "pio_stall_count": 0,
            "capture_origin_lo": 1,
            "capture_origin_hi": 0,
            "train_sequence": sequence,
            "train_epoch": sequence & 0xFF,
            "calibration_generation": sequence,
            "topology_generation": 1,
            "topology_crc32": 2,
            "profile_crc32": 3,
            "schedule_crc32": 4,
            "best_lag_sample": lag,
            "best_distance": distance,
            "second_distance": distance + margin,
            "margin": margin,
        },
    }


def test_metric_statistics_uses_nearest_rank_p99() -> None:
    result = metric_statistics([1, 2, 3, 4])
    assert result == {
        "count": 4,
        "min": 1,
        "max": 4,
        "mean": 2.5,
        "p99": 4,
        "stddev": pytest.approx(1.118033988749895),
    }


def test_summarize_trials_reports_repeat_distribution() -> None:
    trials = [
        make_trial(10, 100, 20, 400),
        make_trial(11, 101, 24, 390),
        make_trial(12, 100, 22, 395),
    ]
    result = summarize_trials(trials, max_reject_ratio=0.0, max_lag_span=1)
    assert result["passed"] is True
    assert result["accepted_count"] == 3
    assert result["lag_histogram"] == {"100": 2, "101": 1}
    assert result["lag_span"] == 1
    assert result["best_distance"]["mean"] == 22.0
    assert result["margin"]["min"] == 390


def test_summarize_trials_classifies_reject_and_sequence_failure() -> None:
    trials = [
        make_trial(10, 100, 20, 400),
        make_trial(10, 100, 0, 0, state=4, reason=0x106),
    ]
    result = summarize_trials(trials, max_reject_ratio=0.0, max_lag_span=0)
    assert result["passed"] is False
    assert result["reject_categories"] == {
        "accepted": 1,
        "correlation_manchester": 1,
    }
    assert "reject_ratio" in result["gate_failures"]
    assert reject_reason_name(0x10B) == "correlation_distance"


def test_phase_matrix_eliminates_mixed_candidate_without_failing_stage() -> None:
    trials = [
        make_trial(10, 100, 20, 200, phase=8),
        make_trial(11, 100, 20, 0, phase=9),
        make_trial(12, 100, 24, 390, phase=10),
    ]
    result = summarize_phase_matrix(
        trials, max_reject_ratio=0.0, max_lag_span=0)["0"]
    assert result["phase_candidates"]["9"]["passed"] is False
    assert result["phase_candidates"]["9"]["gate_failures"] == ["mixed_peak"]
    assert result["selected_probe_phase_cycles"] == 10
    assert result["passed"] is True


def test_phase_matrix_selection_is_worst_case_and_deterministic() -> None:
    trials = [
        make_trial(10, 100, 20, 300, phase=8),
        make_trial(11, 100, 22, 350, phase=8),
        make_trial(12, 100, 19, 300, phase=9),
        make_trial(13, 100, 21, 350, phase=9),
    ]
    result = summarize_phase_matrix(
        trials, max_reject_ratio=0.0, max_lag_span=0)["0"]
    assert result["selected_probe_phase_cycles"] == 9

    trials.extend([
        make_trial(14, 100, 19, 300, phase=7),
        make_trial(15, 100, 21, 350, phase=7),
    ])
    result = summarize_phase_matrix(
        trials, max_reject_ratio=0.0, max_lag_span=0)["0"]
    assert result["selected_probe_phase_cycles"] == 7


def test_phase_matrix_fails_only_when_no_candidate_is_eligible() -> None:
    trials = [
        make_trial(10, 100, 20, 0, phase=8),
        make_trial(11, 100, 20, 0, phase=9),
    ]
    result = summarize_phase_matrix(
        trials, max_reject_ratio=0.0, max_lag_span=0)["0"]
    assert result["selected_probe_phase_cycles"] is None
    assert result["passed"] is False


def test_integrity_gate_is_independent_of_correlation_quality() -> None:
    correlation_reject = make_trial(
        10, 100, 20, 0, state=4, reason=0x10C)
    assert trial_integrity_category(correlation_reject) == "complete"
    correlation_reject["snapshot"]["dma_overrun_count"] = 1
    assert trial_integrity_category(correlation_reject) == "dma_overrun"


def test_integrity_gate_rejects_wrong_physical_identity() -> None:
    trial = make_trial(10, 100, 20, 300)
    trial["snapshot"]["board_id_lo"] ^= 1
    assert trial_integrity_category(trial) == "board_identity"


def test_unselected_integrity_failure_does_not_fail_phase_matrix() -> None:
    bad = make_trial(10, 100, 20, 300, phase=8)
    bad["snapshot"]["pio_stall_count"] = 1
    good = make_trial(11, 100, 20, 300, phase=9)
    result = summarize_phase_matrix(
        [bad, good], max_reject_ratio=0.0, max_lag_span=0)["0"]
    assert result["phase_candidates"]["8"]["integrity_complete"] is False
    assert "integrity" in result["phase_candidates"]["8"]["gate_failures"]
    assert result["selected_probe_phase_cycles"] == 9
    assert result["passed"] is True


@pytest.mark.parametrize('field,value,category', [
    ('train_epoch', 11, 'epoch_identity'),
    ('calibration_generation', 9, 'calibration_generation'),
    ('topology_generation', 0, 'topology_generation'),
    ('marker_flags', 0, 'marker_flags'),
    ('local_node', 1, 'local_node_identity'),
])
def test_single_board_keeps_epoch_identity_and_edge_gates(field, value, category):
    trial = make_trial(10, 13, 20, 300)
    trial['snapshot'][field] = value
    assert trial_integrity_category(trial) == category
    assert not summarize_trials([trial], 0.0, None)['passed']


def test_single_board_coded_runs_reference_once_and_stops(monkeypatch):
    board = Board('COM3', '0010071E65B5CB38', '', '20260901021308')
    calls = []
    monkeypatch.setattr(coded, 'coded_status', lambda *_: {'train_sequence': 9})
    monkeypatch.setattr(coded, 'start_coded', lambda actual, _: calls.append(actual) or 'OK')
    def wait(actual, _args, *, previous_sequence):
        assert actual is board and previous_sequence == 9
        return make_trial(10, 13, 20, 300)['snapshot']
    monkeypatch.setattr(coded, 'wait_reference_node', wait)
    stopped = []
    monkeypatch.setattr(coded, 'stop_coded', lambda boards, _: stopped.extend(boards))
    result = coded.run_reference_node([board], 0, 1, False, Namespace(probe_phase_cycles=10))
    assert calls == [board] and stopped == [board]
    assert result['calibration_topology']['mode'] == 'SINGLE_BOARD_DUAL_SLOT'
    assert result['accepted'] and trial_integrity_category(result) == 'complete'
    assert len(result['started']) == 1


def test_single_board_prepare_failure_keeps_actions_and_cleans_up(monkeypatch):
    board = Board('COM3', '0010071E65B5CB38', '', '20260901021308')
    def prepare(_boards, _node, _args, actions):
        actions.append({'command': 'TOPOLOGY', 'response': 'bad'})
        raise RuntimeError('owner rejected topology')
    monkeypatch.setattr(coded, 'prepare_ring', prepare)
    stopped = []
    monkeypatch.setattr(coded, 'stop_coded', lambda boards, _: stopped.extend(boards))
    monkeypatch.setattr(coded.coarse, 'wait_ring_stopped', lambda *_: {})
    cleanup = []
    monkeypatch.setattr(coded.coarse, '_control_command',
                        lambda _b, command, *_a, **_k: cleanup.append(command))
    result = coded.run_reference_node([board], 0, 1, True, Namespace(probe_phase_cycles=10))
    assert not result['accepted'] and 'owner rejected topology' in result['error']
    assert result['actions'][0]['response'] == 'bad'
    assert stopped == [board]
    assert cleanup == ['SYSTem:TDMA:RING:STOP', 'CALibration:TOPology:PROBe 0']


def test_wait_coded_ignores_stale_terminal_sequence(monkeypatch):
    responses = iter([{'state': 3, 'train_sequence': 9},
                      {'state': 3, 'train_sequence': 10}])
    monkeypatch.setattr(coded, 'coded_status', lambda *_: next(responses))
    monkeypatch.setattr(coded.time, 'sleep', lambda _: None)
    result = coded.wait_reference_node(Board('P', 'A', '', ''),
        Namespace(coded_timeout=1., poll_interval=.02), previous_sequence=9)
    assert result['train_sequence'] == 10


def test_coded_start_timeout_cannot_reuse_old_matching_snapshot(monkeypatch):
    snapshot = {'state': 3, 'codebook_id': 1, 'coarse_min_sample': 0,
                'coarse_max_sample': 255, 'train_sequence': 9}
    monkeypatch.setattr(coded, 'coded_status', lambda *_: snapshot)
    commands = []
    def command(_board, text, _args):
        commands.append(text)
        return '0,"No error"' if text == 'SYSTem:ERR?' else '<timeout>'
    monkeypatch.setattr(coded, 'board_command', command)
    args = Namespace(timeout=1., action_timeout=.1, settle=0., codebook=1,
                     min_lag=0, max_lag=255, max_distance=512, min_margin=0,
                     coded_timeout=.01, poll_interval=.001, gap=0.)
    with pytest.raises(RuntimeError, match='START rejected after 3 attempts'):
        coded.start_coded(Board('P', 'A', '', ''), args)
    assert sum('CODEd:STARt' in text for text in commands) == 3


@pytest.mark.parametrize('field,value,category', [
    ('build_id_lo', 0, 'build_identity'),
    ('train_sequence', 0, 'stale_sequence'),
])
def test_coded_rejects_missing_generation_or_wrong_build(field, value, category):
    trial = make_trial(10, 13, 20, 300)
    trial['snapshot'][field] = value
    assert trial_integrity_category(trial) == category


@pytest.mark.parametrize('board_ids,minimum,maximum,expected,source', [
    (['A'], None, None, (0, 255), 'single_board_bounded_search'),
    (['A', 'B'], None, None, (80, 120), 'multi_board_default'),
    (['A'], 4, 31, (4, 31), 'explicit_cli'),
    (['A'], None, 31, (0, 31), 'explicit_cli'),
])
def test_lag_window_is_bounded_and_respects_explicit_arguments(
        board_ids, minimum, maximum, expected, source):
    args = Namespace(board_id=board_ids, min_lag=minimum, max_lag=maximum)
    assert coded.resolve_lag_window(args) == source
    assert (args.min_lag, args.max_lag) == expected
