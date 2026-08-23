#!/usr/bin/env python3
"""Build and verify a signed DHRT100 factory-recovery package.

The package is deliberately independent of UF2 transport.  It contains only
the regions which a v2 factory baseline is allowed to program, a canonical
JSON descriptor, and an externally-produced low-S P-256 signature.  Private
keys never enter this tool or the repository.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import struct
import sys
from pathlib import Path
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.ota_keys import gen_ota_key_registry  # noqa: E402
from tools.ota_keys.verify_ota_signature import verify_signature  # noqa: E402


FACTORY_PACKAGE_MAGIC = 0x46414350  # "FACP"
FACTORY_PACKAGE_VERSION = 1
FACTORY_PACKAGE_HEADER_SIZE = 64
FACTORY_PACKAGE_SIGNATURE_SIZE = 64
FACTORY_PACKAGE_ALIGNMENT = 256
FACTORY_PACKAGE_FLAG_FULL_ERASE_REQUIRED = 1
FACTORY_PACKAGE_HEADER_FORMAT = "<16I"

REGION_FILES: Mapping[str, str] = {
    "BOOTLOADER": "DHRT100_BOOT.bin",
    "APP_A": "DHRT100.bin",
    "RECOVERY": "DHRT100_RECOVERY.bin",
    "BOOT_CONTROL": "factory_boot_control.bin",
    "OTA_STAGE": "factory_map_manifest.bin",
}


class FactoryPackageError(ValueError):
    """Raised when a factory package violates a fail-closed invariant."""


def _crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def _align_up(value: int, alignment: int = FACTORY_PACKAGE_ALIGNMENT) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def _load_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FactoryPackageError(f"cannot read {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise FactoryPackageError(f"{label} must be a JSON object")
    return value


def _u32(value: object, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 0xFFFFFFFF:
        raise FactoryPackageError(f"{label} must be an unsigned 32-bit integer")
    return value


def _load_inputs(
    map_manifest_path: Path,
    report_path: Path,
    region_paths: Mapping[str, Path],
) -> tuple[dict[str, Any], list[dict[str, Any]], bytes]:
    manifest = _load_json(map_manifest_path, "Flash map manifest")
    report = _load_json(report_path, "factory region report")
    if manifest.get("deployment_state") != "target_not_deployed":
        raise FactoryPackageError("factory package requires target_not_deployed map")
    if report.get("deployment_state") != manifest.get("deployment_state"):
        raise FactoryPackageError("map/report deployment state mismatch")
    if report.get("full_erase_required") is not True:
        raise FactoryPackageError("factory package requires full_erase_required=true")
    map_version = _u32(manifest.get("map_version"), "map_version")
    if _u32(report.get("map_version"), "report.map_version") != map_version:
        raise FactoryPackageError("map/report version mismatch")

    partitions = manifest.get("partitions")
    if not isinstance(partitions, list):
        raise FactoryPackageError("map manifest partitions must be a list")
    partition_by_id: dict[str, dict[str, Any]] = {}
    for partition in partitions:
        if not isinstance(partition, dict) or not isinstance(partition.get("id"), str):
            raise FactoryPackageError("map manifest has an invalid partition")
        identifier = partition["id"]
        if identifier in partition_by_id:
            raise FactoryPackageError(f"duplicate partition {identifier}")
        partition_by_id[identifier] = partition

    raw_regions = report.get("regions")
    if not isinstance(raw_regions, list) or not raw_regions:
        raise FactoryPackageError("factory report has no regions")
    regions: list[dict[str, Any]] = []
    payload = bytearray()
    seen: set[str] = set()
    for raw in raw_regions:
        if not isinstance(raw, dict) or not isinstance(raw.get("partition"), str):
            raise FactoryPackageError("factory report contains an invalid region")
        partition_id = raw["partition"]
        if partition_id in seen:
            raise FactoryPackageError(f"duplicate factory region {partition_id}")
        seen.add(partition_id)
        partition = partition_by_id.get(partition_id)
        if partition is None:
            raise FactoryPackageError(f"region {partition_id} is absent from map")
        path = region_paths.get(partition_id)
        if path is None:
            raise FactoryPackageError(f"no payload supplied for region {partition_id}")
        try:
            data = path.read_bytes()
        except OSError as exc:
            raise FactoryPackageError(f"cannot read {partition_id}: {exc}") from exc
        expected_size = _u32(raw.get("size"), f"{partition_id}.size")
        expected_hash = raw.get("sha256")
        actual_hash = hashlib.sha256(data).hexdigest()
        if expected_size != len(data) or expected_hash != actual_hash:
            raise FactoryPackageError(f"factory report hash/size drifted for {partition_id}")
        partition_size = _u32(partition.get("size"), f"{partition_id}.capacity")
        partition_offset = _u32(partition.get("offset"), f"{partition_id}.offset")
        if len(data) > partition_size:
            raise FactoryPackageError(f"{partition_id} exceeds partition capacity")
        payload_offset = len(payload)
        payload.extend(data)
        regions.append(
            {
                "partition": partition_id,
                "offset": partition_offset,
                "size": len(data),
                "sha256": actual_hash,
                "payload_offset": payload_offset,
                "payload_size": len(data),
                "baseline": raw.get("baseline", ""),
            }
        )

    # The recovery image and BCB are mandatory for a controlled restore.  The
    # map/report gate already checks the exact region set; keep this explicit so
    # a future report cannot silently produce an App-only package.
    required = {"BOOTLOADER", "APP_A", "RECOVERY", "BOOT_CONTROL", "OTA_STAGE"}
    if seen != required:
        raise FactoryPackageError(
            f"factory package region set must be {sorted(required)}, got {sorted(seen)}"
        )
    return manifest, regions, bytes(payload)


def _pack_header(
    *,
    manifest_size: int,
    payload_offset: int,
    payload_size: int,
    package_size: int,
    map_version: int,
    key_id: int,
    signature_offset: int,
    package_crc32: int,
) -> bytes:
    values = (
        FACTORY_PACKAGE_MAGIC,
        FACTORY_PACKAGE_VERSION,
        FACTORY_PACKAGE_HEADER_SIZE,
        FACTORY_PACKAGE_HEADER_SIZE,
        manifest_size,
        payload_offset,
        payload_size,
        package_size,
        map_version,
        key_id,
        signature_offset,
        FACTORY_PACKAGE_SIGNATURE_SIZE,
        package_crc32,
        FACTORY_PACKAGE_FLAG_FULL_ERASE_REQUIRED,
        0,
        0,
    )
    return struct.pack(FACTORY_PACKAGE_HEADER_FORMAT, *values)


def build_factory_package(
    *,
    map_manifest_path: Path,
    report_path: Path,
    region_paths: Mapping[str, Path],
    key_id: int,
    signature: bytes | None = None,
) -> tuple[bytes, bytes]:
    """Return ``(package, signing_transcript)`` for supplied factory regions."""
    if not 0 < key_id <= 0xFFFFFFFF:
        raise FactoryPackageError("key_id must be in range 1..0xFFFFFFFF")
    manifest, regions, payload = _load_inputs(
        map_manifest_path, report_path, region_paths
    )
    report = _load_json(report_path, "factory region report")
    product = str(report.get("product", "DHRT100"))
    if not product:
        raise FactoryPackageError("factory report product is empty")
    descriptor = {
        "schema_version": 1,
        "product": product,
        "map_version": _u32(manifest.get("map_version"), "map_version"),
        "xip_base": _u32(
            (manifest.get("geometry") or {}).get("xip_base"), "geometry.xip_base"
        ),
        "deployment_state": manifest["deployment_state"],
        "full_erase_required": True,
        "erased_store_partitions": report.get("erased_store_partitions", []),
        "regions": regions,
    }
    manifest_bytes = _canonical_json(descriptor)
    payload_offset = _align_up(FACTORY_PACKAGE_HEADER_SIZE + len(manifest_bytes))
    padding = bytes(payload_offset - FACTORY_PACKAGE_HEADER_SIZE - len(manifest_bytes))
    signature_offset = payload_offset + len(payload)
    package_size = signature_offset + FACTORY_PACKAGE_SIGNATURE_SIZE
    body = manifest_bytes + padding + payload
    header = _pack_header(
        manifest_size=len(manifest_bytes),
        payload_offset=payload_offset,
        payload_size=len(payload),
        package_size=package_size,
        map_version=descriptor["map_version"],
        key_id=key_id,
        signature_offset=signature_offset,
        package_crc32=_crc32(body),
    )
    transcript = header + body
    if signature is None:
        return transcript, transcript
    if len(signature) != FACTORY_PACKAGE_SIGNATURE_SIZE:
        raise FactoryPackageError("factory signature must be exactly 64 bytes")
    return transcript + signature, transcript


def _parse_package(package: bytes) -> tuple[tuple[int, ...], dict[str, Any], bytes, bytes, bytes]:
    if len(package) < FACTORY_PACKAGE_HEADER_SIZE + FACTORY_PACKAGE_SIGNATURE_SIZE:
        raise FactoryPackageError("factory package is truncated")
    header = struct.unpack_from(FACTORY_PACKAGE_HEADER_FORMAT, package, 0)
    (
        magic,
        version,
        header_size,
        manifest_offset,
        manifest_size,
        payload_offset,
        payload_size,
        package_size,
        map_version,
        key_id,
        signature_offset,
        signature_size,
        package_crc32,
        flags,
        _,
        _,
    ) = header
    if magic != FACTORY_PACKAGE_MAGIC or version != FACTORY_PACKAGE_VERSION:
        raise FactoryPackageError("factory package magic/version is invalid")
    if header_size != FACTORY_PACKAGE_HEADER_SIZE or manifest_offset != header_size:
        raise FactoryPackageError("factory package header geometry is invalid")
    if package_size != len(package) or signature_size != FACTORY_PACKAGE_SIGNATURE_SIZE:
        raise FactoryPackageError("factory package size/signature geometry is invalid")
    if not flags & FACTORY_PACKAGE_FLAG_FULL_ERASE_REQUIRED:
        raise FactoryPackageError("factory package is missing full-erase policy")
    if manifest_offset + manifest_size > payload_offset:
        raise FactoryPackageError("factory manifest overlaps payload")
    if payload_offset + payload_size != signature_offset or signature_offset + signature_size != package_size:
        raise FactoryPackageError("factory package region offsets are invalid")
    body = package[manifest_offset:signature_offset]
    if _crc32(body) != package_crc32:
        raise FactoryPackageError("factory package CRC mismatch")
    try:
        descriptor = json.loads(package[manifest_offset : manifest_offset + manifest_size].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise FactoryPackageError(f"factory descriptor is invalid: {exc}") from exc
    if not isinstance(descriptor, dict):
        raise FactoryPackageError("factory descriptor must be an object")
    if descriptor.get("map_version") != map_version or descriptor.get("full_erase_required") is not True:
        raise FactoryPackageError("factory descriptor/header identity mismatch")
    return header, descriptor, package[payload_offset:signature_offset], package[signature_offset:], package[:signature_offset]


def verify_factory_package(
    package: bytes,
    *,
    key_config: Path,
    profile: str,
) -> dict[str, Any]:
    header, descriptor, payload, signature, transcript = _parse_package(package)
    key_id = header[9]
    try:
        keys, allowed_mask, _ = gen_ota_key_registry.load_registry(key_config, profile)
        gen_ota_key_registry.require_signing_key(keys, allowed_mask, key_id)
        key = next(entry for entry in keys if entry["key_id"] == key_id)
        public_key = key["public_key"]
        if not isinstance(public_key, bytes):
            raise FactoryPackageError("factory key registry entry is invalid")
        verify_signature(public_key, transcript, signature)
    except (OSError, ValueError, StopIteration) as exc:
        raise FactoryPackageError(f"factory signature verification failed: {exc}") from exc

    regions = descriptor.get("regions")
    if not isinstance(regions, list) or not regions:
        raise FactoryPackageError("factory descriptor has no regions")
    for region in regions:
        if not isinstance(region, dict):
            raise FactoryPackageError("factory descriptor contains invalid region")
        offset = _u32(region.get("payload_offset"), "region.payload_offset")
        size = _u32(region.get("payload_size"), "region.payload_size")
        if offset + size > len(payload):
            raise FactoryPackageError("factory region exceeds payload")
        if hashlib.sha256(payload[offset : offset + size]).hexdigest() != region.get("sha256"):
            raise FactoryPackageError(f"factory region hash mismatch: {region.get('partition')}")
        if region.get("size") != size:
            raise FactoryPackageError(f"factory region size mismatch: {region.get('partition')}")
    return {
        "product": descriptor.get("product"),
        "map_version": header[8],
        "xip_base": descriptor.get("xip_base"),
        "key_id": key_id,
        "region_count": len(regions),
        "package_size": len(package),
        "package_sha256": hashlib.sha256(package).hexdigest(),
        "regions": regions,
    }


def _region_paths(build_dir: Path) -> dict[str, Path]:
    return {partition: build_dir / filename for partition, filename in REGION_FILES.items()}


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    build = sub.add_parser("build", help="build a signed factory package")
    build.add_argument("--map-manifest", required=True, type=Path)
    build.add_argument("--report", required=True, type=Path)
    build.add_argument("--build-dir", required=True, type=Path)
    build.add_argument("--key-id", required=True, type=int)
    build.add_argument("--signature-file", type=Path)
    build.add_argument("--output", required=True, type=Path)
    build.add_argument("--signing-transcript-output", type=Path)
    build.add_argument("--signing-request-output", type=Path)
    verify = sub.add_parser("verify", help="verify a signed factory package")
    verify.add_argument("--package", required=True, type=Path)
    verify.add_argument("--config", required=True, type=Path)
    verify.add_argument("--profile", default="v2_candidate")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    try:
        if args.command == "build":
            signature = args.signature_file.read_bytes() if args.signature_file else None
            package_or_transcript, transcript = build_factory_package(
                map_manifest_path=args.map_manifest,
                report_path=args.report,
                region_paths=_region_paths(args.build_dir),
                key_id=args.key_id,
                signature=signature,
            )
            if args.signing_transcript_output:
                args.signing_transcript_output.write_bytes(transcript)
            if args.signing_request_output:
                args.signing_request_output.write_text(
                    json.dumps(
                        {
                            "schema_version": 1,
                            "algorithm": "ecdsa-p256-sha256",
                            "signature_format": "raw-r-s-64",
                            "key_id": args.key_id,
                            "transcript_sha256": hashlib.sha256(transcript).hexdigest(),
                            "transcript_size": len(transcript),
                        },
                        indent=2,
                        sort_keys=True,
                    )
                    + "\n",
                    encoding="utf-8",
                    newline="\n",
                )
            if signature is None:
                raise FactoryPackageError(
                    "signature is required; transcript/request were emitted for external signing"
                )
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(package_or_transcript)
            print(
                f"factory_package=OK package={args.output} size={len(package_or_transcript)} "
                f"transcript_sha256={hashlib.sha256(transcript).hexdigest()}"
            )
            return 0
        summary = verify_factory_package(
            args.package.read_bytes(), key_config=args.config, profile=args.profile
        )
        print(
            f"factory_package_verify=OK product={summary['product']} "
            f"map_version={summary['map_version']} key_id={summary['key_id']} "
            f"regions={summary['region_count']} package_sha256={summary['package_sha256']}"
        )
        return 0
    except (FactoryPackageError, OSError) as exc:
        print(f"factory_package=FAILED detail={exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
