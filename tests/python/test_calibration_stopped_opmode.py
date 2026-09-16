"""STOP-only APPLY recovery must distinguish refusal from unknown application."""
from argparse import Namespace
from collections import deque
import json

import pytest

from tools.calibration_ring_validate import calibration_clk_train as coarse
from tools.tdma_ring_monitor.tdma_start_ring import Board


PROFILE = (7, 10000000, 1000000, 4096, 0, 1383759744)
BOARD = Board("P", "A", "", "BUILD")


def options():
    return Namespace(arm_wait=.05, idle_poll_interval=0., gap=0.)


class Bench:
    def __init__(self, outcomes=("success",)):
        self.outcomes = deque(outcomes)
        self.active = PROFILE
        self.staged = PROFILE
        self.stage_count, self.apply_count, self.reject_count, self.last = 7, 11, 3, 0
        self.errors = deque()
        self.calls = []
        self.applies = 0
        self.queries = 0
        self.config = 19
        self.query_hook = None
        self.stop_hook = None

    def command(self, board, text, args):
        self.calls.append(text)
        if text == "SYSTem:TDMA:RING:STATus?":
            fields = dict.fromkeys(coarse.RUNTIME_FIELDS, 0)
            fields.update(ring_config_seq=self.config, ring_applied_config_seq=self.config)
            if self.stop_hook:
                self.stop_hook(self, fields)
            return ",".join(str(fields[k]) for k in coarse.RUNTIME_FIELDS)
        if text == "SYSTem:TDMA:OPMode?":
            self.queries += 1
            if self.query_hook:
                raw = self.query_hook(self)
                if raw is not None:
                    return raw
            return ",".join(map(str, (*self.active, *self.staged, self.stage_count,
                                      self.apply_count, self.reject_count, self.last)))
        if text == "SYSTem:ERRor?":
            return self.errors.popleft() if self.errors else '0,"No error"'
        if text == "SYSTem:TDMA:OPMode:APPLy":
            self.applies += 1
            outcome = self.outcomes.popleft()
            if outcome in ("entry_busy", "service_busy", "busy_run", "multiple_errors"):
                if outcome in ("service_busy", "busy_run"):
                    self.reject_count = (self.reject_count + 1) & 0xffffffff
                    self.last = 1 if outcome == "service_busy" else 3
                self.errors.append('-200,"Execution error"')
                if outcome == "multiple_errors":
                    self.errors.append('-200,"another command"')
                return "<timeout>"
            if outcome == "transport_exception":
                raise OSError("connection lost")
            if outcome == "unknown":
                return "<timeout>"
            if outcome == "other_error":
                self.errors.append('-222,"Data out of range"')
                return "<timeout>"
            self.active = self.staged
            self.apply_count = (self.apply_count + 1) & 0xffffffff
            self.last = 0
            if outcome == "lost_success":
                return "<timeout>"
            if outcome == "negative_after_apply":
                self.errors.append('-200,"Execution error"')
                return "<timeout>"
            if outcome == "wrong_ack":
                return "7,10000000,1000000,4096,0,99"
            if outcome == "synthetic_ack":
                return "OK(no payload; verified by state readback)"
            return ",".join(map(str, PROFILE))
        raise AssertionError(text)


def invoke(monkeypatch, bench, actions=None):
    monkeypatch.setattr(coarse, "board_command", bench.command)
    monkeypatch.setattr(coarse.time, "sleep", lambda _: None)
    if actions is None:
        actions = []
    return coarse._apply_stopped_opmode(BOARD, PROFILE, options(), actions)


@pytest.mark.parametrize("outcomes", [
    ("success",), ("entry_busy", "success"), ("service_busy", "success"),
    ("entry_busy", "service_busy", "success"),
])
def test_same_profile_bounded_apply_requires_fresh_stop_and_counter(monkeypatch, outcomes):
    bench = Bench(outcomes)
    actions = []
    assert invoke(monkeypatch, bench, actions) == PROFILE
    assert bench.applies == len(outcomes)
    assert bench.calls.count("SYSTem:TDMA:RING:STATus?") == 2 * len(outcomes)
    assert actions[-1]["passed"]
    assert all(not row["passed"] for row in actions[:-1])
    assert all(row["error_after"].startswith("-200,") for row in actions[:-1])
    assert all(row["stopped_before"]["ring_config_seq"] == 19 and
               row["stopped_after"]["ring_config_seq"] == 19 for row in actions)
    assert all(row["expected_profile"] == PROFILE for row in actions)
    assert bench.calls.count("SYSTem:TDMA:OPMode:STAGe 7") == 0


def test_permanent_refusal_stops_after_three_and_keeps_originals(monkeypatch):
    bench = Bench(("entry_busy",) * 3)
    actions = []
    with pytest.raises(RuntimeError, match="limit"):
        invoke(monkeypatch, bench, actions)
    assert bench.applies == 3 and len(actions) == 3
    assert all(row["response"] == "<timeout>" and not row["passed"] for row in actions)


@pytest.mark.parametrize("outcome", [
    "unknown", "lost_success", "negative_after_apply", "other_error", "wrong_ack",
    "synthetic_ack", "transport_exception", "multiple_errors", "busy_run",
])
def test_unknown_or_unattributable_or_nontransient_never_retries(monkeypatch, outcome):
    bench = Bench((outcome, "success"))
    actions = []
    with pytest.raises(RuntimeError):
        invoke(monkeypatch, bench, actions)
    assert bench.applies == 1 and len(actions) == 1 and not actions[0]["passed"]
    assert "error" in actions[0]


@pytest.mark.parametrize("field", ["stage_count", "apply_count", "reject_count", "last", "active", "staged"])
def test_post_ack_counter_and_profile_drift_rejected(monkeypatch, field):
    bench = Bench()
    def mutate(b):
        if b.queries == 2:
            if field in ("active", "staged"):
                setattr(b, field, PROFILE[:-1] + (99,))
            else:
                setattr(b, field, getattr(b, field) + 1)
    bench.query_hook = mutate
    with pytest.raises(RuntimeError):
        invoke(monkeypatch, bench)
    assert bench.applies == 1


@pytest.mark.parametrize("field", ["stage_count", "apply_count", "reject_count", "last", "active", "staged"])
def test_between_attempt_drift_prevents_second_apply(monkeypatch, field):
    bench = Bench(("entry_busy", "success"))
    def mutate(b):
        if b.queries == 3:
            if field in ("active", "staged"):
                setattr(b, field, PROFILE[:-1] + (99,))
            else:
                setattr(b, field, getattr(b, field) + 1)
    bench.query_hook = mutate
    with pytest.raises(RuntimeError):
        invoke(monkeypatch, bench)
    assert bench.applies == 1


def test_unchanged_apply_count_cannot_confirm_already_matching_profile(monkeypatch):
    bench = Bench()
    bench.query_hook = lambda b: setattr(b, "apply_count", 11) if b.queries == 2 else None
    with pytest.raises(RuntimeError, match="completion"):
        invoke(monkeypatch, bench)


def test_exact_uint32_counter_wrap_is_supported(monkeypatch):
    bench = Bench()
    bench.apply_count = 0xffffffff
    assert invoke(monkeypatch, bench) == PROFILE
    assert bench.apply_count == 0


@pytest.mark.parametrize("raw", ["<timeout>", "OK", ",".join(["0"] * 15), ",".join(["-1"] * 16)])
def test_invalid_initial_snapshot_cannot_apply(monkeypatch, raw):
    bench = Bench()
    bench.query_hook = lambda _: raw
    with pytest.raises(RuntimeError):
        invoke(monkeypatch, bench)
    assert bench.applies == 0


def test_stale_errors_are_recorded_and_drained_before_apply(monkeypatch):
    bench = Bench()
    bench.errors.extend(['-200,"old error"', '-222,"old range"'])
    actions = []
    assert invoke(monkeypatch, bench, actions) == PROFILE
    assert actions[0]["errors_drained_before"] == [
        '-200,"old error"', '-222,"old range"', '0,"No error"']


@pytest.mark.parametrize("errors", [["<timeout>"], ['-200,"old error"'] * 16])
def test_unclean_error_queue_prevents_apply(monkeypatch, errors):
    bench = Bench()
    bench.errors.extend(errors)
    with pytest.raises(RuntimeError):
        invoke(monkeypatch, bench)
    assert bench.applies == 0


def test_config_drift_during_apply_is_not_hidden_by_stopped_state(monkeypatch):
    bench = Bench(("service_busy", "success"))
    def change(b, fields):
        if b.applies:
            fields["ring_config_seq"] = fields["ring_applied_config_seq"] = 20
    bench.stop_hook = change
    with pytest.raises(RuntimeError, match="configuration"):
        invoke(monkeypatch, bench)
    assert bench.applies == 1


def test_fresh_stop_failure_prevents_next_apply(monkeypatch):
    bench = Bench(("entry_busy", "success"))
    reads = []
    def stopped(*_):
        reads.append(True)
        if len(reads) == 3:
            raise RuntimeError("STOP lost")
        return dict(ring_enabled=0, ring_adapter_started=0,
                    ring_config_seq=19, ring_applied_config_seq=19)
    monkeypatch.setattr(coarse, "wait_ring_stopped", stopped)
    with pytest.raises(RuntimeError, match="STOP lost"):
        invoke(monkeypatch, bench)
    assert bench.applies == 1


def test_main_preparation_uses_real_helper_before_acquisition(monkeypatch, tmp_path):
    boards = [BOARD, Board("Q", "B", "", "BUILD")]
    benches = {b.address: Bench(("entry_busy", "success")) for b in boards}
    args = Namespace(**vars(options()), board_id=["A", "B"], expected_build="BUILD", level=7,
        pulse_start=10, pulse_limit=100, growth_factor=10, repeats=1, binary_refine=True,
        short_open=False, probe_phase_cycles=2, dry_run=False, out_dir=tmp_path)
    cleanup = []
    def command(board, text, a):
        if text == "SYSTem:TDMA:RING:STOP":
            return "OK"
        if text == "CALibration:TRAINing:STAGe:CLEar":
            return "OK"
        if text == "CALibration:TOPology:PROBe 1,2":
            return "1,2"
        if text == "SYSTem:TDMA:OPMode:STAGe 7":
            return ",".join(map(str, PROFILE))
        if text == "CALibration:TOPology:PROBe 0":
            cleanup.append(board.address)
            return "0,0"
        return benches[board.address].command(board, text, a)
    def acquire(*_):
        assert all(b.applies == 2 for b in benches.values())
        raise RuntimeError("intentional acquisition stop after checked preparation")
    monkeypatch.setattr(coarse, "parse_args", lambda: args)
    monkeypatch.setattr(coarse, "discover", lambda _: {b.address: b for b in boards})
    monkeypatch.setattr(coarse, "board_command", command)
    monkeypatch.setattr(coarse, "acquire_reference_node", acquire)
    assert coarse.main() == 1
    report = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    assert "intentional acquisition stop" in report["error"]
    applied = [r for r in report["preparation_actions"] if r.get("disposition") ==
               "APPLIED_AND_READ_BACK_WHILE_STOPPED"]
    assert {r["board"] for r in applied} == {"A", "B"}
    assert set(cleanup) == {"A", "B"}
