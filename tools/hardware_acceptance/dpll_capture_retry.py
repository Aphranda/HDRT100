"""Step-29 retry policy owned by the hardware acceptance workflow.

The workflow, not a step implementation, decides whether a failed DPLL joint
capture gets a fresh 60 s window. Deterministic admission/configuration
failures (role, receipt, source fingerprint, manifest) must fail once: the
historical failure retried them, spent two full windows and still ended the
whole run without a credential.

Only evidence that the capture actually started and that no follower is stuck
on the ROLE (4) or SOURCE (9) gate with zero matches remains retryable.
"""
from __future__ import annotations

import json
from pathlib import Path

# Matcher skip reasons (components/vdc_dpll_manager/inc/vdc_priority_match.h).
MATCH_ROLE = 4
MATCH_SOURCE = 9
DETERMINISTIC_MARKERS = (
    "STARTUP_CONFIGURATION",
    "provisional receipt",
    "source fingerprint",
    "migrated source",
)


def match_counts(raw: str) -> tuple[int, int] | None:
    """Return (matched, first_reject_reason) from a MATCH:STATus? page."""
    try:
        fields = [int(value) for value in str(raw).split(",")]
    except ValueError:
        return None
    if len(fields) < 7:
        return None
    # The page is append-only: first_reject_reason precedes anchor_fallback in
    # the current trailer, while legacy 63-field pages end at first_reject.
    first = fields[-2] if len(fields) >= 64 else fields[-1]
    return fields[6], first


def capture_failure_is_retryable(joint_dir: Path, message: str) -> bool:
    """Whether a failed joint capture can be retried in a fresh window."""
    text = str(message)
    if any(marker in text for marker in DETERMINISTIC_MARKERS):
        return False
    probe_path = joint_dir / "capture" / "input-probe.json"
    if not probe_path.is_file():
        return True
    try:
        probe = json.loads(probe_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return True
    if (probe.get("startup_preflight") or {}).get("passed") is False:
        return False
    uids = list((probe.get("workflow_inputs") or {}).get("board_uids") or [])
    boards = probe.get("boards") or {}
    followers = ([boards[uid] for uid in uids[1:] if uid in boards]
                 if uids else list(boards.values()))
    counts = [match_counts(str(row.get("match") or "")) for row in followers]
    counts = [item for item in counts if item is not None]
    if counts and all(matched == 0 and first in (MATCH_ROLE, MATCH_SOURCE)
                      for matched, first in counts):
        return False
    return True


def newest_capture_dir(out_dir: Path, *, exclude=()) -> Path | None:
    """Newest `dpll-four-board-short*` attempt directory not in ``exclude``."""
    excluded = {Path(path) for path in exclude}
    candidates = sorted(
        (path for path in out_dir.glob("dpll-four-board-short*")
         if path.is_dir() and path not in excluded),
        key=lambda path: path.stat().st_mtime, reverse=True)
    return candidates[0] if candidates else None


def step_29_is_retryable(run, error: BaseException, before=()) -> bool:
    """Workflow-side decision for one failed step-29 attempt.

    ``before`` is the attempt-directory set observed before the step ran, so a
    stale directory from an earlier invocation can never decide this attempt.
    """
    joint_dir = newest_capture_dir(run.out_dir, exclude=before)
    if joint_dir is None:
        return False
    return capture_failure_is_retryable(joint_dir, str(error))
