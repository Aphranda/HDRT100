"""Design model for single-port guide formation (leader-first join).

NOT firmware. It fixes the state machine, timing and failure codes that the
TDMA adapter change must implement, so the firmware work has an executable
acceptance baseline. Mirrors
``docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`` section 9.1
(option B: coordinator transmits first, followers join by listening).

Rules modelled:
- coordinator boots straight to RUNNING and transmits in its slot;
- a follower boots to BOOT_LISTEN with TX disabled;
- a follower joins after hearing the coordinator frame, then becomes RUNNING;
- join is allowed while ``cycle <= join_window_cycles`` (inclusive);
- the first cycle after the window with no join is JOIN_TIMEOUT;
- a missing slot is reported as MISSING_SLOT and status DEGRADED, never
  silently healthy;
- claiming a slot already held by another running board is SLOT_CONFLICT.
"""

from __future__ import annotations

from dataclasses import dataclass, field

BOOT_LISTEN = "BOOT_LISTEN"
JOINED = "JOINED"
ARMED = "ARMED"
RUNNING = "RUNNING"
STOPPED = "STOPPED"
FAULT = "FAULT"

JOIN_TIMEOUT = "JOIN_TIMEOUT"
SLOT_CONFLICT = "SLOT_CONFLICT"
MISSING_SLOT = "MISSING_SLOT"

STATUS_FORMING = "FORMING"
STATUS_READY = "READY"
STATUS_DEGRADED = "DEGRADED"


@dataclass
class Board:
    slot: int
    is_coordinator: bool = False
    state: str = BOOT_LISTEN
    reason: str = ""
    joined_cycle: int = -1


@dataclass
class Formation:
    expected_slots: tuple[int, ...]
    join_window_cycles: int = 2
    boards: list[Board] = field(default_factory=list)
    cycle_index: int = -1
    status: str = STATUS_FORMING
    missing_slots: tuple[int, ...] = ()
    reason: str = ""

    def boot(self) -> None:
        self.boards = []
        self.cycle_index = -1
        self.status = STATUS_FORMING
        self.missing_slots = ()
        self.reason = ""
        coordinator_slot = min(self.expected_slots)
        self.boards.append(
            Board(
                slot=coordinator_slot,
                is_coordinator=True,
                state=RUNNING,
                joined_cycle=0,
            )
        )
        for slot in self.expected_slots:
            if slot != coordinator_slot:
                self.boards.append(Board(slot=slot))

    def by_slot(self, slot: int) -> Board | None:
        for board in self.boards:
            if board.slot == slot:
                return board
        return None

    def claim(self, slot: int, board: Board) -> bool:
        """Attach ``board`` to ``slot``; reject a slot already held."""
        holder = self.by_slot(slot)
        if holder is not None and holder is not board and holder.state in (
            JOINED,
            ARMED,
            RUNNING,
        ):
            board.state = FAULT
            board.reason = SLOT_CONFLICT
            self.reason = SLOT_CONFLICT
            self.status = STATUS_DEGRADED
            return False
        board.slot = slot
        return True

    def cycle(self, heard: set[int] | None = None) -> None:
        """Advance one ring cycle.

        ``heard`` is the set of slots whose frames arrived. The same set is
        applied to every follower; per-follower link failure is modelled by
        calling ``fail`` for that slot.
        """
        self.cycle_index += 1
        coordinator_slot = min(self.expected_slots)
        heard = heard if heard is not None else {coordinator_slot}
        for board in self.boards:
            if board.is_coordinator or board.state != BOOT_LISTEN:
                continue
            if coordinator_slot in heard and self.cycle_index <= self.join_window_cycles:
                board.state = RUNNING
                board.joined_cycle = self.cycle_index
            elif self.cycle_index > self.join_window_cycles:
                board.state = STOPPED
                board.reason = JOIN_TIMEOUT
        self.missing_slots = tuple(
            slot
            for slot in self.expected_slots
            if (board := self.by_slot(slot)) is None or board.state != RUNNING
        )
        if self.missing_slots and self.cycle_index > self.join_window_cycles:
            self.status = STATUS_DEGRADED
            self.reason = self.reason or MISSING_SLOT
        elif not self.missing_slots:
            self.status = STATUS_READY
            self.reason = ""

    def fail(self, slot: int, reason: str) -> None:
        board = self.by_slot(slot)
        if board is None:
            raise KeyError(slot)
        board.state = STOPPED
        board.reason = reason

    def running_slots(self) -> tuple[int, ...]:
        return tuple(b.slot for b in self.boards if b.state == RUNNING)


def test_all_boards_join_within_window() -> None:
    formation = Formation(expected_slots=(0, 1, 2, 3))
    formation.boot()
    for _ in range(3):
        formation.cycle(heard={0})
    assert formation.status == STATUS_READY
    assert formation.missing_slots == ()
    assert formation.reason == ""
    assert formation.running_slots() == (0, 1, 2, 3)
    assert [b.joined_cycle for b in formation.boards] == [0, 0, 0, 0]


def test_join_timeout_stops_board_and_reports_missing_slot() -> None:
    formation = Formation(expected_slots=(0, 1, 2, 3))
    formation.boot()
    for _ in range(4):
        formation.cycle(heard=set())
    assert formation.status == STATUS_DEGRADED
    assert formation.missing_slots == (1, 2, 3)
    assert formation.reason == MISSING_SLOT
    followers = [b for b in formation.boards if not b.is_coordinator]
    assert all(b.state == STOPPED and b.reason == JOIN_TIMEOUT for b in followers)


def test_join_window_boundary_is_inclusive() -> None:
    joined_at_window = Formation(expected_slots=(0, 1), join_window_cycles=2)
    joined_at_window.boot()
    joined_at_window.cycle(heard=set())
    joined_at_window.cycle(heard=set())
    joined_at_window.cycle(heard={0})
    assert joined_at_window.by_slot(1).joined_cycle == 2
    assert joined_at_window.status == STATUS_READY

    missed_window = Formation(expected_slots=(0, 1), join_window_cycles=2)
    missed_window.boot()
    for _ in range(4):
        missed_window.cycle(heard=set())
    assert missed_window.by_slot(1).state == STOPPED
    assert missed_window.by_slot(1).reason == JOIN_TIMEOUT


def test_partial_formation_is_degraded_not_ready() -> None:
    formation = Formation(expected_slots=(0, 1, 2, 3))
    formation.boot()
    for _ in range(3):
        formation.cycle(heard={0})
    formation.fail(3, "LINK_OPEN")
    formation.cycle(heard={0})
    assert formation.status == STATUS_DEGRADED
    assert 3 in formation.missing_slots
    assert formation.by_slot(0).state == RUNNING


def test_slot_conflict_is_fatal_for_the_claimant() -> None:
    formation = Formation(expected_slots=(0, 1, 2, 3))
    formation.boot()
    for _ in range(3):
        formation.cycle(heard={0})
    intruder = Board(slot=1)
    assert formation.claim(1, formation.by_slot(1)) is True
    assert formation.claim(1, intruder) is False
    assert intruder.state == FAULT
    assert intruder.reason == SLOT_CONFLICT
    assert formation.status == STATUS_DEGRADED


def test_coordinator_alone_never_reports_ready() -> None:
    formation = Formation(expected_slots=(0, 1, 2, 3))
    formation.boot()
    for _ in range(5):
        formation.cycle(heard={0})
    assert formation.running_slots() == (0, 1, 2, 3)
    formation.boards = [b for b in formation.boards if b.is_coordinator]
    formation.cycle(heard={0})
    assert formation.status == STATUS_DEGRADED
    assert formation.missing_slots == (1, 2, 3)
