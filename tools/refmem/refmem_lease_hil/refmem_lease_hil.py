#!/usr/bin/env python3
"""N3.3.3 two-board lease HIL: A TAKE -> B CONFLICT -> A RELEASE -> B TAKE.

Boards: A=COM5 (slot 0, source), B=COM4 (slot 1, target).  The lease record
lives on B; A's TAKE/RELEASE travel over the process-image ring, B's TAKE is a
local CTRL:EXECute (target mask = B only).  The ring is stopped between the
release and the retake to prove the lease persists across ring stop->start
(the record is RAM, not reset).
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


def parse_values(text):
    values = []
    for item in text.split(","):
        item = item.strip().strip('"')
        try:
            values.append(int(item, 0))
        except ValueError:
            values.append(item)
    return values


class LeaseHil:
    def __init__(self, args):
        self.args = args
        self.out_dir = args.out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.build = ""
        self.records = {}

    def reset_boards(self):
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

    def lease(self, port):
        values = parse_values(
            scpi_retry(port, "SYSTem:REFMEM:LEASE?"))
        if len(values) < 6:
            raise RuntimeError(f"{port}: lease parse failed: {values}")
        return {
            "owner_slot": values[0], "epoch": values[1], "state": values[2],
            "expiry_ms": values[3], "lease_seq": values[4],
            "state_name": values[5],
        }

    def ctrl(self, port):
        values = parse_values(
            scpi_retry(port, "SYSTem:CTRL:STATus?"))
        if len(values) < 13:
            raise RuntimeError(f"{port}: ctrl parse failed: {values}")
        return {
            "control_seq": values[0], "state": values[2],
            "command_seq": values[3], "target_mask": values[4],
            "required_mask": values[5], "ack_flags": values[6],
            "nack_flags": values[7], "last_reason": values[10],
            "last_completed_seq": values[12],
        }

    def exec_lease(self, port, name, target_bit, epoch, ttl=None):
        # The catalog id is the stable selector; the generated name spelling
        # must match exactly, so use 5/6 (TAKE/RELEASE) instead.
        command_id = 5 if "TAKE" in name else 6
        args = f"{command_id},{target_bit},{target_bit},{epoch}"
        if ttl is not None:
            args += f",{ttl}"
        return scpi(port, f"SYSTem:CTRL:EXECute {args}")

    def step(self, name, expect, actual):
        passed = expect(actual)
        self.records[name] = {
            "expect": getattr(expect, "__doc__", "") or "",
            "actual": actual, "passed": passed,
        }
        print(json.dumps({"step": name, "passed": passed, "actual": actual},
                         ensure_ascii=False))
        if not passed:
            raise RuntimeError(f"step {name} failed: {actual}")

    def run(self):
        self.reset_boards()
        self.discover()
        self.seat()

        # 1) A takes B's lease over the ring.
        self.step("queue_take_a", lambda v: "QUEUED" in v,
                  self.exec_lease(self.args.source_port, "REFTMEM_LEASE_TAKE",
                                  self.args.target_bit, 1, 60000))
        self.start_ring("take_a")
        time.sleep(1.0)
        lease_b = self.lease(self.args.target_port)
        self.step("b_granted_to_a",
                  lambda v: v["state"] == 1 and v["owner_slot"] == 0 and
                  v["epoch"] == 1, lease_b)

        # 2) B's own take must conflict with A.
        self.step("queue_take_b_conflict", lambda v: "QUEUED" in v,
                  self.exec_lease(self.args.target_port, "REFTMEM_LEASE_TAKE",
                                  self.args.target_bit, 2, 60000))
        time.sleep(1.5)
        lease_b = self.lease(self.args.target_port)
        self.step("b_still_owned_by_a",
                  lambda v: v["state"] == 1 and v["owner_slot"] == 0,
                  lease_b)
        ctrl_b = self.ctrl(self.args.target_port)
        self.step("b_take_conflict_nack",
                  lambda v: v["state"] == 5 and v["last_reason"] == 6,
                  ctrl_b)

        # 3) Stop the ring; the lease must persist (RAM, no reset).
        self.stop_ring()
        lease_b = self.lease(self.args.target_port)
        self.step("lease_survives_ring_stop",
                  lambda v: v["state"] == 1 and v["owner_slot"] == 0,
                  lease_b)

        # 4) A releases over a second ring; B retakes locally.
        self.step("queue_release_a", lambda v: "QUEUED" in v,
                  self.exec_lease(self.args.source_port,
                                  "REFTMEM_LEASE_RELEASE",
                                  self.args.target_bit, 1))
        self.start_ring("release_a")
        time.sleep(1.0)
        lease_b = self.lease(self.args.target_port)
        self.step("b_free_after_release",
                  lambda v: v["state"] == 0, lease_b)

        self.step("queue_take_b", lambda v: "QUEUED" in v,
                  self.exec_lease(self.args.target_port, "REFTMEM_LEASE_TAKE",
                                  self.args.target_bit, 2, 60000))
        time.sleep(1.5)
        lease_b = self.lease(self.args.target_port)
        self.step("b_granted_epoch2",
                  lambda v: v["state"] == 1 and v["owner_slot"] == 1 and
                  v["epoch"] == 2, lease_b)
        ctrl_b = self.ctrl(self.args.target_port)
        self.step("b_take_acked", lambda v: v["state"] == 4, ctrl_b)

        self.stop_ring()

        report = {
            "build": self.build,
            "source_port": self.args.source_port,
            "target_port": self.args.target_port,
            "target_bit": self.args.target_bit,
            "records": self.records,
            "passed": all(r["passed"] for r in self.records.values()),
            "timestamp": datetime.now(timezone.utc).isoformat(),
        }
        (self.out_dir / "report.json").write_text(
            json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        lines = ["# N3.3.3 lease HIL", "",
                 f"- build: `{self.build}`", "",
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
    parser.add_argument("--target-port", default="COM4")
    parser.add_argument("--target-bit", type=int, default=2)
    parser.add_argument("--matrix", type=Path,
                        default=ROOT / "out/HardwareAcceptance/20261022/"
                        "p3-173429/trn03-matrix.json")
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "out/refmem_lease_hil" /
                        datetime.now().strftime("%Y%m%d_%H%M%S"))
    parser.add_argument("--ring-timeout-s", type=float, default=180.0)
    parser.add_argument("--reset-settle-s", type=float, default=8.0)
    args = parser.parse_args()
    try:
        return LeaseHil(args).run()
    except Exception as exc:  # noqa: BLE001
        print(json.dumps({"error": str(exc)}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
