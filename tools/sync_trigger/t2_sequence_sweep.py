"""Sweep the per-position group period of the T2 reservation sequence.

Timing is an INSTALL parameter, not a build parameter: t2_workflow.py turns a period
into each TEMPlate row's delta_q and installs it while the ring is stopped. This tool
therefore scans several periods on one firmware image - no build, no OTA, no flash -
and reports, per follower role, how far the group sequence actually advanced:

  groups exec/count   completed groups vs the mounted group count
  next                group Core0 is preparing for
  in_flight           0xF when no group is in the hardware owner's hands
  done                bitmask of completed groups

Read the verdict together with the window row: a period whose sequences stall at
done=0b1 (or whose positions start failing late/no_plan) is below what the current
single-depth backends can sustain, because a follower observes a group completing and
only then arms the next one. Sustaining tight spacing needs the depth>=2 absolute
deadline queue registered as SYNC-SCHED-001 in the SYNC_IO domain.

Usage:
  python tools/sync_trigger/t2_sequence_sweep.py                      # default periods
  python tools/sync_trigger/t2_sequence_sweep.py --periods 20,50,200
  python tools/sync_trigger/t2_sequence_sweep.py --groups 1,8,8,8 --dry-run
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / "tools" / "sync_trigger" / "t2_workflow.py"

# WIN t+2s slot 1 SWITCH_A ... | win 3/3/3/0/0/0/0 (...) | fail late/... 0/0/0/0/0
#   | groups exec/count 3/8 slotpack next|inflight|done 497/497/497
SAMPLE = re.compile(
    r"slot (\d) (\S+).*?\|\s*win (\S+) \([^)]*\)\s*\|\s*fail \S+ (\S+) "
    r"\|\s*groups exec/count (\d+)/(\d+) "
    r"slotpack next\|inflight\|done (\d+)/(\d+)/(\d+)")
NONE = 0xF


def decode(pack: int) -> tuple[int, int, int]:
    return pack & 0xF, (pack >> 4) & 0xF, (pack >> 8) & 0xFF


def sweep(period: int, groups: str, timeout: float) -> tuple[dict, str]:
    command = [sys.executable, str(WORKFLOW), "--full",
               "--seq-groups", groups, "--seq-period-slices", str(period)]
    print(f"\n=== period {period} fine slices: {' '.join(command[1:])}", flush=True)
    finished = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                              timeout=timeout)
    output = finished.stdout + finished.stderr
    best: dict[tuple[int, str], tuple] = {}
    for (slot, role, win, fail, executed, count,
         pack_next, pack_flight, pack_done) in SAMPLE.findall(output):
        key = (int(slot), role)
        next_group, in_flight, done = decode(int(pack_next))
        previous = best.get(key)
        if previous is None or int(executed) > previous[0]:
            best[key] = (int(executed), int(count), next_group, in_flight, done,
                         len(bin(done).split("1")) - 1, win, fail)
    verdict = ""
    for line in output.splitlines():
        if line.strip().startswith("VERDICT"):
            verdict = line.strip()
    return best, verdict


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--periods", default="20,50,100,200",
                        help="group periods in fine slices (delta_q step per group)")
    parser.add_argument("--groups", default="1,8,8,8",
                        help="groups per role: COUNTER,SWITCH_A,GATEWAY,SWITCH_B")
    parser.add_argument("--timeout", type=float, default=600.0, help="per-run seconds")
    parser.add_argument("--dry-run", action="store_true", help="print the commands only")
    args = parser.parse_args()

    periods = [int(item) for item in args.periods.split(",") if item.strip()]
    if args.dry_run:
        for period in periods:
            print(f"{sys.executable} {WORKFLOW} --full --seq-groups {args.groups} "
                  f"--seq-period-slices {period}")
        return 0

    summary: dict[int, dict] = {}
    for period in periods:
        try:
            summary[period], verdict = sweep(period, args.groups, args.timeout)
        except subprocess.TimeoutExpired:
            print(f"period {period}: TIMEOUT after {args.timeout:.0f}s", flush=True)
            continue
        print(f"period {period:>5}  {verdict}")
        for (slot, role), row in sorted(summary[period].items()):
            (executed, count, next_group, in_flight, done, done_groups,
             win, fail) = row
            flight = "none" if in_flight == NONE else str(in_flight)
            print(f"    slot {slot} {role:<9} groups {executed}/{count}"
                  f"  next {next_group}  in_flight {flight}"
                  f"  done 0b{done:08b} ({done_groups} groups)")
            print(f"        win {win} (entered/announced/frozen/executed/abandoned/...)"
                  f"  fail {fail} (late/expired/no_plan/conflict/backend)")

    print("\n=== summary (groups completed per position) ===")
    header = "period " + "".join(f"{role:>12}" for role in
                                 ("SWITCH_A", "GATEWAY", "SWITCH_B"))
    print(header)
    for period in periods:
        rows = summary.get(period) or {}
        cells = []
        for role in ("SWITCH_A", "GATEWAY", "SWITCH_B"):
            match = [row for (_, name), row in rows.items() if name == role]
            cells.append(f"{match[0][0]}/{match[0][1]}" if match else "-")
        print(f"{period:>6} " + "".join(f"{cell:>12}" for cell in cells))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
