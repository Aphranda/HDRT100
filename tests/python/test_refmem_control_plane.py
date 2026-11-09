"""Host tests for the REFMEM control plane (M3 / R2-1..R2-2)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.refmem.control_plane import (  # noqa: E402
    ControlPlane, RejectedError, SimulatedTransport, TimeoutError,
    TransportError,
)


def test_write_returns_sequence():
    t = SimulatedTransport()
    plane = ControlPlane(t)
    # id 42 (TRIGGER_ANGLE_SWEEP) has exactly three parameter words.
    assert plane.write(42, [1, 2, 3]) == 1
    assert t.commands[-1].startswith('SYSTem:CTRL:EXECute 42,15,15,1,2,3')


def test_write_rejects_execution_error():
    class Bad(SimulatedTransport):
        def execute(self, command):
            return '-200,"NOPE"'

    try:
        ControlPlane(Bad()).write(42, [1, 2, 3])
    except RejectedError as exc:
        assert 'NOPE' in exc.response
    else:
        raise AssertionError('expected RejectedError')


def test_read_returns_per_slot_rows():
    plane = ControlPlane(SimulatedTransport())
    page = plane.read(6, offset=0, count=2)
    assert page['key'] == 6 and page['offset'] == 0 and page['count'] == 2
    assert page['rows'][0] == {
        'slot': 0, 'valid': 1, 'epoch': 1, 'seq': 1,
        'age_ms': 0, 'stale': 0, 'value': [6] * 6}
    assert page['rows'][1]['slot'] == 1
    assert page['rows'][1]['valid'] == 0
    assert page['rows'][1]['age_ms'] == 0xFFFFFFFF
    assert page['rows'][1]['stale'] == 1


def test_read_unimplemented_key_is_stale():
    page = ControlPlane(SimulatedTransport()).read(3)
    assert page['valid'] == 0 and page['stale'] == 1
    assert page['age_ms'] == 0xFFFFFFFF
    assert page['rows'][0]['valid'] == 0
    assert page['rows'][0]['value'] == [0, 0, 0]  # trigger_state schema: 3 words


def test_read_bad_key():
    plane = ControlPlane(SimulatedTransport())
    try:
        plane.read(99)
    except RejectedError:
        pass
    else:
        raise AssertionError('expected RejectedError for out-of-range key')


def test_wait_terminal_returns_per_slot():
    t = SimulatedTransport(terminal_after=2)
    plane = ControlPlane(t)
    plane.write(42, [1, 2, 3])
    result = plane.wait_terminal([1, 2, 3, 4], timeout_s=1.0, poll_s=0.0)
    assert result == {1: 4, 2: 4, 3: 4, 4: 4}
    assert all(cmd.startswith('SYSTem:COMMAND:TARget?')
               for cmd in t.commands if 'COMMAND:TARget' in cmd)


def test_wait_aggregate_returns_masks():
    t = SimulatedTransport(terminal_after=2)
    plane = ControlPlane(t)
    plane.write(42, [1, 2, 3])
    result = plane.wait_aggregate(0xF, timeout_s=1.0, poll_s=0.0)
    assert result['state'] == 4
    assert result['ack'] == 0xF and result['nack'] == 0


def test_wait_terminal_tolerates_missing_record():
    class NoRecord(SimulatedTransport):
        def execute(self, command):
            if command.startswith('SYSTem:COMMAND:TARget?'):
                raise TimeoutError('no response')
            return super().execute(command)

    plane = ControlPlane(NoRecord())
    try:
        plane.wait_terminal([1], timeout_s=0.01, poll_s=0.0)
    except TimeoutError as exc:
        assert '1' in str(exc)
    else:
        raise AssertionError('expected TimeoutError, not TransportError')


def test_wait_terminal_treats_nack_terminal():
    class Nack(SimulatedTransport):
        def execute(self, command):
            if command.startswith('SYSTem:COMMAND:TARget?'):
                return '3,9,5,45,15,15,0,2,12'
            return super().execute(command)

    plane = ControlPlane(Nack())
    assert plane.wait_terminal([3], timeout_s=0.05, poll_s=0.0) == {3: 5}


def test_wait_terminal_timeout_lists_pending():
    class Stuck(SimulatedTransport):
        def execute(self, command):
            if command.startswith('SYSTem:COMMAND:TARget?'):
                return '2,7,3,45,15,15,0,0,0'
            return super().execute(command)

    plane = ControlPlane(Stuck())
    try:
        plane.wait_terminal([2], timeout_s=0.01, poll_s=0.0)
    except TimeoutError as exc:
        assert '2' in str(exc)
    else:
        raise AssertionError('expected TimeoutError')


def test_transport_error_is_wrapped():
    class Closed:
        def execute(self, command):
            raise OSError('port closed')

    try:
        ControlPlane(Closed()).write(42, [1, 2, 3])
    except TransportError as exc:
        assert 'port closed' in str(exc)
    else:
        raise AssertionError('expected TransportError')


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith('test_')]
    for fn in tests:
        fn()
    print(f'refmem control plane tests passed ({len(tests)})')


if __name__ == '__main__':
    main()
