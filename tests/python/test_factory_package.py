from __future__ import annotations

import json
from pathlib import Path

import pytest
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from tools.factory_package.factory_package import (
    FactoryPackageError,
    build_factory_package,
    verify_factory_package,
)


def _fixture(tmp_path: Path) -> tuple[Path, Path, dict[str, Path]]:
    map_path = tmp_path / "map.json"
    map_path.write_text(
        json.dumps(
            {
                "deployment_state": "target_not_deployed",
                "map_version": 2,
                "geometry": {"xip_base": 0x10000000},
                "partitions": [
                    {"id": item, "offset": index * 0x1000, "size": 0x2000}
                    for index, item in enumerate(
                        ["BOOTLOADER", "APP_A", "RECOVERY", "BOOT_CONTROL", "OTA_STAGE"]
                    )
                ],
            }
        ),
        encoding="utf-8",
    )
    payloads: dict[str, Path] = {}
    regions: list[dict[str, object]] = []
    for index, partition in enumerate(
        ["BOOTLOADER", "APP_A", "RECOVERY", "BOOT_CONTROL", "OTA_STAGE"]
    ):
        path = tmp_path / f"{partition}.bin"
        data = bytes([index + 1]) * (8 + index)
        path.write_bytes(data)
        payloads[partition] = path
        import hashlib

        regions.append(
            {
                "partition": partition,
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    report_path = tmp_path / "report.json"
    report_path.write_text(
        json.dumps(
            {
                "product": "DHRT100",
                "deployment_state": "target_not_deployed",
                "map_version": 2,
                "full_erase_required": True,
                "regions": regions,
                "erased_store_partitions": ["SCRATCH"],
            }
        ),
        encoding="utf-8",
    )
    return map_path, report_path, payloads


def _key_config(tmp_path: Path, private_key: ec.EllipticCurvePrivateKey) -> tuple[Path, bytes]:
    public = private_key.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    config = tmp_path / "keys.json"
    config.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "algorithm": "ecdsa-p256-sha256",
                "keys": [
                    {
                        "key_id": 7,
                        "role": "factory",
                        "revoked": False,
                        "public_key_hex": public.hex(),
                    }
                ],
                "profiles": {
                    "factory_test": {
                        "require_signature": True,
                        "allowed_roles": ["factory"],
                    }
                },
            }
        ),
        encoding="utf-8",
    )
    return config, public


def test_signed_factory_package_round_trip(tmp_path: Path) -> None:
    map_path, report_path, payloads = _fixture(tmp_path)
    private = ec.generate_private_key(ec.SECP256R1())
    config, _ = _key_config(tmp_path, private)
    unsigned, transcript = build_factory_package(
        map_manifest_path=map_path,
        report_path=report_path,
        region_paths=payloads,
        key_id=7,
    )
    assert unsigned == transcript
    der = private.sign(transcript, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    order = int("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)
    signature = r.to_bytes(32, "big") + min(s, order - s).to_bytes(32, "big")
    package, _ = build_factory_package(
        map_manifest_path=map_path,
        report_path=report_path,
        region_paths=payloads,
        key_id=7,
        signature=signature,
    )
    summary = verify_factory_package(package, key_config=config, profile="factory_test")
    assert summary["product"] == "DHRT100"
    assert summary["map_version"] == 2
    assert summary["key_id"] == 7
    assert summary["region_count"] == 5


def test_factory_package_rejects_payload_tamper(tmp_path: Path) -> None:
    map_path, report_path, payloads = _fixture(tmp_path)
    private = ec.generate_private_key(ec.SECP256R1())
    config, _ = _key_config(tmp_path, private)
    _, transcript = build_factory_package(
        map_manifest_path=map_path,
        report_path=report_path,
        region_paths=payloads,
        key_id=7,
    )
    der = private.sign(transcript, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    order = int("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)
    signature = r.to_bytes(32, "big") + min(s, order - s).to_bytes(32, "big")
    package, _ = build_factory_package(
        map_manifest_path=map_path,
        report_path=report_path,
        region_paths=payloads,
        key_id=7,
        signature=signature,
    )
    tampered = bytearray(package)
    tampered[-65] ^= 0x01
    with pytest.raises(FactoryPackageError, match="CRC mismatch|signature verification"):
        verify_factory_package(bytes(tampered), key_config=config, profile="factory_test")


def test_factory_package_requires_full_erase_and_all_regions(tmp_path: Path) -> None:
    map_path, report_path, payloads = _fixture(tmp_path)
    report = json.loads(report_path.read_text(encoding="utf-8"))
    report["full_erase_required"] = False
    report_path.write_text(json.dumps(report), encoding="utf-8")
    with pytest.raises(FactoryPackageError, match="full_erase_required"):
        build_factory_package(
            map_manifest_path=map_path,
            report_path=report_path,
            region_paths=payloads,
            key_id=7,
        )
