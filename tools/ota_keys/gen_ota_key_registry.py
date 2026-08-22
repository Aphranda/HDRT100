#!/usr/bin/env python3
"""Generate a public-only portable OTA key registry for one build profile."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROLE_BITS = {"dev": 1, "release": 2, "factory": 4}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    return parser.parse_args()


def load_registry(path: Path, profile_name: str) -> tuple[list[dict[str, object]], int, bool]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read OTA public key config: {exc}") from exc
    if document.get("schema_version") != 1:
        raise ValueError("unsupported OTA public key schema_version")
    if document.get("algorithm") != "ecdsa-p256-sha256":
        raise ValueError("unsupported OTA public key algorithm")

    profiles = document.get("profiles")
    if not isinstance(profiles, dict) or profile_name not in profiles:
        raise ValueError(f"unknown OTA key profile: {profile_name}")
    profile = profiles[profile_name]
    if not isinstance(profile, dict) or not isinstance(profile.get("require_signature"), bool):
        raise ValueError("OTA key profile must define boolean require_signature")
    allowed_roles = profile.get("allowed_roles")
    if not isinstance(allowed_roles, list) or not allowed_roles:
        raise ValueError("OTA key profile allowed_roles must be a non-empty list")
    allowed_mask = 0
    seen_roles: set[str] = set()
    for role in allowed_roles:
        if not isinstance(role, str) or role not in ROLE_BITS:
            raise ValueError("OTA key profile contains an unknown role")
        if role in seen_roles:
            raise ValueError(f"OTA key profile contains duplicate role: {role}")
        seen_roles.add(role)
        allowed_mask |= ROLE_BITS[role]

    raw_keys = document.get("keys")
    if not isinstance(raw_keys, list):
        raise ValueError("OTA public keys must be a list")
    keys: list[dict[str, object]] = []
    seen_ids: set[int] = set()
    for raw in raw_keys:
        if not isinstance(raw, dict):
            raise ValueError("OTA public key entry must be an object")
        key_id = raw.get("key_id")
        role = raw.get("role")
        revoked = raw.get("revoked", False)
        public_hex = raw.get("public_key_hex")
        if (not isinstance(key_id, int) or isinstance(key_id, bool) or
                key_id <= 0 or key_id > 0xFFFFFFFF):
            raise ValueError("OTA key_id must be in range 1..0xFFFFFFFF")
        if key_id in seen_ids:
            raise ValueError(f"duplicate OTA key_id: {key_id}")
        if not isinstance(role, str) or role not in ROLE_BITS or not isinstance(revoked, bool):
            raise ValueError(f"invalid OTA key role/revocation for key_id {key_id}")
        if not isinstance(public_hex, str):
            raise ValueError(f"missing public_key_hex for key_id {key_id}")
        try:
            public_key = bytes.fromhex(public_hex)
        except ValueError as exc:
            raise ValueError(f"invalid public_key_hex for key_id {key_id}") from exc
        if len(public_key) != 65 or public_key[0] != 0x04:
            raise ValueError(f"key_id {key_id} must use 65-byte uncompressed SEC1 P-256")
        seen_ids.add(key_id)
        keys.append({
            "key_id": key_id,
            "role_mask": ROLE_BITS[role],
            "flags": 1 if revoked else 0,
            "public_key": public_key,
        })
    return keys, allowed_mask, profile["require_signature"]


def render_header(require_signature: bool) -> str:
    return (
        "#ifndef PORTABLE_OTA_KEY_REGISTRY_GENERATED_H\n"
        "#define PORTABLE_OTA_KEY_REGISTRY_GENERATED_H\n\n"
        "#include <stdbool.h>\n"
        "#include \"pota_manifest_verifier.h\"\n\n"
        "extern const pota_public_key_registry_t g_portable_ota_key_registry;\n"
        f"#define PORTABLE_OTA_REQUIRE_SIGNATURE {1 if require_signature else 0}\n\n"
        "#endif\n"
    )


def render_source(keys: list[dict[str, object]], allowed_mask: int) -> str:
    lines = ['#include "portable_ota_key_registry.generated.h"', ""]
    if keys:
        lines.append("static const pota_public_key_entry_t s_keys[] = {")
        for key in keys:
            public_key = key["public_key"]
            assert isinstance(public_key, bytes)
            encoded = ", ".join(f"0x{byte:02X}u" for byte in public_key)
            lines.extend([
                "    {",
                f"        .key_id = {key['key_id']}u,",
                f"        .role_mask = {key['role_mask']}u,",
                f"        .flags = {key['flags']}u,",
                f"        .public_key = {{{encoded}}},",
                "    },",
            ])
        lines.extend(["};", ""])
        entries = "s_keys"
        count = "(uint32_t)(sizeof(s_keys) / sizeof(s_keys[0]))"
    else:
        entries = "NULL"
        count = "0u"
    lines.extend([
        "const pota_public_key_registry_t g_portable_ota_key_registry = {",
        f"    .entries = {entries},",
        f"    .entry_count = {count},",
        f"    .allowed_role_mask = {allowed_mask}u,",
        "};",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    try:
        keys, allowed_mask, require_signature = load_registry(args.config, args.profile)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.source.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(render_header(require_signature), encoding="utf-8", newline="\n")
    args.source.write_text(render_source(keys, allowed_mask), encoding="utf-8", newline="\n")
    print(
        f"ota_key_registry=OK profile={args.profile} keys={len(keys)} "
        f"require_signature={str(require_signature).lower()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
