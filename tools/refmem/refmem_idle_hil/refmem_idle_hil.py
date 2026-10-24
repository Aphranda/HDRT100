#!/usr/bin/env python3
"""N4.1 idle-window gate HIL (local config command).

With the idle window closed, a config-class command must be NACKed with
RUN_STATE_DENIED (reason 7); with the window open the same command must pass
the gate (ACK, or a non-gate business reason).  No ring is needed: the command
targets only the local board.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PYTHON = sys.executable
SCPI = ROOT / "tools" / "interface" / "scpi_query" / "scpi_query.py"
PICOTOOL = (
    Path(os.environ.get("USERPROFILE", ""))
    / ".pico-sdk" / "picotool" / "2.2.0-a4" / "picotool" / "picotool.exe"
)

BOARDS = [
    ("COM5", "0010071E65B5CB38", 0),
    ("COM4", "FB276192BEF9CCE1", 1),
    ("COM3", "2BD5090FE009FA2A", 2),
    ("COM6", "A1E549202D18ED6A", 3),
]


def run(cmd, timeout=60.0):
    return subprocess.run(
        cmd, cwd=ROOT, text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=timeout, check=False,
    )


def scpi(port, command, timeout=10.0):
    result = run([PYTHON, str(SCPI), port, command, "--timeout",
                  str(timeout), "--settle", "0.25"], timeout + 5.0)
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError(f"{port}: empty SCPI response for {command!r}")
    line = lines[-1]
    if "=>" in line:
        line = line.split("=>", 1)[1].strip()
    return line.strip().strip('"')


def parse_values(text):
    values = []
    for item in text.split(","):
        item = item.strip().strip('"')
        try:
            values.append(int(item, 0))
        except ValueError:
            values.append(item)
    return values


class IdleHil:
    def __init__(self, args):
        self.args = args
        self.out_dir = args.out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.records = {}
        self.build = ""

    def reset(self):
        for _, serial, _ in BOARDS:
            run([str(PICOTOOL), "reboot", "-f", "--ser", serial], 30.0)
        time.sleep(self.args.reset_settle_s)

    def discover(self):
        builds = set()
        for port, expected, _ in BOARDS:
            idn = scpi(port, "*IDN?")
            build = scpi(port, "SYSTem:FW:BUILD?")
            if expected not in idn:
                raise RuntimeError(f"{port}: expected {expected}, got {idn}")
            builds.add(build.strip('"'))
        if len(builds) != 1:
            raise RuntimeError(f"mixed builds: {builds}")
        self.build = next(iter(builds))

    def cmd_status(self, port):
        values = parse_values(scpi(port, "SYSTem:CTRL:STATus?"))
        if len(values) < 13:
            raise RuntimeError(f"{port}: ctrl parse failed: {values}")
        return {"state": values[2], "reason": values[10],
                "command_seq": values[3]}

    def step(self, name, expect, actual):
        passed = expect(actual)
        self.records[name] = {"actual": actual, "passed": passed}
        print(json.dumps({"step": name, "passed": passed, "actual": actual},
                         ensure_ascii=False))
        if not passed:
            raise RuntimeError(f"step {name} failed: {actual}")

    def local_config(self, port):
        # REFMEM_LOAD_NODE (id 2), target/required = the board's own bit.
        bit = 1 << self.args.slot
        return scpi(port, f"SYSTem:CTRL:EXECute 2,{bit},{bit},"
                          "1,1,0,0,0,0,0")

    def run(self):
        self.reset()
        self.discover()
        port = self.args.port

        self.step("window_default_open",
                  lambda v: v == 1,
                  parse_values(scpi(port, "SYSTem:REFMEM:IDLE:WINDow?"))[0])
        self.step("close_window", lambda v: v == 0,
                  parse_values(scpi(port, "SYSTem:REFMEM:IDLE:WINDow 0"))[0])

        self.step("config_queued_closed",
                  lambda v: "QUEUED" in v, self.local_config(port))
        time.sleep(1.5)
        status = self.cmd_status(port)
        self.step("config_rejected_run_state",
                  lambda v: v["state"] == 5 and v["reason"] == 7, status)

        self.step("open_window", lambda v: v == 1,
                  parse_values(scpi(port, "SYSTem:REFMEM:IDLE:WINDow 1"))[0])
        self.step("config_queued_open",
                  lambda v: "QUEUED" in v, self.local_config(port))
        time.sleep(1.5)
        status = self.cmd_status(port)
        self.step("config_passes_gate",
                  lambda v: v["reason"] != 7, status)

        # Restore a known-open window.
        scpi(port, "SYSTem:REFMEM:IDLE:WINDow 1")

        report = {
            "build": self.build,
            "port": port,
            "records": self.records,
            "passed": all(r["passed"] for r in self.records.values()),
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }
        (self.out_dir / "report.json").write_text(
            json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        lines = ["# N4.1 idle-window HIL", "",
                 f"- build: `{self.build}`", f"- port: `{port}`", "",
                 "| step | passed | actual |", "|---|---|---|"]
        for name, record in self.records.items():
            lines.append(f"| {name} | {'yes' if record['passed'] else 'no'} | "
                         f"`{json.dumps(record['actual'], ensure_ascii=False)}` |")
        (self.out_dir / "REPORT.md").write_text("\n".join(lines) + "\n",
                                                encoding="utf-8")
        print(json.dumps({"passed": report["passed"],
                          "out_dir": str(self.out_dir)}, ensure_ascii=False))
        return 0 if report["passed"] else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--slot", type=int, default=1)
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "out/refmem_idle_hil" /
                        datetime.now().strftime("%Y%m%d_%H%M%S"))
    parser.add_argument("--reset-settle-s", type=float, default=8.0)
    args = parser.parse_args()
    try:
        return IdleHil(args).run()
    except Exception as exc:  # noqa: BLE001
        print(json.dumps({"error": str(exc)}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
