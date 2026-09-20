"""Pure decoding and provenance helpers for the VDC priority probe chain."""

import csv
import hashlib
import json
from pathlib import Path


IDS = [
    "0010071E65B5CB38", "FB276192BEF9CCE1",
    "2BD5090FE009FA2A", "A1E549202D18ED6A",
]
OBSERVER_FIELDS = (
    "service snapshot eligible path expand submitted accepted rejected "
    "last_sequence last_config last_result"
).split()
TRACE_FIELDS = (
    "armed complete sample_count dropped first_update_seq last_update_seq "
    "start_ms end_ms"
).split()
BASELINE_SOURCE = "2e61755a453b740193a28061463f4e0bba3f1460616cec485918173fa6cfcbd9"
BASELINE_PACKAGE = "8353019b485c986b19ded5436a8a7261fb2cea94fbb0e920efbe6dbb9b9a9ce2"
BASELINE_OTA = "83db8bad02ccbaa40896b640fddb6500d66cfaf77db53f87c589c66ca3e15d51"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(raw):
    return [value.strip().strip('"') for value in next(csv.reader([raw]))]


def uints(raw, names):
    items = fields(raw)
    if len(items) != len(names):
        raise ValueError(f"field count mismatch: {raw!r}")
    result = dict(zip(names, (int(value, 0) for value in items), strict=True))
    if any(value < 0 or value > 0xFFFFFFFF for value in result.values()):
        raise ValueError(f"non-uint32 result: {raw!r}")
    return result


def observer_delta(before, after):
    return {
        key: (after[key] - before[key]) & 0xFFFFFFFF
        for key in OBSERVER_FIELDS[:8]
    }


def bind_receipt(root, p3, working_fingerprint, validate_ota):
    path = p3 if p3.is_file() else p3 / "acceptance.json"
    receipt = json.loads(path.read_text(encoding="utf-8"))
    if not receipt.get("passed") or not receipt.get("strict_gates_passed"):
        raise ValueError("P3 receipt must pass its strict gates")
    if receipt.get("tdma_board_ids") != IDS or receipt.get("ota_board_ids") != IDS:
        raise ValueError("receipt must bind exactly NO1..NO4 in physical order")
    artifacts = []
    for item in receipt.values():
        if isinstance(item, dict) and "path" in item and "sha256" in item:
            artifact = Path(item["path"])
            if not artifact.is_absolute():
                artifact = root / artifact
            if sha(artifact) != item["sha256"]:
                raise ValueError(f"P3 artifact hash mismatch: {artifact}")
            artifacts.append({"path": str(artifact), "sha256": item["sha256"]})
    ota_path = root / receipt["ota_summary"]["path"]
    ota = json.loads(ota_path.read_text(encoding="utf-8"))
    validate_ota(ota, IDS, receipt["build_id"])
    baseline = receipt["source_tree_sha256"] == BASELINE_SOURCE
    if baseline and (
            receipt["firmware_package"]["sha256"] != BASELINE_PACKAGE or
            receipt["ota_summary"]["sha256"] != BASELINE_OTA):
        raise ValueError("known baseline source has unexpected package/OTA hash")
    fingerprint, count = working_fingerprint(root)
    return receipt, {
        "receipt_path": str(path.resolve()), "receipt_sha256": sha(path),
        "board_source_sha256": receipt["source_tree_sha256"],
        "board_source_file_count": receipt["source_file_count"],
        "package_sha256": receipt["firmware_package"]["sha256"],
        "ota_sha256": receipt["ota_summary"]["sha256"], "artifacts": artifacts,
        "working_source_sha256": fingerprint, "working_source_file_count": count,
        "working_source_matches_board_receipt": (
            fingerprint == receipt["source_tree_sha256"] and
            count == receipt["source_file_count"]),
        "known_p3_r2_baseline": baseline,
        "binding_limit": (
            "Board image is bound by selected P3/package/OTA evidence plus online UID/build. "
            "Build labels alone are not package fingerprints; no board flash hash query exists. "
            "Unflashed workspace edits are never treated as the board image."),
    }


def native_input_summary(decoded):
    samples = decoded["samples"]
    baseline = decoded["baseline"]["snapshot"]
    if not samples:
        return {"sample_count": 0, "reason": "no_runtime_snapshot",
                "terminal": decoded["terminal"]}
    last = samples[-1]["snapshot"]
    before, after = baseline["runtime"], last["runtime"]
    counters = {
        key: (value - before[key]) & 0xFFFFFFFF
        for key, value in after.items()
        if key in before and ("count" in key or key.endswith("_bad"))
    }
    return {
        "sample_count": len(samples), "terminal": decoded["terminal"],
        "collection_errors": decoded["errors"], "runtime_counter_deltas": counters,
        "first_runtime_snapshot": samples[0]["snapshot"],
        "last_runtime_snapshot": last,
        "qualification": "Sparse Core0 diagnostics; not per-edge accuracy or WCET proof",
    }
