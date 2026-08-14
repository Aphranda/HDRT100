#!/usr/bin/env python3
"""Build a minimal RefMem table image package for SD/System Pack staging."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.refmem_table_image import refmem_table_image


MAGIC = refmem_table_image.MAGIC
FORMAT_VERSION = refmem_table_image.FORMAT_VERSION
HEADER_SIZE = refmem_table_image.HEADER_SIZE
TABLE_COUNT = refmem_table_image.TABLE_COUNT
PRODUCT_ID = "RP2350_TRIG"
HARDWARE_ID = "rp2350_trig"
DEFAULT_TABLE_NAMES = refmem_table_image.TABLE_NAMES
NODE_COUNT = refmem_table_image.NODE_COUNT
BOARD_CAPABILITY_COUNT = refmem_table_image.BOARD_CAPABILITY_COUNT
NODE_LOAD_COUNT = refmem_table_image.NODE_LOAD_COUNT
crc32 = refmem_table_image.crc32
build_package = refmem_table_image.build_package


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
