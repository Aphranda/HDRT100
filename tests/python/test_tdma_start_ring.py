from argparse import Namespace
import time

import pytest

from tools.tdma_ring_monitor.tdma_start_ring import (
    _board_command_on_serial,
    persistent_sessions_enabled,
    resolve_board_ids,
)
from tools.scpi_common.scpi_serial import (
    read_scpi_response,
    serial_lifecycle_mode,
)


def args(board_id=None, reference_id=None, forward_id=None):
    return Namespace(board_id=board_id,
                     reference_id=reference_id,
                     forward_id=forward_id)


def test_resolve_three_to_eight_board_ring_order():
    three = ["REF", "FWD1", "FWD2"]
    eight = [f"NODE{i}" for i in range(8)]
    assert resolve_board_ids(args(board_id=three)) == three
    assert resolve_board_ids(args(board_id=eight)) == eight


def test_resolve_legacy_two_board_ids():
    assert resolve_board_ids(
        args(reference_id="REF", forward_id="FWD")) == ["REF", "FWD"]


def test_validation_sessions_are_persistent_by_default():
    options = args()
    options.keep_open = True
    assert persistent_sessions_enabled(options) is True


def test_imported_helpers_do_not_open_persistent_sessions_implicitly():
    assert persistent_sessions_enabled(args()) is False


def test_acceptance_environment_enables_persistent_sessions(monkeypatch):
    monkeypatch.setenv("HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS", "1")
    assert persistent_sessions_enabled(args()) is True
    options = args()
    options.short_open = True
    assert persistent_sessions_enabled(options) is False


def test_phase_lifecycle_enables_one_session_per_board(monkeypatch):
    monkeypatch.setenv("HAOFV_SERIAL_LIFECYCLE", "phase")
    monkeypatch.delenv("HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS", raising=False)
    assert serial_lifecycle_mode() == "phase"
    assert persistent_sessions_enabled(args()) is True


def test_command_lifecycle_keeps_short_open_fallback(monkeypatch):
    monkeypatch.setenv("HAOFV_SERIAL_LIFECYCLE", "command")
    monkeypatch.setenv("HAOFV_ACCEPTANCE_PERSISTENT_SESSIONS", "1")
    assert serial_lifecycle_mode() == "command"
    assert persistent_sessions_enabled(args()) is False


def test_short_open_disables_persistent_sessions():
    options = args()
    options.short_open = True
    assert persistent_sessions_enabled(options) is False


class _ReadSerial:
    def __init__(self, payload: bytes) -> None:
        self.payload = bytearray(payload)

    def read(self, size: int) -> bytes:
        if not self.payload:
            return b""
        value = bytes(self.payload[:size])
        del self.payload[:size]
        return value


def test_write_command_consumes_bare_ack_without_waiting_for_timeout():
    serial = _ReadSerial(b"OK\r\n")
    started = time.monotonic()
    response = read_scpi_response(
        serial, "SYSTem:TDMA:RING:STOP", 1.0, require_match=True)
    assert response == "OK"
    assert time.monotonic() - started < 0.1


def test_query_skips_stale_ack_and_returns_matching_payload():
    serial = _ReadSerial(b"OK\r\n1\r\n")
    assert read_scpi_response(
        serial, "SYSTem:TDMA:RING:DIAGnostic?", 1.0,
        require_match=True) == "1"


def test_unknown_write_uses_action_timeout(monkeypatch):
    observed = []
    import tools.tdma_ring_monitor.tdma_start_ring as ring

    monkeypatch.setattr(
        ring, "command",
        lambda _serial, _text, timeout: observed.append(timeout) or "OK")
    options = Namespace(timeout=3.0, action_timeout=0.5)
    assert _board_command_on_serial(
        object(), "CALibration:TRAINing:STAGe:BEGin 4", options,
        object()) == "OK"
    assert observed == [0.5]


def test_query_keeps_full_timeout(monkeypatch):
    observed = []
    import tools.tdma_ring_monitor.tdma_start_ring as ring

    monkeypatch.setattr(
        ring, "command",
        lambda _serial, _text, timeout: observed.append(timeout) or "1")
    options = Namespace(timeout=3.0, action_timeout=0.5)
    assert _board_command_on_serial(
        object(), "SYSTem:TDMA:RING:DIAGnostic?", options,
        object()) == "1"
    assert observed == [3.0]


def test_software_reset_disconnect_is_successful_handoff(monkeypatch):
    class FakeSerial:
        def write(self, payload):
            return len(payload)

        def flush(self):
            return None

    def disconnected(*args, **kwargs):
        raise OSError("target disconnected for reset")

    import tools.tdma_ring_monitor.tdma_start_ring as ring
    monkeypatch.setattr(ring, "command", disconnected)
    result = _board_command_on_serial(
        object(), "SYSTem:BOOT:RESet", Namespace(timeout=3.0), FakeSerial())
    assert "software reset" in result


@pytest.mark.parametrize("board_ids", [
    ["ONLY"],
    [f"NODE{i}" for i in range(9)],
    ["SAME", "SAME"],
])
def test_resolve_rejects_invalid_board_sets(board_ids):
    with pytest.raises(ValueError):
        resolve_board_ids(args(board_id=board_ids))
