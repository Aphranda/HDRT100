#!/usr/bin/env python3
"""REFTMEM_CMD over the TDMA second-class seat: stepwise HIL workflow + report.

This tool is deliberately host-only.  It resets the four boards, queues one
REFTMEM_CMD before the ring starts (so SCPI is not needed while the ring owns
core0), starts the ring without T2/VDC, then reads the read-only counters from
every board and writes a machine and human readable report.

The command is carried by the existing `tdma_maintenance_stream` fragmentation
path (22 B chunk / 10 B data / 80 B max); no new bus and no second command slot
are introduced.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
import time
import zlib
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
    ("COM5", "0010071E65B5CB38", "reference"),
    ("COM4", "FB276192BEF9CCE1", "target"),
    ("COM3", "2BD5090FE009FA2A", "target"),
    ("COM6", "A1E549202D18ED6A", "target"),
]

# Base frame from the verified 56 B REFMEM_CMD codec.  Offset 12 is
# target_mask, offset 52 is CRC32 over bytes 0..51.
BASE_WIRE_HEX = (
    "444D435201000000640000000200000002000000010000000100000000000000"
    "00000000A08601000000000003000200000000006685A4E7"
)

FIELDS = [
    "load_initialized", "submitted", "served", "rejected", "window_closed",
    "rx_events", "decoded", "posted", "acked", "tx_ok", "tx_fail",
    "tx_stage", "rx_seen", "rx_delivered", "rx_drop_count",
    "last_rx_packet_size", "last_rx_payload_class", "rx_config_control",
    "rx_process_image", "build_class", "build_size", "build_load",
    "inject_attempts", "inject_ok",
    "parser_registered", "parser_unknown", "refmem_parser_ok",
    "refmem_parser_reject", "t2_static_ok", "t2_static_reject",
    "t2_static_applied", "t2_static_last_config", "t2_static_last_value",
    "t2_dynamic_ok", "t2_dynamic_reject",
]


def make_wire(target_mask: int) -> str:
    wire = bytearray.fromhex(BASE_WIRE_HEX)
    if len(wire) != 56:
        raise ValueError("base REFMEM_CMD must be 56 bytes")
    wire[12:16] = struct.pack("<I", target_mask)
    wire[52:56] = struct.pack("<I", zlib.crc32(bytes(wire[:52])) & 0xFFFFFFFF)
    return wire.hex().upper()


def make_t2_static(target_mask: int, config_id: int = 7,
                   value: int = 11) -> str:
    wire = struct.pack("<HBBIII", 0x5453, 1, 0, target_mask,
                       config_id, value)
    return wire.hex().upper()


def make_t2_dynamic(target_mask: int, run_id: int = 42, config_id: int = 7,
                    value: int = 9) -> str:
    wire = struct.pack("<HBBIIIII", 0x5444, 1, 0, target_mask,
                       run_id, config_id, value, 0)
    return wire.hex().upper()


def run(cmd: list[str], timeout: float = 60.0) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd, cwd=ROOT, text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=timeout, check=False,
    )


def scpi(port: str, command: str, timeout: float = 10.0) -> str:
    result = run([PYTHON, str(SCPI), port, command, "--timeout",
                  str(timeout), "--settle", "0.25"], timeout + 5.0)
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError(f"{port}: empty SCPI response for {command!r}")
    line = lines[-1]
    if "=>" in line:
        line = line.split("=>", 1)[1].strip()
    return line.strip().strip('"')


def parse_status(text: str) -> dict[str, int]:
    values = [int(item.strip(), 0) for item in text.split(",")]
    if len(values) < len(FIELDS):
        raise ValueError(f"status has {len(values)} fields, expected {len(FIELDS)}")
    return {name: values[index] for index, name in enumerate(FIELDS)}


class Workflow:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.out_dir = args.out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.steps: list[dict] = []
        self.statuses: dict[str, dict[str, int]] = {}
        if args.payload_hex:
            self.wire = args.payload_hex.strip().upper()
        elif args.payload_type == 0x53:
            self.wire = make_t2_static(args.target_mask)
        elif args.payload_type == 0x54:
            self.wire = make_t2_dynamic(args.target_mask)
        else:
            self.wire = make_wire(args.target_mask)

    def step(self, name: str, fn) -> None:
        started = time.time()
        entry = {"name": name, "started_at_utc": datetime.now(timezone.utc).isoformat(),
                 "passed": False, "evidence": None, "error": None}
        try:
            entry["evidence"] = fn()
            entry["passed"] = True
            print(f"PASS {name}")
        except Exception as exc:  # report the failure, then stop unless --keep-going
            entry["error"] = f"{type(exc).__name__}: {exc}"
            print(f"FAIL {name}: {entry['error']}")
            if not self.args.keep_going:
                self.steps.append(entry)
                self.write_report()
                raise
        finally:
            entry["elapsed_s"] = round(time.time() - started, 3)
            if not self.steps or self.steps[-1] is not entry:
                self.steps.append(entry)

    def reset_boards(self) -> dict:
        if not PICOTOOL.is_file():
            raise FileNotFoundError(f"picotool not found: {PICOTOOL}")
        results = {}
        for _, serial, _ in BOARDS:
            result = run([str(PICOTOOL), "reboot", "-f", "--ser", serial], 30.0)
            results[serial] = result.stdout.strip()
        time.sleep(self.args.reset_settle_s)
        return results

    def discover(self) -> dict:
        found = {}
        for port, expected, role in BOARDS:
            idn = scpi(port, "*IDN?")
            build = scpi(port, "SYSTem:FW:BUILD?")
            if expected not in idn:
                raise RuntimeError(f"{port}: expected {expected}, got {idn}")
            found[expected] = {"port": port, "role": role, "idn": idn,
                               "build": build.strip('"')}
        builds = {item["build"] for item in found.values()}
        if len(builds) != 1:
            raise RuntimeError(f"mixed builds: {builds}")
        self.build = next(iter(builds))
        return found

    def seat(self) -> dict:
        result = {}
        for port, _, _ in BOARDS:
            result[port] = scpi(port, "SYSTem:TDMA:SEAT:ROTation 1,2,7")
        return result

    def queue(self) -> dict:
        if self.args.reset_before_queue:
            self.reset_boards()
            self.discover()
            self.seat()
        port = self.args.source_port
        response = scpi(
            port,
            f'SYSTem:REFMEM:CMD:SUBmit:TYPe {self.args.payload_type},'
            f'"{self.wire}"')
        if "QUEUED" not in response:
            raise RuntimeError(f"{port}: command not queued: {response}")
        status = scpi(port, "SYSTem:REFMEM:CMD:STATus?")
        parsed = parse_status(status)
        if parsed["submitted"] < 1:
            raise RuntimeError(f"{port}: submitted counter did not advance: {parsed}")
        self.statuses[port] = parsed
        return {"port": port, "type": self.args.payload_type,
                "wire": self.wire, "response": response, "status": parsed}

    def start_ring(self) -> dict:
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
            "--window-s", "0.5", "--sample-interval-s", "0.25",
            "--startup-timeout-s", "3.0", "--startup-stable-samples", "3",
            "--startup-poll-interval-s", "0.05",
            "--out-dir", str(self.out_dir / "ring"),
        ]
        result = run(command, self.args.ring_timeout_s)
        (self.out_dir / "ring.log").write_text(result.stdout, encoding="utf-8")
        if result.returncode != 0:
            raise RuntimeError(f"ring start failed rc={result.returncode}")
        return {"returncode": result.returncode,
                "log": str(self.out_dir / "ring.log")}

    def read_statuses(self) -> dict:
        time.sleep(self.args.status_settle_s)
        for port, _, _ in BOARDS:
            self.statuses[port] = parse_status(
                scpi(port, "SYSTem:REFMEM:CMD:STATus?"))
        return {port: self.statuses[port] for port, _, _ in BOARDS}

    def verify(self) -> dict:
        source = self.statuses[self.args.source_port]
        slot_by_port = {"COM5": 0, "COM4": 1, "COM3": 2, "COM6": 3}
        targets = [port for port, _, role in BOARDS
                   if role == "target" and
                   (self.args.target_mask & (1 << slot_by_port[port]))]
        errors = []
        if source["submitted"] < 1:
            errors.append("source submitted == 0")
        if source["tx_ok"] < 1:
            errors.append("source tx_ok == 0")
        if source["inject_attempts"] < 1:
            errors.append("source inject_attempts == 0")
        if source["parser_registered"] < 3:
            errors.append("source parser_registered < 3")
        for port in targets:
            row = self.statuses[port]
            if self.args.expect_reject:
                if self.args.payload_type == 0x52:
                    ok_field, reject_field = (
                        "refmem_parser_ok", "refmem_parser_reject")
                elif self.args.payload_type == 0x53:
                    ok_field, reject_field = (
                        "t2_static_ok", "t2_static_reject")
                elif self.args.payload_type == 0x54:
                    ok_field, reject_field = (
                        "t2_dynamic_ok", "t2_dynamic_reject")
                else:
                    ok_field, reject_field = ("rx_events", "parser_unknown")
                if row[reject_field] < 1:
                    errors.append(f"{port}: {reject_field} == 0")
                if row[ok_field] != 0:
                    errors.append(f"{port}: {ok_field} != 0")
                continue
            if self.args.payload_type == 0x52:
                fields = ("rx_events", "decoded", "posted", "acked")
            elif self.args.payload_type == 0x53:
                fields = ("t2_static_ok", "t2_static_applied")
            elif self.args.payload_type == 0x54:
                fields = ("t2_dynamic_ok",)
            else:
                fields = ("rx_events",)
            for field in fields:
                if row[field] < 1:
                    errors.append(f"{port}: {field} == 0")
        return {"passed": not errors, "errors": errors,
                "targets": targets,
                "source": {k: source[k] for k in FIELDS},
                "target_status": {p: self.statuses[p] for p in targets}}

    def write_report(self) -> None:
        verification = {}
        for entry in self.steps:
            if entry["name"].startswith("verify"):
                verification = entry.get("evidence") or {}
        report = {
            "schema": "HAOFV_REFTMEM_CMD_TDMA_WORKFLOW_V1",
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "build": getattr(self, "build", None),
            "wire": self.wire,
            "payload_type": self.args.payload_type,
            "target_mask": self.args.target_mask,
            "steps": self.steps,
            "statuses": {port: self.statuses.get(port) for port, _, _ in BOARDS},
            "verification": verification,
            "passed": bool(verification.get("passed")) and all(
                step["passed"] for step in self.steps),
        }
        (self.out_dir / "report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
        lines = [
            "# REFMEM_CMD TDMA workflow report",
            "",
            f"- build: `{report['build']}`",
            f"- payload_type: `0x{report['payload_type']:02X}`",
            f"- target_mask: `0x{report['target_mask']:02X}`",
            f"- wire: `{self.wire}`",
            f"- passed: **{report['passed']}**",
            "",
            "## Steps",
            "",
            "| step | result | elapsed_s | evidence |",
            "|---|---|---|---|",
        ]
        for step in self.steps:
            evidence = "" if step["evidence"] is None else str(
                step["evidence"]).replace("|", "\\|")
            lines.append(
                f"| {step['name']} | {'PASS' if step['passed'] else 'FAIL'} | "
                f"{step.get('elapsed_s', 0)} | {evidence[:300]} |")
        lines += ["", "## Status", ""]
        for port, _, _ in BOARDS:
            row = self.statuses.get(port)
            if row:
                lines.append(f"- `{port}`: " + ", ".join(
                    f"{key}={row[key]}" for key in FIELDS))
        if verification.get("errors"):
            lines += ["", "## Errors", ""]
            lines += [f"- {error}" for error in verification["errors"]]
        (self.out_dir / "REPORT.md").write_text(
            "\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="REFTMEM_CMD second-class TDMA HIL workflow and report")
    parser.add_argument("--matrix", type=Path,
                        default=ROOT / "out/HardwareAcceptance/20261022/"
                        "p3-173429/trn03-matrix.json")
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--source-port", default="COM5")
    parser.add_argument("--target-mask", type=lambda value: int(value, 0),
                        default=0x0F)
    parser.add_argument("--payload-type", type=lambda value: int(value, 0),
                        default=0x52,
                        help="second-class parser type: 0x52 REFMEM, "
                             "0x53 T2 static, 0x54 T2 dynamic")
    parser.add_argument("--payload-hex", default=None,
                        help="override encoded payload bytes")
    parser.add_argument("--expect-reject", action="store_true",
                        help="require the selected parser to reject on every target")
    parser.add_argument("--ring-timeout-s", type=float, default=180.0)
    parser.add_argument("--ring-export-timeout", type=float, default=120.0)
    parser.add_argument("--reset-settle-s", type=float, default=8.0)
    parser.add_argument("--status-settle-s", type=float, default=3.0)
    parser.add_argument("--reset-before-queue", action="store_true")
    parser.add_argument("--keep-going", action="store_true")
    parser.add_argument("--no-reset", action="store_true")
    args = parser.parse_args()
    if args.out_dir is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        args.out_dir = ROOT / "out" / "refmem_cmd_tdma" / stamp
    workflow = Workflow(args)
    try:
        if not args.no_reset:
            workflow.step("reset_boards", workflow.reset_boards)
        workflow.step("discover_boards", workflow.discover)
        workflow.step("set_second_class_seat", workflow.seat)
        workflow.step("queue_refmem_cmd", workflow.queue)
        workflow.step("start_ring_without_t2", workflow.start_ring)
        workflow.step("read_statuses", workflow.read_statuses)
        workflow.step("verify_all_target_ack", workflow.verify)
    except Exception:
        workflow.write_report()
        print(json.dumps({"passed": False,
                          "report": str(workflow.out_dir / "report.json")}))
        return 1
    workflow.write_report()
    report = json.loads((workflow.out_dir / "report.json").read_text(
        encoding="utf-8"))
    print(json.dumps({"passed": report["passed"],
                      "report": str(workflow.out_dir / "report.json"),
                      "markdown": str(workflow.out_dir / "REPORT.md")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
