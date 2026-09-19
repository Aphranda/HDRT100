from copy import deepcopy
import json

import pytest

from tools.hardware_acceptance.sequence_timing_analyze import (
    analyze, delta, main, stats, write_timing_csv, write_timing_markdown,
)


def fixture_report():
    records = []
    for position, tick in ((1, 0xfffffff0), (2, (0xfffffff0 + 2000) & 0xffffffff)):
        for index in range(8):
            elapsed = 20 * (index + 1)
            records.append(dict(ordinal=len(records) + 1, outcome_flags=7,
                position_admitted_tick_ms=tick, cycle_elapsed_ms=elapsed,
                sample_done_tick_ms=(tick + elapsed) & 0xffffffff,
                observed_pulses=1000 * position + index * 10, threshold_pulses=position * 1000))
    message = dict(run=1, generation=1, exchange_id=1, phase=9, ready=8, completed=7,
                   offer_delay_ms=2, return_delay_ms=6, inbox_delay_ms=2, message_total_ms=10)
    return dict(passed=True, settings=dict(positions=2, source_hz=1000, threshold=1000,
        settle_us=10, gateway_pulse_us=1000), two_positions=dict(history=records,
        samples=[dict(link=message), dict(link=deepcopy(message))]), transcript=[
        dict(command="TRIG:START", at=0, elapsed=0),
        dict(command="READ:SEQ:COUNTER?", at=.1, elapsed=0, response="1,1,1,1000,100,0,100,0,0,0,9,0"),
        dict(command="READ:SEQ:COUNTER?", at=1.1, elapsed=0, response="1,1,1,1000,1100,1,100,0,8,8,9,0"),
        dict(command="READ:ANGLE:POSITION?", at=2.2, elapsed=0)])


def test_scaling_wrap_and_dedup_preserve_raw_measurements():
    result = analyze(fixture_report())
    assert result["estimated_ms_per_software_tick"] == .5
    assert result["full_position"]["raw_software_ticks"]["mean"] == 160
    assert result["full_position"]["estimated_ms"]["mean"] == 80
    assert result["later_sample_intervals"]["estimated_ms"]["mean"] == 10
    assert result["host_counter_rate_hz"] == pytest.approx(1000)
    assert result["estimated_wait_after_sample_ms"]["mean"] == 920
    assert result["sampled_last_message"]["message_total_ms"]["estimated_ms"]["count"] == 1
    assert delta(5, 0xfffffffe) == 7


@pytest.mark.parametrize("change", ["failed", "missing", "partial"])
def test_incomplete_evidence_rejected(change):
    report = fixture_report()
    if change == "failed":
        report["passed"] = False
    elif change == "missing":
        report["two_positions"]["history"].pop()
    else:
        report["two_positions"]["history"][0]["outcome_flags"] = 3
    with pytest.raises(ValueError):
        analyze(report)


def test_stats_p95_nearest_rank():
    assert stats(range(1, 101))["p95"] == 95


def test_timer1_preserves_hardware_time_despite_declared_frequency():
    report = fixture_report()
    report["settings"]["build"] = "20260919090000"
    result = analyze(report, clock_source="timer1", timer1_build="20260919090000")
    report["settings"]["source_hz"] = 5000
    faster_declaration = analyze(report, clock_source="timer1", timer1_build="20260919090000")
    assert result["clock_source"] == "timer1"
    assert result["firmware_build"] == "20260919090000"
    assert result["clock_provenance"]["asserted_timer1_build"] == "20260919090000"
    assert result["clock_scale_ms_per_unit"] == 1
    assert "estimated_ms_per_software_tick" not in result
    assert result["full_position"] == {"hardware_ms": stats([160, 160])}
    assert result["later_sample_intervals"]["hardware_ms"]["mean"] == 20
    assert result["wait_after_sample_hardware_ms"]["mean"] == 1840
    assert result["interval"]["hardware_ms"]["mean"] == 2000
    for key in ("full_position", "later_sample_intervals", "interval", "states", "sampled_last_message"):
        assert result[key] == faster_declaration[key]
    assert result["position_period_diagnostic"]["measured_mean_to_declared_ratio"] == 2
    assert faster_declaration["position_period_diagnostic"]["measured_mean_to_declared_ratio"] == 10
    assert faster_declaration["position_period_diagnostic"]["normalized"] is False


@pytest.mark.parametrize("report_build,asserted_build", [
    (None, "new"), ("old", "new"), ("new", None), ("new", "")])
def test_timer1_requires_exact_explicit_build(report_build, asserted_build):
    report = fixture_report()
    if report_build is not None:
        report["settings"]["build"] = report_build
    with pytest.raises(ValueError, match="build"):
        analyze(report, clock_source="timer1", timer1_build=asserted_build)


def test_timer1_build_requires_timer1_source():
    with pytest.raises(ValueError, match="clock-source timer1"):
        analyze(fixture_report(), timer1_build="new")


@pytest.mark.parametrize("residual,expected_mismatches", [(-1, 1), (0, 0), (1, 0), (2, 0), (3, 1)])
def test_timer1_separately_floor_rounded_components(residual, expected_mismatches):
    report = fixture_report()
    report["settings"]["build"] = "new"
    for entry in report["two_positions"]["samples"]:
        entry["link"]["message_total_ms"] = 10 + residual
    result = analyze(report, clock_source="timer1", timer1_build="new")
    assert result["sampled_message_components_sum_mismatches"] == expected_mismatches
    assert result["sampled_message_floor_rounding_residual_ms"] == stats([residual])


def test_timer1_cli_records_source_and_build_provenance(tmp_path, monkeypatch, capsys):
    report = fixture_report()
    report["settings"]["build"] = "new"
    source, output = tmp_path / "report.json", tmp_path / "analysis.json"
    source.write_text(json.dumps(report), encoding="utf-8")
    monkeypatch.setattr("sys.argv", ["sequence_timing_analyze.py", str(source), "--out", str(output),
                                    "--clock-source", "timer1", "--timer1-build", "new"])
    main()
    result = json.loads(output.read_text(encoding="utf-8"))
    assert result["clock_source"] == "timer1"
    assert result["clock_provenance"]["report_build"] == "new"
    assert result["source"] == str(source)
    assert len(result["source_sha256"]) == 64
    assert result["full_position"]["hardware_ms"]["mean"] == 160
    capsys.readouterr()


def timed_report():
    report = fixture_report()
    report["settings"]["build"] = "raw"
    for ordinal, record in enumerate(report["two_positions"]["history"], 1):
        index = (ordinal - 1) % 8
        record.update(run=1, generation=2, binding_epoch=3, exchange_id=ordinal,
                      position=(ordinal - 1) // 8 + 1, sequence_index=index)
        identity = {key: record[key] for key in
                    ("ordinal", "run", "generation", "binding_epoch", "exchange_id", "position", "sequence_index")}
        base = index * 5000000
        record["timing"] = dict(identity, version=1, clock_hz=250000000, flags=63,
            request_ticks=base, applied_ticks=base + 1, offered_ticks=base + 2,
            returned_ticks=base + 3, fire_queued_ticks=base + 4, done_ticks=base + 5000000)
    return report


def test_raw_details_are_additive_and_keep_four_ns_ticks(tmp_path):
    report = timed_report()
    result = analyze(report, clock_source="timer1", timer1_build="raw")
    detail = result["detailed_timing"]
    assert detail["tick_ns"] == 4 and len(detail["states"]) == 16
    assert detail["states"][0]["segments_ns"]["request_to_applied"] == 4
    assert detail["states"][1]["previous_done_to_fire_queue_ns"] == 16
    assert detail["positions"][0]["total_ns"] == 160000000
    for position in detail["positions"]:
        assert sum(position["segments_ns"].values()) == position["total_ns"]
        assert sum(position["state_intervals_ns"]) == position["total_ns"]
    assert detail["positions"][-1]["wait_to_next_position_ms"] is None
    path = tmp_path / "timings.csv"
    write_timing_csv(path, result)
    import csv
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 16 and rows[0]["request_to_applied_us"] == "0.004"
    assert rows[0]["previous_done_to_fire_queue_us"] == ""
    with pytest.raises(FileExistsError):
        write_timing_csv(path, result)
    result["source_sha256"] = "evidence"
    markdown = tmp_path / "timings.md"
    write_timing_markdown(markdown, result)
    text = markdown.read_text(encoding="utf-8")
    assert "250000000 Hz, 4 ns" in text and "| 160.000000 | N/A |" in text
    assert "not physical IO edges" in text


@pytest.mark.parametrize("change", ["missing", "clock", "flags", "identity", "backward", "duration", "position"])
def test_raw_timing_rejects_unreliable_breakdowns(change):
    report = timed_report()
    row = report["two_positions"]["history"][1]
    if change == "missing": del row["timing"]
    elif change == "clock": row["timing"]["clock_hz"] = 10000000
    elif change == "flags": row["timing"]["flags"] = 31
    elif change == "identity": row["timing"]["run"] += 1
    elif change == "backward": row["timing"]["offered_ticks"] = 0
    elif change == "duration": row["timing"]["done_ticks"] += 250000
    elif change == "position": row["position"] += 1
    with pytest.raises(ValueError):
        analyze(report, clock_source="timer1", timer1_build="raw")


def test_raw_timing_cannot_be_analyzed_as_software_ticks():
    with pytest.raises(ValueError, match="TIMER1"):
        analyze(timed_report())


@pytest.mark.parametrize("mode", ["quiet_capture", "diagnostic_stress", "live"])
def test_observation_load_is_explicit_in_analysis_and_exports(tmp_path, mode):
    report = timed_report()
    if mode != "live":
        report["settings"][mode] = True
    if mode == "quiet_capture":
        report["transcript"] = [row for row in report["transcript"]
                                if row["command"] != "READ:SEQ:COUNTER?"]
    result = analyze(report, clock_source="timer1", timer1_build="raw")
    expected = {"quiet_capture": "deferred_vector_readback",
                "diagnostic_stress": "active_diagnostic_stress", "live": "live_scpi_history_polling"}[mode]
    assert result["observation_mode"] == expected
    if mode == "quiet_capture":
        assert result["host_counter_rate_hz"] is None
        assert result["host_counter_observations"] == 0
    result["source_sha256"] = "test"
    write_timing_markdown(tmp_path / "report.md", result)
    write_timing_csv(tmp_path / "report.csv", result)
    assert expected in (tmp_path / "report.md").read_text(encoding="utf-8")
    assert expected in (tmp_path / "report.csv").read_text(encoding="utf-8-sig")
