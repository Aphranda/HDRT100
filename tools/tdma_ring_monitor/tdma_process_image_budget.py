#!/usr/bin/env python3
"""Validate and report the mandatory-first TDMA process-image layout."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAYOUT_HEADER = ROOT / "components/tdma/inc/tdma_process_image_layout.h"
FLIGHT_HEADER = ROOT / "components/tdma/inc/tdma_flight_engine.h"

NUMBER_RE = re.compile(
    r"^#define\s+([A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|[0-9]+)u?\s*$",
    re.MULTILINE,
)


@dataclass(frozen=True)
class ProcessImageRegion:
    name: str
    priority: str
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


@dataclass(frozen=True)
class ProcessImageBudget:
    node_count: int
    node_bytes: int
    fast_header_bytes: int
    body_bytes: int
    mandatory_body_bytes: int
    optional_body_capacity: int
    optional_body_bytes: int
    runtime_free_bytes: int
    process_image_bytes: int
    regions: tuple[ProcessImageRegion, ...]


def _definitions(path: Path) -> dict[str, int]:
    text = path.read_text(encoding="utf-8")
    return {
        name: int(value, 0)
        for name, value in NUMBER_RE.findall(text)
    }


def load_budget(
    layout_header: Path = LAYOUT_HEADER,
    flight_header: Path = FLIGHT_HEADER,
) -> ProcessImageBudget:
    values = _definitions(flight_header)
    values.update(_definitions(layout_header))

    required = (
        "TDMA_FLIGHT_SHORT_SLOT_COUNT",
        "TDMA_FLIGHT_SHORT_SLOT_SIZE",
        "TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE",
        "TDMA_PROCESS_IMAGE_VDC_OFFSET",
        "TDMA_PROCESS_IMAGE_VDC_SIZE",
        "TDMA_PROCESS_IMAGE_REFMEM_OFFSET",
        "TDMA_PROCESS_IMAGE_REFMEM_SIZE",
        "TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET",
        "TDMA_PROCESS_IMAGE_ACK_QUALITY_SIZE",
        "TDMA_PROCESS_IMAGE_CONTROL_OFFSET",
        "TDMA_PROCESS_IMAGE_CONTROL_SIZE",
        "TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET",
        "TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_SIZE",
        "TDMA_PROCESS_IMAGE_CRC_OFFSET",
        "TDMA_PROCESS_IMAGE_CRC_SIZE",
    )
    missing = [name for name in required if name not in values]
    if missing:
        raise ValueError(f"missing layout definitions: {', '.join(missing)}")

    regions = (
        ProcessImageRegion(
            "fast_header", "transport", 0,
            values["TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE"]),
        ProcessImageRegion(
            "vdc_dpll", "mandatory",
            values["TDMA_PROCESS_IMAGE_VDC_OFFSET"],
            values["TDMA_PROCESS_IMAGE_VDC_SIZE"]),
        ProcessImageRegion(
            "critical_refmem", "mandatory",
            values["TDMA_PROCESS_IMAGE_REFMEM_OFFSET"],
            values["TDMA_PROCESS_IMAGE_REFMEM_SIZE"]),
        ProcessImageRegion(
            "ack_fence_quality", "mandatory",
            values["TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET"],
            values["TDMA_PROCESS_IMAGE_ACK_QUALITY_SIZE"]),
        ProcessImageRegion(
            "control_token", "mandatory",
            values["TDMA_PROCESS_IMAGE_CONTROL_OFFSET"],
            values["TDMA_PROCESS_IMAGE_CONTROL_SIZE"]),
        ProcessImageRegion(
            "diagnostic", "optional",
            values["TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET"],
            values["TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_SIZE"]),
        ProcessImageRegion(
            "mailbox_crc", "mandatory",
            values["TDMA_PROCESS_IMAGE_CRC_OFFSET"],
            values["TDMA_PROCESS_IMAGE_CRC_SIZE"]),
    )
    node_bytes = values["TDMA_FLIGHT_SHORT_SLOT_SIZE"]
    body_bytes = node_bytes - values["TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE"]
    mandatory = sum(r.size for r in regions if r.priority == "mandatory")
    optional = sum(r.size for r in regions if r.priority == "optional")
    optional_capacity = body_bytes - mandatory
    return ProcessImageBudget(
        node_count=values["TDMA_FLIGHT_SHORT_SLOT_COUNT"],
        node_bytes=node_bytes,
        fast_header_bytes=values["TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE"],
        body_bytes=body_bytes,
        mandatory_body_bytes=mandatory,
        optional_body_capacity=optional_capacity,
        optional_body_bytes=optional,
        runtime_free_bytes=optional_capacity - optional,
        process_image_bytes=(
            values["TDMA_FLIGHT_SHORT_SLOT_COUNT"] * node_bytes),
        regions=regions,
    )


def validate_budget(budget: ProcessImageBudget) -> list[str]:
    errors: list[str] = []
    previous_end = 0
    for region in budget.regions:
        if region.offset != previous_end:
            errors.append(
                f"{region.name}: offset {region.offset} does not follow "
                f"previous end {previous_end}")
        if region.size <= 0:
            errors.append(f"{region.name}: size must be positive")
        previous_end = region.end
    if previous_end != budget.node_bytes:
        errors.append(
            f"layout ends at {previous_end}, Node mailbox is {budget.node_bytes}")
    if budget.mandatory_body_bytes > budget.body_bytes:
        errors.append("mandatory body exceeds Node body")
    if budget.optional_body_bytes > budget.optional_body_capacity:
        errors.append("optional body exceeds residual capacity")
    if budget.runtime_free_bytes != 0:
        errors.append("configured layout must leave no runtime-free bytes")
    return errors


def render_markdown(budget: ProcessImageBudget) -> str:
    lines = [
        "| region | priority | offset | size | end |",
        "|---|---|---:|---:|---:|",
    ]
    lines.extend(
        f"| {r.name} | {r.priority} | {r.offset} | {r.size} | {r.end} |"
        for r in budget.regions
    )
    lines.extend((
        "",
        f"- Node mailbox: {budget.node_bytes} B",
        f"- Node body: {budget.body_bytes} B",
        f"- mandatory body: {budget.mandatory_body_bytes} B",
        f"- optional capacity / configured: "
        f"{budget.optional_body_capacity} B / {budget.optional_body_bytes} B",
        f"- runtime-free: {budget.runtime_free_bytes} B",
        f"- process image: {budget.node_count} x {budget.node_bytes} B = "
        f"{budget.process_image_bytes} B",
    ))
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--format", choices=("markdown", "json"),
                        default="markdown")
    parser.add_argument("--out", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    budget = load_budget()
    errors = validate_budget(budget)
    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    output = (
        json.dumps(asdict(budget), indent=2) + "\n"
        if args.format == "json" else render_markdown(budget)
    )
    if args.out is None:
        print(output, end="")
    else:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output, encoding="utf-8")
        print(f"out={args.out.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
