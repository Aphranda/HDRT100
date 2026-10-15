"""One-command T2 iteration loop: flash the change, run the T2 workflow, judge.

Process ruling (2026-10-15, T2-DEPLOY-003): a batch is iterated by flashing and
running T2, NOT by paying a P3 per change - P3 covers the DPLL/joint capture and
never proves a T2 behaviour. The full `--profile full` P3 runs once, at batch
close, after the function is verified here.

Usage:
    python tools/sync_trigger/t2_iterate.py            # flash + run + judge
    python tools/sync_trigger/t2_iterate.py --no-flash # only run + judge
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FLASH = [sys.executable, "tools/hardware_acceptance/hardware_acceptance_workflow.py",
         "--steps", "0-7"]
T2 = [sys.executable, "tools/sync_trigger/t2_workflow.py", "--full"]

# Step-1 judgement fields, read from the tool's own output.
WANT = ("WIN t+", "POSITION distribution", "ACTION execution", "failed steps")


def run(command: list[str]) -> int:
    print("+", " ".join(command[1:]), flush=True)
    return subprocess.call(command, cwd=str(ROOT))


def judge(text: str) -> int:
    """Report the Step-1 criteria; returns a process exit code."""
    problems: list[str] = []

    # 1) Followers must consume beyond the legacy three slots. The masks printed
    #    in the WIN line (rt frozen / act armed) are 3-bit and saturate at 0b111,
    #    so they can never answer this; the window cumulative totals appended to
    #    T2:ACTion? can, and the driver prints them as "win a/b/c/d/e"
    #    (entered/announced/frozen/executed/abandoned).
    win: dict[str, tuple[int, int]] = {}
    failed_masks: dict[str, int] = {}
    for line in text.splitlines():
        slot = re.search(r"WIN t\+\d+s slot (\d+)", line)
        if slot:
            got = re.search(r"win (\d+)/(\d+)/(\d+)/(\d+)/(\d+)", line)
            if got:
                frozen, executed = int(got.group(3)), int(got.group(4))
                prev = win.get(slot.group(1), (0, 0))
                win[slot.group(1)] = (max(prev[0], frozen), max(prev[1], executed))
        act = re.search(r"ACTION slot \d+ \S+ [0-9A-Fa-f]+ armed \d+ completed \d+"
                        r" failed (\d+)", line)
        if act:
            key = line.split()[2]
            failed_masks[key] = max(failed_masks.get(key, 0), int(act.group(1)))
    for slot in sorted(s for s in win if s != "0"):
        frozen, executed = win[slot]
        moved = frozen > 3 or executed > 3
        print(f"  slot {slot}: window frozen {frozen} executed {executed}"
              f" {'OK (beyond the legacy 3)' if moved else 'FAIL (not beyond the legacy 3)'}")
        if not moved:
            problems.append(f"slot {slot} window frozen {frozen}/executed {executed}"
                            " (not beyond the legacy 3)")
    # Pre-existing action failures are their own item (see VDC-PROGRESS-20261015-058);
    # they are reported here but are NOT part of the Step-1 criterion.
    for role in sorted(failed_masks):
        print(f"  note: {role} action failed mask = {failed_masks[role]}"
              " (pre-existing item, not a Step-1 criterion)")
    # 2) action execution must cover every target role.
    for line in text.splitlines():
        match = re.search(r"ACTION execution: (\d+) of (\d+)", line)
        if match:
            done, total = int(match.group(1)), int(match.group(2))
            followers = total - 1            # COUNTER never executes locally
            print(f"  ACTION execution {done}/{total}"
                  f" {'OK' if done >= followers else 'FAIL (a target role starved)'}")
            if done < followers:
                problems.append(f"ACTION execution {done}/{total}")

    # 3) the workflow itself must stay green.
    if re.search(r"failed steps:\s*none", text):
        print("  T2 workflow: failed steps none OK")
    else:
        match = re.search(r"failed steps:\s*\[([^\]]*)\]", text)
        problems.append(f"T2 workflow failed steps: {match.group(1) if match else '?'}")
        print(f"  T2 workflow: FAILED STEPS {match.group(1) if match else '?'}")

    if problems:
        print("\nVERDICT: not yet - " + "; ".join(problems))
        return 1
    print("\nVERDICT: Step-1 criteria met on this run"
          " (followers consume past the legacy 3, every target role acts, workflow green)")
    print("Next: run the full P3 once for the batch credential, then commit.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-flash", action="store_true",
                        help="skip the build+OTA step and only run T2 + judge")
    args = parser.parse_args()
    if not args.no_flash and run(FLASH) != 0:
        print("flash step failed; not running T2")
        return 1
    completed = subprocess.run(T2, cwd=str(ROOT), capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
    text = completed.stdout + completed.stderr
    keep = [line for line in text.splitlines() if any(w in line for w in WANT)]
    print("\n".join(keep[-40:]), flush=True)
    if completed.returncode != 0 and not keep:
        print("T2 workflow returned", completed.returncode)
    return judge(text) if completed.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
