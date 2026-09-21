"""Host tests for the offline synchronization metrology analyzer."""

import json
import math
import random
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.domains.vdc.sync_metrology.vdc_sync_metrology import (  # noqa: E402
    MetrologyRefusal,
    analyze,
    main,
    overlapping_adev,
    sha256_file,
)
from tools.vdc_sync_metrology.vdc_sync_metrology import analyze as legacy_analyze  # noqa: E402

REAL_600S = (ROOT / "out/HardwareAcceptance/20260919/dpll-delay-center-r2"
             / "plus20-batched-600s")


def test_legacy_import_forwards_to_classified_implementation():
    assert legacy_analyze is analyze


def synth_windows(count=24, cadence_s=5.0, slope=0.0, sigma_ns=0.0,
                  span_s=0.001, seed=7, start_ns=1_000_000_000_000,
                  jitter_ns=0, complete=True, overrun=False,
                  pairs_override=None, host_ns=True):
    rng = random.Random(seed)
    windows = []
    for index in range(count):
        anchor = start_ns + int(round(index * cadence_s * 1e9))
        if jitter_ns:
            anchor += rng.randint(-jitter_ns, jitter_ns)
        phase_ns = slope * (index * cadence_s) * 1e9
        pairs = []
        if pairs_override is not None and index in pairs_override:
            pairs = pairs_override[index]
        else:
            for _ in range(3):
                noise = rng.gauss(0.0, sigma_ns) if sigma_ns else 0.0
                second = noise + (rng.gauss(0.0, 0.01) if sigma_ns else 0.0)
                pairs.append([phase_ns + noise, phase_ns + second])
        window = dict(
            index=index,
            due_s=cadence_s * (index + 1),
            anchor_ns=anchor,
            end_ns=anchor + 300_000_000,
            span_s=span_s,
            pairs_ns=[(float(a), float(b)) for a, b in pairs],
            scatter_ns=[abs(float(b) - float(a)) for a, b in pairs],
            pairing="nearest periodic edge; absolute ordinal unproven",
            within_50ns=True,
            within_100ns=True,
            complete=complete,
            overrun=overrun,
            has_host_ns=host_ns,
        )
        windows.append(window)
    return windows


def write_probe(tmp_path, windows, large_ambiguity=False, drop_host=False):
    capture = tmp_path / "capture"
    capture.mkdir(parents=True, exist_ok=True)
    samples = []
    for window in windows:
        sample = {
            "due_s": window["due_s"],
            "started_s": window["due_s"],
            "ended_s": window["due_s"] + 3.0,
            "capture_complete": window["complete"],
            "capture_elapsed_s": 3.0,
            "overrun": window["overrun"],
            "phase": {
                "edges_s": [[0.0, window["span_s"]] for _ in range(4)],
                "relative_ns": [list(pair) for pair in window["pairs_ns"]],
                "pairing": window["pairing"],
                "large_phase_ambiguity": large_ambiguity,
                "within_50ns": True,
                "within_100ns": True,
            },
        }
        if not (drop_host or not window["has_host_ns"]):
            sample["acquisition_host_ns"] = [window["anchor_ns"], window["end_ns"]]
        samples.append(sample)
    probe = capture / "input-probe.json"
    probe.write_text(json.dumps({"scope_samples": samples}), encoding="utf-8")
    return probe


def test_offset_ppb_recovers_injected_slope(tmp_path):
    windows = synth_windows(count=24, slope=2e-9)
    write_probe(tmp_path, windows)
    receipt = analyze(tmp_path, threshold_ns=25.0)
    for channel in receipt["channels"]:
        assert channel["offset_ppb"] == pytest.approx(2.0, abs=1e-9)
        assert channel["sigma_ppb"] == pytest.approx(0.0, abs=1e-9)
        assert channel["residual_rms_ns"] == pytest.approx(0.0, abs=1e-6)


def test_gap_bound_is_offset_times_max_gap(tmp_path):
    windows = synth_windows(count=24, slope=2e-9, span_s=0.001)
    write_probe(tmp_path, windows)
    receipt = analyze(tmp_path, threshold_ns=25.0)
    gaps = receipt["unobserved"]
    assert gaps["max_gap_s"] == pytest.approx(4.999, abs=1e-6)
    assert gaps["overlapping_windows"] == 0
    # (|offset| + 2 sigma) * max_gap, with a zero-residual fit.
    assert receipt["verdict"]["gap_drift_bound_ns"] == pytest.approx(2.0 * 4.999, rel=1e-9)
    assert receipt["verdict"]["gap_drift_bound_within_threshold"] is True

    strict = analyze(tmp_path, threshold_ns=5.0)
    assert strict["verdict"]["gap_drift_bound_within_threshold"] is False


def test_overlapping_adev_matches_white_phase_noise_law():
    rng = random.Random(11)
    cadence = 5.0
    samples = 2001
    sigma = 2e-9
    times = [index * cadence for index in range(samples)]
    phases = [rng.gauss(0.0, sigma) for _ in range(samples)]
    table = {entry["factor"]: entry for entry in overlapping_adev(times, phases)}
    # White phase noise: ADEV(tau) = sqrt(3) * sigma_phase / tau.
    assert table[1]["adev"] == pytest.approx(math.sqrt(3) * sigma / cadence, rel=0.05)
    assert table[1]["adev"] / table[4]["adev"] == pytest.approx(4.0, rel=0.10)


def test_adev_records_insufficient_factors(tmp_path):
    windows = synth_windows(count=10, slope=0.0)
    write_probe(tmp_path, windows)
    receipt = analyze(tmp_path)
    table = {entry["factor"]: entry for entry in receipt["channels"][0]["adev"]}
    assert table[1]["adev"] is not None
    assert table[64]["adev"] is None
    assert table[64]["differences"] == 0
    assert "need" in table[64]["reason"]


def test_refuses_without_host_timestamps(tmp_path):
    windows = synth_windows(count=24)
    write_probe(tmp_path, windows, drop_host=True)
    out = tmp_path / "metrology.json"
    assert main([str(tmp_path), "--json-out", str(out)]) == 2
    assert not out.exists()
    with pytest.raises(MetrologyRefusal):
        analyze(tmp_path)


def test_refuses_on_large_phase_ambiguity(tmp_path):
    windows = synth_windows(count=24)
    write_probe(tmp_path, windows, large_ambiguity=True)
    with pytest.raises(MetrologyRefusal):
        analyze(tmp_path)


def test_refuses_incomplete_window_without_opt_in(tmp_path):
    windows = synth_windows(count=24)
    windows[3]["complete"] = False
    write_probe(tmp_path, windows)
    with pytest.raises(MetrologyRefusal):
        analyze(tmp_path)


def test_allow_incomplete_excludes_window(tmp_path):
    windows = synth_windows(count=24, slope=1e-9)
    windows[3]["overrun"] = True
    write_probe(tmp_path, windows)
    # Excluding a window widens a real gap, so the cadence stops being uniform and
    # the operator must acknowledge that separately; the refusal must not hide it.
    with pytest.raises(MetrologyRefusal):
        analyze(tmp_path, allow_incomplete=True)
    receipt = analyze(tmp_path, allow_incomplete=True, allow_nonuniform=True)
    assert receipt["excluded_windows"] == 1
    assert receipt["window_count"] == 23
    assert receipt["receipt_complete"] is False
    assert receipt["verdict"]["receipt_complete"] is False
    assert receipt["unobserved"]["max_gap_s"] == pytest.approx(9.999, abs=1e-6)
    assert any("excluded" in note for note in receipt["notes"])


def test_refuses_nonuniform_cadence_unless_allowed(tmp_path):
    windows = synth_windows(count=24, slope=1e-9, jitter_ns=900_000_000)
    write_probe(tmp_path, windows)
    with pytest.raises(MetrologyRefusal):
        analyze(tmp_path)
    receipt = analyze(tmp_path, allow_nonuniform=True)
    assert receipt["cadence_uniform"] is False
    assert receipt["cadence"]["jitter_fraction"] > 0.05


def test_receipt_binds_input_bytes(tmp_path):
    windows = synth_windows(count=24, slope=1e-9)
    probe = write_probe(tmp_path, windows)
    first = analyze(tmp_path)
    assert first["inputs"][0]["sha256"] == sha256_file(probe)
    assert first["inputs"][0]["size_bytes"] == probe.stat().st_size
    assert first["inputs"][0]["sha256"] == analyze(tmp_path)["inputs"][0]["sha256"]

    tampered = json.loads(probe.read_text(encoding="utf-8"))
    tampered["scope_samples"][0]["phase"]["relative_ns"][0][0] += 1.0
    probe.write_text(json.dumps(tampered), encoding="utf-8")
    assert analyze(tmp_path)["inputs"][0]["sha256"] != first["inputs"][0]["sha256"]


def test_receipt_never_claims_continuous_lock(tmp_path):
    windows = synth_windows(count=24, slope=1e-9)
    write_probe(tmp_path, windows)
    receipt = analyze(tmp_path)
    verdict = receipt["verdict"]
    assert verdict["sampled_window_lock"] is True
    assert verdict["continuous_lock_claimed"] is False
    assert verdict["measurement_noise_floor_qualified"] is False
    assert verdict["unobserved_interval_bounded"] is True
    assert receipt["anchors_crosschecked"] == 0
    assert any("noise floor" in item for item in receipt["limitations"])


def test_main_writes_receipt_and_reports_summary(tmp_path, capsys):
    windows = synth_windows(count=24, slope=1e-9)
    write_probe(tmp_path, windows)
    out = tmp_path / "receipt" / "metrology.json"
    assert main([str(tmp_path), "--json-out", str(out)]) == 0
    assert out.is_file()
    payload = json.loads(out.read_text(encoding="utf-8"))
    assert payload["schema"] == "HAOFV_VDC_SYNC_METROLOGY_V1"
    captured = capsys.readouterr()
    assert "sampled-window only" in captured.out


@pytest.mark.skipif(not REAL_600S.is_dir(), reason="600 s evidence not present")
def test_real_600s_evidence_bounds_unobserved_interval():
    receipt = analyze(REAL_600S, threshold_ns=25.0)
    assert receipt["window_count"] == 120
    assert receipt["receipt_complete"] is True
    assert receipt["anchors_crosschecked"] == 120
    assert receipt["unobserved"]["max_gap_s"] > 4.9
    assert receipt["unobserved"]["duty_fraction"] < 0.001
    for channel in receipt["channels"]:
        assert abs(channel["offset_ppb"]) + channel["sigma_2ppb"] < 0.05
        assert channel["unobserved_within_threshold"] is True
        assert channel["implied_white_phase_sigma_ns"] < 10.0
