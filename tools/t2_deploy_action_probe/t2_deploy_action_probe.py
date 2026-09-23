"""Bounded abstract T2 action lifecycle model.

This tool models the cross-board intent/READY/fence/completion boundary for one
business action. It does not open serial ports, drive GPIO, or claim hardware.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum


class ActionError(ValueError):
    """A rejected action fact that must remain visible to the caller."""


class ActionState(str, Enum):
    CREATED = "CREATED"
    PREPARED = "PREPARED"
    FENCED = "FENCED"
    EXECUTED = "EXECUTED"
    COMPLETED = "COMPLETED"
    REJECTED = "REJECTED"


@dataclass(frozen=True)
class Slot:
    name: str
    uid: str
    tdma_slot: int
    role: str


@dataclass(frozen=True)
class Intent:
    reservation_id: int
    deployment_generation: int
    run_id: int
    grid_epoch: int
    grid_generation: int
    ordinal: int
    target_vdc_ns: int
    target_slot: str
    action: str
    payload: int


@dataclass(frozen=True)
class Ready:
    reservation_id: int
    deployment_generation: int
    target_slot: str
    accepted: bool
    local_deadline_ticks: int
    reason: str = "OK"


@dataclass(frozen=True)
class SyncReadyStamp:
    """READY/time correspondence transported by the SYNC priority slot."""
    reservation_id: int
    deployment_generation: int
    target_slot: str
    local_event_ticks: int
    ready_vdc_ns: int
    window_start_vdc_ns: int
    window_end_vdc_ns: int
    sync_slot_seq: int


@dataclass(frozen=True)
class Fence:
    reservation_id: int
    deployment_generation: int
    fence_seq: int
    required_slots: frozenset[str]
    ready_slots: frozenset[str]


@dataclass(frozen=True)
class Completion:
    reservation_id: int
    deployment_generation: int
    target_slot: str
    executed: bool
    actual_event_valid: bool
    actual_local_ticks: int | None
    reason: str


@dataclass
class Record:
    intent: Intent
    state: ActionState = ActionState.CREATED
    ready: dict[str, Ready] = field(default_factory=dict)
    sync_stamps: dict[str, SyncReadyStamp] = field(default_factory=dict)
    fence: Fence | None = None
    completion: Completion | None = None
    rejections: list[str] = field(default_factory=list)


class ActionBook:
    """Single writer model for one deployment generation and action set."""

    def __init__(self, slots: tuple[Slot, ...], deployment_generation: int):
        if len(slots) != 4 or len({s.name for s in slots}) != 4:
            raise ActionError("four distinct business slots are required")
        self.slots = {s.name: s for s in slots}
        self.deployment_generation = deployment_generation
        self.records: dict[int, Record] = {}
        self.last_fence_seq = 0
        self.windows: dict[int, tuple[int, int]] = {}

    def create(self, intent: Intent) -> Record:
        if intent.deployment_generation != self.deployment_generation:
            raise ActionError("DEPLOYMENT_GENERATION")
        if intent.reservation_id in self.records:
            raise ActionError("DUPLICATE_RESERVATION")
        slot = self.slots.get(intent.target_slot)
        if slot is None:
            raise ActionError("TARGET_SLOT")
        if intent.run_id == 0 or intent.grid_epoch == 0 or intent.grid_generation == 0:
            raise ActionError("IDENTITY")
        if intent.target_vdc_ns <= 0 or intent.ordinal < 0:
            raise ActionError("GRID_TARGET")
        if slot.role not in {"SWITCH", "GATEWAY", "COUNTER"}:
            raise ActionError("ROLE")
        record = Record(intent=intent)
        self.records[intent.reservation_id] = record
        return record

    def accept_ready(self, ready: Ready) -> None:
        record = self._record(ready.reservation_id)
        self._same_generation(record, ready.deployment_generation)
        if ready.target_slot not in self.slots:
            self._reject(record, "READY_TARGET")
        if record.state not in {ActionState.CREATED, ActionState.PREPARED}:
            self._reject(record, "READY_STATE")
        if not ready.accepted or ready.local_deadline_ticks <= 0:
            self._reject(record, ready.reason if not ready.accepted else "READY_DEADLINE")
        if ready.target_slot in record.ready:
            self._reject(record, "READY_DUPLICATE")
        record.ready[ready.target_slot] = ready
        record.state = ActionState.PREPARED

    def issue_fence(self, reservation_id: int, required_slots: set[str], fence_seq: int) -> Fence:
        record = self._record(reservation_id)
        self._same_generation(record, record.intent.deployment_generation)
        if record.state != ActionState.PREPARED:
            self._reject(record, "FENCE_STATE")
        if not required_slots or not required_slots.issubset(self.slots):
            self._reject(record, "FENCE_SLOTS")
        ready_slots = set(record.ready)
        if not required_slots.issubset(ready_slots):
            record.rejections.append("FENCE_MISSING_READY")
            raise ActionError("FENCE_MISSING_READY")
        if not required_slots.issubset(record.sync_stamps):
            record.rejections.append("FENCE_MISSING_SYNC_STAMP")
            raise ActionError("FENCE_MISSING_SYNC_STAMP")
        windows = [record.sync_stamps[s] for s in required_slots]
        start = min(stamp.window_start_vdc_ns for stamp in windows)
        end = max(stamp.window_end_vdc_ns for stamp in windows)
        if start >= end:
            self._reject(record, "SYNC_WINDOW_INVALID")
        for old_id, (old_start, old_end) in self.windows.items():
            if old_id != reservation_id and start < old_end and old_start < end:
                self._reject(record, "SYNC_WINDOW_OVERLAP")
        if fence_seq <= self.last_fence_seq:
            self._reject(record, "FENCE_SEQUENCE")
        fence = Fence(reservation_id, self.deployment_generation, fence_seq,
                      frozenset(required_slots), frozenset(ready_slots))
        self.last_fence_seq = fence_seq
        record.fence = fence
        self.windows[reservation_id] = (start, end)
        record.state = ActionState.FENCED
        return fence

    def bind_sync_stamp(self, stamp: SyncReadyStamp) -> None:
        record = self._record(stamp.reservation_id)
        self._same_generation(record, stamp.deployment_generation)
        if stamp.target_slot not in record.ready:
            self._reject(record, "SYNC_WITHOUT_READY")
        if stamp.target_slot in record.sync_stamps:
            self._reject(record, "SYNC_DUPLICATE")
        if stamp.local_event_ticks <= 0 or stamp.sync_slot_seq <= 0:
            self._reject(record, "SYNC_IDENTITY")
        if not (stamp.window_start_vdc_ns <= stamp.ready_vdc_ns <= stamp.window_end_vdc_ns):
            self._reject(record, "SYNC_TIMESTAMP_WINDOW")
        record.sync_stamps[stamp.target_slot] = stamp

    def execute(self, reservation_id: int, fence: Fence, now_ticks: int) -> None:
        record = self._record(reservation_id)
        self._same_generation(record, fence.deployment_generation)
        if record.fence != fence or record.state != ActionState.FENCED:
            self._reject(record, "FENCE_STALE")
        if now_ticks > record.ready[record.intent.target_slot].local_deadline_ticks:
            self._reject(record, "DEADLINE_LATE")
        record.state = ActionState.EXECUTED

    def complete(self, completion: Completion) -> None:
        record = self._record(completion.reservation_id)
        self._same_generation(record, completion.deployment_generation)
        if completion.target_slot != record.intent.target_slot:
            self._reject(record, "COMPLETION_TARGET")
        if record.state != ActionState.EXECUTED:
            self._reject(record, "COMPLETION_STATE")
        if not completion.executed or not completion.actual_event_valid:
            self._reject(record, completion.reason or "COMPLETION_INVALID")
        record.completion = completion
        record.state = ActionState.COMPLETED

    def retire_generation(self, generation: int) -> None:
        if generation <= self.deployment_generation:
            for record in self.records.values():
                if record.state not in {ActionState.COMPLETED, ActionState.REJECTED}:
                    record.rejections.append("GENERATION_RETIRED")
                    record.state = ActionState.REJECTED

    def _record(self, reservation_id: int) -> Record:
        try:
            return self.records[reservation_id]
        except KeyError as exc:
            raise ActionError("RESERVATION") from exc

    def _same_generation(self, record: Record, generation: int) -> None:
        if generation != self.deployment_generation:
            self._reject(record, "DEPLOYMENT_GENERATION")

    @staticmethod
    def _reject(record: Record, reason: str):
        record.rejections.append(reason)
        record.state = ActionState.REJECTED
        raise ActionError(reason)


def default_slots() -> tuple[Slot, ...]:
    return (
        Slot("COUNTER", "0010071E65B5CB38", 0, "COUNTER"),
        Slot("SWITCH_A", "FB276192BEF9CCE1", 1, "SWITCH"),
        Slot("GATEWAY", "2BD5090FE009FA2A", 2, "GATEWAY"),
        Slot("SWITCH_B", "A1E549202D18ED6A", 3, "SWITCH"),
    )


def run_positive_demo() -> Record:
    book = ActionBook(default_slots(), deployment_generation=1)
    intent = Intent(1, 1, 99, 20260923, 2, 1800000000, 1800000000000,
                    "GATEWAY", "VNA_TRIG", 1)
    book.create(intent)
    book.accept_ready(Ready(1, 1, "GATEWAY", True, 500000000, "OK"))
    book.accept_ready(Ready(1, 1, "SWITCH_A", True, 499000000, "OK"))
    book.accept_ready(Ready(1, 1, "SWITCH_B", True, 499000000, "OK"))
    for index, slot in enumerate(("SWITCH_A", "SWITCH_B", "GATEWAY"), 1):
        book.bind_sync_stamp(SyncReadyStamp(
            1, 1, slot, 900000 + index, 1800000000000 + index * 100,
            1799999999000, 1800000001000, index))
    # The gateway action consumes the same intent after all required roles are ready.
    book.records[1].intent = Intent(1, 1, 99, 20260923, 2, 1800000000,
                                    1800000000000, "GATEWAY", "VNA_TRIG", 1)
    fence = book.issue_fence(1, {"SWITCH_A", "SWITCH_B", "GATEWAY"}, 1)
    book.execute(1, fence, 499000000)
    book.complete(Completion(1, 1, "GATEWAY", True, True, 499000123, "OK"))
    return book.records[1]


if __name__ == "__main__":
    record = run_positive_demo()
    print({"state": record.state.value, "reservation_id": record.intent.reservation_id,
           "rejections": record.rejections})
