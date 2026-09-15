from __future__ import annotations

from argparse import Namespace
import sys
from types import SimpleNamespace
from unittest.mock import patch

from tools.ota_boot_commit.ota_boot_commit import (
    build_matches_expected,
    candidate_ports,
    open_matching_instrument,
    parse_args,
    run,
    slot_is_committed,
)


def test_no_commit_mode_is_explicit(monkeypatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        ["ota_boot_commit.py", "COM8", "--no-commit"],
    )
    args = parse_args()
    assert args.no_commit is True


def test_candidate_ports_prefers_original_and_deduplicates_case() -> None:
    discovered = [SimpleNamespace(device="com8"), SimpleNamespace(device="COM4")]
    with patch(
        "tools.ota_boot_commit.ota_boot_commit.list_ports.comports",
        return_value=discovered,
    ):
        assert candidate_ports("COM8", discover=True) == ["COM8", "COM4"]


def test_reconnect_follows_serial_number_to_changed_com_port() -> None:
    opened: list[str] = []

    class FakeSerial:
        def __init__(self, port: str, *_args, **_kwargs) -> None:
            opened.append(port)
            if port == "COM8":
                raise OSError("port disappeared")
            self.port = port
            self.closed = False

        def close(self) -> None:
            self.closed = True

    def fake_command(ser, _text, _timeout, **_kwargs):
        assert ser.port == "COM4"
        return "NO.1,DHRT100,839E1AE79EA20F31,0.1.0"

    discovered = [SimpleNamespace(device="COM4")]
    with (
        patch("tools.ota_boot_commit.ota_boot_commit.serial.Serial", FakeSerial),
        patch("tools.ota_boot_commit.ota_boot_commit.command", fake_command),
        patch("tools.ota_boot_commit.ota_boot_commit.list_ports.comports",
              return_value=discovered),
        patch("tools.ota_boot_commit.ota_boot_commit.try_open_matching_visa",
              return_value=(None, None)),
        patch("tools.ota_boot_commit.ota_boot_commit.time.sleep"),
    ):
        instrument = open_matching_instrument(
            "COM8", "839E1AE79EA20F31", 115200, 1.0, 0.0, 0.1)

    assert instrument.kind == "serial"
    assert instrument.endpoint == "COM4"
    assert opened == ["COM8", "COM4"]
    instrument.close()


def test_reconnect_falls_back_to_usbtmc() -> None:
    visa_instrument = SimpleNamespace(kind="usbtmc", endpoint="USB::INSTR")
    with (
        patch("tools.ota_boot_commit.ota_boot_commit.try_open_matching_port",
              return_value=(None, None, OSError("no CDC"))),
        patch("tools.ota_boot_commit.ota_boot_commit.try_open_matching_visa",
              return_value=(visa_instrument, None)),
    ):
        result = open_matching_instrument(
            "COM8", "839E1AE79EA20F31", 115200, 1.0, 0.0, 0.1)
    assert result is visa_instrument


def test_committed_slot_requires_active_confirmed_and_no_attempts() -> None:
    assert slot_is_committed("2,0,2,0,1") is True
    assert slot_is_committed("2,0,1,1,1") is False
    assert slot_is_committed("2,0,2,1,1") is False
    assert slot_is_committed("invalid") is False


def test_expected_build_is_a_precommit_gate() -> None:
    assert build_matches_expected('"20260915153459"', "20260915153459") is True
    assert build_matches_expected('"unexpected"', "20260915153459") is False
    assert build_matches_expected('"any"', None) is True


def test_build_mismatch_skips_commit(tmp_path) -> None:
    class FakeInstrument:
        kind = "usbtmc"
        endpoint = "USB::INSTR"

        def __init__(self) -> None:
            self.commands: list[str] = []

        def execute(self, command: str, _timeout: float) -> str:
            self.commands.append(command)
            responses = {
                "SYSTem:FW:BUILD?": '"unexpected"',
                "SYSTem:OTA:SLOT?": "2,0,1,1,0",
                "SYSTem:OTA:RES?": '0,"NONE","APPLIED",2,1,1',
                "SYSTem:OTA:TXN?": "0,0,0,0,0,0,0,0",
                "SYSTem:OTA:JOUR?": "0,0,0,0,0,0,0,0,0,0,0,0,0",
                "SYSTem:OTA:STAT?": '"IDLE",1,"NONE",0',
                "SYSTem:ERRor?": '0,"No error"',
            }
            return responses[command]

        def __enter__(self):
            return self

        def __exit__(self, *_args) -> None:
            return None

    instrument = FakeInstrument()
    args = Namespace(
        port="COM8",
        baud=115200,
        timeout=0.1,
        settle=0.0,
        reopen_timeout=1.0,
        boot_wait=0.0,
        serial_number="SERIAL",
        skip_boot=True,
        no_commit=False,
        expected_build="expected",
        out_dir=tmp_path,
    )
    with patch(
        "tools.ota_boot_commit.ota_boot_commit.open_matching_instrument",
        return_value=instrument,
    ):
        assert run(args) == 1

    assert "SYSTem:OTA:COMMit" not in instrument.commands
