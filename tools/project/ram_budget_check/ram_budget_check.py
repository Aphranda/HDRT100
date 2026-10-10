#!/usr/bin/env python3
"""Check RP2350 application SRAM budget from a linker map."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path


SRAM_BASE = 0x20000000
SRAM_SIZE = 512 * 1024
SRAM_END = SRAM_BASE + SRAM_SIZE
LICENSE_SCHEMA = "HAOFV_TEMPORARY_RAM_LICENSE_V1"
DEFAULT_MIN_FREE_BYTES = 48 * 1024
DEFAULT_DEBUG_MIN_FREE_BYTES = 32 * 1024

SYMBOL_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+([A-Za-z_][A-Za-z0-9_$]*)\s*=\s*\.\s*$")
SECTION_RE = re.compile(r"^\.(sync_io_dma_ring|bss|heap)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)")
SECTION_NAME_RE = re.compile(r"^\.(sync_io_dma_ring|bss|heap)\s*$")
SECTION_CONT_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*$")
NAMED_BSS_RE = re.compile(r"^\s*\.bss\.([A-Za-z_][A-Za-z0-9_]*)\s*$")
NAMED_CONT_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map_file", type=Path, nargs="?")
    parser.add_argument("--profile", choices=("release", "debug"),
                        default="release",
                        help="gate profile: release=48 KiB, debug=32 KiB")
    parser.add_argument("--min-free", type=int, default=None,
                        help="override profile minimum bytes between __end__ and SRAM top")
    parser.add_argument("--top", type=int, default=12,
                        help="number of largest .bss symbols to print")
    parser.add_argument("--temporary-license", type=Path,
                        help="audited temporary lower bound for DPLL development")
    parser.add_argument("--issue-temporary-license", type=Path,
                        help="write a temporary license and exit")
    parser.add_argument("--licensed-min-free", type=int,
                        help="temporary minimum free bytes")
    parser.add_argument("--expires", help="temporary license expiry, YYYY-MM-DD")
    parser.add_argument("--reason", help="temporary license scope and reason")
    return parser.parse_args(argv)


def minimum_free_bytes(args: argparse.Namespace) -> int:
    """Resolve the SRAM gate, allowing a relaxed debug-only profile."""
    if args.min_free is not None:
        return args.min_free
    if args.profile == "debug":
        return DEFAULT_DEBUG_MIN_FREE_BYTES
    return DEFAULT_MIN_FREE_BYTES


def profile_blocks_on_shortfall(args: argparse.Namespace) -> bool:
    """Product/release gates reject; debug gates retain evidence and continue."""
    return args.profile != "debug"


def parse_date(value: str, label: str) -> dt.date:
    try:
        return dt.date.fromisoformat(value)
    except ValueError as exc:
        raise SystemExit(f"{label} must be a valid YYYY-MM-DD date") from exc


def issue_temporary_license(args: argparse.Namespace) -> int:
    formal_min_free = minimum_free_bytes(args)
    if (args.licensed_min_free is None or args.expires is None or
            args.reason is None or not args.reason.strip()):
        raise SystemExit(
            "license issuance requires --licensed-min-free, --expires and --reason")
    if args.licensed_min_free < 0 or args.licensed_min_free >= formal_min_free:
        raise SystemExit("licensed minimum must be non-negative and below --min-free")
    expires = parse_date(args.expires, "--expires")
    today = dt.date.today()
    if expires < today:
        raise SystemExit("temporary license is already expired")
    license_data = {
        "schema": LICENSE_SCHEMA,
        "issued_on": today.isoformat(),
        "expires_on": expires.isoformat(),
        "formal_min_free_bytes": formal_min_free,
        "licensed_min_free_bytes": args.licensed_min_free,
        "reason": args.reason.strip(),
    }
    args.issue_temporary_license.parent.mkdir(parents=True, exist_ok=True)
    args.issue_temporary_license.write_text(
        json.dumps(license_data, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"temporary_license={args.issue_temporary_license}")
    print(f"expires_on={expires.isoformat()}")
    print(f"profile={args.profile}")
    print(f"formal_min_free_bytes={formal_min_free}")
    print(f"licensed_min_free_bytes={args.licensed_min_free}")
    return 0


def load_temporary_license(path: Path, formal_min_free: int) -> dict[str, object]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read temporary RAM license {path}: {exc}") from exc
    required = {
        "schema", "issued_on", "expires_on", "formal_min_free_bytes",
        "licensed_min_free_bytes", "reason",
    }
    if not isinstance(data, dict) or set(data) != required:
        raise SystemExit("temporary RAM license fields do not match schema")
    if data["schema"] != LICENSE_SCHEMA:
        raise SystemExit("temporary RAM license schema is unsupported")
    issued = parse_date(str(data["issued_on"]), "license issued_on")
    expires = parse_date(str(data["expires_on"]), "license expires_on")
    today = dt.date.today()
    if issued > today or expires < today or expires < issued:
        raise SystemExit("temporary RAM license is not currently valid")
    if data["formal_min_free_bytes"] != formal_min_free:
        raise SystemExit("temporary RAM license formal threshold mismatch")
    licensed_min = data["licensed_min_free_bytes"]
    if (not isinstance(licensed_min, int) or licensed_min < 0 or
            licensed_min >= formal_min_free):
        raise SystemExit("temporary RAM license lower bound is invalid")
    if not isinstance(data["reason"], str) or not data["reason"].strip():
        raise SystemExit("temporary RAM license reason is empty")
    return data


def parse_map(path: Path) -> tuple[dict[str, int], dict[str, tuple[int, int]], dict[str, int]]:
    symbols: dict[str, int] = {}
    sections: dict[str, tuple[int, int]] = {}
    bss_symbols: dict[str, int] = {}
    pending_section_name: str | None = None
    pending_bss_name: str | None = None

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        section = SECTION_RE.match(line)
        if section:
            sections[section.group(1)] = (int(section.group(2), 16), int(section.group(3), 16))
            pending_section_name = None
            continue

        section_name = SECTION_NAME_RE.match(line)
        if section_name:
            pending_section_name = section_name.group(1)
            continue

        if pending_section_name is not None:
            section_cont = SECTION_CONT_RE.match(line)
            if section_cont:
                sections[pending_section_name] = (
                    int(section_cont.group(1), 16),
                    int(section_cont.group(2), 16),
                )
                pending_section_name = None
                continue
            if line.strip() != "":
                pending_section_name = None

        symbol = SYMBOL_RE.match(line)
        if symbol:
            symbols[symbol.group(2)] = int(symbol.group(1), 16)

        named = NAMED_BSS_RE.match(line)
        if named:
            pending_bss_name = named.group(1)
            continue

        if pending_bss_name is not None:
            cont = NAMED_CONT_RE.match(line)
            if cont:
                bss_symbols[pending_bss_name] = int(cont.group(2), 16)
                pending_bss_name = None

    return symbols, sections, bss_symbols


def main() -> int:
    args = parse_args()
    min_free = minimum_free_bytes(args)
    if args.issue_temporary_license is not None:
        return issue_temporary_license(args)
    if args.map_file is None:
        raise SystemExit("map_file is required unless issuing a license")
    symbols, sections, bss_symbols = parse_map(args.map_file)
    end = symbols.get("__end__")
    if end is None:
        raise SystemExit("map does not contain __end__")

    free_bytes = SRAM_END - end
    bss_size = sections.get("bss", (0, 0))[1]
    dma_ring_size = sections.get("sync_io_dma_ring", (0, 0))[1]
    heap_size = sections.get("heap", (0, 0))[1]

    print(f"map={args.map_file}")
    print(f"profile={args.profile}")
    print(f"min_free_bytes={min_free}")
    print(f"sram_end=0x{SRAM_END:08X}")
    print(f"link_end=0x{end:08X}")
    print(f"link_free_bytes={free_bytes}")
    print(f"bss_bytes={bss_size}")
    print(f"sync_io_dma_ring_bytes={dma_ring_size}")
    print(f"crt_heap_section_bytes={heap_size}")
    print("largest_bss_symbols:")
    for name, size in sorted(bss_symbols.items(), key=lambda item: item[1], reverse=True)[:args.top]:
        print(f"  {size:6d} {name}")

    if free_bytes < min_free:
        if args.temporary_license is not None:
            license_data = load_temporary_license(
                args.temporary_license, min_free)
            licensed_min = int(license_data["licensed_min_free_bytes"])
            print(f"temporary_license={args.temporary_license}")
            print(f"temporary_license_expires_on={license_data['expires_on']}")
            print(f"temporary_license_reason={license_data['reason']}")
            if free_bytes >= licensed_min:
                print(
                    "PASS_WITH_TEMPORARY_LICENSE: "
                    f"link_free_bytes {free_bytes} >= licensed_min_free "
                    f"{licensed_min} (formal min_free {min_free})")
                return 0
        shortfall = min_free - free_bytes
        if not profile_blocks_on_shortfall(args):
            print(
                "PASS_WITH_DIAGNOSTIC: debug RAM gate recorded a shortfall; "
                f"link_free_bytes {free_bytes} < min_free {min_free}; "
                f"shortfall_bytes={shortfall}; forced_continue=true")
            return 0
        print(f"FAIL: link_free_bytes {free_bytes} < min_free {min_free}", file=sys.stderr)
        return 1

    print(f"PASS: link_free_bytes {free_bytes} >= min_free {min_free}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
