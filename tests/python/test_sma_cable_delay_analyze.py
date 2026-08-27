import importlib.util
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "sma_cable_delay_validate" / "sma_cable_delay_analyze.py"
SWEEP_SCRIPT = ROOT / "tools" / "sma_cable_delay_validate" / "sma_cable_delay_sweep.py"
WIRE_SCRIPT = ROOT / "tools" / "sma_cable_delay_validate" / "sma_cable_wire_order.py"


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
