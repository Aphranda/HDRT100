"""Date-partitioned run logs for calibration workflows."""

from __future__ import annotations

import time
from datetime import date
from pathlib import Path


class RunLog:
    """Append-only text log under logs/YYYY-MM-DD/."""

    def __init__(self, root, label: str, run_id: str = "") -> None:
        self.root = Path(root)
        self.label = label
        self.run_id = run_id
        stamp = time.strftime("%H%M%S")
        suffix = f"-{run_id}" if run_id else ""
        self.day = date.today().isoformat()
        self.path = self.root / self.day / f"{label}-{stamp}{suffix}.log"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._write_header()

    def _write_header(self) -> None:
        self.path.write_text(
            f"# {self.label} {self.day} {time.strftime('%H:%M:%S')}\n",
            encoding="utf-8")

    def write(self, text: str) -> None:
        with self.path.open("a", encoding="utf-8") as stream:
            stream.write(f"[{time.strftime('%H:%M:%S')}] {text}\n")

    def close(self) -> None:
        self.write("log closed")
