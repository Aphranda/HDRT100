#!/usr/bin/env python3
"""Verify actual destination PIO sampling phase on two single-board paths."""
from __future__ import annotations

import argparse
import csv
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from calibration_ring_validate import calibration_marker_loopback as marker  # noqa: E402

FIELDS = (
    "version", "state", "scope", "flags", "reject_reason", "epoch", "generation",
    "codebook", "direction", "sample_period_ns", "base_ns", "base_samples",
    "base_quantized_ns", "offset_samples", "effective_samples", "source_pipeline_cycles",
    "destination_cycles", "programmed_delay_instruction", "programmed_delay_cycles", "tx_pin",
    "rx_pin", "code_samples", "tx_words", "tx_remaining", "tx_drained", "requested_words",
    "produced_words", "rx_remaining", "origin_lead_samples", "origin_high_samples",
    "launch_guard_samples", "fault",
)
PREFIX = "CAL:SCK:RXPHASE"
STATUS_QUERY = f"READ:{PREFIX}?"
PINS = {0: (25, 28), 1: (29, 24)}
CAPTURE_WORDS = 256
NOP_INSTRUCTION = 0xA042  # RP2350 PIO MOV Y,Y, the SDK pio_encode_nop().


@dataclass(frozen=True)
class PhaseProtocol:
    signal: str
    scope: int
    pins: tuple[tuple[int, int], ...]
    idle_level: int

    @property
    def prefix(self):
        return f"CAL:{self.signal}:RXPHASE"

    @property
    def raw_schema(self):
        return "HAOFV_RXPHASE_RAW_V1" if self.signal == "SCK" else "HAOFV_MARK_RXPHASE_RAW_V1"

    @property
    def origin_width_field(self):
        return "origin_low_samples" if self.idle_level else "origin_high_samples"

    @property
    def fields(self):
        return tuple(self.origin_width_field if name == "origin_high_samples" else name for name in FIELDS)


SCK_PROTOCOL = PhaseProtocol("SCK", 4, ((25, 28), (29, 24)), 0)
MARK_PROTOCOL = PhaseProtocol("MARK", 5, ((26, 27),), 1)


def parse_status(raw, *, protocol=SCK_PROTOCOL):
    values = [int(value.strip(), 0) for value in next(csv.reader([raw]), [])]
    if len(values) != len(FIELDS) or values[0] != 2:
        raise ValueError("RXPHASE status version/width mismatch")
    if any(not (-10 <= value <= 10 if name == "offset_samples" else
                0 <= value <= 0xFFFFFFFF) for name, value in zip(protocol.fields, values)):
        raise ValueError("RXPHASE status field outside range")
    return dict(zip(protocol.fields, values))


def decode_codeword(words, *, codebook, epoch, direction, protocol=SCK_PROTOCOL):
    waveform, vector = marker.marker_raw_waveform(
        codebook_id=codebook, epoch=epoch, source_node=0)
    logical, _, _ = marker.marker_logical_bits(
        codebook_id=codebook, epoch=epoch, source_node=0)
    samples = [(word >> bit) & 1 for word in words for bit in range(32)]
    candidates = []
    for lag in range(min(256, len(samples) - len(waveform) + 1)):
        distance = sum(a != b for a, b in zip(waveform, samples[lag:lag + len(waveform)]))
        candidates.append((distance, lag))
    if not candidates:
        return dict(passed=False, failures=["full_codeword_truncated"])
    candidates.sort()
    distance, lag = candidates[0]
    margin = candidates[1][0] - distance if len(candidates) > 1 else 0
    validation = marker.validate_marker_window(samples, lag,
        half_chip_samples=vector.half_chip_samples, expected_header=vector.header)
    half = vector.half_chip_samples
    decoded = []
    for index in range(len(logical)):
        start = lag + index * half * 2
        first, second = sum(samples[start:start + half]), sum(samples[start + half:start + half * 2])
        decoded.append(None if first * 2 == half or second * 2 == half or
                       (first * 2 > half) == (second * 2 > half) else int(first * 2 > half))
    errors = sum(actual != expected for actual, expected in zip(decoded, logical))
    failures = []
    if len(logical) != 321 or errors or not validation["all_flags_valid"]:
        failures.append("full_321bit_header_inverse_crc_eof")
    if distance > marker.MAX_MARKER_DISTANCE or margin <= 0:
        failures.append("correlation_distance_or_margin")
    if any(sample != protocol.idle_level for sample in samples[:lag]):
        failures.append("launch_guard_activity")
    end = lag + len(waveform)
    if end + 64 > len(samples) or any(sample != protocol.idle_level for sample in samples[end:]):
        failures.append("tail_truncated_or_activity")
    return dict(passed=not failures, failures=failures, lag_samples=lag,
                best_distance=distance, margin=margin, logical_bit_count=len(logical),
                logical_bit_errors=errors, validation=validation, source_node=0,
                raw_sample_count=len(samples))


def evaluate(status, words, *, codebook, epoch, base_ns, offset_samples, direction,
             fault_idle_low=False, protocol=SCK_PROTOCOL):
    waveform, _ = marker.marker_raw_waveform(codebook_id=codebook, epoch=epoch,
                                             source_node=0)
    base_samples = (base_ns + 2) // 4
    effective = base_samples + offset_samples
    if direction not in range(len(protocol.pins)):
        return dict(passed=False, failures=["direction"], phase_loaded=False,
                    phase_effect_verified=False, trn_accepted=False)
    tx_pin, rx_pin = protocol.pins[direction]
    expected = dict(version=2, scope=protocol.scope, epoch=epoch, codebook=codebook, direction=direction,
        sample_period_ns=4, base_ns=base_ns, base_samples=base_samples,
        base_quantized_ns=base_samples * 4, offset_samples=offset_samples,
        effective_samples=effective, source_pipeline_cycles=1, destination_cycles=effective + 1,
        programmed_delay_instruction=NOP_INSTRUCTION | effective << 8,
        programmed_delay_cycles=effective + 1, tx_pin=tx_pin, rx_pin=rx_pin,
        code_samples=len(waveform), tx_words=(320 + len(waveform) + 31) // 32,
        requested_words=CAPTURE_WORDS, origin_lead_samples=64,
        launch_guard_samples=128, fault=int(fault_idle_low))
    expected[protocol.origin_width_field] = 64
    failures = [name for name, value in expected.items() if status.get(name) != value]
    if not status.get("generation") or not 0 <= effective <= 31:
        failures.append("generation_or_effective_phase")
    if (not isinstance(words, list) or any(type(word) is not int or
            not 0 <= word <= 0xFFFFFFFF for word in words) or
            len(words) != status.get("produced_words")):
        failures.append("raw_words")
    if status.get("produced_words", -1) + status.get("rx_remaining", -1) != CAPTURE_WORDS:
        failures.append("rx_dma_count")
    code = None
    if fault_idle_low:
        if (status.get("state") != 3 or status.get("reject_reason") != 4 or
                status.get("produced_words") != 0 or words or status.get("flags") != 11 or
                status.get("tx_remaining") != 0 or status.get("tx_drained") != 1):
            failures.append("expected_idle_high_timeout" if protocol.idle_level else "expected_idle_low_timeout")
    else:
        for name, value in dict(state=2, flags=15, reject_reason=0, tx_remaining=0,
                                tx_drained=1, produced_words=CAPTURE_WORDS, rx_remaining=0).items():
            if status.get(name) != value:
                failures.append(name)
        if not failures:
            code = decode_codeword(words, codebook=codebook, epoch=epoch, direction=direction, protocol=protocol)
            failures.extend(code["failures"])
    nominal_lag = 128 - base_samples
    best_lag = code.get("lag_samples") if code else None
    residual = best_lag - nominal_lag if best_lag is not None else None
    return dict(passed=not failures, failures=failures, codeword=code,
                best_lag_sample=best_lag, nominal_lag_sample=nominal_lag,
                residual_offset_sample_count=residual,
                recommended_offset_sample_count=(offset_samples + residual if residual is not None else None),
                pipeline_calibrated=False,
                instruction_readback_supported=False, phase_effect_verified=False,
                signal=protocol.signal, origin_pulse_level=1 - protocol.idle_level,
                phase_loaded=not any(name in failures for name in expected) and
                    "generation_or_effective_phase" not in failures,
                diagnostic_only=True, trn_accepted=False, production_training_certified=False,
                requested_base_ns=base_ns, quantized_base_ns=base_samples * 4)


def run_trial(exchange, args, epoch, direction=None, offset_samples=None, *, protocol=SCK_PROTOCOL):
    direction = args.direction if direction is None else direction
    offset_samples = args.offset_samples if offset_samples is None else offset_samples
    result = dict(epoch=epoch, direction=direction, offset_samples=offset_samples, passed=False)

    def wait(states):
        deadline = time.monotonic() + args.capture_timeout
        while time.monotonic() < deadline:
            status = parse_status(exchange(f"READ:{protocol.prefix}?"), protocol=protocol)
            if status["state"] in states:
                return status
            time.sleep(.02)
        raise RuntimeError("RXPHASE state timeout")

    def stop():
        if exchange(f"{protocol.prefix}:STOP").strip('"') != "OK":
            raise ValueError("RXPHASE STOP rejected")
        return wait({0})

    try:
        stop()
        request = [args.codebook, epoch, CAPTURE_WORDS, args.base_ns, offset_samples,
                   direction, int(args.fault_idle_low)]
        response = exchange(f"{protocol.prefix}:START " + ",".join(map(str, request)))
        if [int(value) for value in next(csv.reader([response]), [])] != request:
            raise ValueError("RXPHASE START echo mismatch")
        status = wait({2, 3})
        result["status"] = status
        if status["epoch"] != epoch or not status["generation"] or not 0 <= status["produced_words"] <= CAPTURE_WORDS:
            raise ValueError("RXPHASE snapshot identity/count mismatch")
        words = []
        while len(words) < status["produced_words"]:
            offset, count = len(words), min(32, status["produced_words"] - len(words))
            raw = exchange(f"READ:{protocol.prefix}:DATA? {epoch},{offset},{count},{status['generation']}")
            words.extend(marker.parse_data(raw, epoch=epoch, total=status["produced_words"],
                                            offset=offset, count=count))
        if parse_status(exchange(f"READ:{protocol.prefix}?"), protocol=protocol) != status:
            raise ValueError("RXPHASE generation/snapshot changed during download")
        result["capture"] = dict(schema=protocol.raw_schema, epoch=epoch,
            generation=status["generation"], sample_bits=1, sample_period_ns=4,
            rx_pin=status["rx_pin"], bit_order="lsb_first", words=words)
        result.update(evaluate(status, words, codebook=args.codebook, epoch=epoch,
            base_ns=args.base_ns, offset_samples=offset_samples, direction=direction,
            fault_idle_low=args.fault_idle_low, protocol=protocol))
    except (ValueError, RuntimeError, OSError) as exc:
        result.update(passed=False, error=f"{type(exc).__name__}: {exc}")
    finally:
        try:
            result["cleanup"] = stop()
        except (ValueError, RuntimeError, OSError) as exc:
            result.update(passed=False, cleanup_error=str(exc))
    return result


def revalidate_trials(trials, *, protocol=SCK_PROTOCOL):
    """One raw-evidence gate shared by scan, selection and fresh residual runs."""
    failures, identities = [], set()
    revalidated = []
    configurations = set()
    for trial in trials:
        identity = (trial.get("epoch"), trial.get("status", {}).get("generation"))
        if identity in identities:
            failures.append("duplicate_capture_identity")
        identities.add(identity)
        try:
            status, capture = trial["status"], trial["capture"]
            configurations.add(tuple(status[name] for name in
                ("codebook", "base_ns", "sample_period_ns", "origin_lead_samples",
                 protocol.origin_width_field, "launch_guard_samples", "tx_words", "scope")))
            if (capture["generation"] != status["generation"] or capture["epoch"] != trial["epoch"] or
                    capture.get("schema") != protocol.raw_schema or
                    capture.get("sample_bits") != 1 or capture.get("sample_period_ns") != 4 or
                    capture.get("rx_pin") != status["rx_pin"] or capture.get("bit_order") != "lsb_first" or
                    trial.get("cleanup", {}).get("state") != 0 or trial.get("error") or trial.get("cleanup_error")):
                raise ValueError("raw capture identity/cleanup mismatch")
            evaluated = evaluate(status, capture["words"], codebook=status["codebook"],
                epoch=trial["epoch"], base_ns=status["base_ns"], offset_samples=trial["offset_samples"],
                direction=trial["direction"], protocol=protocol)
            if not evaluated["passed"] or not trial.get("passed"):
                raise ValueError("raw revalidation failed")
            revalidated.append({**trial, **evaluated})
        except (ValueError, KeyError, TypeError) as exc:
            failures.append(f"trial{trial.get('epoch')}_raw:{exc}")
    if len(configurations) != 1:
        failures.append("scan_source_configuration_changed")
    return dict(failures=failures, trials=revalidated, identities=sorted(identities),
                configuration=list(next(iter(configurations))) if len(configurations) == 1 else None)


def scan_from_raw(raw, trial_count, directions, offsets, repeats, *, protocol=SCK_PROTOCOL):
    failures, rows = list(raw["failures"]), []
    revalidated = raw["trials"]
    if (not directions or len(set(directions)) != len(directions) or
            any(direction not in range(len(protocol.pins)) for direction in directions) or repeats < 1 or
            len(set(offsets)) != len(offsets)):
        failures.append("invalid_scan_dimensions")
    for direction in directions:
        intercepts = []
        for offset in offsets:
            selected = [trial for trial in revalidated if trial.get("direction") == direction and
                        trial.get("offset_samples") == offset]
            if len(selected) != repeats or any(not trial.get("passed") or not trial.get("phase_loaded") or
                not (trial.get("codeword") or {}).get("passed") for trial in selected):
                failures.append(f"direction{direction}_offset{offset}_incomplete")
                continue
            lags = [trial["codeword"]["lag_samples"] for trial in selected]
            intercepts.extend(lag + offset for lag in lags)
            rows.append(dict(direction=direction, offset_samples=offset, lag_samples=lags))
        if len(set(offsets)) < 2 or not intercepts or len(set(intercepts)) != 1:
            failures.append(f"direction{direction}_lag_offset_slope_not_minus_one")
    if trial_count != len(directions) * len(offsets) * repeats:
        failures.append("trial_count")
    return dict(passed=not failures, failures=failures, observations=rows,
                phase_effect_verified=not failures, instruction_readback_supported=False,
                required_lag_per_offset_slope=-1, trn_accepted=False,
                directions=list(directions), offsets=list(offsets), repeats=repeats,
                generation_identities=raw["identities"], configuration=raw["configuration"])


def evaluate_scan(trials, directions, offsets, repeats, *, protocol=SCK_PROTOCOL):
    return scan_from_raw(revalidate_trials(trials, protocol=protocol), len(trials), directions, offsets, repeats,
                         protocol=protocol)


def select_offsets(trials, scan_result, *, protocol=SCK_PROTOCOL):
    raw = revalidate_trials(trials, protocol=protocol)
    failures = list(raw["failures"])
    selected, observations = {}, []
    try:
        rescored = scan_from_raw(raw, len(trials), scan_result["directions"],
                                scan_result["offsets"], scan_result["repeats"], protocol=protocol)
        if scan_result.get("passed") is not True or not rescored["passed"]:
            failures.append("scan_not_passed")
        if (scan_result.get("generation_identities") != rescored["generation_identities"] or
                scan_result.get("configuration") != rescored["configuration"]):
            failures.append("scan_identity_mismatch")
        for direction in scan_result["directions"]:
            rows = [trial for trial in raw["trials"] if trial["direction"] == direction]
            candidates = {trial["recommended_offset_sample_count"] for trial in rows}
            if len(candidates) != 1:
                failures.append(f"direction{direction}_candidate_disagreement")
                continue
            candidate = next(iter(candidates))
            if (not -10 <= candidate <= 10 or any(not 0 <= trial["status"]["base_samples"] +
                                                   candidate <= 31 for trial in rows)):
                failures.append(f"direction{direction}_candidate_outside_executable_range")
                continue
            selected[str(direction)] = candidate
            observations.append(dict(direction=direction, candidate_offset_samples=candidate,
                raw_candidates=[trial["recommended_offset_sample_count"] for trial in rows]))
    except (ValueError, KeyError, TypeError) as exc:
        failures.append(f"scan_metadata:{exc}")
    return dict(passed=not failures, failures=failures,
        selected_by_direction=selected if not failures else {}, observations=observations,
        scan_generation_identities=raw["identities"], configuration=raw["configuration"],
        nominal_lag_formula="launch_guard_samples - base_samples",
        pipeline_model_source="calibration_training_sck.c:calibration_training_sck_evaluate_core1",
        pipeline_calibrated=False, diagnostic_only=True, trn_accepted=False,
        production_training_certified=False, parameter_flash_saved=False)


def evaluate_residual(trials, selected_by_direction, repeats, *,
                      scan_generation_identities=None, expected_configuration=None, protocol=SCK_PROTOCOL):
    raw = revalidate_trials(trials, protocol=protocol)
    failures = list(raw["failures"])
    observations = []
    try:
        selected = {int(direction): offset for direction, offset in selected_by_direction.items()}
        if (not selected or any(direction not in range(len(protocol.pins)) or type(offset) is not int or
                not -10 <= offset <= 10 for direction, offset in selected.items()) or repeats < 1):
            raise ValueError("invalid selected directions/offsets/repeats")
        if not scan_generation_identities:
            raise ValueError("scan generation blacklist required")
        blacklist = {tuple(identity) for identity in scan_generation_identities}
        old_epochs = {identity[0] for identity in blacklist}
        old_generations = {identity[1] for identity in blacklist}
        if any(epoch in old_epochs or generation in old_generations for epoch, generation in raw["identities"]):
            failures.append("scan_capture_reused")
        if expected_configuration is None or list(expected_configuration) != raw["configuration"]:
            failures.append("scan_configuration_missing_or_changed")
        for direction, offset in selected.items():
            rows = [trial for trial in raw["trials"] if trial["direction"] == direction]
            if len(rows) != repeats:
                failures.append(f"direction{direction}_repeat_count")
            for trial in rows:
                if trial["offset_samples"] != offset or trial["status"]["offset_samples"] != offset:
                    failures.append(f"direction{direction}_selected_offset_not_loaded")
                if trial["residual_offset_sample_count"] != 0:
                    failures.append(f"direction{direction}_residual_nonzero")
                observations.append(dict(direction=direction, epoch=trial["epoch"],
                    generation=trial["status"]["generation"], loaded_offset_samples=trial["offset_samples"],
                    residual_offset_samples=trial["residual_offset_sample_count"],
                    best_lag_sample=trial["best_lag_sample"]))
        if len(trials) != len(selected) * repeats or any(trial["direction"] not in selected for trial in raw["trials"]):
            failures.append("residual_trial_count_or_direction")
    except (ValueError, KeyError, TypeError, IndexError) as exc:
        failures.append(f"residual_identity:{exc}")
    return dict(passed=not failures, failures=failures, observations=observations,
        generation_identities=raw["identities"], configuration=raw["configuration"],
        zero_residual_verified=not failures, pipeline_calibrated=False,
        pipeline_model_source="calibration_training_sck.c:calibration_training_sck_evaluate_core1",
        diagnostic_only=True, trn_accepted=False, production_training_certified=False,
        parameter_flash_saved=False)


def run(args, *, protocol=SCK_PROTOCOL):
    args.out_dir.mkdir(parents=True, exist_ok=False)
    output = dict(schema="HAOFV_RXPHASE_DIAGNOSTIC_V1", passed=False, diagnostic_only=True,
        phase_loaded=False, trn_accepted=False, production_training_certified=False,
        parameter_flash_saved=False, prior_evidence_bound=False, base_source="CLI_DIAGNOSTIC",
        physical_board_count=1, logical_slot_count=2, trials=[], transcript=[])
    output.update(base_ns=args.base_ns, offsets=args.offset_samples, directions=args.direction,
                  codebook=args.codebook, fault_idle_low=args.fault_idle_low)
    if protocol.signal == "MARK":
        output["schema"] = "HAOFV_MARK_RXPHASE_DIAGNOSTIC_V1"
        output["fault_idle_high"] = output.pop("fault_idle_low")
    output.update(signal=protocol.signal, origin_pulse_level=1 - protocol.idle_level)
    args.board_ids, args.keep_open, args.action_timeout = [args.board_id], True, args.timeout
    try:
        board = marker.discover(args).get(args.board_id)
        if board is None or board.build != args.expected_build:
            raise ValueError("target UID/build not found")
        output["board"] = asdict(board)

        def exchange(command):
            response = marker.board_command(board, command, args)
            output["transcript"].append(dict(command=command, response=response, monotonic=time.monotonic()))
            if response == "<timeout>":
                raise RuntimeError(f"SCPI timeout: {command}")
            return response

        if exchange("*IDN?") != board.idn or exchange("SYST:FW:BUILD?").strip('"') != args.expected_build:
            raise ValueError("live identity changed before mutation")
        exchange("SYST:TDMA:RING:STOP")
        deadline = time.monotonic() + args.capture_timeout
        while True:
            ring = marker.parse_status_named(exchange("SYST:REFMEM:SYNC:TDMA:STAT?"))
            if not ring["ring_up_running"] and not ring["ring_down_running"]:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("ring STOP not confirmed")
            time.sleep(.02)
        for direction in args.direction:
            for offset in args.offset_samples:
                for _ in range(args.repeats):
                    epoch = args.epoch + len(output["trials"])
                    trial = run_trial(exchange, args, epoch, direction, offset, protocol=protocol)
                    output["trials"].append(trial)
                    (args.out_dir / f"trial-{epoch}.json").write_text(json.dumps(trial, indent=2) + "\n", encoding="utf-8")
                    if trial.get("cleanup_error"):
                        raise RuntimeError("cleanup failed")
        output["final_idn"] = exchange("*IDN?")
        output["final_build"] = exchange("SYST:FW:BUILD?").strip('"')
        output["final_error"] = exchange("SYST:ERR?")
        identity_ok = (output["final_idn"] == board.idn and output["final_build"] == args.expected_build and
                       output["final_error"].startswith("0,"))
        output["scan"] = evaluate_scan(output["trials"], args.direction, args.offset_samples, args.repeats,
                                       protocol=protocol)
        output["phase_loaded"] = bool(output["trials"]) and all(t.get("phase_loaded") for t in output["trials"])
        output["passed"] = identity_ok and (all(t["passed"] for t in output["trials"])
            if args.fault_idle_low else output["scan"]["passed"])
    except (ValueError, RuntimeError, OSError) as exc:
        output["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        marker.close_persistent_connections()
        (args.out_dir / "summary.json").write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    return output


def main(protocol=SCK_PROTOCOL):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--expected-build", required=True)
    parser.add_argument("--base-ns", type=int, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--direction", type=int, choices=range(len(protocol.pins)), action="append")
    parser.add_argument("--offset-samples", type=int, action="append")
    parser.add_argument("--codebook", type=int, choices=range(4), default=1)
    parser.add_argument("--epoch", type=int, default=1)
    parser.add_argument("--repeats", type=int, default=3)
    # The wire fault bit selects the protocol's idle level; retain the shared
    # internal argument name for compatibility with SCK acquisition callers.
    parser.add_argument("--fault-idle-high" if protocol.idle_level else "--fault-idle-low",
                        dest="fault_idle_low", action="store_true")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.)
    parser.add_argument("--capture-timeout", type=float, default=5.)
    parser.add_argument("--settle", type=float, default=.05)
    args = parser.parse_args()
    args.direction = args.direction or list(range(len(protocol.pins)))
    args.offset_samples = args.offset_samples or [-1, 0, 1]
    count = len(args.direction) * len(args.offset_samples) * args.repeats
    if (not 1 <= args.repeats <= 100 or not 1 <= args.epoch <= args.epoch + count - 1 <= 255 or
            not 1 <= args.base_ns <= 0xFFFFFFFF or len(set(args.direction)) != len(args.direction) or
            len(set(args.offset_samples)) != len(args.offset_samples) or
            any(not -10 <= offset <= 10 or not 0 <= (args.base_ns + 2) // 4 + offset <= 31
                for offset in args.offset_samples) or
            (not args.fault_idle_low and len(args.offset_samples) < 2) or
            any(not math.isfinite(value) or value <= 0 for value in (args.timeout, args.capture_timeout)) or
            not math.isfinite(args.settle) or args.settle < 0):
        parser.error("invalid phase/scan/timeout bounds")
    result = run(args, protocol=protocol)
    print(f"passed={result['passed']} phase_loaded={result['phase_loaded']} trn_accepted=False out_dir={args.out_dir}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
