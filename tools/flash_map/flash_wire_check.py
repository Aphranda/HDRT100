#!/usr/bin/env python3
"""Validate HAOFV Flash/OTA/TDMA wire contract input and golden vectors."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def validate(path: Path) -> list[str]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"wire input unreadable: {exc}"]
    errors: list[str] = []
    if data.get("wire_version") != 1:
        errors.append("wire_version must be 1")
    if data.get("unknown_required_policy") != "reject":
        errors.append("unknown required fields must reject")
    reasons = set(data.get("reject_reasons", []))
    required_reasons = {"bad_magic", "bad_version", "bad_length", "bad_crc", "unknown_required", "identity_mismatch", "generation_replay", "destination_forbidden", "offset_not_durable", "signature_invalid", "security_counter_rollback"}
    if required_reasons - reasons:
        errors.append("reject reasons incomplete")
    contracts = data.get("contracts", [])
    names: set[str] = set()
    for index, contract in enumerate(contracts):
        name = contract.get("name")
        if not isinstance(name, str) or not name:
            errors.append(f"contracts[{index}] missing name")
        elif name in names:
            errors.append(f"duplicate contract: {name}")
        else:
            names.add(name)
        if not contract.get("fields"):
            errors.append(f"contracts[{index}] fields empty")
        if not contract.get("commit_order"):
            errors.append(f"contracts[{index}] commit_order empty")
    for index, vector in enumerate(data.get("golden_vectors", [])):
        if vector.get("contract") not in names:
            errors.append(f"golden_vectors[{index}] references unknown contract")
        if vector.get("expected") not in {"accept", "reject"}:
            errors.append(f"golden_vectors[{index}] expected must accept/reject")
        if vector.get("expected") == "reject" and vector.get("reason") not in reasons:
            errors.append(f"golden_vectors[{index}] reject reason not registered")
    if len(data.get("golden_vectors", [])) < 4:
        errors.append("at least four golden vectors required")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("contracts", type=Path)
    args = parser.parse_args()
    errors = validate(args.contracts)
    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    data = json.loads(args.contracts.read_text(encoding="utf-8"))
    print(f"flash_wire=OK contracts={len(data['contracts'])} golden={len(data['golden_vectors'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
