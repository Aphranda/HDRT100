"""RP2350_TRIG -- Internal Trigger Frequency Measurement Tool

Uses the MCU-side MEASure:FREQuency? SCPI command for precision frequency
measurement.  The MCU gates with time_us_64() (1 us resolution), eliminating
USB CDC polling jitter.

Features:
  - Auto-detects signal presence and approximate frequency
  - Multi-gate precision measurement
  - Stability analysis (spread, ppm, stdev)
  - JSON report output

Usage:
  python tools/trigger_meas/trigger_meas.py COM4
  python tools/trigger_meas/trigger_meas.py COM4 --gates 100,500,1000,2000
  python tools/trigger_meas/trigger_meas.py COM4 --runs 5 --gate 1000
  python tools/trigger_meas/trigger_meas.py COM4 --json
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:
    raise SystemExit("pyserial required: python -m pip install pyserial")

ROOT = Path(__file__).resolve().parents[2]

# ── CLI ──

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("port", help="USB CDC serial port, e.g. COM4")
    p.add_argument("--gates", type=str, default="50,100,200,500,1000,2000",
                   help="Comma-separated gate times in ms (default: 50,100,200,500,1000,2000)")
    p.add_argument("--gate", type=int, default=0,
                   help="Single gate override.  Runs N times at this gate (see --runs).")
    p.add_argument("--runs", type=int, default=1,
                   help="Repeat each gate measurement N times (default: 1)")
    p.add_argument("--seq-len", type=int, default=256,
                   help="SEQ_STEP sequence length (default: 64)")
    p.add_argument("--seq-width", type=int, default=4,
                   help="SEQ_STEP output width in bits (default: 4)")
    p.add_argument("--json", action="store_true",
                   help="Output results as JSON")
    p.add_argument("--out-dir", type=Path, default=None,
                   help="Save JSON report to directory")
    return p.parse_args()


# ── Communication ──

class MeasTool:
    def __init__(self, port: str):
        self.ser = serial.Serial(port, 115200, timeout=0.5)

    def _read_response(self, timeout: float | None = None) -> str:
        """Read SCPI response, skipping log lines starting with '['."""
        if timeout is not None:
            self.ser.timeout = timeout
        try:
            for _ in range(32):  # max 32 lines to skip logs
                raw = self.ser.readline()
                if not raw:
                    return ""  # timeout
                line = raw.decode(errors="replace").strip()
                if not line or line.startswith("["):
                    continue
                return line
        finally:
            if timeout is not None:
                self.ser.timeout = 0.5  # restore default
        return ""

    def scpi(self, cmd: str, delay: float = 0.02) -> str:
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode())
        if "MEAS:FREQ?" in cmd:
            time.sleep(delay)
            return self._read_response(timeout=delay + 0.5)
        time.sleep(delay)
        return self._read_response()

    def drain(self, wait: float = 0.05):
        time.sleep(wait)
        try:
            self.ser.reset_input_buffer()
        except Exception:
            pass

    def arm(self, seq_len: int = 256, seq_width: int = 4):
        self.scpi("*RST")             # reset SCPI state first
        self.scpi("TRIG:DIS")
        self.drain(0.02)
        self.scpi(f"TRIG:SEQ:LENG {seq_len}")
        self.scpi(f"TRIG:SEQ:WIDT {seq_width}")
        self.scpi("TRIG:MODE 1")
        self.drain(0.02)
        result = self.scpi("TRIG:ARM")
        if "OK" not in result:
            raise RuntimeError(f"ARM failed: {result}")

    def disarm(self):
        self.scpi("TRIG:DIS")

    def measure_freq(self, gate_ms: int) -> int:
        """Run internal frequency measurement.  Returns Hz, or 0 if no signal."""
        raw = self.scpi(f"MEAS:FREQ? {gate_ms}", delay=gate_ms / 1000.0 + 0.1)
        try:
            return int(raw)
        except ValueError:
            return -1

    def get_stat(self) -> dict:
        """Parse STAT:TRIG? response."""
        raw = self.scpi("STAT:TRIG?")
        parts = raw.replace('"', "").split(",")
        if len(parts) < 9:
            return {}
        return {
            "mode": parts[0],
            "state": int(parts[1]),
            "source_pin": int(parts[2]),
            "seq_index": int(parts[3]),
            "trigger_count": int(parts[6]),
            "output_count": int(parts[7]),
            "error_code": int(parts[8]),
        }

    def close(self):
        self.ser.close()


# ── Signal detection ──

def detect_signal(tool: MeasTool) -> dict | None:
    """Quick probe to detect signal presence and estimate frequency.

    Strategy:
      1. Take a 100ms snapshot of STAT:TRIG? to see if trigger_count is changing.
      2. If changing, do a 100ms MEAS:FREQ? for initial estimate.
      3. Refine with a 500ms measurement.
    """

    # Phase 1: is anything happening at all?
    s0 = tool.get_stat()
    time.sleep(0.05)
    s1 = tool.get_stat()

    if s0.get("trigger_count", 0) == s1.get("trigger_count", 0):
        # No triggers at all -- signal absent or PIO not detecting edges
        return None

    # Phase 2: quick internal measurement
    f_100 = tool.measure_freq(100)
    if f_100 <= 0:
        return None

    # Phase 3: refine with 500ms gate for better accuracy
    f_500 = tool.measure_freq(500)
    if f_500 <= 0:
        f_500 = f_100

    return {
        "present": True,
        "freq_hz": f_500,
        "freq_100ms": f_100,
        "trigger_count_start": s0.get("trigger_count", 0),
        "trigger_count_end": s1.get("trigger_count", 0),
    }


# ── Main ──

def run_measurements(tool: MeasTool, gates: list[int], runs: int,
                     seq_len: int, seq_width: int) -> dict:
    """Arm, detect signal, run precision measurements, disarm."""

    build = tool.scpi("SYST:FW:BUILD?").strip('"')

    # ── Signal detection ──
    print("Probing signal...", end=" ", flush=True)
    sig = detect_signal(tool)
    if sig is None:
        print("NO SIGNAL")
        print("\n  No trigger edges detected on GPIO16.")
        print("  Check: signal source connected?  Correct pin?  Voltage level?")
        print("  PIO armed and waiting for edges.")
        tool.disarm()
        return {
            "timestamp": datetime.now().isoformat(),
            "build_id": build,
            "signal": {"present": False},
            "error": "no_signal",
        }

    f0 = sig["freq_hz"]
    print(f"{f0} Hz")
    print(f"  Quick estimate: 100ms={sig['freq_100ms']}Hz  500ms={f0}Hz")

    # ── Precision measurements ──
    results: dict[str, list] = {}
    all_freqs = []

    if runs == 1:
        print(f"\n{'Gate(ms)':>8s}  {'Freq(Hz)':>8s}  {'Deviation':>10s}  Stability")
        print("-" * 55)

    for gate in gates:
        freqs = []
        for _ in range(runs):
            f = tool.measure_freq(gate)
            if f > 0:
                freqs.append(f)
                all_freqs.append(f)

        results[str(gate)] = freqs

        if runs == 1 and freqs:
            f = freqs[0]
            dev = f - f0
            bar = "#" * max(1, min(40, f // 200))
            print(f"{gate:>8d}  {f:>8d}  {dev:>+10d}  {bar}")
        elif freqs:
            mean_f = statistics.mean(freqs)
            stdev_f = statistics.stdev(freqs) if len(freqs) > 1 else 0
            cv = stdev_f / mean_f * 100 if mean_f > 0 else 0
            dev = mean_f - f0
            print(f"{gate:>8d}  {mean_f:>8.0f}  {dev:>+10.0f}  "
                  f"n={len(freqs)} stdev={stdev_f:.1f}Hz cv={cv:.3f}%")

    tool.disarm()

    # ── Summary ──
    valid = [f for f in all_freqs if f > 0]
    summary: dict = {
        "timestamp": datetime.now().isoformat(),
        "build_id": build,
        "signal": sig,
        "config": {
            "seq_len": seq_len,
            "seq_width": seq_width,
            "triggers_per_entry": 1,
        },
        "gates": results,
    }

    if len(valid) >= 2:
        mean_f = statistics.mean(valid)
        stdev_f = statistics.stdev(valid)
        spread = max(valid) - min(valid)
        summary["statistics"] = {
            "mean_hz": round(mean_f, 1),
            "stdev_hz": round(stdev_f, 1),
            "min_hz": min(valid),
            "max_hz": max(valid),
            "spread_hz": spread,
            "spread_ppm": round(spread / mean_f * 1e6, 1) if mean_f > 0 else 0,
        }
        s = summary["statistics"]
        print(f"\n  Summary: {s['mean_hz']:.1f} Hz  "
              f"spread={s['spread_hz']}Hz ({s['spread_ppm']:.0f}ppm)  "
              f"stdev={s['stdev_hz']:.1f}Hz")

        # Grade
        ppm = s["spread_ppm"]
        if ppm < 100:
            grade = "EXCELLENT (<100 ppm)"
        elif ppm < 1000:
            grade = "GOOD (<1000 ppm)"
        elif ppm < 10000:
            grade = "FAIR (<10000 ppm)"
        else:
            grade = "POOR - check signal source"
        print(f"  Grade: {grade}")

    return summary


def main():
    args = parse_args()

    if args.gate > 0:
        gates = [args.gate]
    else:
        gates = [int(x.strip()) for x in args.gates.split(",") if x.strip()]

    if not gates:
        raise SystemExit("No valid gate values")

    tool = MeasTool(args.port)
    try:
        tool.drain(0.1)
        tool.arm(args.seq_len, args.seq_width)
        summary = run_measurements(tool, gates, args.runs,
                                   args.seq_len, args.seq_width)
    finally:
        tool.close()

    if args.json:
        print("\n" + json.dumps(summary, indent=2))

    if args.out_dir:
        out_dir = Path(args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        json_path = out_dir / f"trigger_meas_{ts}.json"
        json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"\nSaved: {json_path}")


if __name__ == "__main__":
    main()
