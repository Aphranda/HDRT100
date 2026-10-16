"""Sweep the ANNOUNCEMENT LEAD of the T2 reservation scan on one firmware image.

Why this tool exists
--------------------
FREEZE is supposed to give the follower enough time to act: the position is
announced, frozen and only then armed at its own absolute slice. The lead is not a
firmware constant, it is scan configuration: `T2:CONFigure ... threshold ...` sets the
announcement point (1000 - threshold) per mille of one position interval before the
target, and the lead in TIME is that fraction of the position interval. Everything
after the announcement (lane transport, decode, FREEZE admission, Core0 solve, Core1
arm) has to fit inside that budget, so a too small lead shows up as
TRIGGER_T2_ACTION_FAIL_LATE and nothing else.

This tool re-runs the scan (install + collect only - never a build, never an OTA) for
several thresholds and reports, per follower role:

  executed/positions   window executed total
  late                 action_fail(LATE) - the whole residual loss in the 2026-10-16 run
  abandoned            window slots released without execution
  freeze resend        FREEZE latched per position (>1 means the window is hand-tight)
  lead                lead pulses (target - threshold) and its estimate in ms

Usage:
  python tools/sync_trigger/t2_declare_lead_sweep.py --thresholds 950,900,800,600
  python tools/sync_trigger/t2_declare_lead_sweep.py --positions 48 --sample-ticks 12
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / "tools" / "sync_trigger" / "t2_workflow.py"

SAMPLE = re.compile(
    r"WIN t\+(\d+)s slot (\d) (\S+).*?\|\s*win (\S+) \([^)]*\)\s*\|\s*fail \S+ (\S+) ")
COUNTER = re.compile(r"t\+(\d+)s counter loaded \d+ count (\d+)")
FREEZE = re.compile(r"FREEZE latched (\d+) kept (\d+) taken (\d+)")
FREEZE_SLOT = re.compile(r"POSITION slot (\d)")
LEAD = re.compile(r"targets \[([^\]]*)\] thresholds \[([^\]]*)\]")


def run(threshold: int, positions: int, sample_ticks: int, timeout: float) -> dict:
    env = dict(os.environ)
    # The scan's FIRST position sits at 0, so N positions need a terminal of
    # (N-1) x step: passing N x 1000 scanned N+1 positions, which is why a
    # "--positions 361" run reported executed 362 (361 + 1 abandoned).
    terminal_mdeg = (positions - 1) * 1000
    env.update({
        "T2_SCAN_OVERRIDE_TERMINAL_MDEG": str(terminal_mdeg),
        "T2_SCAN_OVERRIDE_START_MDEG": "0",
        "T2_SCAN_OVERRIDE_STEP_MDEG": "1000",
        "T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE": str(threshold),
        "T2_SAMPLE_TICKS": str(sample_ticks),
    })
    for name in ("T2_SEQ_GROUPS", "T2_SEQ_PERIOD_SLICES"):
        env.pop(name, None)
    command = [sys.executable, str(WORKFLOW), "--full"]
    print(f"\n=== threshold {threshold} (lead {(1000 - threshold) / 10.0:.1f}% of one "
          f"position interval): {' '.join(command[1:])}", flush=True)
    finished = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                              text=True, timeout=timeout)
    output = finished.stdout + finished.stderr
    result: dict = {"threshold": threshold, "boards": {}, "lead_pulses": None,
                    "rate_pps": None, "verdict": ""}
    counts: list[tuple[int, int]] = []
    for seconds, value in COUNTER.findall(output):
        counts.append((int(seconds), int(value)))
    if len(counts) >= 2:
        (t0, c0), (t1, c1) = counts[0], counts[-1]
        if t1 > t0:
            result["rate_pps"] = (c1 - c0) / float(t1 - t0)
    for targets, thresholds in LEAD.findall(output):
        target_values = [int(v) for v in re.findall(r"\d+", targets)]
        threshold_values = [int(v) for v in re.findall(r"\d+", thresholds)]
        leads = [t - h for t, h in zip(target_values, threshold_values)]
        result["observed_lead"] = max(leads) if leads else None
    # The lead is a property of the REQUEST, not of the sample we happen to read: the
    # override command uses a 1000-pulse step, so (1000 - threshold) per mille is that
    # many pulses. Parsing it from the runtime page picked up partially consumed
    # targets and reported 50 pulses for the 900 run.
    result["lead_pulses"] = 1000 - threshold
    for line in output.splitlines():
        if line.strip().startswith("VERDICT"):
            result["verdict"] = line.strip()
        for seconds, slot, role, win, fail in SAMPLE.findall(line):
            window = [v.strip() for v in win.split("/")]
            reasons = [v.strip() for v in fail.split("/")]
            result["boards"][int(slot)] = {
                "role": role, "entered": window[0], "executed": window[3],
                "abandoned": window[4], "late": reasons[0],
                "seconds": int(seconds), "fail": fail,
            }
        freeze_slot = FREEZE_SLOT.search(line)
        for latched, kept, taken in FREEZE.findall(line):
            if freeze_slot is not None and int(freeze_slot.group(1)) in result["boards"]:
                result["boards"][int(freeze_slot.group(1))]["freeze_latched"] = int(latched)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--thresholds", default="950,900,800,600",
                        help="announce thresholds in per mille (lead = 1000 - threshold)")
    parser.add_argument("--positions", type=int, default=48,
                        help="scan positions (terminal = (positions-1) x 1000 mdeg, so "
                             "N means exactly N positions: the first sits at 0)")
    parser.add_argument("--sample-ticks", type=int, default=12)
    parser.add_argument("--timeout", type=float, default=420.0)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    thresholds = [int(v) for v in args.thresholds.split(",") if v.strip()]
    if args.dry_run:
        for threshold in thresholds:
            print(f"T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE={threshold} "
                  f"T2_SCAN_OVERRIDE_TERMINAL_MDEG={args.positions * 1000} "
                  f"T2_SAMPLE_TICKS={args.sample_ticks} python {WORKFLOW} --full")
        return 0

    results = []
    for threshold in thresholds:
        try:
            results.append(run(threshold, args.positions, args.sample_ticks, args.timeout))
        except subprocess.TimeoutExpired:
            print(f"threshold {threshold}: TIMEOUT after {args.timeout:.0f}s", flush=True)

    for result in results:
        lead = result["lead_pulses"]
        rate = result["rate_pps"]
        lead_ms = (lead / rate * 1000.0) if (lead and rate) else None
        print(f"\nthreshold {result['threshold']}  lead {lead} pulses"
              + (f" ≈ {lead_ms:.1f} ms" if lead_ms else " (rate unknown)")
              + f"  {result['verdict']}")
        for slot, board in sorted(result["boards"].items()):
            latched = board.get("freeze_latched")
            per_position = (f"{latched / max(1, args.positions):.2f}x/pos"
                            if latched else "?")
            print(f"    slot {slot} {board['role']:<9} executed {board['executed']}"
                  f"  late {board['late']}  abandoned {board['abandoned']}"
                  f"  freeze {per_position}")

    print(f"\n=== summary over {args.positions} positions ===")
    print("threshold  lead_pulses  lead_ms  " + "".join(
        f"{role:>22}" for role in ("SWITCH_A", "GATEWAY", "SWITCH_B")))
    for result in results:
        lead = result["lead_pulses"]
        rate = result["rate_pps"]
        lead_ms = (lead / rate * 1000.0) if (lead and rate) else 0.0
        cells = []
        for role in ("SWITCH_A", "GATEWAY", "SWITCH_B"):
            board = next((b for b in result["boards"].values() if b["role"] == role), None)
            cells.append(f"{board['executed']}/{board['late']}" if board else "-")
        delivered = any(b["executed"] not in ("0", "?") for b in result["boards"].values())
        marker = "" if delivered else "   <void: no delivery this run>"
        print(f"{result['threshold']:>9} {str(lead):>12} {lead_ms:>7.1f}  "
              + "".join(f"{cell:>22}" for cell in cells) + marker)
    print("\n(executed/late per role; the sweep finds the lead at which late reaches 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
