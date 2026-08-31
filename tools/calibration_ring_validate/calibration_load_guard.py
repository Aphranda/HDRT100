#!/usr/bin/env python3
"""Prove offline calibration leaves the TDMA realtime load mask untouched.

Calibration and training commands publish Core0-to-Core1 intents.  Those
intents are consumed by the ring-stopped offline Core1 service and therefore
must not borrow the online TDMA calibration phase.  This guard records the
entry schedule and rejects any tool or firmware path that mutates the load
    mask or quarantine state.  Online schedule-miss counters are not scored
    while the ring is deliberately stopped and the phase table is bypassed.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from typing import Any, Sequence

from tdma_start_ring import Board, board_command


CALIBRATION_LOAD_MASK = 1 << 2
CALIBRATION_PHASE = 3
SCHEDULE_HEADER_FIELDS = 8
SCHEDULE_PHASE_FIELDS = (
    "start_cycle", "end_cycle", "wcet_cycles", "last_start_cycle",
    "last_runtime_cycles", "max_runtime_cycles", "run_count", "skip_count",
    "start_miss_count", "overrun_count", "deadline_miss_count",
)


def read_schedule(board: Board, args: argparse.Namespace) -> dict[str, Any]:
    raw = board_command(board, "SYSTem:TDMA:SCHEDule?", args)
    try:
        values = [int(value.strip().strip('"'), 0)
                  for value in next(csv.reader([raw]), [])]
    except ValueError as exc:
        raise RuntimeError(
            f"{board.address}: invalid TDMA schedule {raw!r}") from exc
    if len(values) < SCHEDULE_HEADER_FIELDS:
        raise RuntimeError(f"{board.address}: truncated TDMA schedule {raw!r}")
    phase_count = values[3]
    expected = (SCHEDULE_HEADER_FIELDS +
                phase_count * len(SCHEDULE_PHASE_FIELDS))
    if phase_count <= CALIBRATION_PHASE or len(values) != expected:
        raise RuntimeError(
            f"{board.address}: invalid TDMA schedule shape: "
            f"phase_count={phase_count}, fields={len(values)}, "
            f"expected={expected}")
    phases = []
    for phase in range(phase_count):
        start = SCHEDULE_HEADER_FIELDS + phase * len(SCHEDULE_PHASE_FIELDS)
        phases.append(dict(zip(
            SCHEDULE_PHASE_FIELDS,
            values[start:start + len(SCHEDULE_PHASE_FIELDS)])))
    return {
        "enabled_mask": values[4],
        "quarantined_mask": values[5],
        "cycle_count": values[6],
        "schedule_miss_count": values[7],
        "phases": phases,
    }


@dataclass
class CalibrationLoadGuard:
    boards: Sequence[Board]
    args: argparse.Namespace
    timeout_s: float = 2.0

    def __post_init__(self) -> None:
        self._original: dict[str, int] = {}
        self._before: dict[str, dict[str, Any]] = {}
        self._after: dict[str, dict[str, Any]] = {}
        self._entered = False

    def __enter__(self) -> "CalibrationLoadGuard":
        if self._entered:
            raise RuntimeError("calibration load guard cannot be entered twice")
        self._entered = True
        for board in self.boards:
            before = read_schedule(board, self.args)
            if (int(before["enabled_mask"]) & CALIBRATION_LOAD_MASK) != 0:
                raise RuntimeError(
                    f"{board.address}: offline calibration requires the "
                    "online calibration load to remain disabled")
            if (int(before["quarantined_mask"]) &
                    CALIBRATION_LOAD_MASK) != 0:
                raise RuntimeError(
                    f"{board.address}: calibration quarantine is not clean; "
                    "use a software reboot before retrying")
            self._before[board.address] = before
            self._original[board.address] = int(before["enabled_mask"])
        return self

    def verify(self) -> None:
        errors: list[str] = []
        for board in self.boards:
            original = self._original.get(board.address)
            if original is None:
                continue
            try:
                after = read_schedule(board, self.args)
                self._after[board.address] = after
                before = self._before[board.address]
                if int(after["enabled_mask"]) != original:
                    errors.append(
                        f"{board.address}: load mask changed "
                        f"{original}->{after['enabled_mask']}")
                if int(after["quarantined_mask"]) != int(
                        before["quarantined_mask"]):
                    errors.append(
                        f"{board.address}: quarantine mask changed "
                        f"{before['quarantined_mask']}->"
                        f"{after['quarantined_mask']}")
            except Exception as exc:  # retain every board cleanup attempt
                errors.append(f"{board.address}: {type(exc).__name__}: {exc}")
        if errors:
            raise RuntimeError(
                "offline calibration disturbed TDMA schedule: " +
                "; ".join(errors))

    def __exit__(self, exc_type: object, exc: object,
                 traceback: object) -> bool:
        try:
            self.verify()
        except Exception:
            if exc is None:
                raise
        return False

    def evidence(self) -> dict[str, Any]:
        passed = (len(self._before) == len(self.boards) and
                  len(self._after) == len(self.boards) and
                  all(int(self._after[address]["enabled_mask"]) ==
                      int(before["enabled_mask"]) and
                      int(self._after[address]["quarantined_mask"]) ==
                      int(before["quarantined_mask"])
                      for address, before in self._before.items()))
        return {
            "schema": "HAOFV_CALIBRATION_LOAD_GUARD_V1",
            "passed": passed,
            "calibration_load_mask": CALIBRATION_LOAD_MASK,
            "before": {
                address: {
                    "enabled_mask": int(row["enabled_mask"]),
                    "quarantined_mask": int(row["quarantined_mask"]),
                }
                for address, row in self._before.items()
            },
            "execution_domain": "ring_stopped_offline_core1",
            "after": {
                address: {
                    "enabled_mask": int(row["enabled_mask"]),
                    "quarantined_mask": int(row["quarantined_mask"]),
                    "schedule_miss_count": int(row["schedule_miss_count"]),
                }
                for address, row in self._after.items()
            },
        }
