#!/usr/bin/env python3
"""Build a minimal RefMem table image package for SD/System Pack staging."""

from __future__ import annotations

import argparse
import binascii
import json
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"RMTP"
FORMAT_VERSION = 1
HEADER_SIZE = 64
TABLE_COUNT = 9
PRODUCT_ID = "RP2350_TRIG"
HARDWARE_ID = "rp2350_trig"
DEFAULT_TABLE_NAMES = (
    "ApplicationMap",
    "BoardCapability",
    "GenericNode",
    "NodeLoad",
    "FbInstance",
    "EventLink",
    "DataLink",
    "DeploymentGate",
    "ConnectionQuality",
)


@dataclass(frozen=True)
class TableEntry:
    table_id: int
    offset: int
    size: int
    crc32: int


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def build_table_payload(table_id: int, name: str) -> bytes:
    text = f"{name}:placeholder:v{FORMAT_VERSION}:table={table_id}\n".encode("ascii")
    return text.ljust(64, b"\0")


def build_package() -> tuple[bytes, list[TableEntry]]:
    payload = bytearray()
    entries: list[TableEntry] = []
    table_dir_size = TABLE_COUNT * 16
    cursor = HEADER_SIZE + table_dir_size

    for table_id, name in enumerate(DEFAULT_TABLE_NAMES):
        data = build_table_payload(table_id, name)
        entries.append(TableEntry(table_id=table_id, offset=cursor, size=len(data), crc32=crc32(data)))
        payload.extend(data)
        cursor += len(data)

    table_dir = bytearray()
    for entry in entries:
        table_dir.extend(struct.pack("<IIII", entry.table_id, entry.offset, entry.size, entry.crc32))

    payload_crc = crc32(payload)
    total_size = HEADER_SIZE + len(table_dir) + len(payload)
    header = bytearray(HEADER_SIZE)
    struct.pack_into("<4sIIIIII", header, 0, MAGIC, FORMAT_VERSION, HEADER_SIZE, total_size,
                     TABLE_COUNT, table_dir_size, payload_crc)
    package = bytes(header + table_dir + payload)
    package_crc = crc32(package)
    package = bytearray(package)
    struct.pack_into("<I", package, 28, package_crc)
    return bytes(package), entries


def write_outputs(output_dir: Path) -> None:
    refmem_dir = output_dir / "refmem"
    refmem_dir.mkdir(parents=True, exist_ok=True)

    package, entries = build_package()
    package_path = refmem_dir / "app_model.rmtp"
    package_path.write_bytes(package)

    manifest = {
        "magic": MAGIC.decode("ascii"),
        "format_version": FORMAT_VERSION,
        "product_id": PRODUCT_ID,
        "hardware_id": HARDWARE_ID,
        "table_count": TABLE_COUNT,
        "package": str(package_path.relative_to(output_dir)).replace("\\", "/"),
        "size": len(package),
        "crc32": f"{crc32(package):08X}",
        "tables": [
            {
                "table_id": entry.table_id,
                "name": DEFAULT_TABLE_NAMES[entry.table_id],
                "offset": entry.offset,
                "size": entry.size,
                "crc32": f"{entry.crc32:08X}",
            }
            for entry in entries
        ],
    }
    (refmem_dir / "app_model.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    idx = [
        "magic=RP2350_TRIG_REFMEM",
        f"schema={FORMAT_VERSION}",
        f"product_id={PRODUCT_ID}",
        f"hardware_id={HARDWARE_ID}",
        f"table_count={TABLE_COUNT}",
        f"package=/refmem/app_model.rmtp,type=refmem_table_image,size={len(package)},crc32={crc32(package):08X}",
    ]
    for entry in entries:
        idx.append(
            f"table={entry.table_id},offset={entry.offset},size={entry.size},crc32={entry.crc32:08X}"
        )
    (refmem_dir / "app_model.idx").write_text("\n".join(idx) + "\n", encoding="utf-8", newline="\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("build/refmem_pack"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    write_outputs(args.output_dir)
    print(f"refmem_pack={args.output_dir / 'refmem' / 'app_model.rmtp'}")
    print(f"refmem_idx={args.output_dir / 'refmem' / 'app_model.idx'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
