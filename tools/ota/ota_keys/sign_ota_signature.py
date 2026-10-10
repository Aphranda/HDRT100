#!/usr/bin/env python3
"""Sign an OTA transcript with a local P-256 key and emit raw low-S r||s."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils


P256_ORDER = int(
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551",
    16,
)
RAW_SIGNATURE_SIZE = 64


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-key", required=True, type=Path,
                        help="local PEM P-256 private key (never a production registry file)")
    parser.add_argument("--transcript", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="raw 64-byte r||s signature output")
    return parser.parse_args()


def sign_transcript(private_key: bytes, transcript: bytes) -> bytes:
    key = serialization.load_pem_private_key(private_key, password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
            key.curve, ec.SECP256R1):
        raise ValueError("private key must be an unencrypted P-256 key")

    der = key.sign(transcript, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    if not 0 < r < P256_ORDER or not 0 < s < P256_ORDER:
        raise ValueError("generated signature scalar outside P-256 order")
    s = min(s, P256_ORDER - s)
    signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    if len(signature) != RAW_SIGNATURE_SIZE:
        raise AssertionError("raw signature length invariant violated")
    return signature


def main() -> int:
    args = parse_args()
    transcript = args.transcript.read_bytes()
    signature = sign_transcript(args.private_key.read_bytes(), transcript)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(signature)
    print(f"ota_signature_sign=OK transcript_size={len(transcript)} "
          f"transcript_sha256={hashlib.sha256(transcript).hexdigest()} "
          f"signature_size={len(signature)} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
