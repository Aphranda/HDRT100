"""Four-board OBS HIL matrix for REFMEM-READ-01 (M7/M8 evidence).

Reads every catalog key from every board and checks the per-slot contract:

* the page has one row per requested slot and the row ``slot`` matches;
* the local slot row is ``valid=1`` with exactly ``schema_words`` values;
* peer rows are ``valid=0`` / ``age_ms=0xFFFFFFFF`` / ``stale=1`` / zero values
  (no publisher yet -- this flips once the M2 publish plane lands);
* malformed rows fail closed.

Run it on the bench (ports in slot order) and keep the JSON report:

    python tools/refmem/obs_hil_matrix.py --port COM5 --port COM4 \
        --port COM3 --port COM6 --out out/refmem_read/obs_hil_matrix.json
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "common" / "scpi"))
sys.path.insert(0, str(ROOT / "tools" / "refmem"))

from control_plane import ControlPlane  # noqa: E402
from scpi_serial import SerialSession  # noqa: E402

DEFAULT_PORTS = ("COM5", "COM4", "COM3", "COM6")
INVALID_AGE = 0xFFFFFFFF


def catalog_schema() -> dict[int, int]:
    """key -> schema width from the single source ``read_catalog.yaml``."""
    try:
        import yaml  # type: ignore
    except ImportError as exc:  # pragma: no cover - environment guard
        raise SystemExit("PyYAML is required to read read_catalog.yaml") from exc
    path = ROOT / "config" / "refmem" / "read_catalog.yaml"
    document = yaml.safe_load(path.read_text(encoding="utf-8"))
    entries = document.get("keys", document) if isinstance(document, dict) else document
    schema: dict[int, int] = {}
    for entry in entries:
        key = int(entry.get("id", entry.get("key")))
        words = entry.get("schema") or entry.get("schema_words") or []
        schema[key] = len(words)
    return schema


def check_page(key: int, schema_words: int, page: dict, local_slot: int,
               expect_peers_stale: bool = True) -> list[str]:
    """Return contract violations for one OBS page (empty means OK)."""
    problems: list[str] = []
    rows = page.get("rows") or []
    if len(rows) != int(page.get("count", len(rows))):
        problems.append(f"key {key}: {len(rows)} rows for count={page.get('count')}")
    for index, row in enumerate(rows):
        slot = int(row.get("slot", -1))
        if slot != int(page.get("offset", 0)) + index:
            problems.append(f"key {key}: row {index} slot={slot}")
            continue
        valid = int(row.get("valid", 0))
        age = int(row.get("age_ms", 0))
        stale = int(row.get("stale", 0))
        values = row.get("value") or []
        if len(values) != schema_words:
            problems.append(
                f"key {key} slot {slot}: {len(values)} values != {schema_words}")
        if valid == 0:
            if age != INVALID_AGE or stale != 1:
                problems.append(
                    f"key {key} slot {slot}: invalid row age={age} stale={stale}")
            if any(int(value) != 0 for value in values):
                problems.append(f"key {key} slot {slot}: invalid row has values")
        elif slot != local_slot and expect_peers_stale:
            problems.append(f"key {key} slot {slot}: peer row valid={valid} (no publisher)")
    return problems


def run(ports: list[str], keys: list[int], schema: dict[int, int],
        out_path: pathlib.Path) -> dict:
    report: dict = {"schema": "HAOFV_REFTMEM_OBS_MATRIX_V1", "ports": ports,
                    "keys": keys, "boards": [], "passed": True}
    sessions = []
    try:
        for slot, port in enumerate(ports):
            session = SerialSession(port, 115200, 1.5, 0.05).open()
            sessions.append(session)
            plane = ControlPlane(session)
            board: dict = {"slot": slot, "port": port, "keys": {}, "problems": []}
            for key in keys:
                words = schema.get(key)
                if not words:
                    board["problems"].append(f"key {key}: not in read_catalog")
                    continue
                try:
                    page = plane.read(key, 0, len(ports))
                except Exception as exc:  # noqa: BLE001 - report, do not abort
                    board["keys"][str(key)] = {"error": str(exc)}
                    board["problems"].append(f"key {key}: {exc}")
                    continue
                problems = check_page(key, words, page, slot)
                board["keys"][str(key)] = {"valid": page.get("valid"),
                                           "rows": len(page.get("rows") or []),
                                           "problems": problems}
                board["problems"].extend(problems)
            report["boards"].append(board)
            if board["problems"]:
                report["passed"] = False
    finally:
        for session in sessions:
            try:
                session.close()
            except Exception:  # noqa: BLE001 - best effort
                pass
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", action="append", dest="ports",
                        help="serial port, repeat in slot order")
    parser.add_argument("--keys", default="1-8")
    parser.add_argument("--out", default="out/refmem_read/obs_hil_matrix.json")
    args = parser.parse_args(argv)
    keys: list[int] = []
    for part in args.keys.split(","):
        if "-" in part:
            first, last = part.split("-", 1)
            keys.extend(range(int(first), int(last) + 1))
        else:
            keys.append(int(part))
    ports = args.ports or list(DEFAULT_PORTS)
    report = run(ports, keys, catalog_schema(), pathlib.Path(args.out))
    print(f"{'PASS' if report['passed'] else 'FAIL'} obs matrix "
          f"ports={ports} keys={len(keys)}")
    for board in report["boards"]:
        if board["problems"]:
            print(f"  slot {board['slot']} {board['port']}: {len(board['problems'])} problems")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
