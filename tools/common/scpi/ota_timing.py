#!/usr/bin/env python3
"""Structured timing and serial-response evidence for OTA tools."""

from __future__ import annotations

import json
import time
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


class OtaTiming:
    """Collect monotonic phase timings and preserve exact serial responses."""

    def __init__(self, transport: str) -> None:
        self.transport = transport
        self.started = time.monotonic()
        self.started_at_utc = datetime.now(timezone.utc).isoformat()
        self.events: list[dict[str, Any]] = []

    def record(self, stage: str, elapsed_s: float, **details: Any) -> None:
        event: dict[str, Any] = {
            "sequence": len(self.events),
            "stage": stage,
            "at_s": time.monotonic() - self.started,
            "elapsed_s": elapsed_s,
        }
        event.update(details)
        self.events.append(event)

    def stage_stats(self) -> dict[str, dict[str, float | int]]:
        values: dict[str, list[float]] = defaultdict(list)
        for event in self.events:
            values[event["stage"]].append(float(event["elapsed_s"]))
        return {
            stage: {
                "count": len(samples),
                "total_s": sum(samples),
                "min_s": min(samples),
                "mean_s": sum(samples) / len(samples),
                "max_s": max(samples),
            }
            for stage, samples in values.items()
        }

    def write(self, out_dir: Path, *, passed: bool, error: str = "",
              metadata: dict[str, Any] | None = None) -> None:
        out_dir.mkdir(parents=True, exist_ok=True)
        summary = {
            "transport": self.transport,
            "passed": passed,
            "error": error,
            "started_at_utc": self.started_at_utc,
            "elapsed_s": time.monotonic() - self.started,
            "metadata": metadata or {},
            "stage_stats": self.stage_stats(),
            "events": self.events,
        }
        (out_dir / "timing.json").write_text(
            json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        serial_events = [
            event for event in self.events
            if "request" in event or "response" in event or
            "received_lines" in event
        ]
        with (out_dir / "serial_trace.jsonl").open(
                "w", encoding="utf-8", newline="\n") as handle:
            for event in serial_events:
                handle.write(json.dumps(event, ensure_ascii=False) + "\n")
