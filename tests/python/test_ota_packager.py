from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

import pytest
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

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


def test_signing_transcript_is_canonical_and_signature_independent() -> None:
    layout = ota_packager.DeploymentLayout(
        app_a=ota_packager.AppPartition(offset=0x40000, size=0x180000),
        app_b=ota_packager.AppPartition(offset=0x1C0000, size=0x180000),
    )
    kwargs = dict(
        product_id="DHRT100",
        hardware_id="dhrt100",
        app_version=(1, 2, 3),
        build_id="golden-vector",
        min_bootloader_version=(0, 1, 0),
        layout=layout,
        security_counter=9,
        key_id=7,
    )
    request = ota_packager.build_package(
        b"slot-a", b"slot-b", prepare_signing=True, **kwargs)
    signed = ota_packager.build_package(
        b"slot-a", b"slot-b", signature=bytes(range(64)), **kwargs)
    request_transcript = ota_packager.build_signing_transcript(request)
    signed_transcript = ota_packager.build_signing_transcript(signed)

    assert request_transcript == signed_transcript
    assert request_transcript[16:20] == bytes(4)
    assert request_transcript[280:344] == bytes(64)
    assert hashlib.sha256(request_transcript).hexdigest() == (
        "bbf4a80004fbd2bf2d2149c7813ca583bbc17189d4bc49d67c597e8c494a865b"
    )


def test_manifest_p256_low_s_golden_vector() -> None:
    public_key = bytes.fromhex(
        "04d54c4dfde2e3b7c6dbfb90bd3451f99deca87d5a5f45137f5ba53613c0beecec"
        "9dd13a9a3c3ecf241482d809eb87be02941219cb6dcdad5b36f1ead5946fa67f")
    signature = bytes.fromhex(
        "fcc3492e3e42d20e8eaeec2777f6c70fc02ad8d9ca9bdcfb25e1391d4023308d"
        "789ea6b962102aa92ae7b1803540a7b79e6901cef80d12fac0fefc768e3dcff7")
    request = ota_packager.build_package(
        b"slot-a", b"slot-b",
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
    transcript = ota_packager.build_signing_transcript(request)
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    p256_order = int(
        "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)
    assert 0 < r < p256_order
    assert 0 < s <= p256_order // 2

    verifier = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), public_key)
    verifier.verify(
        utils.encode_dss_signature(r, s), transcript, ec.ECDSA(hashes.SHA256()))

    bad_signature = utils.encode_dss_signature(r ^ 1, s)
    with pytest.raises(InvalidSignature):
        verifier.verify(bad_signature, transcript, ec.ECDSA(hashes.SHA256()))


def test_signing_request_contains_only_public_release_metadata() -> None:
    transcript = bytes(range(256)) * 2
    request = ota_packager.build_signing_request(
        transcript,
        transcript_path=Path("candidate.transcript.bin"),
        product_id="DHRT100",
        hardware_id="dhrt100",
        app_version="1.2.3",
        build_id="build",
        min_bootloader_version="0.1.0",
        security_counter=9,
        key_id=7,
        image_a=b"a",
        image_b=b"bb",
    )
    encoded = json.dumps(request)
    assert request["algorithm"] == "ecdsa-p256-sha256"
    assert request["signature_format"] == "raw-r-s-64"
    assert request["transcript_sha256"] == hashlib.sha256(transcript).hexdigest()
    assert "private" not in encoded.lower()


@pytest.mark.parametrize(
    ("security_counter", "key_id", "signature"),
    [
        (1, 0, bytes(64)),
        (0, 1, bytes(64)),
        (1, 1, bytes(63)),
    ],
)
def test_signed_manifest_metadata_fails_closed(
    security_counter: int, key_id: int, signature: bytes
) -> None:
    layout = ota_packager.DeploymentLayout(
        app_a=ota_packager.AppPartition(offset=0x40000, size=0x180000),
        app_b=ota_packager.AppPartition(offset=0x1C0000, size=0x180000),
    )
    with pytest.raises(ValueError):
        ota_packager.build_package(
            b"a",
            b"b",
            product_id="DHRT100",
            hardware_id="dhrt100",
            app_version=(1, 0, 0),
            build_id="negative",
            min_bootloader_version=(0, 1, 0),
            layout=layout,
            security_counter=security_counter,
            key_id=key_id,
            signature=signature,
        )


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


def test_v2_candidate_manifest_requires_explicit_opt_in() -> None:
    manifest = ROOT / "config" / "flash_map_gen" / "flash_map_v2_manifest.json"
    with pytest.raises(ValueError, match="explicit"):
        ota_packager.load_deployment_layout(manifest)
    layout = ota_packager.load_deployment_layout(
        manifest, allow_target_not_deployed=True
    )
    assert layout.app_a.offset == 0x00080000
    assert layout.app_b.offset == 0x00280000
