"""Tracked STOP-only observer snapshot layer over the match ancestry."""

from __future__ import annotations

import hashlib
from pathlib import Path
from types import SimpleNamespace

from tools.workflows.vdc_joint_capture.match import load_match


SNAPSHOT = "SYSTem:TDMA:EVENt:SNAPshot?"


def load_observer(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked observer layer over an admitted MatchProbe module."""
    match = (runtime_module if runtime_module is not None else
             load_match(runtime_entrypoint))
    if not hasattr(match, "MatchProbe") or not hasattr(match, "parent"):
        raise ValueError("Historical match entrypoint lacks MatchProbe/parent")
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    class ObserverProbe(match.MatchProbe):
        def collect(self):
            if not self.barrier or self.block_queries:
                raise RuntimeError("Observer snapshot requires all-board STOP")
            for board in self.boards:
                self.report["boards"][board.address]["event_snapshot_raw"] = \
                    self.command(board, SNAPSHOT)
                self.save()
            super().collect()

    return SimpleNamespace(
        __file__=str(source),
        ObserverProbe=ObserverProbe,
        probe=match,
        SNAPSHOT=SNAPSHOT,
        source_path=source,
        source_sha256=source_sha256,
    )
