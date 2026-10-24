#!/usr/bin/env python3
"""N4.2 STOP->stage->START transaction HIL (local config commands).

BEGIN opens the idle window and marks a transaction; a config stage is ACKed;
COMMIT applies and closes the window; the same config stage is then rejected
with RUN_STATE_DENIED (reason 7).  A second BEGIN + failing stage + ABORT
reopens the window without applying the failed stage.
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


def values(text):
    out = []
    for item in text.split(","):
        item = item.strip().strip('"')
        try:
            out.append(int(item, 0))
        except ValueError:
            out.append(item)
    return out


class ConfigTxnHil:
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

    def txn_status(self, port):
        v = values(scpi(port, "SYSTem:REFMEM:CONFig:STATus?"))
        if len(v) < 2:
            raise RuntimeError(f"{port}: txn status parse failed: {v}")
        return {"active": v[0], "window": v[1]}

    def ctrl(self, port):
        v = values(scpi(port, "SYSTem:CTRL:STATus?"))
        if len(v) < 13:
            raise RuntimeError(f"{port}: ctrl parse failed: {v}")
        return {"state": v[2], "reason": v[10]}

    def stage(self, port, node_id=1):
        bit = 1 << self.args.slot
        return scpi(port, f"SYSTem:CTRL:EXECute 2,{bit},{bit},"
                          f"{node_id},1,0,0,0,0,0")

    def step(self, name, expect, actual):
        passed = expect(actual)
        self.records[name] = {"actual": actual, "passed": passed}
        print(json.dumps({"step": name, "passed": passed, "actual": actual},
                         ensure_ascii=False))
        if not passed:
            raise RuntimeError(f"step {name} failed: {actual}")

    def run(self):
        self.reset()
        self.discover()
        port = self.args.port

        self.step("initial_idle",
                  lambda v: v["active"] == 0 and v["window"] == 1,
                  self.txn_status(port))
        self.step("begin", lambda v: v == 1,
                  values(scpi(port, "SYSTem:REFMEM:CONFig:BEGIN"))[0])
        self.step("begin_status",
                  lambda v: v["active"] == 1 and v["window"] == 1,
                  self.txn_status(port))
        self.step("stage_inside_window", lambda v: "QUEUED" in v,
                  self.stage(port))
        time.sleep(1.5)
        self.step("stage_acked",
                  lambda v: v["state"] == 4, self.ctrl(port))

        self.step("commit", lambda v: v == 1,
                  values(scpi(port, "SYSTem:REFMEM:CONFig:COMMit"))[0])
        self.step("commit_status",
                  lambda v: v["active"] == 0 and v["window"] == 0,
                  self.txn_status(port))
        self.step("stage_after_commit_queued", lambda v: "QUEUED" in v,
                  self.stage(port))
        time.sleep(1.5)
        self.step("stage_after_commit_rejected",
                  lambda v: v["state"] == 5 and v["reason"] == 7,
                  self.ctrl(port))

        self.step("begin_abort", lambda v: v == 1,
                  values(scpi(port, "SYSTem:REFMEM:CONFig:BEGIN"))[0])
        self.step("stage_invalid", lambda v: "QUEUED" in v,
                  self.stage(port, node_id=99))
        time.sleep(1.5)
        self.step("invalid_stage_not_gate",
                  lambda v: v["reason"] != 7, self.ctrl(port))
        self.step("abort", lambda v: v == 0,
                  values(scpi(port, "SYSTem:REFMEM:CONFig:ABORt"))[0])
        self.step("abort_status",
                  lambda v: v["active"] == 0 and v["window"] == 1,
                  self.txn_status(port))

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
        lines = ["# N4.2 config transaction HIL", "",
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
                        default=ROOT / "out/refmem_config_txn_hil" /
                        datetime.now().strftime("%Y%m%d_%H%M%S"))
    parser.add_argument("--reset-settle-s", type=float, default=8.0)
    args = parser.parse_args()
    try:
        return ConfigTxnHil(args).run()
    except Exception as exc:  # noqa: BLE001
        print(json.dumps({"error": str(exc)}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
