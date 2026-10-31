"""Repeatability and fault-injection matrix driver for TDMA stream OTA (host)."""
from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

MATRIX_PLAN_VERSION = "OTA_TDMA_MATRIX_PLAN_V1"
DROP_COMMAND = "SYST:REFMEM:CMD:DROP"


@dataclass
class MatrixCase:
    name: str
    drop_chunks: int = 0
    repeats: int = 1
    expectation: str = "pass"


@dataclass
class MatrixPlan:
    image: str
    port: str
    relay_node: int
    block_size: int
    cases: list[MatrixCase] = field(default_factory=list)
    version: str = MATRIX_PLAN_VERSION

    def to_dict(self) -> dict:
        return {
            "version": self.version,
            "image": self.image,
            "port": self.port,
            "relay_node": self.relay_node,
            "block_size": self.block_size,
            "cases": [asdict(case) for case in self.cases],
        }

    def to_json(self) -> str:
        return json.dumps(self.to_dict(), indent=2)


def default_plan(image: str, port: str, relay_node: int, block_size: int) -> MatrixPlan:
    return MatrixPlan(
        image=image,
        port=port,
        relay_node=relay_node,
        block_size=block_size,
        cases=[
            MatrixCase("clean", repeats=3),
            MatrixCase("drop-1", drop_chunks=1),
            MatrixCase("drop-3", drop_chunks=3),
            MatrixCase("drop-8", drop_chunks=8, expectation="fail"),
        ],
    )


@dataclass
class CaseResult:
    name: str
    drop_chunks: int
    attempt: int
    passed: bool
    recovered: bool
    reason: str


def classify(name: str, drop_chunks: int, attempt: int, ok: bool, attempts_used: int) -> CaseResult:
    """Map one attempt outcome onto PASS / RECOVERED / FAIL classes."""
    if ok:
        return CaseResult(name, drop_chunks, attempt, True, attempts_used > 1,
                          "recovered" if attempts_used > 1 else "clean")
    return CaseResult(name, drop_chunks, attempt, False, False,
                      "drop-exhausted" if drop_chunks else "transfer-failed")


def run_matrix(plan: MatrixPlan, attempt_runner, drop_sender=None) -> list[CaseResult]:
    """Run every case; ``attempt_runner(case)`` returns (ok, attempts_used)."""
    results: list[CaseResult] = []
    for case in plan.cases:
        for attempt in range(1, case.repeats + 1):
            if case.drop_chunks and drop_sender is not None:
                drop_sender(case.drop_chunks)
            ok, attempts_used = attempt_runner(case)
            results.append(classify(case.name, case.drop_chunks, attempt, ok,
                                    attempts_used))
    return results


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--port", required=True)
    parser.add_argument("--relay-node", type=int, required=True)
    parser.add_argument("--block-size", type=int, default=512)
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    plan = default_plan(str(args.image), args.port, args.relay_node, args.block_size)
    text = plan.to_json()
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
