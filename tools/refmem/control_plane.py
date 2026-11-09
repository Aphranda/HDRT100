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


def _parse_obs_rows(rest: list[int], offset: int, count: int,
                    response: str) -> list[dict]:
    """Parse the REFMEM-READ-01 row tail.

    Accepts the per-slot form (M4 firmware) and the legacy single-board form
    ``key,offset,count,valid,age_ms,<words>`` so a host tool survives the
    transition.  The per-slot form is recognised by ``slot == offset + i``
    plus 0/1 valid/stale flags.
    """
    if count > 0 and rest and len(rest) % count == 0:
        width = len(rest) // count
        if width >= 6:
            rows = []
            ok = True
            for i in range(count):
                base = i * width
                slot, valid, epoch, seq, age, stale = rest[base:base + 6]
                if (slot != offset + i or valid not in (0, 1)
                        or stale not in (0, 1)):
                    ok = False
                    break
                rows.append({'slot': slot, 'valid': valid, 'epoch': epoch,
                             'seq': seq, 'age_ms': age, 'stale': stale,
                             'value': rest[base + 6:base + width]})
            if ok:
                return rows
    if len(rest) >= 2 and rest[0] in (0, 1):
        valid, age = rest[0], rest[1]
        return [{'slot': offset, 'valid': valid, 'epoch': 0, 'seq': 0,
                 'age_ms': age, 'stale': 0 if valid else 1,
                 'value': rest[2:]}]
    raise TransportError(f'bad OBS rows {response!r}')


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
        ``targets``/``required`` are slot masks.  ``params`` must carry exactly
        the catalog row's ``param_words`` words -- the firmware reads that many
        and answers ``BAD_ARGUMENT`` on a mismatch.  The device answers
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

        Returns ``{'key','offset','count','valid','age_ms','stale','rows'}``
        where ``rows`` is a list of per-slot dicts
        ``{'slot','valid','epoch','seq','age_ms','stale','value'}`` (contract
        REFMEM-READ-01).  ``valid``/``age_ms``/``stale`` at the top level are a
        convenience summary: valid if any row is valid; otherwise the first
        row's invalid age and stale=1.
        """
        response = self._execute(
            f'SYSTem:REFMEM:OBS? {int(key)},{int(offset)},{int(count)}')
        tokens = _fields(response)
        if self._is_error(response):
            raise RejectedError('read rejected', response)
        if len(tokens) < 3:
            raise TransportError(f'short OBS response {response!r}')
        key_out, offset_out, count_out = (int(tokens[0], 0), int(tokens[1], 0),
                                          int(tokens[2], 0))
        rest = [int(t, 0) for t in tokens[3:]]
        rows = _parse_obs_rows(rest, offset_out, count_out, response)
        valid_rows = [r for r in rows if r['valid']]
        return {
            'key': key_out,
            'offset': offset_out,
            'count': count_out,
            'valid': 1 if valid_rows else 0,
            'age_ms': valid_rows[0]['age_ms'] if valid_rows else 0xFFFFFFFF,
            'stale': 0 if any(r['stale'] == 0 for r in valid_rows) else 1,
            'rows': rows,
        }

    # -- wait ------------------------------------------------------------
    TERMINAL_STATES = (4, 5, 6)  # ACKED / NACKED / TIMED_OUT

    def wait_aggregate(self, required: int, timeout_s: float = 60.0,
                       poll_s: float = 0.2) -> dict:
        """Wait for the origin's aggregated record to cover ``required``.

        ``SYSTem:CTRL:STATus?`` is the origin's single command record; the
        reply fold writes every target's terminal ACK/NACK into it.  This is
        the only wait that works for remote targets: a target keeps its own
        record locally and the origin has no ``COMMAND:TARget?`` row for it.
        Returns ``{'state','ack','nack','reason'}``.
        """
        deadline = self._clock() + timeout_s
        last = None
        while self._clock() < deadline:
            tokens = _fields(self._execute('SYSTem:CTRL:STATus?'))
            if len(tokens) >= 13:
                state, ack = int(tokens[2], 0), int(tokens[6], 0)
                nack, reason = int(tokens[7], 0), int(tokens[10], 0)
                last = {'state': state, 'ack': ack, 'nack': nack,
                        'reason': reason}
                if (state in self.TERMINAL_STATES
                        and ((ack | nack) & required) == required):
                    return last
            time.sleep(poll_s)
        raise TimeoutError(
            f'aggregate not terminal/complete for required=0x{required:X} '
            f'(last={last})')

    def wait_terminal(self, slots: list[int], timeout_s: float = 60.0,
                      poll_s: float = 0.2) -> dict[int, int]:
        """Poll **local** command records until terminal.

        ``SYSTem:COMMAND:TARget? <slot>`` answers only for a record that lives
        on this board (the origin's own record; a target's record lives on the
        target).  A slot with no record yet is treated as *pending*, not as a
        transport failure -- the firmware answers an empty record with no
        response at all.  For a remote command use :meth:`wait_aggregate`.
        """
        deadline = self._clock() + timeout_s
        pending = sorted(set(int(s) for s in slots))
        last: dict[int, tuple[int, int]] = {}
        while self._clock() < deadline:
            for slot in list(pending):
                seen = self._try_slot_state(slot)
                if seen is None:
                    continue
                state, reason = seen
                last[slot] = (state, reason)
                if state in self.TERMINAL_STATES:
                    pending.remove(slot)
            if not pending:
                return {slot: state for slot, (state, _) in last.items()}
            time.sleep(poll_s)
        raise TimeoutError(
            f'terminal state not reached for slots {pending} (last={last})')

    def _try_slot_state(self, slot: int) -> tuple[int, int] | None:
        """Return ``(state, reason)`` or ``None`` when no record exists yet."""
        try:
            return self._slot_state(slot)
        except TransportError:
            return None

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
    """Deterministic REFMEM transport for host tests (no hardware).

    The OBS answer mirrors the **current firmware** shape: a single board row
    ``key,offset,count,valid,age_ms,<schema words>``.  All eight keys have a
    local provider; a peer slot still answers ``valid=0``.
    """

    OBS_SCHEMA = {1: 2, 2: 2, 3: 3, 4: 7, 5: 5, 6: 6, 7: 3, 8: 4}
    OBS_IMPLEMENTED = frozenset({1, 2, 3, 4, 5, 6, 7, 8})

    def __init__(self, slots=(1, 2, 3, 4), terminal_after: int = 2) -> None:
        self.slots = list(slots)
        self.terminal_after = terminal_after
        self.commands: list[str] = []
        self._polls: dict[int, int] = {}
        self._agg_polls = 0
        self._seq = 0
        self.running = True

    def execute(self, command: str) -> str:  # noqa: C901 - small protocol switch
        self.commands.append(command)
        nums = [t for t in _fields(command) if t.lstrip('-').isdigit()]
        if command.startswith('SYSTem:CTRL:EXECute'):
            self._seq += 1
            self._polls = {s: 0 for s in self.slots}
            self._agg_polls = 0
            return f'QUEUED,{self._seq}'
        if command.startswith('SYSTem:CTRL:STATus?'):
            count = self._agg_polls
            self._agg_polls += 1
            state = 4 if count >= self.terminal_after else 3
            ack = 0xF if state == 4 else 0
            return (f'0,0,{state},{self._seq},15,15,{ack},0,0,0,0,0,'
                    f'{self._seq if state == 4 else 0}')
        if command.startswith('SYSTem:COMMAND:TARget?'):
            slot = int(nums[0], 0)
            count = self._polls.get(slot, 0)
            self._polls[slot] = count + 1
            state = 4 if count >= self.terminal_after else 3
            return f'{slot},{self._seq},{state},45,15,15,0,0,0'
        if command.startswith('SYSTem:REFMEM:OBS?'):
            key, offset, count = (int(t, 0) for t in nums)
            words = self.OBS_SCHEMA.get(key)
            if words is None:
                return '-200,"OBS_KEY"'
            if count <= 0:
                count = 1
            cells: list[int] = []
            for slot in range(offset, offset + count):
                if slot == 0 and key in self.OBS_IMPLEMENTED:
                    cells += [slot, 1, 1, 1, 0, 0] + [key] * words
                else:
                    cells += [slot, 0, 0, 0, 0xFFFFFFFF, 1] + [0] * words
            return f'{key},{offset},{count},' + ','.join(map(str, cells))
        return '-200,"UNKNOWN"'
