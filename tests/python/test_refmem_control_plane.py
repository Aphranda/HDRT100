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
    assert plane.write(50, [1, 2, 3]) == 1
    assert t.commands[-1].startswith('SYSTem:REFMEM:CMD:SUBMit:TYPe 50,15,15')


def test_write_rejects_execution_error():
    class Bad(SimulatedTransport):
        def execute(self, command):
            return '-200,"NOPE"'

    try:
        ControlPlane(Bad()).write(50)
    except RejectedError as exc:
        assert 'NOPE' in exc.response
    else:
        raise AssertionError('expected RejectedError')


def test_read_pages_and_stale_flag():
    plane = ControlPlane(SimulatedTransport())
    page = plane.read(3, offset=2, count=2)
    assert page['key'] == 3 and page['offset'] == 2 and page['count'] == 2
    assert page['valid'] == 1 and page['stale'] == 0
    assert page['rows'] == [2, 3]


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
    plane.write(50)
    result = plane.wait_terminal([1, 2, 3, 4], timeout_s=1.0, poll_s=0.0)
    assert result == {1: 1, 2: 1, 3: 1, 4: 1}


def test_wait_terminal_timeout_lists_pending():
    class Stuck(SimulatedTransport):
        def execute(self, command):
            if command.startswith('SYSTem:REFMEM:COMMAND:STATus?'):
                return '3'
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
        ControlPlane(Closed()).write(50)
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
