"""Exact-match probe with STOP-only observer snapshot; reuse verified controls."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("exact_match_probe",
    Path(__file__).parent / "dpll-priority-match-r1/capture_match.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

class ObserverProbe(probe.MatchProbe):
    def collect(self):
        assert self.barrier and not self.block_queries
        for board in self.boards:
            self.report["boards"][board.address]["event_snapshot_raw"] = self.command(
                board, "SYSTem:TDMA:EVENt:SNAPshot?")
            self.save()
        super().collect()

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--receipt", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--seconds", type=float, default=3.0)
    p.add_argument("--baseline", type=Path)
    p.add_argument("--tap-baseline", type=Path)
    p.add_argument("--check-only", action="store_true")
    args = p.parse_args()
    if not 2 <= args.seconds <= 8:
        p.error("seconds must be 2..8")
    run = ObserverProbe(args)
    run.report["tool_sources"].append(dict(path=str(Path(__file__)),
        sha256=probe.parent.base.sha(Path(__file__))))
    run.run()
    print(json.dumps(dict(passed=run.report["passed"], errors=run.report["errors"],
        out=str(args.out))))
    raise SystemExit(0 if run.report["passed"] else 2)
