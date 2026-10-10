#!/usr/bin/env python3
"""Static OTA HAOFV owner, snapshot and stack-shape gate."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


FUNCTIONS = {
    "pota_bcb_scan_step": 64,
    "pota_bcb_txn_begin_from_selection": 128,
    "ota_metadata_mark_pending_step": 128,
    "body_crc32": 32,
}


def function_block(disassembly: str, name: str) -> str:
    match = re.search(
        rf"(?ms)^[0-9a-f]+ <{re.escape(name)}>:\s*(.*?)(?=^\n[0-9a-f]+ <|\Z)",
        disassembly,
    )
    if not match:
        raise ValueError(f"function missing from disassembly: {name}")
    return match.group(1)


def stack_frame(block: str) -> int:
    values = [
        int(value, 16)
        for value in re.findall(r"\bsub(?:\.w)?\s+sp(?:,\s*sp)?\s*,\s*#(0x[0-9a-f]+|[0-9]+)", block, re.I)
    ]
    return max(values, default=0)


def run(root: Path, disassembly: Path) -> dict[str, object]:
    dis = disassembly.read_text(encoding="utf-8", errors="ignore")
    source_paths = [
        root / "components/ota_manager/src/ota_metadata.c",
        root / "third_party/portable_ota/src/pota_boot_control_store.c",
        root / "middleware/scpi_port/src/scpi_ota_commands.c",
    ]
    source = "\n".join(path.read_text(encoding="utf-8") for path in source_paths)
    failures: list[str] = []
    checks: dict[str, object] = {}

    forbidden = (
        "selection_hint",
        "pota_bcb_trace_fn",
        "diagnostics_fault_capture",
        "OTA_TRACE_PHASE_HARDFAULT",
    )
    for symbol in forbidden:
        if symbol in source:
            failures.append(f"temporary or hidden OTA symbol remains: {symbol}")
    checks["forbidden_symbols"] = {symbol: symbol not in source for symbol in forbidden}

    if "ota_ao_get_metadata(" in source:
        failures.append("SCPI/product source still references non-snapshot metadata reader")
    checks["snapshot_reader_boundary"] = "ota_ao_get_metadata(" not in source

    frames: dict[str, int] = {}
    for name, limit in FUNCTIONS.items():
        try:
            frame = stack_frame(function_block(dis, name))
        except ValueError as exc:
            failures.append(str(exc))
            continue
        frames[name] = frame
        if frame > limit:
            failures.append(f"{name} stack frame {frame} exceeds {limit}")
    checks["stack_frames"] = frames
    checks["stack_limits"] = FUNCTIONS
    return {"passed": not failures, "failures": failures, "checks": checks}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--dis", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    report = run(args.root.resolve(), args.dis.resolve())
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for name, value in report["checks"].items():
        print(f"{'OK' if value else 'FAIL'} {name}: {value}")
    if report["failures"]:
        for failure in report["failures"]:
            print(f"FAIL {failure}")
        return 1
    print("OK ota_haofv_gate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
