#!/usr/bin/env python3
"""Run the invalid-payload rejection suite for the second-class parsers."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = Path(__file__).with_name("refmem_cmd_tdma_workflow.py")
PYTHON = sys.executable


def bad_refmem() -> str:
    wire = bytearray.fromhex(
        "444D435201000000640000000F000000020000000100000001000000"
        "0000000000000000A086010000000000030002000000000001D512E2")
    wire[52:56] = b"\x00\x00\x00\x00"
    return wire.hex().upper()


def bad_t2_static() -> str:
    wire = bytearray(struct.pack("<HBBIII", 0x5453, 1, 0, 0x0F, 7, 11))
    wire[0] = 0
    return wire.hex().upper()


def bad_t2_dynamic() -> str:
    wire = bytearray(struct.pack("<HBBIIIII", 0x5444, 1, 0, 0x0F,
                                 42, 7, 9, 0))
    wire[20:24] = (1).to_bytes(4, "little")
    return wire.hex().upper()


CASES = [
    (0x52, "refmem_bad_crc", bad_refmem),
    (0x53, "t2_static_bad_magic", bad_t2_static),
    (0x54, "t2_dynamic_bad_reserved", bad_t2_dynamic),
]


def main() -> int:
    parser = argparse.ArgumentParser(description="Invalid payload parser suite")
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--target-mask", type=lambda value: int(value, 0),
                        default=0x0F)
    parser.add_argument("--attempts", type=int, default=3)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    results = []
    for payload_type, name, builder in CASES:
        payload = builder()
        passed = False
        report = None
        attempts = []
        for attempt in range(1, max(1, args.attempts) + 1):
            child = args.out_dir / name / f"attempt{attempt}"
            command = [
                PYTHON, str(WORKFLOW),
                "--payload-type", hex(payload_type),
                "--payload-hex", payload,
                "--expect-reject",
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
            report = (json.loads(report_path.read_text(encoding="utf-8"))
                      if report_path.is_file() else None)
            passed = bool(report and report.get("passed"))
            attempts.append({"attempt": attempt, "passed": passed,
                             "report": str(report_path),
                             "log": str(child / "run.log")})
            if passed:
                break
        results.append({
            "payload_type": payload_type,
            "name": name,
            "payload": payload,
            "passed": passed,
            "attempts": attempts,
            "verification": (report or {}).get("verification"),
        })
    suite = {
        "schema": "HAOFV_REFTMEM_CMD_PARSER_REJECT_SUITE_V1",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "target_mask": args.target_mask,
        "passed": all(item["passed"] for item in results),
        "cases": results,
    }
    (args.out_dir / "reject_suite_report.json").write_text(
        json.dumps(suite, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# REFMEM second-class invalid-payload suite",
        "",
        f"- target_mask: `0x{args.target_mask:02X}`",
        f"- passed: **{suite['passed']}**",
        "",
        "| case | type | result | rejection evidence |",
        "|---|---|---|---|",
    ]
    for item in results:
        lines.append(
            f"| {item['name']} | `0x{item['payload_type']:02X}` | "
            f"{'PASS' if item['passed'] else 'FAIL'} | "
            f"{item['verification']} |")
    (args.out_dir / "REJECT_SUITE_REPORT.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"passed": suite["passed"],
                      "report": str(args.out_dir / "reject_suite_report.json"),
                      "markdown": str(args.out_dir / "REJECT_SUITE_REPORT.md")}))
    return 0 if suite["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
