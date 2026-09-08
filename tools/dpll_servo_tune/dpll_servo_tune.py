#!/usr/bin/env python3
"""Closed-loop debug tuning of the NO1..NO4 VDC DPLL servo profile.

The tool probes profiles through the debug SCPI coefficient mailbox, then
stores each completed round's verified best profile to Flash.  It reads the
internal DPLL vector/readiness from every in-ring node, scores the observed
residual, frequency correction and reject count, and keeps or reverts each
small candidate step.  NO5 is intentionally excluded because it is an
external observer, not a control participant.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
import time
from dataclasses import dataclass, replace
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
STORE_COMMAND = "SYSTem:SYNC:VDC:DPLL:STORe"
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


def response_is_ok(response: str) -> bool:
    """Accept both bare and detailed SCPI success responses."""
    fields = [item.strip().strip('"') for item in next(csv.reader([response]), [])]
    return bool(fields) and fields[0].upper() == "OK"


def response_is_unavailable(response: str) -> bool:
    fields = [item.strip().strip('"') for item in next(csv.reader([response]), [])]
    return bool(fields) and fields[0].upper() == "UNAVAILABLE"


def _persist_profile(serials: dict[str, Any], boards: list[Board],
                     profile: Profile, timeout: float,
                     events: list[dict[str, Any]], round_index: int) -> None:
    """Persist the accepted profile only after all nodes report it applied.

    Probe profiles are deliberately never stored.  The firmware-side STORE
    transaction performs its own Flash readback, while this preflight avoids
    persisting a pending mailbox value before Core1 has installed it.
    """
    verified: dict[str, dict[str, Any]] = {}
    for board in boards:
        response = command(serials[board.name], COEFFICIENT_QUERY, timeout)
        fields = [item.strip().strip('"')
                  for item in next(csv.reader([response]), [])]
        if len(fields) != 9:
            raise RuntimeError(
                f"{board.name}: invalid coefficient status before Flash store: "
                f"{response!r}")
        observed = parse_profile(response)
        requested_generation = int(fields[6], 0)
        applied_generation = int(fields[7], 0)
        pending = bool(int(fields[8], 0))
        if observed != profile or pending:
            raise RuntimeError(
                f"{board.name}: accepted profile is not applied before Flash "
                f"store (observed={observed}, requested={requested_generation}, "
                f"applied={applied_generation}, pending={pending})")
        verified[board.name] = {
            "profile": observed.__dict__,
            "requested_generation": requested_generation,
            "applied_generation": applied_generation,
        }

    for board in boards:
        response = command(serials[board.name], STORE_COMMAND, timeout)
        if not response_is_ok(response):
            raise RuntimeError(
                f"{board.name}: Flash store rejected: {response!r}")
        events.append({
            "label": "flash_store",
            "round": round_index,
            "board": board.name,
            "profile": profile.__dict__,
            "verified_runtime": verified[board.name],
            "response": response,
        })


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


def _apply_profile(serials: dict[str, Any], boards: list[Board],
                   profile: Profile, timeout: float, events: list[dict[str, Any]],
                   label: str, round_index: int | None = None,
                   retry_count: int = 5, retry_delay_s: float = 0.1) -> None:
    """Write one runtime mailbox profile to every in-ring board."""
    for board in boards:
        last_error: RuntimeError | None = None
        response = ""
        for attempt in range(retry_count + 1):
            try:
                response = command(serials[board.name], profile.command(), timeout)
                if response_is_ok(response):
                    break
                if not response_is_unavailable(response):
                    raise RuntimeError(
                        f"{board.name}: runtime profile rejected: {response!r}")
                last_error = RuntimeError(
                    f"{board.name}: runtime profile unavailable: {response!r}")
            except RuntimeError as exc:
                last_error = exc
            if attempt < retry_count:
                time.sleep(retry_delay_s)
        else:
            raise RuntimeError(
                f"{board.name}: runtime profile unavailable after "
                f"{retry_count + 1} attempts") from last_error
        event: dict[str, Any] = {
            "label": label,
            "board": board.name,
            "profile": profile.__dict__,
            "response": response,
        }
        if attempt:
            event["attempt"] = attempt
        if round_index is not None:
            event["round"] = round_index
        events.append(event)


def _window_score(samples: list[dict[str, Any]], args: argparse.Namespace,
                  profile: Profile | None = None) -> dict[str, Any]:
    """Score a stable observation window instead of one SCPI snapshot.

    Phase RMS is the primary objective.  Frequency error and near-limit DCO
    corrections prevent a seemingly good instant from hiding an integrator or
    rate-limit problem.  The large safety penalty makes an unlocked/rejected
    candidate unsuitable for a descent step even in debug mode.
    """
    by_board: dict[str, list[dict[str, Any]]] = {}
    for sample in samples:
        for name, board_sample in sample["boards"].items():
            by_board.setdefault(name, []).append(board_sample)

    rows: list[dict[str, Any]] = []
    safe = bool(by_board)
    limit_ppb = (profile.sanity_freq_limit_ppb if profile is not None else
                 args.sanity_freq_limit_ppb)
    adjustment_limit = abs(int(limit_ppb)) * args.saturation_ratio
    for name, board_samples in sorted(by_board.items()):
        phase_values = [int(item["filter"].get(
            "phase_error_ns", item["vector"].get("last_phase_error_ns", 0)))
            for item in board_samples]
        frequency_values = [int(item["filter"].get(
            "frequency_error_ppb", item["vector"].get("last_frequency_error_ppb", 0)))
            for item in board_samples]
        adjustment_values = [int(item["filter"].get(
            "period_adjust_ppb", item["vector"].get("dco_period_adjust_ppb", 0)))
            for item in board_samples]
        phase_rms = math.sqrt(sum(value * value for value in phase_values) /
                              len(phase_values))
        frequency_rms = math.sqrt(sum(value * value for value in frequency_values) /
                                  len(frequency_values))
        adjustment_peak = max(abs(value) for value in adjustment_values)
        saturation_excess = max(0.0, adjustment_peak - adjustment_limit)
        rejected = any(
            int(item["vector"].get("gate_reject_code", 0)) != 0 or
            int(item["filter"].get("reject_count", 0)) != 0 or
            int(item["readiness"].get("rejected_count", 0)) != 0
            for item in board_samples)
        locked = all(int(item["readiness"].get("locked", 0)) == 1 and
                     int(item["filter"].get("state",
                                            item["vector"].get("state", 0))) == 5
                     for item in board_samples)
        input_ready = all(int(item["readiness"].get("input_ready", 0)) == 1
                          for item in board_samples)
        row_safe = locked and input_ready and not rejected and saturation_excess == 0
        row_loss = (phase_rms + args.frequency_weight * frequency_rms +
                    args.saturation_penalty * saturation_excess)
        if not (locked and input_ready and not rejected):
            row_loss += args.hard_failure_penalty
        rows.append({
            "board": name,
            "samples": len(board_samples),
            "phase_rms_ns": phase_rms,
            "frequency_rms_ppb": frequency_rms,
            "period_adjust_peak_ppb": adjustment_peak,
            "adjustment_limit_ppb": adjustment_limit,
            "saturation_excess_ppb": saturation_excess,
            "locked": locked,
            "input_ready": input_ready,
            "rejected": rejected,
            "safe": row_safe,
            "loss": row_loss,
        })
        safe = safe and row_safe
    objective = getattr(args, "objective", "sum")
    if objective == "worst-board":
        worst = max(rows, key=lambda row: float(row["loss"]), default=None)
        value = float(worst["loss"]) if worst is not None else 0.0
        worst_board = worst["board"] if worst is not None else None
    elif objective == "sum":
        value = sum(float(row["loss"]) for row in rows)
        worst_board = None
    else:
        raise ValueError(f"unsupported tuning objective: {objective!r}")
    return {"value": value, "objective": objective,
            "worst_board": worst_board, "boards": rows, "safe": safe,
            "sample_count": len(samples)}


def _observe_window(serials: dict[str, Any], boards: list[Board],
                    profile: Profile, args: argparse.Namespace,
                    out_dir: Path, label: str,
                    events: list[dict[str, Any]]) -> dict[str, Any]:
    if args.settle_s:
        time.sleep(args.settle_s)
    started = time.monotonic()
    samples: list[dict[str, Any]] = []
    while True:
        elapsed = time.monotonic() - started
        board_samples: dict[str, dict[str, Any]] = {}
        for board in boards:
            last_error: RuntimeError | None = None
            for attempt in range(args.scpi_read_retries + 1):
                try:
                    board_samples[board.name] = read_board(
                        serials[board.name], args.timeout)
                    if attempt:
                        events.append({"label": "scpi_read_recovered",
                                       "window": label, "board": board.name,
                                       "attempt": attempt})
                    break
                except (OSError, RuntimeError, TimeoutError, ValueError) as exc:
                    last_error = exc
                    if attempt < args.scpi_read_retries:
                        time.sleep(args.scpi_retry_delay_s)
            else:
                raise RuntimeError(
                    f"{label}:{board.name}: SCPI read unavailable after "
                    f"{args.scpi_read_retries + 1} attempts") from last_error
        samples.append({
            "elapsed_s": elapsed,
            "boards": board_samples,
        })
        if time.monotonic() - started >= args.observation_s:
            break
        time.sleep(max(0.0, args.sample_interval_s))
    trace_path = out_dir / f"{label}_samples.json"
    trace_path.write_text(json.dumps({
        "schema": "HAOFV_DPLL_TUNE_WINDOW_V1",
        "profile": profile.__dict__,
        "duration_s": time.monotonic() - started,
        "samples": samples,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    result = {
        "label": label,
        "profile": profile.__dict__,
        "samples_path": str(trace_path),
        "score": _window_score(samples, args, profile),
    }
    events.append(result)
    return result


def _project_profile(profile: Profile, kp_q16: float, ki_q16: float,
                     args: argparse.Namespace) -> Profile:
    return replace(
        profile,
        kp_q16=max(args.kp_min, min(args.kp_max, int(round(kp_q16)))),
        ki_q16=max(args.ki_min, min(args.ki_max, int(round(ki_q16)))),
    )


def _gradient_candidate(profile: Profile, kp_gradient: float,
                        ki_gradient: float, args: argparse.Namespace) -> Profile:
    """Scale finite-difference gradients by their probe steps before descent."""
    scaled_kp = kp_gradient * args.gradient_kp_probe
    scaled_ki = ki_gradient * args.gradient_ki_probe
    magnitude = max(abs(scaled_kp), abs(scaled_ki), 1.0)
    return _project_profile(
        profile,
        profile.kp_q16 - args.gradient_kp_step * scaled_kp / magnitude,
        profile.ki_q16 - args.gradient_ki_step * scaled_ki / magnitude,
        args)


def _gradient_score_hard_safe(result: dict[str, Any]) -> bool:
    return all(row["locked"] and row["input_ready"] and not row["rejected"]
               for row in result["score"]["boards"])


def _saturation_excess(result: dict[str, Any]) -> float:
    return max((float(row["saturation_excess_ppb"])
                for row in result["score"]["boards"]), default=0.0)


def _select_improved_trial(
        best: dict[str, Any],
        trials: list[tuple[str, Profile, dict[str, Any]]],
        current: Profile) -> tuple[str, Profile, dict[str, Any]] | None:
    """Choose the best safe descent or probe result under the same gate."""
    current_saturation = _saturation_excess(best)
    eligible: list[tuple[str, Profile, dict[str, Any]]] = []
    for trial in trials:
        _, profile, result = trial
        saturation_ok = (
            result["score"]["safe"] if best["score"]["safe"] else
            _saturation_excess(result) <= current_saturation)
        if (profile != current and _gradient_score_hard_safe(result) and
                saturation_ok and
                float(result["score"]["value"]) < float(best["score"]["value"])):
            eligible.append(trial)
    return min(eligible, key=lambda trial: float(trial[2]["score"]["value"]),
               default=None)


def _finite_difference(plus: dict[str, Any], minus: dict[str, Any],
                       plus_value: int, minus_value: int) -> float:
    if plus_value == minus_value:
        return 0.0
    return ((float(plus["score"]["value"]) - float(minus["score"]["value"])) /
            (plus_value - minus_value))


def _gradient_tune(serials: dict[str, Any], boards: list[Board],
                   current: Profile, args: argparse.Namespace, out_dir: Path,
                   events: list[dict[str, Any]]) -> tuple[Profile, int, float]:
    """Run bounded finite-difference descent against windowed SCPI evidence."""
    _apply_profile(serials, boards, current, args.timeout, events, "apply_initial")
    accepted_profile = current
    try:
        baseline = _observe_window(serials, boards, current, args, out_dir,
                                   "baseline", events)
        best = baseline
        accepted = 0
        for round_index in range(args.rounds):
            probes = {
            "kp_plus": _project_profile(
                current, current.kp_q16 + args.gradient_kp_probe,
                current.ki_q16, args),
            "kp_minus": _project_profile(
                current, current.kp_q16 - args.gradient_kp_probe,
                current.ki_q16, args),
            "ki_plus": _project_profile(
                current, current.kp_q16,
                current.ki_q16 + args.gradient_ki_probe, args),
            "ki_minus": _project_profile(
                current, current.kp_q16,
                current.ki_q16 - args.gradient_ki_probe, args),
            }
            observations: dict[str, dict[str, Any]] = {}
            for direction, probe in probes.items():
                if probe == current:
                    # A bound may intentionally freeze one axis during a
                    # one-dimensional descent.  Its finite difference is
                    # defined as zero, so another identical window cannot
                    # add information and only lengthens the live tune.
                    observations[direction] = baseline
                    events.append({"label": "probe_fixed_axis",
                                   "round": round_index,
                                   "direction": direction,
                                   "profile": current.__dict__})
                    continue
                _apply_profile(serials, boards, probe, args.timeout, events,
                               f"probe_{direction}", round_index)
                observations[direction] = _observe_window(
                    serials, boards, probe, args, out_dir,
                    f"round_{round_index}_{direction}", events)

            kp_gradient = _finite_difference(
            observations["kp_plus"], observations["kp_minus"],
            probes["kp_plus"].kp_q16, probes["kp_minus"].kp_q16)
            ki_gradient = _finite_difference(
            observations["ki_plus"], observations["ki_minus"],
            probes["ki_plus"].ki_q16, probes["ki_minus"].ki_q16)
            candidate = _gradient_candidate(current, kp_gradient, ki_gradient, args)
            _apply_profile(serials, boards, candidate, args.timeout, events,
                           "apply_gradient_candidate", round_index)
            candidate_result = _observe_window(
                serials, boards, candidate, args, out_dir,
                f"round_{round_index}_candidate", events)
            current_saturation = _saturation_excess(best)
            candidate_saturation = _saturation_excess(candidate_result)
            usable = _gradient_score_hard_safe(candidate_result)
        # A currently saturated debug profile may descend only toward equal or
        # lower saturation.  Once the baseline is unsaturated, every accepted
        # profile must remain unsaturated as well.
            saturation_ok = (
                candidate_result["score"]["safe"] if best["score"]["safe"] else
                candidate_saturation <= current_saturation)
            improved = float(candidate_result["score"]["value"]) < float(
                best["score"]["value"])
            trials = [("gradient", candidate, candidate_result)]
            trials.extend((direction, probe, observations[direction])
                          for direction, probe in probes.items())
            selected_trial = _select_improved_trial(best, trials, current)
            events.append({
            "label": "gradient",
            "round": round_index,
            "kp_gradient": kp_gradient,
            "ki_gradient": ki_gradient,
            "candidate": candidate.__dict__,
            "candidate_usable": usable,
            "saturation_ok": saturation_ok,
            "baseline_saturation_excess_ppb": current_saturation,
            "candidate_saturation_excess_ppb": candidate_saturation,
            "selected_source": (selected_trial[0] if selected_trial else None),
            })
            if selected_trial is not None:
                source, selected_profile, selected_result = selected_trial
                current = selected_profile
                accepted_profile = current
                best = selected_result
                _apply_profile(serials, boards, current, args.timeout, events,
                               "apply_accepted", round_index)
                accepted += 1
                events.append({"label": "accept", "round": round_index,
                               "profile": current.__dict__,
                               "score": selected_result["score"],
                               "source": source})
            else:
                _apply_profile(serials, boards, current, args.timeout, events,
                               "rollback", round_index)
                events.append({"label": "rollback_reason", "round": round_index,
                               "profile": current.__dict__, "improved": improved,
                               "usable": usable, "saturation_ok": saturation_ok})
            if args.store_accepted:
                _persist_profile(serials, boards, current, args.timeout,
                                 events, round_index)
        return current, accepted, float(best["score"]["value"])
    except BaseException:
        _apply_profile(serials, boards, accepted_profile, args.timeout, events,
                       "exception_rollback")
        raise


def parse_int_list(value: str) -> list[int]:
    return [int(item.strip(), 0) for item in value.split(",") if item.strip()]


def tune(args: argparse.Namespace) -> dict[str, Any]:
    boards = [parse_board(value) for value in args.board]
    if len({board.name for board in boards}) != len(boards):
        raise ValueError("duplicate board name")
    if args.rounds <= 0 or args.settle_s < 0:
        raise ValueError("rounds must be positive and settle_s non-negative")
    if args.mode == "gradient" and (
            args.observation_s <= 0 or args.sample_interval_s < 0 or
            args.gradient_kp_probe <= 0 or args.gradient_ki_probe <= 0 or
            args.gradient_kp_step <= 0 or args.gradient_ki_step <= 0 or
            args.kp_min > args.kp_max or args.ki_min > args.ki_max or
            not 0.0 <= args.saturation_ratio <= 1.0):
        raise ValueError("invalid bounded gradient tuning arguments")
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

        if args.mode == "gradient":
            current, accepted, best_score = _gradient_tune(
                serials, boards, current, args, out_dir, events)
        else:
            _apply_profile(serials, boards, current, args.timeout, events,
                           "apply_initial")
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
                    _apply_profile(serials, boards, candidate, args.timeout, events,
                                   "apply_candidate", round_index)
                    current = candidate
                    round_results.append(observe(f"candidate_{round_index}"))
                selected = min(round_results,
                               key=lambda item: int(item["score"]["value"]))
                selected_score = int(selected["score"]["value"])
                if selected_score < best_score:
                    accepted_profile = Profile(**selected["profile"])
                    _apply_profile(serials, boards, accepted_profile, args.timeout,
                                   events, "apply_accepted", round_index)
                    best_score = selected_score
                    accepted += 1
                    events.append({"label": "accept", "round": round_index,
                                   "profile": accepted_profile.__dict__,
                                   "score": selected["score"]})
                else:
                    _apply_profile(serials, boards, accepted_profile, args.timeout,
                                   events, "rollback", round_index)
                    events.append({"label": "rollback", "round": round_index,
                                   "profile": accepted_profile.__dict__, "score": best_score})
                if args.store_accepted:
                    _persist_profile(serials, boards, accepted_profile,
                                     args.timeout, events, round_index)

            current = accepted_profile
    result = {
        "schema": "HAOFV_DPLL_SERVO_TUNE_RUN_V2",
        "boards": [board.__dict__ for board in boards],
        "rounds": args.rounds,
        "accepted_steps": accepted,
        "selected_profile": current.__dict__,
        "best_score": best_score,
        "mode": args.mode,
        "objective": args.objective,
        "flash_store_accepted": args.store_accepted,
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
    parser.add_argument("--mode", choices=("coordinate", "gradient"),
                        default="coordinate")
    parser.add_argument("--objective", choices=("worst-board", "sum"),
                        default="worst-board",
                        help="Minimize the worst NO1-NO4 loss, or aggregate loss.")
    parser.add_argument("--observation-s", type=float, default=5.0,
                        help="Gradient-mode SCPI observation window per probe.")
    parser.add_argument("--sample-interval-s", type=float, default=0.1,
                        help="Gradient-mode interval between window samples.")
    parser.add_argument("--gradient-kp-probe", type=int, default=512,
                        help="Finite-difference perturbation of Kp Q16.")
    parser.add_argument("--gradient-ki-probe", type=int, default=16,
                        help="Finite-difference perturbation of Ki Q16.")
    parser.add_argument("--gradient-kp-step", type=int, default=256,
                        help="Maximum projected descent step for Kp Q16.")
    parser.add_argument("--gradient-ki-step", type=int, default=8,
                        help="Maximum projected descent step for Ki Q16.")
    parser.add_argument("--kp-min", type=int, default=4096)
    parser.add_argument("--kp-max", type=int, default=24576)
    parser.add_argument("--ki-min", type=int, default=64)
    parser.add_argument("--ki-max", type=int, default=512)
    parser.add_argument("--frequency-weight", type=float, default=0.05,
                        help="Loss weight in ns per ppb RMS frequency error.")
    parser.add_argument("--saturation-ratio", type=float, default=0.8,
                        help="Period-adjust ratio that starts saturation penalty.")
    parser.add_argument("--saturation-penalty", type=float, default=10.0,
                        help="Loss weight in ns per ppb beyond the safe limit.")
    parser.add_argument("--hard-failure-penalty", type=float, default=1_000_000.0,
                        help="Loss penalty for unlock, reject, or input loss.")
    parser.add_argument("--scpi-read-retries", type=int, default=2,
                        help="Retries for a transient SCPI UNAVAILABLE response.")
    parser.add_argument("--scpi-retry-delay-s", type=float, default=0.1)
    parser.add_argument("--store-accepted", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Persist each completed round's verified best profile to Flash.")
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
