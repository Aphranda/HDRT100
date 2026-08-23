from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import utils

from tools.flash_map.make_factory_baseline import build_signed_slot_manifest
from tools.ota_packager import ota_packager


def test_factory_slot_manifest_contains_signed_header(tmp_path: Path) -> None:
    image_a = b"A" * 128
    image_b = b"B" * 160
    map_manifest = tmp_path / "map.json"
    map_manifest.write_text(json.dumps({
        "schema_version": 1,
        "map_version": 2,
        "deployment_state": "target_not_deployed",
        "geometry": {"xip_base": 0x10000000},
        "partitions": [
            {"id": "APP_A", "offset": 0x80000, "size": 0x200000,
             "store_type": "image", "executable": True},
            {"id": "APP_B", "offset": 0x280000, "size": 0x200000,
             "store_type": "image", "executable": True},
        ],
    }), encoding="utf-8")
    build_info = tmp_path / "build_info.c"
    build_info.write_text('const char g_project_build_id[] = "debug";\n',
                          encoding="utf-8")
    private = ec.generate_private_key(ec.SECP256R1())
    private_path = tmp_path / "debug.pem"
    private_path.write_bytes(private.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))
    lane = build_signed_slot_manifest(
        map_manifest, image_a, image_b, build_info, private_path, 7, 1,
        {"FLASH_GEOMETRY_PROGRAM_SIZE_BYTES": 256,
         "FLASH_GEOMETRY_ERASE_SIZE_BYTES": 4096,
         "FLASH_MAP_VERSION": 2})
    assert len(lane) == 4096
    body = lane[:768]
    commit = lane[768:1024]
    assert struct.unpack_from("<6I", body, 0) == (0x534D4244, 1, 2, 1, 1, 512)
    header = body[32:544]
    assert struct.unpack_from("<I", body, 24)[0] == ota_packager.crc32(header)
    body_zero_crc = body[:28] + bytes(4) + body[32:]
    assert struct.unpack_from("<I", body, 28)[0] == ota_packager.crc32(body_zero_crc)
    assert struct.unpack_from("<7I", commit, 0)[0:6] == (
        0x534D434D, 1, 2, 1, 1, struct.unpack_from("<I", body, 28)[0])
    signature = header[280:344]
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    transcript = ota_packager.build_signing_transcript(header)
    public = private.public_key()
    public.verify(utils.encode_dss_signature(r, s), transcript, ec.ECDSA(hashes.SHA256()))
    assert hashlib.sha256(image_a).digest() == header[344:376]
