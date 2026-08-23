from __future__ import annotations

import json
from pathlib import Path

import pytest
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from tools.factory_package.factory_package import build_factory_package
from tools.factory_restore.factory_restore import (
    FactoryRestoreError,
    validate_factory_artifacts,
)
from tools.uf2_join.uf2_join import make_block


def _fixture(tmp_path: Path) -> tuple[Path, Path, Path]:
    map_path = tmp_path / "map.json"
    map_path.write_text(
        json.dumps(
            {
                "deployment_state": "target_not_deployed",
                "map_version": 2,
                "geometry": {"xip_base": 0x10000000, "total_size": 0x01000000},
                "partitions": [
                    {"id": item, "offset": index * 0x1000, "size": 0x1000}
                    for index, item in enumerate(
                        ["BOOTLOADER", "APP_A", "RECOVERY", "BOOT_CONTROL", "OTA_STAGE"]
                    )
                ],
            }
        ),
        encoding="utf-8",
    )
    names = ["BOOTLOADER", "APP_A", "RECOVERY", "BOOT_CONTROL", "OTA_STAGE"]
    payloads: dict[str, Path] = {}
    region_records: list[dict[str, object]] = []
    for index, name in enumerate(names):
        path = tmp_path / f"{name}.bin"
        path.write_bytes(bytes([index + 1]) * (256 + index))
        payloads[name] = path
        import hashlib

        region_records.append(
            {"partition": name, "size": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        )
    report_path = tmp_path / "report.json"
    report_path.write_text(
        json.dumps(
            {
                "product": "DHRT100",
                "deployment_state": "target_not_deployed",
                "map_version": 2,
                "full_erase_required": True,
                "regions": region_records,
            }
        ),
        encoding="utf-8",
    )
    private = ec.generate_private_key(ec.SECP256R1())
    public = private.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    config_path = tmp_path / "keys.json"
    config_path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "algorithm": "ecdsa-p256-sha256",
                "keys": [{"key_id": 7, "role": "factory", "revoked": False, "public_key_hex": public.hex()}],
                "profiles": {"factory_test": {"require_signature": True, "allowed_roles": ["factory"]}},
            }
        ),
        encoding="utf-8",
    )
    _, transcript = build_factory_package(
        map_manifest_path=map_path,
        report_path=report_path,
        region_paths=payloads,
        key_id=7,
    )
    r, s = utils.decode_dss_signature(private.sign(transcript, ec.ECDSA(hashes.SHA256())))
    order = int("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)
    signature = r.to_bytes(32, "big") + min(s, order - s).to_bytes(32, "big")
    package, _ = build_factory_package(
        map_manifest_path=map_path,
        report_path=report_path,
        region_paths=payloads,
        key_id=7,
        signature=signature,
    )
    package_path = tmp_path / "factory.fpk"
    package_path.write_bytes(package)

    chunks: list[bytes] = []
    block_no = 0
    for index, name in enumerate(names):
        data = payloads[name].read_bytes()
        for offset in range(0, len(data), 256):
            chunks.append((0x10000000 + index * 0x1000 + offset, data[offset : offset + 256]))
    for address, data in chunks:
        block_no += 1
        # block count is corrected below after collecting all chunks.
    blocks = [
        make_block(address, data, index, len(chunks), 0xE48BFF59)
        for index, (address, data) in enumerate(chunks)
    ]
    uf2_path = tmp_path / "factory.uf2"
    uf2_path.write_bytes(b"".join(blocks))
    return package_path, uf2_path, config_path


def test_factory_restore_validates_package_and_uf2(tmp_path: Path) -> None:
    package, uf2, config = _fixture(tmp_path)
    plan = validate_factory_artifacts(package, uf2, key_config=config, profile="factory_test")
    assert plan["full_erase_required"] is True
    assert plan["region_count"] == 5
    assert plan["uf2_block_count"] == 9
    assert plan["flash_size"] == 0x01000000


def test_factory_restore_rejects_uf2_address_drift(tmp_path: Path) -> None:
    package, uf2, config = _fixture(tmp_path)
    data = bytearray(uf2.read_bytes())
    data[12:16] = (0x1000).to_bytes(4, "little")
    uf2.write_bytes(data)
    with pytest.raises(FactoryRestoreError, match="does not match"):
        validate_factory_artifacts(package, uf2, key_config=config, profile="factory_test")
