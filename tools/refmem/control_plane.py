"""Unified REFMEM control plane for host tools (M3 / R2-1..R2-2).

One object owns the read/write/wait surface so pages stop hand-rolling SCPI:

* ``write`` submits a catalog command through ``SYSTem:REFMEM:CMD:SUBMit:TYPe``
  and returns the accepted sequence (or raises a typed error);
* ``read`` pages a read key through ``SYSTem:REFMEM:OBS?`` (contract
  REFMEM-READ-01; the firmware side lands with M2) and returns per-page rows;
* ``wait_terminal`` polls ``COMMAND:STATus?``/``NODE:CONTrol?`` until every
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
        ``targets``/``required`` are slot masks. Raises ``RejectedError`` when
        the device answers with an execution error (``-200``/``ERR``).
        """
        parts = [str(int(command_type)), str(int(targets)), str(int(required))]
        if params:
            parts += [str(int(p)) for p in params]
        response = self._execute('SYSTem:REFMEM:CMD:SUBMit:TYPe ' + ','.join(parts))
        tokens = _fields(response)
        if not tokens:
            raise TransportError('empty submit response')
        text = response.upper()
        if '-200' in response or 'ERR' in text:
            raise RejectedError('command rejected', response)
        try:
            return int(tokens[0], 0)
        except ValueError as exc:
            raise TransportError(f'bad submit response {response!r}') from exc

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
        if any(t.lstrip('-').isdigit() is False for t in tokens[:6]) and tokens:
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
    def wait_terminal(self, slots: list[int], timeout_s: float = 60.0,
                      poll_s: float = 0.2) -> dict[int, int]:
        """Poll until every slot reports a terminal command state.

        Returns ``{slot: status}`` where status is the raw device field; raises
        ``TimeoutError`` listing the slots that never settled (instead of
        returning a misleading bool).
        """
        deadline = self._clock() + timeout_s
        pending = sorted(set(int(s) for s in slots))
        last: dict[int, int] = {}
        while self._clock() < deadline:
            for slot in list(pending):
                status = self._slot_status(slot)
                last[slot] = status
                if status in (0, 1, 2):  # IDLE / COMPLETE / FAILED per contract
                    pending.remove(slot)
            if not pending:
                return last
            time.sleep(poll_s)
        raise TimeoutError(
            f'terminal state not reached for slots {pending} (last={last})')

    def _slot_status(self, slot: int) -> int:
        response = self._execute(f'SYSTem:REFMEM:COMMAND:STATus? {int(slot)}')
        tokens = _fields(response)
        if not tokens:
            raise TransportError('empty status response')
        try:
            return int(tokens[0], 0)
        except ValueError as exc:
            raise TransportError(f'bad status response {response!r}') from exc

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
        if command.startswith('SYSTem:REFMEM:CMD:SUBMit:TYPe'):
            self._seq += 1
            self._polls = {s: 0 for s in self.slots}
            return str(self._seq)
        if command.startswith('SYSTem:REFMEM:COMMAND:STATus?'):
            slot = int(nums[0], 0)
            count = self._polls.get(slot, 0)
            self._polls[slot] = count + 1
            return '1' if count >= self.terminal_after else '3'
        if command.startswith('SYSTem:REFMEM:OBS?'):
            key, offset, count = (int(t, 0) for t in nums)
            if key <= 0 or key > 8:
                return '-200,"OBS_KEY"'
            rows = ','.join(str((offset + i) & 0xFF) for i in range(count))
            return f'{key},{offset},{count},1,17' + (',' + rows if rows else '')
        return '-200,"UNKNOWN"'
