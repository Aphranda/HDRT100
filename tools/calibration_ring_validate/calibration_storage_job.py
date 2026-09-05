"""Bounded StorageAO FILE_WRITE wait shared by calibration capture tools."""

from __future__ import annotations

import csv
import time
from typing import Any, Callable


Execute = Callable[[Any, str, Any], str]


def wait_file_write_job(
        board: Any, job_id: int, expected_path: str, args: Any,
        execute: Execute, label: str) -> dict[str, Any]:
    """Wait for one exact FILE_WRITE job and retain timing/state evidence."""
    timeout_s = float(args.storage_timeout)
    poll_interval_s = max(0.0, float(args.poll_interval))
    if timeout_s <= 0.0:
        raise ValueError("storage_timeout must be positive")
    started = time.monotonic()
    deadline = started + timeout_s
    poll_count = 0
    last = ""
    state_history: list[dict[str, Any]] = []
    previous_signature: tuple[Any, ...] | None = None
    while time.monotonic() < deadline:
        last = execute(board, "SYSTem:STORage:JOB?", args)
        poll_count += 1
        values = [value.strip().strip('"')
                  for value in next(csv.reader([last]), [])]
        elapsed_s = round(time.monotonic() - started, 6)
        if len(values) >= 8:
            try:
                snapshot = {
                    "elapsed_s": elapsed_s,
                    "state": values[0],
                    "job_id": int(values[1], 0),
                    "type": values[2],
                    "path": values[3],
                    "size": int(values[4], 0),
                    "kind": values[5],
                    "path_hash": int(values[6], 0),
                    "error": int(values[7], 0),
                }
            except ValueError:
                snapshot = {"elapsed_s": elapsed_s, "raw": last}
            signature = tuple(snapshot.get(field) for field in (
                "state", "job_id", "type", "path", "size", "error"))
            if signature != previous_signature:
                state_history.append(snapshot)
                previous_signature = signature
            if snapshot.get("job_id") == job_id:
                if (snapshot.get("type") != "FILE_WRITE" or
                        snapshot.get("path") != expected_path):
                    raise RuntimeError(
                        f"{board.address}: {label} SD job identity changed: "
                        f"{last!r}")
                if snapshot.get("state") == "DONE":
                    return {
                        "job_id": job_id,
                        "size": int(snapshot["size"]),
                        "state": "DONE",
                        "storage_elapsed_s": elapsed_s,
                        "storage_poll_count": poll_count,
                        "storage_state_history": state_history,
                        "storage_final_raw": last,
                    }
                if snapshot.get("state") == "FAILED":
                    raise RuntimeError(
                        f"{board.address}: {label} SD job failed after "
                        f"{elapsed_s:.3f}s/{poll_count} polls: {last!r}")
        if poll_interval_s > 0.0:
            time.sleep(poll_interval_s)
    elapsed_s = round(time.monotonic() - started, 6)
    raise RuntimeError(
        f"{board.address}: {label} SD job timeout after "
        f"{elapsed_s:.3f}s/{poll_count} polls; "
        f"state_history={state_history!r}; last={last!r}")
