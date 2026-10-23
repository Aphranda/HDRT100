#!/usr/bin/env python3
"""Extract the SCPI control surface for REFMEM command-catalog planning.

Scans the SCPI port registration tables and writes a CSV with one row per
command: pattern, callback, query/setter, domain.  This is the N5.1 inventory;
the REFMEM command id/schema is filled in later per domain.
"""

from __future__ import annotations

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCPI_DIR = ROOT / "middleware" / "scpi_port"
OUT = ROOT / "out" / "refmem" / "SCPI_CONTROL_COMMANDS.csv"

PATTERN_RE = re.compile(
    r'\{\s*\.pattern\s*=\s*"([^"]+)"\s*,\s*\.callback\s*=\s*(\w+)\s*\}'
)


def main() -> int:
    rows = []
    for path in sorted(SCPI_DIR.rglob("*")):
        if path.suffix not in (".h", ".c"):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in PATTERN_RE.finditer(text):
            pattern, callback = match.group(1), match.group(2)
            rows.append({
                "pattern": pattern,
                "callback": callback,
                "kind": "query" if "?" in pattern else "setter",
                "domain": pattern.split(":", 1)[0] if ":" in pattern else pattern,
                "source": str(path.relative_to(ROOT)).replace("\\", "/"),
            })
    if not rows:
        print("no SCPI patterns found", file=sys.stderr)
        return 1
    seen = set()
    unique = []
    for row in rows:
        key = (row["pattern"], row["callback"])
        if key in seen:
            continue
        seen.add(key)
        unique.append(row)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=[
            "pattern", "callback", "kind", "domain", "source",
        ])
        writer.writeheader()
        writer.writerows(unique)
    queries = sum(1 for row in unique if row["kind"] == "query")
    setters = len(unique) - queries
    print(f"total={len(unique)} queries={queries} setters={setters}")
    print(f"written {OUT}")
    domains: dict[str, int] = {}
    for row in unique:
        if row["kind"] == "setter":
            domains[row["domain"]] = domains.get(row["domain"], 0) + 1
    for domain, count in sorted(domains.items(), key=lambda item: -item[1]):
        print(f"  {domain}: {count} setters")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
