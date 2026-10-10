#!/usr/bin/env python3
"""Generate a local P-256 OTA signing key and its public registry entry."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-key", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--key-id", required=True, type=int)
    parser.add_argument("--role", choices=("dev", "factory", "release"), default="dev")
    parser.add_argument("--profile", default="v2_debug")
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 0 < args.key_id <= 0xFFFFFFFF:
        raise SystemExit("key id must be in range 1..0xFFFFFFFF")
    if args.private_key.exists() and not args.force:
        raise SystemExit(f"refusing to overwrite existing key: {args.private_key}")

    private = ec.generate_private_key(ec.SECP256R1())
    pem = private.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )
    public = private.public_key().public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )
    args.private_key.parent.mkdir(parents=True, exist_ok=True)
    args.private_key.write_bytes(pem)

    document = json.loads(args.config.read_text(encoding="utf-8"))
    keys = [entry for entry in document.get("keys", [])
            if entry.get("key_id") != args.key_id]
    keys.append({
        "key_id": args.key_id,
        "role": args.role,
        "revoked": False,
        "public_key_hex": public.hex(),
    })
    document["keys"] = sorted(keys, key=lambda entry: entry["key_id"])
    profiles = document.setdefault("profiles", {})
    profiles.setdefault(args.profile, {
        "require_signature": True,
        "allowed_roles": [args.role],
    })
    args.config.write_text(json.dumps(document, indent=2) + "\n",
                          encoding="utf-8", newline="\n")
    print(f"ota_key_generate=OK key_id={args.key_id} role={args.role} "
          f"private_key={args.private_key} public_key_hex={public.hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
