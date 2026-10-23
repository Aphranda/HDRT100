#!/usr/bin/env python3
"""Run the REFMEM_CMD TDMA parser suite and aggregate a report."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = Path(__file__).with_name("refmem_cmd_tdma_workflow.py")
PYTHON = sys.executable

PARSERS = [
    (0x52, "refmem_cmd"),
    (0x53, "t2_static"),
    (0x54, "t2_dynamic"),
]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run all second-class parsers")
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--target-mask", type=lambda value: int(value, 0),
                        default=0x0F)
    parser.add_argument("--attempts", type=int, default=3)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    results = []
    for payload_type, name in PARSERS:
        attempts = []
        passed = False
        report = None
        for attempt in range(1, max(1, args.attempts) + 1):
            child = args.out_dir / name / f"attempt{attempt}"
            command = [
                PYTHON, str(WORKFLOW),
                "--payload-type", hex(payload_type),
                "--target-mask", hex(args.target_mask),
                "--out-dir", str(child),
            ]
            run = subprocess.run(
                command, cwd=ROOT, text=True, encoding="utf-8",
                errors="replace", stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, check=False,
            )
            child.mkdir(parents=True, exist_ok=True)
            (child / "run.log").write_text(run.stdout, encoding="utf-8")
            report_path = child / "report.json"
            report = None
            if report_path.is_file():
                report = json.loads(report_path.read_text(encoding="utf-8"))
            passed = bool(report and report.get("passed"))
            attempts.append({
                "attempt": attempt,
                "returncode": run.returncode,
                "passed": passed,
                "report": str(report_path),
                "log": str(child / "run.log"),
            })
            if passed:
                break
        results.append({
            "payload_type": payload_type,
            "name": name,
            "returncode": 0 if passed else 1,
            "passed": passed,
            "attempts": attempts,
            "report": attempts[-1]["report"],
            "log": attempts[-1]["log"],
            "verification": (report or {}).get("verification"),
        })
    suite = {
        "schema": "HAOFV_REFTMEM_CMD_PARSER_SUITE_V1",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "target_mask": args.target_mask,
        "passed": all(item["passed"] for item in results),
        "parsers": results,
    }
    (args.out_dir / "suite_report.json").write_text(
        json.dumps(suite, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# REFMEM second-class parser suite",
        "",
        f"- target_mask: `0x{args.target_mask:02X}`",
        f"- passed: **{suite['passed']}**",
        "",
        "| parser | type | result | report |",
        "|---|---|---|---|",
    ]
    for item in results:
        lines.append(
            f"| {item['name']} | `0x{item['payload_type']:02X}` | "
            f"{'PASS' if item['passed'] else 'FAIL'} | {item['report']} |")
    (args.out_dir / "SUITE_REPORT.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"passed": suite["passed"],
                      "report": str(args.out_dir / "suite_report.json"),
                      "markdown": str(args.out_dir / "SUITE_REPORT.md")}))
    return 0 if suite["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
