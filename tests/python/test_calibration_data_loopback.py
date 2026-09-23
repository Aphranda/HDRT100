"""Independent DATA source identity, same-shot evidence, and loaded-phase gates."""
from argparse import Namespace
from copy import deepcopy

import pytest

from tools.calibration_ring_validate import calibration_data_loopback as data


CONFIG = dict(zip(data.CONFIG_FIELDS, (1, 2, 0, 0, 123, 234, 345, 456, 6, 6, 0, 4)))


def pack(samples, bits):
    per_word = 32 // bits
    return [sum(value << (index * bits) for index, value in enumerate(samples[start:start + per_word]))
            for start in range(0, len(samples), per_word)]


def fixture(epoch=7, offset=0, *, fault=0, codebook=1, lag=None, data_source=1, source_shift=0, forward_shift=0):
    mark_wave, _ = data.marker.marker_raw_waveform(codebook_id=codebook, epoch=epoch, source_node=0)
    data_wave, _ = data.marker.marker_raw_waveform(codebook_id=codebook, epoch=epoch, source_node=data_source)
    firstfall = next(i for i in range(1, len(mark_wave)) if mark_wave[i - 1] and not mark_wave[i])
    starts = (256, 269 + forward_shift, 1287 + source_shift + forward_shift, 1300 + source_shift + forward_shift)
    channels = []
    for index, start in enumerate(starts):
        idle = int(index < 2)
        wave = mark_wave if idle else data_wave
        channel = [idle] * 8192
        if not fault == 1 and not (fault == 2 and index >= 2):
            channel[start:start + len(wave)] = wave
        channels.append(channel)
    common = pack([sum(channel[i] << (pin - 24) for pin, channel in zip((26, 27, 29, 24), channels))
                   for i in range(8192)], 8)
    lag = 126 - offset + forward_shift if lag is None else lag
    phased_samples = [0] * 8192
    if not fault:
        phased_samples[lag:lag + len(data_wave)] = data_wave
    phased = [] if fault == 1 else pack(phased_samples, 1)
    status = dict.fromkeys(data.FIELDS, 0)
    status.update(version=1, state=3 if fault else 2, scope=6, flags=3 if fault == 1 else 15,
        reject_reason=4 if fault == 1 else 1 if fault == 2 else 0,
        epoch=epoch, generation=epoch + 100, codebook=codebook, sample_period_ns=4,
        base_ns=26, base_samples=7, base_quantized_ns=28, mark_offset=-1, data_offset=offset,
        gap_samples=1000, guard_samples=128, source_long=1000, sink_long=886,
        source_phase=7, sink_phase=21 + offset,
        source_programmed_instruction=0xA642, sink_programmed_instruction=0xA042 | ((20 + offset) << 8),
        marker_samples=len(mark_wave), data_samples=len(data_wave),
        marker_tx_words=(320 + len(mark_wave) + 31) // 32, data_tx_words=(64 + len(data_wave) + 31) // 32,
        marker_drained=1, data_drained=int(fault != 1),
        data_remaining=199 if fault == 1 else 0,
        phased_requested=256, phased_produced=0 if fault == 1 else 256,
        phased_remaining=256 if fault == 1 else 0,
        common_requested=2048, common_produced=2048,
        marker_sm=0, data_sm=1, phased_sm=2, common_sm=3,
        marker_dma=5, data_dma=6, phased_dma=4, common_dma=8,
        marker_tx_pin=26, marker_rx_pin=27, data_tx_pin=29, data_rx_pin=24,
        edge_mask=0 if fault == 1 else 3 if fault == 2 else 15,
        t1_sample=0 if fault == 1 else 256 + firstfall,
        t2_sample=0 if fault == 1 else starts[1] + firstfall,
        t3_sample=0 if fault else starts[2], t4_sample=0 if fault else starts[3],
        fault=fault, nominal_lag=128, marker_source_node=0, data_source_node=1)
    request = dict(codebook=codebook, epoch=epoch, base_ns=26, mark_offset=-1,
                   data_offset=offset, gap_samples=1000, fault=fault)
    captures = {name: dict(schema=data.SCHEMA, kind=kind, epoch=epoch, generation=epoch + 100,
        sample_bits=bits, sample_period_ns=4, bit_order="lsb_first", gpio_base=24, words=words)
        for (name, (kind, _, bits)), words in zip(data.KINDS.items(), (phased, common))}
    return dict(epoch=epoch, status=status, request=request, captures=captures,
                passed=True, cleanup=dict(scope=6, **dict.fromkeys(data.CLEANUP_ZERO_FIELDS, 0)), configuration=deepcopy(CONFIG))


def evaluate(trial):
    return data.evaluate(trial["status"], trial["captures"]["phased"]["words"],
                         trial["captures"]["common"]["words"], **trial["request"])


def trials():
    return [fixture(epoch=1 + i * 3 + repeat, offset=offset)
            for i, offset in enumerate((-4, -2, 0, 2, 4)) for repeat in range(3)]


@pytest.mark.parametrize("codebook", range(4))
def test_same_shot_common_and_phased_decode_independent_321bit_sources(codebook):
    item = fixture(codebook=codebook)
    result = evaluate(item)
    assert result["passed"], result
    assert result["common_codewords"]["26"]["source_node"] == 0
    assert result["common_codewords"]["29"]["source_node"] == 1
    assert result["phased_codeword"]["logical_bit_errors"] == 0
    assert result["best_lag_sample"] == 126 and result["recommended_offset_sample_count"] == -2
    assert result["capture_origin_common_sample"] == 1300 - 126
    assert result["forward_path_samples"] == result["return_path_samples"] == 13
    assert not result["instruction_readback_supported"] and not result["trn_accepted"]
    assert not result["production_training_certified"]


def test_marker_copied_to_data_cannot_supply_data_source_identity():
    result = evaluate(fixture(data_source=0))
    assert not result["passed"]
    assert "common_pin29_codeword" in result["failures"] and "phased_codeword" in result["failures"]


@pytest.mark.parametrize("fault", [1, 2])
def test_faults_bind_exact_terminal_state_to_raw_idle_pads(fault):
    item = fixture(fault=fault)
    assert evaluate(item)["passed"]
    item["captures"]["common"]["words"][-1] ^= 1 << 5
    assert not evaluate(item)["passed"]


@pytest.mark.parametrize("change", ["programmed", "dma", "sm", "generation", "t4", "abort", "lease", "foreign_scope"])
def test_hardware_resource_and_timing_snapshot_must_match_raw_capture(change):
    item = fixture()
    field, value = {"programmed": ("sink_programmed_instruction", 0), "dma": ("common_dma", 4),
        "sm": ("data_sm", 0), "generation": ("generation", 0), "t4": ("t4_sample", 1301),
        "abort": ("abort_pending", 1), "lease": ("extra_lease_claimed", 1), "foreign_scope": ("scope", 5)}[change]
    item["status"][field] = value
    assert not evaluate(item)["passed"]


def test_raw_scan_selects_and_reloads_fresh_offset_without_cached_acceptance():
    rows = trials()
    scan = data.evaluate_scan(rows, [-4, -2, 0, 2, 4], 3)
    assert scan["passed"] and scan["phase_effect_verified"]
    selected = data.select_offset(rows, scan)
    assert selected["passed"] and selected["selected_offset"] == -2
    fresh = [fixture(epoch=30 + i, offset=-2) for i in range(3)]
    result = data.evaluate_residual(fresh, -2, 3,
        scan_generation_identities=selected["scan_generation_identities"], expected_configuration=selected["configuration"])
    assert result["passed"] and result["zero_residual_verified"]
    assert not result["pipeline_calibrated"]


@pytest.mark.parametrize("failure", ["fixed_lag", "foreign_generation", "swapped_kinds", "missing_raw",
    "wrong_source", "config_changed", "cleanup", "cleanup_lease", "reused_generation", "crc_corrupt", "moving_source"])
def test_scan_recomputes_full_raw_evidence_and_binds_config_and_generation(failure):
    rows = [fixture(epoch=1 + i, offset=offset) for i, offset in enumerate((-2, 0, 2))]
    first = rows[0]
    if failure == "fixed_lag":
        rows = [fixture(epoch=t["epoch"], offset=t["request"]["data_offset"], lag=126) for t in rows]
    elif failure == "moving_source":
        # Correct receiver lag slope alone cannot certify fixed source timing.
        rows[0] = fixture(epoch=1, offset=-2, source_shift=1)
    elif failure == "foreign_generation":
        first["captures"]["common"]["generation"] += 1
    elif failure == "swapped_kinds":
        first["captures"]["common"], first["captures"]["phased"] = first["captures"]["phased"], first["captures"]["common"]
    elif failure == "missing_raw":
        first["captures"]["common"].pop("words")
    elif failure == "wrong_source":
        rows[0] = fixture(epoch=1, offset=-2, data_source=0)
    elif failure == "config_changed":
        first["configuration"]["schedule_crc"] += 1
    elif failure == "cleanup":
        first["cleanup"]["state"] = 1
    elif failure == "cleanup_lease":
        first["cleanup"]["extra_lease_claimed"] = 1
    elif failure == "reused_generation":
        first["status"]["generation"] = 102
        for capture in first["captures"].values():
            capture["generation"] = 102
    else:
        # Invert a complete logical header bit, preserving cached success flags.
        words = first["captures"]["phased"]["words"]
        for sample in range(128 + 40 * 20, 128 + 41 * 20):
            words[sample // 32] ^= 1 << (sample % 32)
    scan = data.evaluate_scan(rows, [-2, 0, 2], 1)
    assert not scan["passed"] and not scan["phase_effect_verified"]


@pytest.mark.parametrize("failure", ["reused_epoch", "reused_generation", "nonzero", "no_metadata", "configuration"])
def test_residual_must_be_fresh_selected_and_configuration_bound(failure):
    source = [fixture(epoch=1 + i, offset=offset) for i, offset in enumerate((-2, 0, 2))]
    selected = data.select_offset(source, data.evaluate_scan(source, [-2, 0, 2], 1))
    fresh = [fixture(epoch=20, offset=-2)]
    metadata = dict(scan_generation_identities=selected["scan_generation_identities"], expected_configuration=selected["configuration"])
    if failure == "reused_epoch":
        fresh = [fixture(epoch=1, offset=-2)]
    elif failure == "reused_generation":
        fresh[0]["status"]["generation"] = 101
        for capture in fresh[0]["captures"].values():
            capture["generation"] = 101
    elif failure == "nonzero":
        fresh = [fixture(epoch=20, offset=-2, lag=129)]
    elif failure == "no_metadata":
        metadata = {}
    else:
        fresh[0]["configuration"]["profile_crc"] += 1
    assert not data.evaluate_residual(fresh, -2, 1, **metadata)["passed"]


@pytest.mark.parametrize("failure", [None, "echo", "config_changed", "status_changed", "chunk", "timeout", "idlehigh", "idlelow"])
def test_trial_acquires_both_kinds_under_one_identity_and_role_scoped_stop(failure):
    item = fixture(fault=1 if failure == "idlehigh" else 2 if failure == "idlelow" else 0)
    idle = dict.fromkeys(data.FIELDS, 0)
    idle.update(version=1, scope=6)
    calls, downloads = [], []
    active = False
    configs = 0

    def exchange(command):
        nonlocal active, configs
        calls.append(command)
        if command == "CAL:DATA:LOOP:STOP":
            active = False
            return "OK"
        if command == "READ:CAL:DATA:LOOP:CONFIG?":
            configs += 1
            config = dict(CONFIG)
            if failure == "config_changed" and configs > 1:
                config["schedule_crc"] += 1
            return ",".join(str(config[field]) for field in data.CONFIG_FIELDS)
        if command.startswith("CAL:DATA:LOOP:START "):
            active = True
            return command.split()[1] + (",0" if failure == "echo" else "")
        if command.startswith("READ:CAL:DATA:LOOP:DATA? "):
            if failure == "timeout":
                raise RuntimeError("SCPI timeout")
            epoch, kind, offset, count, generation = map(int, command.split()[1].split(","))
            assert (epoch, generation) == (7, 107)
            name = "phased" if kind == 0 else "common"
            words = item["captures"][name]["words"]
            downloads.append((kind, offset))
            return ",".join(map(str, [epoch, len(words), offset + int(failure == "chunk"), count] + words[offset:offset + count]))
        assert command == "READ:CAL:DATA:LOOP?"
        status = dict(item["status"] if active else idle)
        if active and downloads and failure == "status_changed":
            status["generation"] += 1
        return ",".join(str(status[field]) for field in data.FIELDS)

    args = Namespace(**{key: value for key, value in item["request"].items() if key != "epoch"}, capture_timeout=1.)
    result = data.run_trial(exchange, args, 7)
    assert result["passed"] is (failure in (None, "idlehigh", "idlelow")), result
    assert calls.count("CAL:DATA:LOOP:STOP") == 2 and result["cleanup"]["state"] == 0
    assert calls[-2:] == ["CAL:DATA:LOOP:STOP", "READ:CAL:DATA:LOOP?"]
    if failure is None:
        assert downloads == [(0, i) for i in range(0, 256, 32)] + [(1, i) for i in range(0, 2048, 32)]


def test_status_requires_64_fields_version_and_only_two_signed_offsets():
    status = fixture()["status"]
    raw = ",".join(str(status[key]) for key in data.FIELDS)
    assert data.parse_status(raw) == status
    for bad in (raw + ",0", raw.replace("1,2,6", "2,2,6", 1)):
        with pytest.raises(ValueError):
            data.parse_status(bad)
    status["generation"] = -1
    with pytest.raises(ValueError):
        data.parse_status(",".join(str(status[key]) for key in data.FIELDS))


def window_trials(shifts=(0, 0, 1)):
    return [fixture(epoch=1 + group * len(shifts) + repeat, offset=offset, forward_shift=shift)
        for group, offset in enumerate((-2, 0, 2)) for repeat, shift in enumerate(shifts)]


def window_selection():
    rows = window_trials()
    scan = data.evaluate_scan(rows, [-2, 0, 2], 3)
    return rows, scan, data.select_offset(rows, scan, diagnostic_window=True)


def test_tx_cs_anchor_explains_raw_path_jitter_without_erasing_candidate_disagreement():
    rows, scan, selected = window_selection()
    assert scan["passed"] and scan["phase_effect_verified"]
    assert scan["phase_origin_pin"] == 26
    assert scan["measured_path_histograms"]["forward_path_samples"] == {13: 6, 14: 3}
    assert "common_path_samples" not in scan["configuration"]
    normal, jitter = evaluate(rows[0]), evaluate(rows[2])
    assert normal["capture_origin_after_tx_cs_sample"] == jitter["capture_origin_after_tx_cs_sample"]
    assert normal["capture_origin_after_rx_cs_sample"] - jitter["capture_origin_after_rx_cs_sample"] == 1
    assert not data.select_offset(rows, scan)["passed"]  # Existing strict policy remains strict.
    assert selected["passed"] and selected["diagnostic_candidate_allowed"]
    assert selected["selected_offset"] == -2 and selected["candidate_disagreement"]
    assert not selected["strict_fixed_offset_verified"]
    assert selected["residual_policy"]["residual_support"] == [0, 1]
    assert selected["policy_id"] == data.DIAGNOSTIC_WINDOW_POLICY_ID
    assert not selected["trn_accepted"] and not selected["production_training_certified"]


def test_strict_zero_residual_cannot_accept_a_new_uniform_source_residence():
    rows = [fixture(epoch=1 + index, offset=offset) for index, offset in enumerate((-2, 0, 2))]
    scan = data.evaluate_scan(rows, [-2, 0, 2], 1)
    selection = data.select_offset(rows, scan)
    assert selection["passed"]
    fresh = [fixture(epoch=20 + index, offset=-2, source_shift=1) for index in range(3)]
    for trial in fresh:
        measured = evaluate(trial)
        assert measured["passed"] and measured["residual_offset_sample_count"] == 0
        assert measured["responder_residence_samples"] == selection["configuration"]["source_residence_samples"] + 1
    result = data.evaluate_residual(fresh, -2, 3,
        scan_generation_identities=selection["scan_generation_identities"],
        expected_configuration=selection["configuration"])
    assert not result["passed"] and not result["zero_residual_verified"]
    assert "scan_metadata_missing_or_changed" in result["failures"]


@pytest.mark.parametrize("failure", ["mode_disagreement", "tie", "span2", "too_few", "moving_source"])
def test_diagnostic_candidate_requires_consistent_strict_majority_and_one_sample_span(failure):
    repeats = 4 if failure == "tie" else 2 if failure == "too_few" else 3
    shifts = (0, 0, 1, 1) if failure == "tie" else (0, 1) if failure == "too_few" else (0, 0, 2) if failure == "span2" else (0, 0, 1)
    rows = window_trials(shifts)
    if failure == "mode_disagreement":
        rows[-3:] = [fixture(epoch=7 + index, offset=2, forward_shift=shift) for index, shift in enumerate((0, 1, 1))]
    elif failure == "moving_source":
        # Match the lag shift too: normalization alone now passes, so the
        # independent source-residence gate must still reject this mutation.
        rows[0] = fixture(epoch=1, offset=-2, source_shift=1, lag=129)
    scan = data.evaluate_scan(rows, [-2, 0, 2], repeats)
    selected = data.select_offset(rows, scan, diagnostic_window=True)
    assert not selected["passed"] and not selected["diagnostic_candidate_allowed"]


@pytest.mark.parametrize("shifts,strict_zero", [((0, 0, 1), False), ((0, 0, 0), True)])
def test_fresh_bounded_residual_is_distinct_from_strict_zero(shifts, strict_zero):
    rows, scan, selected = window_selection()
    fresh = [fixture(epoch=20 + index, offset=-2, forward_shift=shift) for index, shift in enumerate(shifts)]
    kwargs = dict(scan_generation_identities=selected["scan_generation_identities"], expected_configuration=selected["configuration"])
    result = data.evaluate_residual(fresh, -2, 3, **kwargs, diagnostic_window=True, residual_policy=selected["residual_policy"],
                                   scan_trials=rows, scan=scan)
    assert result["passed"] and result["bounded_residual_verified"]
    assert result["strict_zero_residual_verified"] is strict_zero
    assert result["zero_residual_verified"] is strict_zero
    assert not result["trn_accepted"] and not result["production_training_certified"]
    assert data.evaluate_residual(fresh, -2, 3, **kwargs)["passed"] is strict_zero


@pytest.mark.parametrize("failure", ["all_plus_one", "new_negative_side", "span2", "tie", "stale",
    "wrong_policy", "no_policy", "moving_source", "too_few", "wrong_selected", "policy_other_scan", "swapped_support", "no_raw_scan"])
def test_diagnostic_residual_rejects_unseen_or_noncentered_or_stale_evidence(failure):
    rows, scan, selected = window_selection()
    shifts = {"all_plus_one": (1, 1, 1), "new_negative_side": (0, 0, -1), "span2": (0, 0, 2),
              "tie": (0, 0, 1, 1), "too_few": (0, 0)}.get(failure, (0, 0, 1))
    fresh = [fixture(epoch=20 + index, offset=-2, forward_shift=shift) for index, shift in enumerate(shifts)]
    policy = deepcopy(selected["residual_policy"])
    if failure == "stale":
        fresh[0] = deepcopy(rows[0])
    elif failure == "wrong_policy":
        policy["policy_id"] = data.STRICT_POLICY_ID
    elif failure == "no_policy":
        policy = None
    elif failure == "moving_source":
        fresh[0] = fixture(epoch=20, offset=-2, source_shift=1, lag=129)
    elif failure == "wrong_selected":
        fresh[0] = fixture(epoch=20, offset=-1)
    elif failure == "policy_other_scan":
        policy["scan_generation_identities"] = [[1, 99]]
    elif failure == "swapped_support":
        policy["residual_support"] = [-1, 0]
        fresh = [fixture(epoch=20 + index, offset=-2, forward_shift=shift) for index, shift in enumerate((0, 0, -1))]
    elif failure == "no_raw_scan":
        rows = None
    result = data.evaluate_residual(fresh, -2, len(fresh), diagnostic_window=True, residual_policy=policy,
        scan_generation_identities=selected["scan_generation_identities"], expected_configuration=selected["configuration"],
        scan_trials=rows, scan=scan)
    assert not result["passed"] and not result["bounded_residual_verified"]
    assert not result["zero_residual_verified"]
