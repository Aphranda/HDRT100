from tools.scpi_common import scpi_serial


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
