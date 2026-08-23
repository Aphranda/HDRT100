#!/usr/bin/env python3
"""Build a v2 factory BCB baseline, map-manifest blob, and region report."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.ota_keys.sign_ota_signature import sign_transcript
from tools.ota_packager import ota_packager


DEFINE_RE = re.compile(r"^#define\s+(?P<name>[A-Z0-9_]+)\s+(?P<value>0x[0-9A-Fa-f]+|[0-9]+)u?$")
METADATA_FORMAT = "<10I32s2I32s20I"
METADATA_SIZE = struct.calcsize(METADATA_FORMAT)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map-header", type=Path, required=True)
    parser.add_argument("--map-manifest", type=Path, required=True)
    parser.add_argument("--bcb-header", type=Path, required=True)
    parser.add_argument("--metadata-header", type=Path, required=True)
    parser.add_argument("--bootloader", type=Path, required=True)
    parser.add_argument("--app-a", type=Path, required=True)
    parser.add_argument("--app-b", type=Path, required=True)
    parser.add_argument("--build-id-file", type=Path, required=True)
    parser.add_argument("--recovery", type=Path, required=True)
    parser.add_argument("--boot-control", type=Path, required=True)
    parser.add_argument("--manifest-blob", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--signing-key", type=Path, required=True)
    parser.add_argument("--key-id", type=int, required=True)
    parser.add_argument("--security-counter", type=int, required=True)
    return parser.parse_args()


def load_defines(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = DEFINE_RE.match(line.strip())
        if match is not None:
            values[match.group("name")] = int(match.group("value"), 0)
    return values


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def replace_u32(data: bytes, offset: int, value: int) -> bytes:
    result = bytearray(data)
    struct.pack_into("<I", result, offset, value)
    return bytes(result)


def build_metadata(app: bytes, metadata_defines: dict[str, int]) -> bytes:
    values: list[object] = [
        metadata_defines["OTA_METADATA_MAGIC"],
        metadata_defines["OTA_METADATA_VERSION"],
        1, 1, 0, 1, 0, 0, len(app), crc32(app),
        bytes(32), 0, 0, bytes(32),
        0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 0, 0, 3, 0,
    ]
    metadata = struct.pack(METADATA_FORMAT, *values)
    if len(metadata) != METADATA_SIZE or METADATA_SIZE > 220:
        raise ValueError("factory metadata layout does not fit the BCB payload")
    metadata = replace_u32(metadata, 128, crc32(replace_u32(metadata[:132], 128, 0)))
    metadata = replace_u32(metadata, 168, crc32(replace_u32(metadata[132:172], 36, 0)))
    metadata = replace_u32(metadata, 188, crc32(replace_u32(metadata[172:192], 16, 0)))
    return metadata


def build_boot_control(map_defines: dict[str, int],
                       bcb_defines: dict[str, int],
                       metadata_defines: dict[str, int],
                       app: bytes) -> bytes:
    page_size = map_defines["FLASH_GEOMETRY_PROGRAM_SIZE_BYTES"]
    partition_size = map_defines["FLASH_MAP_BOOT_CONTROL_SIZE"]
    lane_count = bcb_defines["POTA_BCB_LANE_COUNT"]
    lane_size = partition_size // lane_count
    lane_pages = lane_size // page_size
    if (page_size != bcb_defines["POTA_BCB_PAGE_SIZE"] or
            partition_size % lane_count != 0 or lane_pages < 3):
        raise ValueError("unsupported BCB factory geometry")

    metadata = build_metadata(app, metadata_defines)
    payload_capacity = bcb_defines["POTA_BCB_BODY_PAYLOAD_SIZE"]
    if len(metadata) > payload_capacity:
        raise ValueError("factory metadata does not fit the BCB payload")
    payload = metadata + bytes(payload_capacity - len(metadata))
    body = bytearray(b"\xFF" * page_size)
    struct.pack_into("<7I", body, 0,
                     bcb_defines["POTA_BCB_BODY_MAGIC"],
                     map_defines["FLASH_MAP_SCHEMA_VERSION"],
                     map_defines["FLASH_MAP_VERSION"], 1, 0, 0,
                     len(metadata))
    struct.pack_into("<I", body, 28, crc32(metadata))
    body[36:] = payload
    struct.pack_into("<I", body, 32, crc32(replace_u32(bytes(body), 32, 0)))

    body_crc = struct.unpack_from("<I", body, 32)[0]
    commit = bytearray(b"\xFF" * page_size)
    struct.pack_into("<7I", commit, 0,
                     bcb_defines["POTA_BCB_COMMIT_MAGIC"],
                     map_defines["FLASH_MAP_SCHEMA_VERSION"],
                     map_defines["FLASH_MAP_VERSION"], 1, 1, body_crc,
                     bcb_defines["POTA_BCB_COMMIT_MARKER"] ^ 1)

    seal = bytearray(b"\xFF" * page_size)
    struct.pack_into("<6I", seal, 0,
                     bcb_defines["POTA_BCB_SEAL_MAGIC"],
                     map_defines["FLASH_MAP_SCHEMA_VERSION"],
                     map_defines["FLASH_MAP_VERSION"], 1, 0,
                     bcb_defines["POTA_BCB_SEAL_MARKER"] ^ 1)
    struct.pack_into("<I", seal, 16, crc32(replace_u32(bytes(seal), 16, 0)))

    output = bytearray(b"\xFF" * partition_size)
    output[0:page_size] = body
    output[page_size:2 * page_size] = commit
    seal_offset = (lane_pages - 1) * page_size
    output[seal_offset:seal_offset + page_size] = seal
    return bytes(output)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_signed_slot_manifest(map_manifest: Path, app_a: bytes, app_b: bytes,
                               build_id_file: Path, signing_key: Path,
                               key_id: int, security_counter: int,
                               map_defines: dict[str, int]) -> bytes:
    layout = ota_packager.load_deployment_layout(
        map_manifest, allow_target_not_deployed=True)
    build_id = ota_packager.read_build_id(
        argparse.Namespace(build_id_file=build_id_file, build_id=""))
    common = dict(
        product_id="DHRT100", hardware_id="dhrt100",
        app_version=(0, 1, 0), build_id=build_id,
        min_bootloader_version=(0, 1, 0), layout=layout,
        security_counter=security_counter, key_id=key_id,
    )
    unsigned = ota_packager.build_package(app_a, app_b, **common,
                                           prepare_signing=True)
    transcript = ota_packager.build_signing_transcript(unsigned)
    signature = sign_transcript(signing_key.read_bytes(), transcript)
    package = ota_packager.build_package(app_a, app_b, **common,
                                         signature=signature)
    header = package[:ota_packager.PACKAGE_HEADER_SIZE]
    if len(header) != 512:
        raise ValueError("signed package header size drifted")

    page_size = map_defines["FLASH_GEOMETRY_PROGRAM_SIZE_BYTES"]
    erase_size = map_defines["FLASH_GEOMETRY_ERASE_SIZE_BYTES"]
    if page_size != 256 or erase_size != 4096:
        raise ValueError("unsupported v2 manifest geometry")
    body = bytearray(b"\xFF" * 768)
    struct.pack_into("<6I", body, 0, 0x534D4244, 1,
                     map_defines["FLASH_MAP_VERSION"], 1, 1, 512)
    body[24:24 + 4] = struct.pack("<I", crc32(header))
    body[32:32 + len(header)] = header
    body_crc = crc32(bytes(body[:28] + b"\x00\x00\x00\x00" + body[32:]))
    struct.pack_into("<I", body, 28, body_crc)
    commit = bytearray(b"\xFF" * 256)
    struct.pack_into("<7I", commit, 0, 0x534D434D, 1,
                     map_defines["FLASH_MAP_VERSION"], 1, 1, body_crc,
                     0x51A7C04D ^ 1)
    lane = bytearray(b"\xFF" * erase_size)
    lane[:768] = body
    lane[768:1024] = commit
    return bytes(lane)


def main() -> int:
    args = parse_args()
    map_defines = load_defines(args.map_header)
    bcb_defines = load_defines(args.bcb_header)
    metadata_defines = load_defines(args.metadata_header)
    bootloader = args.bootloader.read_bytes()
    app = args.app_a.read_bytes()
    app_b = args.app_b.read_bytes()
    recovery = args.recovery.read_bytes()
    manifest_data = json.loads(args.map_manifest.read_text(encoding="utf-8"))
    if manifest_data.get("deployment_state") != "target_not_deployed":
        raise ValueError("factory baseline requires target_not_deployed manifest")
    if (manifest_data.get("schema_version") !=
            map_defines["FLASH_MAP_SCHEMA_VERSION"] or
            manifest_data.get("map_version") !=
            map_defines["FLASH_MAP_VERSION"]):
        raise ValueError("map header and manifest versions disagree")

    canonical_manifest = (json.dumps(manifest_data, indent=2, sort_keys=True) + "\n").encode("utf-8")
    slot_manifest = build_signed_slot_manifest(
        args.map_manifest, app, app_b, args.build_id_file, args.signing_key,
        args.key_id, args.security_counter, map_defines)
    partitions = {item["id"]: item for item in manifest_data["partitions"]}
    ota_stage_size = partitions["OTA_STAGE"]["size"]
    if len(canonical_manifest) + len(slot_manifest) > ota_stage_size:
        raise ValueError("factory OTA_STAGE content exceeds partition capacity")
    ota_stage = bytearray(b"\xFF" * ota_stage_size)
    ota_stage[:len(canonical_manifest)] = canonical_manifest
    slot_offset = ota_stage_size - 4 * map_defines["FLASH_GEOMETRY_ERASE_SIZE_BYTES"]
    ota_stage[slot_offset:slot_offset + len(slot_manifest)] = slot_manifest
    canonical_manifest = bytes(ota_stage)
    boot_control = build_boot_control(map_defines, bcb_defines,
                                      metadata_defines, app)
    for partition_id, size in (
        ("BOOTLOADER", len(bootloader)),
        ("APP_A", len(app)),
        ("RECOVERY", len(recovery)),
        ("BOOT_CONTROL", len(boot_control)),
        ("OTA_STAGE", len(canonical_manifest)),
    ):
        if size > partitions[partition_id]["size"]:
            raise ValueError(
                f"{partition_id} factory baseline exceeds partition capacity")
    args.boot_control.write_bytes(boot_control)
    args.manifest_blob.write_bytes(canonical_manifest)

    report = {
        "schema": 1,
        "product": "DHRT100",
        "deployment_state": manifest_data["deployment_state"],
        "map_version": manifest_data["map_version"],
        "full_erase_required": True,
        "regions": [
            {"partition": "BOOTLOADER", "size": len(bootloader),
             "sha256": sha256(bootloader)},
            {"partition": "APP_A", "size": len(app), "sha256": sha256(app)},
            {"partition": "RECOVERY", "size": len(recovery),
             "sha256": sha256(recovery)},
            {"partition": "BOOT_CONTROL", "size": len(boot_control),
             "sha256": sha256(boot_control), "baseline": "valid_bcb_lane0"},
            {"partition": "OTA_STAGE", "size": len(canonical_manifest),
             "sha256": sha256(canonical_manifest), "baseline": "map_manifest"},
        ],
        "erased_store_partitions": [
            item["id"] for item in manifest_data["partitions"]
            if item["store_type"] not in {"image", "boot_control", "reserved"}
            and item["id"] != "OTA_STAGE"
        ],
        "boot_control_capacity": partitions["BOOT_CONTROL"]["size"],
    }
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8", newline="\n")
    print(f"factory_boot_control={args.boot_control}")
    print(f"factory_map_manifest={args.manifest_blob}")
    print(f"factory_region_report={args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
