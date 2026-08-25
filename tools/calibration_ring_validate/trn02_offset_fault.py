#!/usr/bin/env python3
"""Classify an intentional TRN-02 offset fault before running it on hardware.

The tool records the expected failure contract.  It never promotes a faulted
trial to an accepted calibration candidate and is safe to use in CI or as the
input manifest for a hardware fault-injection run.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def classify_offset_fault(
    configured_offset: int,
    fault_offset: int,
    search_start: int,
    search_end: int,
    guard_samples: int,
) -> dict[str, object]:
    if search_start > search_end or guard_samples < 0:
        raise ValueError("invalid search window")
    effective = configured_offset + fault_offset
    if effective < -10 or effective > 10:
        reason = "ARM_REJECTED_OFFSET_RANGE"
    elif effective - guard_samples < search_start or \
            effective + guard_samples > search_end:
        reason = "TIMEOUT_EXPECTED_WINDOW_MISSED"
    else:
        reason = "CORRELATION_REJECT_EXPECTED"
    return {
        "phase": "TRN-02_OFFSET_FAULT",
        "configured_offset_sample_count": configured_offset,
        "fault_offset_sample_count": fault_offset,
        "effective_offset_sample_count": effective,
        "search_window_samples": [search_start, search_end],
        "guard_sample_count": guard_samples,
        "expected_failure": reason,
        "accepted": False,
        "active_candidate_allowed": False,
        "trn03_staging_allowed": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configured-offset", type=int, default=0)
    parser.add_argument("--fault-offset", type=int, required=True)
    parser.add_argument("--search-start", type=int, default=-10)
    parser.add_argument("--search-end", type=int, default=10)
    parser.add_argument("--guard-samples", type=int, default=0)
    parser.add_argument("--fault-link", type=int)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    result = classify_offset_fault(
        args.configured_offset, args.fault_offset,
        args.search_start, args.search_end, args.guard_samples)
    if args.fault_link is not None:
        result["fault_link"] = args.fault_link
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    print(encoded, end="")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encoded, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
