#!/usr/bin/env python3
"""Validate HAOFV Flash/OTA/TDMA wire contract input and golden vectors."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


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
    corpus_contracts: set[str] = set()
    for index, corpus in enumerate(data.get("parser_corpus", [])):
        contract = corpus.get("contract")
        if contract not in names:
            errors.append(f"parser_corpus[{index}] references unknown contract")
        elif contract in corpus_contracts:
            errors.append(f"duplicate parser corpus: {contract}")
        else:
            corpus_contracts.add(contract)
        if corpus.get("stage") not in {
            "deployed_compatibility",
            "deployed_package_header",
            "transport_envelope",
        }:
            errors.append(f"parser_corpus[{index}] has invalid stage")
        parser_symbol = corpus.get("parser")
        if not isinstance(parser_symbol, str) or not parser_symbol:
            errors.append(f"parser_corpus[{index}] missing parser")
        for key in ("source", "fixture"):
            value = corpus.get(key)
            candidate = ROOT / value if isinstance(value, str) else None
            if candidate is None or not candidate.is_file():
                errors.append(f"parser_corpus[{index}] {key} does not exist")
                continue
            if parser_symbol and parser_symbol not in candidate.read_text(
                encoding="utf-8", errors="replace"
            ):
                errors.append(
                    f"parser_corpus[{index}] parser missing from {key}"
                )
        mutations = corpus.get("mutations")
        if not isinstance(mutations, list) or not mutations:
            errors.append(f"parser_corpus[{index}] mutations empty")
    missing_corpus = names - corpus_contracts
    if missing_corpus:
        errors.append(
            "parser corpus missing contracts: " + ",".join(sorted(missing_corpus))
        )
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
    print(
        f"flash_wire=OK contracts={len(data['contracts'])} "
        f"corpus={len(data['parser_corpus'])} "
        f"golden={len(data['golden_vectors'])}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
