from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest

from tools.flash_map.flash_consumer_check import (
    FlashConsumerError,
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
        require_tokens(linker, ("INCLUDE flash_map_v1_compat.ldinc",))


def test_ota_descriptor_rejects_run_offset_drift(tmp_path: Path) -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    (tmp_path / "RP2350_TRIG.bin").write_bytes(b"A")
    (tmp_path / "RP2350_TRIG_B.bin").write_bytes(b"B")
    package = bytearray(512 + 2)
    struct.pack_into("<IIIIII", package, 0, 0x474B5054, 2, 512, len(package), 0, 2)
    struct.pack_into("<IIIIII", package, 192, 1, 512, 1, 0, 0xDEADBEEF, 0)
    struct.pack_into("<IIIIII", package, 224, 2, 513, 1, 0, 0x001C0000, 0)
    (tmp_path / "RP2350_TRIG_UPDATE.pkg").write_bytes(package)
    with pytest.raises(FlashConsumerError, match="APP_A descriptor"):
        check_ota_package(tmp_path, manifest)
