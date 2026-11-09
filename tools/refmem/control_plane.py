"""Unified REFMEM control plane for host tools (M3 / R2-1..R2-2).

One object owns the read/write/wait surface so pages stop hand-rolling SCPI:

* ``write`` submits a catalog command through ``SYSTem:CTRL:EXECute``
  and returns the accepted sequence (or raises a typed error);
* ``read`` pages a read key through ``SYSTem:REFMEM:OBS?`` (contract
  REFMEM-READ-01) and returns per-page rows;
* ``wait_terminal`` polls ``SYSTem:COMMAND:TARget?`` per slot until every
  targeted slot is terminal, returning per-slot results instead of a bool.

The transport is any object with ``execute(command) -> str`` (a SCPI session or
the simulator below). Errors are explicit: ``TransportError`` when the
transport itself fails, ``RejectedError`` when the device answers with an
execution error, and ``TimeoutError`` when a terminal state never arrives.
"""
from __future__ import annotations

import time


class ControlPlaneError(RuntimeError):
    """Base class for control-plane failures."""


class TransportError(ControlPlaneError):
    """The SCPI transport failed (port closed, timeout, ...)."""


class RejectedError(ControlPlaneError):
    """The device rejected the command (execution error)."""

    def __init__(self, message: str, response: str) -> None:
        super().__init__(message)
        self.response = response


class TimeoutError(ControlPlaneError):  # noqa: A001 - explicit domain name
    """A terminal state did not arrive before the deadline."""


def _fields(text: str) -> list[str]:
    return [t for t in str(text).replace('"', ' ').replace(',', ' ').split() if t]


class ControlPlane:
    """Read/write/wait over one REFMEM transport."""

    def __init__(self, transport, clock=time.monotonic) -> None:
        self._t = transport
        self._clock = clock

    # -- write -----------------------------------------------------------
    def write(self, command_type: int, params: list[int] | None = None,
              targets: int = 0xF, required: int = 0xF) -> int:
        """Submit one catalog command; returns the sequence number.

        ``command_type`` is the catalog id (``config/refmem/command_catalog.yaml``),
        ``targets``/``required`` are slot masks.  The device answers
        ``QUEUED,<command_seq>`` (``SYSTem:CTRL:EXECute``); an execution error
        (``-200``/``ERR``) raises ``RejectedError``.
        """
        parts = [str(int(command_type)), str(int(targets)), str(int(required))]
        if params:
            parts += [str(int(p)) for p in params]
        response = self._execute('SYSTem:CTRL:EXECute ' + ','.join(parts))
        tokens = _fields(response)
        if not tokens:
            raise TransportError('empty CTRL:EXECute response')
        if self._is_error(response):
            raise RejectedError('command rejected', response)
        # The contract answer is "QUEUED,<seq>"; accept a bare sequence too so a
        # firmware that only returns the number keeps working.
        if tokens[0].upper() == 'QUEUED':
            if len(tokens) < 2:
                raise TransportError(f'QUEUED without sequence: {response!r}')
            tokens = tokens[1:]
        try:
            return int(tokens[0], 0)
        except ValueError as exc:
            raise TransportError(f'bad CTRL:EXECute response {response!r}') from exc

    # -- read ------------------------------------------------------------
    def read(self, key: int, offset: int = 0, count: int = 0) -> dict:
        """Page one read key.

        Returns ``{'key', 'offset', 'count', 'valid', 'age_ms', 'rows'}`` where
        ``rows`` is a list of int tuples (schema per key, contract
        REFMEM-READ-01). ``valid == 0`` means the local mirror has no snapshot
        yet: ``age_ms`` is then ``0xFFFFFFFF`` and ``stale`` is ``1``.
        """
        response = self._execute(
            f'SYSTem:REFMEM:OBS? {int(key)},{int(offset)},{int(count)}')
        tokens = _fields(response)
        if self._is_error(response):
            raise RejectedError('read rejected', response)
        if len(tokens) < 6:
            raise TransportError(f'short OBS response {response!r}')
        key_out, offset_out, count_out = (int(tokens[0], 0), int(tokens[1], 0),
                                          int(tokens[2], 0))
        valid, age_ms = int(tokens[3], 0), int(tokens[4], 0)
        rows_flat = [int(t, 0) for t in tokens[5:]]
        return {
            'key': key_out,
            'offset': offset_out,
            'count': count_out,
            'valid': valid,
            'age_ms': age_ms,
            'stale': 1 if valid == 0 else 0,
            'rows': rows_flat,
        }

    # -- wait ------------------------------------------------------------
    TERMINAL_STATES = (4, 5, 6)  # ACKED / NACKED / TIMED_OUT

    def wait_terminal(self, slots: list[int], timeout_s: float = 60.0,
                      poll_s: float = 0.2) -> dict[int, int]:
        """Poll every slot until its command record is terminal.

        Reads ``SYSTem:COMMAND:TARget? <slot>`` (the per-slot REFMEM record,
        not the origin-only ``CTRL:STATus?``).  Returns ``{slot: state}``;
        states follow ``refmem_command_state_t`` and terminal means
        ACKED(4)/NACKED(5)/TIMED_OUT(6).  A slot that never settles raises
        ``TimeoutError`` with the last ``(state, reason)`` per pending slot.
        """
        deadline = self._clock() + timeout_s
        pending = sorted(set(int(s) for s in slots))
        last: dict[int, tuple[int, int]] = {}
        while self._clock() < deadline:
            for slot in list(pending):
                state, reason = self._slot_state(slot)
                last[slot] = (state, reason)
                if state in self.TERMINAL_STATES:
                    pending.remove(slot)
            if not pending:
                return {slot: state for slot, (state, _) in last.items()}
            time.sleep(poll_s)
        raise TimeoutError(
            f'terminal state not reached for slots {pending} (last={last})')

    def _slot_state(self, slot: int) -> tuple[int, int]:
        response = self._execute(f'SYSTem:COMMAND:TARget? {int(slot)}')
        tokens = _fields(response)
        if not tokens:
            raise TransportError('empty COMMAND:TARget response')
        if self._is_error(response):
            raise RejectedError('target read rejected', response)
        # slot,command_seq,state,type,target,required,ack,nack,last_reason
        if len(tokens) < 9:
            raise TransportError(
                f'short COMMAND:TARget response {response!r}')
        return int(tokens[2], 0), int(tokens[8], 0)

    @staticmethod
    def _is_error(response: str) -> bool:
        head = response.strip()
        return head.startswith('-') or head.upper().startswith('ERR')

    def _execute(self, command: str) -> str:
        try:
            return self._t.execute(command)
        except Exception as exc:  # noqa: BLE001 - transport boundary
            raise TransportError(str(exc)) from exc


class SimulatedTransport:
    """Deterministic REFMEM transport for host tests (no hardware)."""

    def __init__(self, slots=(1, 2, 3, 4), terminal_after: int = 2) -> None:
        self.slots = list(slots)
        self.terminal_after = terminal_after
        self.commands: list[str] = []
        self._polls: dict[int, int] = {}
        self._seq = 0
        self.running = True

    def execute(self, command: str) -> str:  # noqa: C901 - small protocol switch
        self.commands.append(command)
        nums = [t for t in _fields(command) if t.lstrip('-').isdigit()]
        if command.startswith('SYSTem:CTRL:EXECute'):
            self._seq += 1
            self._polls = {s: 0 for s in self.slots}
            return f'QUEUED,{self._seq}'
        if command.startswith('SYSTem:COMMAND:TARget?'):
            slot = int(nums[0], 0)
            count = self._polls.get(slot, 0)
            self._polls[slot] = count + 1
            state = 4 if count >= self.terminal_after else 3
            return f'{slot},{self._seq},{state},45,15,15,0,0,0'
        if command.startswith('SYSTem:REFMEM:OBS?'):
            key, offset, count = (int(t, 0) for t in nums)
            if key <= 0 or key > 8:
                return '-200,"OBS_KEY"'
            rows = ','.join(str((offset + i) & 0xFF) for i in range(count))
            return f'{key},{offset},{count},1,17' + (',' + rows if rows else '')
        return '-200,"UNKNOWN"'
