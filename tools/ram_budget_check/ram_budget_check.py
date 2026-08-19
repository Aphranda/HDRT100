#!/usr/bin/env python3
"""Check RP2350 application SRAM budget from a linker map."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SRAM_BASE = 0x20000000
SRAM_SIZE = 512 * 1024
SRAM_END = SRAM_BASE + SRAM_SIZE

SYMBOL_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+([A-Za-z_][A-Za-z0-9_$]*)\s*=\s*\.\s*$")
SECTION_RE = re.compile(r"^\.(sync_io_dma_ring|bss|heap)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)")
SECTION_NAME_RE = re.compile(r"^\.(sync_io_dma_ring|bss|heap)\s*$")
SECTION_CONT_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*$")
NAMED_BSS_RE = re.compile(r"^\s*\.bss\.([A-Za-z_][A-Za-z0-9_]*)\s*$")
NAMED_CONT_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map_file", type=Path)
    parser.add_argument("--min-free", type=int, default=96 * 1024,
                        help="minimum bytes between __end__ and SRAM top")
    parser.add_argument("--top", type=int, default=12,
                        help="number of largest .bss symbols to print")
    return parser.parse_args()


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
    symbols, sections, bss_symbols = parse_map(args.map_file)
    end = symbols.get("__end__")
    if end is None:
        raise SystemExit("map does not contain __end__")

    free_bytes = SRAM_END - end
    bss_size = sections.get("bss", (0, 0))[1]
    dma_ring_size = sections.get("sync_io_dma_ring", (0, 0))[1]
    heap_size = sections.get("heap", (0, 0))[1]

    print(f"map={args.map_file}")
    print(f"sram_end=0x{SRAM_END:08X}")
    print(f"link_end=0x{end:08X}")
    print(f"link_free_bytes={free_bytes}")
    print(f"bss_bytes={bss_size}")
    print(f"sync_io_dma_ring_bytes={dma_ring_size}")
    print(f"crt_heap_section_bytes={heap_size}")
    print("largest_bss_symbols:")
    for name, size in sorted(bss_symbols.items(), key=lambda item: item[1], reverse=True)[:args.top]:
        print(f"  {size:6d} {name}")

    if free_bytes < args.min_free:
        print(f"FAIL: link_free_bytes {free_bytes} < min_free {args.min_free}", file=sys.stderr)
        return 1

    print(f"PASS: link_free_bytes {free_bytes} >= min_free {args.min_free}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
