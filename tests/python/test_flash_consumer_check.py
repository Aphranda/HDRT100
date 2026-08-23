from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest

from tools.ota_packager import ota_packager
from tools.flash_map.flash_consumer_check import (
    FlashConsumerError,
    check_factory_report,
    check_ota_package,
    check_source_consumers,
    load_manifest,
    parse_flash_memory,
    require_tokens,
)


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "config" / "flash_map_gen" / "flash_map_v1_compat_manifest.json"


def test_repository_live_consumers_reference_generated_compatibility_map() -> None:
    assert load_manifest(MANIFEST)["deployment_state"] == "deployed_compatibility"
    check_source_consumers(ROOT)


def test_link_map_parser_reads_flash_origin_and_length(tmp_path: Path) -> None:
    path = tmp_path / "app.map"
    path.write_text("Name Origin Length Attributes\nFLASH 0x10040000 0x00180000 xr\n", encoding="utf-8")
    assert parse_flash_memory(path) == (0x10040000, 0x00180000)


def test_source_consumer_check_rejects_literal_only_linker(tmp_path: Path) -> None:
    linker = tmp_path / "linker" / "rp2350_bootloader.ld"
    linker.parent.mkdir(parents=True)
    linker.write_text("FLASH ORIGIN = 0x10000000", encoding="utf-8")
    with pytest.raises(FlashConsumerError, match="generated-map tokens"):
        require_tokens(linker, ("INCLUDE flash_map_active.ldinc",))


def test_ota_descriptor_rejects_run_offset_drift(tmp_path: Path) -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    (tmp_path / "DHRT100.bin").write_bytes(b"A")
    (tmp_path / "DHRT100_B.bin").write_bytes(b"B")
    package = bytearray(512 + 2)
    struct.pack_into("<IIIIII", package, 0, 0x474B5054, 2, 512, len(package), 0, 2)
    struct.pack_into("<IIIIII", package, 192, 1, 512, 1, 0, 0xDEADBEEF, 0)
    struct.pack_into("<IIIIII", package, 224, 2, 513, 1, 0, 0x001C0000, 0)
    (tmp_path / "DHRT100_UPDATE.pkg").write_bytes(package)
    with pytest.raises(FlashConsumerError, match="APP_A descriptor"):
        check_ota_package(tmp_path, manifest)


def test_v2_candidate_rejects_unsigned_update_package(tmp_path: Path) -> None:
    manifest = json.loads(
        (ROOT / "config" / "flash_map_gen" / "flash_map_v2_manifest.json").read_text(
            encoding="utf-8"))
    (tmp_path / "DHRT100.bin").write_bytes(b"A")
    (tmp_path / "DHRT100_B.bin").write_bytes(b"B")
    package = bytearray(514)
    struct.pack_into("<IIIIII", package, 0, 0x474B5054, 2, 512, len(package), 0, 2)
    (tmp_path / "candidate.pkg").write_bytes(package)

    with pytest.raises(FlashConsumerError, match="not fully signed"):
        check_ota_package(
            tmp_path, manifest, "candidate.pkg", require_signature=True)


def test_v2_candidate_accepts_structurally_signed_update_package(tmp_path: Path) -> None:
    manifest_path = ROOT / "config" / "flash_map_gen" / "flash_map_v2_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    image_a = b"A"
    image_b = b"B"
    (tmp_path / "DHRT100.bin").write_bytes(image_a)
    (tmp_path / "DHRT100_B.bin").write_bytes(image_b)
    package = ota_packager.build_package(
        image_a,
        image_b,
        product_id="DHRT100",
        hardware_id="dhrt100",
        app_version=(1, 0, 0),
        build_id="candidate",
        min_bootloader_version=(0, 1, 0),
        layout=ota_packager.load_deployment_layout(
            manifest_path, allow_target_not_deployed=True),
        security_counter=9,
        key_id=7,
        signature=bytes(range(64)),
    )
    (tmp_path / "candidate.pkg").write_bytes(package)

    check_ota_package(
        tmp_path, manifest, "candidate.pkg", require_signature=True)


def test_factory_report_rejects_incomplete_region_coverage(tmp_path: Path) -> None:
    manifest = json.loads(
        (ROOT / "config/flash_map_gen/flash_map_v2_manifest.json").read_text(
            encoding="utf-8"))
    (tmp_path / "factory_region_report.json").write_text(
        json.dumps({
            "deployment_state": "target_not_deployed",
            "map_version": 2,
            "full_erase_required": True,
            "regions": [],
        }),
        encoding="utf-8",
    )
    with pytest.raises(FlashConsumerError, match="coverage is incomplete"):
        check_factory_report(tmp_path, manifest)
