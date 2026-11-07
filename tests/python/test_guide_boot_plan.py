from tools.calibration.guide_cmdplane_probe import (
    BOOT_PLAN_FIELDS,
    boot_verdict,
    parse_boot_plan,
)


def ring(enabled=1, nodes=4, up=1, down=1, adapter=1, up_tx=10, down_rx=12):
    values = [0] * 51
    values[0] = enabled
    values[1] = nodes
    values[4] = up
    values[5] = down
    values[8] = adapter
    values[49] = up_tx
    values[50] = down_rx
    return values


def plan(action, slot, have_profile=1, fail_step=0, half_ring=0):
    return {
        "action": action,
        "reason": 0,
        "steps": 7,
        "fail_step": fail_step,
        "listen_pending": 0,
        "join_timeout": 0,
        "have_profile": have_profile,
        "slot": slot,
        "nodes": 4,
        "level": 23,
        "flags": 1,
        "profile_gen": 1,
        "topology_crc": 0x1234,
        "topology_fail": 0,
        "apply_fail": 0,
        "join_frame_seen": 1 if slot else 0,
        "half_ring": half_ring,
        "boot_done": 1,
        "boot_next_step": 7,
    }


def good_set():
    plans = {1: plan(1, 0), 2: plan(2, 1), 3: plan(2, 2), 4: plan(2, 3)}
    rings = {no: ring() for no in plans}
    later = {no: ring(up_tx=20, down_rx=22) for no in plans}
    return plans, rings, later


def test_parse_boot_plan_maps_all_fields() -> None:
    raw = ("1,0,7,0,0,0,1,0,4,23,1,3,4660,0,0,0,0")
    parsed = parse_boot_plan(raw)
    assert list(parsed) == BOOT_PLAN_FIELDS
    assert parsed["action"] == 1
    assert parsed["slot"] == 0
    assert parsed["level"] == 23
    assert parsed["topology_crc"] == 4660


def test_parse_boot_plan_short_readback_is_rejected_by_verdict() -> None:
    parsed = parse_boot_plan("1,0")
    assert parsed["have_profile"] == -1
    ok, reasons = boot_verdict({1: parsed}, {1: ring()})
    assert not ok
    assert any("fail_step" in reason for reason in reasons)


def test_boot_verdict_accepts_zero_port_self_formation() -> None:
    plans, rings, later = good_set()
    ok, reasons = boot_verdict(plans, rings, later)
    assert ok, reasons


def test_boot_verdict_rejects_missing_profile() -> None:
    plans, rings, later = good_set()
    plans[2]["have_profile"] = 0
    ok, reasons = boot_verdict(plans, rings, later)
    assert not ok
    assert any("NO.2 no boot profile" in reason for reason in reasons)


def test_boot_verdict_rejects_failed_boot_step() -> None:
    plans, rings, later = good_set()
    plans[3]["fail_step"] = 4
    ok, reasons = boot_verdict(plans, rings, later)
    assert not ok
    assert any("fail_step=4" in reason for reason in reasons)


def test_boot_verdict_rejects_unfinished_boot_service() -> None:
    plans, rings, later = good_set()
    plans[2]["boot_done"] = 0
    plans[2]["boot_next_step"] = 4
    ok, reasons = boot_verdict(plans, rings, later)
    assert not ok
    assert any("not finished" in reason for reason in reasons)


def test_boot_verdict_rejects_half_ring() -> None:
    plans, rings, later = good_set()
    plans[4]["half_ring"] = 1
    ok, reasons = boot_verdict(plans, rings, later)
    assert not ok
    assert any("half_ring" in reason for reason in reasons)


def test_boot_verdict_rejects_wrong_action() -> None:
    plans, rings, later = good_set()
    plans[1]["action"] = 2  # slot 0 must be the START_REF role
    ok, reasons = boot_verdict(plans, rings, later)
    assert not ok
    assert any("want=1" in reason for reason in reasons)


def test_boot_verdict_rejects_silent_ring() -> None:
    plans, rings, later = good_set()
    later = {no: ring() for no in plans}  # totals do not advance
    ok, reasons = boot_verdict(plans, rings, later)
    assert not ok
    assert any("did not advance" in reason for reason in reasons)


def test_boot_verdict_rejects_ring_down() -> None:
    plans, rings, later = good_set()
    rings[3] = ring(down=0)
    ok, reasons = boot_verdict(plans, rings, later)
    assert not ok
    assert any("enabled/up/down/adapter=1/1/0/1" in reason for reason in reasons)


def test_boot_verdict_rejects_wrong_node_count() -> None:
    plans, rings, later = good_set()
    rings[2] = ring(nodes=3)
    ok, reasons = boot_verdict(plans, rings, later)
    assert not ok
    assert any("nodes=3" in reason for reason in reasons)
