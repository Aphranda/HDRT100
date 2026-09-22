"""High-resolution monotonic clock identity for host-side timing evidence."""

from __future__ import annotations

import math
import time
from typing import Any


SCHEMA = "HAOFV_HOST_EVIDENCE_CLOCK_V1"
CLOCK_NAME = "perf_counter"
MAX_RESOLUTION_NS = 1000


def identity() -> dict[str, Any]:
    info = time.get_clock_info(CLOCK_NAME)
    resolution_ns = max(1, math.ceil(info.resolution * 1_000_000_000))
    result = {
        "schema": SCHEMA,
        "name": CLOCK_NAME,
        "implementation": info.implementation,
        "monotonic": info.monotonic,
        "adjustable": info.adjustable,
        "resolution_ns": resolution_ns,
    }
    validate(result)
    return result


def validate(value: Any) -> dict[str, Any]:
    if (not isinstance(value, dict) or value.get("schema") != SCHEMA or
            value.get("name") != CLOCK_NAME or
            not isinstance(value.get("implementation"), str) or
            not value["implementation"] or value.get("monotonic") is not True or
            value.get("adjustable") is not False or
            isinstance(value.get("resolution_ns"), bool) or
            not isinstance(value.get("resolution_ns"), int) or
            not 0 < value["resolution_ns"] <= MAX_RESOLUTION_NS):
        raise ValueError("host evidence clock identity or resolution is invalid")
    return value


def now_ns() -> int:
    return time.perf_counter_ns()
