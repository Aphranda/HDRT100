"""RP2350_TRIG — Trigger Timing Stability Test

Measures PIO trigger timing accuracy and jitter via SCPI STAT:TRIG? polling.
Connects to the device over USB CDC, configures SEQ_STEP mode, arms the
trigger, and samples the trigger counter at regular intervals to compute
long-term accuracy and short-term stability.

Usage:
  python tools/trigger_timing_test/trigger_timing_test.py COM4 [--seq-len 64] [--duration 20] [--signal-hz 1000]
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


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("port", help="USB CDC serial port, e.g. COM4")
    p.add_argument("--seq-len", type=int, default=128,
                   help="SEQ_STEP sequence length in output steps (default: 128)")
    p.add_argument("--seq-width", type=int, default=4,
                   help="Output bit width (default: 4)")
    p.add_argument("--duration", type=float, default=20.0,
                   help="Test duration in seconds (default: 20)")
    p.add_argument("--signal-hz", type=float, default=1000.0,
                   help="Expected input signal frequency in Hz (default: 1000)")
    p.add_argument("--out-dir", type=Path, default=None,
                   help="Output directory for test report")
    p.add_argument("--json", action="store_true",
                   help="Output results as JSON")
    return p.parse_args()


class TriggerTester:
    def __init__(self, port: str):
        self.ser = serial.Serial(port, 115200, timeout=1.0)

    def scpi(self, cmd: str, delay: float = 0.15) -> str:
        """Send SCPI command, return first non-log response line."""
        self.ser.write((cmd + "\n").encode())
        time.sleep(delay)
        resp = []
        for _ in range(8):
            raw = self.ser.readline()
            if not raw:
                break
            line = raw.decode(errors="replace").strip()
            if not line:
                continue
            if line.startswith("["):
                continue
            resp.append(line)
        return resp[0] if resp else ""

    def drain(self, wait: float = 0.5):
        time.sleep(wait)
        self.ser.read(self.ser.in_waiting or 1)

    def parse_stat(self, raw: str) -> dict | None:
        """Parse STAT:TRIG? response.
        Format: mode,state,source_pin,seq_index,?,?,trigger_count,output_count,error_code
        """
        s = raw.replace('"', "")
        f = s.split(",")
        if len(f) < 9:
            return None
        return {
            "mode": f[0],
            "state": int(f[1]),
            "source_pin": int(f[2]),
            "seq_index": int(f[3]),
            "trigger_count": int(f[6]),
            "output_count": int(f[7]),
            "error_code": int(f[8]),
        }

    def setup(self, seq_len: int, seq_width: int):
        """Configure SEQ_STEP and ARM."""
        self.scpi("TRIG:DIS", 0.5)
        self.drain(0.3)
        self.scpi(f"TRIG:SEQ:LENG {seq_len}")
        self.scpi(f"TRIG:SEQ:WIDT {seq_width}")
        self.scpi("TRIG:MODE 1")
        self.drain(0.5)

        # Verify configuration
        raw = self.scpi("STAT:TRIG?")
        d = self.parse_stat(raw)
        if d is None or d["state"] != 1:
            raise RuntimeError(f"SEQ_STEP configure failed: {raw}")

        result = self.scpi("TRIG:ARM")
        if "OK" not in result:
            raise RuntimeError(f"ARM failed: {result}")
        self.drain(0.3)

    def sample(self) -> tuple[float, dict]:
        """Take one timing sample. Returns (wall_time_s, parsed_stat)."""
        t_before = time.monotonic()
        raw = self.scpi("STAT:TRIG?")
        t_after = time.monotonic()
        t_mid = (t_before + t_after) / 2.0
        d = self.parse_stat(raw)
        if d is None:
            raise RuntimeError(f"STAT parse failed: {raw}")
        return t_mid, d

    def run(self, seq_len: int, seq_width: int, duration: float,
            signal_hz: float) -> dict:
        """Run timing stability test. Returns results dict."""
        print(f"BUILD: {self.scpi('SYST:FW:BUILD?')}")
        print(f"INIT:  {self.scpi('STAT:TRIG?')}")

        self.setup(seq_len, seq_width)
        print(f"ARMED: seq_len={seq_len} width={seq_width} signal={signal_hz}Hz")

        # ── Sampling loop ──
        samples: list[tuple[float, dict]] = []
        t0 = time.monotonic()

        print(f"\n{'Sample':>6s} {'t(s)':>8s} {'trig_count':>12s} {'delta':>8s} "
              f"{'rate(Hz)':>10s} {'seq_idx':>8s}")
        print("-" * 60)

        target_interval = max(0.3, duration / 30.0)  # ~30 samples
        next_t = t0 + target_interval

        while (time.monotonic() - t0) < duration:
            sleep_t = next_t - time.monotonic()
            if sleep_t > 0:
                time.sleep(sleep_t)
            else:
                next_t = time.monotonic()  # resync

            t_mid, d = self.sample()
            samples.append((t_mid, d))
            next_t += target_interval

            # Print progress
            if len(samples) >= 2:
                i = len(samples) - 1
                dt = samples[i][0] - samples[i - 1][0]
                dtrig = d["trigger_count"] - samples[i - 1][1]["trigger_count"]
                rate = dtrig / dt if dt > 0 else 0
                print(f"{i:>6d} {t_mid - t0:>8.2f} {d['trigger_count']:>12d} "
                      f"{dtrig:>8d} {rate:>10.1f} {d['seq_index']:>8d}")
            else:
                i = len(samples) - 1
                print(f"{i:>6d} {t_mid - t0:>8.2f} {d['trigger_count']:>12d} "
                      f"{'---':>8s} {'---':>10s} {d['seq_index']:>8d}")

        print()

        # ── Compute per-sample rates ──
        rates = []
        deltas = []
        for i in range(1, len(samples)):
            dt = samples[i][0] - samples[i - 1][0]
            dtrig = samples[i][1]["trigger_count"] - samples[i - 1][1]["trigger_count"]
            if dt > 0 and dtrig >= 0:
                rates.append(dtrig / dt)
                deltas.append(dtrig)

        if len(rates) < 3:
            raise RuntimeError("Not enough samples for analysis")

        # ── Statistics ──
        total_t = samples[-1][0] - samples[0][0]
        total_trig = samples[-1][1]["trigger_count"] - samples[0][1]["trigger_count"]
        avg_rate = total_trig / total_t if total_t > 0 else 0
        mean_rate = statistics.mean(rates)
        stdev_rate = statistics.stdev(rates)
        cv_pct = stdev_rate / mean_rate * 100 if mean_rate > 0 else float("inf")

        # Rate distribution buckets
        buckets: dict[int, int] = {}
        for r in rates:
            b = round(r / 20) * 20
            buckets[b] = buckets.get(b, 0) + 1

        # Expected vs actual
        expected_trig = total_t * signal_hz
        deviation_ppm = (total_trig - expected_trig) / expected_trig * 1_000_000

        results = {
            "timestamp": datetime.now().isoformat(),
            "build_id": self.scpi("SYST:FW:BUILD?").strip('"'),
            "config": {
                "seq_len": seq_len,
                "seq_width": seq_width,
                "signal_hz": signal_hz,
                "duration_s": total_t,
            },
            "accuracy": {
                "total_triggers": total_trig,
                "expected_triggers": round(expected_trig),
                "deviation_ppm": round(deviation_ppm, 1),
                "avg_rate_hz": round(avg_rate, 2),
                "triggers_per_entry": 1,
            },
            "stability": {
                "num_samples": len(samples),
                "num_rates": len(rates),
                "mean_rate_hz": round(mean_rate, 2),
                "stdev_rate_hz": round(stdev_rate, 2),
                "cv_pct": round(cv_pct, 4),
                "min_rate_hz": round(min(rates), 1),
                "max_rate_hz": round(max(rates), 1),
                "peak_peak_hz": round(max(rates) - min(rates), 1),
                "rate_distribution": {str(k): v for k, v in sorted(buckets.items())},
            },
            "grade": (
                "EXCELLENT" if abs(deviation_ppm) < 100 and cv_pct < 0.5
                else "GOOD" if abs(deviation_ppm) < 1000 and cv_pct < 2.0
                else "FAIR" if abs(deviation_ppm) < 10000
                else "POOR"
            ),
        }

        # ── DISARM ──
        disarm = self.scpi("TRIG:DIS", 0.5)
        print(f"DISARM: {disarm}")
        final = self.scpi("STAT:TRIG?")
        print(f"FINAL:  {final}")

        return results

    def close(self):
        self.ser.close()


def print_report(results: dict):
    """Print human-readable timing stability report."""
    c = results["config"]
    a = results["accuracy"]
    s = results["stability"]

    print(f"\n{'=' * 60}")
    print(f"TRIGGER TIMING STABILITY REPORT")
    print(f"{'=' * 60}")
    print(f"  Build:       {results['build_id']}")
    print(f"  Timestamp:   {results['timestamp']}")
    print(f"  Config:      seq_len={c['seq_len']} width={c['seq_width']} "
          f"signal={c['signal_hz']}Hz trig/entry={a['triggers_per_entry']}")
    print()

    print(f"── Long-term Accuracy ({c['duration_s']:.1f}s) ──")
    print(f"  Total triggers:      {a['total_triggers']}")
    print(f"  Expected @{c['signal_hz']}Hz:  {a['expected_triggers']}")
    print(f"  Deviation:           {a['deviation_ppm']:.1f} ppm "
          f"({a['deviation_ppm']/10000:.4f}%)")
    print(f"  Avg rate:            {a['avg_rate_hz']:.2f} Hz")
    print()

    print(f"── Short-term Stability ({s['num_rates']} samples) ──")
    print(f"  Mean sample rate:    {s['mean_rate_hz']:.2f} Hz")
    print(f"  Rate stdev:          {s['stdev_rate_hz']:.2f} Hz")
    print(f"  CV (stdev/mean):     {s['cv_pct']:.4f}%")
    print(f"  Rate min:            {s['min_rate_hz']:.1f} Hz")
    print(f"  Rate max:            {s['max_rate_hz']:.1f} Hz")
    print(f"  Peak-peak spread:    {s['peak_peak_hz']:.1f} Hz")

    print(f"\n  Rate distribution:")
    for b_str, count in sorted(s["rate_distribution"].items(),
                                key=lambda x: int(x[0])):
        b = int(b_str)
        bar = "#" * count
        print(f"    {b:>6d} Hz: {bar} ({count})")

    print(f"\n── Grade ──")
    print(f"  {results['grade']}")
    print()

    # Interpret
    print(f"── Interpretation ──")
    if abs(a["deviation_ppm"]) < 200:
        print(f"  Long-term accuracy {a['deviation_ppm']:.0f}ppm = lab-grade.")
        print(f"  PIO is not missing edges; clock reference is stable.")
    else:
        print(f"  Deviation {a['deviation_ppm']:.0f}ppm > 200ppm.")
        print(f"  Check signal source accuracy or clk_sys reference.")

    trig_per_entry = a["triggers_per_entry"]
    quantization_noise = trig_per_entry / (c["signal_hz"] * (c["duration_s"] / s["num_rates"])) * 100
    if s["cv_pct"] < 1.0:
        print(f"  CV {s['cv_pct']:.2f}% < 1% = real-time counter working correctly.")
        print(f"  No DMA rollover quantization artifact detected.")
    else:
        print(f"  CV {s['cv_pct']:.2f}% > 1% — possible residual quantization.")
        print(f"  Expected quantization noise floor: ~{quantization_noise:.2f}%")

    print(f"  Real PIO jitter (~6.7ns) is ~{6.7e-9 * c['signal_hz'] * 100:.1e}% "
          f"of trigger period — only measurable with oscilloscope.")


def main():
    args = parse_args()

    tester = TriggerTester(args.port)
    try:
        tester.drain(3.0)
        results = tester.run(
            seq_len=args.seq_len,
            seq_width=args.seq_width,
            duration=args.duration,
            signal_hz=args.signal_hz,
        )
    finally:
        tester.close()

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        print_report(results)

    # Write report to file if requested
    if args.out_dir:
        out_dir = Path(args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        json_path = out_dir / f"timing_test_{ts}.json"
        json_path.write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(f"\nReport saved to: {json_path}")


if __name__ == "__main__":
    main()
