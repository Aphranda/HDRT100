"""T3 requires raw single-board dual-slot evidence and continuous feedback."""
from argparse import Namespace
from copy import deepcopy
import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.calibration_ring_validate import trn03_single_board as wrapper
from test_calibration_marker_loopback import full_capture, pack_samples
from test_calibration_sck_loopback import capture_fixture


UID = "839E1AE79EA20F31"
BUILD = "20260921010101"
IDN = f"NO.1,DHRT100,{UID},0.1.0"


def args_for(tmp_path):
    return Namespace(board_id=UID, expected_build=BUILD, port=None,
                     marker_summary=tmp_path / "marker.json",
                     sck_summary=tmp_path / "sck.json", data_summary=tmp_path / "data.json",
                     out_dir=tmp_path / "result", duration_s=3., poll_interval_s=.25,
                     operating_level=7, probe_phase_cycles=2)


def report_fixture(kind):
    status, samples, _ = full_capture() if kind == "marker" else capture_fixture()
    raw_kind = "MARKER" if kind == "marker" else "SCK"
    module = wrapper.marker if kind == "marker" else wrapper.sck
    fields = wrapper.marker.V2_FIELDS if kind == "marker" else wrapper.sck.FIELDS
    idle = dict.fromkeys(fields, 0)
    idle["version"] = status["version"]
    idle = module.parse_status(",".join(str(idle[field]) for field in fields))
    trial = dict(epoch=7, passed=True, status=status, offset_samples=0,
                 cleanup=idle, capture=dict(
                     schema=f"HAOFV_{raw_kind}_LOOP_RAW_V1", epoch=7,
                     capture_generation=42, gpio_base=24, sample_bits=8,
                     sample_period_ns=4, words=pack_samples(samples)))
    report = dict(schema=wrapper.SCHEMAS[kind], passed=True,
                physical_board_count=1, logical_slot_count=2,
                production_training_certified=False, parameter_flash_saved=False,
                board=dict(address=UID, build=BUILD, idn=IDN, port="COM3"),
                final_idn=IDN, final_build=BUILD, final_error='0,"No error"',
                transcript=[], trials=[trial])
    report["transcript"] = transcript_fixture(kind, report)
    return report


def transcript_fixture(kind, report):
    prefix = "CALibration:MARKer:LOOP" if kind == "marker" else "CAL:SCK:LOOP"
    fields = wrapper.marker.V2_FIELDS if kind == "marker" else wrapper.sck.FIELDS
    records = []

    def add(command, response):
        records.append(dict(command=command, response=response))

    def status_raw(status):
        return ",".join(str(status[field]) for field in fields)

    add("SYSTem:TDMA:RING:STOP", "OK")
    add("SYSTem:REFMEM:SYNC:TDMA:STATus?", ",".join("0" for _ in wrapper.tdma.TDMA_FIELDS))
    for trial in report["trials"]:
        status = trial["status"]
        add(f"{prefix}:STOP", "OK")
        add(f"READ:{prefix}?", status_raw(trial["cleanup"]))
        request = [status["codebook"], trial["epoch"], status["requested_words"]]
        if kind == "sck":
            request.append(trial["offset_samples"])
        request.append(0)
        request_raw = ",".join(map(str, request))
        add(f"{prefix}:STARt {request_raw}", request_raw)
        add(f"READ:{prefix}?", status_raw(status))
        words = trial["capture"]["words"]
        for offset in range(0, len(words), 32):
            count = min(32, len(words) - offset)
            args = [trial["epoch"], offset, count, status["capture_generation"]]
            reply = [trial["epoch"], len(words), offset, count] + words[offset:offset + count]
            add(f"READ:{prefix}:DATA? " + ",".join(map(str, args)), ",".join(map(str, reply)))
        add(f"READ:{prefix}?", status_raw(status))
        add(f"{prefix}:STOP", "OK")
        add(f"READ:{prefix}?", status_raw(trial["cleanup"]))
    add("*IDN?", report["final_idn"])
    add("SYST:FW:BUILD?", '"' + report["final_build"] + '"')
    add("SYST:ERR?", report["final_error"])
    return records


def save_report(tmp_path, kind, report):
    path = tmp_path / f"{kind}.json"
    path.write_text(json.dumps(report), encoding="utf-8")
    return path


@pytest.mark.parametrize("kind", ["marker", "sck"])
def test_real_raw_marker_and_sck_revalidated(tmp_path, kind):
    path = save_report(tmp_path, kind, report_fixture(kind))
    evidence = wrapper.preflight_input(path, kind, UID, BUILD)
    assert evidence["passed"], evidence.get("error")
    assert len(evidence["sha256"]) == 64
    assert evidence["trials"][0]["revalidated"]["passed"]


@pytest.mark.parametrize("kind", ["marker", "sck"])
@pytest.mark.parametrize("fault", [
    "uid", "build", "physical_count", "logical_count", "flash", "production",
    "final_idn", "final_build", "final_error", "transcript", "fault_mode",
    "trial_failed", "cleanup", "duplicate", "generation", "words", "derived_time",
    "capture_schema", "raw_corruption", "replay", "synthetic", "forced_continue",
])
def test_claimed_pass_never_overrides_invalid_input(tmp_path, kind, fault):
    report = report_fixture(kind)
    trial = report["trials"][0]
    if fault in ("uid", "build"):
        report["board"]["address" if fault == "uid" else "build"] = "wrong"
    elif fault in ("physical_count", "logical_count"):
        report["physical_board_count" if fault == "physical_count" else "logical_slot_count"] = 3
    elif fault in ("flash", "production"):
        report["parameter_flash_saved" if fault == "flash" else "production_training_certified"] = True
    elif fault in ("final_idn", "final_build", "final_error"):
        report[fault] = "wrong"
    elif fault == "transcript":
        report["transcript"] = []
    elif fault == "fault_mode":
        report["fault_idle_low"] = True
    elif fault == "trial_failed":
        trial["passed"] = False
    elif fault == "cleanup":
        trial["cleanup"]["state"] = 2
    elif fault == "duplicate":
        report["trials"].append(deepcopy(trial))
    elif fault == "generation":
        trial["capture"]["capture_generation"] += 1
    elif fault == "words":
        trial["capture"]["words"] = []
    elif fault == "derived_time":
        trial["status"]["t1_ns"] += 4
    elif fault == "capture_schema":
        trial["capture"]["schema"] = "other"
    elif fault == "raw_corruption":
        trial["capture"]["words"] = [0] * 2048
    else:
        trial["provenance"] = {fault: True}
    result = wrapper.preflight_input(save_report(tmp_path, kind, report), kind, UID, BUILD)
    assert not result["passed"]
    assert result["error"]


@pytest.mark.parametrize("report", [None, [], {"trials": None}])
def test_malformed_report_fails_closed(tmp_path, report):
    result = wrapper.preflight_input(save_report(tmp_path, "marker", report), "marker", UID, BUILD)
    assert not result["passed"]


@pytest.mark.parametrize("kind", ["marker", "sck"])
@pytest.mark.parametrize("fault", ["idn", "build", "error", "start_echo", "start_request",
    "terminal_status", "post_status", "chunk_generation", "chunk_epoch", "chunk_offset",
    "chunk_count", "chunk_response_epoch", "chunk_response_count", "chunk_words",
    "chunk_reused", "chunk_reordered", "missing_chunk", "stop_ack", "cleanup_status",
    "trailing", "only_idn", "exception"])
def test_transcript_must_bind_complete_acquisition(tmp_path, kind, fault):
    report = report_fixture(kind)
    records = report["transcript"]
    data_indexes = [index for index, item in enumerate(records) if ":DATA? " in item["command"]]
    first = data_indexes[0]
    if fault in ("idn", "build", "error"):
        index = {"idn": -3, "build": -2, "error": -1}[fault]
        records[index]["response"] = "OTHER_UID" if fault == "idn" else "wrong"
    elif fault in ("start_echo", "start_request"):
        key = "response" if fault == "start_echo" else "command"
        records[first - 2][key] += ",1"
    elif fault in ("terminal_status", "post_status", "cleanup_status"):
        index = first - 1 if fault == "terminal_status" else data_indexes[-1] + 1
        if fault == "cleanup_status":
            index += 2
        fields = wrapper.marker.V2_FIELDS if kind == "marker" else wrapper.sck.FIELDS
        values = records[index]["response"].split(",")
        values[fields.index("capture_generation")] = "999"
        records[index]["response"] = ",".join(values)
    elif fault.startswith("chunk_response") or fault == "chunk_words":
        index = {"chunk_response_epoch": 0, "chunk_response_count": 3, "chunk_words": 4}[fault]
        values = records[first]["response"].split(",")
        values[index] = str(int(values[index]) ^ 1)
        records[first]["response"] = ",".join(values)
    elif fault in ("chunk_generation", "chunk_epoch", "chunk_offset", "chunk_count"):
        index = {"chunk_generation": 3, "chunk_epoch": 0, "chunk_offset": 1, "chunk_count": 2}[fault]
        header, argument = records[first]["command"].split()
        values = argument.split(",")
        values[index] = str(int(values[index]) + 1)
        records[first]["command"] = header + " " + ",".join(values)
    elif fault == "chunk_reused":
        records[first + 1] = deepcopy(records[first])
    elif fault == "chunk_reordered":
        records[first], records[first + 1] = records[first + 1], records[first]
    elif fault == "missing_chunk":
        records.pop(first)
    elif fault == "stop_ack":
        records[data_indexes[-1] + 2]["response"] = "ERROR"
    elif fault == "trailing":
        records.append(deepcopy(records[-1]))
    elif fault == "only_idn":
        report["transcript"] = [{"command": "*IDN?", "response": "OTHER_UID"}]
    elif fault == "exception":
        records[first]["exception"] = "timeout"
    result = wrapper.preflight_input(save_report(tmp_path, kind, report), kind, UID, BUILD)
    assert not result["passed"], fault
    assert result["error"]


@pytest.mark.parametrize("kind", ["marker", "sck"])
@pytest.mark.parametrize("fault", [None, "reused_trial", "reversed_trial", "cross_generation"])
def test_transcript_trials_are_bound_in_order(tmp_path, kind, fault):
    report = report_fixture(kind)
    second = deepcopy(report["trials"][0])
    second["status"]["capture_generation"] += 1
    second["capture"]["capture_generation"] += 1
    report["trials"].append(second)
    report["transcript"] = transcript_fixture(kind, report)
    records = report["transcript"]
    starts = [index for index, item in enumerate(records) if ":STARt " in item["command"]]
    width = starts[1] - starts[0]
    if fault == "reused_trial":
        records[starts[1] - 2:starts[1] - 2 + width] = deepcopy(records[starts[0] - 2:starts[1] - 2])
    elif fault == "reversed_trial":
        report["trials"].reverse()
    elif fault == "cross_generation":
        records[starts[1] + 2] = deepcopy(records[starts[0] + 2])
    result = wrapper.preflight_input(save_report(tmp_path, kind, report), kind, UID, BUILD)
    assert result["passed"] is (fault is None), result.get("error")
    if fault is None:
        bound = result["transcript_validation"]["trials"]
        assert bound[0]["transcript_end"] == bound[1]["transcript_start"]


def test_missing_data_evaluator_never_launches_child(tmp_path, monkeypatch):
    args = args_for(tmp_path)
    for kind in wrapper.SCHEMAS:
        report = report_fixture("sck" if kind == "data" else kind)
        report["schema"] = wrapper.SCHEMAS[kind]
        save_report(tmp_path, kind, report)

    def forbidden(*args, **kwargs):
        pytest.fail("board child must not start before DATA raw verification")

    monkeypatch.setattr(wrapper.subprocess, "run", forbidden)
    output = wrapper.run(args)
    assert not output["passed"]
    assert not output["child_started"]
    assert "DATA raw evaluator unavailable" in output["input_validation"]["data"]["error"]
    assert (args.out_dir / "summary.json").exists()


def sample_fixture(sequence):
    tdma = dict.fromkeys(wrapper.tdma.TDMA_FIELDS, 0)
    tdma.update(ring_enabled=1, ring_node_count=2, ring_local_slot_id=0,
                ring_reference_slot_id=0, ring_up_running=1, ring_down_running=1,
                ring_adapter_started=1, simultaneous_feedback_loop_evidence=1,
                ring_reference_tx_timestamp_ns_lo=1000 * sequence,
                ring_feedback_rx_timestamp_ns_lo=1000 * sequence + 52,
                ring_feedback_round_trip_ns=52, ring_feedback_timeout_ns=2000000,
                ring_timestamp_resolution_ns=4, ring_timestamp_flags=2)
    for field in (wrapper.tdma.RING_SEQ, wrapper.tdma.RING_UP_TX_SEQUENCE,
                  wrapper.tdma.RING_DOWN_RX_SEQUENCE, wrapper.tdma.RING_ADAPTER_TX_COUNT,
                  wrapper.tdma.RING_ADAPTER_RX_COUNT):
        tdma[wrapper.tdma.TDMA_FIELDS[field]] = sequence
    phys = dict.fromkeys(wrapper.tdma.PHYS_FIELDS, 0)
    phys.update(baud_hz=10000000, tx_csn_pin=26, tx_sck_pin=25, tx_pin=29,
                rx_csn_pin=27, rx_sck_pin=28, rx_pin=24)
    return refresh_raw(dict(tdma=list(tdma.values()), phys=phys))


def refresh_raw(sample):
    sample["raw_tdma"] = ",".join(map(str, sample["tdma"]))
    sample["raw_phys"] = ",".join(str(sample["phys"][field]) for field in wrapper.tdma.PHYS_FIELDS)
    return sample


def child_fixture():
    stopped = sample_fixture(40)
    stopped["tdma"][wrapper.tdma.RING_UP_RUNNING] = 0
    stopped["tdma"][wrapper.tdma.RING_DOWN_RUNNING] = 0
    return dict(passed=True, board_address=UID, build=BUILD, failures=[],
                electrical_data_loopback_passed=True, formal_feedback_evidence=True,
                ring_setup=dict(enabled=True, node_count=2, local_slot=0, reference_slot=0,
                                operating_level=7, probe_phase_cycles=2,
                                cleanup_sample=refresh_raw(stopped)),
                samples=[sample_fixture(value) for value in (10, 20, 30)])


def test_child_true_only_with_every_raw_gate(tmp_path):
    result = wrapper.evaluate_child(child_fixture(), 0, args_for(tmp_path))
    assert result["passed"], result


@pytest.mark.parametrize("index", [0, 1, 2])
@pytest.mark.parametrize("field,value", [
    ("ring_reference_tx_timestamp_ns_lo", 0),
    ("ring_reference_tx_timestamp_ns_lo", 999999),
    ("ring_feedback_rx_timestamp_ns_lo", 1),
    ("ring_feedback_round_trip_ns", 53),
    ("ring_feedback_timeout_ns", 0),
    ("ring_feedback_timeout_ns", 51),
    ("ring_timestamp_resolution_ns", 0),
    ("ring_timestamp_resolution_ns", 101),
    ("ring_timestamp_flags", 0),
    ("ring_timestamp_flags", 3),
    ("ring_reference_tx_timestamp_ns_hi", -1),
    ("ring_feedback_rx_timestamp_ns_hi", 0x100000000),
    ("ring_timestamp_flags", 0x100000002),
    ("ring_feedback_timeout_ns", 0x100000000),
])
def test_claimed_formal_rejects_incoherent_raw_pair(tmp_path, index, field, value):
    child = child_fixture()
    sample = child["samples"][index]
    sample["tdma"][wrapper.tdma.TDMA_FIELDS.index(field)] = value
    refresh_raw(sample)
    result = wrapper.evaluate_child(child, 0, args_for(tmp_path))
    assert result["gates"]["raw_formal_feedback"]
    assert not result["gates"]["raw_feedback_pair"]
    assert not result["passed"]


def test_pair_crosses_low_word_rollover_and_may_lag_latest_tx(tmp_path):
    child = child_fixture()
    for sample in child["samples"]:
        changes = dict(ring_reference_tx_timestamp_ns_lo=0xFFFFFFF0,
                       ring_reference_tx_timestamp_ns_hi=7,
                       ring_feedback_rx_timestamp_ns_lo=36,
                       ring_feedback_rx_timestamp_ns_hi=8)
        for name, value in changes.items():
            sample["tdma"][wrapper.tdma.TDMA_FIELDS.index(name)] = value
        sample["tdma"][wrapper.tdma.RING_UP_TX_SEQUENCE] += 1
        refresh_raw(sample)
    result = wrapper.evaluate_child(child, 0, args_for(tmp_path))
    assert result["passed"], result


@pytest.mark.parametrize("index", [0, 1, 2])
def test_startup_interior_or_final_feedback_missing_is_not_dropped(tmp_path, index):
    child = child_fixture()
    sample = child["samples"][index]
    sample["tdma"][wrapper.tdma.SIMULTANEOUS] = 0
    sample["tdma"][wrapper.tdma.RING_LAST_ERROR] = 5
    refresh_raw(sample)
    result = wrapper.evaluate_child(child, 0, args_for(tmp_path))
    assert not result["passed"]
    assert result["gates"]["raw_electrical"]
    assert not result["gates"]["raw_formal_feedback"]


@pytest.mark.parametrize("fault", ["counter_reset", "bad_reset", "phy_bad_reset", "raw_mismatch",
                                   "raw_short", "phy_short", "phy_negative", "baud", "topology",
                                   "pin", "stopped_missing", "stopped_running", "empty", "null"])
def test_raw_gates_reject_recovered_or_malformed_samples(tmp_path, fault):
    child = child_fixture()
    sample = child["samples"][1]
    if fault == "counter_reset":
        sample["tdma"][wrapper.tdma.RING_SEQ] = 1
    elif fault == "bad_reset":
        sample["tdma"][wrapper.tdma.RING_ADAPTER_RX_BAD_COUNT] = 1
    elif fault == "phy_bad_reset":
        sample["phys"]["rx_bad_count"] = 1
    elif fault == "phy_negative":
        sample["phys"]["tx_count"] = -1
    elif fault in ("baud", "pin"):
        sample["phys"]["baud_hz" if fault == "baud" else "tx_pin"] += 1
    elif fault == "topology":
        sample["tdma"][wrapper.tdma.TDMA_FIELDS.index("ring_node_count")] = 1
    elif fault == "stopped_missing":
        del child["ring_setup"]["cleanup_sample"]
    elif fault == "stopped_running":
        child["ring_setup"]["cleanup_sample"] = sample_fixture(40)
    elif fault in ("empty", "null"):
        child["samples"] = [] if fault == "empty" else [None, None]
    refresh_raw(sample)
    if fault == "raw_mismatch":
        sample["tdma"][wrapper.tdma.SIMULTANEOUS] = 0
    elif fault == "raw_short":
        sample["raw_tdma"] = "1,2"
    elif fault == "phy_short":
        sample["raw_phys"] = "1,2"
    result = wrapper.evaluate_child(child, 0, args_for(tmp_path))
    assert not result["passed"], result


@pytest.mark.parametrize("formal", [True, False])
def test_child_handoff_retains_log_hash_and_diagnostic_scope(tmp_path, monkeypatch, formal):
    args = args_for(tmp_path)
    monkeypatch.setattr(wrapper, "preflight_input", lambda *args: {
        "passed": True, "board": {"port": "COM3"}})
    child = child_fixture()
    child["formal_feedback_evidence"] = formal

    def child_run(command, **kwargs):
        assert command[2] == "COM3"
        for flag, value in (("--expected-serial-number", UID), ("--expected-build", BUILD),
                            ("--node-count", "2"), ("--local-slot", "0"),
                            ("--reference-slot", "0")):
            assert command[command.index(flag) + 1] == value
        assert not any("matrix" in part.lower() or "save" in part.lower() for part in command)
        child_dir = Path(command[command.index("--out-dir") + 1])
        child_dir.mkdir()
        (child_dir / "summary.json").write_text(json.dumps(child), encoding="utf-8")
        kwargs["stdout"].write("child evidence\n")
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(wrapper.subprocess, "run", child_run)
    output = wrapper.run(args)
    assert output["passed"] is formal
    assert output["child_started"]
    assert len(output["child_summary_sha256"]) == 64
    assert (args.out_dir / "tdma-loopback.log").read_text(encoding="utf-8") == "child evidence\n"
    assert output["diagnostic_only"]
    for flag in ("active_candidate_allowed", "production_training_certified",
                 "calibration_phase_matrix_applied", "parameter_flash_saved"):
        assert output[flag] is False
