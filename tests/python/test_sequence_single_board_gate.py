import hashlib
import json
import copy
from pathlib import Path

import pytest

from tools.hardware_acceptance import sequence_single_board_gate as gate
from tools.hardware_acceptance.sequence_trigger_acceptance import AcceptanceError
from tools.visa_ota_update import visa_ota_update as visa


def cycle_report(tool_sha256: str, profile: str = "finite") -> dict:
    diagnostics = {
        command: {"error_before": '0,"No error"',
                  "error_after": '0,"No error"', "response": "0"}
        for command in ("READ:SEQ:LINK:TRANSPORT?", "SYST:TDMA:FLIGHT:PROCESS?",
                        "SYST:TDMA:FLIGHT:FIFO?", "SYST:REFMEM:SYNC:FLIGHT?")
    }
    diagnostics["READ:SEQ:LINK:TRANSPORT?"]["parsed"] = {
        "snapshot_quality": 1, "snapshot_quality_name": "FRESH"}
    pause = profile == "pause_resume"
    report = {
        "passed": True,
        "scope": "single_board_rj45_dut_vna_functional_cycle",
        "functional_cycle_verified": True,
        "tdma_stability_verified": False,
        "multi_board_verified": False,
        "independent_input_count_verified": False,
        "waveform_verified": False,
        "rf_path_verified": False,
        "p3_receipt": False,
        "failure": None,
        "cleanup_failures": [],
        "tool_sha256": tool_sha256,
        "settings": {"serial_number": "board-1", "build": "build-1",
                     "gui_control": True, "minimum_events": gate.MINIMUM_EVENTS,
                     "repeat": 0 if pause else gate.FINITE_REPEAT,
                     "pause_resume": pause, "scpi_next": True},
        "transport_before_cleanup": diagnostics,
        "stopped": {"io": {name: 0 for name in ("outputs", "owned", "armed", "busy")}},
        "after_stop_quiet": {"io": {
            name: 0 for name in ("outputs", "owned", "armed", "busy")}},
        "ring_stop_readbacks": [{"tdma": [0] * (
            max(gate.cycle.ring.RING_ENABLED,
                gate.cycle.ring.RING_ADAPTER_STARTED) + 1)}],
        "samples": [{"link": {"phase": 3, "exchange_id": 7}}],
        "ready_injection": {
            "identity": [1, 2, 3],
            "command": f"TRIG:SEQ:INJECT READY,{gate.cycle.READY_BATCH_MAX if pause else 8 * gate.FINITE_REPEAT}",
            "count": gate.cycle.READY_BATCH_MAX if pause else 8 * gate.FINITE_REPEAT,
            "response": "1",
        },
    }
    if pause:
        report["pause_resume"] = {"passed": True, "exchange_rotated": True}
    else:
        report["repeat_result"] = (
            f"{gate.FINITE_REPEAT},{gate.FINITE_REPEAT},1")
    return report


def ota_summary(package: str) -> dict:
    report = {"passed": True, "results": [{
        "board": {"serial_number": "board-1"},
        "passed": True,
        "forced_continue": False,
        "send": {"passed": True, "returncode": 0, "command": ["ota_send.py", package]},
        "commit": {"passed": True, "returncode": 0, "command": [
            "ota_boot_commit.py", "--expected-build", "build-1"]},
    }]}
    path = Path(package)
    if not path.is_file():
        return report
    metadata = visa.package_info(path, "build-1")
    report.update(package=metadata, preflight=dict(passed=True, serial_number="board-1"),
        tools={str(p.relative_to(gate.ROOT)).replace("\\", "/"): gate.p3.sha256_file(p)
               for p in (Path(visa.__file__), visa.SEND_TOOL, visa.COMMIT_TOOL)})
    child = dict(passed=True, failed=0, failures=[], serial_number="board-1", expected_build="build-1",
        records=[dict(command="SYSTem:FW:BUILD?", response='"build-1"'),
                 dict(command="SYSTem:OTA:COMMit", response="1"),
                 dict(command="SYSTem:OTA:SLOT?", response="1,0,1,0,0"),
                 dict(command="SYSTem:ERRor?", response='0,"No error"')])
    child_path = path.parent / "commit-child.json"
    write_json(child_path, child)
    report["results"][0]["commit"].update(summary=child,
        summary_artifact=dict(path=str(child_path), sha256=gate.p3.sha256_file(child_path)))
    for name in ("send", "commit"):
        stage = report["results"][0][name]
        stdout = (f"idn=HAOFV,DHRT100,board-1,1\nsize={metadata['size']}\n"
                  f"crc32=0x{metadata['crc32']:08X}\nstatus=READY_TO_REBOOT\nboot=requested\n")
        for stream, contents in (("stdout", stdout), ("stderr", "")):
            artifact = path.parent / f"{name}.{stream}.log"
            artifact.write_bytes(contents.encode("utf-8"))
            stage[stream] = contents
            stage[stream + "_artifact"] = dict(path=str(artifact), sha256=gate.p3.sha256_file(artifact))
    return report


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def evidence(path: Path, root: Path) -> dict:
    return {"path": path.relative_to(root).as_posix(),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def owner(steps=0, *, state="READY", run=1, plan=tuple(range(8))):
    index = steps % len(plan)
    return dict(state=state, run_id=run, generation=2, count=len(plan), accepted=steps,
                completed=steps, current_index=index, current_state=plan[index],
                completed_index=index, completed_state=plan[index], cancelled=0,
                error="NONE", faults=0, backend_fault=0)


def zero_io():
    return dict(outputs=0, inputs=0, owned=0, armed=0, busy=0)


def ring_row(value=0):
    return {"tdma": [value] * (max(gate.cycle.ring.RING_ADAPTER_TX_COUNT,
        gate.cycle.ring.RING_ADAPTER_RX_COUNT, gate.cycle.ring.RING_ENABLED,
        gate.cycle.ring.RING_ADAPTER_STARTED) + 1)}


def quiet(row):
    return {key: {"sequence": copy.deepcopy(row), "io": zero_io()}
            for key in ("idle_before_quiet", "idle_after_quiet")}


def common_report(scope, sha, **settings):
    return dict(passed=True, scope=scope, tool_sha256=sha, failure=None, cleanup_failures=[],
        identity={"idn": ["vendor", "model", "board-1", "version"], "build": "build-1"},
        settings=dict(serial_number="board-1", build="build-1", **settings),
        waveform_verified=False, external_waveform_verified=False,
        independent_input_count_verified=False, independent_pulse_count_verified=False,
        multi_board_verified=False, rf_path_verified=False, p3_receipt=False)


def repeat_report(sha):
    report = common_report("single_board_independent_sp8t_repeat", sha,
        source="MANUAL", repeat=10, configure_only=False, source_hz=50)
    row = owner(79, state="IDLE")
    report.update(functional_execution_verified=True, software_input_simulation_verified=True,
                  software_next_sent=79,
        configured_repeat={"configured": 10}, repeat_result=dict(configured=10, active=10, finished=1),
        samples=[{"sequence": owner(0)}, {"sequence": row}], **quiet(row))
    report["cleanup"] = dict(**quiet(row), ring_stop_samples=[ring_row()])
    return report


def start_report(sha):
    report = common_report("single_board_start_status_samples", sha, output_only=False,
                           settle_us=300000, pulse_us=300000, abort_pulse_us=1500000)
    report.update(io_loopback_verified=True, output_pad_sequence_verified=True, phases={})
    for name, mode in (("pulse", "PULSE"), ("level", "LEVEL"), ("none", "NONE"),
                       ("pulse_reload", "PULSE"), ("singleton", "PULSE"),
                       ("stop_startup", "PULSE"), ("restart_after_stop", "PULSE")):
        single = name == "singleton"
        plan = gate.start.PLAN[:1] if single else gate.start.PLAN
        mask = 0 if mode == "NONE" else 8
        pulse = 1500000 if name == "stop_startup" else 300000
        phase = dict(passed=True, failure=None, configuration={
            "plan": ["SP8T", "4294967295", "1234", str(len(plan)), "0", *map(str, plan)],
            "source": ["MANUAL", "RISING"], "repeat": {"configured": int(single)},
            "output": ["7", str(mask), mode, "300000", str(pulse if mode == "PULSE" else 0), "2", "1"]})
        for key, code, high in (("settling_low", 5, False), ("startup_high", 5, True),
                ("startup_settled", 5, mode == "LEVEL"), ("high_before_stop", 5, True),
                ("next_high", 2, True), ("next_settled", 2, mode == "LEVEL")):
            phase[key] = [dict(owned=7 | mask, outputs=code | (8 if high else 0), inputs=2 if high else 0)]
        first = owner(0, state="IDLE" if single else "READY", plan=plan)
        phase.update(startup_result=[first], startup_quiet=copy.deepcopy(first),
            repeat_result=dict(configured=int(single), active=int(single), finished=int(single)),
            next_result=[owner(1, plan=plan)], next_quiet=owner(1, plan=plan),
            before_stop=owner(0, plan=plan), stopped=owner(0, state="IDLE", plan=plan),
            quiet=quiet(owner(0, state="IDLE", plan=plan)))
        report["phases"][name] = phase
    report["cleanup"] = dict(**quiet(owner(state="IDLE")), ring_stop_samples=[ring_row()])
    return report


def position_entry(events=10, positions=0, phase=9, steps=0, triggers=0, ready=0, run=1, repeat=2):
    return dict(link=dict(enabled=1, phase=phase, error=0, binding_epoch=3, model_epoch=4,
        run=run, generation=2, repeat=repeat, triggers=triggers, ready=ready, completed=steps),
        counter=dict(enabled=1, input=3, threshold=1000, events=events, positions=positions,
                     history_total=triggers, history_retained=triggers, phase=phase, error=0, fault_events=0),
        owner=owner(steps, run=run))


def history(positions=2):
    records = []
    for i in range(positions * 8):
        ordinal, position, index = i + 1, i // 8 + 1, i % 8
        admitted, elapsed = position * 1000, (index + 1) * 20
        records.append(dict(ordinal=ordinal, run=1, generation=2, binding_epoch=3,
            exchange_id=ordinal, position=position, sequence_index=index, sequence_state=index,
            output_code=index, threshold_pulses=position * 1000,
            observed_pulses=position * 1000 + index, trigger_ordinal=ordinal,
            ready_ordinal=ordinal, position_admitted_tick_ms=admitted,
            sample_done_tick_ms=admitted + elapsed, cycle_elapsed_ms=elapsed, outcome_flags=7))
    return records


def position_cycles(positions=2):
    return [dict(position=i, target_ms=200, elapsed_ms=160,
                 threshold_pulse_ordinal=i * 1000) for i in range(1, positions + 1)]


def position_config(gui_sha, *, repeat_count=2, manual=False):
    return dict(passed=True, flight_mode="2", repeat_configuration={"configured": repeat_count},
        input_simulation={"method": "SCPI", "counter_command": "TRIG:SEQ:INJECT IN3,<count>",
                          "ready_command": "TRIG:SEQ:INJECT READY,<count>" if manual else None,
                          "physical_edge_count_verified": False,
                          "physical_ready_verified": not manual},
        gui_control={"module_sha256": gui_sha}, configured={
            "counter": dict(enabled=1, slot=1, input=3, threshold=1000),
            "link": dict(enabled=1, phase=1, error=0, dutslot=2, vnaslot=3,
                         input=0 if manual else 2, outputmask=8)},
        roles={str(i): f"{i},{name},1,1,0,0,0,0,0,0" for i, name in ((2, "COUNTER"), (5, "DUT"), (7, "VNA"))},
        plan='"SP8T",0,0,8,0,0,1,2,3,4,5,6,7', codes={str(i): f"{i},{i}" for i in range(8)})


def position_report(sha, gui_sha):
    report = common_report("single_board_position_two_rounds_and_busy_boundary", sha,
        threshold=1000, lifecycle=True, source_hz=50, duration=55, gateway_timeout_ms=10000, counter_input="IN3",
        position_cycle_target_ms=200)
    report["software_input_simulation_verified"] = True
    for name, manual in (("two_positions", False), ("busy_boundary", True)):
        profile = position_config(gui_sha, manual=manual)
        final = position_entry(2000, 1 if manual else 2, 7 if manual else 8,
                               0 if manual else 15, 1 if manual else 16, 0 if manual else 16)
        terminal = owner(0 if manual else 15, state="IDLE")
        samples = [position_entry(), position_entry(999), position_entry(1000, 1, 3, triggers=1)]
        if manual:
            final["link"]["error"] = final["counter"]["error"] = 5
            final["counter"]["fault_events"] = 2000
            profile["expected_busy_fault"] = copy.deepcopy(final)
        else:
            profile.update(history=history(), position_cycles=position_cycles(),
                           terminal_repeat=dict(configured=2, run=2, finished=1))
        profile.update(injections=[
                dict(command="TRIG:SEQ:INJECT IN3,999", count=999, issued_at=1.0, response="1"),
                dict(command="TRIG:SEQ:INJECT IN3,1", count=1, issued_at=2.0, response="1"),
                dict(command="TRIG:SEQ:INJECT IN3,1000", count=1000, issued_at=3.0, response="1")],
            ready_injection=None,
            input_simulation={
                "method": "SCPI", "counter_command": "TRIG:SEQ:INJECT IN3,<count>",
                "ready_command": "TRIG:SEQ:INJECT READY,<count>" if manual else None,
                "physical_edge_count_verified": False,
                "physical_ready_verified": not manual},
            samples=[*samples, final], no_premature_sample_observed=True,
            terminal_owner_samples=[terminal], ring_before=ring_row(), ring_after=ring_row(1))
        report[name] = profile
    life = position_config(gui_sha, repeat_count=0)
    life["lifecycle_identity"] = dict(binding_epoch=3, model_epoch=4, run=1, generation=2)
    for name, events, phase in (("waiting", 0, 9), ("initial_partial", 1, 9),
                                ("paused", 1, 6), ("counting_while_paused", 4, 6),
                                ("resumed", 4, 9)):
        row = position_entry(events, phase=phase, repeat=0)
        row["owner"]["state"] = "PAUSED" if phase == 6 else "READY"
        life[name] = [row]
    life["position_complete"] = [position_entry(1000, 1, 9, 7, 8, 8, repeat=0)]
    life["restarted"] = [position_entry(run=2, repeat=0)]
    life.update(injections=[
            dict(command="TRIG:SEQ:INJECT IN3,1", count=1, issued_at=1.0, response="1"),
            dict(command="TRIG:SEQ:INJECT IN3,3", count=3, issued_at=2.0, response="1"),
            dict(command="TRIG:SEQ:INJECT IN3,996", count=996, issued_at=3.0, response="1")],
        ready_injection=None,
        input_simulation={
            "method": "SCPI", "counter_command": "TRIG:SEQ:INJECT IN3,<count>",
            "ready_command": None, "physical_edge_count_verified": False,
            "physical_ready_verified": True},
        history=history(1), position_cycles=position_cycles(1),
                stop=owner(7, state="IDLE"), stop_io=zero_io(),
                restart_stop=owner(state="IDLE", run=2), restart_stop_io=zero_io())
    report.update(lifecycle=life, stopped=owner(state="IDLE"), stopped_io=zero_io(),
                  ring_stopped_samples=[ring_row()])
    return report


def receipt_fixture(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    tool = tmp_path / "tools/hardware_acceptance/sequence_tdma_cycle_validate.py"
    tool.parent.mkdir(parents=True)
    tool.write_text("validator\n", encoding="utf-8")
    tool_sha = hashlib.sha256(tool.read_bytes()).hexdigest()
    package = tmp_path / "out/fw.pkg"
    package.parent.mkdir()
    fmt = visa.package_format
    package.write_bytes(fmt.build_package(b"image A", b"image B", product_id="DHRT100",
        hardware_id="dhrt100", app_version=(1, 0, 0), build_id="build-1",
        min_bootloader_version=(0, 1, 0), layout=fmt.DeploymentLayout(
            fmt.AppPartition(0x20000, 0x100000), fmt.AppPartition(0x120000, 0x100000))))
    ota = tmp_path / "out/ota.json"
    finite = tmp_path / "out/finite.json"
    pause = tmp_path / "out/pause.json"
    write_json(ota, ota_summary(str(package.resolve())))
    for relative in json.loads(ota.read_text(encoding="utf-8"))["tools"]:
        target = tmp_path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((gate.ROOT / relative).read_bytes())
    write_json(finite, cycle_report(tool_sha))
    write_json(pause, cycle_report(tool_sha, "pause_resume"))
    changed = ["components/sync_trigger/src/trigger_sequence_link.c"]
    receipt = {
        "schema": gate.RECEIPT_SCHEMA,
        "passed": True,
        "acceptance_scope": gate.ACCEPTANCE_SCOPE,
        "limitations": ["no_p3", "no_multi_board", "no_waveform", "no_rf",
                        "no_independent_edge_count", "no_tdma_stability"],
        "serial_number": "board-1",
        "build_id": "build-1",
        "source_tree_sha256": "tree",
        "source_file_count": 4,
        "changed_sources": changed,
        "validator_sha256": tool_sha,
        "firmware_package": evidence(package, tmp_path),
        "ota_summary": evidence(ota, tmp_path),
        "finite_report": evidence(finite, tmp_path),
        "pause_resume_report": evidence(pause, tmp_path),
    }
    hashes = {}
    for name, relative in gate.VALIDATOR_PATHS.items():
        path = tmp_path / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists():
            path.write_text(name + " validator\n", encoding="utf-8")
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    receipt.update(validator_hashes=hashes, source_hz=50)
    for name, report in (("repeat", repeat_report(hashes["repeat"])),
                         ("start", start_report(hashes["start"])),
                         ("position", position_report(hashes["position"], hashes["gui"]))):
        path = tmp_path / "out" / (name + ".json")
        write_json(path, report)
        receipt[name + "_report"] = evidence(path, tmp_path)
    host = tmp_path / "out/host.json"
    log = tmp_path / "out/host.log"
    log.write_text("4 passed\n", encoding="utf-8")
    xml = tmp_path / "out/host.xml"
    xml.write_text('<testsuites><testsuite tests="4" failures="0" errors="0" skipped="0">' +
        ''.join(f'<testcase classname="{p[:-3].replace(chr(47), chr(46))}" name="test_real"/>'
                for p in gate.HOST_TESTS) + '</testsuite></testsuites>', encoding="utf-8")
    write_json(host, dict(command=gate._host_command(host.parent), returncode=0,
                         log=evidence(log, tmp_path), junit=evidence(xml, tmp_path)))
    receipt["host_report"] = evidence(host, tmp_path)
    monkeypatch.setattr(gate, "changed_staged_sources", lambda root: changed)
    monkeypatch.setattr(gate, "staged_source_fingerprint", lambda root: ("tree", 4))
    monkeypatch.setattr(gate, "working_source_fingerprint", lambda root: ("tree", 4))
    monkeypatch.setattr(gate.p3, "read_index_json", lambda root, path: receipt)
    return receipt, finite


def test_check_staged_accepts_matching_receipt(tmp_path, monkeypatch):
    receipt_fixture(tmp_path, monkeypatch)
    gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_check_staged_rejects_stale_source_fingerprint(tmp_path, monkeypatch):
    receipt_fixture(tmp_path, monkeypatch)
    monkeypatch.setattr(gate, "staged_source_fingerprint", lambda root: ("new-tree", 4))
    monkeypatch.setattr(gate, "working_source_fingerprint", lambda root: ("new-tree", 4))
    with pytest.raises(AcceptanceError, match="no matching"):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_check_staged_rejects_modified_evidence(tmp_path, monkeypatch):
    _, finite = receipt_fixture(tmp_path, monkeypatch)
    finite.write_text("{}", encoding="utf-8")
    with pytest.raises(AcceptanceError, match="missing or changed"):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_scope_rejects_non_allowlisted_firmware():
    with pytest.raises(AcceptanceError, match="outside"):
        gate.validate_scope(["drivers/flash/src/flash.c"])


def test_report_rejects_unavailable_transport_snapshot():
    report = cycle_report("a" * 64)
    report["transport_before_cleanup"]["READ:SEQ:LINK:TRANSPORT?"]["parsed"][
        "snapshot_quality"] = 0
    with pytest.raises(AcceptanceError, match="coherent attributed"):
        gate.validate_cycle_report(report, profile="finite", serial_number="board-1",
                                   build_id="build-1", tool_sha256="a" * 64)


def test_report_rejects_pause_without_exchange_rotation():
    report = cycle_report("a" * 64, "pause_resume")
    report["pause_resume"]["exchange_rotated"] = False
    with pytest.raises(AcceptanceError, match="exchange rotation"):
        gate.validate_cycle_report(report, profile="pause_resume",
                                   serial_number="board-1", build_id="build-1",
                                   tool_sha256="a" * 64)


@pytest.mark.parametrize("mutation", ["missing", "duplicate", "rejected"])
def test_report_rejects_invalid_scpi_ready_batch(mutation):
    report = cycle_report("a" * 64)
    if mutation == "missing":
        report.pop("ready_injection")
    elif mutation == "duplicate":
        report["ready_injection"]["count"] -= 1
    else:
        report["ready_injection"]["response"] = "0"
    with pytest.raises(AcceptanceError, match="SCPI READY"):
        gate.validate_cycle_report(report, profile="finite", serial_number="board-1",
                                   build_id="build-1", tool_sha256="a" * 64)


def test_ota_summary_rejects_another_package(tmp_path):
    requested = tmp_path / "current.pkg"
    requested.write_bytes(b"current")
    summary = ota_summary(str(tmp_path / "old.pkg"))
    with pytest.raises(AcceptanceError, match="supplied firmware package"):
        gate.validate_ota_summary(summary, serial_number="board-1",
                                  build_id="build-1", package=requested)


def test_precommit_routes_only_allowlisted_source_to_single_board_gate():
    hook = (gate.ROOT / ".githooks/pre-commit").read_text(encoding="utf-8")
    assert "sequence_single_board_gate.py covers-staged" in hook
    assert "sequence_single_board_gate.py check-staged" in hook
    assert hook.index("covers-staged") < hook.index("p3_hardware_acceptance.py check-staged")
    assert "|tools/*|tests/*)" in hook


@pytest.mark.parametrize("profile,mutation", [
    ("repeat", "software_next"), ("repeat", "wrong_count"), ("repeat", "idle_drift"),
    ("repeat", "configure_only"), ("start", "missing_phase"), ("start", "cable"),
    ("start", "startup_advance"), ("start", "none_status"), ("position", "history"),
    ("position", "no_busy_fault"), ("position", "paused_counter"), ("position", "restart"),
    ("position", "no_rj45"), ("position", "short_duration"), ("position", "gui_hash"),
    ("position", "no_lifecycle"), ("position", "wrong_build"),
])
def test_rehashed_profile_tampering_is_rejected(tmp_path, monkeypatch, profile, mutation):
    receipt, _ = receipt_fixture(tmp_path, monkeypatch)
    path = tmp_path / receipt[profile + "_report"]["path"]
    report = json.loads(path.read_text(encoding="utf-8"))
    if mutation == "software_next": report["software_next_sent"] = 1
    elif mutation == "wrong_count": report["samples"][-1]["sequence"]["completed"] = 78
    elif mutation == "idle_drift": report["idle_after_quiet"]["sequence"]["accepted"] += 1
    elif mutation == "configure_only": report["settings"]["configure_only"] = True
    elif mutation == "missing_phase": del report["phases"]["stop_startup"]
    elif mutation == "cable": report["phases"]["pulse"]["startup_high"][-1]["inputs"] = 0
    elif mutation == "startup_advance": report["phases"]["level"]["startup_result"][-1]["accepted"] = 1
    elif mutation == "none_status": report["phases"]["none"]["startup_settled"][-1]["outputs"] |= 8
    elif mutation == "history": report["two_positions"]["history"][3]["observed_pulses"] = 2000
    elif mutation == "no_busy_fault": report["busy_boundary"]["samples"][-1]["counter"]["error"] = 0
    elif mutation == "paused_counter": report["lifecycle"]["counting_while_paused"][-1]["counter"]["events"] = 15
    elif mutation == "restart": report["lifecycle"]["restarted"][-1]["link"]["run"] = 1
    elif mutation == "no_rj45": report["two_positions"]["ring_after"] = report["two_positions"]["ring_before"]
    elif mutation == "short_duration": report["settings"]["duration"] = 54
    elif mutation == "gui_hash": report["two_positions"]["gui_control"]["module_sha256"] = "old"
    elif mutation == "no_lifecycle": del report["lifecycle"]
    elif mutation == "wrong_build": report["identity"]["build"] = "old"
    write_json(path, report)
    receipt[profile + "_report"] = evidence(path, tmp_path)
    with pytest.raises(AcceptanceError):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


@pytest.mark.parametrize("mutation", ["v1", "unstaged", "validator", "missing_report", "source_hz"])
def test_v2_identity_rejects_stale_or_missing_evidence(tmp_path, monkeypatch, mutation):
    receipt, _ = receipt_fixture(tmp_path, monkeypatch)
    if mutation == "v1": receipt["schema"] = "HAOFV_SEQUENCE_SINGLE_BOARD_RECEIPT_V1"
    elif mutation == "unstaged": monkeypatch.setattr(gate, "working_source_fingerprint", lambda root: ("dirty", 4))
    elif mutation == "validator": receipt["validator_hashes"]["start"] = "old"
    elif mutation == "missing_report": del receipt["repeat_report"]
    elif mutation == "source_hz": receipt["source_hz"] = 100
    with pytest.raises(AcceptanceError):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


@pytest.mark.parametrize("mutation", ["skip", "failure", "empty", "wrong_module", "short_command", "log"])
def test_host_evidence_requires_complete_fixed_regression(tmp_path, monkeypatch, mutation):
    receipt, _ = receipt_fixture(tmp_path, monkeypatch)
    path = tmp_path / receipt["host_report"]["path"]
    host = json.loads(path.read_text(encoding="utf-8"))
    xml = tmp_path / host["junit"]["path"]
    text = xml.read_text(encoding="utf-8")
    if mutation == "skip": text = text.replace('skipped="0"', 'skipped="1"')
    elif mutation == "failure": text = text.replace('</testsuite>', '<failure/></testsuite>')
    elif mutation == "empty": text = '<testsuites/>'
    elif mutation == "wrong_module": text = text.replace('test_refmem_layout', 'test_unrelated')
    elif mutation == "short_command": host["command"].remove(gate.HOST_TESTS[0])
    elif mutation == "log": (tmp_path / host["log"]["path"]).write_text("changed", encoding="utf-8")
    xml.write_text(text, encoding="utf-8")
    host["junit"] = evidence(xml, tmp_path)
    write_json(path, host)
    receipt["host_report"] = evidence(path, tmp_path)
    with pytest.raises(AcceptanceError):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


@pytest.mark.parametrize("mutation", ["same_path_package", "child_log", "child_summary", "returncode", "legacy"])
def test_ota_original_artifacts_are_bound(tmp_path, monkeypatch, mutation):
    receipt, _ = receipt_fixture(tmp_path, monkeypatch)
    package = tmp_path / receipt["firmware_package"]["path"]
    ota = tmp_path / receipt["ota_summary"]["path"]
    report = json.loads(ota.read_text(encoding="utf-8"))
    if mutation == "same_path_package": package.write_bytes(package.read_bytes() + b"replacement")
    elif mutation == "child_log": Path(report["results"][0]["send"]["stdout_artifact"]["path"]).write_text("changed", encoding="utf-8")
    elif mutation == "child_summary": Path(report["results"][0]["commit"]["summary_artifact"]["path"]).write_text("{}", encoding="utf-8")
    elif mutation == "returncode": report["results"][0]["send"]["returncode"] = 1
    elif mutation == "legacy": del report["preflight"]
    with pytest.raises(AcceptanceError):
        gate.validate_ota_summary(report, serial_number="board-1", build_id="build-1", package=package, root=tmp_path)


@pytest.mark.parametrize("source_hz,duration", [(50, 55), (100, 35), (1000, 17)])
def test_position_window_tracks_declared_frequency(source_hz, duration):
    assert gate.position_duration(source_hz) == duration
    args = gate.argparse.Namespace(serial_number="board-1", build="build-1", port="COM3",
                                   visa_resource=None, source_hz=source_hz)
    cli = gate._profile_args(args, Path("out/position.json"), "position")
    assert float(cli[cli.index("--duration") + 1]) == duration
    assert cli[cli.index("--gateway-timeout-ms") + 1] == "10000"
    assert "--lifecycle" in cli and "1000" == cli[cli.index("--threshold") + 1]
    assert cli[cli.index("--counter-input") + 1] == "IN3"


@pytest.mark.parametrize("frequency", [0, -1, float("nan"), float("inf"), None, True])
def test_invalid_source_frequency_is_rejected(frequency):
    with pytest.raises(AcceptanceError):
        gate.position_duration(frequency)


def test_scope_expansion_is_exact_and_excludes_unrelated_tdma():
    gate.validate_scope(["application/src/app.c", "tools/visa_ota_update/visa_ota_update.py",
                         "tests/python/test_refmem_layout.py",
                         "application/src/app_runtime.c",
                         "components/tdma/src/tdma_service.c",
                         "components/tdma/src/tdma_runtime_owner.c",
                         "components/vdc_domain/src/vdc_timestamp_clock.c",
                         "middleware/scpi_port/src/scpi_system_snapshot_commands.c",
                         "tests/unit/test_trigger_sequence_history_vector.c",
                         "tools/hardware_acceptance/sequence_timing_analyze.py"])
    with pytest.raises(AcceptanceError, match="outside"):
        gate.validate_scope(["components/tdma/src/tdma_ring_runtime.c"])
    assert not any("*" in path for path in gate.SOURCE_ALLOWLIST)
