"""Design model for the single-port guide plan (C-segment payload).

NOT firmware. It fixes the plan record that the C segment writes: identity,
segment order, trigger width, delta/arm floors, table sizes and payload
carrier. Mirrors ``docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md``
sections 2 and 9.5/9.6/9.8.

Validation codes:
- MISSING_IDENTITY: build/topology/profile/schedule/calibration identity is zero
- SEGMENT_ORDER: segments are not exactly C,T,R,S,Q
- TRIGGER_NOT_SINGLE_WORD: the trigger must fit one 4 B word (single chunk)
- DELTA_BELOW_FLOOR: delta below the derived 2 s floor (section 9.5)
- ARM_WINDOW_BELOW_FLOOR: arm window below the 1 s floor
- TABLE_TOO_LARGE: a table does not match the node count / candidate budget
- STAGE_LINKS_INCOMPLETE: stage link count != node count
- PAYLOAD_OVERFLOW: plan does not fit the ref slot
"""

from __future__ import annotations

from dataclasses import dataclass, field

SEGMENTS = ("C", "T", "R", "S", "Q")

TRIGGER_WORD_BYTES = 4
DELTA_FLOOR_MS = 2000
ARM_WINDOW_FLOOR_MS = 1000
REF_SLOT_PAYLOAD_MAX = 992
INLINE_PAYLOAD_MAX = 64
PAIR_BYTES = 8
TRIAL_BYTES = 8
LINK_REF_BYTES = 8
HEADER_BYTES = 32


@dataclass
class Plan:
    build: str
    topology_generation: int
    topology_crc32: int
    profile_crc32: int
    schedule_crc32: int
    calibration_generation: int
    node_count: int = 4
    segments: tuple[str, ...] = SEGMENTS
    trigger_words: int = 1
    delta_ms: int = DELTA_FLOOR_MS
    arm_window_ms: int = ARM_WINDOW_FLOOR_MS
    pairs: int = 12
    candidates: int = 6
    repeats: int = 3
    stage_links: int = 4
    link_refs: list[tuple[int, int]] = field(default_factory=list)

    def trials(self) -> int:
        return self.candidates * self.repeats

    def payload_bytes(self) -> int:
        return (
            HEADER_BYTES
            + self.pairs * PAIR_BYTES
            + self.trials() * TRIAL_BYTES
            + self.stage_links * LINK_REF_BYTES
        )

    def carrier(self) -> str:
        size = self.payload_bytes()
        if size <= INLINE_PAYLOAD_MAX:
            return "inline"
        if size <= REF_SLOT_PAYLOAD_MAX:
            return "ref"
        return "overflow"


def validate(plan: Plan) -> list[str]:
    errors: list[str] = []
    if (
        not plan.build
        or plan.topology_generation == 0
        or plan.topology_crc32 == 0
        or plan.profile_crc32 == 0
        or plan.schedule_crc32 == 0
        or plan.calibration_generation == 0
    ):
        errors.append("MISSING_IDENTITY")
    if tuple(plan.segments) != SEGMENTS:
        errors.append("SEGMENT_ORDER")
    if plan.trigger_words != 1 or plan.trigger_words * TRIGGER_WORD_BYTES > 64:
        errors.append("TRIGGER_NOT_SINGLE_WORD")
    if plan.delta_ms < DELTA_FLOOR_MS:
        errors.append("DELTA_BELOW_FLOOR")
    if plan.arm_window_ms < ARM_WINDOW_FLOOR_MS:
        errors.append("ARM_WINDOW_BELOW_FLOOR")
    if plan.pairs != plan.node_count * (plan.node_count - 1):
        errors.append("TABLE_TOO_LARGE")
    if plan.stage_links != plan.node_count:
        errors.append("STAGE_LINKS_INCOMPLETE")
    if plan.carrier() == "overflow":
        errors.append("PAYLOAD_OVERFLOW")
    return errors


def test_valid_plan_is_accepted() -> None:
    plan = Plan(
        build="20261105120000",
        topology_generation=1,
        topology_crc32=0x1234,
        profile_crc32=0x2345,
        schedule_crc32=0x3456,
        calibration_generation=7,
    )
    assert validate(plan) == []
    assert plan.carrier() == "ref"
    assert plan.payload_bytes() <= REF_SLOT_PAYLOAD_MAX


def test_missing_identity_is_rejected() -> None:
    plan = Plan(
        build="",
        topology_generation=1,
        topology_crc32=0,
        profile_crc32=0x2345,
        schedule_crc32=0x3456,
        calibration_generation=7,
    )
    assert "MISSING_IDENTITY" in validate(plan)


def test_trigger_must_fit_one_chunk() -> None:
    plan = Plan(
        build="b",
        topology_generation=1,
        topology_crc32=1,
        profile_crc32=1,
        schedule_crc32=1,
        calibration_generation=1,
        trigger_words=2,
    )
    assert "TRIGGER_NOT_SINGLE_WORD" in validate(plan)


def test_delta_floor_is_enforced() -> None:
    plan = Plan(
        build="b",
        topology_generation=1,
        topology_crc32=1,
        profile_crc32=1,
        schedule_crc32=1,
        calibration_generation=1,
        delta_ms=1500,
    )
    assert "DELTA_BELOW_FLOOR" in validate(plan)


def test_arm_window_floor_is_enforced() -> None:
    plan = Plan(
        build="b",
        topology_generation=1,
        topology_crc32=1,
        profile_crc32=1,
        schedule_crc32=1,
        calibration_generation=1,
        arm_window_ms=500,
    )
    assert "ARM_WINDOW_BELOW_FLOOR" in validate(plan)


def test_pair_table_must_match_node_count() -> None:
    plan = Plan(
        build="b",
        topology_generation=1,
        topology_crc32=1,
        profile_crc32=1,
        schedule_crc32=1,
        calibration_generation=1,
        pairs=20,
    )
    assert "TABLE_TOO_LARGE" in validate(plan)


def test_stage_links_must_cover_every_node() -> None:
    plan = Plan(
        build="b",
        topology_generation=1,
        topology_crc32=1,
        profile_crc32=1,
        schedule_crc32=1,
        calibration_generation=1,
        stage_links=3,
    )
    assert "STAGE_LINKS_INCOMPLETE" in validate(plan)


def test_segments_must_be_ordered() -> None:
    plan = Plan(
        build="b",
        topology_generation=1,
        topology_crc32=1,
        profile_crc32=1,
        schedule_crc32=1,
        calibration_generation=1,
        segments=("C", "R", "T", "S", "Q"),
    )
    assert "SEGMENT_ORDER" in validate(plan)


def test_oversized_ladder_overflows_the_ref_slot() -> None:
    plan = Plan(
        build="b",
        topology_generation=1,
        topology_crc32=1,
        profile_crc32=1,
        schedule_crc32=1,
        calibration_generation=1,
        candidates=200,
        repeats=3,
    )
    assert plan.carrier() == "overflow"
    assert "PAYLOAD_OVERFLOW" in validate(plan)
