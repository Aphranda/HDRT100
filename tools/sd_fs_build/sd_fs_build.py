#!/usr/bin/env python3
"""Build the RP2350_TRIG SD-card filesystem staging tree."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import shutil
import struct
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUILD_DIR = ROOT / "build"
DEFAULT_OUTPUT_DIR = DEFAULT_BUILD_DIR / "sdcard"
PACKAGE_MAGIC = 0x474B5054
PACKAGE_HEADER_SIZE = 512
TEXT_FIELD_SIZE = 32


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def file_info(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    return {
        "path": str(path),
        "size": len(data),
        "crc32": f"0x{crc32(data):08X}",
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def read_text_field(header: bytes, offset: int) -> str:
    raw = header[offset : offset + TEXT_FIELD_SIZE]
    return raw.split(b"\x00", 1)[0].split(b"\xFF", 1)[0].decode("ascii", errors="replace")


def read_u32(header: bytes, offset: int) -> int:
    return struct.unpack_from("<I", header, offset)[0]


def parse_package(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < PACKAGE_HEADER_SIZE:
        raise ValueError(f"package too small: {path}")
    header = data[:PACKAGE_HEADER_SIZE]
    magic = read_u32(header, 0)
    if magic != PACKAGE_MAGIC:
        raise ValueError(f"not a unified OTA package: {path}")

    info = file_info(path)
    package_size = read_u32(header, 12)
    if package_size != len(data):
        raise ValueError(f"package header size {package_size} does not match file size {len(data)}: {path}")

    images = []
    image_count = read_u32(header, 20)
    for index in range(image_count):
        cursor = 192 + index * 32
        slot, offset, size, image_crc, run_offset, flags = struct.unpack_from("<IIIIII", header, cursor)
        images.append(
            {
                "slot": slot,
                "offset": offset,
                "size": size,
                "crc32": f"0x{image_crc:08X}",
                "run_offset": f"0x{run_offset:08X}",
                "flags": f"0x{flags:08X}",
            }
        )

    info.update(
        {
            "magic": f"0x{magic:08X}",
            "version": read_u32(header, 4),
            "header_size": read_u32(header, 8),
            "package_size": package_size,
            "image_count": image_count,
            "product_id": read_text_field(header, 32),
            "hardware_id": read_text_field(header, 64),
            "app_version": f"{read_u32(header, 96)}.{read_u32(header, 100)}.{read_u32(header, 104)}",
            "min_bootloader_version_packed": f"0x{read_u32(header, 108):08X}",
            "build_id": read_text_field(header, 112),
            "payload_sha256": header[144:176].hex(),
            "images": images,
        }
    )
    return info


def copy_file(src: Path, dst: Path, sd_root: Path) -> dict[str, Any]:
    if not src.exists():
        raise FileNotFoundError(src)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    info = file_info(dst)
    info["source"] = str(src)
    info["sd_path"] = "/" + dst.relative_to(sd_root).as_posix()
    return info


def ensure_layout(output_dir: Path) -> None:
    for relative in (
        "update",
        "update/compat",
        "factory",
        "logs",
        "config",
        "capture",
        "resource",
        "reports",
    ):
        (output_dir / relative).mkdir(parents=True, exist_ok=True)


def latest_validation_dirs(build_dir: Path) -> list[Path]:
    dirs = [path for path in build_dir.glob("ota_validation*") if path.is_dir()]
    return sorted(dirs, key=lambda path: path.stat().st_mtime, reverse=True)[:3]


def copy_validation_reports(build_dir: Path, output_dir: Path) -> list[dict[str, str]]:
    copied: list[dict[str, str]] = []
    for validation_dir in latest_validation_dirs(build_dir):
        report_dir = output_dir / "reports" / validation_dir.name
        for name in ("summary.txt", "summary.json"):
            src = validation_dir / name
            if src.exists():
                dst = report_dir / name
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
                copied.append({"source": str(src), "sd_path": "/" + dst.relative_to(output_dir).as_posix()})
    return copied


def write_zip(output_dir: Path, zip_path: Path) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, mode="w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(output_dir.rglob("*")):
            archive.write(path, path.relative_to(output_dir))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR, help="firmware build directory")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR, help="SD-card staging directory")
    parser.add_argument("--package", type=Path, help="unified OTA package, default: <build-dir>/RP2350_TRIG_UPDATE.pkg")
    parser.add_argument("--factory", type=Path, help="factory UF2, default: <build-dir>/RP2350_TRIG_FACTORY.uf2")
    parser.add_argument("--no-raw-compat", action="store_true", help="do not include raw .bin compatibility payloads")
    parser.add_argument("--no-reports", action="store_true", help="do not copy latest OTA validation summaries")
    parser.add_argument("--no-zip", action="store_true", help="do not create RP2350_TRIG_SDCARD.zip")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    output_dir = args.output_dir.resolve()
    package = (args.package or build_dir / "RP2350_TRIG_UPDATE.pkg").resolve()
    factory = (args.factory or build_dir / "RP2350_TRIG_FACTORY.uf2").resolve()

    ensure_layout(output_dir)

    package_manifest = parse_package(package)
    copied_package = copy_file(package, output_dir / "update" / "RP2350_TRIG_UPDATE.pkg", output_dir)
    factory_manifest = copy_file(factory, output_dir / "factory" / "RP2350_TRIG_FACTORY.uf2", output_dir)

    raw_compat: list[dict[str, Any]] = []
    if not args.no_raw_compat:
        for name in ("RP2350_TRIG.bin", "RP2350_TRIG_B.bin"):
            src = build_dir / name
            if src.exists():
                raw_compat.append(copy_file(src, output_dir / "update" / "compat" / name, output_dir))

    reports = [] if args.no_reports else copy_validation_reports(build_dir, output_dir)

    default_config = {
        "product_id": package_manifest["product_id"],
        "hardware_id": package_manifest["hardware_id"],
        "ota_default_file": "/update/RP2350_TRIG_UPDATE.pkg",
        "ota_default_format": "unified_pkg",
        "ota_mode": "DIRECT_AB",
        "raw_bin_compatibility_dir": "/update/compat",
    }
    (output_dir / "config" / "default_config.json").write_text(
        json.dumps(default_config, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    readme = (
        "RP2350_TRIG SD card filesystem\n"
        "================================\n\n"
        "Copy the contents of this directory to the root of a FAT32 SD card.\n"
        "The default offline OTA payload is /update/RP2350_TRIG_UPDATE.pkg.\n"
        "Raw .bin files under /update/compat are kept for compatibility and bench work.\n"
        "Bootloader recovery still uses /factory/RP2350_TRIG_FACTORY.uf2 through BOOTSEL.\n"
    )
    (output_dir / "README.txt").write_text(readme, encoding="utf-8")
    (output_dir / "update" / "last_result.txt").write_text("NO_RESULT\n", encoding="utf-8")

    manifest = {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "architecture": "HAOFV",
        "boundary": "SD is a maintenance medium; device-side storage_manager should feed OtaAO events.",
        "output_dir": str(output_dir),
        "layout": ["/update", "/update/compat", "/factory", "/logs", "/config", "/capture", "/resource", "/reports"],
        "ota_default": {
            "format": "unified_pkg",
            "path": "/update/RP2350_TRIG_UPDATE.pkg",
            "mode": "DIRECT_AB",
            "raw_bin_compatibility": not args.no_raw_compat,
        },
        "package": package_manifest | {"sd_copy": copied_package},
        "factory": factory_manifest,
        "raw_compat": raw_compat,
        "reports": reports,
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    zip_path = build_dir / "RP2350_TRIG_SDCARD.zip"
    if not args.no_zip:
        write_zip(output_dir, zip_path)

    print(f"sd_output={output_dir}")
    print("ota_default=/update/RP2350_TRIG_UPDATE.pkg")
    print(f"package_size={package_manifest['size']}")
    print(f"package_crc32={package_manifest['crc32']}")
    print(f"build_id={package_manifest['build_id']}")
    print(f"factory=/factory/RP2350_TRIG_FACTORY.uf2")
    print(f"manifest={output_dir / 'manifest.json'}")
    if not args.no_zip:
        print(f"zip={zip_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
