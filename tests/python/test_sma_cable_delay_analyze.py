import importlib.util
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "sma_cable_delay_validate" / "sma_cable_delay_analyze.py"
SWEEP_SCRIPT = ROOT / "tools" / "sma_cable_delay_validate" / "sma_cable_delay_sweep.py"
WIRE_SCRIPT = ROOT / "tools" / "sma_cable_delay_validate" / "sma_cable_wire_order.py"
FIVE_BOARD_SCRIPT = (ROOT / "tools" / "sma_cable_delay_validate" /
                     "sma_cable_five_board_validate.py")
APPOINTMENT_SCRIPT = (ROOT / "tools" / "sma_cable_delay_validate" /
                      "sma_cable_appointment_validate.py")
SYMMETRIC_RTT_SCRIPT = (ROOT / "tools" / "sma_cable_delay_validate" /
                        "sma_cable_symmetric_rtt.py")


def _load_module():
    spec = importlib.util.spec_from_file_location("sma_cable_delay_analyze", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_phase_fit_recovers_delay():
    module = _load_module()
    delay_ps = 32_500
    points = []
    for frequency_hz in (2_000_000, 6_000_000, 10_000_000, 14_000_000):
        phase = 27_000 - frequency_hz * delay_ps * 360_000 // 1_000_000_000_000
        while phase >= 180_000:
            phase -= 360_000
        while phase < -180_000:
            phase += 360_000
        points.append(module.PhasePoint(frequency_hz, phase))
    fit = module.fit_phase(points)
    assert abs(fit["total_delay_ps"] - delay_ps) <= 2


def test_coarse_equal_baseline_keeps_absolute_delay_unknown(tmp_path):
    output = tmp_path / "analysis"
    subprocess.run(
        [sys.executable, str(SCRIPT), "--coarse-equal", "--output-dir", str(output)],
        check=True,
        cwd=ROOT,
    )
    result = json.loads((output / "sma_cable_delay.json").read_text(encoding="utf-8"))
    assert result["channel_relative_delay_ps"] == [0, 0, 0, 0]
    assert result["common_cable_delay_valid"] is False
    assert result["relative_only"] is True


def test_sweep_response_parser_preserves_signed_phase():
    spec = importlib.util.spec_from_file_location("sma_cable_delay_sweep", SWEEP_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    values = [2_000_000, 1_984_126, 250_000_000, 126, 256]
    for channel in range(4):
        values.extend([1, -1000 * (channel + 1), 16 + channel])
    parsed = module.parse_response(",".join(str(value) for value in values))
    assert parsed["actual_frequency_hz"] == 1_984_126
    assert parsed["channels"][3]["phase_mdeg"] == -4000


def test_wire_order_inference_requires_one_to_one_diagonal():
    spec = importlib.util.spec_from_file_location("sma_cable_wire_order", WIRE_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    measurements = []
    for source in range(1, 5):
        for input_channel in range(1, 5):
            measurements.append(module.WireMeasurement(
                source, source, input_channel, 1 << (source - 1),
                1 << (source - 1), 0,
                source == input_channel,
            ))
    inferred, passed = module.infer_wire_order(measurements)
    assert inferred == [1, 2, 3, 4]
    assert passed is True


def test_five_board_validation_requires_passing_wire_order(tmp_path):
    spec = importlib.util.spec_from_file_location(
        "sma_cable_five_board_validate", FIVE_BOARD_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    result_path = tmp_path / "wire.json"
    result_path.write_text(json.dumps({
        "schema": "sma-cable-delay/wire-order-v1",
        "passed": True,
        "inferred_source_to_input": [1, 2, 3, 4],
    }), encoding="utf-8")
    result = module.require_wire_order_result(result_path)
    assert result["passed"] is True

    result_path.write_text(json.dumps({
        "schema": "sma-cable-delay/wire-order-v1",
        "passed": False,
        "inferred_source_to_input": [2, 1, 3, 4],
    }), encoding="utf-8")
    try:
        module.require_wire_order_result(result_path)
    except RuntimeError as exc:
        assert "preflight failed" in str(exc)
    else:
        raise AssertionError("crossed wiring must block dynamic validation")


def test_five_board_phase_repeat_spread_wraps_at_half_turn():
    spec = importlib.util.spec_from_file_location(
        "sma_cable_five_board_validate_spread", FIVE_BOARD_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    assert module.phase_repeat_spread_mdeg([179_000, -179_000, 178_000]) == 3_000
    assert module.phase_repeat_spread_mdeg([0, 90_000, 180_000, 270_000]) == 270_000


def test_five_board_diagnostic_svg_marks_noncoherent_boundary(tmp_path):
    spec = importlib.util.spec_from_file_location(
        "sma_cable_five_board_validate_svg", FIVE_BOARD_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    records = [
        {"frequency_hz": frequency, "channel": channel,
         "valid_count": 4, "phase_spread_mdeg": channel * 10_000}
        for frequency in (2_000_000, 4_000_000)
        for channel in range(1, 5)
    ]
    output = tmp_path / "repeatability.svg"
    module.write_diagnostic_svg(output, records)
    text = output.read_text(encoding="utf-8")
    assert "phase-slope delay fit blocked" in text
    assert "NO4 → IN4" in text


def test_five_board_validator_parser_includes_frequency_and_duty():
    spec = importlib.util.spec_from_file_location(
        "sma_cable_five_board_validate_parser", FIVE_BOARD_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    values = [2_000_000, 1_984_126, 250_000_000, 126, 512]
    for channel in range(4):
        values.extend((1, -1000 * channel, 32, 33,
                       1_984_126 + channel, 500_000 + channel))
    result = module.parse_validator_response(",".join(map(str, values)))
    assert result["channels"][2]["falling_edge_count"] == 33
    assert result["channels"][3]["observed_frequency_hz"] == 1_984_129
    assert result["channels"][3]["duty_cycle_ppm"] == 500_003


def test_mark_appointment_summary_gates_signal_quality():
    spec = importlib.util.spec_from_file_location(
        "sma_cable_appointment_validate", APPOINTMENT_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    records = []
    for _ in range(3):
        records.append({
            "source_actual_frequency_hz": 2_000_000,
            "channels": [{
                "valid": True,
                "observed_frequency_hz": 2_000_001,
                "duty_cycle_ppm": 500_100,
            }],
        })
    summary = module.summarize_appointment(records, 3)
    assert summary["signal_quality_passed"] is True
    records[0]["channels"][0]["duty_cycle_ppm"] = 600_000
    summary = module.summarize_appointment(records, 3)
    assert summary["signal_quality_passed"] is False


def test_symmetric_rtt_infers_all_bidirectional_routes():
    spec = importlib.util.spec_from_file_location(
        "sma_cable_symmetric_rtt_routes", SYMMETRIC_RTT_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    scans = {}
    for node_no in range(1, 5):
        forward = []
        reverse = []
        for output_channel in range(1, 5):
            forward.append({
                "output_channel": output_channel,
                "detected_input_channels": (
                    [node_no] if output_channel == 1 else []),
            })
            reverse.append({
                "output_channel": output_channel,
                "detected_input_channels": (
                    [1] if output_channel == 5 - node_no else []),
            })
        scans[node_no] = {
            "node_to_validator": forward,
            "validator_to_node": reverse,
        }
    routes, passed = module.infer_routes(scans)
    assert passed is True
    assert [(item.node_output_channel,
             item.validator_input_channel,
             item.validator_output_channel,
             item.node_input_channel) for item in routes] == [
        (1, 1, 4, 1),
        (1, 2, 3, 1),
        (1, 3, 2, 1),
        (1, 4, 1, 1),
    ]


def test_symmetric_rtt_parser_and_summary_keep_quantized_values():
    spec = importlib.util.spec_from_file_location(
        "sma_cable_symmetric_rtt_summary", SYMMETRIC_RTT_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    values = [1, 1, 250_000_000, 4000, 17, 18, 10,
              32_000, 0, 16_000, 0, 1]
    parsed = module.parse_rtt_response(values)
    assert parsed["path_sum_ps"] == 32_000
    assert parsed["mean_leg_delay_ps"] == 16_000
    records = []
    for node_no in range(1, 5):
        for delay in (12_000, 16_000, 16_000):
            records.append({
                "node_no": node_no,
                "valid": True,
                "mean_leg_delay_ps": delay,
            })
    summary = module.summarize_records(records, 3)
    assert summary["passed"] is True
    assert summary["nodes"][0]["median_mean_leg_delay_ps"] == 16_000
    assert summary["nodes"][0]["quantized_delay_histogram"] == {
        "12000": 1, "16000": 2}
