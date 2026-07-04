#!/usr/bin/env python3
"""Build the RP2350_TRIG SD-card filesystem staging tree."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import os
import shutil
import stat
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
SCHEMA_VERSION = 1

SYSTEM_LAYOUT = (
    "profile",
    "profile/profiles",
    "mission",
    "cal",
    "snapshots/boot",
    "snapshots/arm",
    "snapshots/fault",
    "snapshots/run",
    "traces/run",
    "traces/fault",
    "reports/run",
    "reports/fault",
    "reports/acceptance",
    "logs",
    "update",
    "update/compat",
    "factory",
)

LEGACY_LAYOUT = (
    "config",
    "capture",
    "resource",
)


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def crc32_text(value: str) -> str:
    return value.removeprefix("0x").upper()


def file_info(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    checksum = crc32(data)
    return {
        "path": str(path),
        "size": len(data),
        "crc32": f"0x{checksum:08X}",
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


def clean_output_dir(output_dir: Path) -> None:
    resolved = output_dir.resolve()
    if len(resolved.parts) < 3:
        raise ValueError(f"refusing to clean unsafe output directory: {resolved}")
    if resolved.exists():
        def clear_readonly_and_retry(function, path, _exc_info):
            os.chmod(path, stat.S_IWRITE)
            function(path)

        shutil.rmtree(resolved, onerror=clear_readonly_and_retry)


def ensure_layout(output_dir: Path, include_legacy: bool) -> list[str]:
    layout = list(SYSTEM_LAYOUT)
    if include_legacy:
        layout.extend(LEGACY_LAYOUT)
    for relative in layout:
        (output_dir / relative).mkdir(parents=True, exist_ok=True)
    return ["/" + relative for relative in layout]


def write_json(path: Path, payload: dict[str, Any], sd_root: Path) -> dict[str, Any]:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    info = file_info(path)
    info["sd_path"] = "/" + path.relative_to(sd_root).as_posix()
    return info


def write_default_system_files(output_dir: Path, package_manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    common = {
        "schema": SCHEMA_VERSION,
        "product_id": package_manifest["product_id"],
        "hardware_id": package_manifest["hardware_id"],
        "build_id": package_manifest["build_id"],
        "source": "sd_fs_build",
    }
    written: dict[str, dict[str, Any]] = {}

    profile = {
        "magic": "RP2350_TRIG_PROFILE",
        **common,
        "name": "default",
        "trigger": {
            "mode": "IDLE",
            "armed": False,
        },
    }
    written["profile"] = write_json(output_dir / "profile" / "active.json", profile, output_dir)

    mission = {
        "magic": "RP2350_TRIG_MISSION",
        **common,
        "name": "default",
        "steps": [],
    }
    written["mission"] = write_json(output_dir / "mission" / "recipe.json", mission, output_dir)

    node_map = {
        "magic": "RP2350_TRIG_NODE_MAP",
        **common,
        "nodes": {
            "A0": "unassigned",
            "A1": "unassigned",
            "A2": "unassigned",
            "A3": "unassigned",
        },
    }
    written["node_map"] = write_json(output_dir / "mission" / "node_map.json", node_map, output_dir)

    calibration = {
        "magic": "RP2350_TRIG_CAL",
        **common,
        "board": {
            "timebase_ppm": 0,
            "trigger_delay_ns": 0,
        },
    }
    written["calibration"] = write_json(output_dir / "cal" / "board_cal.json", calibration, output_dir)

    sequence = output_dir / "mission" / "sequence.bin"
    sequence.write_bytes(b"")
    written["sequence"] = file_info(sequence)
    written["sequence"]["sd_path"] = "/" + sequence.relative_to(output_dir).as_posix()
    return written


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


def write_manifest_idx(output_dir: Path,
                       package_manifest: dict[str, Any],
                       files: dict[str, dict[str, Any]]) -> None:
    required = (
        ("profile", "profile"),
        ("mission", "mission"),
        ("calibration", "calibration"),
        ("ota_package", "package"),
    )
    lines = [
        "magic=RP2350_TRIG_SD",
        f"schema={SCHEMA_VERSION}",
        f"product_id={package_manifest['product_id']}",
        f"hardware_id={package_manifest['hardware_id']}",
        f"pack_version={package_manifest['app_version']}",
        f"min_firmware={package_manifest['app_version']}",
        f"build_id={package_manifest['build_id']}",
        "default.profile=/profile/active.json",
        "default.mission=/mission/recipe.json",
        "default.calibration=/cal/board_cal.json",
        "default.ota_package=/update/RP2350_TRIG_UPDATE.pkg",
    ]
    for object_type, key in required:
        info = files[key]
        lines.append(
            f"required={info['sd_path']},type={object_type},"
            f"size={info['size']},crc32={crc32_text(info['crc32'])}"
        )
    (output_dir / "manifest.idx").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR, help="firmware build directory")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR, help="SD-card staging directory")
    parser.add_argument("--package", type=Path, help="unified OTA package, default: <build-dir>/RP2350_TRIG_UPDATE.pkg")
    parser.add_argument("--factory", type=Path, help="factory UF2, default: <build-dir>/RP2350_TRIG_FACTORY.uf2")
    parser.add_argument("--no-raw-compat", action="store_true", help="do not include raw .bin compatibility payloads")
    parser.add_argument("--no-reports", action="store_true", help="do not copy latest OTA validation summaries")
    parser.add_argument("--no-zip", action="store_true", help="do not create RP2350_TRIG_SDCARD.zip")
    parser.add_argument("--legacy-layout", action="store_true", help="also create legacy /config, /capture, /resource")
    parser.add_argument("--clean", action="store_true", help="remove the output directory before generating it")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    output_dir = args.output_dir.resolve()
    package = (args.package or build_dir / "RP2350_TRIG_UPDATE.pkg").resolve()
    factory = (args.factory or build_dir / "RP2350_TRIG_FACTORY.uf2").resolve()

    if args.clean:
        clean_output_dir(output_dir)
    layout = ensure_layout(output_dir, args.legacy_layout)

    package_manifest = parse_package(package)
    copied_package = copy_file(package, output_dir / "update" / "RP2350_TRIG_UPDATE.pkg", output_dir)
    factory_manifest = copy_file(factory, output_dir / "factory" / "RP2350_TRIG_FACTORY.uf2", output_dir)
    system_files = write_default_system_files(output_dir, package_manifest)

    raw_compat: list[dict[str, Any]] = []
    if not args.no_raw_compat:
        for name in ("RP2350_TRIG.bin", "RP2350_TRIG_B.bin"):
            src = build_dir / name
            if src.exists():
                raw_compat.append(copy_file(src, output_dir / "update" / "compat" / name, output_dir))

    reports = [] if args.no_reports else copy_validation_reports(build_dir, output_dir)

    if args.legacy_layout:
        default_config = {
            "product_id": package_manifest["product_id"],
            "hardware_id": package_manifest["hardware_id"],
            "ota_default_file": "/update/RP2350_TRIG_UPDATE.pkg",
            "ota_default_format": "unified_pkg",
            "ota_mode": "DIRECT_AB",
            "raw_bin_compatibility_dir": "/update/compat",
        }
        write_json(output_dir / "config" / "default_config.json", default_config, output_dir)

    readme = (
        "RP2350_TRIG SD card filesystem\n"
        "================================\n\n"
        "Copy the contents of this directory to the root of a FAT32 SD card.\n"
        "The firmware-readable index is /manifest.idx; /manifest.json is for tools.\n"
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
        "layout": layout,
        "defaults": {
            "profile": "/profile/active.json",
            "mission": "/mission/recipe.json",
            "node_map": "/mission/node_map.json",
            "calibration": "/cal/board_cal.json",
        },
        "ota_default": {
            "format": "unified_pkg",
            "path": "/update/RP2350_TRIG_UPDATE.pkg",
            "mode": "DIRECT_AB",
            "raw_bin_compatibility": not args.no_raw_compat,
        },
        "system_files": system_files,
        "package": package_manifest | {"sd_copy": copied_package},
        "factory": factory_manifest,
        "raw_compat": raw_compat,
        "reports": reports,
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    write_manifest_idx(
        output_dir,
        package_manifest,
        {
            **system_files,
            "package": copied_package,
        },
    )

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
    print(f"manifest_idx={output_dir / 'manifest.idx'}")
    if not args.no_zip:
        print(f"zip={zip_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
