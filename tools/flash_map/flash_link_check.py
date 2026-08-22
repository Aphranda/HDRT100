#!/usr/bin/env python3
"""Check linked Flash write ownership and the core1 RAM-resident closure."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REQUIRED_RAM_CODE = {
    "drv_flash_core1_lockout_poll",
    "drv_flash_lockout_core1_poll",
    "flash_range_erase",
    "flash_range_program",
}
REQUIRED_RAM_DATA_SECTIONS = {"bss.s_lockout"}
PARKED_CALLERS = {
    "drv_flash_erase_parked": {"flash_transaction_erase"},
    "drv_flash_program_parked": {"flash_transaction_program"},
}
# The synchronous raw entry points are intentionally not part of the App
# image.  Checking only the symbol table is insufficient: dead-code/linker
# retention can leave a symbol present even when no caller exists, while a
# direct call can be introduced through an otherwise-allowed wrapper.  Keep
# this list separate from PARKED_CALLERS so the owner rule is explicit.
DIRECT_RAW_TARGETS = {"drv_flash_erase", "drv_flash_program"}
FORBIDDEN_APP_SYMBOLS = {"drv_flash_erase", "drv_flash_program"}
CORE1_CLOSURE = {
    "drv_flash_core1_lockout_poll",
    "drv_flash_lockout_core1_poll",
}
BOOT_FORBIDDEN_SYMBOL_TOKENS = (
    "freertos",
    "xtask",
    "vtask",
    "pvport",
    "xqueue",
    "xsemaphore",
    "scpi",
    "tdma",
    "fatfs",
    "littlefs",
    "flash_transaction",
    "resource_arbiter",
    "storage_manager",
    "ota_ao",
)
BOOT_RAW_CALLERS = {
    "drv_flash_erase": {"main", "ota_metadata_flash_erase"},
    "drv_flash_program": {"main", "ota_metadata_flash_program"},
}

MEMORY_RE = re.compile(
    r"^(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s+"
    r"(?P<origin>0x[0-9A-Fa-f]+)\s+"
    r"(?P<length>0x[0-9A-Fa-f]+)\s+"
    r"(?P<attributes>[A-Za-z]+)\s*$"
)
SYMBOL_RE = re.compile(
    r"^\s*(?P<address>0x[0-9A-Fa-f]+)\s+"
    r"(?:(?:0x[0-9A-Fa-f]+)\s+)?"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_.$]*)"
    r"(?:\s*=\s*0x[0-9A-Fa-f]+)?\s*$"
)
SECTION_RE = re.compile(
    r"^\s+\.(?P<name>[A-Za-z0-9_.$]+)\s+"
    r"(?P<address>0x[0-9A-Fa-f]+)\s+0x[0-9A-Fa-f]+(?:\s|$)"
)
SECTION_NAME_RE = re.compile(r"^\s+\.(?P<name>[A-Za-z0-9_.$]+)\s*$")
SECTION_ADDRESS_RE = re.compile(
    r"^\s+(?P<address>0x[0-9A-Fa-f]+)\s+0x[0-9A-Fa-f]+(?:\s|$)"
)
FUNCTION_RE = re.compile(r"^\s*[0-9A-Fa-f]+\s+<(?P<name>[^>]+)>:\s*$")
REFERENCE_RE = re.compile(r"<(?P<name>[A-Za-z_][A-Za-z0-9_.$]*)(?:\+0x[0-9A-Fa-f]+)?>")
HEX_RE = re.compile(r"\b(?:0x)?(?P<value>[0-9A-Fa-f]{8})\b")


class FlashLinkError(ValueError):
    pass


def parse_memory_regions(map_text: str) -> dict[str, tuple[int, int, str]]:
    regions: dict[str, tuple[int, int, str]] = {}
    in_memory = False
    for line in map_text.splitlines():
        if line.strip() == "Memory Configuration":
            in_memory = True
            continue
        if not in_memory:
            continue
        if line.strip() == "Linker script and memory map":
            break
        match = MEMORY_RE.match(line)
        if match is None or match.group("name") == "Name":
            continue
        regions[match.group("name")] = (
            int(match.group("origin"), 16),
            int(match.group("length"), 16),
            match.group("attributes"),
        )
    return regions


def parse_map_addresses(map_text: str) -> tuple[dict[str, int], dict[str, int]]:
    symbols: dict[str, int] = {}
    sections: dict[str, int] = {}
    pending_section: str | None = None
    for line in map_text.splitlines():
        symbol = SYMBOL_RE.match(line)
        if symbol is not None:
            address = int(symbol.group("address"), 16)
            if address != 0:
                symbols[symbol.group("name")] = address
        section = SECTION_RE.match(line)
        if section is not None:
            sections[section.group("name")] = int(section.group("address"), 16)
            pending_section = None
            continue
        section_name = SECTION_NAME_RE.match(line)
        if section_name is not None:
            pending_section = section_name.group("name")
            continue
        if pending_section is not None:
            section_address = SECTION_ADDRESS_RE.match(line)
            if section_address is not None:
                sections[pending_section] = int(section_address.group("address"), 16)
            pending_section = None
    return symbols, sections


def parse_disassembly(dis_text: str) -> tuple[dict[str, set[str]], dict[str, str]]:
    callers = {target: set() for target in PARKED_CALLERS}
    callers.update({target: set() for target in DIRECT_RAW_TARGETS})
    bodies: dict[str, list[str]] = {symbol: [] for symbol in CORE1_CLOSURE}
    current = ""
    for line in dis_text.splitlines():
        function = FUNCTION_RE.match(line)
        if function is not None:
            current = function.group("name")
            continue
        if current in bodies:
            bodies[current].append(line)
        for reference in REFERENCE_RE.finditer(line):
            target = reference.group("name")
            if target in callers and current and current != target:
                callers[target].add(current)
    return callers, {name: "\n".join(lines) for name, lines in bodies.items()}


def address_in_ram(address: int, regions: dict[str, tuple[int, int, str]]) -> bool:
    for name, (origin, length, attributes) in regions.items():
        if (name == "RAM" or name.startswith("SCRATCH_")) and "x" in attributes:
            if origin <= address < origin + length:
                return True
    return False


def validate_link_contract(map_text: str, dis_text: str, profile: str = "app") -> list[str]:
    failures: list[str] = []
    regions = parse_memory_regions(map_text)
    symbols, sections = parse_map_addresses(map_text)
    callers, bodies = parse_disassembly(dis_text)

    if profile == "boot":
        boot_text = (map_text + "\n" + dis_text).lower()
        for token in BOOT_FORBIDDEN_SYMBOL_TOKENS:
            if token in boot_text:
                failures.append(f"forbidden Boot dependency linked: token={token}")
        for required in (
            "bootloader_validate_slot_direct",
            "ota_metadata_load",
            "ota_metadata_store",
            "drv_flash_erase",
            "drv_flash_program",
        ):
            if required not in boot_text:
                failures.append(f"required Boot dependency symbol missing: {required}")
        for target, allowed in BOOT_RAW_CALLERS.items():
            actual = callers[target]
            if actual != allowed:
                failures.append(
                    f"Boot raw caller drift for {target}: "
                    f"expected={sorted(allowed)} actual={sorted(actual)}"
                )
        return failures

    if "RAM" not in regions:
        failures.append("link map has no RAM memory region")

    for symbol in sorted(REQUIRED_RAM_CODE):
        address = symbols.get(symbol)
        if address is None:
            failures.append(f"required RAM code symbol missing: {symbol}")
        elif not address_in_ram(address, regions):
            failures.append(f"required RAM code symbol is outside SRAM: {symbol}=0x{address:08X}")

    for section in sorted(REQUIRED_RAM_DATA_SECTIONS):
        address = sections.get(section)
        if address is None:
            failures.append(f"required RAM data section missing: .{section}")
        elif not address_in_ram(address, regions):
            failures.append(f"required RAM data section is outside SRAM: .{section}=0x{address:08X}")

    for target, allowed in PARKED_CALLERS.items():
        actual = callers[target]
        if actual != allowed:
            failures.append(
                f"parked raw caller drift for {target}: "
                f"expected={sorted(allowed)} actual={sorted(actual)}"
            )

    for symbol in sorted(FORBIDDEN_APP_SYMBOLS):
        if symbol in symbols:
            failures.append(f"synchronous raw write linked into App: {symbol}")

    # A symbol-table absence check does not prove ownership.  Reject any
    # actual App call edge to the synchronous raw API, including calls from
    # a wrapper that happens to survive link-time garbage collection.
    for target in sorted(DIRECT_RAW_TARGETS):
        actual = callers[target]
        if actual:
            failures.append(
                f"synchronous raw caller linked into App for {target}: "
                f"callers={sorted(actual)}"
            )

    xip_base = symbols.get("FLASH_COMPAT_GEOMETRY_XIP_BASE")
    xip_size = symbols.get("FLASH_COMPAT_GEOMETRY_TOTAL_SIZE")
    if xip_base is None or xip_size is None:
        failures.append("link map is missing generated Flash geometry symbols")
    else:
        for symbol, body in bodies.items():
            if not body:
                failures.append(f"core1 RAM closure body missing: {symbol}")
                continue
            for match in HEX_RE.finditer(body):
                value = int(match.group("value"), 16)
                if xip_base <= value < xip_base + xip_size:
                    failures.append(
                        f"core1 RAM closure references XIP: {symbol}->0x{value:08X}"
                    )
                    break

    poll_body = bodies.get("drv_flash_lockout_core1_poll", "")
    if "cpsid" not in poll_body or "PRIMASK" not in poll_body:
        failures.append("core1 RAM park loop does not save/disable/restore interrupts")
    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, required=True, dest="map_path")
    parser.add_argument("--dis", type=Path, required=True, dest="dis_path")
    parser.add_argument("--profile", choices=("app", "boot"), default="app")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        failures = validate_link_contract(
            args.map_path.read_text(encoding="utf-8", errors="replace"),
            args.dis_path.read_text(encoding="utf-8", errors="replace"),
            args.profile,
        )
    except OSError as exc:
        print(f"flash_link_contract=FAILED: {exc}", file=sys.stderr)
        return 1
    if failures:
        for failure in failures:
            print(f"flash_link_contract=FAILED: {failure}", file=sys.stderr)
        return 1
    print(
        f"flash_link_contract=OK profile={args.profile} "
        f"map={args.map_path.name} dis={args.dis_path.name}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
