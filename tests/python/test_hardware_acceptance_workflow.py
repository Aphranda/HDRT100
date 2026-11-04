"""Offline checks of the step-addressable hardware acceptance and its workflow CLI."""
import inspect
import json
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "hardware_acceptance"))
import workflow_steps as p3  # noqa: E402
import hardware_acceptance_workflow as workflow  # noqa: E402
from test_dpll_capture_retry import write_probe  # noqa: E402


def make_args(tmp_path, **overrides):
    values = dict(root=p3.ROOT, config=p3.DEFAULT_CONFIG, receipt=tmp_path / "receipt.json",
                  command="run", full=False, tdma_only=False, diagnostic_continue=False,
                  out_dir=tmp_path / "run", reuse_topology=None, build_dir=None,
                  package=None, ota_summary=None, flash_method="ota", strict_stage=[])
    values.update(overrides)
    return SimpleNamespace(**values)


@pytest.fixture
def fingerprint(monkeypatch):
    value = {"current": ("a" * 64, 10)}
    monkeypatch.setattr(p3, "working_source_fingerprint", lambda _root: value["current"])
    return value


def test_registry_is_37_contiguous_titled_steps():
    assert sorted(p3.ACCEPTANCE_STEPS) == list(range(37))
    assert all(title for title, _function in p3.ACCEPTANCE_STEPS.values())
    assert p3.ACCEPTANCE_BENCH_STEPS < set(p3.ACCEPTANCE_STEPS)


def test_every_state_producer_actually_puts_its_key():
    for key, producer in p3.ACCEPTANCE_STATE_PRODUCERS.items():
        source = inspect.getsource(p3.ACCEPTANCE_STEPS[producer][1])
        assert f"{key}=" in source or f'"{key}"' in source, (key, producer)


def test_run_and_resume_plans_keep_the_monolithic_order():
    run_plan = p3.acceptance_step_plan(SimpleNamespace(command="run"))
    resume_plan = p3.acceptance_step_plan(SimpleNamespace(command="resume"))
    assert run_plan == [0, 1, 2, 3, 4, 5, *range(7, 37)]
    assert resume_plan == [0, 1, 3, 5, 6, *range(7, 37)]


def test_monolithic_run_executes_the_plan_in_order(tmp_path, fingerprint, monkeypatch):
    executed = []
    stubs = {number: (title, (lambda number: lambda run: executed.append(number))(number))
             for number, (title, _function) in p3.ACCEPTANCE_STEPS.items()}
    monkeypatch.setattr(p3, "ACCEPTANCE_STEPS", stubs)
    p3._run_acceptance_impl(make_args(tmp_path))
    assert executed == p3.acceptance_step_plan(SimpleNamespace(command="run"))
    state = json.loads((tmp_path / "run" / p3.HAWF_STATE_FILE).read_text(encoding="utf-8"))
    assert [state["steps"][str(n)]["sequence"] for n in executed] == list(range(1, len(executed) + 1))


def test_state_round_trip_keeps_paths(tmp_path):
    value = {"a": Path("x/y"), "b": [Path("z"), {"c": Path("w")}], "d": 3, "e": None}
    encoded = json.loads(json.dumps(p3._state_encode(value)))
    assert p3._state_decode(encoded) == value


def test_state_persists_across_invocations(tmp_path, fingerprint):
    run = p3.AcceptanceRun.create(make_args(tmp_path))
    run.execute(1)
    again = p3.AcceptanceRun.load(make_args(tmp_path), run.out_dir)
    assert again.get("fingerprint_before") == "a" * 64
    assert len(again.state["invocations"]) == 2
    assert again.state["steps"]["1"]["status"] == "OK"
    assert not list(run.out_dir.glob("*.tmp"))


def test_missing_prerequisite_names_the_producing_step(tmp_path, fingerprint):
    run = p3.AcceptanceRun.create(make_args(tmp_path))
    with pytest.raises(p3.MissingPrerequisite, match="p3_summary_path is missing: run step 14"):
        run.execute(15)
    assert run.state["steps"]["15"]["status"] == "FAIL"


def test_rerun_replaces_only_that_steps_diagnostic_failures(tmp_path, fingerprint, monkeypatch):
    calls = {"n": 0}

    def noisy(run):
        calls["n"] += 1
        run.diagnostic_failures.append({"phase": "TRN-01 SCK training", "attempt": calls["n"]})
    stubs = dict(p3.ACCEPTANCE_STEPS)
    stubs[19] = ("noisy", noisy)
    stubs[20] = ("other", lambda run: run.diagnostic_failures.append({"phase": "TRN-02 DATA"}))
    monkeypatch.setattr(p3, "ACCEPTANCE_STEPS", stubs)
    run = p3.AcceptanceRun.create(make_args(tmp_path))
    shared = run.diagnostic_failures
    run.execute(19)
    run.execute(20)
    run.execute(19)
    assert run.diagnostic_failures is shared
    assert [(row["step"], row.get("attempt")) for row in run.diagnostic_failures] == [
        (20, None), (19, 2)]


def test_tdma_flash_route_is_reserved(tmp_path, fingerprint):
    run = p3.AcceptanceRun.create(make_args(tmp_path, flash_method="tdma"))
    run.put(package=tmp_path / "pkg", build_id="b")
    with pytest.raises(p3.AcceptanceError, match="reserved and not implemented"):
        run.execute(4)


def eligible_run(tmp_path):
    run = p3.AcceptanceRun.create(make_args(tmp_path))
    run.put(fingerprint_before="a" * 64)
    for sequence, number in enumerate(p3.credential_required_steps(run), 1):
        run.state["steps"][str(number)] = {"status": "OK", "sequence": sequence}
    run.state["sequence"] = len(run.state["steps"])
    return run


def test_complete_ordered_single_fingerprint_run_is_eligible(tmp_path, fingerprint):
    assert p3.credential_ineligibility(eligible_run(tmp_path)) == []


@pytest.mark.parametrize("mutation,reason", [
    (lambda run: run.state["steps"].pop("20"), "step 20 was not run"),
    (lambda run: run.state["steps"]["20"].update(sequence=999), "step 21 ran before an upstream"),
    (lambda run: run.state["steps"]["20"].update(status="REUSED"), "step 20 last status REUSED"),
    (lambda run: run.state["steps"]["24"].update(status="FAIL"), "step 24 last status FAIL"),
    (lambda run: run.state.update(flash_method="picotool"), "flash method picotool"),
    (lambda run: run.state["invocations"].append(
        {"source_tree_sha256": "b" * 64, "elapsed_s": 0}), "different source fingerprint"),
])
def test_credential_refusals_are_named(tmp_path, fingerprint, mutation, reason):
    run = eligible_run(tmp_path)
    mutation(run)
    assert any(reason in text for text in p3.credential_ineligibility(run))


def test_record_only_run_leaves_the_config_receipt_byte_identical(tmp_path, fingerprint):
    args = make_args(tmp_path)
    original = b'{"schema": "previous credential"}\n'
    args.receipt.write_bytes(original)
    run = p3.AcceptanceRun.create(args)
    run.execute(0)
    # The DPLL step overwrites the config receipt with its provisional receipt.
    args.receipt.write_bytes(b'{"schema": "provisional"}\n')
    run.put(provisional_receipt_written=True)
    record = run.execute(35)
    assert args.receipt.read_bytes() == original
    assert record["status"] == "warn" and "record only" in record["reason"]
    saved = json.loads((run.out_dir / "workflow-record.json").read_text(encoding="utf-8"))
    assert saved["schema"] == p3.HAWF_RECORD_SCHEMA and saved["credential"] is False
    assert saved["reasons"]


def test_workflow_selection_and_profiles():
    assert workflow.parse_selection("1-3,7,2") == [1, 2, 3, 7]
    with pytest.raises(SystemExit):
        workflow.parse_selection("40")
    assert set(workflow.PROFILES) >= {"full", "calibration", "firmware", "retrain", "tdma",
                                      "tail", "offline"}
    # DPLL needs a fresh TDMA stage right before it, and a continued profile must run
    # through the tail: nothing downstream of a rerun step may be left stale.
    for name in ("retrain", "tdma"):
        steps = workflow.PROFILES[name]["steps"]
        assert steps == list(range(steps[0], 37)) and steps[0] <= 24, name
    for value in workflow.PROFILES.values():
        assert value["steps"] is None or set(value["steps"]) <= set(p3.ACCEPTANCE_STEPS)
    offline = set(workflow.PROFILES["offline"]["steps"])
    assert not offline & p3.ACCEPTANCE_BENCH_STEPS


@pytest.mark.parametrize("profile", ["retrain", "tdma", "tail", "offline"])
def test_continuing_profiles_require_a_run(profile):
    with pytest.raises(SystemExit, match="--run"):
        workflow.main(["--profile", profile])


def test_firmware_profile_requires_a_calibration_source():
    with pytest.raises(SystemExit, match="--reuse-from"):
        workflow.main(["--profile", "firmware"])


def test_acceptance_args_select_resume_only_with_package_and_ota_summary(tmp_path):
    ns = SimpleNamespace(full=False, tdma_only=False, diagnostic_continue=False,
                         package=tmp_path / "p.pkg", ota_summary=None, build_dir=None,
                         reuse_topology=None, method=None)
    assert workflow.acceptance_args(ns).command == "run"
    ns.ota_summary = tmp_path / "ota.json"
    resumed = workflow.acceptance_args(ns)
    assert resumed.command == "resume" and resumed.package == tmp_path / "p.pkg"
    continued = workflow.acceptance_args(
        SimpleNamespace(full=False, tdma_only=False, diagnostic_continue=False, package=None,
                        ota_summary=None, build_dir=None, reuse_topology=None, method=None),
        launch={"full": True, "flash_method": "picotool"})
    assert continued.full is True and continued.flash_method == "picotool"
    assert continued.config == workflow.FULL_CONFIG


def test_previous_receipt_snapshot_never_takes_a_provisional(tmp_path, fingerprint, monkeypatch):
    args = make_args(tmp_path)
    args.receipt.write_text(json.dumps({"schema": p3.PROVISIONAL_SCHEMA}), encoding="utf-8")
    committed = {"schema": "committed credential"}
    monkeypatch.setattr(p3, "read_index_json", lambda root, path: committed)
    run = p3.AcceptanceRun.create(args)
    run.execute(0)
    snapshot = run.state["previous_receipt_path"]
    assert json.loads(snapshot.read_text(encoding="utf-8")) == committed


def test_failed_workflow_restores_the_config_receipt(tmp_path, fingerprint, monkeypatch):
    original = b'{"schema": "previous credential"}\n'
    receipt = tmp_path / "receipt.json"
    receipt.write_bytes(original)

    def provisional_then_fail(run):
        run.receipt_path.write_text('{"schema": "provisional"}', encoding="utf-8")
        run.put(provisional_receipt_written=True)
        raise p3.AcceptanceError("capture failed")
    stubs = dict(p3.ACCEPTANCE_STEPS)
    stubs[29] = ("capture", provisional_then_fail)
    monkeypatch.setattr(p3, "ACCEPTANCE_STEPS", stubs)
    monkeypatch.setattr(p3, "DEFAULT_RECEIPT", receipt)
    monkeypatch.setattr(workflow, "emit_flow_data", lambda *args: None)
    monkeypatch.setattr(p3, "acceptance_output_path", lambda root, requested, stamp: tmp_path / "run")
    assert workflow.main(["--steps", "0,29"]) == 1
    assert receipt.read_bytes() == original


def test_step15_rerun_keeps_training_handoff_entries(tmp_path, fingerprint, monkeypatch):
    run = p3.AcceptanceRun.create(make_args(tmp_path))
    summary = tmp_path / "p3.json"
    summary.write_text("{}", encoding="utf-8")
    run.put(p3_summary_path=summary)
    monkeypatch.setattr(p3, "validate_p3", lambda summary, config: {"trial_count": 1})
    monkeypatch.setattr(p3, "p3_link_delays", lambda summary, config: [80, 82, 84, 86])
    run.execute(15)
    run.get("parameter_handoff")["stages"].append({"phase": "TRN-00 MARK"})
    run.execute(15)
    assert run.get("parameter_handoff")["stages"] == [{"phase": "TRN-00 MARK"}]
    monkeypatch.setattr(p3, "p3_link_delays", lambda summary, config: [90, 92, 94, 96])
    run.execute(15)
    assert run.get("parameter_handoff")["stages"] == []


def test_capture_rerun_uses_a_fresh_directory(tmp_path, monkeypatch):
    seen = []
    monkeypatch.setattr(p3, "_run_step", lambda command, root, log, **kwargs: seen.append(
        (Path(command[command.index("--out") + 1]).name, log.name, kwargs.get("heartbeat_s"))))
    config = p3.load_bench_config(p3.ROOT / p3.DEFAULT_CONFIG)
    first = p3.dpll_short_run_capture(tmp_path, config, tmp_path, scope_enabled=True)
    first.mkdir()
    second = p3.dpll_short_run_capture(tmp_path, config, tmp_path, scope_enabled=True)
    assert [row[0] for row in seen] == ["dpll-four-board-short", "dpll-four-board-short-attempt2"]
    assert seen[1][1] == "dpll-short-attempt2.log" and seen[0][2] == 10.0
    assert second.name == "dpll-four-board-short-attempt2"


class _FakeRun:
    def __init__(self, out_dir, outcomes, on_fail=None):
        self.out_dir = out_dir
        self.root = out_dir
        self.config = {}
        self.state = {"steps": {}}
        self._outcomes = list(outcomes)
        self._on_fail = on_fail
        self.calls = 0
        self.saved = 0

    def save(self):
        self.saved += 1

    def execute(self, number):
        self.calls += 1
        outcome = self._outcomes.pop(0)
        if isinstance(outcome, BaseException):
            if self._on_fail is not None:
                self._on_fail()
            self.state["steps"][str(number)] = {
                "status": "FAIL", "seconds": 0.0, "reason": str(outcome)}
            raise outcome
        return outcome


def test_workflow_retries_step29_only_when_policy_allows(tmp_path):
    def partial_match():
        write_probe(tmp_path / "dpll-four-board-short",
                    followers=[(6000, 9), (6000, 9), (6000, 9)])
    run = _FakeRun(
        tmp_path,
        [RuntimeError("Capture exited 2"), {"status": "OK", "seconds": 1.0}],
        on_fail=partial_match)
    record, error = workflow.execute_step_with_retry(run, 29)
    assert run.calls == 2 and error is None and record["status"] == "OK"
    assert run.config["dpll_short_capture_attempts"] == 1


def test_workflow_does_not_retry_deterministic_step29(tmp_path):
    def role_reject():
        write_probe(tmp_path / "dpll-four-board-short", preflight=False,
                    followers=[(0, 4), (0, 4), (0, 4)])
    run = _FakeRun(tmp_path, [RuntimeError("Capture exited 2")],
                   on_fail=role_reject)
    record, error = workflow.execute_step_with_retry(run, 29)
    assert run.calls == 1 and error is not None and record["status"] == "FAIL"


def test_workflow_does_not_retry_other_steps(tmp_path):
    run = _FakeRun(tmp_path, [RuntimeError("boom")])
    record, error = workflow.execute_step_with_retry(run, 1)
    assert run.calls == 1 and error is not None and record["status"] == "FAIL"


def test_blocking_diagnostic_failure_is_visible_to_the_workflow():
    run = SimpleNamespace(diagnostic_failures=[
        {"step": 26, "phase": "warn-only", "error": "not blocking"},
        {"step": 26, "phase": "TDMA stopped-record handoff", "blocking": True,
         "error": "explicit startup barrier timed out"},
    ])
    blocking = workflow.blocking_diagnostic_failure(run, 26)
    assert blocking is not None
    assert blocking["phase"] == "TDMA stopped-record handoff"
    # An earlier step's blocking failure must not fail a later step.
    assert workflow.blocking_diagnostic_failure(run, 27) is None


def test_no_blocking_failure_returns_none():
    run = SimpleNamespace(diagnostic_failures=[{"step": 26, "phase": "warn-only"}])
    assert workflow.blocking_diagnostic_failure(run, 26) is None


def test_workflow_rejects_changed_tree_before_step29(tmp_path, monkeypatch):
    run = _FakeRun(tmp_path, [{"status": "OK", "seconds": 1.0}])
    run.state.update(fingerprint_before="a" * 64, source_count=10,
                     invocations=[{}], sequence=0)
    monkeypatch.setattr(p3, "working_source_fingerprint",
                        lambda root: ("b" * 64, 10))
    record, error = workflow.execute_step_with_retry(run, 29)
    assert run.calls == 0
    assert record["status"] == "FAIL"
    assert "source changed since step 1" in str(error)
    assert run.state["steps"]["29"] is record and run.saved == 1


def test_workflow_runs_step29_on_a_matching_tree(tmp_path, monkeypatch):
    run = _FakeRun(tmp_path, [{"status": "OK", "seconds": 1.0}])
    run.state.update(fingerprint_before="a" * 64, source_count=10,
                     invocations=[{}], sequence=0)
    monkeypatch.setattr(p3, "working_source_fingerprint",
                        lambda root: ("a" * 64, 10))
    record, error = workflow.execute_step_with_retry(run, 29)
    assert run.calls == 1 and error is None and record["status"] == "OK"
    assert run.config["dpll_short_capture_attempts"] == 1


def test_time_budget_override_is_parsed_and_bounded():
    assert workflow.parse_time_budget("900,1200") == (900.0, 1200.0)
    for raw in ("900", "900,800", "0,1200", "x,1200"):
        with pytest.raises(SystemExit):
            workflow.parse_time_budget(raw)


def test_scope_monitor_disables_forced_internal(tmp_path, monkeypatch):
    base = tmp_path / "bench.json"
    base.write_text(json.dumps({"dpll_short_force_internal": True}), encoding="utf-8")
    monkeypatch.setattr(p3, "DEFAULT_CONFIG", base)
    # Never write the production scope config: that file is part of the
    # acceptance receipt binding, and a test must not change its hash.
    monkeypatch.setattr(workflow, "SCOPE_CONFIG", tmp_path / "p3_bench_scope.json")
    ns = SimpleNamespace(full=False, tdma_only=False, diagnostic_continue=False,
                         monitor="scope")
    args = workflow.acceptance_args(ns)
    data = json.loads(args.config.read_text(encoding="utf-8"))
    assert data["dpll_short_force_internal"] is False


def test_profile_full_sets_full_flag():
    ns = SimpleNamespace(profile="full", full=False, tdma_only=False,
                         diagnostic_continue=False, monitor=None)
    args = workflow.acceptance_args(ns, launch={})
    assert args.full is True


def test_serial_gap_has_100ms_floor():
    timing = {"serial_timeout_s": 3.0, "input_settle_s": 0.05,
              "output_handoff_s": 0.05}
    command: list[str] = []
    p3.add_serial_timing(command, timing, gap=True)
    assert command[-2:] == ["--gap", "0.1"]
    timing["output_handoff_s"] = 0.2
    command = []
    p3.add_serial_timing(command, timing, gap=True)
    assert command[-2:] == ["--gap", "0.2"]


@pytest.mark.parametrize("page,expected", [
    ("1,0,2,2,0", True),
    ("0,0,1,1,0", False),
    ("1,0,0,1,0", False),
    ("1,0,2,2,1", False),
    ("1,0,2,2", False),
    ("bad", False),
])
def test_dpll_role_is_follower(page, expected):
    assert p3.dpll_role_is_follower(page) is expected


def test_tdma_rerun_discards_the_previous_recorder(tmp_path, fingerprint, monkeypatch):
    commands = []
    monkeypatch.setattr(p3, "_run_step", lambda command, root, log, **kwargs: commands.append(command) or 0)
    run = p3.AcceptanceRun.create(make_args(tmp_path))
    run.put(build_id="b", trn03_matrix_path=tmp_path / "m.json")
    run.execute(24)
    run.execute(24)
    assert "--cancel-unsaved-record" not in commands[0]
    assert "--cancel-unsaved-record" in commands[1]
