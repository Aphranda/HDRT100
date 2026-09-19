#!/usr/bin/env python3
"""Offline timing statistics from a finite position validation report.

The source frequency is declared, not independently measured. Legacy software
ticks are normalized to the declared position period. Explicit TIMER1 analysis
requires a matching firmware build and preserves hardware milliseconds.
LINK timings are sampled last-message snapshots, not complete message traces.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = sorted(values)
    if not values:
        return {"count": 0}
    return dict(count=len(values), min=values[0], mean=statistics.mean(values),
                p50=statistics.median(values), p95=values[math.ceil(.95 * len(values)) - 1],
                max=values[-1])


def delta(end, start):
    return (end - start) & 0xffffffff


TIMING_BOUNDARIES = ("request", "applied", "offered", "returned", "fire_queued", "done")
TIMING_SEGMENTS = ("request_to_applied", "applied_to_offer", "offer_to_return",
                   "return_to_fire_queue", "fire_queue_to_done")


def detailed_timing(records, positions):
    """Use same-position TIMER1 offsets; never join separate clock origins."""
    rows, position_rows = [], []
    clock_hz = records[0]["timing"]["clock_hz"]
    if not isinstance(clock_hz, int) or clock_hz <= 0 or 1000000000 % clock_hz:
        raise ValueError("timing clock must have an exact integer nanosecond period")
    tick_ns = 1000000000 // clock_hz
    identity = tuple(records[0][k] for k in ("run", "generation", "binding_epoch"))
    for position in range(1, positions + 1):
        group = records[(position - 1) * 8:position * 8]
        previous_done = 0
        totals = {name: 0 for name in (*TIMING_SEGMENTS, "before_request")}
        for index, record in enumerate(group):
            timing = record["timing"]
            if (timing["version"] != 1 or timing["flags"] != 63 or
                timing["clock_hz"] != clock_hz or record["position"] != position or
                record["sequence_index"] != index or
                tuple(record[k] for k in ("run", "generation", "binding_epoch")) != identity or
                any(timing[k] != record[k] for k in ("ordinal", "run", "generation", "binding_epoch",
                                                   "exchange_id", "position", "sequence_index"))):
                raise ValueError("timing identity, clock or completeness mismatch")
            ticks = [timing[name + "_ticks"] for name in TIMING_BOUNDARIES]
            if (any(not isinstance(v, int) or not 0 <= v <= 0xffffffffffffffff for v in ticks) or
                ticks != sorted(ticks) or ticks[0] < previous_done or
                ticks[-1] * 1000 // clock_hz != record["cycle_elapsed_ms"]):
                raise ValueError("timing boundaries or history duration mismatch")
            durations = dict(zip(TIMING_SEGMENTS, (b - a for a, b in zip(ticks, ticks[1:]))))
            durations["before_request"] = ticks[0] - previous_done
            for name, value in durations.items():
                totals[name] += value
            row = dict(position=position, state=index, ordinal=record["ordinal"],
                       threshold_pulses=record["threshold_pulses"],
                       observed_pulses=record["observed_pulses"],
                       ticks=dict(zip(TIMING_BOUNDARIES, ticks)),
                       segments_ns={name: value * tick_ns for name, value in durations.items()},
                       completed_offset_ns=ticks[-1] * tick_ns,
                       previous_done_to_fire_queue_ns=(ticks[4] - previous_done) * tick_ns if index else None)
            rows.append(row)
            previous_done = ticks[-1]
        assert sum(totals.values()) == previous_done
        position_rows.append(dict(position=position, total_ns=previous_done * tick_ns,
            segments_ns={name: value * tick_ns for name, value in totals.items()},
            state_intervals_ns=[sum(row["segments_ns"].values()) for row in rows[-8:]],
            wait_to_next_position_ms=(delta(records[position * 8]["position_admitted_tick_ms"],
                                          group[-1]["sample_done_tick_ms"]) if position < positions else None)))
    return dict(clock_hz=clock_hz, tick_ns=tick_ns, positions=position_rows, states=rows,
                segment_stats_ns={name: stats([r["segments_ns"][name] for r in rows]) for name in totals},
                previous_done_to_fire_queue_ns=stats([r["previous_done_to_fire_queue_ns"] for r in rows
                    if r["previous_done_to_fire_queue_ns"] is not None]),
                boundaries={"request": "LINK accepted state request (first state may already be primed)",
                    "applied": "LINK observed code/settle completion from IO owner",
                    "offered": "LINK fragment offered to RefMem transport, not physical TX edge",
                    "returned": "validated physical RJ45 return delivered to LINK inbox",
                    "fire_queued": "gateway FIRE command accepted into owner mailbox, not OUT edge",
                    "done": "LINK observed READY and output no longer busy, not READY edge"})


def write_timing_csv(path, result):
    detail = result.get("detailed_timing")
    if detail is None:
        raise ValueError("CSV requires a complete --timing-evidence hardware report")
    path.parent.mkdir(parents=True, exist_ok=True)
    names = ("position", "state", "ordinal", "threshold_pulses", "observed_pulses")
    segments = (*TIMING_SEGMENTS, "before_request")
    columns = ["build", "observation_mode", "clock_hz", "tick_ns", *names, *[k + "_ticks" for k in TIMING_BOUNDARIES],
               *[k + "_us" for k in segments], "completed_offset_us", "previous_done_to_fire_queue_us"]
    with path.open("x", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for row in detail["states"]:
            values = {key: row[key] for key in names}
            values.update(build=result["firmware_build"], clock_hz=detail["clock_hz"], tick_ns=detail["tick_ns"])
            values["observation_mode"] = result["observation_mode"]
            values.update({key + "_ticks": value for key, value in row["ticks"].items()})
            values.update({key + "_us": value / 1000 for key, value in row["segments_ns"].items()})
            values["completed_offset_us"] = row["completed_offset_ns"] / 1000
            gap = row["previous_done_to_fire_queue_ns"]
            values["previous_done_to_fire_queue_us"] = None if gap is None else gap / 1000
            writer.writerow(values)


def write_timing_markdown(path, result):
    detail = result.get("detailed_timing")
    if detail is None:
        raise ValueError("Markdown requires a complete --timing-evidence hardware report")
    lines = ["# Position Timing Breakdown", "",
             f"Build: `{result['firmware_build']}`. Source SHA-256: `{result['source_sha256']}`.", "",
             f"TIMER1: {detail['clock_hz']} Hz, {detail['tick_ns']} ns per raw tick.", "",
             f"Observation mode: `{result['observation_mode']}`.",
             "RAM timestamp recording still adds work; this is not a zero-overhead measurement.", "",
             "All marks are Core1 observations or submissions, not physical IO edges.",
             "The first state of the first position was primed by START.", "",
             "## Per-State Stages", "", "| Stage | Mean us | P95 us | Max us |",
             "| --- | ---: | ---: | ---: |"]
    for name, values in detail["segment_stats_ns"].items():
        lines.append(f"| {name} | {values['mean'] / 1000:.3f} | {values['p95'] / 1000:.3f} | {values['max'] / 1000:.3f} |")
    lines += ["", "## Per-Position Totals", "",
              "Stage totals include all eight states; durations below are milliseconds.",
              "Waiting is from the last completion to the next position admission, rounded to milliseconds.", "",
              "| Position | Request/applied | Applied/offer | Offer/return | Return/fire queue | Fire queue/done | Before requests | Total | Wait |",
              "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in detail["positions"]:
        durations = [row["segments_ns"][name] for name in (*TIMING_SEGMENTS, "before_request")]
        cells = [str(row["position"]), *[f"{value / 1000000:.6f}" for value in durations],
                 f"{row['total_ns'] / 1000000:.6f}",
                 "N/A" if row["wait_to_next_position_ms"] is None else str(row["wait_to_next_position_ms"])]
        lines.append("| " + " | ".join(cells) + " |")
    lines += ["", "## Each State Within a Position", "",
              "S0 starts at position admission; later intervals start at the previous observed completion. Units: ms.", "",
              "| Position | S0 | S1 | S2 | S3 | S4 | S5 | S6 | S7 |",
              "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in detail["positions"]:
        lines.append("| " + " | ".join([str(row["position"]),
            *[f"{value / 1000000:.6f}" for value in row["state_intervals_ns"]]]) + " |")
    lines += ["", "## Boundaries", ""]
    lines += [f"- `{name}`: {description}." for name, description in detail["boundaries"].items()]
    lines += ["", "The previous-done to FIRE-queued interval excludes physical edge timing and is not the READY-to-trigger acceptance metric."]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        stream.write("\n".join(lines) + "\n")


def analyze(report, *, clock_source="software-tick", timer1_build=None):
    profile = report["two_positions"]
    records = profile["history"]
    settings = report["settings"]
    if clock_source not in ("software-tick", "timer1"):
        raise ValueError("unsupported clock source")
    hardware_clock = clock_source == "timer1"
    if hardware_clock:
        if not timer1_build:
            raise ValueError("TIMER1 analysis requires --timer1-build")
        if settings.get("build") != timer1_build:
            raise ValueError("report settings.build does not match --timer1-build")
    elif timer1_build is not None:
        raise ValueError("--timer1-build requires --clock-source timer1")
    positions = settings.get("positions", 2)
    if not report["passed"] or len(records) != positions * 8 or positions <= 1:
        raise ValueError("requires a passed report with complete multi-position history")
    if not all(row["ordinal"] == i and row["outcome_flags"] == 7
               for i, row in enumerate(records, 1)):
        raise ValueError("history is incomplete or out of order")
    source_hz, threshold = settings["source_hz"], settings["threshold"]
    declared_period_ms = 1000 * threshold / source_hz
    starts = [records[i * 8]["position_admitted_tick_ms"] for i in range(positions)]
    intervals = [delta(b, a) for a, b in zip(starts, starts[1:])]
    if not all(0 < value < 0x80000000 for value in intervals):
        raise ValueError("position timestamps are not increasing")
    # Only legacy reports use a source-derived estimate. Hardware time is
    # independent of the declared signal source frequency.
    scale = 1.0 if hardware_clock else declared_period_ms / statistics.mean(intervals)
    cycles = [records[i * 8 + 7]["cycle_elapsed_ms"] for i in range(positions)]
    first = [records[i * 8]["cycle_elapsed_ms"] for i in range(positions)]
    steps = [[] for _ in range(8)]
    request_gaps = []
    for i in range(positions):
        group = records[i * 8:(i + 1) * 8]
        steps[0].append(group[0]["cycle_elapsed_ms"])
        for j in range(1, 8):
            steps[j].append(delta(group[j]["sample_done_tick_ms"], group[j - 1]["sample_done_tick_ms"]))
            request_gaps.append((group[j]["observed_pulses"] - group[j - 1]["observed_pulses"]) * 1000 / source_hz)
    start_command = next(row for row in report["transcript"] if row["command"] == "TRIG:START")
    final_angle = [row for row in report["transcript"] if row["command"] == "READ:ANGLE:POSITION?"][-1]
    counter_observations = []
    for entry in report["transcript"]:
        if entry["command"] != "READ:SEQ:COUNTER?" or entry["at"] < start_command["at"]:
            continue
        fields = [int(v) for v in entry["response"].split(",")]
        if 0 < fields[4] < threshold * positions:
            counter_observations.append((entry["at"] + entry["elapsed"] / 2, fields[4]))
    # Regression gives a host-time cross-check, with USB query latency uncertainty.
    rate = None
    if len(counter_observations) >= 2:
        xbar = statistics.mean(x for x, _ in counter_observations)
        ybar = statistics.mean(y for _, y in counter_observations)
        spread = sum((x - xbar) ** 2 for x, _ in counter_observations)
        if spread > 0:
            rate = sum((x - xbar) * (y - ybar) for x, y in counter_observations) / spread
    snapshots = []
    seen = set()
    for entry in profile["samples"]:
        row = entry["link"]
        if not row["message_total_ms"]:
            continue
        # Suppress repeated unchanged polls. No message kind/unique timing id
        # exists in this query; even this sample remains incomplete and biased.
        key = tuple(row[k] for k in ("run", "generation", "exchange_id", "phase", "ready", "completed",
                                     "offer_delay_ms", "return_delay_ms", "inbox_delay_ms", "message_total_ms"))
        if key in seen:
            continue
        seen.add(key)
        snapshots.append(row)
    def measured(values):
        if hardware_clock:
            return dict(hardware_ms=stats(values))
        return dict(raw_software_ticks=stats(values), estimated_ms=stats([value * scale for value in values]))
    stage_rows = [dict(state=index, **measured(values)) for index, values in enumerate(steps)]
    rounding_residuals = [
        row["message_total_ms"] - row["offer_delay_ms"] - row["return_delay_ms"] - row["inbox_delay_ms"]
        for row in snapshots]
    result = dict(
        scope=("offline TIMER1 hardware milliseconds, no physical IO edge timestamps or waveform calibration"
               if hardware_clock else "offline estimates, no physical timestamp or waveform calibration"),
        clock_source=clock_source, firmware_build=settings.get("build"),
        observation_mode=("active_diagnostic_stress" if settings.get("diagnostic_stress") else
                          "deferred_vector_readback" if settings.get("quiet_capture") else
                          "live_scpi_history_polling"),
        clock_provenance=dict(report_build=settings.get("build"),
                              asserted_timer1_build=timer1_build,
                              selection="explicit build assertion" if hardware_clock else "legacy default"),
        clock_scale_ms_per_unit=scale,
        positions=positions, records=len(records), declared_input_hz=source_hz,
        threshold=threshold, declared_position_period_ms=declared_period_ms,
        observed_host_elapsed_s=final_angle["at"] - start_command["at"],
        host_counter_rate_hz=rate, host_counter_observations=len(counter_observations),
        interval=measured(intervals), full_position=measured(cycles),
        first_sample=measured(first), later_sample_intervals=measured([v for s in steps[1:] for v in s]),
        states=stage_rows, request_to_next_request_ms_from_pulse_count=stats(request_gaps),
        position_request_delay_ms_from_pulse_count=stats([
            (records[i * 8]["observed_pulses"] - records[i * 8]["threshold_pulses"]) * 1000 / source_hz
            for i in range(positions)]),
        final_state_request_delay_ms_from_pulse_count=stats([
            (records[i * 8 + 7]["observed_pulses"] - records[i * 8 + 7]["threshold_pulses"]) * 1000 / source_hz
            for i in range(positions)]),
        sampled_last_message={key: measured([row[key] for row in snapshots]) for key in
                              ("offer_delay_ms", "return_delay_ms", "inbox_delay_ms", "message_total_ms")},
        sampled_message_components_sum_mismatches=sum(
            not (0 <= residual <= 2) if hardware_clock else residual != 0
            for residual in rounding_residuals),
        sampled_message_floor_rounding_residual_ms=stats(rounding_residuals),
        configured_settle_us=settings["settle_us"], configured_output_pulse_us=settings["gateway_pulse_us"],
        limitations=["Source frequency is declared, not independently measured.",
                     "SCPI polling and RAM timestamp recording can perturb timing; compare only explicitly identified observation modes.",
                     "Host observation time includes any planned quiet wait and readback latency, not just sequence execution.",
                     ("TIMER1 source is asserted by the selected firmware build; millisecond fields cannot prove microsecond IO timing."
                      if hardware_clock else "Scaled software time is an estimate and cannot prove microsecond IO timing."),
                     "Sample completion is recorded at gateway READY with owner no longer busy, before READY_NEXT advance.",
                     "Last-message query lacks kind and unique timing identity; sampled biased subset only.",
                     "Physical READY is OUT loopback, not real VNA measurement latency.",
                     "No separate timestamps for code write, settled output, trigger edge, or READY edge."])
    wait_key = "wait_after_sample_hardware_ms" if hardware_clock else "estimated_wait_after_sample_ms"
    result[wait_key] = stats([
        (intervals[i] - cycles[i]) * scale for i in range(positions - 1)])
    if hardware_clock:
        result["position_period_diagnostic"] = dict(
            measured_hardware_ms=stats(intervals),
            declared_period_ms=declared_period_ms,
            measured_mean_to_declared_ratio=statistics.mean(intervals) / declared_period_ms,
            measured_minus_declared_ms=stats([value - declared_period_ms for value in intervals]),
            normalized=False)
    else:
        result["estimated_ms_per_software_tick"] = scale
    if any("timing" in record for record in records):
        if not hardware_clock or not all("timing" in record for record in records):
            raise ValueError("raw timing requires TIMER1 mode and every completed record")
        result["detailed_timing"] = detailed_timing(records, positions)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--clock-source", choices=("software-tick", "timer1"), default="software-tick")
    parser.add_argument("--timer1-build", help="Exact report settings.build whose timestamps use TIMER1")
    parser.add_argument("--csv", type=Path, help="per-state raw ticks and measured stage durations")
    parser.add_argument("--markdown", type=Path, help="readable per-position and per-state tables")
    args = parser.parse_args()
    raw = args.report.read_bytes()
    result = analyze(json.loads(raw), clock_source=args.clock_source, timer1_build=args.timer1_build)
    result["source"] = str(args.report)
    result["source_sha256"] = hashlib.sha256(raw).hexdigest()
    if args.csv:
        write_timing_csv(args.csv, result)
    if args.markdown:
        write_timing_markdown(args.markdown, result)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
