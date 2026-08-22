from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

import pytest

from tools.ota_packager import ota_packager


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "config" / "flash_map_gen" / "flash_map_v1_compat_manifest.json"


def _layout() -> ota_packager.DeploymentLayout:
    return ota_packager.load_deployment_layout(MANIFEST)


def _read_c_string(data: bytes) -> str:
    return data.split(b"\0", 1)[0].decode("ascii")


def test_ota_package_header_matches_payload_layout() -> None:
    image_a = b"A" * 17
    image_b = b"B" * 19

    package = ota_packager.build_package(
        image_a,
        image_b,
        product_id="DHRT100",
        hardware_id="dhrt100",
        app_version=(1, 2, 3),
        build_id="202608140001",
        min_bootloader_version=(0, 1, 0),
        layout=_layout(),
    )

    magic, version, header_size, package_size, _, image_count = struct.unpack_from("<IIIIII", package, 0)
    assert magic == ota_packager.PACKAGE_MAGIC
    assert version == ota_packager.PACKAGE_VERSION
    assert header_size == ota_packager.PACKAGE_HEADER_SIZE
    assert package_size == len(package)
    assert image_count == 2
    assert _read_c_string(package[32:64]) == "DHRT100"
    assert _read_c_string(package[64:96]) == "dhrt100"

    slot_a, offset_a, size_a, crc_a, run_a, _ = struct.unpack_from("<IIIIII", package, 192)
    slot_b, offset_b, size_b, crc_b, run_b, _ = struct.unpack_from("<IIIIII", package, 224)
    assert (slot_a, offset_a, size_a, crc_a, run_a) == (
        ota_packager.SLOT_A,
        ota_packager.PACKAGE_HEADER_SIZE,
        len(image_a),
        ota_packager.crc32(image_a),
        _layout().app_a.offset,
    )
    assert (slot_b, size_b, crc_b, run_b) == (
        ota_packager.SLOT_B,
        len(image_b),
        ota_packager.crc32(image_b),
        _layout().app_b.offset,
    )
    assert offset_b % ota_packager.PACKAGE_PAYLOAD_ALIGNMENT == 0
    assert package[offset_a : offset_a + size_a] == image_a
    assert package[offset_b : offset_b + size_b] == image_b
    assert package[144:176] == hashlib.sha256(package[ota_packager.PACKAGE_HEADER_SIZE:]).digest()


def test_ota_package_can_carry_external_manifest_security_metadata() -> None:
    signature = bytes(range(64))
    package = ota_packager.build_package(
        b"A" * 32,
        b"B" * 32,
        product_id="DHRT100",
        hardware_id="dhrt100",
        app_version=(1, 2, 3),
        build_id="signed",
        min_bootloader_version=(0, 1, 0),
        layout=_layout(),
        security_counter=9,
        key_id=7,
        signature=signature,
    )
    assert struct.unpack_from("<I", package, 256)[0] == ota_packager.MANIFEST_EXTENSION_MAGIC
    assert struct.unpack_from("<I", package, 268)[0] == 9
    assert struct.unpack_from("<I", package, 272)[0] == 7
    assert struct.unpack_from("<I", package, 276)[0] == len(signature)
    assert package[280:344] == signature


@pytest.mark.parametrize("value", ["1.2", "1.2.3.4", "1.2.256", "1.-1.0"])
def test_parse_semver_rejects_invalid_versions(value: str) -> None:
    with pytest.raises(ValueError):
        ota_packager.parse_semver(value)


def test_parse_semver_packs_three_byte_version() -> None:
    assert ota_packager.parse_semver("1.2.3") == (1, 2, 3)
    assert ota_packager.pack_version((1, 2, 3)) == 0x010203


def test_ota_package_rejects_image_larger_than_manifest_partition() -> None:
    layout = ota_packager.DeploymentLayout(
        app_a=ota_packager.AppPartition(offset=1, size=3),
        app_b=ota_packager.AppPartition(offset=4, size=3),
    )
    with pytest.raises(ValueError, match="Slot A image size"):
        ota_packager.build_package(
            b"AAAA", b"B", product_id="p", hardware_id="h",
            app_version=(1, 0, 0), build_id="b",
            min_bootloader_version=(1, 0, 0), layout=layout,
        )


@pytest.mark.parametrize(
    ("mutation", "message"),
    [
        (lambda data: data.update(deployment_state="target_not_deployed"), "deployed_compatibility"),
        (lambda data: data.__setitem__("partitions", []), "missing APP_A"),
    ],
)
def test_manifest_rejects_non_live_or_missing_app_partitions(
    tmp_path: Path, mutation, message: str
) -> None:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    mutation(data)
    path = tmp_path / "map.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(ValueError, match=message):
        ota_packager.load_deployment_layout(path)
