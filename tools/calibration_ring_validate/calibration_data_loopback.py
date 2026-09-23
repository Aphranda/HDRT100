#!/usr/bin/env python3
"""Acquire independent MARK-gated DATA and same-shot common/phased evidence."""
from __future__ import annotations

import csv
from collections import Counter
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from calibration_ring_validate import calibration_marker_loopback as marker

PREFIX = "CAL:DATA:LOOP"
FIELDS = tuple("version state scope flags reject_reason epoch generation codebook sample_period_ns "
    "base_ns base_samples base_quantized_ns mark_offset data_offset gap_samples guard_samples "
    "source_long sink_long source_phase sink_phase source_programmed_instruction sink_programmed_instruction "
    "marker_samples data_samples marker_tx_words data_tx_words marker_remaining data_remaining "
    "marker_drained data_drained phased_requested phased_produced phased_remaining common_requested "
    "common_produced common_remaining marker_sm data_sm phased_sm common_sm marker_dma data_dma phased_dma "
    "common_dma marker_tx_pin marker_rx_pin data_tx_pin data_rx_pin edge_mask t1_sample t2_sample "
    "t3_sample t4_sample fault de_mask extra_lease_claimed transition abort_mask abort_pending nominal_lag "
    "marker_source_node data_source_node reserved0 reserved1".split())
CONFIG_FIELDS = tuple("version node_count local_slot reference_slot topology_generation topology_crc "
    "profile_crc schedule_crc config_seq applied_config_seq calibration_generation sample_period_ns".split())
KINDS = {"phased": (0, 256, 1), "common": (1, 2048, 8)}
SCHEMA = "HAOFV_DATA_LOOP_RAW_V1"
CLEANUP_ZERO_FIELDS = ("state", "de_mask", "extra_lease_claimed", "transition", "abort_mask", "abort_pending")
DIAGNOSTIC_WINDOW_POLICY_ID = "DATA_LOOP_DIAGNOSTIC_ADJACENT_SAMPLE_V1"
STRICT_POLICY_ID = "DATA_LOOP_STRICT_FIXED_OFFSET_V1"
POLICY_SOURCE = ["tools/calibration_ring_validate/calibration_data_train.py:summarize_repeat_matrix",
    "tools/calibration_ring_validate/calibration_data_train.py:--max-offset-span default=1",
    "docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md:2.1.1 and 7"]


def parse_status(raw):
    values = [int(v.strip(), 0) for v in next(csv.reader([raw]), [])]
    if len(values) != 64 or values[0] != 1:
        raise ValueError("DATA status schema/version mismatch")
    if any(not (-10 <= value <= 10 if index in (12, 13) else 0 <= value <= 0xFFFFFFFF)
           for index, value in enumerate(values)):
        raise ValueError("DATA status field outside range")
    return dict(zip(FIELDS, values))


def parse_configuration(raw):
    values = marker.csv_u32(raw)
    if len(values) != len(CONFIG_FIELDS):
        raise ValueError("DATA configuration schema mismatch")
    result = dict(zip(CONFIG_FIELDS, values))
    if (result["version"] != 1 or result["node_count"] != 2 or result["local_slot"] != 0 or
            result["reference_slot"] != 0 or result["sample_period_ns"] != 4 or
            result["config_seq"] != result["applied_config_seq"] or
            any(not result[key] for key in ("topology_generation", "topology_crc", "profile_crc", "schedule_crc"))):
        raise ValueError("DATA configuration is not settled dual-slot identity")
    return result


def _wave(codebook, epoch, source):
    return marker.marker_raw_waveform(codebook_id=codebook, epoch=epoch, source_node=source)


def _decode(samples, *, codebook, epoch, source, starts, idle, check_guard=True):
    waveform, vector = _wave(codebook, epoch, source)
    logical, _, _ = marker.marker_logical_bits(codebook_id=codebook, epoch=epoch, source_node=source)
    candidates = sorted((sum(a != b for a, b in zip(waveform, samples[start:start + len(waveform)])), start)
        for start in starts if 0 <= start and start + len(waveform) + 64 <= len(samples))
    if len(candidates) < 2:
        return dict(passed=False, failures=["truncated_codeword"])
    distance, start = candidates[0]
    margin = candidates[1][0] - distance
    validation = marker.validate_marker_window(samples, start,
        half_chip_samples=vector.half_chip_samples, expected_header=vector.header)
    half = vector.half_chip_samples
    errors = 0
    for index, expected in enumerate(logical):
        pos = start + index * half * 2
        first, second = sum(samples[pos:pos + half]), sum(samples[pos + half:pos + half * 2])
        actual = None if first * 2 == half or second * 2 == half or (first * 2 > half) == (second * 2 > half) else int(first * 2 > half)
        errors += actual != expected
    failures = []
    if len(logical) != 321 or errors or not validation["all_flags_valid"]:
        failures.append("full_321bit_header_inverse_crc_eof_source_identity")
    if distance > marker.MAX_MARKER_DISTANCE or margin <= 0:
        failures.append("correlation_distance_or_margin")
    if check_guard and any(bit != idle for bit in samples[:start]):
        failures.append("guard_activity")
    if any(bit != idle for bit in samples[start + len(waveform):]):
        failures.append("tail_activity")
    return dict(passed=not failures, failures=failures, start_sample=start,
        logical_bit_count=len(logical), logical_bit_errors=errors, best_distance=distance,
        margin=margin, source_node=source, validation=validation)


def evaluate(status, phased_words, common_words, *, codebook, epoch, base_ns,
             mark_offset, data_offset, gap_samples, fault=0):
    """Reconstruct timing and both independent source identities from raw words."""
    failures = []
    b = (base_ns + 2) // 4
    source_phase, sink_phase = 1 + b + mark_offset, 1 + 3 * b + mark_offset + data_offset
    waveform, _ = _wave(codebook, epoch, 0)
    expected = dict(version=1, scope=6, epoch=epoch, codebook=codebook,
        sample_period_ns=4, base_ns=base_ns, base_samples=b, base_quantized_ns=4 * b,
        mark_offset=mark_offset, data_offset=data_offset, gap_samples=gap_samples, guard_samples=128,
        source_long=gap_samples, sink_long=gap_samples + 2 * b - 128,
        source_phase=source_phase, sink_phase=sink_phase,
        source_programmed_instruction=0xA042 | ((source_phase - 1) << 8),
        sink_programmed_instruction=0xA042 | ((sink_phase - 1) << 8),
        marker_samples=len(waveform), data_samples=len(waveform),
        marker_tx_words=(320 + len(waveform) + 31) // 32, data_tx_words=(64 + len(waveform) + 31) // 32,
        phased_requested=256, common_requested=2048, marker_sm=0, data_sm=1, phased_sm=2, common_sm=3,
        marker_dma=5, data_dma=6, phased_dma=4, common_dma=8,
        marker_tx_pin=26, marker_rx_pin=27, data_tx_pin=29, data_rx_pin=24,
        fault=fault, de_mask=0, extra_lease_claimed=0, transition=0, abort_mask=0, abort_pending=0,
        nominal_lag=128, marker_source_node=0, data_source_node=1, reserved0=0, reserved1=0)
    failures.extend(name for name, value in expected.items() if status.get(name) != value)
    if (not status.get("generation") or not 1 <= source_phase <= 32 or not 1 <= sink_phase <= 32 or
            not 256 <= gap_samples <= 1000 or not -10 <= mark_offset <= 10 or not -10 <= data_offset <= 10 or fault not in (0, 1, 2)):
        failures.append("request_bounds_or_generation")
    for name, words in (("phased", phased_words), ("common", common_words)):
        if (not isinstance(words, list) or any(type(word) is not int or not 0 <= word <= 0xFFFFFFFF for word in words) or
                len(words) != status.get(name + "_produced") or
                status.get(name + "_produced", -1) + status.get(name + "_remaining", -1) != KINDS[name][1]):
            failures.append(name + "_raw_count")
    terminal = dict(state=3 if fault else 2, reject_reason=4 if fault == 1 else 1 if fault == 2 else 0,
        flags=3 if fault == 1 else 15, marker_remaining=0, marker_drained=1,
        data_drained=0 if fault == 1 else 1, phased_produced=0 if fault == 1 else 256,
        common_produced=2048, edge_mask=0 if fault == 1 else 3 if fault == 2 else 15)
    if fault != 1:
        terminal["data_remaining"] = 0
    elif not 0 < status.get("data_remaining", 0) <= status.get("data_tx_words", 0):
        failures.append("waiting_data_dma_count")
    failures.extend(name for name, value in terminal.items() if status.get(name) != value)
    result = dict(passed=False, failures=failures, diagnostic_only=True, trn_accepted=False,
        production_training_certified=False, parameter_flash_saved=False,
        instruction_readback_supported=False, phase_effect_verified=False, pipeline_calibrated=False)
    if failures:
        return result
    packed = [(word >> bit) & 255 for word in common_words for bit in (0, 8, 16, 24)]
    channels = {pin: [(value >> (pin - 24)) & 1 for value in packed] for pin in (26, 27, 29, 24)}
    phased = [(word >> bit) & 1 for word in phased_words for bit in range(32)]
    edges = [next((index for index in range(1, len(channels[pin]))
                  if channels[pin][index - 1] == (1 if pin in (26, 27) else 0) and
                  channels[pin][index] == (0 if pin in (26, 27) else 1)), None)
             for pin in (26, 27, 29, 24)]
    for index, edge in enumerate(edges, 1):
        if status[f"t{index}_sample"] != (0 if edge is None else edge):
            failures.append(f"t{index}_raw_mismatch")
    if fault == 1:
        if any(bit != int(pin in (26, 27)) for pin, bits in channels.items() for bit in bits) or phased:
            failures.append("cs_idle_high_raw")
    elif fault == 2:
        if any(channels[29]) or any(channels[24]) or any(phased):
            failures.append("data_idle_low_raw")
    elif any(edge is None for edge in edges) or not 0 < edges[0] < edges[1] < edges[2] < edges[3]:
        failures.append("raw_edge_order")
    decoded = {}
    first_fall = next(i for i in range(1, len(waveform)) if waveform[i - 1] and not waveform[i])
    for index, pin in enumerate((26, 27, 29, 24)):
        if fault == 1 or (fault == 2 and pin in (29, 24)):
            continue
        edge = edges[index]
        anchor = None if edge is None else edge - (first_fall if pin in (26, 27) else 0)
        decoded[str(pin)] = _decode(channels[pin], codebook=codebook, epoch=epoch,
            source=0 if pin in (26, 27) else 1, starts=[] if anchor is None else range(anchor - 2, anchor + 3),
            idle=int(pin in (26, 27)))
        if not decoded[str(pin)]["passed"]:
            failures.append(f"common_pin{pin}_codeword")
    if not fault:
        phase_code = _decode(phased, codebook=codebook, epoch=epoch, source=1, starts=range(256), idle=0)
        result["phased_codeword"] = phase_code
        if not phase_code["passed"]:
            failures.append("phased_codeword")
        elif decoded.get("24", {}).get("passed"):
            lag = phase_code["start_sample"]
            origin = decoded["24"]["start_sample"] - lag
            result.update(best_lag_sample=lag, residual_offset_sample_count=lag - 128,
                recommended_offset_sample_count=data_offset + lag - 128,
                capture_origin_common_sample=origin, capture_origin_after_cs_sample=origin - edges[0],
                capture_origin_cs_pin=26, capture_origin_after_tx_cs_sample=origin - edges[0],
                capture_origin_after_rx_cs_sample=origin - edges[1],
                common_path_total_samples=edges[3] - edges[0],
                forward_path_samples=edges[1] - edges[0], responder_residence_samples=edges[2] - edges[1],
                return_path_samples=edges[3] - edges[2])
    result.update(passed=not failures, common_codewords=decoded, raw_edge_samples=edges)
    return result


def run_trial(exchange, args, epoch):
    result = dict(epoch=epoch, passed=False)
    request = dict(codebook=args.codebook, epoch=epoch, base_ns=args.base_ns, mark_offset=args.mark_offset,
                   data_offset=args.data_offset, gap_samples=args.gap_samples, fault=args.fault)
    result["request"] = request

    def wait(states):
        deadline = time.monotonic() + args.capture_timeout
        while time.monotonic() < deadline:
            status = parse_status(exchange(f"READ:{PREFIX}?"))
            if status["state"] in states:
                return status
            time.sleep(.02)
        raise RuntimeError("DATA state timeout")

    def stop():
        if exchange(f"{PREFIX}:STOP").strip('"') != "OK":
            raise ValueError("DATA STOP rejected")
        stopped = wait({0})
        if stopped["scope"] != 6 or any(stopped[key] != 0 for key in CLEANUP_ZERO_FIELDS):
            raise ValueError("DATA STOP left resources or foreign scope")
        return stopped

    try:
        stop()
        before = parse_configuration(exchange(f"READ:{PREFIX}:CONFIG?"))
        result["configuration"] = before
        values = list(request.values())
        response = exchange(f"{PREFIX}:START " + ",".join(map(str, values)))
        if [int(value) for value in next(csv.reader([response]), [])] != values:
            raise ValueError("DATA START echo mismatch")
        status = wait({2, 3})
        result["status"] = status
        if status["epoch"] != epoch or not status["generation"]:
            raise ValueError("DATA capture identity mismatch")
        captures = result["captures"] = {}
        for name, (kind, limit, bits) in KINDS.items():
            total = status[name + "_produced"]
            if not 0 <= total <= limit:
                raise ValueError("DATA count exceeds buffer")
            words = []
            captures[name] = dict(schema=SCHEMA, kind=kind, epoch=epoch, generation=status["generation"],
                sample_bits=bits, sample_period_ns=4, bit_order="lsb_first", gpio_base=24,
                words=words)
            while len(words) < total:
                offset, count = len(words), min(32, total - len(words))
                response = exchange(f"READ:{PREFIX}:DATA? {epoch},{kind},{offset},{count},{status['generation']}")
                words.extend(marker.parse_data(response, epoch=epoch, total=total, offset=offset, count=count))
        if parse_status(exchange(f"READ:{PREFIX}?")) != status:
            raise ValueError("DATA snapshot changed during download")
        if parse_configuration(exchange(f"READ:{PREFIX}:CONFIG?")) != before:
            raise ValueError("DATA configuration changed during acquisition")
        result.update(evaluate(status, captures["phased"]["words"], captures["common"]["words"], **request))
    except (ValueError, RuntimeError, OSError, KeyError) as exc:
        result.update(passed=False, error=f"{type(exc).__name__}: {exc}")
    finally:
        try:
            result["cleanup"] = stop()
        except (ValueError, RuntimeError, OSError) as exc:
            result.update(passed=False, cleanup_error=str(exc))
    return result


def _revalidate(trials):
    failures, rows, identities, configurations = [], [], [], []
    for trial in trials:
        try:
            status, captures, request = trial["status"], trial["captures"], trial["request"]
            identity = (trial["epoch"], status["generation"])
            if any(identity[0] == old[0] or identity[1] == old[1] for old in identities):
                raise ValueError("reused epoch or generation")
            identities.append(identity)
            if (request["epoch"] != trial["epoch"] or request["fault"] != 0 or trial.get("passed") is not True or
                    trial.get("cleanup", {}).get("scope") != 6 or
                    any(trial.get("cleanup", {}).get(key) != 0 for key in CLEANUP_ZERO_FIELDS) or
                    trial.get("error") or trial.get("cleanup_error")):
                raise ValueError("trial request/cleanup failed")
            for name, (kind, _, bits) in KINDS.items():
                expected = dict(schema=SCHEMA, kind=kind, epoch=trial["epoch"], generation=status["generation"],
                    sample_bits=bits, sample_period_ns=4, bit_order="lsb_first", gpio_base=24)
                if any(captures[name].get(key) != value for key, value in expected.items()):
                    raise ValueError("raw capture kind/identity mismatch")
            configuration = dict(board_config=trial["configuration"], **{key: request[key]
                for key in ("codebook", "base_ns", "mark_offset", "gap_samples")})
            parse_configuration(",".join(str(configuration["board_config"][key]) for key in CONFIG_FIELDS))
            evaluated = evaluate(status, captures["phased"]["words"], captures["common"]["words"], **request)
            if not evaluated["passed"]:
                raise ValueError("raw evaluation failed")
            # Unlike quantized forward/return path observations, this is the
            # fixed source residence established by the scan. Bind it also
            # for strict residual callers that do not use a window policy.
            configuration["source_residence_samples"] = evaluated["responder_residence_samples"]
            configurations.append(configuration)
            rows.append(dict(epoch=identity[0], generation=identity[1], data_offset=request["data_offset"], **evaluated))
        except (ValueError, KeyError, TypeError, IndexError) as exc:
            failures.append(f"raw_trial:{exc}")
    if not configurations or any(config != configurations[0] for config in configurations):
        failures.append("source_configuration_changed_or_missing")
    # Physical path samples are measurements, not configuration identity.
    # Forward/return quantization may vary, but the programmed source must
    # retain the same residence relative to its real RX-CS gate.
    if len({row["responder_residence_samples"] for row in rows}) != 1:
        failures.append("source_residence_changed_or_missing")
    return dict(failures=failures, rows=rows, generation_identities=[list(v) for v in identities],
                configuration=configurations[0] if configurations else None)


def evaluate_scan(trials, offsets, repeats):
    raw = _revalidate(trials)
    failures, observations = list(raw["failures"]), []
    if repeats < 1 or len(set(offsets)) < 2 or len(set(offsets)) != len(offsets):
        failures.append("scan_dimensions")
    intercepts = []
    for offset in offsets:
        rows = [row for row in raw["rows"] if row["data_offset"] == offset]
        if len(rows) != repeats:
            failures.append("scan_repeat_count")
        lags = [row["best_lag_sample"] for row in rows]
        normalized = [row["best_lag_sample"] - row["common_path_total_samples"] + offset for row in rows]
        intercepts.extend(normalized)
        observations.append(dict(data_offset=offset, lag_samples=lags,
            common_path_total_samples=[row["common_path_total_samples"] for row in rows],
            normalized_lag_plus_offset_samples=normalized))
    if len(trials) != len(offsets) * repeats or len(set(intercepts)) != 1:
        failures.append("scan_count_or_slope_not_minus_one")
    if len({row["capture_origin_after_tx_cs_sample"] - row["data_offset"] for row in raw["rows"]}) != 1:
        failures.append("common_to_phased_origin_slope_not_plus_one")
    measurements = {key: dict(sorted(Counter(row[key] for row in raw["rows"]).items()))
        for key in ("forward_path_samples", "responder_residence_samples", "return_path_samples")}
    return dict(passed=not failures, failures=failures, observations=observations,
        offsets=list(offsets), repeats=repeats, phase_effect_verified=not failures,
        phase_origin_pin=26, phase_effect_formula="lag - (t4 - t1) + data_offset = constant",
        measured_path_histograms=measurements,
        generation_identities=raw["generation_identities"], configuration=raw["configuration"],
        trn_accepted=False, production_training_certified=False)


def _strict_majority_mode(values):
    counts = Counter(values)
    if not counts:
        return None
    candidate, count = counts.most_common(1)[0]
    return candidate if count * 2 > len(values) else None


def select_offset(trials, scan, *, diagnostic_window=False):
    rescored = evaluate_scan(trials, scan.get("offsets", []), scan.get("repeats", 0))
    raw = _revalidate(trials)
    failures = list(rescored["failures"])
    if (scan.get("passed") is not True or scan.get("generation_identities") != rescored["generation_identities"] or
            scan.get("configuration") != rescored["configuration"]):
        failures.append("scan_identity_or_result")
    values = [row["recommended_offset_sample_count"] for row in raw["rows"]]
    candidates = set(values)
    selected = next(iter(candidates)) if len(candidates) == 1 else None
    disagreement = len(candidates) != 1
    if diagnostic_window:
        selected = _strict_majority_mode(values)
        if scan.get("repeats", 0) < 3:
            failures.append("diagnostic_window_requires_three_repeats")
        if not values or max(values) - min(values) > 1:
            failures.append("diagnostic_candidate_span")
        group_modes = [_strict_majority_mode([row["recommended_offset_sample_count"]
            for row in raw["rows"] if row["data_offset"] == offset]) for offset in scan.get("offsets", [])]
        if selected is None or any(mode != selected for mode in group_modes):
            failures.append("candidate_group_modes_or_majority")
    config = raw["configuration"]
    if (selected is None or not -10 <= selected <= 10 or config is None or
            not 1 <= 1 + 3 * ((config["base_ns"] + 2) // 4) + config["mark_offset"] + selected <= 32):
        failures.append("candidate_disagreement_or_unexecutable")
    support = sorted({value - selected for value in values}) if selected is not None else []
    residences = {row["responder_residence_samples"] for row in raw["rows"]}
    intercepts = {row["capture_origin_after_tx_cs_sample"] - row["data_offset"] for row in raw["rows"]}
    policy_id = DIAGNOSTIC_WINDOW_POLICY_ID if diagnostic_window else STRICT_POLICY_ID
    policy = dict(policy_id=policy_id, source=POLICY_SOURCE, residual_support=support,
        selected_offset=selected, source_residence_samples=next(iter(residences)) if len(residences) == 1 else None,
        phase_origin_intercept_samples=next(iter(intercepts)) if len(intercepts) == 1 else None,
        scan_generation_identities=raw["generation_identities"], configuration=config)
    return dict(passed=not failures, failures=failures, selected_offset=selected if not failures else None,
        scan_generation_identities=raw["generation_identities"], configuration=config,
        diagnostic_candidate_allowed=not failures, strict_fixed_offset_verified=not failures and not disagreement,
        candidate_disagreement=disagreement, candidate_histogram=dict(sorted(Counter(values).items())),
        policy_id=policy_id, policy_source=POLICY_SOURCE, residual_policy=policy if not failures else None,
        trn_accepted=False, production_training_certified=False, parameter_flash_saved=False)


def evaluate_residual(trials, selected_offset, repeats, *, scan_generation_identities=None, expected_configuration=None,
                      diagnostic_window=False, residual_policy=None, scan_trials=None, scan=None):
    raw = _revalidate(trials)
    failures = list(raw["failures"])
    old = scan_generation_identities or []
    if not old or expected_configuration is None or raw["configuration"] != expected_configuration:
        failures.append("scan_metadata_missing_or_changed")
    if any(epoch == prior[0] or generation == prior[1] for epoch, generation in raw["generation_identities"] for prior in old):
        failures.append("scan_capture_reused")
    if (type(selected_offset) is not int or not -10 <= selected_offset <= 10 or repeats < 1 or
            len(trials) != repeats or len(raw["rows"]) != repeats):
        failures.append("residual_dimensions")
    if any(row["data_offset"] != selected_offset for row in raw["rows"]):
        failures.append("selected_offset_not_loaded")
    values = [row["residual_offset_sample_count"] for row in raw["rows"]]
    strict_zero = bool(values) and all(value == 0 for value in values)
    if diagnostic_window:
        if repeats < 3:
            failures.append("diagnostic_window_requires_three_repeats")
        try:
            policy = residual_policy
            if scan_trials is None or scan is None:
                raise ValueError("original raw scan required")
            rescored_selection = select_offset(scan_trials, scan, diagnostic_window=True)
            if not rescored_selection["passed"] or policy != rescored_selection["residual_policy"]:
                raise ValueError("residual policy does not match revalidated raw scan")
            support = policy["residual_support"]
            if (policy["policy_id"] != DIAGNOSTIC_WINDOW_POLICY_ID or policy["selected_offset"] != selected_offset or
                    policy["scan_generation_identities"] != scan_generation_identities or policy["configuration"] != expected_configuration or
                    not isinstance(support, list) or any(type(value) is not int for value in support) or
                    support != sorted(set(support)) or 0 not in support or max(support) - min(support) > 1):
                raise ValueError("unbound or invalid diagnostic residual policy")
            if (not values or any(value not in support for value in values) or max(values) - min(values) > 1 or
                    _strict_majority_mode(values) != 0):
                failures.append("residual_support_span_or_zero_majority")
            if any(row["responder_residence_samples"] != policy["source_residence_samples"] or
                   row["capture_origin_after_tx_cs_sample"] - row["data_offset"] != policy["phase_origin_intercept_samples"]
                   for row in raw["rows"]):
                failures.append("scan_source_residence_or_phase_origin_changed")
        except (ValueError, KeyError, TypeError) as exc:
            failures.append(f"diagnostic_residual_policy:{exc}")
    elif not strict_zero:
        failures.append("residual_nonzero")
    return dict(passed=not failures, failures=failures,
        zero_residual_verified=not failures and strict_zero,
        strict_zero_residual_verified=not failures and strict_zero,
        bounded_residual_verified=not failures,
        residual_histogram=dict(sorted(Counter(values).items())),
        policy_id=DIAGNOSTIC_WINDOW_POLICY_ID if diagnostic_window else STRICT_POLICY_ID,
        diagnostic_candidate_allowed=not failures,
        observations=raw["rows"], generation_identities=raw["generation_identities"],
        configuration=raw["configuration"], trn_accepted=False, production_training_certified=False,
        pipeline_calibrated=False, parameter_flash_saved=False)
