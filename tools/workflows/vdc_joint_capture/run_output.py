"""Tracked RUN output layer for the VDC joint child workflow."""

from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import re
from types import SimpleNamespace


PREFIX = "SYSTem:VDC:OUTPut:RUN"
TIMING_PREFIX = "SYSTem:VDC:OUTPut:TIMing"
FIELDS = (
    "schema generation state reason blocks edges source_retirements first_model "
    "last_model model_changes tick_hz transfer_count pio_enabled dma_busy session "
    "period_ns high_ns duration_ms delay_ns ring_config role_generation clock_epoch "
    "clock_run blocks_planned plan_rejects binding_rejects anchor_before anchor_after "
    "last_rising_tick last_falling_tick first_ordinal last_ordinal expires_tick "
    "last_local_ns last_target_vdc_ns maximum_bridge_width_ticks prepared_config arm_config "
    "start_pc program_offset start_raw_flags start_raw_observed start_raw_after "
    "service_last_tick service_last_gap_ticks service_max_gap_ticks submit_last_tick "
    "submit_max_gap_ticks refill_min_margin_ticks retire_raw_tick service_observations "
    "submit_service_observation retire_raw_valid retire_pc retire_fstat retire_fdebug "
    "retire_dma_ctrl retire_dma_remaining retire_pio_ctrl last_phase last_outcome"
).split()
OUTCOMES = (
    "ring_unavailable ring_wait model_unavailable identity_wait dma_not_ready "
    "bridge_unavailable backend_snapshot_unavailable postplan_ring_unavailable "
    "plan_rejected submit_rejected submitted binding_cancelled"
).split()
PHASES = ("prepared", "running")
FIELDS += [f"{phase}_{outcome}" for phase in PHASES for outcome in OUTCOMES]
FIELDS += ["prefetched_blocks", "cache_hits", "cache_invalidations"]
FIELDS_V3 = tuple(FIELDS)
FAST_FIELDS = (
    "fast_calls fast_submissions fast_empty fast_body_max_us fast_wall_samples "
    "fast_wall_max_cycles fast_budget_overruns"
).split()
FIELDS += FAST_FIELDS
FIELDS_V4 = tuple(FIELDS)
TIMELINE_FIELDS = (
    "plan_ahead_us commit_ahead_us refill_low_us timeline_bridge_samples "
    "partial_plan_steps plan_waits refill_waits commit_waits block_edges schedule_cycles"
).split()
FIELDS += TIMELINE_FIELDS
FIELDS += ["timeline_raw_before", "timeline_local_ns", "timeline_raw_after"]
FIELDS += [
    "fifo_words_per_edge", "fixed_high_ticks", "service_sequence",
    "last_outcome_service_sequence", "last_invalidation_service_sequence",
    "last_outcome_tick", "last_invalidation_tick", "last_submit_failure",
]
FIELDS += ["timebase", "initial_raw_tick", "initial_local_ns"]
WIDE_FIELDS = set((
    "last_outcome_tick last_invalidation_tick anchor_before anchor_after "
    "last_rising_tick last_falling_tick first_ordinal last_ordinal expires_tick "
    "last_local_ns last_target_vdc_ns maximum_bridge_width_ticks start_raw_observed "
    "start_raw_after service_last_tick service_last_gap_ticks service_max_gap_ticks "
    "submit_last_tick submit_max_gap_ticks refill_min_margin_ticks retire_raw_tick "
    "timeline_raw_before timeline_local_ns timeline_raw_after initial_raw_tick "
    "initial_local_ns"
).split())


def _load_module(path: Path):
    spec = importlib.util.spec_from_file_location("vdc_joint_phase_base", path)
    if spec is None or spec.loader is None:
        raise ValueError("Historical phase entrypoint cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_run_output(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked RUN layer over the admitted phase ancestry."""
    phase = runtime_module if runtime_module is not None else _load_module(runtime_entrypoint)
    if not hasattr(phase, "PhaseProbe") or not hasattr(phase, "follow"):
        raise ValueError("Historical phase entrypoint lacks PhaseProbe/follow")
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    def fields(raw):
        return phase.follow.parent.fields(raw)

    def parse_output_timing(raw):
        tokens = fields(raw)
        if (len(tokens) != 3 or
                not all(re.fullmatch(r"[+-]?[0-9]+", token.strip()) for token in tokens)):
            raise ValueError("TIMing snapshot requires exactly three decimal UInt32 fields")
        values = list(map(int, tokens))
        if not all(0 <= value < (1 << 32) for value in values):
            raise ValueError("TIMing snapshot outside UInt32 range")
        return dict(zip(TIMELINE_FIELDS[:3], values))

    def parse_run_output(raw):
        tokens = fields(raw)
        if not all(re.fullmatch(r"[+-]?[0-9]+", token.strip()) for token in tokens):
            raise ValueError("RUN fields must be decimal SCPI integers")
        values = list(map(int, tokens))
        if len(values) != len(FIELDS):
            raise ValueError(
                f"RUN schema 10 requires {len(FIELDS)} fields, received {len(values)}")
        run = dict(zip(FIELDS, values))
        if run["schema"] != 10:
            raise ValueError(f"Expected RUN schema 10, received {run['schema']}")
        for name, value in run.items():
            maximum = (1 << 64) - 1 if name in WIDE_FIELDS else (1 << 32) - 1
            if name == "delay_ns":
                if not -(1 << 31) <= value < (1 << 31):
                    raise ValueError("delay_ns outside signed 32-bit range")
            elif not 0 <= value <= maximum:
                raise ValueError(f"{name} outside unsigned field range")
        if run["last_phase"] >= len(PHASES) or run["last_outcome"] > len(OUTCOMES):
            raise ValueError("Unknown final client phase/outcome")
        if not 1 <= run["block_edges"] <= 16 or run["period_ns"] == 0:
            raise ValueError("Invalid fixed block length or output period")
        if (run["commit_ahead_us"] <= run["refill_low_us"] or
                run["block_edges"] != (
                    (run["commit_ahead_us"] - run["refill_low_us"]) * 1000 //
                    run["period_ns"])):
            raise ValueError("Exported block length disagrees with applied timing/period")
        if run["edges"] != run["blocks"] * run["block_edges"]:
            raise ValueError("Fixed block admission accounting mismatch")
        if run["timebase"] != 1:
            raise ValueError("RUN coordinate must be TIMER1_NS")
        if any(run[name] for name in (
                "maximum_bridge_width_ticks", "timeline_bridge_samples",
                "timeline_raw_before", "timeline_local_ns", "timeline_raw_after")):
            raise ValueError("RUN10 reserved bridge fields must be zero")
        if (run["tick_hz"] and
                run["initial_local_ns"] !=
                run["initial_raw_tick"] * 1_000_000_000 // run["tick_hz"]):
            raise ValueError("Initial TIMER1 coordinate conversion mismatch")
        return run

    class RunProbe(phase.PhaseProbe):
        def __init__(self, opt):
            super().__init__(opt)
            self.scope = None
            self.original_timing = {}
            self.trial_timing = (24000, 32000, 16000)
            self.trial_timing_applied = False
            self.report["schema"] = "DPLL_LIVE_PIO_RUN_TIMER1_V1"
            self.report["plan"].update(
                diagnostic_gpio_output=True,
                output_period_ns=opt.period_ns,
                output_high_ns=2000,
                output_duration_ms=20000,
                physical_lock_qualified=False,
                output_scope=(
                    "finite blocks, evolving DCO; physical pulse analysis is separate"),
                scope_window=(
                    "startup neighborhood only, actual time range from frozen PRE"),
                run_status_schema=10,
                output_timing=(
                    "STOP-only requested snapshot; applied timing and fixed block length "
                    "exported after STOP; no parameter writes by this capture tool"),
                cached_fallback_diagnostics=(
                    "STOP-only counters; body microseconds exclude ownership entry/exit; "
                    "wall cycles include function entry/exit but exclude reporting; "
                    "no new physical acceptance gate"),
                refill_diagnostics=(
                    "STOP-only retirement and interval observations; "
                    "no counter-only causal claim"),
                internal_scope_owner=False,
            )
            self.report.setdefault("tool_sources", []).append({
                "path": str(source), "sha256": source_sha256})
            phase.follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[PREFIX.upper()] = 1
            phase.follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[
                TIMING_PREFIX.upper()] = 3

        def control(self, board, text, expected="OK"):
            result = super().control(board, text, expected)
            if (text.upper().startswith(phase.follow.parent.SYNC.upper() + " ") and
                    int(text.split()[-1]) > 0 and board == self.boards[0]):
                for participant in self.boards:
                    row = self.report["boards"][participant.address]
                    row["run_prepare_requested"] = True
                    self.save()
                    raw = self.command(
                        participant, f"{PREFIX} {self.opt.period_ns},2000,20000")
                    row["run_request"] = int(raw.strip('"'))
                    if row["run_request"] <= 0:
                        raise RuntimeError("RUN prepare rejected")
                    self.save()
            return result

        def configure(self):
            if self.opt.scope:
                raise RuntimeError(
                    "Internal scope ownership is disabled; use workflow external_scope")
            if not self.barrier or self.block_queries:
                raise RuntimeError("RUN configuration requires all-board STOP")
            for board in self.boards:
                row = self.report["boards"][board.address]
                raw = row["output_timing_requested_raw"] = self.command(
                    board, TIMING_PREFIX + "?")
                row["output_timing_requested"] = parse_output_timing(raw)
                self.original_timing[board.address] = row["output_timing_requested"].copy()
            for board in self.boards:
                values = self.trial_timing
                expected = ",".join(map(str, values))
                self.control(board, f"{TIMING_PREFIX} {expected}", expected)
                row = self.report["boards"][board.address]
                raw = row["output_timing_trial_raw"] = self.command(
                    board, TIMING_PREFIX + "?")
                row["output_timing_trial"] = parse_output_timing(raw)
                if tuple(row["output_timing_trial"][name]
                         for name in TIMELINE_FIELDS[:3]) != values:
                    raise RuntimeError("Applied output timing mismatch")
            self.trial_timing_applied = False
            self.report["plan"]["output_timing"] = (
                "STOP-only A/B trial; outer lifecycle restores after capture")
            self.save()
            super().configure()

        def stop_all(self, label, passes=1):
            for board in self.boards:
                if self.report["boards"][board.address].get("run_prepare_requested"):
                    try:
                        self.control(board, PREFIX + ":STOP")
                    except Exception as exc:
                        self.error(label + ":run-stop:" + board.address, exc)
            super().stop_all(label, passes)

        def collect(self):
            if not self.barrier or self.block_queries:
                raise RuntimeError("RUN collection requires all-board STOP")
            for board in self.boards:
                row = self.report["boards"][board.address]
                if not row.get("run_prepare_requested"):
                    continue
                try:
                    raw = row["run_output_raw"] = self.command(board, PREFIX + "?")
                    run = row["run_output"] = parse_run_output(raw)
                    if run["generation"] != row["run_request"]:
                        raise RuntimeError("RUN generation mismatch")
                    if run["state"] not in (0, 4):
                        raise RuntimeError("RUN did not retire")
                    if run["blocks"] <= 1 or run["edges"] != run["block_edges"] * run["blocks"]:
                        raise RuntimeError("RUN block accounting mismatch")
                    expected_timing = row.get(
                        "output_timing_trial", row["output_timing_requested"])
                    if {name: run[name] for name in TIMELINE_FIELDS[:3]} != expected_timing:
                        raise RuntimeError("RUN timing differs from applied timing")
                    if run["source_retirements"] <= 0 or run["model_changes"] <= 0:
                        raise RuntimeError("RUN lacks source/model activity")
                    if run["blocks_planned"] != run["blocks"]:
                        raise RuntimeError("RUN planned block count mismatch")
                    if run["pio_enabled"] or run["dma_busy"]:
                        raise RuntimeError("RUN hardware did not retire")
                    if (run["anchor_before"] >= run["anchor_after"] or
                            run["anchor_after"] - run["anchor_before"] >
                            run["tick_hz"] // 10000):
                        raise RuntimeError("RUN anchor interval invalid")
                    row["live_output_passed"] = True
                except Exception as exc:
                    self.error("run-output:" + board.address, exc)
                self.save()
            super().collect()
            self.report["passed"] = not self.report["errors"]
            self.save()

        def run(self):
            try:
                super().run()
            finally:
                if self.trial_timing_applied:
                    for board in self.boards:
                        try:
                            old = self.original_timing[board.address]
                            expected = ",".join(
                                str(old[name]) for name in TIMELINE_FIELDS[:3])
                            self.control(board, f"{TIMING_PREFIX} {expected}", expected)
                        except Exception as exc:
                            self.error("timing-restore:" + board.address, exc)
                    self.trial_timing_applied = False
                self.report["conclusion_scope"] = (
                    "Backend counters describe admitted finite PIO blocks under evolving "
                    "local DCO, not observed physical edges. A completed scope record covers "
                    "only its frozen PRE time window; independent offline edge analysis is "
                    "required. Requested ticks use TIMER1-derived ns and retain the backend "
                    "enable-offset interval. No TIMER0 bridge is used. No 100 ns, full-window "
                    "convergence or LOCKED claim.")
                self.report["passed"] = bool(
                    self.report["passed"] and not self.report["errors"])
                self.save()

    return SimpleNamespace(
        RunProbe=RunProbe,
        phase=phase,
        PREFIX=PREFIX,
        TIMING_PREFIX=TIMING_PREFIX,
        FIELDS=FIELDS,
        WIDE_FIELDS=WIDE_FIELDS,
        parse_output_timing=parse_output_timing,
        parse_run_output=parse_run_output,
        source_path=source,
        source_sha256=source_sha256,
    )
