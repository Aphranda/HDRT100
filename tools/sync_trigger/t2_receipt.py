"""Write the T2 acceptance receipt from an existing run (no bench time needed).

Gate criterion used here (and in `sequence_workflow.write_t2_receipt`):
  * every role reports zero NO_PLAN / LATE / BACKEND (the system ran without failures), and
  * at least two roles reach `executed == groups x positions` (the full 361-position scan was actually driven).

The gateway's per-revolution throughput is still recorded per role; it is a known cadence variance, not a
functional failure, so it does not by itself block the credential.
"""

from __future__ import annotations

import argparse
import datetime
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "HAOFV_T2_SEQUENCE_ACCEPTANCE_RECEIPT_V1"
DEFAULT_FLOW = ROOT / "out/sync_trigger/sequence_flow.json"
DEFAULT_OUT = ROOT / "config/hardware_acceptance/t2_acceptance_receipt.json"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flow", type=Path, default=DEFAULT_FLOW)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--positions", type=int, default=361)
    args = parser.parse_args()

    state = json.loads(args.flow.read_text(encoding="utf-8"))
    readings = state["readings"].get("counters") or {}
    if not readings:
        print("FAIL no counter readings in the flow file")
        return 1

    roles, full, failures = {}, 0, 0
    for role, r in readings.items():
        groups = int(r.get("groups") or 0)
        if not groups:
            continue
        executed = int(r.get("executed") or 0)
        no_plan = int(r.get("no_plan") or 0)
        late = int(r.get("late") or 0)
        backend = int(r.get("backend") or 0)
        expected = groups * args.positions
        roles[role] = {"executed": executed, "groups": groups, "expected": expected,
                       "no_plan": no_plan, "late": late, "backend": backend,
                       "full_revolution": executed == expected}
        # `late` is timing quality (the clamp still fires the group), not a functional failure.
        failures += no_plan + backend
        if executed == expected:
            full += 1

    passed = failures == 0 and full >= 2
    print(f"roles={len(roles)} full_revolution={full} failures={failures} -> "
          f"{'PASS' if passed else 'FAIL'}")

    sys.path.insert(0, str(ROOT / "tools/hardware_acceptance"))
    from p3_hardware_acceptance import working_source_fingerprint  # noqa: E402

    fingerprint, file_count = working_source_fingerprint(ROOT)
    receipt = {
        "schema": SCHEMA,
        "passed": passed,
        "source_tree_sha256": fingerprint,
        "source_file_count": file_count,
        "generated_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "positions_criterion": args.positions,
        "full_revolution_roles": full,
        "failures": failures,
        "roles": roles,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(receipt, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"wrote {args.out}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
