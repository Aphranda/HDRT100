"""Tracked long-window policy over the remaining native trace ancestry."""

from __future__ import annotations

from fractions import Fraction
import hashlib
import math
from pathlib import Path
import time
from types import SimpleNamespace

from tools.workflows.vdc_joint_capture.trace import load_trace


THRESHOLDS = (1_000_000_000, 1_500_000_000, 1_800_000_000,
              4_000_000_000, 8_000_000_000)
MAX_NS = 10_000_000_000
QUANTIZATION_NS = 1000
RUN_SECONDS = 11
TRIAL_SECONDS = 20


def require(condition, message):
    if not condition:
        raise ValueError(message)


def decision_oracle(record):
    """Recompute positive rational secants with explicit outward rounding."""
    expected_low, expected_high = (
        record["expected_delta_lo"], record["expected_delta_hi"])
    local_low, local_high = record["local_delta_lo"], record["local_delta_hi"]
    require(THRESHOLDS[0] <= expected_low <= expected_high <= MAX_NS and
            0 < local_low <= local_high <= MAX_NS,
            "Decision interval outside admitted positive bounds")
    error_low = math.floor(
        Fraction(local_low, expected_high) * 10**9 - 10**9)
    error_high = math.ceil(
        Fraction(local_high, expected_low) * 10**9 - 10**9)
    require((error_low, error_high) ==
            (record["error_lo_ppb"], record["error_hi_ppb"]),
            "Fraction secant differs from recorded error interval")
    step = (-(error_low // 4) if error_low > 10 else
            ((-error_high) // 4 if error_high < -10 else 0))
    step = max(-1000, min(1000, step))
    require(step == record["delta_ppb"],
            "Recorded step differs from existing decision criterion")
    before, after = record["before_seq"], record["after_seq"]
    require(before > 0 and record["model_token"] > 0,
            "Missing model/DCO identity")
    if after == before + 1:
        require(step != 0 and record["after_ppb"] == record["before_ppb"] + step,
                "Actual DCO apply is inconsistent")
        outcome = "applied"
    else:
        require(after == before and record["after_ppb"] == record["before_ppb"],
                "Unexplained DCO change")
        outcome = "rejected" if step else "no_adjust"
    scale = Fraction(10**9 + record["before_ppb"], 10**9)
    require(scale > 0, "Nonpositive DCO scale")
    width_bound = math.ceil((2 * QUANTIZATION_NS + 1) * scale) + 1
    require(local_high - local_low <= width_bound,
            "Local interval exceeds full quantization bound")
    return {
        "index": record["index"],
        "event_sequence": record["event_sequence"],
        "baseline_sequence": record["baseline_sequence"],
        "error_ppb": [error_low, error_high],
        "step_ppb": step,
        "outcome": outcome,
        "local_width_ns": local_high - local_low,
        "quantization_width_bound_ns": width_bound,
    }


def check_windows(records):
    groups, checks = {}, []
    previous_event = -1
    for record in records:
        event = record["event_sequence"]
        baseline = record["baseline_sequence"]
        require(event > previous_event and event > baseline,
                "Decision event rollback")
        previous_event = event
        checks.append(decision_oracle(record))
        key = (baseline, record["model_token"])
        group = groups.setdefault(key, [])
        require(len(group) < len(THRESHOLDS),
                "More than five decisions per baseline/model")
        if group:
            prior = group[-1]
            future = [value for value in THRESHOLDS
                      if value > prior["expected_delta_lo"]]
            require(bool(future) and record["expected_delta_lo"] >= future[0],
                    "Same baseline reused without a strictly later threshold")
            require(prior["delta_ppb"] == 0 and
                    prior["before_seq"] == prior["after_seq"],
                    "Prior decision did not retain its baseline")
            require(prior["error_lo_ppb"] <= 10 and
                    prior["error_hi_ppb"] >= -10 and
                    (prior["error_lo_ppb"] < -10 or
                     prior["error_hi_ppb"] > 10),
                    "Retained baseline was not an uncertain deadband overlap")
            require(record["before_seq"] == prior["after_seq"] and
                    record["before_ppb"] == prior["after_ppb"],
                    "Retained model changed")
        group.append(record)
    return {
        "decisions": checks,
        "baselines": [{
            "baseline_sequence": key[0],
            "model_token": key[1],
            "decision_count": len(values),
            "intervals_ns": [row["expected_delta_lo"] for row in values],
            "observed_interval_thresholds_ns": [
                max(threshold for threshold in THRESHOLDS
                    if threshold <= row["expected_delta_lo"])
                for row in values],
        } for key, values in groups.items()],
        "max_decisions_per_baseline": max(map(len, groups.values()), default=0),
        "observed_four_second_interval": any(
            row["expected_delta_lo"] >= THRESHOLDS[3] for row in records),
        "observed_eight_second_interval": any(
            row["expected_delta_lo"] >= THRESHOLDS[4] for row in records),
        "long_interval_required": False,
        "limit": (
            "Observed interval bins, not reconstructed next_evaluation history. "
            "A late sample may skip thresholds; consumed but unpublished preparations "
            "require host tests."),
    }


def check_last_projection(follower, status):
    tick_hz = status["tick_hz"]
    delta = follower["raw_lo"] - follower["baseline_raw"]
    require(tick_hz > 0 and 0 < delta <= 10 * tick_hz,
            "Last raw delta is outside ten-second bound")
    nominal = Fraction(delta * 10**9, tick_hz)
    scale = Fraction(10**9 + follower["before_ppb"], 10**9)
    require(scale > 0, "Nonpositive DCO scale")
    lower = math.floor(max(0, math.floor(nominal) - QUANTIZATION_NS) * scale)
    upper = math.ceil((math.ceil(nominal) + QUANTIZATION_NS) * scale)
    actual = (follower["local_interval_lo"], follower["local_interval_hi"])
    require(0 < lower <= actual[0] <= actual[1] <= upper,
            "Last local interval escapes independent correlated projection")
    return {
        "raw_delta_ticks": delta,
        "tick_hz": tick_hz,
        "raw_delta_seconds": float(Fraction(delta, tick_hz)),
        "nominal_delta_ns_fraction": [nominal.numerator, nominal.denominator],
        "independent_candidate_ns": [lower, upper],
        "actual_local_interval_ns": list(actual),
        "quantization_margin_each_endpoint_ns": QUANTIZATION_NS,
        "intersection_may_narrow": True,
        "scope": (
            "Final decision only; other decision records do not contain raw endpoints."),
    }


def load_longwindow(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked long-window layer over an admitted TraceProbe module."""
    parent = (runtime_module if runtime_module is not None else
              load_trace(runtime_entrypoint))
    if not hasattr(parent, "TraceProbe") or not hasattr(parent, "follow"):
        raise ValueError("Historical trace entrypoint lacks TraceProbe/follow")
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    class LongWindowProbe(parent.TraceProbe):
        def __init__(self, options):
            super().__init__(options)
            self.report["schema"] = "TYPED_LONGWINDOW_ELEVEN_SECOND_PROBE_V1"
            self.report["plan"].update(
                run_seconds=RUN_SECONDS,
                temporary_trial_seconds=TRIAL_SECONDS,
                native_typed_trace=True,
                trace_capture=True,
                diagnostic_gpio_output=False,
                window_thresholds_ns=list(THRESHOLDS),
                max_interval_ns=MAX_NS,
                require_long_interval=False,
                require_phase_lock=False,
                correlated_local_delta_quantization_ns=QUANTIZATION_NS,
                expected_capacity={
                    "approximate_matches": 55,
                    "approximate_decisions": 11,
                    "capacity": 76,
                    "dropped_records_are_failure": True,
                },
                scope=(
                    "Finite recorded decisions, endpoint DCO consistency and retained "
                    "baseline coverage; no physical lock claim"),
            )
            dependencies = [
                source, Path(parent.__file__), Path(parent.follow.__file__),
                Path(parent.follow.observer.__file__),
                Path(parent.follow.match.__file__),
                Path(parent.follow.match.direct.__file__),
                Path(parent.follow.parent.__file__),
                Path(parent.follow.parent.base.__file__), Path(parent.trace.__file__),
            ]
            known = {row["path"] for row in self.report["tool_sources"]}
            for path in dependencies:
                if str(path) not in known:
                    self.report["tool_sources"].append({
                        "path": str(path), "sha256": parent.follow.parent.base.sha(path)})
                    known.add(str(path))

        def acquire(self):
            self.block_queries = True
            self.barrier = False
            for board in reversed(self.boards):
                self.control(board, "SYSTem:TDMA:RING:START")
            time.sleep(0.2)
            epoch = int(time.time()) & 0xFFFFFFFF
            grant = int(self.command(
                self.boards[0],
                f"CALibration:ORIGin:TRIAL {epoch},8192,256,"
                f"{self.report['clock_hz'] * TRIAL_SECONDS}").strip('"'))
            require(grant > 0 and not grant & 1,
                    "Temporary trial admission failed")
            self.report["trial_epoch"] = grant
            self.report["quiet_wait_started_monotonic"] = time.monotonic()
            self.save()
            time.sleep(RUN_SECONDS)
            self.report["quiet_wait_elapsed_s"] = (
                time.monotonic() - self.report["quiet_wait_started_monotonic"])

        def collect(self):
            super().collect()
            for board in self.boards[1:]:
                row = self.report["boards"][board.address]
                try:
                    row["longwindow_check"] = check_windows(row["trace_decisions"])
                    row["correlated_last_decision"] = check_last_projection(
                        row["follow"], row["trace_stop_ack"]["status"])
                    row["longwindow_independent_checks_passed"] = True
                except Exception as exc:
                    self.error("longwindow-independent:" + board.address, exc)
                self.save()
            self.report["passed"] = not self.report["errors"]
            self.save()

    return SimpleNamespace(
        __file__=str(source),
        LongWindowProbe=LongWindowProbe,
        parent=parent,
        THRESHOLDS=THRESHOLDS,
        MAX_NS=MAX_NS,
        QUANTIZATION_NS=QUANTIZATION_NS,
        RUN_SECONDS=RUN_SECONDS,
        TRIAL_SECONDS=TRIAL_SECONDS,
        decision_oracle=decision_oracle,
        check_windows=check_windows,
        check_last_projection=check_last_projection,
        source_path=source,
        source_sha256=source_sha256,
    )
