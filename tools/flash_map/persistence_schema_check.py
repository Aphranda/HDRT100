#!/usr/bin/env python3
"""Validate the HAOFV persistence namespace/object registry."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

REQUIRED = {
    "namespace", "object_type", "type_id", "schema_version", "writer", "reader",
    "compatibility", "frequency", "endurance", "atomicity", "rollback",
    "factory_default", "diagnostic_projection", "sd_evidence",
}
FORBIDDEN_RUNTIME = {
    "domain_vector", "ecc_state", "queue_fifo", "lock_counter_cursor",
    "vdc_lock", "refmem_epoch_ack", "pio_dma_runtime",
}


def validate(path: Path) -> list[str]:
    errors: list[str] = []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"registry unreadable: {exc}"]
    if data.get("registry_version") != 1:
        errors.append("registry_version must be 1")
    policy = data.get("unknown_field_policy", {})
    if policy.get("required") != "fail_closed":
        errors.append("required unknown fields must fail closed")
    if policy.get("optional") != "skip_preserve_length_and_integrity":
        errors.append("optional unknown fields must preserve length/integrity")
    inventory = set(data.get("negative_inventory", []))
    missing = FORBIDDEN_RUNTIME - inventory
    if missing:
        errors.append("negative inventory missing: " + ",".join(sorted(missing)))
    objects = data.get("objects")
    if not isinstance(objects, list) or not objects:
        return errors + ["objects must be a non-empty list"]
    ids: set[int] = set()
    names: set[str] = set()
    for index, obj in enumerate(objects):
        if not isinstance(obj, dict):
            errors.append(f"objects[{index}] must be an object")
            continue
        missing_fields = REQUIRED - obj.keys()
        if missing_fields:
            errors.append(f"objects[{index}] missing: {','.join(sorted(missing_fields))}")
        type_id = obj.get("type_id")
        if not isinstance(type_id, int) or type_id <= 0:
            errors.append(f"objects[{index}] type_id must be positive")
        elif type_id in ids:
            errors.append(f"duplicate type_id: {type_id}")
        else:
            ids.add(type_id)
        name = obj.get("object_type")
        if not isinstance(name, str) or not name:
            errors.append(f"objects[{index}] object_type must be non-empty")
        elif name in names:
            errors.append(f"duplicate object_type: {name}")
        else:
            names.add(name)
        if obj.get("schema_version") != 1:
            errors.append(f"objects[{index}] schema_version must be 1")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("registry", type=Path)
    args = parser.parse_args()
    errors = validate(args.registry)
    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    data = json.loads(args.registry.read_text(encoding="utf-8"))
    print(f"persistence_schema=OK objects={len(data['objects'])} types={len(data['objects'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
