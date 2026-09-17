"""Host checks for identity-bound USB maintenance; no hardware required."""
from contextlib import contextmanager
import json

import pytest

from tools.usb_runtime_switch import usb_runtime_switch as usb


UID = "839E1AE79EA20F31"
BUILD = "20260917012959"
VISA = f"USB0::0xCAFE::0x4001::{UID}::INSTR"


def args(tmp_path, *extra):
    return usb.parse_args(["--serial-number", UID, "--build", BUILD,
                           "--out", str(tmp_path / "report.json"), *extra])


def report():
    return {"exchanges": []}


def fake_connection(monkeypatch):
    @contextmanager
    def connect(*_):
        yield object()
    monkeypatch.setattr(usb, "connection", connect)


@pytest.mark.parametrize("identity,build", [("OTHER", BUILD), (UID, "wrong")])
def test_identity_mismatch_never_writes(monkeypatch, tmp_path, identity, build):
    fake_connection(monkeypatch)
    commands = []

    def command(self, cmd, **_):
        commands.append(cmd)
        return {"*IDN?": f"H,DHRT100,{identity},1", "SYST:FW:BUILD?": build}[cmd]

    monkeypatch.setattr(usb.Session, "command", command)
    with pytest.raises(RuntimeError, match="mismatch"):
        usb.run(args(tmp_path, "--port", "COM10"), report())
    assert all(command.endswith("?") for command in commands)


def test_enumeration_requires_exact_serial_vendor_and_transport():
    devices = {"serial": [{"port": "COM10", "serial_number": UID, "vid": 0xCAFE},
                          {"port": "COM11", "serial_number": UID + "X", "vid": 0xCAFE}],
               "visa": [VISA, VISA.replace(UID, UID + "X"),
                        VISA.replace("0xCAFE", "0x1234"), VISA.replace("INSTR", "RAW")]}
    assert usb.candidates(devices, UID, "USBTMC") == [("USBTMC", VISA)]
    assert usb.candidates(devices, UID, "CDC") == [("CDC", "COM10")]


def test_target_enumeration_timeout(monkeypatch, tmp_path):
    monkeypatch.setattr(usb, "discover", lambda: {"serial": [], "visa": []})
    clock = iter([0, 99])
    monkeypatch.setattr(usb.time, "monotonic", lambda: next(clock))
    with pytest.raises(RuntimeError, match="did not re-enumerate"):
        usb.await_target(args(tmp_path), report())


def test_wrong_reenumerated_identity_cannot_pass(monkeypatch, tmp_path):
    fake_connection(monkeypatch)
    monkeypatch.setattr(usb, "discover", lambda: {"serial": [], "visa": [VISA]})
    monkeypatch.setattr(usb.Session, "identity", lambda _: (_ for _ in ()).throw(
        RuntimeError("board build mismatch")))
    clock = iter([0, 99])
    monkeypatch.setattr(usb.time, "monotonic", lambda: next(clock))
    data = report()
    with pytest.raises(RuntimeError, match="did not re-enumerate"):
        usb.await_target(args(tmp_path), data)
    assert "build mismatch" in data["enumeration"][0]["error"]


def test_verify_only_is_read_only(monkeypatch, tmp_path):
    fake_connection(monkeypatch)
    commands = []

    def command(self, cmd, **_):
        commands.append(cmd)
        return {"*IDN?": f"H,DHRT100,{UID},1", "SYST:FW:BUILD?": BUILD,
                "SYST:USB:MODE?": '"CDC"'}[cmd]

    monkeypatch.setattr(usb.Session, "command", command)
    usb.run(args(tmp_path, "--port", "COM10", "--verify-only"), report())
    assert len(commands) == 3 and all(cmd.endswith("?") for cmd in commands)


def test_failed_run_saved_and_output_not_overwritten(monkeypatch, tmp_path):
    calls = []

    def fail(_args, data):
        calls.append(True)
        data["exchanges"].append({"command": "*IDN?", "response": "bad"})
        raise RuntimeError("identity failed")

    monkeypatch.setattr(usb, "run", fail)
    path = tmp_path / "report.json"
    argv = ["--serial-number", UID, "--build", BUILD, "--out", str(path)]
    assert usb.main(argv) == 1
    data = json.loads(path.read_text(encoding="utf-8"))
    assert not data["passed"] and "identity failed" in data["error"]
    assert data["exchanges"][0]["response"] == "bad"
    with pytest.raises(FileExistsError):
        usb.main(argv)
    assert len(calls) == 1


def test_switch_requires_stop_and_readback_before_boot(monkeypatch, tmp_path):
    fake_connection(monkeypatch)
    calls = []
    monkeypatch.setattr(usb.Session, "identity", lambda _: {"mode": "CDC"})
    monkeypatch.setattr(usb.Session, "stop", lambda _: calls.append("STOP"))

    def command(self, cmd, **_):
        calls.append(cmd)
        return '"OK"' if cmd == "SYST:USB:MODE USBTMC" else '"USBTMC"'

    monkeypatch.setattr(usb.Session, "command", command)
    monkeypatch.setattr(usb.Session, "boot", lambda _: calls.append("BOOT"))
    monkeypatch.setattr(usb, "await_target", lambda *_: {"mode": "USBTMC"})
    data = report()
    usb.run(args(tmp_path, "--port", "COM10"), data)
    assert calls == ["STOP", "SYST:USB:MODE USBTMC", "SYST:USB:MODE?", "BOOT"]
    assert data["final"]["mode"] == "USBTMC"


def test_stop_failure_prevents_mode_write(monkeypatch, tmp_path):
    fake_connection(monkeypatch)
    monkeypatch.setattr(usb.Session, "identity", lambda _: {"mode": "CDC"})
    monkeypatch.setattr(usb.Session, "stop", lambda _: (_ for _ in ()).throw(
        RuntimeError("not idle")))
    monkeypatch.setattr(usb.Session, "command", lambda *_: pytest.fail("unexpected write"))
    with pytest.raises(RuntimeError, match="not idle"):
        usb.run(args(tmp_path, "--port", "COM10"), report())


def test_runtime_parser_rejects_incomplete_snapshot():
    with pytest.raises(RuntimeError, match="invalid TDMA"):
        usb.parse_ring("0,0")


def test_stop_reads_current_sequence_command_and_owner_snapshot(monkeypatch, tmp_path):
    commands = []
    values = ["0"] * len(usb.RUNTIME_FIELDS)
    # Fixture uses the sequence acceptance schema and current firmware commands.
    from tools.hardware_acceptance.sequence_trigger_acceptance import STATUS_FIELDS
    sequence = ["IDLE" if key == "state" else "NONE" if key == "error" else "0"
                for key in STATUS_FIELDS]
    replies = {"TRIG:STOP": "1", "TRIG:SEQ:NEXT?": ",".join(sequence),
               "READ:IO:STAT?": "0,0,0,0,0", "SYST:TDMA:RING:STOP": "1",
               "SYST:TDMA:RING:STAT?": ",".join(values), "SYST:ERR?": '0,"No error"'}

    def command(self, cmd, **_):
        commands.append(cmd)
        return replies[cmd]

    monkeypatch.setattr(usb.Session, "command", command)
    usb.Session(("CDC", "COM10"), object(), args(tmp_path), report()).stop()
    assert "TRIG:SEQ:NEXT?" in commands
    assert "SYST:TDMA:RING:STAT?" in commands


def test_optional_visa_stop_payload_timeout_requires_explicit_opt_in(monkeypatch, tmp_path):
    class VisaTimeout(Exception):
        error_code = -1073807339

    def timeout(*_):
        raise VisaTimeout("no payload")

    monkeypatch.setattr(usb, "visa_command", timeout)
    session = usb.Session(("USBTMC", VISA), object(), args(tmp_path), report())
    assert session.command("SYST:TDMA:RING:STOP", allow_timeout=True) == "<timeout>"
    with pytest.raises(VisaTimeout):
        session.command("SYST:USB:MODE USBTMC")
