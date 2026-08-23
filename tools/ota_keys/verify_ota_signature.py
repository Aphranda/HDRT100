#!/usr/bin/env python3
"""Verify an external raw P-256 OTA manifest signature using public keys only."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.ota_keys import gen_ota_key_registry


P256_ORDER = int(
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)
RAW_SIGNATURE_SIZE = 64


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--key-id", required=True, type=int)
    parser.add_argument("--transcript", required=True, type=Path)
    parser.add_argument("--signature-file", required=True, type=Path)
    return parser.parse_args()


def verify_signature(public_key: bytes, transcript: bytes, signature: bytes) -> None:
    if len(public_key) != 65 or public_key[0] != 0x04:
        raise ValueError("public key must be 65-byte uncompressed SEC1 P-256")
    if len(signature) != RAW_SIGNATURE_SIZE:
        raise ValueError("signature must be a 64-byte raw P-256 value (r || s)")
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    if not 0 < r < P256_ORDER or not 0 < s <= P256_ORDER // 2:
        raise ValueError("signature scalars are invalid or not low-S")

    verifier = ec.EllipticCurvePublicKey.from_encoded_point(
        ec.SECP256R1(), public_key)
    try:
        verifier.verify(
            utils.encode_dss_signature(r, s),
            transcript,
            ec.ECDSA(hashes.SHA256()),
        )
    except InvalidSignature as exc:
        raise ValueError("OTA manifest signature verification failed") from exc


def main() -> int:
    args = parse_args()
    try:
        keys, allowed_mask, _ = gen_ota_key_registry.load_registry(
            args.config, args.profile)
        gen_ota_key_registry.require_signing_key(keys, allowed_mask, args.key_id)
        key = next(entry for entry in keys if entry["key_id"] == args.key_id)
        public_key = key["public_key"]
        assert isinstance(public_key, bytes)
        transcript = args.transcript.read_bytes()
        signature = args.signature_file.read_bytes()
        verify_signature(public_key, transcript, signature)
    except (OSError, ValueError, StopIteration) as exc:
        print(f"ota_signature_verify=FAILED detail={exc}")
        return 1
    print(
        f"ota_signature_verify=OK profile={args.profile} "
        f"key_id={args.key_id} transcript_size={len(transcript)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
