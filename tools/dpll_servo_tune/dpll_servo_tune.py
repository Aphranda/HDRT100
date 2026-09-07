#!/usr/bin/env python3
"""Closed-loop debug tuning of the NO1..NO4 VDC DPLL servo profile.

The tool writes only the debug SCPI coefficient mailbox.  It then reads the
internal DPLL vector/readiness from every in-ring node, scores the observed
residual, frequency correction and reject count, and keeps or reverts each
small candidate step.  NO5 is intentionally excluded because it is an
external observer, not a control participant.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.dpll_vdc_monitor.dpll_vdc_monitor import (  # noqa: E402
    DPLL_VECTOR_FIELDS,
    READINESS_FIELDS,
    parse_named_int_response,
    parse_vector_response,
)
from tools.scpi_common.scpi_serial import (  # noqa: E402
    open_serial_port,
    read_scpi_response,
)

COEFFICIENT_QUERY = "SYSTem:SYNC:VDC:DPLL:COEFficient?"
TUNE_COMMAND = "SYSTem:SYNC:VDC:DPLL:TUNE"
DPLL_VECTOR_QUERY = "SYSTem:REFMEM:DPLL:VECtor?"
READINESS_QUERY = "SYSTem:SYNC:VDC:LOCK:READiness?"
FILTER_QUERY = "SYSTem:SYNC:VDC:DPLL:FILTer?"
FILTER_FIELDS = (
    "state", "update_seq", "phase_error_ns", "frequency_error_ppb",
    "integrator_ppb", "period_adjust_ppb", "good_samples", "reject_count",
)


@dataclass(frozen=True)
class Board:
    name: str
    port: str


@dataclass(frozen=True)
class Profile:
    kp_q16: int
    ki_q16: int
    update_period_us: int
    step_threshold_ns: int
    sanity_freq_limit_ppb: int

    def command(self) -> str:
        return (f"{TUNE_COMMAND} {self.kp_q16},{self.ki_q16},"
                f"{self.update_period_us},{self.step_threshold_ns},"
                f"{self.sanity_freq_limit_ppb}")


def parse_board(value: str) -> Board:
    if "=" not in value:
        raise ValueError("board must be NO1..NO4=PORT")
    name, port = (item.strip() for item in value.split("=", 1))
    name = name.upper()
    if name not in {f"NO{i}" for i in range(1, 5)} or not port:
        raise ValueError("board must be NO1..NO4=PORT")
    return Board(name, port)


def command(ser: Any, text: str, timeout: float) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, text, timeout, require_match=True)


def parse_profile(response: str) -> Profile:
    fields = [item.strip().strip('"') for item in next(csv.reader([response]), [])]
    if len(fields) != 9:
        raise ValueError(f"invalid DPLL coefficient response: {response!r}")
    return Profile(*(int(item, 0) for item in fields[:5]))


def read_board(ser: Any, timeout: float) -> dict[str, Any]:
    vector = parse_vector_response(command(ser, DPLL_VECTOR_QUERY, timeout),
                                   DPLL_VECTOR_FIELDS)
    readiness = parse_named_int_response(
        command(ser, READINESS_QUERY, timeout), READINESS_FIELDS)
    filter_state = parse_named_int_response(
        command(ser, FILTER_QUERY, timeout), FILTER_FIELDS)
    return {"vector": vector, "readiness": readiness,
            "filter": filter_state}


def score(samples: dict[str, dict[str, Any]]) -> dict[str, Any]:
    rows = []
    for name, sample in sorted(samples.items()):
        vector = sample["vector"]
        readiness = sample["readiness"]
        filter_state = sample["filter"]
        phase = abs(int(filter_state.get("phase_error_ns",
                                         vector.get("last_phase_error_ns", 0))))
        frequency = abs(int(filter_state.get(
            "frequency_error_ppb", vector.get("last_frequency_error_ppb", 0))))
        reject = int(vector.get("gate_reject_code", 0))
        state = int(filter_state.get("state", vector.get("state", 0)))
        rows.append({"board": name, "phase_abs_ns": phase,
                     "frequency_abs_ppb": frequency, "reject_code": reject,
                     "state": state,
                     "integrator_ppb": int(filter_state.get("integrator_ppb", 0)),
                     "period_adjust_ppb": int(filter_state.get("period_adjust_ppb", 0)),
                     "input_ready": int(readiness.get("input_ready", 0)),
                     "locked": int(readiness.get("locked", 0))})
    # The state is diagnostic evidence only.  A lower residual dominates;
    # rejects and large frequency corrections break ties.
    value = sum(row["phase_abs_ns"] for row in rows)
    value += sum(row["frequency_abs_ppb"] // 10 for row in rows)
    value += sum(row["reject_code"] != 0 for row in rows) * 100000
    value += sum(row["input_ready"] == 0 for row in rows) * 100000
    return {"value": value, "boards": rows,
            "all_locked": bool(rows) and all(row["locked"] for row in rows)}


def parse_int_list(value: str) -> list[int]:
    return [int(item.strip(), 0) for item in value.split(",") if item.strip()]


def tune(args: argparse.Namespace) -> dict[str, Any]:
    boards = [parse_board(value) for value in args.board]
    if len({board.name for board in boards}) != len(boards):
        raise ValueError("duplicate board name")
    if args.rounds <= 0 or args.settle_s < 0:
        raise ValueError("rounds must be positive and settle_s non-negative")
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    events: list[dict[str, Any]] = []

    with __import__("contextlib").ExitStack() as stack:
        serials = {
            board.name: stack.enter_context(open_serial_port(
                board.port, args.baud, args.timeout, args.settle,
                read_timeout_s=args.serial_read_timeout))
            for board in boards
        }
        current = parse_profile(command(
            serials[boards[0].name], COEFFICIENT_QUERY, args.timeout))
        if args.kp_q16 is not None:
            current = Profile(args.kp_q16, current.ki_q16,
                              current.update_period_us,
                              current.step_threshold_ns,
                              current.sanity_freq_limit_ppb)
        if args.ki_q16 is not None:
            current = Profile(current.kp_q16, args.ki_q16,
                              current.update_period_us,
                              current.step_threshold_ns,
                              current.sanity_freq_limit_ppb)

        def observe(label: str) -> dict[str, Any]:
            if args.settle_s:
                time.sleep(args.settle_s)
            samples = {board.name: read_board(serials[board.name], args.timeout)
                       for board in boards}
            result = {"label": label, "profile": current.__dict__,
                      "score": score(samples), "raw": samples}
            events.append(result)
            return result

        for board in boards:
            response = command(serials[board.name], current.command(), args.timeout)
            events.append({"label": "apply_initial", "board": board.name,
                           "profile": current.__dict__, "response": response})
        baseline = observe("baseline")
        best_score = int(baseline["score"]["value"])
        accepted_profile = current
        accepted = 0
        for round_index in range(args.rounds):
            kp_step = args.kp_step if args.kp_step is not None else max(
                abs(accepted_profile.kp_q16) // 4, 1)
            ki_step = args.ki_step if args.ki_step is not None else max(
                abs(accepted_profile.ki_q16) // 4, 1)
            candidates = [
                Profile(accepted_profile.kp_q16 + kp_step, accepted_profile.ki_q16,
                        accepted_profile.update_period_us, accepted_profile.step_threshold_ns,
                        accepted_profile.sanity_freq_limit_ppb),
                Profile(accepted_profile.kp_q16 - kp_step, accepted_profile.ki_q16,
                        accepted_profile.update_period_us, accepted_profile.step_threshold_ns,
                        accepted_profile.sanity_freq_limit_ppb),
                Profile(accepted_profile.kp_q16, accepted_profile.ki_q16 + ki_step,
                        accepted_profile.update_period_us, accepted_profile.step_threshold_ns,
                        accepted_profile.sanity_freq_limit_ppb),
                Profile(accepted_profile.kp_q16, accepted_profile.ki_q16 - ki_step,
                        accepted_profile.update_period_us, accepted_profile.step_threshold_ns,
                        accepted_profile.sanity_freq_limit_ppb),
            ]
            round_results = []
            for candidate in candidates:
                for board in boards:
                    response = command(serials[board.name], candidate.command(),
                                       args.timeout)
                    events.append({"label": "apply_candidate", "round": round_index,
                                   "board": board.name, "profile": candidate.__dict__,
                                   "response": response})
                current = candidate
                round_results.append(observe(f"candidate_{round_index}"))
            selected = min(round_results,
                           key=lambda item: int(item["score"]["value"]))
            selected_score = int(selected["score"]["value"])
            if selected_score < best_score:
                accepted_profile = Profile(**selected["profile"])
                best_score = selected_score
                accepted += 1
                events.append({"label": "accept", "round": round_index,
                               "profile": accepted_profile.__dict__,
                               "score": selected["score"]})
            else:
                # Restore the last accepted profile after every failed probe.
                for board in boards:
                    command(serials[board.name], accepted_profile.command(), args.timeout)
                events.append({"label": "rollback", "round": round_index,
                               "profile": accepted_profile.__dict__, "score": best_score})

        current = accepted_profile
    result = {
        "schema": "HAOFV_DPLL_SERVO_TUNE_RUN_V1",
        "boards": [board.__dict__ for board in boards],
        "rounds": args.rounds,
        "accepted_steps": accepted,
        "selected_profile": current.__dict__,
        "best_score": best_score,
        "formal_lock_claim": False,
        "events": events,
        "source": "SCPI_DEBUG_TUNE_AND_INTERNAL_DPLL_VECTOR",
    }
    (out_dir / "tune.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", action="append", required=True,
                        metavar="NO1=COM3")
    parser.add_argument("--kp-q16", type=int)
    parser.add_argument("--ki-q16", type=int)
    parser.add_argument("--kp-step", type=int)
    parser.add_argument("--ki-step", type=int)
    parser.add_argument("--rounds", type=int, default=4)
    parser.add_argument("--settle-s", type=float, default=0.25)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--serial-read-timeout", type=float, default=0.1)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--out-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    try:
        print(json.dumps(tune(parse_args()), ensure_ascii=False, indent=2))
    except (OSError, RuntimeError, ValueError, TimeoutError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
