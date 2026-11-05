"""Design model for the P0T pair scan table and adjacency aggregation.

NOT firmware. It fixes how the 12 directed pairs (4 nodes) are turned into an
accepted ring order, mirroring
``docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`` section 9.6.

Rules modelled:
- the scan covers every ordered pair (driver != receiver);
- a pair is adjacent when the receiver observed any activity;
- only a single closed loop covering every node is accepted (each node has
  exactly one outgoing and one incoming edge);
- the ring order starts at the anchor (reference) and walks the loop;
- open chains, branches, multi-loops and duplicated mutual edges are rejected
  with a named reason.
"""

from __future__ import annotations

from dataclasses import dataclass

OPEN_CHAIN = "OPEN_CHAIN"
BRANCH = "BRANCH"
MULTI_LOOP = "MULTI_LOOP"
DUPLICATE_EDGE = "DUPLICATE_EDGE"
NO_ACTIVITY = "NO_ACTIVITY"
ACCEPTED = "ACCEPTED"


def pair_table(slots: tuple[int, ...]) -> list[tuple[int, int]]:
    """Every ordered pair, the fixed scan order."""
    return [(driver, receiver) for driver in slots for receiver in slots if driver != receiver]


@dataclass
class TopologyVerdict:
    accepted: bool
    reason: str
    ring_order: tuple[int, ...]
    adjacency: dict[int, tuple[int, ...]]


def aggregate(
    slots: tuple[int, ...],
    activity: dict[tuple[int, int], bool],
) -> dict[int, tuple[int, ...]]:
    """Map each driver to the receivers that observed its burst."""
    return {
        driver: tuple(
            receiver
            for receiver in slots
            if receiver != driver and activity.get((driver, receiver), False)
        )
        for driver in slots
    }


def accept_ring(
    slots: tuple[int, ...],
    activity: dict[tuple[int, int], bool],
    anchor: int | None = None,
) -> TopologyVerdict:
    adjacency = aggregate(slots, activity)
    if not any(adjacency.values()):
        return TopologyVerdict(False, NO_ACTIVITY, (), adjacency)
    for driver, receivers in adjacency.items():
        if len(receivers) == 0:
            return TopologyVerdict(False, OPEN_CHAIN, (), adjacency)
        if len(receivers) > 1:
            return TopologyVerdict(False, BRANCH, (), adjacency)
    incoming: dict[int, list[int]] = {slot: [] for slot in slots}
    for driver, receivers in adjacency.items():
        incoming[receivers[0]].append(driver)
    for slot, drivers in incoming.items():
        if len(drivers) > 1:
            return TopologyVerdict(False, DUPLICATE_EDGE, (), adjacency)
        if len(drivers) == 0:
            return TopologyVerdict(False, OPEN_CHAIN, (), adjacency)
    start = min(slots) if anchor is None else anchor
    order: list[int] = []
    current = start
    while len(order) < len(slots):
        if current in order:
            return TopologyVerdict(False, MULTI_LOOP, (), adjacency)
        order.append(current)
        current = adjacency[current][0]
    if current != start or set(order) != set(slots):
        return TopologyVerdict(False, MULTI_LOOP, (), adjacency)
    return TopologyVerdict(True, ACCEPTED, tuple(order), adjacency)


def test_pair_table_is_twelve_ordered_pairs() -> None:
    table = pair_table((0, 1, 2, 3))
    assert len(table) == 12
    assert (0, 1) in table and (1, 0) in table
    assert (2, 2) not in table


def test_perfect_ring_is_accepted_and_ordered() -> None:
    activity = {
        (0, 1): True,
        (1, 2): True,
        (2, 3): True,
        (3, 0): True,
    }
    verdict = accept_ring((0, 1, 2, 3), activity)
    assert verdict.accepted
    assert verdict.reason == ACCEPTED
    assert verdict.ring_order == (0, 1, 2, 3)


def test_open_chain_is_rejected() -> None:
    activity = {(0, 1): True, (1, 2): True, (2, 3): True}
    verdict = accept_ring((0, 1, 2, 3), activity)
    assert not verdict.accepted
    assert verdict.reason == OPEN_CHAIN


def test_branch_is_rejected() -> None:
    activity = {(0, 1): True, (0, 2): True, (1, 3): True, (2, 3): True, (3, 0): True}
    verdict = accept_ring((0, 1, 2, 3), activity)
    assert not verdict.accepted
    assert verdict.reason == BRANCH


def test_multi_loop_is_rejected() -> None:
    activity = {(0, 1): True, (1, 0): True, (2, 3): True, (3, 2): True}
    verdict = accept_ring((0, 1, 2, 3), activity)
    assert not verdict.accepted
    assert verdict.reason in (DUPLICATE_EDGE, MULTI_LOOP)


def test_reverse_ring_is_accepted_with_reversed_order() -> None:
    activity = {(0, 3): True, (3, 2): True, (2, 1): True, (1, 0): True}
    verdict = accept_ring((0, 1, 2, 3), activity)
    assert verdict.accepted
    assert verdict.ring_order == (0, 3, 2, 1)


def test_anchor_rotates_the_order() -> None:
    activity = {(0, 1): True, (1, 2): True, (2, 3): True, (3, 0): True}
    verdict = accept_ring((0, 1, 2, 3), activity, anchor=2)
    assert verdict.accepted
    assert verdict.ring_order == (2, 3, 0, 1)


def test_dead_scan_is_not_accepted() -> None:
    verdict = accept_ring((0, 1, 2, 3), {})
    assert not verdict.accepted
    assert verdict.reason == NO_ACTIVITY
