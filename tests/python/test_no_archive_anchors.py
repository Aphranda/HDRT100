"""Ratchet against re-anchoring host tests on archived ``.inc`` fragments.

``*_inc_split_v1`` archives hold the pre-split audit copies.  They are not
built and must never become a test's fact source again: a harness that reads
them stays green while the shipped code moves on.  The allowlist below is the
N5.0.11 migration backlog; it may only shrink.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent

# Built at run time so this file does not match its own scan.
ANCHOR = "_inc" + "_split_v1"

# N5.0.11d backlog: VDC harnesses still anchored on the audit copy.
KNOWN_ANCHORS = {
    "test_refmem_priority_health.py",
    "test_vdc_boundary_owner.py",
    "test_vdc_command_owner.py",
    "test_vdc_feedback_match_integration.py",
    "test_vdc_fixed_output.py",
    "test_vdc_idle_maintenance.py",
    "test_vdc_priority_follow.py",
    "test_vdc_priority_follow_config.py",
    "test_vdc_priority_match.py",
    "test_vdc_priority_phase.py",
    "test_vdc_priority_trace.py",
    "test_vdc_run_output.py",
    "test_vdc_run_timeline.py",
}


def _anchored(directory: Path = TESTS) -> set[str]:
    found: set[str] = set()
    for path in sorted(Path(directory).glob("test_*.py")):
        if path.name == Path(__file__).name:
            continue
        if ANCHOR in path.read_text(encoding="utf-8"):
            found.add(path.name)
    return found


def test_detector_flags_a_planted_anchor(tmp_path):
    (tmp_path / "test_planted.py").write_text(
        f'ARCHIVE = "components/x/archive/x{ANCHOR}/a.inc"\n', encoding="utf-8")
    (tmp_path / "test_clean.py").write_text("VALUE = 1\n", encoding="utf-8")
    assert _anchored(tmp_path) == {"test_planted.py"}


def test_no_new_test_anchors_on_archived_fragments():
    new = _anchored() - KNOWN_ANCHORS
    assert not new, f"new archive anchors: {sorted(new)}"


def test_backlog_only_shrinks():
    stale = KNOWN_ANCHORS - _anchored()
    assert not stale, (
        "these files no longer anchor the archive; drop them from KNOWN_ANCHORS "
        f"to keep the ratchet tight: {sorted(stale)}")
