#!/usr/bin/env python3
"""Ensure a local debug OTA key and a build-local public-key config exist.

This is deliberately debug-only:

* the private key stays under ``out/`` (never source-controlled);
* the public registry is written to the build tree, not the tracked config;
* an existing private key is reused so repeated builds keep the same identity;
* a missing key is generated once and announced on stdout.

It is not a production signer.  Production keys must remain external.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-key", required=True, type=Path)
    parser.add_argument("--source-config", required=True, type=Path)
    parser.add_argument("--output-config", required=True, type=Path)
    parser.add_argument("--key-id", required=True, type=int)
    parser.add_argument("--profile", default="v2_debug")
    parser.add_argument("--role", default="dev", choices=("dev", "factory", "release"))
    return parser.parse_args()


def public_key_hex(private_key: bytes) -> str:
    key = serialization.load_pem_private_key(private_key, password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
            key.curve, ec.SECP256R1):
        raise ValueError("debug key must be an unencrypted P-256 private key")
    public = key.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    return public.hex()


def main() -> int:
    args = parse_args()
    if not 0 < args.key_id <= 0xFFFFFFFF:
        raise SystemExit("key id must be in range 1..0xFFFFFFFF")
    if not args.source_config.is_file():
        raise SystemExit(f"source OTA key config missing: {args.source_config}")

    created = False
    if args.private_key.is_file():
        private = args.private_key.read_bytes()
    else:
        key = ec.generate_private_key(ec.SECP256R1())
        private = key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
        args.private_key.parent.mkdir(parents=True, exist_ok=True)
        args.private_key.write_bytes(private)
        created = True

    public_hex = public_key_hex(private)
    document = json.loads(args.source_config.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise SystemExit("unsupported OTA public key schema_version")
    if document.get("algorithm") != "ecdsa-p256-sha256":
        raise SystemExit("unsupported OTA public key algorithm")

    keys = [entry for entry in document.get("keys", [])
            if entry.get("key_id") != args.key_id]
    keys.append({
        "key_id": args.key_id,
        "role": args.role,
        "revoked": False,
        "public_key_hex": public_hex,
    })
    document["keys"] = sorted(keys, key=lambda entry: entry["key_id"])

    profiles = document.setdefault("profiles", {})
    profile = profiles.setdefault(args.profile, {})
    profile["require_signature"] = True
    allowed = profile.setdefault("allowed_roles", [])
    if args.role not in allowed:
        allowed.append(args.role)

    args.output_config.parent.mkdir(parents=True, exist_ok=True)
    args.output_config.write_text(json.dumps(document, indent=2) + "\n",
                                  encoding="utf-8", newline="\n")
    print(f"ota_debug_key={'generated' if created else 'reused'} "
          f"key_id={args.key_id} profile={args.profile} "
          f"private_key={args.private_key} config={args.output_config}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
