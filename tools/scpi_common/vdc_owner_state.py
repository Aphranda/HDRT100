"""Bounded management-plane acknowledgement of a queued VDC owner change."""
from __future__ import annotations

import csv
import math
import time
from typing import Callable

OWNER_QUERY = "SYSTem:SYNC:VDC:DPLL:OWNer?"


def queued_sequence(response: str) -> int:
    fields = next(csv.reader([response]), [])
    if len(fields) < 2 or fields[0].strip().upper() != "QUEUED":
        raise RuntimeError(f"expected queued owner acknowledgement: {response!r}")
    try:
        sequence = int(fields[-1].strip(), 0)
    except ValueError as exc:
        raise RuntimeError(f"invalid owner sequence: {response!r}") from exc
    if not 0 < sequence <= 0xFFFFFFFF:
        raise RuntimeError(f"invalid owner sequence: {response!r}")
    return sequence


def wait_owner_applied(query: Callable[[str], str], sequence: int,
                       timeout_s: float, evidence: list[dict], *,
                       poll_s: float = 0.02,
                       monotonic: Callable[[], float] | None = None,
                       sleep: Callable[[float], None] | None = None) -> dict:
    """Wait before ARM, never during acquisition; retain every observed state.

    APPLIED alone is insufficient: Core0 must also have consumed the owner ACK
    and published its active calibration/topology (pending == 0).
    """
    if (not 0 < sequence <= 0xFFFFFFFF or
            not math.isfinite(timeout_s) or timeout_s <= 0 or
            not math.isfinite(poll_s) or poll_s <= 0):
        raise ValueError("sequence and finite positive timeouts are required")
    monotonic = monotonic or time.monotonic
    sleep = sleep or time.sleep
    deadline = monotonic() + timeout_s
    while True:
        record = {"command": OWNER_QUERY, "host_monotonic": monotonic()}
        evidence.append(record)
        raw = query(OWNER_QUERY)
        record["response"] = raw
        try:
            fields = [int(field.strip(), 0) for field in next(csv.reader([raw]), [])]
        except (ValueError, csv.Error) as exc:
            raise RuntimeError(f"invalid owner status: {raw!r}") from exc
        if len(fields) != 5 or fields[3] not in (0, 1, 2, 3) or fields[4] not in (0, 1):
            raise RuntimeError(f"invalid owner status: {raw!r}")
        requested, applied, opcode, result, pending = fields
        if requested != sequence:
            raise RuntimeError(f"owner request changed: expected {sequence}, observed {raw!r}")
        if applied == sequence and result == 3:
            raise RuntimeError(f"owner rejected sequence {sequence}: {raw!r}")
        if applied == sequence and result == 2 and pending == 0:
            return {"sequence": sequence, "opcode": opcode, "applied": True}
        remaining = deadline - monotonic()
        if remaining <= 0:
            raise TimeoutError(f"owner application timed out for {sequence}: {raw!r}")
        sleep(min(poll_s, remaining))
