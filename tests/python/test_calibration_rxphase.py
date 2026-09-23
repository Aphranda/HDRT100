"""Real RX phase is instruction-bound, generation-bound, and raw-codeword-bound."""
from argparse import Namespace
from copy import deepcopy
from dataclasses import dataclass

import pytest

from tools.calibration_ring_validate import calibration_rxphase as rx


def fixture(codebook=1, offset=0, direction=0, epoch=7, lag=None, fault=False):
    waveform, _ = rx.marker.marker_raw_waveform(codebook_id=codebook, epoch=epoch, source_node=0)
    lag = 120 - offset if lag is None else lag
    samples = [0] * lag + waveform
    samples += [0] * (rx.CAPTURE_WORDS * 32 - len(samples))
    words = [sum(bit << index for index, bit in enumerate(samples[start:start + 32]))
             for start in range(0, len(samples), 32)]
    status = dict.fromkeys(rx.FIELDS, 0)
    status.update(version=2, state=3 if fault else 2, scope=4, flags=11 if fault else 15,
        reject_reason=4 if fault else 0, epoch=epoch, generation=42 + epoch, codebook=codebook,
        direction=direction, sample_period_ns=4, base_ns=26, base_samples=7, base_quantized_ns=28,
        offset_samples=offset, effective_samples=7 + offset, source_pipeline_cycles=1,
        destination_cycles=8 + offset, programmed_delay_instruction=rx.NOP_INSTRUCTION | (7 + offset) << 8,
        programmed_delay_cycles=8 + offset, tx_pin=rx.PINS[direction][0], rx_pin=rx.PINS[direction][1],
        code_samples=len(waveform), tx_words=(320 + len(waveform) + 31) // 32,
        tx_remaining=0, tx_drained=1, requested_words=256, produced_words=0 if fault else 256,
        rx_remaining=256 if fault else 0, origin_lead_samples=64, origin_high_samples=64,
        launch_guard_samples=128, fault=int(fault))
    return status, [] if fault else words


def evaluate(status, words, **kwargs):
    return rx.evaluate(status, words, codebook=status["codebook"], epoch=status["epoch"],
        base_ns=26, offset_samples=status["offset_samples"], direction=status["direction"], **kwargs)


@pytest.mark.parametrize("codebook", range(4))
@pytest.mark.parametrize("direction", [0, 1])
@pytest.mark.parametrize("offset", [-1, 0, 1])
def test_real_receiver_phase_and_complete_321bit_codeword(codebook, direction, offset):
    status, words = fixture(codebook, offset, direction)
    result = evaluate(status, words)
    assert result["passed"], result
    assert result["phase_loaded"]
    assert not result["instruction_readback_supported"]
    assert not result["phase_effect_verified"]
    assert result["quantized_base_ns"] == 28
    assert result["requested_base_ns"] == 26
    assert result["best_lag_sample"] == 120 - offset
    assert result["residual_offset_sample_count"] == -1 - offset
    assert result["recommended_offset_sample_count"] == -1
    assert result["codeword"]["logical_bit_count"] == 321
    assert not result["trn_accepted"]
    assert not result["pipeline_calibrated"]


@pytest.mark.parametrize("field,value", [
    ("scope", 2), ("generation", 0), ("sample_period_ns", 8), ("base_ns", 28),
    ("base_samples", 6), ("base_quantized_ns", 26), ("effective_samples", 6),
    ("source_pipeline_cycles", 0), ("destination_cycles", 7),
    ("programmed_delay_instruction", 0xA642), ("programmed_delay_cycles", 7), ("tx_pin", 26),
    ("rx_pin", 27), ("code_samples", 0), ("tx_words", 1), ("tx_remaining", 1),
    ("tx_drained", 0), ("requested_words", 255), ("produced_words", 255),
    ("rx_remaining", 1), ("origin_lead_samples", 63), ("origin_high_samples", 63),
    ("launch_guard_samples", 129), ("fault", 1), ("state", 3), ("flags", 7), ("reject_reason", 4),
])
def test_raw_snapshot_or_loaded_instruction_mismatch_fails(field, value):
    status, words = fixture()
    status[field] = value
    assert not evaluate(status, words)["passed"]


@pytest.mark.parametrize("bit", [0, 12, 20, 40, 55, 90, 200, 320])
def test_full_codeword_corruption_cannot_hide_behind_phase_loaded(bit):
    status, words = fixture()
    _, vector = rx.marker.marker_raw_waveform(codebook_id=1, epoch=7, source_node=0)
    for sample in range(120 + bit * vector.half_chip_samples * 2,
                        120 + (bit + 1) * vector.half_chip_samples * 2):
        words[sample // 32] ^= 1 << (sample % 32)
    result = evaluate(status, words)
    assert result["phase_loaded"]
    assert not result["passed"]
    assert not result["trn_accepted"]


def test_raw_words_and_idle_low_timeout():
    status, words = fixture()
    assert not evaluate(status, words[:-1])["passed"]
    assert not evaluate(status, [True] + words[1:])["passed"]
    status, words = fixture(fault=True)
    assert evaluate(status, words, fault_idle_low=True)["passed"]
    status["produced_words"] = 1
    status["rx_remaining"] = 255
    assert not evaluate(status, [0], fault_idle_low=True)["passed"]


@pytest.mark.parametrize("field,value", [("flags", 3), ("flags", 15),
    ("tx_remaining", 1), ("tx_drained", 0), ("reject_reason", 1)])
def test_idle_low_requires_finished_tx_and_specific_rx_timeout(field, value):
    status, words = fixture(fault=True)
    status[field] = value
    assert not evaluate(status, words, fault_idle_low=True)["passed"]


@pytest.mark.parametrize("location", [0, 8191])
def test_raw_guard_or_tail_activity_fails(location):
    status, words = fixture()
    words[location // 32] |= 1 << (location % 32)
    assert not evaluate(status, words)["passed"]


def raw_status(status):
    return ",".join(str(status[field]) for field in rx.FIELDS)


@pytest.mark.parametrize("failure", [None, "start_echo", "epoch", "generation", "chunk_epoch",
    "chunk_offset", "chunk_count", "stop", "fault"])
def test_trial_generation_bound_download_and_stop(failure):
    fault = failure == "fault"
    status, words = fixture(offset=-1, direction=1, fault=fault)
    idle = dict.fromkeys(rx.FIELDS, 0)
    idle["version"] = 2
    active = False
    downloads, calls = [], []

    def exchange(command):
        nonlocal active
        calls.append(command)
        if command.endswith(":STOP"):
            active = False
            return "ERROR" if failure == "stop" else "OK"
        if ":START " in command:
            active = True
            expected = f"1,7,256,26,-1,1,{int(fault)}"
            assert command.split()[1] == expected
            return expected + ",0" if failure == "start_echo" else expected
        if ":DATA? " in command:
            epoch, offset, count, generation = map(int, command.split()[1].split(","))
            assert generation == 49
            downloads.append(offset)
            return ",".join(map(str, [epoch + (failure == "chunk_epoch"), 256,
                offset + (failure == "chunk_offset"), count - (failure == "chunk_count")] + words[offset:offset + count]))
        assert command == rx.STATUS_QUERY
        observed = dict(status if active else idle)
        if active and failure == "epoch":
            observed["epoch"] += 1
        if active and downloads and failure == "generation":
            observed["generation"] += 1
        return raw_status(observed)

    args = Namespace(codebook=1, base_ns=26, direction=1, offset_samples=-1,
                     fault_idle_low=fault, capture_timeout=1.)
    result = rx.run_trial(exchange, args, 7)
    assert result["passed"] is (failure in (None, "fault")), result
    if failure != "stop":
        assert result["cleanup"]["state"] == 0
    assert sum(command.endswith(":STOP") for command in calls) == 2
    if failure is None:
        assert downloads == list(range(0, 256, 32))
    if fault:
        assert downloads == []


@pytest.mark.parametrize("fault", [None, "launch_only", "wrong_slope", "repeat_jitter", "missing", "duplicate"])
def test_offset_slope_requires_actual_sampling_shift_per_direction(fault):
    trials = []
    for direction in (0, 1):
        for offset in (-1, 0, 1):
            for repeat in range(2):
                epoch = len(trials) + 1
                lag = 120 - offset
                if direction == 1 and fault == "launch_only":
                    lag = 120
                elif direction == 1 and fault == "wrong_slope":
                    lag = 120 + offset
                elif direction == 1 and fault == "repeat_jitter":
                    lag += repeat
                status, words = fixture(offset=offset, direction=direction, epoch=epoch, lag=lag)
                trials.append(dict(epoch=epoch, direction=direction, offset_samples=offset,
                    status=status, passed=True, phase_loaded=True, cleanup={"state": 0},
                    capture=dict(schema="HAOFV_RXPHASE_RAW_V1", generation=status["generation"],
                        epoch=epoch, sample_bits=1, sample_period_ns=4, rx_pin=status["rx_pin"],
                        bit_order="lsb_first", words=words),
                    codeword={"passed": True, "lag_samples": lag}))
    if fault == "missing":
        trials.pop()
    elif fault == "duplicate":
        trials[1] = deepcopy(trials[0])
    result = rx.evaluate_scan(trials, [0, 1], [-1, 0, 1], 2)
    assert result["passed"] is (fault is None)
    assert result["phase_effect_verified"] is (fault is None)
    assert not result["instruction_readback_supported"]
    assert not result["trn_accepted"]


def test_parser_width_signed_offset_and_invalid_range():
    status, _ = fixture(offset=-1)
    assert rx.parse_status(raw_status(status)) == status
    with pytest.raises(ValueError):
        rx.parse_status("1,2,3")
    status["offset_samples"] = -11
    with pytest.raises(ValueError):
        rx.parse_status(raw_status(status))


def test_old_instruction_readback_schema_is_rejected():
    status, words = fixture()
    status["version"] = 1
    with pytest.raises(ValueError, match="version/width"):
        rx.parse_status(raw_status(status))
    assert not evaluate(status, words)["passed"]


@pytest.mark.parametrize("failure", [None, "final_idn", "final_build", "final_error", "initial_idn"])
def test_run_keeps_temporary_diagnostic_scope_and_checks_live_identity(tmp_path, monkeypatch, failure):
    @dataclass
    class Board:
        address: str = "UID"
        build: str = "BUILD"
        idn: str = "NO.1,DHRT100,UID,0.1.0"
        port: str = "COM3"

    board = Board()
    monkeypatch.setattr(rx.marker, "discover", lambda args: {"UID": board})
    closed, calls = [], []
    monkeypatch.setattr(rx.marker, "close_persistent_connections", lambda: closed.append(True))
    active, status, words = False, None, []
    idn_count, build_count = 0, 0
    idle = dict.fromkeys(rx.FIELDS, 0)
    idle["version"] = 2

    def command(target, text, args):
        nonlocal active, status, words, idn_count, build_count
        calls.append(text)
        if text == "*IDN?":
            idn_count += 1
            return "OTHER" if ((idn_count == 1 and failure == "initial_idn") or
                                (idn_count == 2 and failure == "final_idn")) else board.idn
        if text == "SYST:FW:BUILD?":
            build_count += 1
            return '"OTHER"' if build_count == 2 and failure == "final_build" else '"BUILD"'
        if text == "SYST:ERR?":
            return '-1,"bad"' if failure == "final_error" else '0,"No error"'
        if text == "SYST:TDMA:RING:STOP":
            return "OK"
        if text == "SYST:REFMEM:SYNC:TDMA:STAT?":
            from tdma_field_parse import FIELDS
            return ",".join("0" for _ in FIELDS)
        if text.endswith(":STOP"):
            active = False
            return "OK"
        if ":START " in text:
            codebook, epoch, _, base, offset, direction, fault = map(int, text.split()[1].split(","))
            status, words = fixture(codebook, offset, direction, epoch, fault=bool(fault))
            active = True
            return text.split()[1]
        if ":DATA? " in text:
            epoch, offset, count, generation = map(int, text.split()[1].split(","))
            assert generation == status["generation"]
            return ",".join(map(str, [epoch, len(words), offset, count] + words[offset:offset + count]))
        assert text == rx.STATUS_QUERY
        return raw_status(status if active else idle)

    monkeypatch.setattr(rx.marker, "board_command", command)
    args = Namespace(board_id="UID", expected_build="BUILD", base_ns=26, codebook=1,
        epoch=1, repeats=1, direction=[0, 1], offset_samples=[-1, 1],
        out_dir=tmp_path / "result", timeout=1., capture_timeout=1., fault_idle_low=False)
    result = rx.run(args)
    assert result["passed"] is (failure is None), result.get("error")
    assert closed
    assert result["prior_evidence_bound"] is False
    assert result["trn_accepted"] is False
    assert result["parameter_flash_saved"] is False
    assert not any("SAVE" in item or "COMMIT" in item for item in calls)
    if failure == "initial_idn":
        assert not any(":START" in item or ":STOP" in item for item in calls)
    assert (args.out_dir / "summary.json").is_file()


def raw_trial(epoch, direction, offset, lag=None):
    status, words = fixture(offset=offset, direction=direction, epoch=epoch, lag=lag)
    return dict(epoch=epoch, direction=direction, offset_samples=offset, status=status,
        capture=dict(schema="HAOFV_RXPHASE_RAW_V1", epoch=epoch, generation=status["generation"],
            sample_bits=1, sample_period_ns=4, rx_pin=status["rx_pin"], bit_order="lsb_first", words=words),
        cleanup={"state": 0}, **evaluate(status, words))


def scan_fixture():
    trials = [raw_trial(1 + direction * 3 + offset + 1, direction, offset)
              for direction in (0, 1) for offset in (-1, 0, 1)]
    return trials, rx.evaluate_scan(trials, [0, 1], [-1, 0, 1], 1)


def test_selection_recomputes_raw_candidates_instead_of_cached_recommendation():
    trials, scan = scan_fixture()
    for trial in trials:
        trial["recommended_offset_sample_count"] = 9
        trial["best_lag_sample"] = 999
    selection = rx.select_offsets(trials, scan)
    assert selection["passed"], selection
    assert selection["selected_by_direction"] == {"0": -1, "1": -1}
    assert not selection["pipeline_calibrated"]
    assert not selection["trn_accepted"]


@pytest.mark.parametrize("fault", ["scan_failed", "scan_dimensions", "scan_generation", "raw_corrupt",
                                   "disagree", "range", "effective_range"])
def test_selection_rejects_unqualified_or_nonexecutable_candidates(fault):
    trials, scan = scan_fixture()
    if fault == "scan_failed":
        scan["passed"] = False
    elif fault == "scan_dimensions":
        scan["repeats"] = 2
    elif fault == "scan_generation":
        scan["generation_identities"] = []
    elif fault == "raw_corrupt":
        trials[0]["capture"]["words"][5] ^= 0xFFFFFFFF
    elif fault == "disagree":
        trials[0] = raw_trial(1, 0, -1, lag=122)
    else:
        intercept = 141 if fault == "range" else 113  # recommendation +20 / -8 (negative effective).
        trials = [raw_trial(trial["epoch"], trial["direction"], trial["offset_samples"],
                            lag=intercept - trial["offset_samples"]) for trial in trials]
        scan = rx.evaluate_scan(trials, [0, 1], [-1, 0, 1], 1)
        assert scan["passed"]
    result = rx.select_offsets(trials, scan)
    assert not result["passed"]
    assert result["selected_by_direction"] == {}


def residual_fixture():
    scans, scan_result = scan_fixture()
    selection = rx.select_offsets(scans, scan_result)
    trials = [raw_trial(20 + direction * 3 + repeat, direction, -1)
              for direction in (0, 1) for repeat in range(3)]
    kwargs = dict(scan_generation_identities=selection["scan_generation_identities"],
                  expected_configuration=selection["configuration"])
    return trials, selection, kwargs


def test_fresh_selected_phase_has_three_raw_zero_residual_repeats_per_direction():
    trials, selection, kwargs = residual_fixture()
    result = rx.evaluate_residual(trials, selection["selected_by_direction"], 3, **kwargs)
    assert result["passed"], result
    assert result["zero_residual_verified"]
    assert len(result["observations"]) == 6
    assert not result["trn_accepted"]
    assert not result["production_training_certified"]
    assert not result["parameter_flash_saved"]


@pytest.mark.parametrize("effective,passed", [(0, True), (31, True), (32, False)])
def test_hardware_delay_field_boundary(effective, passed):
    status, words = fixture()
    base_ns = max(effective * 4, 1)
    status.update(base_ns=base_ns, base_samples=effective, base_quantized_ns=effective * 4,
        effective_samples=effective, destination_cycles=effective + 1,
        programmed_delay_instruction=rx.NOP_INSTRUCTION | effective << 8, programmed_delay_cycles=effective + 1)
    result = rx.evaluate(status, words, codebook=1, epoch=7, base_ns=base_ns, offset_samples=0, direction=0)
    assert result["passed"] is passed


@pytest.mark.parametrize("fault", ["missing_blacklist", "missing_config", "changed_config",
    "old_epoch", "old_generation", "wrong_offset", "nonzero", "cached_zero", "missing_repeat",
    "duplicate", "raw_corrupt", "generation_mismatch", "cleanup", "unknown_direction"])
def test_residual_rejects_stale_wrong_selected_or_nonzero_raw_evidence(fault):
    trials, selection, kwargs = residual_fixture()
    if fault == "missing_blacklist":
        kwargs.pop("scan_generation_identities")
    elif fault == "missing_config":
        kwargs.pop("expected_configuration")
    elif fault == "changed_config":
        kwargs["expected_configuration"][1] = 28
    elif fault == "old_epoch":
        trials[0] = raw_trial(1, 0, -1)
        trials[0]["status"]["generation"] = trials[0]["capture"]["generation"] = 999
    elif fault == "old_generation":
        trials[0]["status"]["generation"] = trials[0]["capture"]["generation"] = 43
    elif fault == "wrong_offset":
        trials[0] = raw_trial(20, 0, 0, lag=121)
    elif fault in ("nonzero", "cached_zero"):
        trials[0] = raw_trial(20, 0, -1, lag=122)
        if fault == "cached_zero":
            trials[0]["residual_offset_sample_count"] = 0
    elif fault == "missing_repeat":
        trials.pop()
    elif fault == "duplicate":
        trials[1] = deepcopy(trials[0])
    elif fault == "raw_corrupt":
        trials[0]["capture"]["words"][5] ^= 0xFFFFFFFF
    elif fault == "generation_mismatch":
        trials[0]["capture"]["generation"] += 1
    elif fault == "cleanup":
        trials[0]["cleanup"]["state"] = 2
    elif fault == "unknown_direction":
        selection["selected_by_direction"] = {"2": -1}
    result = rx.evaluate_residual(trials, selection["selected_by_direction"], 3, **kwargs)
    assert not result["passed"], fault
    assert not result["zero_residual_verified"]
