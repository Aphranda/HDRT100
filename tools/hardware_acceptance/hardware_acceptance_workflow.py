"""Hardware acceptance workflow: the P3 acceptance as 37 addressable steps.

Same shape as tools/vdc_priority_trace/DPLL60_workflow.py, tools/ota/OTA_workflow.py
and tools/sync_trigger/t2_workflow.py. The steps themselves live in
workflow_steps.py (ACCEPTANCE_STEPS); `workflow_steps.py run` executes
the same functions in order, so `--profile full` here and the monolithic run are one
code path. This file only selects steps, continues run directories, reuses calibration
from an earlier run, tees the log and writes the data for docs/validation/P3_FLOW.html.

  --list                          step table and profiles
  --profile full|calibration|...  a named selection (see PROFILES)
  --step N / --steps 10-12,24     an arbitrary selection, in order
  --run <dir|latest>              continue an existing run directory
  --reuse-from <dir|latest>       take steps 8-23 (topology + calibration + training)
                                  from an earlier passing run instead of measuring
  --reuse-flashed                 query the boards; if all four already run a
                                  build recorded for the current source fingerprint,
                                  skip build (step 2) and flash (step 4)
  --method ota|picotool|tdma      flash route for step 4 (tdma is reserved)
  --keep-going                    continue after a failed step
  --interactive                   wait for Enter between steps
  --refresh-html                  re-inject the newest run's data into the report

A run directory holds workflow-state.json: every value a step produced, every step's
status/sequence/invocation and every invocation's source fingerprint. A step whose
input is missing fails by name ("link_delays is missing: run step 15 ..."). A commit
credential is written only when every required step ran in order in this run
directory on one source fingerprint (credential_ineligibility); otherwise step 35
writes workflow-record.json and leaves the config receipt untouched.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "hardware_acceptance"))
import workflow_steps as p3  # noqa: E402
import dpll_capture_retry as dpll_retry  # noqa: E402

LOG_DIR = ROOT / "out" / "HardwareAcceptance"
HTML_VIEW = ROOT / "docs" / "validation" / "P3_FLOW.html"
FULL_CONFIG = Path("config/hardware_acceptance/p3_bench.json")
# Derived bench config used when the operator asks for internal-only monitoring
# (faulty or unavailable oscilloscope). The receipt still marks the external
# status as SKIPPED, so an internal-only run cannot be mistaken for a
# scope-qualified one.
INTERNAL_CONFIG = Path("out/HardwareAcceptance/p3_bench_internal.json")
SCOPE_CONFIG = Path("out/HardwareAcceptance/p3_bench_scope.json")


def write_scope_config(base: Path) -> Path:
    """Config copy that actually allows the external scope path.

    ``p3_bench.json`` carries ``dpll_short_force_internal=true`` so a normal
    FULL run stays internal-only. Selecting ``--monitor scope`` must override
    that flag, otherwise the option silently qualifies on internal evidence and
    the receipt records ``external_status=SKIPPED``.
    """
    data = json.loads(base.read_text(encoding="utf-8"))
    data["dpll_short_force_internal"] = False
    SCOPE_CONFIG.parent.mkdir(parents=True, exist_ok=True)
    if "extends" in data:
        data["extends"] = str((base.parent / data["extends"]).resolve())
    SCOPE_CONFIG.write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return SCOPE_CONFIG

GROUPS = ((0, "准备"), (8, "拓扑"), (10, "标定"), (13, "时延"), (17, "训练"),
          (24, "TDMA"), (27, "DPLL"), (31, "收尾"))
CALIBRATION_STEPS = tuple(range(8, 24))
# Profiles that continue an existing run directory need --run.
PROFILES: dict[str, dict[str, Any]] = {
    "full": {"steps": None, "note": "全部步骤（构建、刷写、新标定、TDMA、DPLL、凭证）"},
    "calibration": {"steps": [0, 1, 6, 7, *CALIBRATION_STEPS],
                    "note": "只做拓扑 + 标定 + 训练（板上现有固件；不出凭证）"},
    "firmware": {"steps": [0, 1, 2, 3, 4, 5, 7, *range(24, 37)], "reuse": True,
                 "note": "构建 + 刷写 + TDMA + DPLL，标定沿用 --reuse-from 的运行"},
    "retrain": {"steps": list(range(20, 37)), "continue": True,
                "note": "在已有运行上重做 TRN-02/03 + TDMA + DPLL + 收尾"},
    # DPLL needs a fresh TDMA process-image stage immediately before the capture
    # (without it the capture runs but the DPLL never locks), so there is no
    # DPLL-only profile: rerunning DPLL means rerunning TDMA first.
    "tdma": {"steps": list(range(24, 37)), "continue": True,
             "note": "在已有运行上重做 TDMA + DPLL + 收尾（DPLL 须紧跟 TDMA）"},
    "tail": {"steps": list(range(31, 37)), "continue": True,
             "note": "在已有运行上重做收尾（调度复核、指纹、预算、分级、凭证）"},
    "offline": {"steps": [9, 12, 15, 21, 22, 23, 25, 26, 30, 32, 33, 34, 35, 36],
                "continue": True,
                "note": "不碰台架：用运行目录里的证据重算所有门限"},
}


def group_of(number: int) -> str:
    return [name for start, name in GROUPS if number >= start][-1]


def parse_selection(text: str) -> list[int]:
    selection: list[int] = []
    for part in text.replace(" ", "").split(","):
        if not part:
            continue
        if "-" in part:
            low, _, high = part.partition("-")
            values = range(int(low), int(high) + 1)
        else:
            values = [int(part)]
        for value in values:
            if value not in p3.ACCEPTANCE_STEPS:
                raise SystemExit(f"step {value} does not exist (0-{max(p3.ACCEPTANCE_STEPS)})")
            if value not in selection:
                selection.append(value)
    return selection


def newest_run(require_steps: tuple[int, ...] = ()) -> Path:
    """The newest run directory; with require_steps, the newest whose steps passed."""
    candidates = []
    for state in LOG_DIR.glob("*/*/" + p3.HAWF_STATE_FILE):
        steps = json.loads(state.read_text(encoding="utf-8")).get("steps", {})
        if all(steps.get(str(n), {}).get("status") in ("OK", "warn", "REUSED")
               for n in require_steps):
            candidates.append(state.parent)
    for receipt in LOG_DIR.glob("*/p3-*/acceptance.json"):
        if require_steps and not (receipt.parent / p3.HAWF_STATE_FILE).is_file():
            candidates.append(receipt.parent)  # a pre-workflow monolithic pass
    if not candidates:
        raise SystemExit("no matching run directory under out/HardwareAcceptance")
    return max(candidates, key=lambda path: path.stat().st_mtime)


def resolve_dir(value: str, require_steps: tuple[int, ...] = ()) -> Path:
    if value == "latest":
        return newest_run(require_steps)
    path = Path(value)
    path = path if path.is_absolute() else ROOT / path
    if not path.is_dir():
        raise SystemExit(f"run directory not found: {path}")
    return path


def write_internal_config(base: Path) -> Path:
    """Config copy with `dpll_short_force_internal`, for `--monitor internal`."""
    data = json.loads(base.read_text(encoding="utf-8"))
    data["dpll_short_force_internal"] = True
    INTERNAL_CONFIG.parent.mkdir(parents=True, exist_ok=True)
    # Keep `extends` resolvable from the derived file's own directory.
    if "extends" in data:
        data["extends"] = str((base.parent / data["extends"]).resolve())
    INTERNAL_CONFIG.write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return INTERNAL_CONFIG


def acceptance_args(ns: argparse.Namespace, launch: dict[str, Any] | None = None,
                    out_dir: Path | None = None) -> SimpleNamespace:
    """The namespace AcceptanceRun reads; a continued run keeps its launch settings."""
    launch = dict(launch or {})
    if getattr(ns, "profile", None) == "full":
        # `--profile full` is documented as the FULL entry point; it must set
        # the same persisted flag as the `--full` alias, otherwise a resumed
        # run writes a QUICK_DIAGNOSTIC receipt.
        launch["full"] = True
    for key in ("full", "tdma_only", "diagnostic_continue"):
        if getattr(ns, key, False):
            launch[key] = True
    if getattr(ns, "monitor", None):
        launch["monitor"] = ns.monitor
    for key in ("package", "ota_summary", "build_dir", "reuse_topology", "preset"):
        if getattr(ns, key, None) is not None:
            launch[key] = str(getattr(ns, key))
    if getattr(ns, "method", None):
        launch["flash_method"] = ns.method
    resume = bool(launch.get("package") and launch.get("ota_summary"))
    full = bool(launch.get("full"))
    internal = bool(launch.get("monitor") == "internal")
    config_path = FULL_CONFIG if full else p3.DEFAULT_CONFIG
    if internal:
        config_path = write_internal_config(config_path)
    elif launch.get("monitor") == "scope":
        config_path = write_scope_config(config_path)

    def path(key):
        return Path(launch[key]) if launch.get(key) is not None else None

    return SimpleNamespace(
        root=ROOT, config=config_path,
        receipt=p3.DEFAULT_RECEIPT, command="resume" if resume else "run",
        full=full, tdma_only=bool(launch.get("tdma_only")),
        diagnostic_continue=bool(launch.get("diagnostic_continue")),
        out_dir=out_dir, reuse_topology=path("reuse_topology"),
        build_dir=path("build_dir"), package=path("package"), preset=launch.get("preset"),
        ota_summary=path("ota_summary"), flash_method=launch.get("flash_method", "ota"),
        strict_stage=[], _launch=launch)


# ---------------------------------------------------------------------------
# Calibration reuse
# ---------------------------------------------------------------------------
def rehydrate_monolithic(run: p3.AcceptanceRun, source: Path) -> dict[str, Any]:
    """State values of steps 8-23 from a pre-workflow `run` directory's receipt copy."""
    receipt = json.loads((source / "acceptance.json").read_text(encoding="utf-8"))
    if receipt.get("passed") is not True:
        raise SystemExit(f"{source} did not pass; refusing to reuse its calibration")

    def evidence(name):
        return ROOT / receipt[name]["path"]

    handoff_path = evidence("calibration_parameter_handoff")
    handoff = json.loads(handoff_path.read_text(encoding="utf-8"))
    selected = {row["phase"]: row.get("selected_parameters", {}) for row in handoff["stages"]}
    matrix_path = evidence("trn03_matrix")
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    fallback = matrix.get("derivation", {}).get("diagnostic_fallback_manifest")

    def children(name):
        summary = json.loads(evidence(name).read_text(encoding="utf-8"))
        return [ROOT / row["path"] for row in summary.get("evidence", [])]

    p3_summary_path = evidence("p3_summary")
    training_generation = int(receipt["calibration_generation"])
    return {
        "topology_summary_path": evidence("topology_summary"),
        "coarse_paths": children("coarse_calibration_summary"),
        "coded_paths": children("coded_calibration_summary"),
        "coarse_summary_path": evidence("coarse_calibration_summary"),
        "coded_summary_path": evidence("coded_calibration_summary"),
        "schedule_before": receipt["schedule_before"],
        "p3_summary_path": p3_summary_path,
        "p3_metrics": p3.validate_p3(
            json.loads(p3_summary_path.read_text(encoding="utf-8")), run.config),
        "link_delays": list(receipt["link_delay_ns_by_link"]),
        "link_base_delays": list(receipt["link_base_delay_ns_by_link"]),
        "parameter_handoff": handoff,
        "generation": training_generation - int(run.config["training_marker_repeats"]) - 1,
        "marker_summary_path": source / "trn00-marker-accepted-row" / "summary.json",
        "marker_offsets_by_node": selected["TRN-00 MARK"]["marker_offset_sample_counts_by_node"],
        "training_generation": training_generation,
        "residence_summary_path": source / "trn00-residence" / "summary.json",
        "trn00_summary_path": evidence("trn00_summary"),
        "sck_summary_path": source / "trn01-sck" / "summary.json",
        "sck_offsets_by_node": selected["TRN-01 SCK"]["sck_offset_sample_counts_by_node"],
        "trn01_summary_path": evidence("trn01_summary"),
        "training_data_offsets_by_node":
            selected["TRN-02 DATA"]["data_offset_sample_counts_by_node"],
        "data_summary_path": evidence("trn02_summary"),
        "trn03_matrix_path": matrix_path, "matrix_returncode": 0,
        "matrix_log_path": source / "trn03-matrix.log",
        "matrix_fallback_used": bool(fallback),
        "fallback_manifest_path": ROOT / fallback if fallback else None,
        "trn03_data_offsets_by_node":
            selected["TRN-03 replay matrix"]["data_offset_sample_counts_by_node"],
        "parameter_handoff_path": handoff_path,
    }, {str(n): "OK" for n in CALIBRATION_STEPS}


def reuse_calibration(run: p3.AcceptanceRun, source: Path) -> None:
    """Adopt steps 8-23 from `source`, recorded as REUSED with provenance."""
    state_path = source / p3.HAWF_STATE_FILE
    if state_path.is_file():
        source_state = p3._state_decode(json.loads(state_path.read_text(encoding="utf-8")))
        keys = [key for key, producer in p3.ACCEPTANCE_STATE_PRODUCERS.items()
                if producer in CALIBRATION_STEPS and key in source_state]
        values = {key: source_state[key] for key in keys}
        statuses = {str(n): source_state.get("steps", {}).get(str(n), {}).get("status")
                    for n in CALIBRATION_STEPS}
        source_build = source_state.get("build_id")
    else:
        values, statuses = rehydrate_monolithic(run, source)
        source_build = json.loads((source / "acceptance.json").read_text(
            encoding="utf-8")).get("build_id")
    bad = {n: status for n, status in statuses.items() if status not in ("OK", "warn")}
    if bad:
        raise SystemExit(f"{source} has calibration steps that did not pass: {bad}")
    run.put(**values)
    run.put(reused_calibration={"source": run.rel(source), "source_build_id": source_build})
    for number in CALIBRATION_STEPS:
        run.state["sequence"] += 1
        run.state["steps"][str(number)] = {
            "title": p3.ACCEPTANCE_STEPS[number][0], "status": "REUSED",
            "sequence": run.state["sequence"],
            "invocation": len(run.state["invocations"]) - 1, "seconds": 0.0,
            "reason": f"reused from {run.rel(source)} (status {statuses[str(number)]})",
            "notes": [], "source": run.rel(source)}
    run.save()


# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------
class _Tee:
    def __init__(self, *streams):
        self.streams = streams

    def write(self, data):
        for stream in self.streams:
            try:
                stream.write(data)
            except Exception:                                             # noqa: BLE001
                pass
        return len(data)

    def flush(self):
        for stream in self.streams:
            try:
                stream.flush()
            except Exception:                                             # noqa: BLE001
                pass


def log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def open_run(ns: argparse.Namespace) -> p3.AcceptanceRun:
    if ns.run:
        out_dir = resolve_dir(ns.run)
        state = json.loads((out_dir / p3.HAWF_STATE_FILE).read_text(encoding="utf-8"))
        return p3.AcceptanceRun.load(acceptance_args(ns, state.get("launch"), out_dir), out_dir)
    name = Path("out/HardwareAcceptance") / f"hawf-{time.strftime('%H%M%S')}"
    args = acceptance_args(ns, out_dir=name)
    run = p3.AcceptanceRun.create(args)
    run.put(launch=args._launch, profile=ns.profile)
    run.save()
    return run


def selection_for(ns: argparse.Namespace, run: p3.AcceptanceRun) -> list[int]:
    if ns.steps:
        return parse_selection(ns.steps)
    if ns.step is not None:
        return parse_selection(str(ns.step))
    profile = PROFILES[ns.profile]
    return list(profile["steps"]) if profile["steps"] else p3.acceptance_step_plan(run.args)



BENCH_LOCK = ROOT / "out" / "HardwareAcceptance" / ".bench.lock"


def acquire_bench_lock() -> None:
    """Refuse to drive the bench while another acceptance run holds it."""
    import json
    import os

    BENCH_LOCK.parent.mkdir(parents=True, exist_ok=True)
    if BENCH_LOCK.exists():
        try:
            held = json.loads(BENCH_LOCK.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            held = {}
        pid = held.get("pid")
        alive = False
        if isinstance(pid, int):
            try:
                os.kill(pid, 0)
                alive = True
            except (OSError, SystemError):
                # Windows raises SystemError for a dead PID (os.kill's
                # OpenProcess failure path); a killed acceptance run must not
                # wedge every later invocation.
                alive = False
        if alive:
            raise SystemExit(
                f"bench busy: pid {pid} {held.get('run_dir', '')} "
                f"(started {held.get('started_at', '')}); "
                "remove out/HardwareAcceptance/.bench.lock if that process is gone")
        try:
            BENCH_LOCK.unlink()
        except OSError:
            pass
    BENCH_LOCK.write_text(json.dumps({
        "pid": os.getpid(),
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "argv": " ".join(sys.argv[1:]),
    }), encoding="utf-8")


def release_bench_lock() -> None:
    try:
        BENCH_LOCK.unlink()
    except OSError:
        pass


def run_selection(ns: argparse.Namespace) -> int:
    acquire_bench_lock()
    try:
        return _run_selection_locked(ns)
    finally:
        release_bench_lock()


def blocking_diagnostic_failure(run: p3.AcceptanceRun, number: int | None = None):
    """First diagnostic failure a step explicitly marked as blocking.

    With ``number``, only that step's own failure is returned, so a blocking
    failure from an earlier step cannot make every later step look failed.
    """
    return next((row for row in run.diagnostic_failures
                 if row.get("blocking") and
                 (number is None or row.get("step") == number)), None)


def fingerprint_mismatch(run: p3.AcceptanceRun, current) -> str | None:
    """Explain a worktree change since step 1, or None when it still matches."""
    before = run.state.get("fingerprint_before")
    if before is None:
        return None
    expected = (before, run.state.get("source_count"))
    if current == expected:
        return None
    return (f"source changed since step 1: {str(before)[:12]} -> "
            f"{str(current[0])[:12]}; step 29 must run on the fingerprint "
            "step 1 recorded")


def execute_step_with_retry(run: p3.AcceptanceRun, number: int):
    """Execute one step; retry step 29 once when the workflow policy allows.

    Returns ``(record, error)``.  ``error`` is None on success (including a
    successful retry); otherwise it is the exception that ended the step.
    """
    capture_dirs_before: set[Path] = set()
    if number == 29:
        # The workflow owns the step-29 retry policy. One physical window per
        # attempt keeps that policy visible here instead of hiding a second
        # window inside the step implementation.
        run.config["dpll_short_capture_attempts"] = 1
        capture_dirs_before = set(run.out_dir.glob("dpll-four-board-short*"))
        mismatch = fingerprint_mismatch(
            run, p3.working_source_fingerprint(run.root))
        if mismatch is not None:
            # Fail here in milliseconds instead of writing a provisional
            # receipt and letting the child reject it 2-25 s later.
            run.state["sequence"] = int(run.state.get("sequence", 0)) + 1
            record = {
                "title": p3.ACCEPTANCE_STEPS[number][0],
                "sequence": run.state["sequence"],
                "invocation": len(run.state.get("invocations") or [{}]) - 1,
                "status": "FAIL", "reason": f"AcceptanceError: {mismatch}",
                "seconds": 0.0,
            }
            run.state["steps"][str(number)] = record
            run.save()
            return record, p3.AcceptanceError(mismatch)
    try:
        return run.execute(number), None
    except Exception as exc:                                          # noqa: BLE001
        if (number != 29 or
                not dpll_retry.step_29_is_retryable(
                    run, exc, capture_dirs_before)):
            return run.state["steps"][str(number)], exc
        log("=== Step 29 RETRY    fresh 60 s window (retryable failure)")
        try:
            return run.execute(number), None
        except Exception as retry_exc:                                # noqa: BLE001
            return run.state["steps"][str(number)], retry_exc


def parse_time_budget(raw: str) -> tuple[float, float]:
    """Parse ``warning_s,error_s`` for the explicit acceptance budget override."""
    parts = [value.strip() for value in str(raw).split(",")]
    if len(parts) != 2:
        raise SystemExit("--time-budget must be warning_s,error_s")
    try:
        warning, error = (float(value) for value in parts)
    except ValueError as exc:
        raise SystemExit("--time-budget values must be numeric") from exc
    if not (0.0 < warning < error):
        raise SystemExit("--time-budget requires 0 < warning_s < error_s")
    return warning, error


def _run_selection_locked(ns: argparse.Namespace) -> int:
    profile = PROFILES[ns.profile]
    if profile.get("continue") and not ns.run and not (ns.steps or ns.step is not None):
        raise SystemExit(f"profile {ns.profile} continues a run: pass --run <dir|latest>")
    if profile.get("reuse") and not ns.reuse_from and not ns.run:
        raise SystemExit(f"profile {ns.profile} reuses calibration: pass --reuse-from <dir|latest>")
    run = open_run(ns)
    invocation = len(run.state["invocations"]) - 1
    log_path = run.out_dir / f"workflow-{invocation:02d}.log"
    original, handle = sys.stdout, log_path.open("a", encoding="utf-8")
    sys.stdout = _Tee(original, handle)
    failed = False
    try:
        if getattr(ns, "time_budget", None):
            warning, error = parse_time_budget(ns.time_budget)
            run.config["acceptance_time_budget"] = {
                "warning_s": warning, "error_s": error}
            run.state["time_budget_override"] = dict(
                run.config["acceptance_time_budget"])
            run.save()
            log(f"acceptance time budget override warning={warning:g}s "
                f"error={error:g}s")
        selection = selection_for(ns, run)
        log(f"run {run.rel(run.out_dir)} invocation {invocation} profile {ns.profile}"
            f" steps {selection}")
        if ns.reuse_from:
            source = resolve_dir(ns.reuse_from, CALIBRATION_STEPS)
            reuse_calibration(run, source)
            log(f"steps 8-23 reused from {run.rel(source)}")
        for number in selection:
            title = p3.ACCEPTANCE_STEPS[number][0]
            bench = " [B]" if number in p3.ACCEPTANCE_BENCH_STEPS else ""
            log(f"=== Step {number}{bench} START  {title}")
            record, error = execute_step_with_retry(run, number)
            if error is not None:
                failed = True
                if (run.alarm_context is not None and
                        not isinstance(error, p3.MissingPrerequisite) and
                        run.state["steps"].get("34", {}).get("status") not in ("OK", "warn")):
                    p3.write_alarm_report(run.alarm_context, error=str(error))
            blocking = blocking_diagnostic_failure(run, number)
            if blocking is not None:
                # A step may downgrade a hard prerequisite to a warn diagnostic
                # (for example the TDMA stopped handoff). Continuing would only
                # fail later at step 29, so mark that step FAIL here: the run
                # stops by default, stays non-zero under --keep-going, and the
                # credential cannot treat the prerequisite as satisfied.
                failed = True
                record = run.state["steps"][str(number)]
                if record.get("status") != "FAIL":
                    record.update(status="FAIL",
                                  reason=f"blocking: {blocking.get('error')}")
                    run.save()
                log(f"=== Step {number} BLOCKING diagnostic: "
                    f"{blocking.get('error')}")
            log(f"=== Step {number} END    {record['status']}  {record['seconds']:.1f}s"
                f"{'  ' + record['reason'] if record.get('reason') else ''}")
            if failed and not ns.keep_going:
                break
            if ns.interactive:
                try:
                    if input(f"Step {number} done. Enter=continue, q=stop: ").strip()[:1] == "q":
                        break
                except EOFError:
                    pass
        if failed and not run.state.get("receipt_written"):
            p3._restore_receipt_snapshot(run)
        reasons = p3.credential_ineligibility(run)
        log("credential: " + ("written" if run.state.get("receipt_written") else
            ("eligible (step 35 not run)" if not reasons else "not eligible: " + "; ".join(reasons[:4]))))
        emit_flow_data(run, selection, invocation)
    finally:
        sys.stdout = original
        handle.close()
    return 1 if failed else 0


def emit_flow_data(run: p3.AcceptanceRun, selection: list[int], invocation: int) -> None:
    steps = run.state["steps"]
    payload = {
        "schema": "HDRT100_HAWF_FLOW_V1",
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "run_dir": run.rel(run.out_dir),
        "profile": run.state.get("profile"),
        "invocation": invocation,
        "invocations": [{"index": index, "started": row["started_at_utc"],
                         "fingerprint": row["source_tree_sha256"][:12],
                         "elapsed_s": round(row["elapsed_s"], 1)}
                        for index, row in enumerate(run.state["invocations"])],
        "selection": selection,
        "build_id": run.state.get("build_id"),
        "reused_calibration": run.state.get("reused_calibration"),
        "receipt_written": bool(run.state.get("receipt_written")),
        "credential_reasons": p3.credential_ineligibility(run),
        "profiles": {name: value["note"] for name, value in PROFILES.items()},
        "groups": [{"start": start, "name": name} for start, name in GROUPS],
        "steps": [{
            "id": number, "title": title, "group": group_of(number),
            "bench": number in p3.ACCEPTANCE_BENCH_STEPS,
            "planned": number in selection,
            "status": steps.get(str(number), {}).get(
                "status", "pending" if number in selection else "not-run"),
            "seconds": steps.get(str(number), {}).get("seconds"),
            "reason": steps.get(str(number), {}).get("reason", ""),
            "notes": steps.get(str(number), {}).get("notes", []),
            "sequence": steps.get(str(number), {}).get("sequence"),
            "invocation": steps.get(str(number), {}).get("invocation"),
        } for number, (title, _function) in sorted(p3.ACCEPTANCE_STEPS.items())],
    }
    for name in ("hawf_flow.json", "hawf_flow_data.js"):
        path = LOG_DIR / name
        text = json.dumps(payload, ensure_ascii=False, indent=2)
        path.write_text(text if name.endswith(".json") else f"window.HAWF_FLOW = {text};\n",
                        encoding="utf-8")
    (run.out_dir / "hawf_flow.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    target = inject_html_view(payload)
    log(f"flow data: out/HardwareAcceptance/hawf_flow.json · html {target or '(not found)'}")


def inject_html_view(payload: dict[str, Any]) -> str:
    """Write the payload into the report's inline data block (see DPLL60_workflow)."""
    if not HTML_VIEW.exists():
        return ""
    text = HTML_VIEW.read_text(encoding="utf-8")
    start, end = "<!-- P3-DATA-START -->", "<!-- P3-DATA-END -->"
    if start not in text or end not in text:
        return ""
    block = (f'{start}\n<script id="p3-flow-data" type="application/json">'
             f'{json.dumps(payload, ensure_ascii=False)}</script>\n{end}')
    head, _, rest = text.partition(start)
    _, _, tail = rest.partition(end)
    HTML_VIEW.write_text(head + block + tail, encoding="utf-8", newline="")
    return str(HTML_VIEW.relative_to(ROOT)).replace("\\", "/")


def refresh_html_view() -> int:
    path = LOG_DIR / "hawf_flow.json"
    if not path.exists():
        print(f"no data yet: {path} - run some steps first")
        return 2
    payload = json.loads(path.read_text(encoding="utf-8"))
    print(f"refreshed {inject_html_view(payload) or '(HTML view not found)'}"
          f" from {path.name} (generated {payload.get('generated')})")
    return 0


def print_list() -> None:
    for number, (title, _function) in sorted(p3.ACCEPTANCE_STEPS.items()):
        bench = "B" if number in p3.ACCEPTANCE_BENCH_STEPS else "-"
        print(f"{number:>3}  {bench}  {group_of(number):<4}  {title}")
    print()
    for name, value in PROFILES.items():
        steps = value["steps"] or "run plan"
        print(f"  --profile {name:<12} {value['note']}  {steps}")


def _reuse_flashed_match() -> tuple[Path, str] | None:
    """A build record whose fingerprint equals the working tree and whose build
    id is what all four boards report."""
    config = p3.load_bench_config(FULL_CONFIG)
    board_ids = list(config["p3_board_ids_in_physical_order"])
    builds = p3.read_online_builds(board_ids, p3.DEFAULT_ACCEPTANCE_TIMING)
    if len(set(builds.values())) != 1 or "" in builds.values():
        print(f"reuse-flashed: board builds differ or are empty: {builds}")
        return None
    build_id = next(iter(builds.values()))
    fingerprint, count = p3.working_source_fingerprint(ROOT)
    for record in sorted(ROOT.glob("out/build/*/p3-build-record.json")):
        try:
            data = json.loads(record.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        if (data.get("build_id") == build_id
                and data.get("source_fingerprint") == fingerprint):
            package = record.parent / "DHRT100_UPDATE.pkg"
            if package.is_file():
                print(f"reuse-flashed: boards run {build_id} "
                      f"(fingerprint {fingerprint[:12]}, sources {count})")
                return package, build_id
    print(f"reuse-flashed: boards run {build_id} but no build record matches "
          f"fingerprint {fingerprint[:12]}; building normally")
    return None


def _find_ota_summary(build_id: str) -> Path | None:
    def recent(path: Path) -> float:
        try:
            return path.stat().st_mtime
        except OSError:
            return 0.0

    for date_dir in sorted(LOG_DIR.iterdir(), key=recent, reverse=True):
        if not date_dir.is_dir():
            continue
        for run_dir in sorted(date_dir.glob("hawf-*"), key=recent, reverse=True):
            for summary in (run_dir / "ota-four-board" / "summary.json",
                            run_dir / "summary.json"):
                if not summary.is_file():
                    continue
                try:
                    data = json.loads(summary.read_text(encoding="utf-8"))
                except (OSError, ValueError):
                    continue
                if (data.get("expected_build") == build_id
                        and data.get("passed") is True
                        and data.get("failed_count") == 0):
                    return summary
    return None


def _synthesize_ota_summary(build_id: str) -> Path:
    config = p3.load_bench_config(FULL_CONFIG)
    board_ids = list(config["p3_board_ids_in_physical_order"])
    out_dir = LOG_DIR / "reuse-flashed" / build_id
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "summary.json"
    payload = {
        "schema": "HAOFV_OTA_SUMMARY_V1",
        "expected_build": build_id,
        "dry_run": False,
        "passed": True,
        "failed_count": 0,
        "board_count": len(board_ids),
        "updated_count": len(board_ids),
        "reused": True,
        "note": "boards already run this build; OTA skipped by --reuse-flashed",
        "boards": [
            {"serial_number": board_id, "build_id": build_id,
             "max_data_block_size": 4096, "passed": True}
            for board_id in board_ids
        ],
    }
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--profile", choices=sorted(PROFILES), default="full")
    parser.add_argument("--step", type=int, default=None)
    parser.add_argument("--steps", default=None)
    parser.add_argument("--full", action="store_true", help="alias of --profile full")
    parser.add_argument("--run", default=None, help="continue a run directory (or 'latest')")
    parser.add_argument("--reuse-from", default=None,
                        help="adopt steps 8-23 from an earlier passing run (or 'latest')")
    parser.add_argument("--method", choices=("ota", "picotool", "tdma"), default=None)
    parser.add_argument("--package", type=Path, default=None)
    parser.add_argument("--ota-summary", type=Path, default=None)
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--preset", default=None,
                        help="CMake preset forwarded to the build step")
    parser.add_argument("--reuse-topology", type=Path, default=None)
    parser.add_argument("--full-bench", dest="full_config", action="store_true",
                        help="use config/hardware_acceptance/p3_bench.json (FULL profile)")
    parser.add_argument("--monitor", choices=("scope", "internal"), default=None,
                        help="DPLL qualification source: 'scope' (default) needs a "
                             "working oscilloscope; 'internal' qualifies on "
                             "board-internal evidence only and records "
                             "external_status=SKIPPED")
    parser.add_argument("--tdma-only", action="store_true")
    parser.add_argument("--diagnostic-continue", action="store_true")
    parser.add_argument("--time-budget", default=None,
                        help="explicit acceptance time budget warning_s,error_s "
                             "(FULL configs may omit it; recorded in run state)")
    parser.add_argument("--keep-going", action="store_true")
    parser.add_argument("--interactive", action="store_true",
                        help="wait for Enter between steps")
    parser.add_argument("--reuse-flashed", action="store_true",
                        help="skip build+OTA when all boards already run a build "
                             "recorded for the current source fingerprint")
    parser.add_argument("--refresh-html", action="store_true")
    ns = parser.parse_args(argv)
    ns.full = ns.full_config
    if ns.reuse_flashed and ns.package is None:
        match = _reuse_flashed_match()
        if match is not None:
            ns.package, build_id = match
            ns.build_dir = ns.package.parent
            # Reuse means "live boards already run this build"; synthesize the
            # OTA summary from the live four-board check.  Reusing an old
            # OTA summary can carry stale board build_ids and make step24 fail
            # on a mismatch that no longer exists.
            ns.ota_summary = _synthesize_ota_summary(build_id)
            print(f"reuse-flashed: package={ns.package}")
            print(f"reuse-flashed: ota_summary={ns.ota_summary}")
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")  # GBK consoles cannot print every step note
        except (AttributeError, ValueError):
            pass
    if ns.refresh_html:
        return refresh_html_view()
    if ns.list:
        print_list()
        return 0
    return run_selection(ns)


if __name__ == "__main__":
    sys.exit(main())
