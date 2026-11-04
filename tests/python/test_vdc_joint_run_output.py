"""Device-free checks for the tracked joint RUN output layer."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.workflows.vdc_joint_capture import run_output


UIDS = ("u1", "u2", "u3", "u4")
INITIAL = (12000, 16000, 6000)
# The trial must satisfy (plan_ahead - refill_low) >= (steps + 1) * cycle at
# the 2.0 ms core1 cycle, i.e. 10 ms for the 16-edge commit window. The commit
# horizon (commit_ahead - block) is what the starvation evidence points at, so
# the trial keeps the 16-edge block and moves that horizon from 16 to 24 ms.
TRIAL = (36000, 40000, 24000)


def fake_phase(events):
    backend = SimpleNamespace(TDMA_CONTROL_RESULT_FIELDS={})
    parent = SimpleNamespace(
        SYNC="SYNC", fields=lambda raw: raw.strip('"').split(","),
        base=SimpleNamespace(BACKEND=backend))
    follow = SimpleNamespace(parent=parent)

    class PhaseProbe:
        def __init__(self, opt):
            self.opt = opt
            self.boards = [SimpleNamespace(address=uid) for uid in UIDS]
            self.barrier = True
            self.block_queries = False
            self.report = {
                "plan": {}, "boards": {uid: {} for uid in UIDS},
                "errors": [], "tool_sources": [], "passed": True,
            }
            self.timing = {uid: INITIAL for uid in UIDS}
            self.run_generation = {uid: 100 + index for index, uid in enumerate(UIDS)}

        def save(self):
            events.append(("save",))

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": repr(exc)})

        def command(self, board, command, unused=None):
            events.append(("command", board.address, command))
            uid = board.address
            if command == run_output.TIMING_PREFIX + "?":
                return ",".join(map(str, self.timing[uid]))
            if command.startswith(run_output.TIMING_PREFIX + " "):
                value = tuple(map(int, command.split(" ", 1)[1].split(",")))
                self.timing[uid] = value
                return ",".join(map(str, value))
            if command.startswith(run_output.PREFIX + " "):
                return str(self.run_generation[uid])
            if command == run_output.PREFIX + "?":
                row = valid_row()
                plan_ahead, commit_ahead, refill_low = self.timing[uid]
                row.update(
                    generation=self.run_generation[uid], source_retirements=1,
                    model_changes=1, blocks_planned=2,
                    plan_ahead_us=plan_ahead, commit_ahead_us=commit_ahead,
                    refill_low_us=refill_low,
                    anchor_before=1000, anchor_after=1100)
                return encode(row)
            return "OK"

        def control(self, board, text, expected="OK"):
            events.append(("control", board.address, text, expected))
            if text.startswith(run_output.TIMING_PREFIX + " "):
                return self.command(board, text)
            return str(expected)

        def configure(self):
            events.append(("parent_configure",))

        def acquire(self):
            events.append(("parent_acquire",))

        def stop_all(self, label, passes=1):
            events.append(("parent_stop_all", label, passes))
            self.barrier = True
            self.block_queries = False

        def collect(self):
            events.append(("parent_collect",))

        def run(self):
            events.append(("parent_run",))

    return SimpleNamespace(PhaseProbe=PhaseProbe, follow=follow)


def valid_row():
    row = dict.fromkeys(run_output.FIELDS, 0)
    row.update(
        schema=10, timebase=1, block_edges=16, period_ns=1_000_000,
        plan_ahead_us=24000, commit_ahead_us=32000, refill_low_us=16000,
        tick_hz=250_000_000, initial_raw_tick=4_294_967_410,
        initial_local_ns=17_179_869_640, blocks=2, edges=32,
        last_outcome=10, delay_ns=-100)
    return row


def encode(row):
    return ",".join(str(row[key]) for key in run_output.FIELDS)


def options(tmp_path, *, scope=False):
    return SimpleNamespace(out=tmp_path, period_ns=1_000_000, scope=scope)


def test_schema10_parser_accepts_golden_and_rejects_closed_set():
    module = run_output.load_run_output(
        Path("unused.py"), runtime_module=fake_phase([]))
    row = valid_row()
    assert len(module.FIELDS) == 119
    assert module.parse_run_output(encode(row)) == row
    for name, value in (
            ("schema", 9), ("timebase", 0), ("timeline_bridge_samples", 1),
            ("timeline_raw_before", 1), ("maximum_bridge_width_ticks", 1),
            ("initial_local_ns", 0), ("initial_raw_tick", 1 << 64)):
        with pytest.raises(ValueError):
            module.parse_run_output(encode(dict(row, **{name: value})))
    with pytest.raises(ValueError):
        module.parse_run_output(",".join(encode(row).split(",")[:-1]))


def test_run_probe_configure_and_prepare_are_tracked(tmp_path):
    events = []
    phase = fake_phase(events)
    module = run_output.load_run_output(Path("unused.py"), runtime_module=phase)
    probe = module.RunProbe(options(tmp_path))
    probe.configure()
    assert set(probe.timing.values()) == {TRIAL}
    assert all(probe.report["boards"][uid]["output_timing_requested"] == {
        "plan_ahead_us": 12000, "commit_ahead_us": 16000, "refill_low_us": 6000}
        for uid in UIDS)
    assert ("parent_configure",) in events
    assert phase.follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS == {
        run_output.PREFIX.upper(): 1, run_output.TIMING_PREFIX.upper(): 3}
    probe.control(probe.boards[0], "SYNC 7", 7)
    assert [probe.report["boards"][uid]["run_request"] for uid in UIDS] == [
        100, 101, 102, 103]


def test_run_probe_rejects_internal_scope_owner(tmp_path):
    module = run_output.load_run_output(
        Path("unused.py"), runtime_module=fake_phase([]))
    probe = module.RunProbe(options(tmp_path, scope=True))
    with pytest.raises(RuntimeError, match="workflow external_scope"):
        probe.configure()


def test_output_stop_precedes_parent_stop_and_collect_validates_all_boards(tmp_path):
    events = []
    module = run_output.load_run_output(
        Path("unused.py"), runtime_module=fake_phase(events))
    probe = module.RunProbe(options(tmp_path))
    probe.configure()
    probe.control(probe.boards[0], "SYNC 7", 7)
    probe.barrier = False
    probe.block_queries = True
    probe.stop_all("final", passes=2)
    controls = [event for event in events if event[0] == "control"]
    stop_positions = [index for index, event in enumerate(events)
                      if event[0] == "control" and event[2] == run_output.PREFIX + ":STOP"]
    parent_position = events.index(("parent_stop_all", "final", 2))
    assert len(stop_positions) == 4 and max(stop_positions) < parent_position
    probe.collect()
    assert ("parent_collect",) in events
    assert all(probe.report["boards"][uid]["live_output_passed"] for uid in UIDS)
    assert not probe.report["errors"] and probe.report["passed"]
