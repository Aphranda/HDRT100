"""Ratchet against re-anchoring host tests on archived ``.inc`` fragments.

``*_inc_split_v1`` archives hold the pre-split audit copies.  They are not
built and must never become a test's fact source again: a harness that reads
them stays green while the shipped code moves on.  The allowlist below is the
N5.0.11 migration backlog; it may only shrink.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent

# Built at run time so this file does not match its own scan.
ANCHOR = "_inc" + "_split_v1"

# N5.0.11d backlog: VDC harnesses still anchored on the audit copy.
KNOWN_ANCHORS: set[str] = set()


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


SRC_FRAGMENT = re.compile(r"components/[\w/]+/src/[\w]+\.inc")


def test_no_test_reads_a_component_src_fragment():
    """``src/*.inc`` fragments were archived into the split audit copies."""
    offenders = sorted(
        path.name for path in TESTS.glob("test_*.py")
        if path.name != Path(__file__).name
        and SRC_FRAGMENT.search(path.read_text(encoding="utf-8")))
    assert not offenders, f"tests still read src/*.inc: {offenders}"
