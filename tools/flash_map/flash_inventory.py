#!/usr/bin/env python3
"""Audit HAOFV raw Flash references and legacy address dependencies."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


RAW_REFERENCE = re.compile(r"\bdrv_flash_(read|erase|program|xip_ptr)\b")
REQUIRED_CALLER_FIELDS = {
    "file", "owner", "contexts", "core", "modes", "partitions", "operations",
    "write_frequency", "power_cut_semantics", "target_api",
}
EXCLUDED_PARTS = {".git", "build", "build-validation", "build-debug", "build-rtos-smoke",
                  "build-multicore-smoke", "build-rtos-multicore-smoke", "tests", "tools",
                  "third_party"}
EXCLUDED_FILES = {
    "drivers/mcu/flash/src/drv_flash.c",
    "drivers/mcu/flash/inc/drv_flash.h",
}


class InventoryError(ValueError):
    pass


def normalize(path: Path) -> str:
    return path.as_posix()


def scan_raw_references(root: Path) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for path in sorted(root.rglob("*.c")):
        relative = normalize(path.relative_to(root))
        if relative in EXCLUDED_FILES or any(part in EXCLUDED_PARTS or part.startswith("build-") for part in path.parts):
            continue
        text = path.read_text(encoding="utf-8")
        operations: set[str] = set()
        lines: list[int] = []
        for line_number, line in enumerate(text.splitlines(), 1):
            matches = list(RAW_REFERENCE.finditer(line))
            if matches:
                operations.update(match.group(1) for match in matches)
                lines.append(line_number)
        if operations:
            result[relative] = {"operations": sorted(operations), "lines": lines}
    return result


def load_inventory(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise InventoryError(f"cannot load inventory {path}: {exc}") from exc
    if not isinstance(data, dict) or set(data) != {"inventory_version", "layout", "raw_callers", "legacy_address_dependencies"}:
        raise InventoryError("inventory root fields are invalid")
    if data["inventory_version"] != 1:
        raise InventoryError("inventory_version must be 1")
    layout = data["layout"]
    if not isinstance(layout, dict) or set(layout) != {
        "active_map_version", "compat_layout_size", "physical_geometry_size",
        "upper_unallocated_size", "migration_state",
    }:
        raise InventoryError("layout inventory fields are invalid")
    compat = int(layout["compat_layout_size"], 0)
    physical = int(layout["physical_geometry_size"], 0)
    upper = int(layout["upper_unallocated_size"], 0)
    if compat + upper != physical or layout["migration_state"] != "v2_target_not_deployed":
        raise InventoryError("layout inventory does not describe the v1 compatibility boundary")
    return data


def validate_inventory(root: Path, inventory: dict[str, Any]) -> dict[str, Any]:
    scanned = scan_raw_references(root)
    allowlist: dict[str, dict[str, Any]] = {}
    for index, caller in enumerate(inventory["raw_callers"]):
        if not isinstance(caller, dict) or set(caller) != REQUIRED_CALLER_FIELDS:
            raise InventoryError(f"raw_callers[{index}] fields are invalid")
        relative = caller["file"]
        if relative in allowlist:
            raise InventoryError(f"duplicate raw caller: {relative}")
        for field in ("owner", "core", "write_frequency", "power_cut_semantics", "target_api"):
            if not isinstance(caller[field], str) or not caller[field]:
                raise InventoryError(f"{relative} has invalid {field}")
        for field in ("contexts", "modes", "partitions", "operations"):
            if not isinstance(caller[field], list) or not caller[field] or any(not isinstance(item, str) or not item for item in caller[field]):
                raise InventoryError(f"{relative} has invalid {field}")
        operations = sorted(caller["operations"])
        if len(operations) != len(set(operations)) or any(item not in {"read", "erase", "program", "xip_ptr"} for item in operations):
            raise InventoryError(f"{relative} has invalid operations")
        if "app" in caller["contexts"] and any(
                item in {"erase", "program"} for item in operations):
            if caller["owner"] != "FlashTransactionAO" or caller["target_api"] != "FlashTransactionAO":
                raise InventoryError(
                    f"App raw write must be owned by FlashTransactionAO: {relative}")
        allowlist[relative] = {**caller, "operations": operations}

    missing = sorted(set(allowlist) - set(scanned))
    unexpected = sorted(set(scanned) - set(allowlist))
    if missing or unexpected:
        raise InventoryError(f"raw caller set drift: missing={missing} unexpected={unexpected}")
    for relative, observed in scanned.items():
        expected = allowlist[relative]["operations"]
        if observed["operations"] != expected:
            raise InventoryError(
                f"raw operation drift in {relative}: expected={expected} observed={observed['operations']}")

    dependencies: list[dict[str, Any]] = []
    seen_dependencies: set[str] = set()
    for index, dependency in enumerate(inventory["legacy_address_dependencies"]):
        if not isinstance(dependency, dict) or set(dependency) != {"file", "expected"}:
            raise InventoryError(f"legacy_address_dependencies[{index}] fields are invalid")
        relative = dependency["file"]
        if relative in seen_dependencies:
            raise InventoryError(f"duplicate legacy address dependency: {relative}")
        seen_dependencies.add(relative)
        expected = dependency["expected"]
        if not isinstance(expected, list) or not expected or any(not isinstance(token, str) or not token for token in expected):
            raise InventoryError(f"{relative} expected token list is invalid")
        path = root / relative
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise InventoryError(f"cannot read legacy address dependency {relative}: {exc}") from exc
        absent = [token for token in expected if token not in text]
        if absent:
            raise InventoryError(f"legacy address dependency drift in {relative}: absent={absent}")
        dependencies.append({"file": relative, "matched_tokens": expected})

    callers = []
    for relative in sorted(scanned):
        callers.append({**allowlist[relative], "source_lines": scanned[relative]["lines"]})
    return {
        "inventory_version": inventory["inventory_version"],
        "layout": inventory["layout"],
        "raw_callers": callers,
        "legacy_address_dependencies": dependencies,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--inventory", type=Path, default=Path("config/flash_raw_call_allowlist.json"))
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    inventory_path = args.inventory if args.inventory.is_absolute() else root / args.inventory
    try:
        report = validate_inventory(root, load_inventory(inventory_path))
        if args.output is not None:
            output = args.output if args.output.is_absolute() else root / args.output
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    except (InventoryError, OSError, ValueError) as exc:
        print(f"flash_inventory=FAILED: {exc}", file=sys.stderr)
        return 1
    print(
        f"flash_inventory=OK callers={len(report['raw_callers'])} "
        f"legacy_dependencies={len(report['legacy_address_dependencies'])} "
        f"active_map={report['layout']['active_map_version']} "
        f"migration={report['layout']['migration_state']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
