from __future__ import annotations

import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_factory_baseline_contains_valid_bcb_and_map_manifest(tmp_path: Path) -> None:
    app = tmp_path / "app.bin"
    app.write_bytes(b"factory-app-image")
    boot_control = tmp_path / "boot_control.bin"
    manifest_blob = tmp_path / "manifest.bin"
    report = tmp_path / "report.json"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/flash_map/make_factory_baseline.py"),
            "--map-header", str(ROOT / "config/flash_map_gen/flash_map_v2.h"),
            "--map-manifest", str(ROOT / "config/flash_map_gen/flash_map_v2_manifest.json"),
            "--bcb-header", str(ROOT / "third_party/portable_ota/include/pota_boot_control_store.h"),
            "--metadata-header", str(ROOT / "components/ota_manager/inc/ota_metadata.h"),
            "--app-a", str(app),
            "--boot-control", str(boot_control),
            "--manifest-blob", str(manifest_blob),
            "--report", str(report),
        ],
        check=True,
        capture_output=True,
        text=True,
    )

    data = boot_control.read_bytes()
    assert len(data) == 0x00040000
    magic, schema, map_version, sequence = struct.unpack_from("<4I", data, 0)
    assert (magic, schema, map_version, sequence) == (0x42434242, 1, 2, 1)
    body_crc = struct.unpack_from("<I", data, 32)[0]
    body_copy = bytearray(data[:256])
    struct.pack_into("<I", body_copy, 32, 0)
    assert body_crc == zlib.crc32(body_copy) & 0xFFFFFFFF
    payload_length = struct.unpack_from("<I", data, 24)[0]
    payload_crc = struct.unpack_from("<I", data, 28)[0]
    metadata = bytearray(data[36:36 + payload_length])
    assert payload_length == 192
    assert payload_crc == zlib.crc32(metadata) & 0xFFFFFFFF
    assert struct.unpack_from("<10I", metadata, 0) == (
        0x4F544D44, 3, 1, 1, 0, 1, 0, 0,
        len(app.read_bytes()), zlib.crc32(app.read_bytes()) & 0xFFFFFFFF,
    )
    metadata_crc = struct.unpack_from("<I", metadata, 128)[0]
    metadata_ext_crc = struct.unpack_from("<I", metadata, 168)[0]
    boot_mode, previous_slot, boot_generation, boot_capabilities = (
        struct.unpack_from("<4I", metadata, 172)
    )
    metadata_ab_crc = struct.unpack_from("<I", metadata, 188)[0]
    metadata_base = bytearray(metadata[:132])
    struct.pack_into("<I", metadata_base, 128, 0)
    metadata_ext = bytearray(metadata[132:172])
    struct.pack_into("<I", metadata_ext, 36, 0)
    metadata_ab = bytearray(metadata[172:192])
    struct.pack_into("<I", metadata_ab, 16, 0)
    assert metadata_crc == zlib.crc32(metadata_base) & 0xFFFFFFFF
    assert metadata_ext_crc == zlib.crc32(metadata_ext) & 0xFFFFFFFF
    assert metadata_ab_crc == zlib.crc32(metadata_ab) & 0xFFFFFFFF
    assert (boot_mode, previous_slot, boot_generation, boot_capabilities) == (
        1, 0, 0, 3,
    )
    commit = struct.unpack_from("<7I", data, 256)
    assert commit == (0x42434243, 1, 2, 1, 1, body_crc, 0xC04D4D49 ^ 1)

    lane_pages = (len(data) // 2) // 256
    seal_offset = (lane_pages - 1) * 256
    assert struct.unpack_from("<I", data, seal_offset)[0] == 0x42434253
    assert json.loads(manifest_blob.read_text(encoding="utf-8"))["map_version"] == 2
    report_data = json.loads(report.read_text(encoding="utf-8"))
    assert report_data["full_erase_required"] is True
    assert report_data["deployment_state"] == "target_not_deployed"
