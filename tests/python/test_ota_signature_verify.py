from __future__ import annotations

from pathlib import Path

import pytest

from tools.ota_keys import gen_ota_key_registry, verify_ota_signature
from tools.ota_packager import ota_packager


ROOT = Path(__file__).resolve().parents[2]
KEY_CONFIG = ROOT / "tests" / "fixtures" / "ota_public_keys_golden.json"
GOLDEN_SIGNATURE = bytes.fromhex(
    "fcc3492e3e42d20e8eaeec2777f6c70fc02ad8d9ca9bdcfb25e1391d4023308d"
    "789ea6b962102aa92ae7b1803540a7b79e6901cef80d12fac0fefc768e3dcff7")


def golden_transcript() -> bytes:
    request = ota_packager.build_package(
        b"slot-a",
        b"slot-b",
        product_id="DHRT100",
        hardware_id="dhrt100",
        app_version=(1, 2, 3),
        build_id="golden-vector",
        min_bootloader_version=(0, 1, 0),
        layout=ota_packager.DeploymentLayout(
            app_a=ota_packager.AppPartition(offset=0x40000, size=0x180000),
            app_b=ota_packager.AppPartition(offset=0x1C0000, size=0x180000)),
        security_counter=9,
        key_id=7,
        prepare_signing=True,
    )
    return ota_packager.build_signing_transcript(request)


def golden_public_key() -> bytes:
    keys, allowed_mask, _ = gen_ota_key_registry.load_registry(
        KEY_CONFIG, "v2_candidate")
    gen_ota_key_registry.require_signing_key(keys, allowed_mask, 7)
    key = keys[0]["public_key"]
    assert isinstance(key, bytes)
    return key


def test_external_ota_signature_golden_vector_verifies() -> None:
    verify_ota_signature.verify_signature(
        golden_public_key(), golden_transcript(), GOLDEN_SIGNATURE)


def test_external_ota_signature_rejects_mutation_and_high_s() -> None:
    transcript = bytearray(golden_transcript())
    transcript[0] ^= 1
    with pytest.raises(ValueError, match="verification failed"):
        verify_ota_signature.verify_signature(
            golden_public_key(), bytes(transcript), GOLDEN_SIGNATURE)

    r = GOLDEN_SIGNATURE[:32]
    s = int.from_bytes(GOLDEN_SIGNATURE[32:], "big")
    high_s_signature = r + (verify_ota_signature.P256_ORDER - s).to_bytes(32, "big")
    with pytest.raises(ValueError, match="low-S"):
        verify_ota_signature.verify_signature(
            golden_public_key(), golden_transcript(), high_s_signature)
