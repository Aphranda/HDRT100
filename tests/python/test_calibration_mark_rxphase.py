"""MARK phase uses active-low origin and idle-high pads, with its own evidence."""
from argparse import Namespace
from copy import deepcopy

import pytest

from tools.calibration_ring_validate import calibration_rxphase as rx


def mark_capture(*, epoch=7, offset=0, codebook=1, lag=None, fault=False):
    waveform, _ = rx.marker.marker_raw_waveform(
        codebook_id=codebook, epoch=epoch, source_node=0)
    lag = 120 - offset if lag is None else lag
    # Only the origin polarity changes. The Manchester codeword is not inverted.
    samples = [1] * lag + waveform
    samples += [1] * (8192 - len(samples))
    words = [sum(value << bit for bit, value in enumerate(samples[start:start + 32]))
             for start in range(0, 8192, 32)]
    status = dict(version=2, state=3 if fault else 2, scope=5,
        flags=11 if fault else 15, reject_reason=4 if fault else 0,
        epoch=epoch, generation=100 + epoch, codebook=codebook, direction=0,
        sample_period_ns=4, base_ns=26, base_samples=7, base_quantized_ns=28,
        offset_samples=offset, effective_samples=7 + offset, source_pipeline_cycles=1,
        destination_cycles=8 + offset, programmed_delay_instruction=0xA042 | ((7 + offset) << 8),
        programmed_delay_cycles=8 + offset, tx_pin=26, rx_pin=27,
        code_samples=len(waveform), tx_words=(320 + len(waveform) + 31) // 32,
        tx_remaining=0, tx_drained=1, requested_words=256,
        produced_words=0 if fault else 256, rx_remaining=256 if fault else 0,
        origin_lead_samples=64, origin_low_samples=64, launch_guard_samples=128,
        fault=int(fault))
    assert len(status) == 32 and set(status) == set(rx.MARK_PROTOCOL.fields)
    return status, [] if fault else words


def evaluate(status, words, *, fault=False):
    return rx.evaluate(status, words, codebook=status["codebook"], epoch=status["epoch"],
        base_ns=26, offset_samples=status["offset_samples"], direction=status["direction"],
        fault_idle_low=fault, protocol=rx.MARK_PROTOCOL)


def trial(*, epoch, offset, lag=None):
    status, words = mark_capture(epoch=epoch, offset=offset, lag=lag)
    return dict(epoch=epoch, direction=0, offset_samples=offset, status=status,
        passed=True, cleanup={"state": 0}, capture=dict(schema="HAOFV_MARK_RXPHASE_RAW_V1",
            epoch=epoch, generation=status["generation"], sample_bits=1, sample_period_ns=4,
            rx_pin=27, bit_order="lsb_first", words=words))


def scan_trials():
    return [trial(epoch=1 + index * 3 + repeat, offset=offset)
            for index, offset in enumerate((-1, 0, 1)) for repeat in range(3)]


def scan(trials):
    return rx.evaluate_scan(trials, [0], [-1, 0, 1], 3, protocol=rx.MARK_PROTOCOL)


@pytest.mark.parametrize("codebook", range(4))
def test_mark_keeps_original_full_codeword_between_idle_high_guard_and_tail(codebook):
    status, words = mark_capture(codebook=codebook, offset=-1)
    raw_status = ",".join(str(status[field]) for field in rx.MARK_PROTOCOL.fields)
    assert rx.parse_status(raw_status, protocol=rx.MARK_PROTOCOL) == status
    assert "origin_high_samples" not in status and status["origin_low_samples"] == 64
    result = evaluate(status, words)
    assert result["passed"], result
    assert result["signal"] == "MARK" and result["origin_pulse_level"] == 0
    assert result["codeword"]["logical_bit_count"] == 321
    assert result["codeword"]["logical_bit_errors"] == 0
    assert result["codeword"]["best_distance"] == 0
    assert result["best_lag_sample"] == 121
    assert result["requested_base_ns"] == 26 and result["quantized_base_ns"] == 28
    assert result["phase_loaded"] and not result["phase_effect_verified"]
    assert not result["instruction_readback_supported"]
    assert not result["trn_accepted"] and not result["production_training_certified"]


@pytest.mark.parametrize("corruption", ["low_guard", "low_tail", "inverted_codeword", "idle_high_only"])
def test_mark_polarity_or_constant_idle_cannot_masquerade_as_a_full_capture(corruption):
    status, words = mark_capture()
    if corruption == "low_guard":
        words[0] &= ~1
    elif corruption == "low_tail":
        words[-1] &= ~(1 << 31)
    elif corruption == "idle_high_only":
        words = [0xFFFFFFFF] * 256
    else:
        for index in range(120, 120 + status["code_samples"]):
            words[index // 32] ^= 1 << (index % 32)
    result = evaluate(status, words)
    assert not result["passed"]
    if corruption == "low_guard":
        assert "launch_guard_activity" in result["failures"]
    if corruption == "low_tail":
        assert "tail_truncated_or_activity" in result["failures"]


@pytest.mark.parametrize("foreign", ["scope", "pins", "schema", "direction"])
def test_sck_evidence_and_second_direction_cannot_enter_mark_scan(foreign):
    trials = scan_trials()
    first = trials[0]
    if foreign == "scope":
        first["status"]["scope"] = 4
    elif foreign == "pins":
        first["status"].update(tx_pin=25, rx_pin=28)
        first["capture"]["rx_pin"] = 28
    elif foreign == "schema":
        first["capture"]["schema"] = "HAOFV_RXPHASE_RAW_V1"
    else:
        first["direction"] = first["status"]["direction"] = 1
    result = scan(trials)
    assert not result["passed"] and not result["phase_effect_verified"]


def test_mark_raw_slope_selects_offset_then_requires_fresh_zero_residual():
    trials = scan_trials()
    result = scan(trials)
    assert result["passed"] and result["phase_effect_verified"]
    assert [row["lag_samples"] for row in result["observations"]] == [[121] * 3, [120] * 3, [119] * 3]
    selection = rx.select_offsets(trials, result, protocol=rx.MARK_PROTOCOL)
    assert selection["passed"] and selection["selected_by_direction"] == {"0": -1}
    fresh = [trial(epoch=20 + repeat, offset=-1) for repeat in range(3)]
    residual = rx.evaluate_residual(fresh, selection["selected_by_direction"], 3,
        scan_generation_identities=selection["scan_generation_identities"],
        expected_configuration=selection["configuration"], protocol=rx.MARK_PROTOCOL)
    assert residual["passed"] and residual["zero_residual_verified"]
    assert not residual["trn_accepted"] and not residual["pipeline_calibrated"]


def test_sck_scan_configuration_cannot_seed_mark_residual():
    trials = scan_trials()
    selection = rx.select_offsets(trials, scan(trials), protocol=rx.MARK_PROTOCOL)
    foreign_configuration = list(selection["configuration"])
    assert foreign_configuration[-1] == 5
    foreign_configuration[-1] = 4
    fresh = [trial(epoch=20 + repeat, offset=-1) for repeat in range(3)]
    result = rx.evaluate_residual(fresh, {"0": -1}, 3,
        scan_generation_identities=selection["scan_generation_identities"],
        expected_configuration=foreign_configuration, protocol=rx.MARK_PROTOCOL)
    assert not result["passed"]
    assert "scan_configuration_missing_or_changed" in result["failures"]


@pytest.mark.parametrize("failure", ["fixed_lag", "stale_raw", "corrupt_raw", "wrong_selected"])
def test_mark_selection_and_residual_recompute_raw_instead_of_trusting_cached_success(failure):
    trials = scan_trials()
    if failure == "fixed_lag":
        trials = [trial(epoch=t["epoch"], offset=t["offset_samples"], lag=120) for t in trials]
        result = scan(trials)
        assert not result["passed"]
        assert not rx.select_offsets(trials, result, protocol=rx.MARK_PROTOCOL)["passed"]
        return
    selection = rx.select_offsets(trials, scan(trials), protocol=rx.MARK_PROTOCOL)
    fresh = [trial(epoch=20 + repeat, offset=-1) for repeat in range(3)]
    if failure == "stale_raw":
        fresh[0] = deepcopy(trials[0])
    elif failure == "corrupt_raw":
        fresh[0]["capture"]["words"][-1] &= ~(1 << 31)
        fresh[0].update(passed=True, residual_offset_sample_count=0)
    else:
        fresh[0] = trial(epoch=20, offset=0)
    result = rx.evaluate_residual(fresh, selection["selected_by_direction"], 3,
        scan_generation_identities=selection["scan_generation_identities"],
        expected_configuration=selection["configuration"], protocol=rx.MARK_PROTOCOL)
    assert not result["passed"] and not result["zero_residual_verified"]


@pytest.mark.parametrize("failure", [None, "idle_high", "foreign_status", "data_timeout"])
def test_mark_trial_uses_only_mark_commands_and_always_stops_its_own_role(failure):
    fault = failure == "idle_high"
    status, words = mark_capture(offset=-1, fault=fault)
    idle = dict.fromkeys(rx.MARK_PROTOCOL.fields, 0)
    idle.update(version=2, scope=5)
    active = False
    calls = []

    def exchange(command):
        nonlocal active
        calls.append(command)
        assert "SCK" not in command and ":P3" not in command
        if command == "CAL:MARK:RXPHASE:STOP":
            active = False
            return "OK"
        if command.startswith("CAL:MARK:RXPHASE:START "):
            expected = f"1,7,256,26,-1,0,{int(fault)}"
            assert command.split()[1] == expected
            active = True
            return expected
        if command.startswith("READ:CAL:MARK:RXPHASE:DATA? "):
            if failure == "data_timeout":
                raise RuntimeError("SCPI timeout")
            epoch, offset, count, generation = map(int, command.split()[1].split(","))
            assert (epoch, generation) == (7, 107)
            return ",".join(map(str, [epoch, 256, offset, count] + words[offset:offset + count]))
        assert command == "READ:CAL:MARK:RXPHASE?"
        observed = dict(status if active else idle)
        if active and failure == "foreign_status":
            observed["scope"] = 4
        return ",".join(str(observed[field]) for field in rx.MARK_PROTOCOL.fields)

    args = Namespace(codebook=1, base_ns=26, direction=0, offset_samples=-1,
                     fault_idle_low=fault, capture_timeout=1.)
    result = rx.run_trial(exchange, args, 7, protocol=rx.MARK_PROTOCOL)
    assert result["passed"] is (failure in (None, "idle_high")), result
    assert calls[0] == "CAL:MARK:RXPHASE:STOP"
    assert calls[-2:] == ["CAL:MARK:RXPHASE:STOP", "READ:CAL:MARK:RXPHASE?"]
    assert calls.count("CAL:MARK:RXPHASE:STOP") == 2
    assert result["cleanup"]["state"] == 0
    if failure in (None, "idle_high"):
        assert result["capture"]["schema"] == "HAOFV_MARK_RXPHASE_RAW_V1"
    if fault:
        assert not any(":DATA?" in command for command in calls)


@pytest.mark.parametrize("failure", ["tx_undrained", "wrong_reject", "partial_capture", "normal_completion"])
def test_idle_high_fault_requires_no_sample_and_expected_timeout(failure):
    status, words = mark_capture(fault=True)
    assert evaluate(status, words, fault=True)["passed"]
    if failure == "tx_undrained":
        status["tx_drained"] = 0
    elif failure == "wrong_reject":
        status["reject_reason"] = 2
    elif failure == "partial_capture":
        status.update(produced_words=1, rx_remaining=255)
        words = [0xFFFFFFFF]
    else:
        status.update(state=2, reject_reason=0)
    result = evaluate(status, words, fault=True)
    assert not result["passed"] and "expected_idle_high_timeout" in result["failures"]


@pytest.mark.parametrize("option", ["--fault-idle-high", "--fault-idle-low", "direction1"])
def test_mark_cli_has_only_its_idle_high_fault_and_single_direction(monkeypatch, option):
    argv = ["calibration_mark_rxphase.py", "--board-id", "board", "--expected-build", "build",
            "--base-ns", "26", "--out-dir", "out/unused-mark-cli-test"]
    argv += ["--direction", "1"] if option == "direction1" else [option]
    monkeypatch.setattr(rx.sys, "argv", argv)
    acquired = []

    def run(args, *, protocol):
        acquired.append(args)
        assert protocol is rx.MARK_PROTOCOL
        assert args.direction == [0] and args.fault_idle_low is True
        return {"passed": True, "phase_loaded": True}

    monkeypatch.setattr(rx, "run", run)
    if option == "--fault-idle-high":
        assert rx.main(rx.MARK_PROTOCOL) == 0
        assert len(acquired) == 1
    else:
        with pytest.raises(SystemExit) as error:
            rx.main(rx.MARK_PROTOCOL)
        assert error.value.code == 2 and not acquired
