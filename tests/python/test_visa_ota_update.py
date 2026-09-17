"""OTA orchestration rejects wrong identities, invalid packages and child failures."""
from contextlib import contextmanager
import json
from pathlib import Path
import subprocess
import sys

import pytest

from tools.visa_ota_update import visa_ota_update as target
from tools.hardware_acceptance.sequence_single_board_gate import validate_ota_summary

UID = "839E1AE79EA20F31"
BUILD = "20260917000001"
RESOURCE = f"USB0::0xCAFE::0x4030::{UID}::INSTR"


@pytest.fixture
def bench(tmp_path, monkeypatch):
    package = tmp_path / "update.pkg"
    fmt = target.package_format
    package.write_bytes(fmt.build_package(b"image A", b"image B", product_id="DHRT100",
        hardware_id="dhrt100", app_version=(1, 0, 0), build_id=BUILD,
        min_bootloader_version=(0, 1, 0), layout=fmt.DeploymentLayout(
            fmt.AppPartition(0x20000, 0x100000), fmt.AppPartition(0x120000, 0x100000))))
    out = tmp_path / "run"
    cli = [RESOURCE, str(package), "--serial-number", UID, "--expected-build", BUILD,
           "--out-dir", str(out)]
    state = dict(calls=[], queries=[], send_rc=0, commit_rc=0, uid=UID,
                 current='"old-build"', child=None, missing_summary=False)

    @contextmanager
    def open_visa(*_):
        yield object()

    def query(_inst, command, _timeout):
        state["queries"].append(command)
        return f"HAOFV,DHRT100,{state['uid']},1" if command == "*IDN?" else state["current"]

    def run(command, **kwargs):
        state["calls"].append(command)
        is_send = command[1] == str(target.SEND_TOOL)
        if is_send:
            output = (f"idn=HAOFV,DHRT100,{UID},1\nsize={package.stat().st_size}\n"
                      f"crc32=0x{fmt.crc32(package.read_bytes()):08X}\n"
                      "status=READY_TO_REBOOT\nboot=requested\n").encode()
        else:
            output = b"commit raw output\n"
            commit_dir = Path(command[command.index("--out-dir") + 1])
            commit_dir.mkdir()
            summary = state["child"] or dict(passed=True, failed=0, failures=[], serial_number=UID,
                expected_build=BUILD, records=[
                    dict(command="SYSTem:FW:BUILD?", response=f'"{BUILD}"'),
                    dict(command="SYSTem:OTA:COMMit", response="1"),
                    dict(command="SYSTem:OTA:SLOT?", response="1,0,1,0,0"),
                    dict(command="SYSTem:ERRor?", response='0,"No error"')])
            if not state["missing_summary"]:
                (commit_dir / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
        kwargs["stdout"].write(output)
        kwargs["stderr"].write(b"raw stderr\xff\n")
        if state.get("raise_child"):
            raise subprocess.TimeoutExpired(command, kwargs["timeout"])
        return subprocess.CompletedProcess(command, state["send_rc" if is_send else "commit_rc"])

    monkeypatch.setattr(target, "open_visa_resource", open_visa)
    monkeypatch.setattr(target, "visa_command", query)
    monkeypatch.setattr(target.subprocess, "run", run)
    return cli, out, package, state


def read(out):
    return json.loads((out / "summary.json").read_text(encoding="utf-8"))


def test_success_is_gate_compatible_and_binds_all_artifacts(bench):
    cli, out, package, state = bench
    assert target.main(cli) == 0
    summary = read(out)
    validate_ota_summary(summary, serial_number=UID, build_id=BUILD, package=package)
    assert summary["package"]["sha256"] == target.sha256(package)
    assert summary["package"]["header_reserved"] == 0
    assert "header_crc32" not in summary["package"]
    assert summary["preflight"]["current_build"] == "old-build"
    assert len(state["calls"]) == 2 and "--boot" in state["calls"][0]
    assert "--skip-boot" in state["calls"][1]
    assert not summary["results"][0]["forced_continue"]
    assert (out / "send.stderr.log").read_bytes() == b"raw stderr\xff\n"
    for stage in ("send", "commit"):
        step = summary["results"][0][stage]
        assert step["returncode"] == 0 and step["started_at"] and step["ended_at"]
        assert step["elapsed_s"] >= 0 and step["stderr_artifact"]["sha256"]
    assert summary["results"][0]["commit"]["summary"]["passed"]


def test_asrl_success_requires_matching_fallback_and_live_identity(bench):
    cli, out, package, state = bench
    cli[0] = "ASRL8::INSTR"
    cli += ["--cdc-fallback-port", "COM8"]
    assert target.main(cli) == 0
    summary = read(out)
    validate_ota_summary(summary, serial_number=UID, build_id=BUILD, package=package)
    assert summary["results"][0]["send"]["command"][2] == "ASRL8::INSTR"
    assert state["queries"] == ["*IDN?", "SYST:FW:BUILD?"]


def test_asrl_mismatched_fallback_is_rejected_before_query(bench):
    cli, out, _, state = bench
    cli[0] = "ASRL8::INSTR"
    cli += ["--cdc-fallback-port", "COM9"]
    assert target.main(cli) == 1
    assert not state["queries"] and not state["calls"]
    assert "identity binding" in read(out)["failure"]


@pytest.mark.parametrize("change", ["uid", "resource", "build", "crc", "current", "current_expected"])
def test_preflight_rejects_before_any_child(bench, change):
    cli, out, package, state = bench
    if change == "uid": state["uid"] = "wrong"
    if change == "resource": cli[0] = RESOURCE.replace(UID, "wrong")
    if change == "build": cli[cli.index("--expected-build") + 1] = "wrong-build"
    if change == "crc":
        data = bytearray(package.read_bytes()); data[-1] ^= 1; package.write_bytes(data)
    if change == "current": state["current"] = "<timeout>"
    if change == "current_expected": cli += ["--expected-current-build", "wrong-build"]
    assert target.main(cli) == 1
    assert not state["calls"] and not read(out)["passed"]
    if change == "uid": assert state["queries"] == ["*IDN?"]


@pytest.mark.parametrize("change", ["send_rc", "commit_rc", "raise_child", "missing_summary", "bad_summary"])
def test_child_failure_cannot_be_rewritten_as_success(bench, change):
    cli, out, _, state = bench
    if change in ("send_rc", "commit_rc"): state[change] = 7
    elif change == "bad_summary": state["child"] = dict(passed=False, failures=["bad build"])
    else: state[change] = True
    assert target.main(cli) == 1
    summary = read(out)
    assert not summary["passed"] and summary["failure"]
    if change in ("send_rc", "raise_child"): assert len(state["calls"]) == 1
    assert (out / "send.stdout.log").exists()
    if change == "commit_rc":
        assert summary["results"][0]["commit"]["summary_artifact"]["sha256"]


def test_commit_summary_uid_cannot_be_faked(bench):
    cli, out, _, state = bench
    state["child"] = dict(passed=True, failed=0, failures=[], serial_number="wrong", expected_build=BUILD)
    assert target.main(cli) == 1
    assert "identity/build" in read(out)["failure"]


@pytest.mark.parametrize("slot", ["0,0,0,0,0", "3,0,3,0,0"])
def test_commit_rejects_invalid_equal_active_confirmed_slots(bench, slot):
    cli, out, _, state = bench
    state["child"] = dict(passed=True, failed=0, failures=[], serial_number=UID,
        expected_build=BUILD, records=[
            dict(command="SYSTem:FW:BUILD?", response=f'"{BUILD}"'),
            dict(command="SYSTem:OTA:COMMit", response="1"),
            dict(command="SYSTem:OTA:SLOT?", response=slot),
            dict(command="SYSTem:ERRor?", response='0,"No error"')])
    assert target.main(cli) == 1
    assert "not A or B" in read(out)["failure"]


def test_existing_output_refused_before_hardware(bench):
    cli, out, _, state = bench
    out.mkdir()
    with pytest.raises(FileExistsError): target.main(cli)
    assert not state["queries"] and not state["calls"]


@pytest.mark.parametrize("option", ["--timeout", "--step-timeout", "--reopen-timeout"])
def test_invalid_deadline_rejected(bench, option):
    with pytest.raises(SystemExit): target.parse_args(bench[0] + [option, "nan"])


def test_real_subprocess_keeps_exact_output_bytes(tmp_path):
    result = {}
    target.run_child([sys.executable, "-c", "import os; os.write(1,b'out\\xff'); os.write(2,b'err\\r\\n')"],
                     tmp_path, "real", result, 10)
    assert (tmp_path / "real.stdout.log").read_bytes() == b"out\xff"
    assert (tmp_path / "real.stderr.log").read_bytes() == b"err\r\n"
    assert result["returncode"] == 0
    assert not result["passed"]  # semantic validation is a separate mandatory gate


def test_real_subprocess_timeout_retains_partial_output(tmp_path):
    result = {}
    with pytest.raises(subprocess.TimeoutExpired):
        target.run_child([sys.executable, "-c", "import os,time; os.write(1,b'before'); time.sleep(10)"],
                         tmp_path, "timeout", result, .5)
    assert (tmp_path / "timeout.stdout.log").read_bytes() == b"before"
    assert not result["passed"] and "TimeoutExpired" in result["exception"]
