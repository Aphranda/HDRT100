#!/usr/bin/env python3
"""Verify live Flash consumers and release artifacts against a generated map."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from typing import Any


PACKAGE_MAGIC = 0x474B5054
PACKAGE_VERSION = 2
MANIFEST_EXTENSION_MAGIC = 0x4D465458
MANIFEST_EXTENSION_VERSION_SLOT_HASHES = 2
MANIFEST_REQUIRED_SIGNATURE_AND_HASHES = 3
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_BLOCK_SIZE = 512


class FlashConsumerError(ValueError):
    pass


def load_manifest(path: Path, *, allow_target_not_deployed: bool = False) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FlashConsumerError(f"cannot read manifest {path}: {exc}") from exc
    state = manifest.get("deployment_state")
    allowed_states = {"deployed_compatibility"}
    if allow_target_not_deployed:
        allowed_states.add("target_not_deployed")
    if state not in allowed_states:
        raise FlashConsumerError(
            "live consumers require deployed_compatibility; only an explicit "
            "factory-migration check may accept target_not_deployed")
    if not isinstance(manifest.get("geometry"), dict) or not isinstance(manifest.get("partitions"), list):
        raise FlashConsumerError("manifest geometry/partitions are invalid")
    ids = [item.get("id") for item in manifest["partitions"] if isinstance(item, dict)]
    required = {"BOOTLOADER", "APP_A", "APP_B", "BOOT_CONTROL"}
    if not required.issubset(ids) or len(ids) != len(set(ids)):
        raise FlashConsumerError("manifest is missing or duplicates a live partition")
    return manifest


def partitions_by_id(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {item["id"]: item for item in manifest["partitions"]}


def require_tokens(path: Path, tokens: tuple[str, ...]) -> None:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise FlashConsumerError(f"cannot read consumer {path}: {exc}") from exc
    missing = [token for token in tokens if token not in text]
    if missing:
        raise FlashConsumerError(f"{path}: missing generated-map tokens {missing}")


def check_source_consumers(root: Path) -> None:
    checks = {
        "components/ota_manager/inc/ota_partition.h": (
            "flash_deployment_map.h",
            "FLASH_DEPLOYMENT_MAP_APP_A_OFFSET",
            "FLASH_DEPLOYMENT_MAP_APP_B_OFFSET",
            "FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_OFFSET",
        ),
        "linker/rp2350_bootloader.ld": (
            "INCLUDE flash_map_active.ldinc",
            "FLASH_ACTIVE_MAP_BOOTLOADER_ORIGIN",
            "FLASH_ACTIVE_MAP_BOOTLOADER_LENGTH",
            "FLASH_IMAGE_ORIGIN",
            "FLASH_IMAGE_LENGTH",
        ),
        "linker/rp2350_app_slot_a.ld": (
            "INCLUDE flash_map_active.ldinc",
            "FLASH_ACTIVE_MAP_APP_A_ORIGIN",
            "FLASH_ACTIVE_MAP_APP_A_LENGTH",
        ),
        "linker/rp2350_app_slot_b.ld": (
            "INCLUDE flash_map_active.ldinc",
            "FLASH_ACTIVE_MAP_APP_B_ORIGIN",
            "FLASH_ACTIVE_MAP_APP_B_LENGTH",
        ),
        "CMakeLists.txt": (
            "PROJECT_FLASH_DEPLOYMENT_MAP v1_compat",
            "PROJECT_FACTORY_MIGRATION_BUILD",
            "PROJECT_ACTIVE_BOOTLOADER_XIP",
            "PROJECT_ACTIVE_APP_A_XIP",
            "PROJECT_ACTIVE_BOOT_CONTROL_XIP",
            "PROJECT_ACTIVE_RECOVERY_XIP",
            "--map-manifest",
        ),
    }
    for relative, tokens in checks.items():
        require_tokens(root / relative, tokens)


def parse_flash_memory(path: Path) -> tuple[int, int]:
    text = path.read_text(encoding="utf-8")
    match = re.search(r"^FLASH\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+", text, re.MULTILINE)
    if match is None:
        raise FlashConsumerError(f"{path}: FLASH memory row not found")
    return int(match.group(1), 16), int(match.group(2), 16)


def check_link_maps(build_dir: Path, manifest: dict[str, Any]) -> None:
    xip_base = manifest["geometry"].get("xip_base")
    if not isinstance(xip_base, int):
        raise FlashConsumerError("manifest xip_base is invalid")
    partitions = partitions_by_id(manifest)
    maps = {
        "DHRT100_BOOT.elf.map": "BOOTLOADER",
        "DHRT100.elf.map": "APP_A",
        "DHRT100_B.elf.map": "APP_B",
    }
    for filename, partition_id in maps.items():
        partition = partitions[partition_id]
        actual = parse_flash_memory(build_dir / filename)
        expected = (xip_base + partition["offset"], partition["size"])
        if actual != expected:
            raise FlashConsumerError(
                f"{filename}: FLASH origin/length {actual!r} != manifest {expected!r}")


def check_binary_sizes(build_dir: Path, manifest: dict[str, Any]) -> None:
    partitions = partitions_by_id(manifest)
    candidate = "RECOVERY" in partitions
    binaries = {
        "DHRT100_BOOT.bin": "BOOTLOADER",
        "DHRT100.bin": "APP_A",
        "DHRT100_B.bin": "APP_B",
        ("factory_boot_control.bin" if candidate
         else "ota_metadata_clear.bin"): "BOOT_CONTROL",
    }
    if "RECOVERY" in partitions:
        binaries["DHRT100_RECOVERY.bin"] = "RECOVERY"
    for filename, partition_id in binaries.items():
        path = build_dir / filename
        size = path.stat().st_size
        capacity = partitions[partition_id]["size"]
        if size > capacity:
            raise FlashConsumerError(f"{filename}: size {size} exceeds {partition_id} capacity {capacity}")
    metadata_name = ("factory_boot_control.bin" if candidate
                     else "ota_metadata_clear.bin")
    metadata_size = (build_dir / metadata_name).stat().st_size
    if metadata_size != partitions["BOOT_CONTROL"]["size"]:
        raise FlashConsumerError(
            f"{metadata_name} must cover the complete BOOT_CONTROL partition")
    if candidate:
        if not (build_dir / "factory_map_manifest.bin").exists() or not (
                build_dir / "factory_region_report.json").exists():
            raise FlashConsumerError(
                "v2 factory baseline manifest/report is missing")


def check_factory_report(build_dir: Path, manifest: dict[str, Any]) -> None:
    report_path = build_dir / "factory_region_report.json"
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FlashConsumerError(f"cannot read factory region report: {exc}") from exc
    if (report.get("deployment_state") != manifest.get("deployment_state") or
            report.get("map_version") != manifest.get("map_version") or
            report.get("full_erase_required") is not True):
        raise FlashConsumerError("factory region report identity/policy is invalid")
    region_list = report.get("regions")
    if not isinstance(region_list, list):
        raise FlashConsumerError("factory region report has no region list")
    regions = {
        item.get("partition"): item for item in region_list
        if isinstance(item, dict) and isinstance(item.get("partition"), str)
    }
    expected = {
        "BOOTLOADER": "DHRT100_BOOT.bin",
        "APP_A": "DHRT100.bin",
        "RECOVERY": "DHRT100_RECOVERY.bin",
        "BOOT_CONTROL": "factory_boot_control.bin",
        "OTA_STAGE": "factory_map_manifest.bin",
    }
    if set(regions) != set(expected):
        raise FlashConsumerError("factory region report coverage is incomplete")
    for partition_id, filename in expected.items():
        data = (build_dir / filename).read_bytes()
        region = regions[partition_id]
        if (region.get("size") != len(data) or
                region.get("sha256") != hashlib.sha256(data).hexdigest()):
            raise FlashConsumerError(
                f"factory region report hash drifted for {partition_id}")


def check_ota_package(build_dir: Path, manifest: dict[str, Any],
                      package_name: str = "DHRT100_UPDATE.pkg", *,
                      require_signature: bool = False) -> None:
    package = (build_dir / package_name).read_bytes()
    if len(package) < 256:
        raise FlashConsumerError("OTA package is shorter than its descriptor table")
    magic, version, _, package_size, _, image_count = struct.unpack_from("<IIIIII", package, 0)
    if (magic, version, package_size, image_count) != (PACKAGE_MAGIC, PACKAGE_VERSION, len(package), 2):
        raise FlashConsumerError("OTA package header is invalid")
    if require_signature:
        extension_magic, extension_version, required_flags, counter, key_id, signature_size = (
            struct.unpack_from("<IIIIII", package, 256))
        signature = package[280:344]
        if (extension_magic != MANIFEST_EXTENSION_MAGIC or
                extension_version != MANIFEST_EXTENSION_VERSION_SLOT_HASHES or
                    required_flags != MANIFEST_REQUIRED_SIGNATURE_AND_HASHES or
                    counter == 0 or key_id == 0 or
                signature_size != 64 or len(signature) != 64 or not any(signature)):
            raise FlashConsumerError("v2 candidate OTA package is not fully signed")
    partitions = partitions_by_id(manifest)
    expected = ((1, "APP_A", "DHRT100.bin"), (2, "APP_B", "DHRT100_B.bin"))
    for index, (expected_slot, partition_id, filename) in enumerate(expected):
        slot, payload_offset, size, _, run_offset, _ = struct.unpack_from(
            "<IIIIII", package, 192 + index * 32)
        partition = partitions[partition_id]
        if slot != expected_slot or run_offset != partition["offset"]:
            raise FlashConsumerError(f"OTA {partition_id} descriptor disagrees with manifest")
        if size != (build_dir / filename).stat().st_size or size > partition["size"]:
            raise FlashConsumerError(f"OTA {partition_id} descriptor size is invalid")
        if payload_offset + size > len(package):
            raise FlashConsumerError(f"OTA {partition_id} payload exceeds package")


def uf2_target_addresses(path: Path) -> set[int]:
    data = path.read_bytes()
    if not data or len(data) % UF2_BLOCK_SIZE != 0:
        raise FlashConsumerError(f"{path}: invalid UF2 length")
    addresses: set[int] = set()
    for cursor in range(0, len(data), UF2_BLOCK_SIZE):
        block = data[cursor:cursor + UF2_BLOCK_SIZE]
        start0, start1, _, address, payload_size = struct.unpack_from("<IIIII", block, 0)
        end_magic = struct.unpack_from("<I", block, UF2_BLOCK_SIZE - 4)[0]
        if (start0, start1, end_magic) != (UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_MAGIC_END):
            raise FlashConsumerError(f"{path}: invalid UF2 block magic")
        if payload_size != 256:
            raise FlashConsumerError(f"{path}: invalid UF2 payload size {payload_size}")
        if address in addresses:
            raise FlashConsumerError(f"{path}: duplicate UF2 target address 0x{address:08X}")
        addresses.add(address)
    return addresses


def check_factory_uf2(build_dir: Path, manifest: dict[str, Any],
                      factory_name: str = "DHRT100_FACTORY.uf2") -> None:
    xip_base = manifest["geometry"]["xip_base"]
    partitions = partitions_by_id(manifest)
    expected_addresses: set[int] = set()
    candidate = "RECOVERY" in partitions
    inputs = (
        ("DHRT100_BOOT.bin", "BOOTLOADER"),
        ("DHRT100.bin", "APP_A"),
        (("factory_boot_control.bin" if candidate
          else "ota_metadata_clear.bin"), "BOOT_CONTROL"),
    )
    if candidate:
        inputs += (
            ("DHRT100_RECOVERY.bin", "RECOVERY"),
            ("factory_map_manifest.bin", "OTA_STAGE"),
        )
    for filename, partition_id in inputs:
        size = (build_dir / filename).stat().st_size
        start = xip_base + partitions[partition_id]["offset"]
        expected_addresses.update(range(start, start + size, 256))
    actual_addresses = uf2_target_addresses(build_dir / factory_name)
    if actual_addresses != expected_addresses:
        extra = sorted(actual_addresses - expected_addresses)
        missing = sorted(expected_addresses - actual_addresses)
        raise FlashConsumerError(
            f"factory UF2 targets drifted: extra={extra[:3]} missing={missing[:3]}")


def check_artifacts(build_dir: Path, manifest: dict[str, Any], *,
                    package_name: str = "DHRT100_UPDATE.pkg",
                    factory_name: str = "DHRT100_FACTORY.uf2",
                    package_optional: bool = False,
                    require_signed_package: bool = False) -> None:
    check_link_maps(build_dir, manifest)
    check_binary_sizes(build_dir, manifest)
    if "RECOVERY" in partitions_by_id(manifest):
        check_factory_report(build_dir, manifest)
    if not package_optional or (build_dir / package_name).exists():
        check_ota_package(
            build_dir, manifest, package_name,
            require_signature=require_signed_package)
    check_factory_uf2(build_dir, manifest, factory_name)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--manifest", type=Path,
                        default=Path("config/flash_map_gen/flash_map_v1_compat_manifest.json"))
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--allow-target-not-deployed", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    manifest_path = args.manifest if args.manifest.is_absolute() else root / args.manifest
    build_dir = args.build_dir if args.build_dir.is_absolute() else root / args.build_dir
    try:
        manifest = load_manifest(
            manifest_path,
            allow_target_not_deployed=args.allow_target_not_deployed,
        )
        check_source_consumers(root)
        candidate = manifest.get("deployment_state") == "target_not_deployed"
        check_artifacts(
            build_dir,
            manifest,
            package_name=("DHRT100_V2_CANDIDATE_UPDATE.pkg" if candidate
                          else "DHRT100_UPDATE.pkg"),
            factory_name=("DHRT100_V2_CANDIDATE_FACTORY.uf2" if candidate
                          else "DHRT100_FACTORY.uf2"),
            package_optional=candidate,
            require_signed_package=candidate,
        )
    except (FlashConsumerError, OSError, KeyError, TypeError) as exc:
        print(f"flash_consumer_check=FAILED detail={exc}")
        return 1
    print("flash_consumer_check=OK source_consumers=5 artifact_groups=4")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
