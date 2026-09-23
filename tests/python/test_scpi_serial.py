import pytest

from tools.scpi_common import scpi_serial


@pytest.mark.parametrize("header", sorted(scpi_serial.SCK_LOOP_START_HEADERS))
def test_sck_loop_signed_start_echo(header):
    command = header + " 1,7,2048,-10,0"
    assert header in scpi_serial.STRUCTURED_WRITE_HEADERS
    assert scpi_serial.scpi_response_matches_command(command, "1,7,2048,-10,0")
    for invalid in ("1,7,2048,-10", "1,7,2048,-10,0,1", "1,7,-2048,0,0"):
        assert not scpi_serial.scpi_response_matches_command(command, invalid)


@pytest.mark.parametrize("header", sorted(scpi_serial.SCK_RXPHASE_START_HEADERS |
                                          scpi_serial.MARK_RXPHASE_START_HEADERS))
def test_rxphase_start_preserves_signed_seven_field_echo(monkeypatch, header):
    command = header + " 1,7,256,26,-1,0,0"
    echo = "1,7,256,26,-1,0,0"
    assert header in scpi_serial.STRUCTURED_WRITE_HEADERS
    assert scpi_serial.scpi_response_matches_command(command, echo)
    for invalid in ("1,7,256,26,-1,0", echo + ",0", "1,7,256,-26,-1,0,0"):
        assert not scpi_serial.scpi_response_matches_command(command, invalid)
    replies = iter(("OK", echo))
    monkeypatch.setattr(scpi_serial, "read_serial_line_idle", lambda *_: next(replies))
    assert scpi_serial.read_scpi_response(None, command, 1., require_match=True) == echo


@pytest.mark.parametrize("header", sorted(scpi_serial.DATA_LOOP_START_HEADERS))
def test_data_loop_start_binds_both_signed_offsets(monkeypatch, header):
    echo = "1,7,26,-1,-2,1000,0"
    command = header + " " + echo
    assert header in scpi_serial.STRUCTURED_WRITE_HEADERS
    assert scpi_serial.scpi_response_matches_command(command, echo)
    for invalid in ("OK", "1,7,26,-1,-2,1000", echo + ",0", "1,7,-26,-1,-2,1000,0",
                    "1,7,26,-1,-2,-1000,0"):
        assert not scpi_serial.scpi_response_matches_command(command, invalid)
    responses = iter(("OK", echo))
    monkeypatch.setattr(scpi_serial, "read_serial_line_idle", lambda *_: next(responses))
    assert scpi_serial.read_scpi_response(None, command, 1., require_match=True) == echo


def test_p3_start_requires_complete_six_field_ack(monkeypatch):
    command = "CAL:P3:START 3,10000000,32,256,11,0"
    echo = "3,10000000,32,256,11,0"
    assert "CAL:P3:START" in scpi_serial.STRUCTURED_WRITE_HEADERS
    assert not scpi_serial.scpi_response_matches_command(command, "ERROR")
    replies = iter(("OK", echo))
    monkeypatch.setattr(scpi_serial, "read_serial_line_idle", lambda *_: next(replies))
    assert scpi_serial.read_scpi_response(None, command, 1., require_match=True) == echo


@pytest.mark.parametrize("header", ["CAL:MARK:LOOP:STAR", "CAL:MARK:LOOP:START",
                                   "CALibration:MARKer:LOOP:STARt"])
def test_marker_loop_start_preserves_numeric_echo(monkeypatch, header):
    command = header + " 1,61,256,0"
    assert header.upper() in scpi_serial.STRUCTURED_WRITE_HEADERS
    assert scpi_serial.scpi_response_matches_command(command, "1,61,256,0")
    assert not scpi_serial.scpi_response_matches_command(command, "1")
    assert not scpi_serial.scpi_response_matches_command(command, "1,61,256")
    replies = iter(("OK", "1,61,256,0"))
    monkeypatch.setattr(scpi_serial, "read_serial_line_idle", lambda *_args: next(replies))
    assert scpi_serial.read_scpi_response(None, command, 1.0, require_match=True) == "1,61,256,0"


def test_open_serial_port_ignores_disconnect_during_cleanup(monkeypatch):
    class DisconnectedSerial:
        def __init__(self, *args, **kwargs):
            self.close_attempted = False

        def reset_input_buffer(self):
            pass

        def reset_output_buffer(self):
            pass

        def flush(self):
            raise OSError("device re-enumerated")

        def close(self):
            self.close_attempted = True
            raise OSError("device is already gone")

    serial_port = DisconnectedSerial()
    monkeypatch.setattr(
        scpi_serial.serial, "Serial", lambda *args, **kwargs: serial_port)

    with scpi_serial.open_serial_port("COM4", 115200, 1.0, 0.0) as opened:
        assert opened is serial_port

    assert serial_port.close_attempted


@pytest.mark.parametrize("command,response", [
    ("SYSTem:TDMA:RING:TOPology 2,0,0", "QUEUED,2,0,0,17"),
    ("SYSTem:TDMA:RING:LOCAL 1", "QUEUED,1,18"),
    ("SYSTem:TDMA:RING:SLOT 1", "QUEUED,1,18"),
    ("CALibration:ACTivate", "QUEUED,19"),
    ("CALibration:ROLLback", "QUEUED,20"),
    ("SYSTem:SYNC:VDC:DPLL:PROVisional 1", "QUEUED,1,21"),
    ("SYSTem:SYNC:VDC:DPLL:OWNer?", "21,21,3,2,0"),
])
def test_owner_mailbox_responses_match_exact_shapes(command, response):
    assert scpi_serial.scpi_response_matches_command(command, response)


@pytest.mark.parametrize("command,response", [
    ("SYSTem:TDMA:RING:TOPology 2,0,0", "2,0,0"),
    ("SYSTem:TDMA:RING:TOPology 2,0,0", "QUEUED,2,0,0"),
    ("SYSTem:TDMA:RING:TOPology 2,0,0", "QUEUED,2,0,0,17,99"),
    ("SYSTem:TDMA:RING:LOCAL 1", "1"),
    ("SYSTem:TDMA:RING:LOCAL 1", "QUEUED,1"),
    ("CALibration:ACTivate", "1"),
    ("CALibration:ACTivate", "QUEUED"),
    ("CALibration:ROLLback", "QUEUED,-1"),
    ("SYSTem:SYNC:VDC:DPLL:PROVisional 1", "QUEUED,21"),
    ("SYSTem:SYNC:VDC:DPLL:PROVisional 1", "QUEUED,1,21,22"),
    ("SYSTem:SYNC:VDC:DPLL:OWNer?", "21,21,3,2"),
    ("SYSTem:SYNC:VDC:DPLL:OWNer?", "21,21,3,2,0,99"),
    ("SYSTem:SYNC:VDC:DPLL:OWNer?", "21,21,3,-1,0"),
])
def test_owner_mailbox_responses_reject_legacy_or_bad_shapes(command, response):
    assert not scpi_serial.scpi_response_matches_command(command, response)


def test_structured_write_discards_bare_ack_and_returns_queued_tuple(monkeypatch):
    replies = iter(("OK", "QUEUED,2,0,0,17"))
    monkeypatch.setattr(
        scpi_serial, "read_serial_line_idle", lambda _ser, _deadline: next(replies))
    monkeypatch.setattr(scpi_serial.time, "monotonic", lambda: 0.0)

    assert scpi_serial.read_scpi_response(
        object(), "SYSTem:TDMA:RING:TOPology 2,0,0", 1.0,
        require_match=True) == "QUEUED,2,0,0,17"
