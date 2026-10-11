#!/usr/bin/env python3
"""Seat acceptance: read-only bench judgement plus a cheap device-build guard.

Bench side: evaluates the first-class seat against the acceptance matrix in
docs/tdma/TDMA_SEAT_ACCEPTANCE_REVIEW.md using nothing but SCPI *queries* — the
tool never sends STOP/START/config, so it cannot disturb a bench session owned by
somebody else. Parse a saved capture with --from-json, or run the built-in cases
with --self-test, both without any hardware attached.

Build side: `--device-syntax` re-compiles the seat-carrying translation units for
the target with the exact flags of the last configure (`compile_commands.json`)
and `-fsyntax-only`. That closes the gap TDMA-PROGRESS-20261011-025 hit: host
tests are blind to missing standard headers, struct members that do not exist and
scratch-budget static asserts, all of which the target build catches.

Modes are explicit: there is no fallback that talks to a port by accident.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

# Sources the device-syntax guard covers: every file carrying first-class seat
# facts or the seat's SCPI surface.
DEVICE_SOURCES = (
    "components/tdma/src/tdma_priority_rx.c",
    "components/tdma/src/tdma_pio_spi_ring_adapter.c",
    "components/tdma/src/tdma_seat_admission.c",
    "components/tdma/src/tdma_seat_mount_store.c",
    "components/distributed_refmem/src/distributed_refmem.c",
    "middleware/scpi_port/src/scpi_system_snapshot_commands.c",
    "components/vdc_dpll_manager/src/vdc_dpll_manager.c",
    # The consumer side carries the end-to-end loss facts the seat verdict reads
    # (freeze abandonment, freeze-missing-local, late arrivals).
    "components/sync_trigger/src/trigger_t2_runtime.c",
)

HEALTH = "SYSTem:TDMA:SEAT:HEALth?"
COUNTERS = "SYSTem:TDMA:SEAT:COUNters?"
ROTATION = "SYSTem:TDMA:SEAT:ROTation?"
POSITION = "SYSTem:REFMEM:SYNC:FLIGHT:POSition?"
SCHEDULE = "SYSTem:TDMA:SCHEDule?"

# Read-only query set, in the order a capture records them.
QUERIES = (HEALTH, COUNTERS, ROTATION, POSITION, SCHEDULE)

# Field counts of the pages this tool reads. A mismatch means the firmware page
# and the documented contract drifted, so every positional read after it would be
# wrong: refuse to interpret rather than guess.
EXPECTED_FIELDS = {HEALTH: 24, COUNTERS: 12, ROTATION: 11, POSITION: 31}

# Field positions of SYSTem:TDMA:SEAT:ROTation?, which reports the declared
# limits the counters above must be read against.
ROTATION_PRIORITY_SHARE = 0
ROTATION_FIRST_CLASS_SHARE = 1
ROTATION_CYCLE = 4
ROTATION_WAIT_BUDGET = 7
ROTATION_HOLD_DEPTH = 8
ROTATION_HOLD_ATTEMPTS = 9
ROTATION_SCHEDULE_ENABLED = 10

# Reason bits of SYSTem:TDMA:SEAT:HEALth? (tdma_pio_spi_ring_adapter.h).
REASON_BITS = (
    (0, "HOLD_DROPPED", "refused first-class record dropped (queue full)"),
    (1, "HOLD_EXPIRED", "held record retired on the retry budget"),
    (2, "ACK_OVERRIDE", "shared control bytes overridden by the first-class fact"),
    (3, "WAIT_VIOLATION", "first-class service wait budget exceeded"),
    (4, "LANE_REJECT", "priority lane refused a first-class record"),
    (5, "RETIRED_WAITING", "first-class fact retired before boarding"),
    (6, "FIRST_CLASS_STARVED", "seat window missing from the loop plan"),
    (7, "DECODE_REJECT", "validated frame whose slice did not decode"),
    (8, "FIRST_CLASS_MISSED", "first-class position lost end to end"),
)

# Which health-page counter(s) must be non-zero when a reason bit is set, so the
# verdict and its own evidence cannot disagree. A tuple means the bit folds
# several raw facts and any one of them is enough.
REASON_EVIDENCE = {
    "HOLD_DROPPED": (18,),
    "HOLD_EXPIRED": (17,),
    "ACK_OVERRIDE": (19,),
    "WAIT_VIOLATION": (6,),
    "LANE_REJECT": (11,),
    "RETIRED_WAITING": (8,),
    "FIRST_CLASS_STARVED": (9,),
    "DECODE_REJECT": (21,),
    "FIRST_CLASS_MISSED": (22, 23),
}


def decode_reason(mask: int) -> list[str]:
    return [f"{name}: {text}" for bit, name, text in REASON_BITS if mask & (1 << bit)]


def evaluate(pages: dict[str, list[int]]) -> dict:
    """Judge one board's capture. `pages` maps query text to its integer fields."""
    checks: list[dict] = []

    def check(item: str, passed: bool, detail: str) -> None:
        checks.append({"item": item, "status": "PASS" if passed else "FAIL",
                       "detail": detail})

    for query, expected in EXPECTED_FIELDS.items():
        fields = pages.get(query)
        ok = fields is not None and len(fields) == expected
        check(f"page-shape {query}", ok,
              f"{len(fields) if fields is not None else 'missing'} fields, expected {expected}")

    health = pages.get(HEALTH) or []
    if len(health) != EXPECTED_FIELDS[HEALTH]:
        check("verdict page readable", False,
              "SYSTem:TDMA:SEAT:HEALth? is empty or refused. It requires a STARTED "
              "priority lane, so a capture taken after STOP cannot carry a verdict: "
              "capture while the ring runs (SEAT:COUNters? stays readable while stopped)")
    if len(health) != EXPECTED_FIELDS[HEALTH]:
        return {"passed": False, "checks": checks, "reason_mask": None,
                "reasons": [], "verdict_ok": None}
    ok_field, reason = health[0], health[1]
    reasons = decode_reason(reason)
    check("verdict fields", ok_field in (0, 1),
          f"ok={ok_field} (must be 0 or 1)")
    check("verdict consistent with reason bits", (ok_field == 1) == (reason == 0),
          f"ok={ok_field} reason_mask={reason}")
    check("seat healthy", ok_field == 1 and reason == 0,
          "all degradation bits clear" if reason == 0 else "; ".join(reasons) or f"mask={reason}")

    # Every set bit must have evidence in the same page, and no counter may be
    # set behind a clear bit (that would mean the verdict is not wired up).
    for bit, name, _text in REASON_BITS:
        indexes = REASON_EVIDENCE[name]
        counters = [health[index] if index < len(health) else None for index in indexes]
        evidence = None not in counters and any(counter > 0 for counter in counters)
        check(f"evidence {name}",
              None not in counters and (bool(reason & (1 << bit)) == evidence),
              f"reason bit {'set' if reason & (1 << bit) else 'clear'}, counters={counters}")

    rotation = pages.get(ROTATION) or []
    if len(rotation) == EXPECTED_FIELDS[ROTATION]:
        check("declared budgets readable",
              rotation[ROTATION_WAIT_BUDGET] > 0 and rotation[ROTATION_HOLD_DEPTH] > 0
              and rotation[ROTATION_HOLD_ATTEMPTS] > 0 and rotation[ROTATION_CYCLE] > 0,
              f"cycle={rotation[ROTATION_CYCLE]} wait_budget={rotation[ROTATION_WAIT_BUDGET]} "
              f"hold_depth={rotation[ROTATION_HOLD_DEPTH]} "
              f"hold_attempts={rotation[ROTATION_HOLD_ATTEMPTS]}")
        if counters_health_ok(health):
            # The window gate is compiled out when the schedule flag is 0, so no
            # starvation can be recorded: a non-zero counter there means the page
            # and the firmware disagree about the build.
            starved = health[9] + health[10] if len(health) > 10 else 0
            check("starvation implies the seat-window schedule",
                  rotation[ROTATION_SCHEDULE_ENABLED] != 0 or starved == 0,
                  f"schedule_enabled={rotation[ROTATION_SCHEDULE_ENABLED]} "
                  f"starved counters={starved}")

    counters = pages.get(COUNTERS) or []
    if len(counters) == EXPECTED_FIELDS[COUNTERS]:
        check("starvation evidence", counters[9] == 0 and counters[10] == 0,
              f"seat_starved={counters[9]} first_class_starved={counters[10]} "
              "(the seat-window gate is disabled by default)")

    return {"passed": all(entry["status"] == "PASS" for entry in checks),
            "checks": checks, "reason_mask": reason, "reasons": reasons,
            "verdict_ok": ok_field}


def counters_health_ok(health: list[int]) -> bool:
    """True when the health page has the fields the cross-checks index."""
    return len(health) > 10


def healthy_capture() -> dict:
    health = [0] * EXPECTED_FIELDS[HEALTH]
    health[0], health[1] = 1, 0
    counters = [0] * EXPECTED_FIELDS[COUNTERS]
    rotation = [0] * EXPECTED_FIELDS[ROTATION]
    rotation[ROTATION_CYCLE] = 2
    rotation[ROTATION_WAIT_BUDGET] = 8
    rotation[ROTATION_HOLD_DEPTH] = 4
    rotation[ROTATION_HOLD_ATTEMPTS] = 4
    rotation[ROTATION_SCHEDULE_ENABLED] = 0
    position = [0] * EXPECTED_FIELDS[POSITION]
    schedule = [0] * 96
    return {HEALTH: health, COUNTERS: counters, ROTATION: rotation,
            POSITION: position, SCHEDULE: schedule}


def self_test() -> dict:
    """Built-in cases: a healthy board, every reason bit in turn, and the two
    ways a capture can be malformed. No hardware and no serial library needed."""
    results = []
    cases = [("healthy", healthy_capture(), True)]
    for _bit, name, _text in REASON_BITS:
        capture = healthy_capture()
        index = REASON_EVIDENCE[name]
        capture[HEALTH][1] = 1 << _bit
        capture[HEALTH][0] = 0
        capture[HEALTH][index[0]] = 1
        cases.append((f"reason {name}", capture, False))
    starved = healthy_capture()
    starved[COUNTERS][9], starved[COUNTERS][10] = 3, 2
    cases.append(("starvation evidence", starved, False))
    short = healthy_capture()
    short[HEALTH] = short[HEALTH][:-1]
    cases.append(("short health page", short, False))
    mismatched = healthy_capture()
    mismatched[HEALTH][1] = 1 << 4          # LANE_REJECT set...
    cases.append(("bit set without counter", mismatched, False))

    for name, capture, expected in cases:
        verdict = evaluate(capture)
        results.append({"case": name, "expected_pass": expected,
                        "passed": verdict["passed"],
                        "reasons": verdict["reasons"],
                        "ok": verdict["passed"] == expected})
    return {"cases": results, "passed": all(entry["ok"] for entry in results)}


def capture_board(port: str, baud: int, timeout_s: float) -> dict:
    """Read-only capture: queries only, in QUERIES order."""
    from tools.common.scpi.scpi_serial import SerialSession  # lazy: self-test needs no pyserial

    pages: dict[str, list[int]] = {}
    with SerialSession(port, baud, timeout_s, 0.05, read_timeout_s=0.02).open() as session:
        for query in QUERIES:
            try:
                raw = session.execute(query)
            except Exception as error:            # a page the firmware refused
                # SEAT:HEALth? requires a started lane (no data beats a zero that
                # looks healthy), so a stopped ring refuses it. Record the refusal
                # instead of aborting the whole capture: the other pages are still
                # evidence, and evaluate() explains what is missing.
                pages[query] = []
                pages.setdefault("_refused", {})[query] = f"{type(error).__name__}"
                continue
            try:
                pages[query] = [int(field.strip().strip('"'), 0) for field in raw.split(",")]
            except ValueError:
                pages[query] = []
    return pages


def write_bundle(out_dir: Path, boards: dict[str, dict]) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    summary = {"tool": "seat_acceptance", "read_only": True,
               "generated": datetime.now().isoformat(timespec="seconds"),
               "boards": {}}
    lines = ["# 一等席验收（只读）", "",
             f"- 生成时间：{summary['generated']}",
             "- 本工具只发查询命令，不发 STOP/START/配置。", ""]
    for port, result in boards.items():
        summary["boards"][port] = result
        lines.append(f"## {port}")
        lines.append("")
        lines.append(f"- 结论：{'PASS' if result['passed'] else 'FAIL'}"
                     f"（ok={result.get('verdict_ok')} reason_mask={result.get('reason_mask')}）")
        for entry in result["checks"]:
            if entry["status"] != "PASS":
                lines.append(f"- {entry['status']} {entry['item']}: {entry['detail']}")
        if result.get("reasons"):
            lines.append("- 退化原因：" + "；".join(result["reasons"]))
        lines.append("")
    summary["passed"] = bool(boards) and all(entry["passed"] for entry in boards.values())
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2),
                                          encoding="utf-8")
    (out_dir / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return out_dir


def find_compile_database(build_dir: Path | None) -> Path | None:
    """Locate a configure's compile_commands.json: an explicit build dir first,
    else the newest one under out/build."""
    if build_dir is not None:
        candidate = build_dir / "compile_commands.json"
        return candidate if candidate.exists() else None
    candidates = sorted((ROOT / "out" / "build").glob("*/compile_commands.json"),
                        key=lambda path: path.stat().st_mtime, reverse=True)
    return candidates[0] if candidates else None


def syntax_command(entry: dict, source: str) -> list[str] | None:
    """Turn one compile-database entry into a -fsyntax-only command for `source`,
    dropping the output/object bookkeeping flags. Pure, so it is unit-tested.

    Windows paths must survive parsing: an `arguments` array is used verbatim, and
    a `command` string is split without POSIX escaping (which would eat the
    backslashes of `C:\\...\\arm-none-eabi-gcc.exe`)."""
    if not entry:
        return None
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and arguments:
        words = [str(word) for word in arguments]
    else:
        command = entry.get("command") or ""
        if not command:
            return None
        # posix=False keeps Windows path separators; the only escaping CMake adds
        # is \" inside macro values, which the shell would strip for us. A token
        # that is entirely quoted is one path with spaces, so drop its quotes.
        words = []
        for word in shlex.split(command, posix=False):
            word = word.replace('\\"', '"')
            if len(word) >= 2 and word[0] == '"' and word[-1] == '"':
                word = word[1:-1]
            words.append(word)
    if not words:
        return None
    result: list[str] = []
    skip_next = False
    for word in words:
        if skip_next:
            skip_next = False
            continue
        if word in ("-o", "-MF", "-MT", "-MQ"):
            skip_next = True
            continue
        if word == "-c" or word.startswith("-o") and word != "-O0" and len(word) > 2:
            continue
        if word in ("-MF", "-MT"):
            continue
        result.append(word)
    if not any(word == str(entry.get("file")) or word.endswith(source) for word in result):
        result.append(source)
    result.append("-fsyntax-only")
    return result


def device_syntax_check(build_dir: Path | None = None,
                        sources: tuple[str, ...] = DEVICE_SOURCES) -> dict:
    """Compile the seat-carrying translation units for the target, syntax only."""
    database = find_compile_database(build_dir)
    if database is None:
        return {"passed": False, "database": None,
                "errors": ["no compile_commands.json; run the canonical device build first "
                           "(python tools/cmake_build_auto/cmake_build_auto.py)"]}
    entries = json.loads(database.read_text(encoding="utf-8"))
    by_source = {}
    for entry in entries:
        path = str(entry.get("file", "")).replace("\\", "/")
        for source in sources:
            if path.endswith(source):
                by_source[source] = entry
    results = []
    for source in sources:
        entry = by_source.get(source)
        if entry is None:
            results.append({"source": source, "status": "SKIP",
                            "detail": "not in the compile database"})
            continue
        command = syntax_command(entry, source)
        try:
            run = subprocess.run(command, cwd=entry.get("directory", str(ROOT)),
                                 capture_output=True, text=True, timeout=300)
            failed = run.returncode != 0
            detail = "" if not failed else (run.stderr or run.stdout).strip().splitlines()[-1:]
        except OSError as error:                     # missing compiler or bad cwd
            failed, detail = True, [f"{type(error).__name__}: {error} ({command[0]})"]
        results.append({"source": source,
                        "status": "FAIL" if failed else "PASS",
                        "detail": detail})
    return {"passed": all(entry["status"] != "FAIL" for entry in results),
            "database": str(database), "results": results}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="First-class seat acceptance: read-only bench queries, or the "
                    "device-syntax guard for the seat-carrying sources.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--port", action="append", metavar="COMx",
                      help="board serial port to query (repeatable)")
    mode.add_argument("--from-json", type=Path,
                      help="evaluate a previously saved capture: {port: {query: [ints]}}")
    mode.add_argument("--self-test", action="store_true",
                      help="run the built-in evaluator cases without hardware")
    mode.add_argument("--device-syntax", action="store_true",
                      help="syntax-compile the seat sources for the target (no hardware)")
    parser.add_argument("--build-dir", type=Path,
                        help="build tree holding compile_commands.json (default: newest)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout-s", type=float, default=3.0)
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "out" / "HardwareAcceptance" /
                        datetime.now().strftime("%Y%m%d") /
                        ("seat-acceptance-" + datetime.now().strftime("%H%M%S")))
    parser.add_argument("--json", action="store_true", help="print the result as JSON")
    args = parser.parse_args(argv)

    if args.self_test:
        result = self_test()
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if result["passed"] else 1

    if args.device_syntax:
        result = device_syntax_check(args.build_dir)
        if args.json:
            print(json.dumps(result, ensure_ascii=False, indent=2))
        else:
            if result.get("database"):
                print(f"compile database: {result['database']}")
            for entry in result.get("results", []):
                print(f"{entry['status']:4} {entry['source']}"
                      + (f"  {entry['detail']}" if entry["detail"] else ""))
            for error in result.get("errors", []):
                print(f"ERROR {error}")
        return 0 if result["passed"] else 1

    if args.from_json:
        raw = json.loads(args.from_json.read_text(encoding="utf-8"))
        boards = {port: evaluate(pages) for port, pages in raw.items()}
    else:
        boards = {port: evaluate(capture_board(port, args.baud, args.timeout_s))
                  for port in args.port}

    out_dir = write_bundle(args.out_dir, boards)
    passed = all(entry["passed"] for entry in boards.values())
    if args.json:
        print(json.dumps({"passed": passed, "boards": boards}, ensure_ascii=False, indent=2))
    else:
        for port, entry in boards.items():
            print(f"{port}: {'PASS' if entry['passed'] else 'FAIL'} "
                  f"ok={entry.get('verdict_ok')} reason_mask={entry.get('reason_mask')}")
            for reason in entry.get("reasons", []):
                print(f"    {reason}")
        print(f"evidence: {out_dir}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
