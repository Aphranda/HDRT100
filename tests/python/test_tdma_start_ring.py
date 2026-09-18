from argparse import Namespace
import time

import pytest

from tools.tdma_ring_monitor.tdma_start_ring import (
    _board_command_on_serial,
    persistent_sessions_enabled,
    resolve_board_ids,
    status,
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


@pytest.mark.parametrize("header", [
    "SYSTem:VDC:FEEDback:SESSion?", "SYST:VDC:FEED:SESS?",
    "SYSTem:TDMA:LOAD:MASK?",
])
@pytest.mark.parametrize("require_match", [False, True])
def test_u32_query_preserves_one_after_bare_ack(header, require_match):
    serial = _ReadSerial(b"OK\r\n1\r\n")
    assert read_scpi_response(serial, header, .05,
                              require_match=require_match) == "1"


def test_non_scalar_query_still_discards_stale_one():
    serial = _ReadSerial(b"OK\r\n1\r\n0,0,5,5\r\n")
    assert read_scpi_response(serial, "SYSTem:TDMA:RING:STATus?", .05,
                              require_match=True) == "0,0,5,5"


def test_topology_command_preserves_firmware_result_tuple():
    serial = _ReadSerial(b"[123] DBG ignored\r\n4,2\r\n4,2,0\r\n")
    assert read_scpi_response(
        serial, "SYSTem:TDMA:RING:TOPology 4,2,0", 0.1,
        require_match=True) == "4,2,0"


@pytest.mark.parametrize("action", ["STAGe 7", "APPLy"])
def test_operating_profile_preserves_firmware_result_tuple(action):
    serial = _ReadSerial(b"7,10000000,1000000,8,0,123\r\n")
    assert read_scpi_response(
        serial, "SYSTem:TDMA:OPMode:" + action, 0.1,
        require_match=True) == "7,10000000,1000000,8,0,123"


@pytest.mark.parametrize("text,payload", [
    ("CALibration:TOPology:PROBe 1,2", "1,2"),
    ("CALibration:TOPology:PROBe 0", "0,0"),
    ("SYSTem:TDMA:RING:TOPology 4,2,0", "4,2,0"),
    ("SYSTem:TDMA:OPMode:STAGe 7", "7,10000000,1000000,4096,0,1383759744"),
    ("SYSTem:TDMA:OPMode:APPLy", "7,10000000,1000000,4096,0,1383759744"),
])
def test_result_write_waits_for_tuple_after_unrelated_bare_ack(text, payload):
    serial = _ReadSerial(("OK\r\n"+payload+"\r\n").encode("ascii"))
    assert read_scpi_response(serial,text,.05,require_match=True) == payload


@pytest.mark.parametrize("text", [
    "CALibration:TOPology:PROBe 1,2", "CALibration:TOPology:PROBe 0",
    "SYSTem:TDMA:RING:TOPology 4,2,0",
    "SYSTem:TDMA:OPMode:STAGe 7", "SYSTem:TDMA:OPMode:APPLy",
])
def test_result_write_uses_response_budget_and_preserves_timeout(monkeypatch, text):
    import tools.tdma_ring_monitor.tdma_start_ring as ring
    observed = []
    def no_result(_serial, _text, timeout):
        observed.append(timeout)
        return "<timeout>"
    monkeypatch.setattr(ring,"command",no_result)
    response = _board_command_on_serial(object(),text,Namespace(timeout=3.,action_timeout=.05),object())
    assert response == "<timeout>"
    assert observed == [3.]


def test_topology_timeout_is_not_reported_as_verified(monkeypatch):
    import tools.tdma_ring_monitor.tdma_start_ring as ring
    monkeypatch.setattr(ring, "command", lambda *args: "<timeout>")
    assert _board_command_on_serial(
        object(), "SYSTem:TDMA:RING:TOPology 4,2,0",
        Namespace(timeout=3.0, action_timeout=0.1), object()) == "<timeout>"


@pytest.mark.parametrize("header", ["SYSTem:TDMA:RING:START", "SYST:TDMA:RING:START"])
@pytest.mark.parametrize("reply", ["<timeout>", "OK", 'ERR', '-200,"Execution error"'])
def test_start_preserves_real_ack_or_failure_without_retry_or_run_query(monkeypatch, header, reply):
    from tools.tdma_ring_monitor import tdma_start_ring as ring
    calls = []
    def exchange(ser, text, timeout):
        calls.append((text, timeout))
        return reply
    monkeypatch.setattr(ring, "command", exchange)
    result = _board_command_on_serial(object(), header,
        Namespace(timeout=3.0, action_timeout=0.1), object())
    assert result == reply
    assert calls == [(header, 0.1)]


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


def test_status_uses_one_bounded_short_open_recovery(monkeypatch):
    import tools.tdma_ring_monitor.tdma_start_ring as ring

    calls = []

    def fake_board_command(_board, _text, options):
        calls.append(bool(getattr(options, "keep_open", False)))
        if len(calls) == 1:
            return "<timeout>"
        values = [0] * len(ring.TDMA_FIELDS)
        values[ring.TDMA_FIELDS.index("ring_enabled")] = 1
        values[ring.TDMA_FIELDS.index("ring_adapter_started")] = 1
        return ",".join(str(value) for value in values)

    monkeypatch.setattr(ring, "board_command", fake_board_command)
    monkeypatch.setattr(ring, "close_persistent_connections", lambda: None)
    options = Namespace(timeout=3.0, keep_open=True, short_open=False)
    result = status(Namespace(address="NODE"), options)
    assert result["ring_adapter_started"] == 1
    assert calls == [True, False]


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
