#!/usr/bin/env python3
"""N4.3 remote config transaction HIL (A stages into B, B's window gates it).

Two-board: A=COM5, B=COM4.  Four-board: targets COM4/COM3/COM6.  Each target
runs its own local CONFig transaction; A's LOAD_NODE command travels over the
ring.  A valid stage is ACKed only while the target window is open; after
COMMIT the same command is NACKed with RUN_STATE_DENIED (7); an invalid stage
inside an open window fails with a business reason, and ABORT reopens.
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
TRN03 = ROOT / "tools" / "calibration_ring_validate" / "trn03_closed_loop.py"
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


def scpi_retry(port, command, attempts=4, delay_s=0.6):
    last = ""
    for _ in range(attempts):
        try:
            last = scpi(port, command)
        except Exception as exc:  # noqa: BLE001
            last = f"<error:{exc}>"
        if "timeout" not in last.lower() and not last.startswith("<error"):
            return last
        time.sleep(delay_s)
    return last


def values(text):
    out = []
    for item in text.split(","):
        item = item.strip().strip('"')
        try:
            out.append(int(item, 0))
        except ValueError:
            out.append(item)
    return out


class RemoteTxnHil:
    def __init__(self, args):
        self.args = args
        self.out_dir = args.out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.build = ""
        self.records = {}

    def reset(self):
        for _, serial, _ in BOARDS:
            run([str(PICOTOOL), "reboot", "-f", "--ser", serial], 30.0)
        time.sleep(self.args.reset_settle_s)

    def discover(self):
        builds = set()
        for port, expected, _ in BOARDS:
            idn = scpi_retry(port, "*IDN?")
            build = scpi_retry(port, "SYSTem:FW:BUILD?")
            if expected not in idn:
                raise RuntimeError(f"{port}: expected {expected}, got {idn}")
            builds.add(build.strip('"'))
        if len(builds) != 1:
            raise RuntimeError(f"mixed builds: {builds}")
        self.build = next(iter(builds))

    def seat(self):
        for port, _, _ in BOARDS:
            scpi(port, "SYSTem:TDMA:SEAT:ROTation 1,2,7")

    def start_ring(self, name):
        command = [
            PYTHON, str(TRN03),
            "--board-id", "0010071E65B5CB38",
            "--board-id", "FB276192BEF9CCE1",
            "--board-id", "2BD5090FE009FA2A",
            "--board-id", "A1E549202D18ED6A",
            "--config", str(self.args.matrix),
            "--expected-build", self.build,
            "--level", "7", "--cycles", "4096",
            "--stage", "process-image",
            "--clock-evidence", "disabled",
            "--leave-running", "--diagnostic-continue",
            "--no-record", "--no-t2",
            "--window-s", "2.0",
            "--sample-interval-s", "0.25",
            "--startup-timeout-s", "3.0", "--startup-stable-samples", "3",
            "--startup-poll-interval-s", "0.05",
            "--out-dir", str(self.out_dir / name / "ring"),
        ]
        result = run(command, self.args.ring_timeout_s)
        (self.out_dir / name / "ring.log").write_text(
            result.stdout, encoding="utf-8")
        if result.returncode != 0:
            raise RuntimeError(f"ring start failed rc={result.returncode}")

    def stop_ring(self):
        for port, _, _ in BOARDS:
            try:
                scpi(port, "SYSTem:TDMA:RING:STOP", timeout=3.0)
            except Exception:  # noqa: BLE001
                pass
        time.sleep(1.0)

    def ctrl(self, port):
        v = values(scpi_retry(port, "SYSTem:CTRL:STATus?"))
        if len(v) < 13:
            raise RuntimeError(f"{port}: ctrl parse failed: {v}")
        return {"state": v[2], "reason": v[10]}

    def txn(self, port):
        v = values(scpi_retry(port, "SYSTem:REFMEM:CONFig:STATus?"))
        return {"active": v[0], "window": v[1]}

    def target_txn(self, command):
        result = {}
        for port in self.args.targets:
            result[port] = values(scpi(port, command))[0]
        return result

    def stage(self, mask, node_id):
        return scpi(self.args.source_port,
                    f"SYSTem:CTRL:EXECute 2,{mask},{mask},"
                    f"{node_id},1,0,0,0,0,0")

    def step(self, name, expect, actual):
        passed = expect(actual)
        self.records[name] = {"actual": actual, "passed": passed}
        print(json.dumps({"step": name, "passed": passed, "actual": actual},
                         ensure_ascii=False))
        if not passed:
            raise RuntimeError(f"step {name} failed: {actual}")

    def run_ring_stage(self, name, mask, node_id):
        queued = self.stage(mask, node_id)
        self.start_ring(name)
        time.sleep(1.0)
        result = {port: self.ctrl(port) for port in self.args.targets}
        self.stop_ring()
        return queued, result

    def run(self):
        self.reset()
        self.discover()
        self.seat()
        mask = self.args.mask
        targets = self.args.targets

        self.step("begin_targets", lambda v: all(x == 1 for x in v.values()),
                  self.target_txn("SYSTem:REFMEM:CONFig:BEGIN"))
        self.step("targets_begin_status",
                  lambda v: all(x["active"] == 1 and x["window"] == 1
                                for x in v.values()),
                  {p: self.txn(p) for p in targets})

        queued, result = self.run_ring_stage("stage_valid", mask, 1)
        self.step("valid_queued", lambda v: "QUEUED" in v, queued)
        self.step("valid_passes_gate",
                  lambda v: all(x["reason"] != 7 for x in v.values()), result)

        self.step("commit_targets", lambda v: all(x == 1 for x in v.values()),
                  self.target_txn("SYSTem:REFMEM:CONFig:COMMit"))
        self.step("targets_commit_status",
                  lambda v: all(x["active"] == 0 and x["window"] == 0
                                for x in v.values()),
                  {p: self.txn(p) for p in targets})

        queued, result = self.run_ring_stage("stage_after_commit", mask, 1)
        self.step("after_commit_queued", lambda v: "QUEUED" in v, queued)
        self.step("after_commit_rejected",
                  lambda v: all(x["state"] == 5 and x["reason"] == 7
                                for x in v.values()), result)

        self.step("begin_for_abort", lambda v: all(x == 1 for x in v.values()),
                  self.target_txn("SYSTem:REFMEM:CONFig:BEGIN"))
        queued, result = self.run_ring_stage("stage_invalid", mask, 99)
        self.step("invalid_queued", lambda v: "QUEUED" in v, queued)
        self.step("invalid_business_reason",
                  lambda v: all(x["state"] == 5 and x["reason"] != 7
                                for x in v.values()), result)
        self.step("abort_targets", lambda v: all(x == 0 for x in v.values()),
                  self.target_txn("SYSTem:REFMEM:CONFig:ABORt"))
        self.step("targets_abort_status",
                  lambda v: all(x["active"] == 0 and x["window"] == 1
                                for x in v.values()),
                  {p: self.txn(p) for p in targets})
        queued, result = self.run_ring_stage("stage_after_abort", mask, 1)
        self.step("after_abort_queued", lambda v: "QUEUED" in v, queued)
        self.step("after_abort_passes_gate",
                  lambda v: all(x["reason"] != 7 for x in v.values()), result)

        for port in targets:
            scpi(port, "SYSTem:REFMEM:IDLE:WINDow 1")

        report = {
            "build": self.build,
            "mask": mask,
            "targets": targets,
            "records": self.records,
            "passed": all(r["passed"] for r in self.records.values()),
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }
        (self.out_dir / "report.json").write_text(
            json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        lines = ["# N4.3 remote config transaction HIL", "",
                 f"- build: `{self.build}`", f"- mask: `{mask}`",
                 f"- targets: `{targets}`", "",
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
    parser.add_argument("--source-port", default="COM5")
    parser.add_argument("--mask", type=int, default=2)
    parser.add_argument("--targets", nargs="+", default=["COM4"])
    parser.add_argument("--matrix", type=Path,
                        default=ROOT / "out/HardwareAcceptance/20261022/"
                        "p3-173429/trn03-matrix.json")
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "out/refmem_remote_txn_hil" /
                        datetime.now().strftime("%Y%m%d_%H%M%S"))
    parser.add_argument("--ring-timeout-s", type=float, default=180.0)
    parser.add_argument("--reset-settle-s", type=float, default=8.0)
    args = parser.parse_args()
    try:
        return RemoteTxnHil(args).run()
    except Exception as exc:  # noqa: BLE001
        print(json.dumps({"error": str(exc)}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
