"""Real libscpi ANGLE configuration, readback and binding failure cases."""
import pytest

from test_sequence_scpi_config import parser, run  # noqa: F401


SETUP = ["@position", "CONF:ANGLE:SWEEP -1,1,1,1", "CONF:ANGLE:INPUT IN1,50"]


def test_sweep_input_apply_threshold_finite_count_and_live_position(parser):
    rows = run(parser, SETUP + ["READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?",
        "READ:ANGLE:SPEED?", "READ:ANGLE:POSITION?", "@first", "READ:ANGLE:POSITION?",
        "@last", "READ:ANGLE:POSITION?"])
    assert all(row["errors"] == 0 for row in rows)
    assert rows[2]["fields"] == ["-1", "1", "1", "1", "3", "1"]
    assert rows[3]["fields"] == ["IN1", "50", "50", "50", "1", "1", "1"]
    assert rows[4]["fields"] == ["1"]
    assert rows[5]["fields"] == ["POSITION", "0", "3", "0", "-1", "0", "0", "0", "1", "0", "1"]
    assert rows[6]["fields"] == ["POSITION", "1", "3", "-1", "0", "52", "2", "1", "1", "0", "1"]
    assert rows[7]["fields"] == ["POSITION", "3", "3", "1", "0", "150", "0", "1", "0", "0", "1"]


def test_reverse_input_first_and_speed_metadata(parser):
    rows = run(parser, ["@position", "CONF:ANGLE:INPUT IN3,1000", "READ:ANGLE:INPUT?",
        "CONF:ANGLE:SWEEP 1,0,-0.25,5", "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?",
        "CONF:ANGLE:SPEED 10", "READ:ANGLE:INPUT?", "READ:ANGLE:SPEED?",
        "@first", "READ:ANGLE:POSITION?"])
    assert all(row["errors"] == 0 for row in rows)
    assert rows[1]["fields"] == ["IN3", "1000", "0", "0", "0", "0", "0"]
    assert rows[3]["fields"] == ["1", "0", "-0.25", "5", "5", "1"]
    assert [float(x) for x in rows[4]["fields"][1:]] == [1000, 5000, 250, .05, 20, 1]
    assert [float(x) for x in rows[6]["fields"][1:]] == [1000, 10000, 250, .025, 40, 1]
    assert rows[7]["fields"] == ["10"]
    assert rows[8]["fields"][3:5] == ["1", "0.75"]


@pytest.mark.parametrize("bad", [
    "CONF:ANGLE:SWEEP 0,1,1", "CONF:ANGLE:SWEEP 0,1,0,1", "CONF:ANGLE:SWEEP 0,1,-1,1",
    "CONF:ANGLE:SWEEP 0,1,0.3,1", "CONF:ANGLE:SWEEP 0,1,1,0", "CONF:ANGLE:SWEEP 0,1,1,-1",
    "CONF:ANGLE:SWEEP 0,1,1,1,", "CONF:ANGLE:SWEEP NAN,1,1,1",
    "CONF:ANGLE:SWEEP 0,1,1,INF", "CONF:ANGLE:SWEEP 0,1e100,1,1",
    "CONF:ANGLE:SWEEP 0,1e-20,1,1",
    "CONF:ANGLE:INPUT IN2,50", "CONF:ANGLE:INPUT IN0,50", "CONF:ANGLE:INPUT IN5,50",
    "CONF:ANGLE:INPUT IN1,0", "CONF:ANGLE:INPUT IN1,-1", "CONF:ANGLE:INPUT IN1,1.5",
    "CONF:ANGLE:INPUT IN1,4294967262", "CONF:ANGLE:INPUT IN1,1e200",
    "CONF:ANGLE:INPUT IN1,50,50", "CONF:ANGLE:INPUT IN1,50,",
    "CONF:ANGLE:SPEED 0", "CONF:ANGLE:SPEED -1", "CONF:ANGLE:SPEED 1e308",
    "CONF:ANGLE:SPEED 1,", "CONF:ANGLE:PULSE RISING,10,30000",
    "CONF:ANGLE:BREAKPOINT 1", "CONF:ANGLE:BREAKPOINT:CLEAR", "READ:ANGLE:PULSE?",
    "READ:ANGLE:BREAKPOINT?", "READ:ANGLE:SWEEP? 1", "READ:ANGLE:INPUT? 1",
    "READ:ANGLE:POSITION? 1", "READ:ANGLE:SPEED? 1",
])
def test_invalid_mutation_and_queries_preserve_binding(parser, bad):
    rows = run(parser, SETUP + ["READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?", bad,
                              "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?"])
    assert rows[4]["errors"] > 0
    assert rows[2]["fields"] == rows[5]["fields"]
    assert rows[3]["fields"] == rows[6]["fields"]


@pytest.mark.parametrize("guard", ["@freeze", "@legacy", "@linkfail"])
def test_busy_and_backend_failure_are_atomic(parser, guard):
    rows = run(parser, SETUP + ["READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?", guard,
        "CONF:ANGLE:SWEEP 0,3,1,5", "CONF:ANGLE:INPUT IN3,100",
        "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?"])
    assert rows[4]["errors"] and rows[5]["errors"]
    assert rows[2]["fields"] == rows[6]["fields"]
    assert rows[3]["fields"] == rows[7]["fields"]


@pytest.mark.parametrize("change", ["@position", "@repeat", "@modelchange"])
def test_external_configuration_invalidates_angle_binding(parser, change):
    rows = run(parser, SETUP + [change, "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?",
        "READ:ANGLE:POSITION?", "CONF:ANGLE:SPEED 5", "READ:ANGLE:SWEEP?"])
    assert all(row["errors"] == 0 for row in rows)
    assert rows[2]["fields"][-1] == rows[3]["fields"][-1] == rows[4]["fields"][-1] == "0"
    assert rows[4]["fields"][7:9] == ["0", "0"]
    assert rows[6]["fields"][-1] == "0"  # metadata SPEED does not reactivate stale angles


def test_unbound_pair_cannot_pretend_to_configure_hardware(parser):
    rows = run(parser, ["CONF:ANGLE:SWEEP 0,1,1,1", "CONF:ANGLE:INPUT IN1,50",
        "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?"])
    assert rows[0]["errors"] == 0 and rows[1]["errors"]
    assert rows[2]["fields"][-1] == "0"
    assert rows[3]["fields"] == ["NONE", "0", "0", "0", "0", "0", "0"]


def test_new_position_binding_starts_fresh_sweep_draft(parser):
    rows = run(parser, SETUP + ["@position", "CONF:ANGLE:SWEEP 0,.02,.01,1",
        "READ:ANGLE:INPUT?", "CONF:ANGLE:INPUT IN1,1000", "READ:ANGLE:INPUT?",
        "READ:ANGLE:SWEEP?"])
    assert all(row["errors"] == 0 for row in rows)
    assert rows[3]["fields"] == ["NONE", "0", "0", "0", "0", "0", "0"]
    assert rows[5]["fields"][0:4] == ["IN1", "1000", "1000", "10"]
    assert rows[6]["fields"][-2:] == ["3", "1"]


def test_plan_step_count_overflow_rejected_before_commit(parser):
    rows = run(parser, ["CONF:TRIG 8,0,1,1", "CONF:SEQ A,0,1,2,3,4,5,6,7", "CONF:SEQ:ACT A",
        "@position", "CONF:ANGLE:SWEEP 0,999999999,1,1", "CONF:ANGLE:INPUT IN1,1",
        "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?"])
    assert rows[4]["errors"] > 0
    assert rows[5]["fields"][-1] == "0"
    assert rows[6]["fields"][0] == "NONE"
