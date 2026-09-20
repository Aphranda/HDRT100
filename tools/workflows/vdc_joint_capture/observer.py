"""Tracked STOP-only observer snapshot layer over the match ancestry."""

from __future__ import annotations

import _imp
import hashlib
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace

from tools.domains.vdc.priority_trace import probe_support as tracked_probe_support


SNAPSHOT = "SYSTem:TDMA:EVENt:SNAPshot?"


def _load_match(path: Path):
    spec = importlib.util.spec_from_file_location("vdc_joint_match_base", path)
    if spec is None or spec.loader is None:
        raise ValueError("Historical match entrypoint cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    missing = object()
    _imp.acquire_lock()
    previous = sys.modules.get("probe_support", missing)
    try:
        sys.modules["probe_support"] = tracked_probe_support
        try:
            spec.loader.exec_module(module)
            return module
        finally:
            if previous is missing:
                sys.modules.pop("probe_support", None)
            else:
                sys.modules["probe_support"] = previous
    finally:
        _imp.release_lock()


def load_observer(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked observer layer over an admitted MatchProbe module."""
    match = runtime_module if runtime_module is not None else _load_match(runtime_entrypoint)
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
