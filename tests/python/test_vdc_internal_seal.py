"""Characterize the tracked internal-summary/startup/seal adapter layers."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.vdc_priority_trace import vdc_priority_trace_capture as helper
from tools.workflows.vdc_joint_capture import internal_seal


UIDS = ("u1", "u2", "u3", "u4")


def bridge_raw(call_index):
    values = {name: 0 for name in internal_seal.BRIDGE_FIELDS}
    raw_before = 1_000_000 + call_index * 20_000_000_000
    values.update(
        schema=1, platform_supported=1, configuration_supported=1,
        bridge_valid=1, clock_ready=1, expected_hz=250_000_000,
        cached_hz=250_000_000, sdk_sys_hz=250_000_000,
        timer0_sample_valid=1, timer1_sample_valid=1,
        timer0_sample_us=raw_before // 250,
        timer1_sample_ticks=raw_before + 1,
        raw_before=raw_before, local_ns=(raw_before // 250) * 1000,
        raw_after=raw_before + 2, tick_hz=250_000_000)
    return ",".join(str(values[name]) for name in internal_seal.BRIDGE_FIELDS)


def fake_runtime(events):
    backend = SimpleNamespace(TDMA_CONTROL_RESULT_FIELDS={})
    parent = SimpleNamespace(
        SYNC="SYNC", MODEL="MODEL?", TX="TX?",
        base=SimpleNamespace(BACKEND=backend))

    class FollowProbe:
        @staticmethod
        def control(probe, board, command, expected):
            events.append(("follow_control", board.address, command, expected))
            return str(expected)

    follow = SimpleNamespace(parent=parent, FollowProbe=FollowProbe, STATUS="FOLLOW?")
    phase = SimpleNamespace(PHASE="PHASE")

    class ExternalProbe:
        def __init__(self, opt):
            self.opt = opt
            self.boards = [SimpleNamespace(address=uid) for uid in UIDS]
            self.report = {
                "plan": {}, "boards": {uid: {} for uid in UIDS},
                "errors": [], "passed": True, "tool_sources": [],
                "binding_generation": 7, "queries_during_acquisition": 0,
            }
            self.block_queries = False
            self.barrier = True
            self.bridge_calls = {uid: 0 for uid in UIDS}

        def save(self):
            events.append(("save",))

        def trace_status(self, board):
            events.append(("trace_status", board.address))
            return {"state": 0, "capture_id": UIDS.index(board.address)}

        def trace_ack(self, board, command, state, capture_id=None):
            origin = board == self.boards[0]
            return {"status": {
                "schema": helper.summary_schema(origin, self.opt.summary_interval_ms),
                "sample_interval_ms": self.opt.summary_interval_ms or 1000,
                "command": command, "state": state, "capture_id": capture_id,
            }}

        def command(self, board, text, unused=None):
            events.append(("command", board.address, text))
            if text == internal_seal.BRIDGE_QUERY:
                call_index = self.bridge_calls[board.address]
                self.bridge_calls[board.address] += 1
                return bridge_raw(call_index)
            if text.startswith("CALibration:ORIGin:TRIAL"):
                return "2"
            return "OK"

        def control(self, board, text, expected="OK"):
            events.append(("external_control", board.address, text, expected))
            return str(expected)

        def error(self, scope, exc):
            self.report["errors"].append({"scope": scope, "error": repr(exc)})

        def run(self):
            events.append(("inherited_run",))
            self.report["strict_replan_qualification"] = {
                "criteria_passed": True, "base_health_passed": True, "passed": True}
            self.report["passed"] = False

    class RunProbe:
        @staticmethod
        def configure(probe):
            events.append(("base_configure",))
            probe.report["model_before_start"] = probe.command(probe.boards[0], parent.MODEL)
            probe.control(probe.boards[0], "SYNC 7", 7)

    def parallel_start(probe):
        events.append(("parallel_start", probe.scope.quiet, probe.block_queries, probe.barrier))

    capture = SimpleNamespace(
        FIELDS=tuple(f"field_{index}" for index in range(119)),
        WIDE_FIELDS=set(), RunProbe=RunProbe, PREFIX="RUN")
    return SimpleNamespace(
        Probe=ExternalProbe, base=SimpleNamespace(), capture=capture,
        follow=follow, phase=phase,
        startup=SimpleNamespace(parallel_start=parallel_start))


def options(tmp_path):
    migrated = {
        "internal_seal": {"path": "old-seal.py", "sha256": "a" * 64},
        "quiet_startup": {"path": "old-startup.py", "sha256": "b" * 64},
        "internal_summary": {"path": "old-summary.py", "sha256": "c" * 64},
    }
    return SimpleNamespace(
        out=tmp_path, seconds=60, summary_interval_ms=10000,
        no_reference=False, expire_output=False,
        workflow_inputs_evidence={"adapter": {"migrated_sources": migrated}})


def test_tracked_configure_omits_model_and_arms_all_summary_traces(tmp_path):
    events = []
    runtime = fake_runtime(events)
    adapter = internal_seal.load_adapter(Path("unused.py"), runtime_module=runtime)
    probe = adapter.Probe(options(tmp_path))
    probe.configure()
    assert probe.report["model_before_start"] is None
    omitted = probe.report["startup_adapter"]["omitted_optional_queries"]
    assert omitted == [{
        "uid": UIDS[0], "command": "MODEL?", "transmitted": False,
        "reason": "Optional management snapshot is not a START prerequisite; native evidence is read after STOP.",
    }]
    assert probe.report["startup_adapter"]["base_sha256"] == "c" * 64
    assert probe.report["plan"]["internal_trace"] is True
    assert probe.report["guard_plan"]["scope"] is False
    assert all(probe.report["boards"][uid]["host_raw_anchors"]["pre"]["bridge"]
               ["bridge_valid"] == 1 for uid in UIDS)
    assert probe.report["plan"]["runtime_anchor_queries"] == 0
    assert probe.report["plan"]["core1_probe_work_added"] is False
    assert probe.report["plan"]["realtime_resource_seat_added"] is False
    assert len([event for event in events if event[0] == "follow_control"]) == 4
    assert all(probe.report["boards"][uid]["trace_arm_requested"] for uid in UIDS)
    assert not any(event[:3] == ("command", UIDS[0], "MODEL?") for event in events)


def test_tracked_command_rewrites_guard_and_expiry(tmp_path):
    events = []
    runtime = fake_runtime(events)
    adapter = internal_seal.load_adapter(
        Path("unused.py"), runtime_module=runtime)
    opt = options(tmp_path)
    opt.expire_output = True
    probe = adapter.Probe(opt)
    trace = helper.TRACE_ROOT
    probe.command(probe.boards[1], f"{trace}:SUMMary:PHASe 77,10000")
    probe.command(probe.boards[3], "SYSTem:VDC:OUTPut:RUN 7,1000,2000,0")
    commands = [event[2] for event in events if event[0] == "command"]
    assert f"{trace}:GUARd:PHASe 77,60" in commands
    assert "SYSTem:VDC:OUTPut:RUN 7,1000,2000,20000" in commands
    assert runtime.follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[
        f"{trace}:GUARD:PHASE".upper()] == 1


def test_stop_only_bridge_anchors_are_ordered_and_configuration_bound(tmp_path):
    events = []
    adapter = internal_seal.load_adapter(
        Path("unused.py"), runtime_module=fake_runtime(events))
    probe = adapter.Probe(options(tmp_path))
    pre = probe.capture_host_raw_anchor(probe.boards[0], "pre")
    post = probe.capture_host_raw_anchor(probe.boards[0], "post")
    assert pre["bridge"]["raw_after"] < post["bridge"]["raw_before"]
    assert internal_seal.bridge_configuration(pre["bridge"]) == \
        internal_seal.bridge_configuration(post["bridge"])
    probe.block_queries = True
    probe.barrier = False
    with pytest.raises(RuntimeError, match="all-board STOP"):
        probe.capture_host_raw_anchor(probe.boards[0], "during")


def test_bridge_parser_rejects_invalid_clock_or_enclosure():
    values = bridge_raw(0).split(",")
    values[internal_seal.BRIDGE_FIELDS.index("cached_hz")] = "150000000"
    with pytest.raises(ValueError, match="clock identities"):
        internal_seal.parse_bridge(",".join(values))
    values = bridge_raw(0).split(",")
    values[internal_seal.BRIDGE_FIELDS.index("raw_after")] = values[
        internal_seal.BRIDGE_FIELDS.index("raw_before")]
    with pytest.raises(ValueError, match="enclosure"):
        internal_seal.parse_bridge(",".join(values))


def test_tracked_quiet_acquire_and_collect_stop_gate(tmp_path, monkeypatch):
    events = []
    adapter = internal_seal.load_adapter(
        Path("unused.py"), runtime_module=fake_runtime(events))
    probe = adapter.Probe(options(tmp_path))
    monkeypatch.setattr(internal_seal.time, "sleep", lambda seconds: events.append(("sleep", seconds)))
    monkeypatch.setattr(internal_seal.helper, "quiet_wait",
                        lambda seconds: events.append(("quiet_wait", seconds)))
    probe.acquire()
    assert ("parallel_start", True, True, False) in events
    assert ("quiet_wait", 62) in events
    assert probe.report["trial_epoch"] == 2
    probe.barrier = False
    with pytest.raises(RuntimeError, match="all-board STOP"):
        probe.collect()


def test_tracked_run_recomputes_qualification_after_inherited_cleanup(tmp_path):
    events = []
    adapter = internal_seal.load_adapter(
        Path("unused.py"), runtime_module=fake_runtime(events))
    probe = adapter.Probe(options(tmp_path))
    probe.run()
    qualification = probe.report["strict_replan_qualification"]
    assert qualification["criteria_passed"] is True
    assert qualification["base_health_passed"] is False
    assert qualification["passed"] is False
    assert "No runtime queries or scope acquisition" in probe.report["conclusion_scope"]
