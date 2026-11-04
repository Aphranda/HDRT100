"""Offline checks of the workflow-owned step-29 retry policy."""
import json
import os
import sys
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "hardware_acceptance"))
import dpll_capture_retry as retry  # noqa: E402


def match_page(*, matched=0, first=0, fields=64):
    values = [0] * fields
    values[6] = matched
    if fields >= 64:
        values[-2] = first
    else:
        values[-1] = first
    return ",".join(map(str, values))


def write_probe(folder: Path, *, preflight=True, followers=(), uids=None):
    capture = folder / "capture"
    capture.mkdir(parents=True)
    boards = {}
    uids = uids or ["origin", *[f"f{index}" for index in range(len(followers))]]
    boards[uids[0]] = {"match": match_page(fields=64)}
    for uid, (matched, first) in zip(uids[1:], followers):
        boards[uid] = {"match": match_page(matched=matched, first=first)}
    (capture / "input-probe.json").write_text(json.dumps({
        "startup_preflight": {"passed": preflight},
        "workflow_inputs": {"board_uids": uids},
        "boards": boards,
    }), encoding="utf-8")
    return folder


def test_match_counts_accepts_current_and_legacy_trailers(tmp_path):
    assert retry.match_counts(match_page(matched=7, first=4)) == (7, 4)
    assert retry.match_counts(match_page(matched=8, first=9, fields=63)) == (8, 9)
    assert retry.match_counts("-200,\"requires STOP\"") is None


def test_missing_probe_stays_retryable(tmp_path):
    assert retry.capture_failure_is_retryable(tmp_path, "Capture exited 2")


def test_deterministic_markers_are_not_retried(tmp_path):
    for message in ("STARTUP_CONFIGURATION: role", "provisional receipt source fingerprint differs",
                    "migrated source run_output sha256 mismatch", "invalid P3 joint provisional receipt"):
        assert not retry.capture_failure_is_retryable(tmp_path, message), message


def test_failed_startup_preflight_is_not_retried(tmp_path):
    write_probe(tmp_path, preflight=False, followers=[(0, 0), (0, 0), (0, 0)])
    assert not retry.capture_failure_is_retryable(tmp_path, "Capture exited 2")


def test_zero_match_role_and_source_are_not_retried(tmp_path):
    for first in (retry.MATCH_ROLE, retry.MATCH_SOURCE):
        folder = tmp_path / f"first-{first}"
        write_probe(folder, followers=[(0, first), (0, first), (0, first)])
        assert not retry.capture_failure_is_retryable(folder, "Capture exited 2")


def test_partial_match_is_retryable(tmp_path):
    write_probe(tmp_path, followers=[(6034, 9), (6173, 9), (6289, 9)])
    assert retry.capture_failure_is_retryable(tmp_path, "Capture exited 2")


def test_step_29_uses_the_newest_attempt(tmp_path):
    old = write_probe(tmp_path / "dpll-four-board-short",
                      preflight=False, followers=[(0, 4), (0, 4), (0, 4)])
    os.utime(old, (1, 1))
    new = write_probe(tmp_path / "dpll-four-board-short-attempt2")
    run = SimpleNamespace(out_dir=tmp_path)
    # The newest attempt has no preflight rejection and no match page data.
    assert retry.step_29_is_retryable(
        run, RuntimeError("Capture exited 2"), before={old})
    (new / "capture" / "input-probe.json").write_text(json.dumps({
        "startup_preflight": {"passed": True},
        "workflow_inputs": {"board_uids": ["origin", "f1", "f2", "f3"]},
        "boards": {"origin": {"match": match_page()},
                   "f1": {"match": match_page(first=4)},
                   "f2": {"match": match_page(first=4)},
                   "f3": {"match": match_page(first=4)}},
    }), encoding="utf-8")
    assert not retry.step_29_is_retryable(
        run, RuntimeError("Capture exited 2"), before={old})
    # A stale directory must never decide a later attempt.
    assert not retry.step_29_is_retryable(
        run, RuntimeError("Capture exited 2"), before={old, new})
    assert old.is_dir()
