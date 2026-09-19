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


def repeat_report(sha, source_hz=50):
    report = common_report("single_board_independent_sp8t_repeat", sha,
        source="MANUAL", repeat=10, configure_only=False, source_hz=source_hz)
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


def position_report(sha, gui_sha, source_hz=50):
    report = common_report("single_board_position_two_rounds_and_busy_boundary", sha,
        threshold=1000, lifecycle=True, source_hz=source_hz, duration=55, gateway_timeout_ms=10000, counter_input="IN3",
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


def calibration_report(sha):
    runs = []
    for run in range(1, gate.CALIBRATION_RUNS + 1):
        epoch = 10 + run
        raw = [0, 1, 250000000, 4, gate.CALIBRATION_WORDS, 15, 5, 5,
               epoch, 92, 144, 164, 216, 0, 124, 104, 52, 0]
        measured = dict(zip(gate.calibration.SNAPSHOT_FIELDS, raw))
        measured.update(raw_fields=raw, clk_path_ns=52, data_path_ns=52,
                        computed_residence_ns=20,
                        computed_raw_path_sum_ns=104)
        runs.append(dict(run=run, baseline_epoch=run, snapshot=measured,
                         epoch_unique=True, raw_measurement_passed=True,
                         firmware_calibration_passed=False,
                         active_eligible=False))
    schedule = dict(enabled_mask=11, quarantined_mask=3,
                    raw_fields=[1, 250000000, 250000, 8, 11, 3, 99, 0])
    records = []
    windows = []
    for run, row in enumerate(runs, 1):
        start = float(run * 10)
        records.append({"run": run, "command": "CAL:LOOP:START 128",
                        "phase": "start", "response": "OK",
                        "host_monotonic_start_s": start,
                        "host_monotonic_end_s": start + 0.1})
        windows.append({"run": run, "host_monotonic_start_s": start + 0.2,
                        "host_monotonic_end_s": start + 5.2,
                        "elapsed_s": 5.0, "records_before": len(records),
                        "records_after": len(records)})
        records.append({"run": run, "command": "READ:CAL:LOOP?",
                        "phase": "terminal",
                        "response": ",".join(map(str, row["snapshot"]["raw_fields"])),
                        "host_monotonic_start_s": start + 5.3,
                        "host_monotonic_end_s": start + 5.4})
    return {
        "schema": gate.calibration.SCHEMA,
        "tool_sha256": sha,
        "scope": "single_board_physical_rj45_reference_loopback",
        "transport": {"kind": "USBTMC", "endpoint": "USB::FAKE"},
        "identity": {"idn": "HAOFV,DHRT100,board-1,1",
                     "serial_number": "board-1", "build": "build-1"},
        "settings": {"serial_number": "board-1", "expected_build": "build-1",
                     "words": gate.CALIBRATION_WORDS,
                     "runs": gate.CALIBRATION_RUNS,
                     "duration_s": gate.CALIBRATION_DURATION_S,
                     "quiet_capture": True},
        "runs_requested": gate.CALIBRATION_RUNS,
        "runs_completed": gate.CALIBRATION_RUNS,
        "runs_raw_passed": gate.CALIBRATION_RUNS,
        "run_results": runs,
        "quiet_windows": windows,
        "raw_measurement_passed": True,
        "firmware_calibration_passed": False,
        "active_eligible": False,
        "formal_calibration_claimed": False,
        "load_guard": {"before": schedule, "after": copy.deepcopy(schedule),
                       "unchanged": True},
        "final_error": '0,"No error"',
        "failure": None,
        "cleanup_failures": [],
        "passed": True,
        "limitations": list(gate.calibration.LIMITATIONS),
        "evidence_boundary": "Raw loopback; not formal calibration.",
        "records": records,
    }


def independent_source_report(sha, source_hz=50):
    """Physical IN1 finite repeat: the same structure with no software NEXT."""
    report = repeat_report(sha, source_hz)
    report["settings"]["source"] = "IN1"
    report["software_input_simulation_verified"] = False
    report["software_next_sent"] = 0
    return report


def dual_slot_physical_report(sha):
    """Dual-slot cycle whose READY credits come from the cabled OUT4->IN2 edge."""
    report = cycle_report(sha)
    report["settings"].update(scpi_next=False, ready_input="IN2")
    report.pop("ready_injection")
    report["link_configuration"] = dict(enabled=1, phase=1, error=0, dutslot=2,
                                        vnaslot=3, input=2, outputmask=8)
    return report


def external_timing(record, clock_hz=250000000):
    ticks = record["cycle_elapsed_ms"] * clock_hz // 1000
    return dict(version=1, clock_hz=clock_hz, ordinal=record["ordinal"], run=record["run"],
                generation=record["generation"], binding_epoch=record["binding_epoch"],
                exchange_id=record["exchange_id"], position=record["position"],
                sequence_index=record["sequence_index"], flags=63, request_ticks=ticks,
                applied_ticks=ticks, offered_ticks=ticks, returned_ticks=ticks,
                fire_queued_ticks=ticks, done_ticks=ticks)


def external_history(positions=30):
    records = history(positions)
    for record in records:
        record["timing"] = external_timing(record)
    return records


def external_vector(clock_hz=250000000, **overrides):
    vector = dict(version=1, clock_hz=clock_hz, capacity=256, run=0, generation=2,
                  binding_epoch=3, threshold=1000, total=0, retained=0, overwritten=0)
    vector.update(overrides)
    return vector


def turntable_external_report(sha, source_hz=50):
    """Physical IN1 30-position quiet-capture turntable profile; no injections."""
    positions = gate.POSITION_EXTERNAL_POSITIONS
    measurements = positions * 8
    wait_s = gate.external_position_wait_s(source_hz)
    vector = external_vector(run=1, total=measurements, retained=measurements)
    profile = dict(
        passed=True, probe_mode="post_run_vector", samples=[],
        no_premature_sample_observed=False,
        input_simulation=dict(gate.EXTERNAL_POSITION_INPUT_SIMULATION),
        observation_limits={"scope": "post-run retained history and natural terminal state"},
        quiet_capture=dict(planned_wait_s=wait_s, source_hz_is_declared=True,
                           command_ordinal_base=1, start_command_ordinal=1, start_response="1",
                           transcript_count_after_start=1,
                           transcript_count_before_first_query=1, first_query_ordinal=2,
                           commands_during_wait=0, elapsed_s=wait_s),
        history=external_history(positions),
        history_vector_before=external_vector(),
        history_vector_after=vector,
        history_vector_after_drain=copy.deepcopy(vector),
        history_thresholds_verified=True,
        position_cycles=position_cycles(positions),
        terminal_repeat=dict(configured=positions, run=positions, finished=1),
        terminal_owner_samples=[owner(measurements - 1, state="IDLE")],
        backend_timing=["PIO0", "4", "0"],
        error_before='0,"No error"', error_after='0,"No error"',
        ring_before=ring_row(), ring_after=ring_row(1))
    report = common_report(gate.EXTERNAL_POSITION_SCOPE, sha,
        external_input=True, counter_input="IN1", positions=positions, threshold=1000,
        quiet_capture=True, timing_evidence=True, schedule_evidence=True, angle_scan=False,
        lifecycle=False, source_hz=source_hz,
        duration=gate.external_position_duration(source_hz),
        gateway_timeout_ms=10000, position_cycle_target_ms=200)
    report.update(software_input_simulation_verified=False,
        two_positions=profile,
        transcript=[{"command": "READ:SEQ:HIST:STAT?", "response": "1"}],
        scheduler={"before_cleanup": dict(error_before='0,"No error"',
            error_after='0,"No error"', version=1, clock_hz=250000000,
            strict_realtime_verified=False)},
        stopped=owner(state="IDLE"), stopped_io=zero_io(),
        ring_stopped_samples=[ring_row()])
    return report


def standalone_sp8t_report(sha):
    """Standalone SP8T pad check that acquires no sequence resource."""
    status = owner(0, state="IDLE")
    positions = [dict(position=position, readback=["1", str(position), "1", "8", "0"],
                      io=dict(inputs=0, outputs=position - 1, owned=0, armed=0, busy=0),
                      status=copy.deepcopy(status))
                 for position in gate.STANDALONE_SP8T_POSITIONS]
    return dict(passed=True, scope=gate.STANDALONE_SP8T_SCOPE, tool_sha256=sha,
        failure=None, identity={"idn": ["vendor", "model", "board-1", "version"],
                                "build": "build-1"},
        settings=dict(serial_number="board-1", build="build-1", mode="standalone-sp8t",
                      port="COM3", visa_resource=None),
        independent_input_count_verified=False, vna_role_runtime_verified=False,
        external_waveform_verified=False, rf_path_verified=False,
        phases={"standalone-sp8t": dict(passed=True, failure=None, cleanup_failure=None,
                                        positions=positions)},
        stopped_status=copy.deepcopy(status), stopped_io=["0"] * 5)


def receipt_fixture(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, *,
                    profile=gate.PROFILE_THREE_MODE, changed=None, source_hz=50):
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
    changed = changed or ["components/sync_trigger/src/trigger_sequence_link.c"]
    receipt = {
        "schema": gate.RECEIPT_SCHEMA,
        "passed": True,
        "acceptance_profile": profile,
        "acceptance_scope": gate.PROFILE_SCOPES[profile],
        "limitations": sorted(gate.PROFILE_LIMITATIONS[profile]),
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
    receipt.update(validator_hashes=hashes, source_hz=source_hz)
    if profile == gate.PROFILE_RESERVATION_FOUNDATION:
        calibration_path = tmp_path / "out/calibration-loopback/summary.json"
        write_json(calibration_path, calibration_report(hashes["calibration"]))
        receipt["calibration_report"] = evidence(calibration_path, tmp_path)
        for key, report in (
                ("independent_source_report", independent_source_report(hashes["repeat"], source_hz)),
                ("dual_slot_physical_report", dual_slot_physical_report(hashes["cycle"])),
                ("turntable_external_report", turntable_external_report(hashes["position"], source_hz)),
                ("standalone_sp8t_report", standalone_sp8t_report(hashes["feedback"]))):
            path = tmp_path / "out" / (key + ".json")
            write_json(path, report)
            receipt[key] = evidence(path, tmp_path)
    for name, report in (("repeat", repeat_report(hashes["repeat"], source_hz)),
                         ("start", start_report(hashes["start"])),
                         ("position", position_report(hashes["position"], hashes["gui"], source_hz))):
        path = tmp_path / "out" / (name + ".json")
        write_json(path, report)
        receipt[name + "_report"] = evidence(path, tmp_path)
    host = tmp_path / "out/host.json"
    log = tmp_path / "out/host.log"
    log.write_text("4 passed\n", encoding="utf-8")
    xml = tmp_path / "out/host.xml"
    host_tests = gate.PROFILE_HOST_TESTS[profile]
    xml.write_text(f'<testsuites><testsuite tests="{len(host_tests)}" failures="0" errors="0" skipped="0">' +
        ''.join(f'<testcase classname="{p[:-3].replace(chr(47), chr(46))}" name="test_real"/>'
                for p in host_tests) + '</testsuite></testsuites>', encoding="utf-8")
    host_report = dict(profile=profile, command=gate._host_command(host.parent, profile),
                       returncode=0, log=evidence(log, tmp_path),
                       junit=evidence(xml, tmp_path))
    if profile == gate.PROFILE_RESERVATION_FOUNDATION:
        vdc_log = tmp_path / "out/vdc-domain-host.log"
        vdc_log.write_text("vdc_domain host unit tests passed\n", encoding="utf-8")
        build_dir = (host.parent / "vdc-domain").relative_to(tmp_path).as_posix()
        host_report["vdc_domain"] = {
            "command": ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                        "-File", "tools/tests/run_vdc_domain_tests.ps1",
                        "-BuildDir", build_dir],
            "returncode": 0,
            "log": evidence(vdc_log, tmp_path),
        }
    write_json(host, host_report)
    receipt["host_report"] = evidence(host, tmp_path)
    monkeypatch.setattr(gate, "changed_staged_sources", lambda root: changed)
    monkeypatch.setattr(gate, "staged_source_fingerprint", lambda root: ("tree", 4))
    monkeypatch.setattr(gate, "working_source_fingerprint", lambda root: ("tree", 4))
    monkeypatch.setattr(gate.p3, "read_index_json", lambda root, path: receipt)
    return receipt, finite


def test_check_staged_accepts_matching_receipt(tmp_path, monkeypatch):
    receipt_fixture(tmp_path, monkeypatch)
    gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_check_staged_accepts_foundation_receipt(tmp_path, monkeypatch):
    changed = ["components/sync_trigger/src/trigger_reservation_clock.c",
               "components/vdc_domain/src/vdc_domain.c",
               "components/vdc_dpll_manager/inc/vdc_timer1_coordinate.h"]
    receipt_fixture(tmp_path, monkeypatch,
                    profile=gate.PROFILE_RESERVATION_FOUNDATION,
                    changed=changed)
    gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


@pytest.mark.parametrize("mutation", ["profile", "scope", "limitation", "vdc_gate", "vdc_log"])
def test_foundation_receipt_rejects_scope_or_host_gate_tampering(
        tmp_path, monkeypatch, mutation):
    changed = ["components/sync_trigger/src/trigger_reservation_clock.c",
               "components/vdc_domain/src/vdc_domain.c"]
    receipt, _ = receipt_fixture(
        tmp_path, monkeypatch, profile=gate.PROFILE_RESERVATION_FOUNDATION,
        changed=changed)
    if mutation == "profile":
        receipt["acceptance_profile"] = gate.PROFILE_THREE_MODE
    elif mutation == "scope":
        receipt["acceptance_scope"] = gate.ACCEPTANCE_SCOPE
    elif mutation == "limitation":
        receipt["limitations"].remove("foundation_only")
    else:
        host_path = tmp_path / receipt["host_report"]["path"]
        host = json.loads(host_path.read_text(encoding="utf-8"))
        if mutation == "vdc_gate":
            del host["vdc_domain"]
        else:
            vdc_log = tmp_path / host["vdc_domain"]["log"]["path"]
            vdc_log.write_text("compiled only\n", encoding="utf-8")
            host["vdc_domain"]["log"] = evidence(vdc_log, tmp_path)
        write_json(host_path, host)
        receipt["host_report"] = evidence(host_path, tmp_path)
    with pytest.raises(AcceptanceError):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


@pytest.mark.parametrize("mutation", [
    "identity", "quiet", "runs", "raw", "formal", "epoch", "flags",
    "timestamps", "quiet_window", "load", "error", "boundary", "tool_hash",
])
def test_foundation_calibration_report_tampering_is_rejected(
        tmp_path, monkeypatch, mutation):
    changed = ["components/sync_trigger/src/trigger_reservation_clock.c",
               "components/calibration_manager/src/calibration_manager.c"]
    receipt, _ = receipt_fixture(
        tmp_path, monkeypatch, profile=gate.PROFILE_RESERVATION_FOUNDATION,
        changed=changed)
    path = tmp_path / receipt["calibration_report"]["path"]
    report = json.loads(path.read_text(encoding="utf-8"))
    if mutation == "identity": report["identity"]["serial_number"] = "other"
    elif mutation == "quiet": report["settings"]["quiet_capture"] = False
    elif mutation == "runs": report["runs_completed"] -= 1
    elif mutation == "raw": report["raw_measurement_passed"] = False
    elif mutation == "formal": report["firmware_calibration_passed"] = True
    elif mutation == "epoch": report["run_results"][1]["snapshot"]["epoch"] = 11
    elif mutation == "flags": report["run_results"][0]["snapshot"]["flags"] = 7
    elif mutation == "timestamps": report["run_results"][0]["snapshot"]["t2_clk_rx"] = 90
    elif mutation == "quiet_window": report["records"][0]["host_monotonic_end_s"] = 12.0
    elif mutation == "load": report["load_guard"]["after"]["enabled_mask"] += 1
    elif mutation == "error": report["final_error"] = '-200,"Execution error"'
    elif mutation == "boundary": report["evidence_boundary"] = "formal calibration accepted"
    elif mutation == "tool_hash": report["tool_sha256"] = "old"
    write_json(path, report)
    receipt["calibration_report"] = evidence(path, tmp_path)
    with pytest.raises(AcceptanceError):
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


def test_foundation_profile_has_an_exact_production_scope():
    expected = {
        "CMakeLists.txt",
        "application/src/app.c",
        "components/calibration_manager/inc/calibration_manager.h",
        "components/distributed_refmem/inc/distributed_refmem.h",
        "middleware/scpi_port/inc/scpi_sync_commands.h",
        "middleware/scpi_port/src/scpi_calibration_commands.c",
        "middleware/scpi_port/src/scpi_sync_commands.c",
        "middleware/scpi_port/src/scpi_system_snapshot_commands.c",
        "middleware/scpi_port/inc/scpi_system_snapshot_commands.h",
        "components/sync_io/src/sync_io.c",
        "components/sync_io/src/sync_io_model_sched.c",
        "components/sync_io/src/sync_io_logic_analyzer.c",
        "components/sync_io/src/sync_io_analyzer_burst.c",
        "components/sync_trigger/inc/trigger_reservation_clock.h",
        "components/sync_trigger/inc/trigger_reservation_config.h",
        "components/sync_trigger/inc/trigger_reservation_planner.h",
        "components/sync_trigger/src/trigger_reservation_clock.c",
        "components/sync_trigger/src/trigger_reservation_config.c",
        "components/sync_trigger/src/trigger_reservation_planner.c",
        "components/vdc_domain/inc/vdc_domain.h",
        "components/vdc_domain/inc/vdc_timestamp_clock.h",
        "components/vdc_domain/src/vdc_domain.c",
        "components/vdc_domain/src/vdc_timestamp_clock.c",
        "components/vdc_dpll_manager/inc/vdc_dpll_manager.h",
        "components/vdc_dpll_manager/inc/vdc_timer1_coordinate.h",
        "components/vdc_dpll_manager/src/vdc_dpll_manager.c",
        "components/calibration_manager/src/calibration_manager.c",
        "components/distributed_refmem/src/distributed_refmem.c",
    }
    assert gate.FOUNDATION_PRODUCTION_ALLOWLIST == expected
    assert "tests/python/test_sync_io_workspace.py" in gate.FOUNDATION_SUPPORT_ALLOWLIST
    assert "tests/python/test_calibration_loopback_validate.py" in gate.FOUNDATION_SUPPORT_ALLOWLIST
    assert "tests/python/test_vdc_dpll_owner_boundary.py" in gate.FOUNDATION_SUPPORT_ALLOWLIST
    assert "tools/calibration_loopback_validate/calibration_loopback_validate.py" in gate.FOUNDATION_SUPPORT_ALLOWLIST
    owner_support = {
        "tools/scpi_common/vdc_owner_state.py",
        "tests/python/test_vdc_owner_state.py",
        "tests/python/test_vdc_owner_execution.py",
        "tests/python/test_dpll_data_direction.py",
        "tests/python/test_trn03_candidate.py",
        "tools/calibration_ring_validate/trn03_closed_loop.py",
        "tests/python/test_trn03_closed_loop.py",
        "tools/calibration_ring_validate/trn03_candidate_import.py",
        "tests/python/test_trn03_candidate_import.py",
        "tools/scpi_common/scpi_serial.py",
        "tests/python/test_scpi_serial.py",
        "tools/tdma_ring_monitor/tdma_single_board_loopback.py",
        "tests/python/test_tdma_single_board_loopback.py",
        "tests/python/test_sequence_realtime_dispatch.py",
        "tools/sequence_trigger_debug_ui/sequence_trigger_debug_ui.py",
        "tests/python/test_sequence_trigger_debug_ui.py",
    }
    assert owner_support <= gate.FOUNDATION_SUPPORT_ALLOWLIST
    assert {path for path in owner_support if path.startswith("tests/python/")} <= set(
        gate.FOUNDATION_HOST_TESTS)
    protocol_support = {
        "tools/hardware_acceptance/sequence_tdma_cycle_validate.py",
        "tests/python/test_sequence_tdma_cycle_validate.py",
        "tools/tdma_ring_monitor/tdma_start_ring.py",
        "tests/python/test_tdma_start_ring.py",
        "tests/python/test_tdma_arm_diagnostics.py",
        "tests/python/test_state_machine_resource_check.py",
        "tests/unit/test_tdma_origin_admission.c",
    }
    assert protocol_support <= gate.FOUNDATION_SUPPORT_ALLOWLIST
    # The new foundation stages are bound to these validators through the receipt
    # fingerprint set.  Only sequence_tdma_cycle_validate.py is also a staged
    # source of this slice, so only that one is asserted against the allowlist;
    # the remaining three are recorded dependencies of the run, not staged files.
    for key in ("repeat", "cycle", "position", "feedback"):
        assert key in gate.VALIDATOR_PATHS
    assert gate.VALIDATOR_PATHS["cycle"] in gate.FOUNDATION_SUPPORT_ALLOWLIST
    changed = sorted(expected | gate.FOUNDATION_SUPPORT_ALLOWLIST)
    assert gate.validate_scope(changed) == changed
    assert gate.acceptance_profile(changed) == gate.PROFILE_RESERVATION_FOUNDATION


def test_foundation_profile_requires_reservation_anchor_and_rejects_expansion():
    with pytest.raises(AcceptanceError, match="outside"):
        gate.validate_scope(["components/vdc_domain/src/vdc_domain.c"])
    changed = ["components/sync_trigger/src/trigger_reservation_clock.c",
               "components/vdc_domain/src/vdc_domain.c"]
    assert gate.acceptance_profile(changed) == gate.PROFILE_RESERVATION_FOUNDATION
    with pytest.raises(AcceptanceError, match="outside"):
        gate.validate_scope(changed + [
            "components/vdc_dpll_manager/src/vdc_time_mapping.c"])


def test_legacy_profile_is_unchanged_for_existing_sequence_scope():
    changed = ["components/sync_trigger/src/trigger_sequence_link.c",
               "components/vdc_domain/src/vdc_timestamp_clock.c"]
    assert gate.acceptance_profile(changed) == gate.PROFILE_THREE_MODE


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


@pytest.mark.parametrize("port,resource", [("COM8", None), (None, "USB::FAKE")])
def test_foundation_calibration_uses_fixed_quiet_profile_and_same_transport(
        port, resource):
    args = gate.argparse.Namespace(serial_number="board-1", build="build-1",
                                   port=port, visa_resource=resource)
    cli = gate._calibration_args(args, Path("out/calibration"))
    assert cli[cli.index("--serial-number") + 1] == "board-1"
    assert cli[cli.index("--expected-build") + 1] == "build-1"
    assert cli[cli.index("--runs") + 1] == str(gate.CALIBRATION_RUNS)
    assert cli[cli.index("--words") + 1] == str(gate.CALIBRATION_WORDS)
    assert "--quiet-capture" in cli
    if resource:
        assert cli[cli.index("--visa-resource") + 1] == resource
    else:
        assert cli[-1] == port


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


FOUNDATION_STAGE_CHANGED = [
    "components/sync_trigger/src/trigger_reservation_clock.c",
    "components/vdc_domain/src/vdc_domain.c",
]

# Stage report key per injected/simulated-path mutation.
STAGE_TAMPER_TARGETS = {
    "independent_manual_source": "independent_source_report",
    "independent_software_next": "independent_source_report",
    "independent_manual_report": "independent_source_report",
    "dual_scpi_next": "dual_slot_physical_report",
    "dual_ready_injection": "dual_slot_physical_report",
    "dual_scpi_credit_index": "dual_slot_physical_report",
    "dual_wrong_ready_pin": "dual_slot_physical_report",
    "turntable_scpi_injection": "turntable_external_report",
    "turntable_injection_command": "turntable_external_report",
    "turntable_simulated_input": "turntable_external_report",
    "turntable_host_command_during_wait": "turntable_external_report",
    "turntable_short_wait": "turntable_external_report",
    "standalone_sequence_resource": "standalone_sp8t_report",
    "standalone_missing_return": "standalone_sp8t_report",
    "standalone_phase_failure": "standalone_sp8t_report",
}


def foundation_receipt(tmp_path, monkeypatch):
    return receipt_fixture(tmp_path, monkeypatch,
                           profile=gate.PROFILE_RESERVATION_FOUNDATION,
                           changed=FOUNDATION_STAGE_CHANGED)


@pytest.mark.parametrize("key", sorted(gate.FOUNDATION_STAGE_REPORT_KEYS))
def test_foundation_receipt_requires_every_new_stage_report(tmp_path, monkeypatch, key):
    receipt, _ = foundation_receipt(tmp_path, monkeypatch)
    del receipt[key]
    with pytest.raises(AcceptanceError, match="missing"):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_three_mode_receipt_rejects_foundation_only_stage_evidence(tmp_path, monkeypatch):
    receipt, _ = receipt_fixture(tmp_path, monkeypatch)
    hashes = {name: gate.p3.sha256_file(tmp_path / relative)
              for name, relative in gate.VALIDATOR_PATHS.items()}
    for key, report in (
            ("independent_source_report", independent_source_report(hashes["repeat"])),
            ("dual_slot_physical_report", dual_slot_physical_report(hashes["cycle"])),
            ("turntable_external_report", turntable_external_report(hashes["position"])),
            ("standalone_sp8t_report", standalone_sp8t_report(hashes["feedback"]))):
        path = tmp_path / "out" / (key + ".json")
        write_json(path, report)
        receipt[key] = evidence(path, tmp_path)
    with pytest.raises(AcceptanceError, match="foundation-only stage evidence"):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


@pytest.mark.parametrize("mutation", sorted(STAGE_TAMPER_TARGETS))
def test_foundation_injected_or_simulated_stage_report_is_rejected(
        tmp_path, monkeypatch, mutation):
    receipt, _ = foundation_receipt(tmp_path, monkeypatch)
    key = STAGE_TAMPER_TARGETS[mutation]
    path = tmp_path / receipt[key]["path"]
    report = json.loads(path.read_text(encoding="utf-8"))
    sha = report["tool_sha256"]
    if mutation == "independent_manual_source":
        # The software-NEXT profile relabelled as the physical IN1 stage.
        report["settings"]["source"] = "MANUAL"
        report["software_input_simulation_verified"] = True
        report["software_next_sent"] = 8 * gate.FINITE_REPEAT - 1
    elif mutation == "independent_software_next":
        report["software_next_sent"] = 8 * gate.FINITE_REPEAT - 1
    elif mutation == "independent_manual_report":
        report = repeat_report(sha, receipt["source_hz"])
    elif mutation == "dual_scpi_next":
        report["settings"]["scpi_next"] = True
    elif mutation == "dual_ready_injection":
        report["ready_injection"] = cycle_report(sha)["ready_injection"]
    elif mutation == "dual_scpi_credit_index":
        report["link_configuration"]["input"] = 0
    elif mutation == "dual_wrong_ready_pin":
        report["settings"]["ready_input"] = "IN1"
    elif mutation == "turntable_scpi_injection":
        profile = report["two_positions"]
        profile["input_simulation"] = {
            "method": "SCPI", "counter_command": "TRIG:SEQ:INJECT IN1,<count>",
            "ready_command": None, "physical_edge_count_verified": False,
            "physical_ready_verified": True}
        profile["injections"] = [dict(command="TRIG:SEQ:INJECT IN1,1000", count=1000,
                                      issued_at=1.0, response="1")]
    elif mutation == "turntable_injection_command":
        report["transcript"].append({"command": "TRIG:SEQ:INJECT IN1,1000", "response": "1"})
    elif mutation == "turntable_simulated_input":
        report["settings"]["external_input"] = False
        report["software_input_simulation_verified"] = True
    elif mutation == "turntable_host_command_during_wait":
        report["two_positions"]["quiet_capture"]["commands_during_wait"] = 1
    elif mutation == "turntable_short_wait":
        silence = report["two_positions"]["quiet_capture"]
        silence["elapsed_s"] = silence["planned_wait_s"] - 1
    elif mutation == "standalone_sequence_resource":
        report["phases"]["standalone-sp8t"]["positions"][2]["io"]["owned"] = 7
    elif mutation == "standalone_missing_return":
        report["phases"]["standalone-sp8t"]["positions"] = \
            report["phases"]["standalone-sp8t"]["positions"][:-1]
    elif mutation == "standalone_phase_failure":
        report["phases"]["standalone-sp8t"]["passed"] = False
        report["phases"]["standalone-sp8t"]["failure"] = "SP8T readback mismatch"
    write_json(path, report)
    receipt[key] = evidence(path, tmp_path)
    with pytest.raises(AcceptanceError):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


@pytest.mark.parametrize("mutation", [
    "independent_claim", "dual_claim", "turntable_claim", "standalone_claim",
])
def test_foundation_stage_reports_reject_unproven_claims(tmp_path, monkeypatch, mutation):
    receipt, _ = foundation_receipt(tmp_path, monkeypatch)
    key = {"independent_claim": "independent_source_report",
           "dual_claim": "dual_slot_physical_report",
           "turntable_claim": "turntable_external_report",
           "standalone_claim": "standalone_sp8t_report"}[mutation]
    path = tmp_path / receipt[key]["path"]
    report = json.loads(path.read_text(encoding="utf-8"))
    if mutation == "independent_claim":
        report["independent_input_count_verified"] = True
    elif mutation == "dual_claim":
        report["tdma_stability_verified"] = True
    elif mutation == "turntable_claim":
        report["independent_input_count_verified"] = True
    else:
        report["vna_role_runtime_verified"] = True
    write_json(path, report)
    receipt[key] = evidence(path, tmp_path)
    with pytest.raises(AcceptanceError, match="overclaims"):
        gate.check_staged(tmp_path, gate.DEFAULT_RECEIPT)


def test_audit_reports_validates_the_new_foundation_stages(tmp_path, monkeypatch):
    receipt, _ = foundation_receipt(tmp_path, monkeypatch)
    argv = ["audit-reports", "--root", str(tmp_path), "--serial-number", "board-1",
            "--build", "build-1", "--source-hz", str(receipt["source_hz"])]
    paths = {
        "finite-report": receipt["finite_report"],
        "pause-resume-report": receipt["pause_resume_report"],
        "repeat-report": receipt["repeat_report"],
        "start-report": receipt["start_report"],
        "position-report": receipt["position_report"],
        "ota-summary": receipt["ota_summary"],
        "package": receipt["firmware_package"],
        "independent-source-report": receipt["independent_source_report"],
        "dual-slot-physical-report": receipt["dual_slot_physical_report"],
        "turntable-external-report": receipt["turntable_external_report"],
        "standalone-sp8t-report": receipt["standalone_sp8t_report"],
    }
    for flag, item in paths.items():
        argv += ["--" + flag, item["path"]]
    gate.audit_reports(gate.parse_args(argv))
    # The physical-source discrimination must hold on the audit path too.
    path = tmp_path / receipt["independent_source_report"]["path"]
    report = json.loads(path.read_text(encoding="utf-8"))
    report["settings"]["source"] = "MANUAL"
    report["software_next_sent"] = 8 * gate.FINITE_REPEAT - 1
    write_json(path, report)
    with pytest.raises(AcceptanceError):
        gate.audit_reports(gate.parse_args(argv))


@pytest.mark.parametrize("port,resource", [("COM8", None), (None, "USB::FAKE")])
def test_foundation_stage_invocations_pin_the_physical_bench(port, resource):
    args = gate.argparse.Namespace(serial_number="board-1", build="build-1", port=port,
                                   visa_resource=resource, source_hz=1000.0)
    independent = gate._independent_source_args(args, Path("out/independent-source.json"))
    assert independent[independent.index("--source") + 1] == "IN1"
    assert independent[independent.index("--repeat") + 1] == str(gate.FINITE_REPEAT)
    assert independent[independent.index("--minimum-events") + 1] == str(gate.MINIMUM_EVENTS)
    assert independent[independent.index("--duration") + 1] == "30"
    assert independent[independent.index("--source-hz") + 1] == "1000.0"
    dual = gate._dual_slot_physical_args(args, Path("out/dual-slot-physical.json"))
    assert dual[dual.index("--ready-input") + 1] == "IN2"
    assert dual[dual.index("--repeat") + 1] == str(gate.FINITE_REPEAT)
    assert dual[dual.index("--minimum-events") + 1] == str(gate.MINIMUM_EVENTS)
    assert "--gui-control" in dual and "--scpi-next" not in dual
    turntable = gate._turntable_external_args(args, Path("out/turntable-external.json"))
    assert turntable[turntable.index("--positions") + 1] == str(gate.POSITION_EXTERNAL_POSITIONS)
    assert turntable[turntable.index("--counter-input") + 1] == "IN1"
    assert turntable[turntable.index("--threshold") + 1] == str(gate.POSITION_THRESHOLD)
    assert turntable[turntable.index("--gateway-timeout-ms") + 1] == "10000"
    assert turntable[turntable.index("--position-cycle-target-ms") + 1] == \
        str(gate.POSITION_CYCLE_TARGET_MS)
    assert turntable[turntable.index("--duration") + 1] == "40"
    for flag in ("--external-input", "--quiet-capture", "--timing-evidence",
                 "--schedule-evidence"):
        assert flag in turntable
    standalone = gate._standalone_sp8t_args(args, Path("out/standalone-sp8t.json"))
    assert standalone[standalone.index("--mode") + 1] == "standalone-sp8t"
    assert standalone[standalone.index("--serial-number") + 1] == "board-1"
    assert standalone[standalone.index("--build") + 1] == "build-1"
    for cli in (independent, dual, turntable, standalone):
        if resource:
            assert cli[cli.index("--visa-resource") + 1] == resource
        else:
            assert cli[cli.index("--port") + 1] == port


def test_external_turntable_window_tracks_declared_frequency():
    assert gate.external_position_wait_s(1000) == 31.2
    assert gate.external_position_duration(1000) == gate.POSITION_EXTERNAL_DURATION_S
    assert gate.external_position_duration(50) == 601.2
    for frequency in (0, -1, float("nan"), float("inf"), None, True):
        with pytest.raises(AcceptanceError):
            gate.external_position_duration(frequency)
