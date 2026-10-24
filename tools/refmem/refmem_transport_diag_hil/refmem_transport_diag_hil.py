#!/usr/bin/env python3
"""N3.2.1 maintenance-stream diagnostics HIL (1 / 2 / 7-frame commands).

For each command size the tool queues exactly one control frame before the
process-image ring starts, then reads the read-only transport counters from
every board.  It does not change transport behaviour; it only localizes where a
maintenance chunk is lost:

  queue depth / enqueued / dequeued / full
  callback attempts / ok / launched / dropped-unlaunched
  emitted sequence new / repeated
  follower rx polls / seen / accepted / dedup-sequence / bad-mailbox
  stream rx_chunk_rejects and fragment counters

Cases:
  1-frame : T2_STATIC 16 B (0x53)
  2-frame : REFMEM_CMD 56 B (0x52)
  7-frame : CTRL:EXECute self-check 196 B (fragmented 0x55)

The 7-frame case can be repeated (--repeat-7) to capture the intermittent
startup-window loss.
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
    ("COM5", "0010071E65B5CB38", 0),
    ("COM4", "FB276192BEF9CCE1", 1),
    ("COM3", "2BD5090FE009FA2A", 2),
    ("COM6", "A1E549202D18ED6A", 3),
]

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
    "t2_dynamic_applied", "t2_dynamic_last_target", "t2_dynamic_last_run",
    "t2_dynamic_last_config", "t2_dynamic_last_value",
    "frag_ok", "frag_reject", "frag_complete", "frag_submit_fail",
    "frag_pending", "frag_pending_index", "frag_pending_count",
    "queue_depth", "queue_enqueued", "queue_dequeued", "queue_full",
    "rx_chunk_rejects",
    "inject_launched", "inject_dropped_unlaunched",
    "inject_new_sequence", "inject_repeated_sequence",
    "inject_last_sequence", "beacon_frames",
    "rx_polls", "rx_seen_chunks", "rx_accepted_chunks",
    "rx_dedup_sequence", "rx_bad_mailbox", "rx_last_sequence",
    "rx_sequence_cursor", "rx_restarts", "rx_duplicate_chunks",
    "tx_awaiting_ack", "tx_ack_ok", "tx_retries", "tx_ack_fail",
    "tx_dropped_chunks",
]

# Expected maintenance chunks for each case.  The raw paths submit one 80 B
# stream frame per command; the fragmented path packs 8 x 9 B records into one
# 72 B stream frame (8 chunks of 10 B).
CASES = [
    {"name": "raw16", "kind": "t2_static", "frames": 1, "chunks": 2},
    {"name": "raw56", "kind": "refmem_cmd", "frames": 1, "chunks": 6},
    {"name": "raw56_x2", "kind": "refmem_cmd_x2", "frames": 2, "chunks": 12},
    {"name": "exec196", "kind": "exec_self_check", "frames": 7, "chunks": 56},
    {"name": "queue_full", "kind": "queue_full", "frames": 0, "chunks": 0},
]


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


def scpi_retry(port: str, command: str, attempts: int = 4,
               delay_s: float = 0.6) -> str:
    last = ""
    for _ in range(attempts):
        try:
            last = scpi(port, command)
            if last and last != "<timeout>":
                return last
        except Exception as exc:  # noqa: BLE001 - retry transient boot/port
            last = f"<error:{exc}>"
        time.sleep(delay_s)
    raise RuntimeError(f"{port}: no response for {command!r}: {last}")


def parse_status(text: str) -> dict[str, int]:
    values = [int(item.strip(), 0) for item in text.split(",")]
    if len(values) < len(FIELDS):
        raise ValueError(f"status has {len(values)} fields, expected {len(FIELDS)}")
    return {name: values[index] for index, name in enumerate(FIELDS)}


def make_refmem_cmd(target_mask: int) -> str:
    wire = bytearray.fromhex(BASE_WIRE_HEX)
    wire[12:16] = struct.pack("<I", target_mask)
    wire[52:56] = struct.pack("<I", zlib.crc32(bytes(wire[:52])) & 0xFFFFFFFF)
    return wire.hex().upper()


def make_t2_static(target_mask: int, config_id: int = 7,
                   value: int = 11) -> str:
    return struct.pack("<HBBIII", 0x5453, 1, 0, target_mask,
                       config_id, value).hex().upper()


class DiagHil:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.out_dir = args.out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.build = ""
        self.records: list[dict] = []

    def reset_boards(self) -> None:
        if not PICOTOOL.is_file():
            raise FileNotFoundError(f"picotool not found: {PICOTOOL}")
        for _, serial, _ in BOARDS:
            run([str(PICOTOOL), "reboot", "-f", "--ser", serial], 30.0)
        time.sleep(self.args.reset_settle_s)

    def discover(self) -> None:
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

    def seat(self) -> None:
        for port, _, _ in BOARDS:
            scpi(port, "SYSTem:TDMA:SEAT:ROTation 1,2,7")

    def queue(self, case: dict) -> dict:
        if case["kind"] == "t2_static":
            response = scpi(
                self.args.source_port,
                f'SYSTem:REFMEM:CMD:SUBmit:TYPe {0x53},"{make_t2_static(self.args.target_mask)}"')
        elif case["kind"] == "refmem_cmd":
            response = scpi(
                self.args.source_port,
                f'SYSTem:REFMEM:CMD:SUBmit:TYPe {0x52},"{make_refmem_cmd(self.args.target_mask)}"')
        elif case["kind"] == "refmem_cmd_x2":
            first = scpi(
                self.args.source_port,
                f'SYSTem:REFMEM:CMD:SUBmit:TYPe {0x52},"{make_refmem_cmd(self.args.target_mask)}"')
            second = scpi(
                self.args.source_port,
                f'SYSTem:REFMEM:CMD:SUBmit:TYPe {0x52},"{make_refmem_cmd(self.args.target_mask)}"')
            response = f"{first};{second}"
        elif case["kind"] == "queue_full":
            responses = []
            for _ in range(9):
                try:
                    responses.append(scpi(
                        self.args.source_port,
                        f'SYSTem:REFMEM:CMD:SUBmit:TYPe {0x52},"{make_refmem_cmd(self.args.target_mask)}"',
                        timeout=3.0))
                except Exception as exc:  # noqa: BLE001 - rejection is expected
                    responses.append(f"<error:{exc}>")
            if "QUEUED" not in responses[0]:
                raise RuntimeError(f"first queue submission failed: {responses[0]}")
            return {
                "response": responses,
                "rejected": sum(1 for item in responses
                                if item == "<timeout>" or
                                "REJECTED" in item or
                                item.startswith("<error")),
            }
        else:
            response = scpi(
                self.args.source_port,
                "SYSTem:CTRL:EXECute REFMEM_MAINTENANCE_SELF_CHECK,"
                f"{self.args.target_mask},{self.args.target_mask},0")
            if self.args.drop > 0:
                scpi(self.args.source_port,
                     f"SYSTem:REFMEM:CMD:DROP {self.args.drop}")
        if "QUEUED" not in response:
            raise RuntimeError(f"command not queued: {response}")
        return {"response": response, "drop": self.args.drop}

    def start_ring(self, case: dict) -> None:
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
            "--out-dir", str(self.out_dir / case["name"] / "ring"),
        ]
        result = run(command, self.args.ring_timeout_s)
        (self.out_dir / case["name"] / "ring.log").write_text(
            result.stdout, encoding="utf-8")
        if result.returncode != 0:
            raise RuntimeError(f"ring start failed rc={result.returncode}")

    def read_statuses(self) -> dict[str, dict[str, int]]:
        time.sleep(self.args.status_settle_s)
        statuses = {}
        for port, _, _ in BOARDS:
            statuses[port] = parse_status(
                scpi_retry(port, "SYSTem:REFMEM:CMD:STATus?"))
        return statuses

    def read_source_ctrl(self) -> dict[str, int] | None:
        text = scpi(self.args.source_port, "SYSTem:CTRL:STATus?", timeout=3.0)
        if not text or text == "<timeout>" or "error" in text.lower():
            return None
        values = [int(item.strip(), 0) for item in text.split(",")]
        if len(values) < 13:
            return None
        return {
            "control_seq": values[0], "state": values[2],
            "command_seq": values[3], "target_mask": values[4],
            "required_mask": values[5], "ack_flags": values[6],
            "nack_flags": values[7], "last_reason": values[10],
        }

    def diagnose(self, case: dict, statuses: dict[str, dict[str, int]],
                 source_ctrl: dict[str, int] | None) -> dict:
        source = statuses[self.args.source_port]
        target_rows = {p: statuses[p] for p, _, _ in BOARDS
                       if p != self.args.source_port}
        targets = list(target_rows.values())
        verdict = []
        if source["inject_dropped_unlaunched"] > 0:
            verdict.append("H2/H3: injected chunks in frames that were not launched")
        if source["inject_repeated_sequence"] > 0:
            verdict.append("H1: emitted maintenance frames reused a transport sequence")
        if source["queue_depth"] > 0:
            verdict.append("H3: TX queue still holds frames (not drained)")
        if source["queue_full"] > 0:
            verdict.append("H3: TX queue rejected a frame (full)")
        if any(t["rx_bad_mailbox"] > 0 for t in targets):
            verdict.append("H4: follower rejected a maintenance mailbox")
        if any(t["rx_restarts"] > 0 for t in targets):
            verdict.append("H4: reassembler restarted on a new header (no permanent wedge)")
        success = False
        if case["kind"] == "t2_static":
            success = any(t["t2_static_ok"] >= 1 for t in targets)
        elif case["kind"] == "refmem_cmd":
            success = any(t["refmem_parser_ok"] >= 1 for t in targets)
        elif case["kind"] == "refmem_cmd_x2":
            success = any(t["refmem_parser_ok"] >= 2 for t in targets)
        elif case["kind"] == "queue_full":
            success = source["queue_full"] > 0
        else:
            success = (all(t["frag_complete"] >= 1 for t in targets) and
                       source_ctrl is not None and
                       source_ctrl.get("state") == 4)
        if not success and not verdict:
            if (case["kind"] == "exec_self_check" and
                    any(t["rx_restarts"] > 0 for t in targets) and
                    all(t["frag_ok"] >= 3 for t in targets)):
                verdict.append(
                    "N3.2.3: RX restarted, no permanent wedge; "
                    "missed frame needs N3.2.4 retry")
            else:
                verdict.append(
                    "delivery incomplete without a source-side loss signature")
        return {
            "case": case["name"],
            "expected_frames": case["frames"],
            "expected_chunks": case["chunks"],
            "success": success,
            "source": source,
            "source_ctrl": source_ctrl,
            "targets": target_rows,
            "target_rx_accepted": [t["rx_accepted_chunks"] for t in targets],
            "target_frag_complete": [t["frag_complete"] for t in targets],
            "verdict": verdict,
        }

    def run_case(self, case: dict, attempt: int) -> dict:
        self.reset_boards()
        self.discover()
        self.seat()
        queued = self.queue(case)
        self.start_ring(case)
        statuses = self.read_statuses()
        source_ctrl = self.read_source_ctrl()
        record = self.diagnose(case, statuses, source_ctrl)
        record["attempt"] = attempt
        record["queued"] = queued
        record["build"] = self.build
        self.records.append(record)
        print(json.dumps({
            "case": case["name"], "attempt": attempt,
            "success": record["success"],
            "source_inject_ok": record["source"]["inject_ok"],
            "source_launched": record["source"]["inject_launched"],
            "source_dropped": record["source"]["inject_dropped_unlaunched"],
            "source_repeated_seq": record["source"]["inject_repeated_sequence"],
            "target_frag_complete": record["target_frag_complete"],
            "source_state": (source_ctrl or {}).get("state"),
            "verdict": record["verdict"],
        }, ensure_ascii=False))
        return record

    def write_report(self) -> None:
        report = {
            "schema": "HAOFV_REFMEM_TRANSPORT_DIAG_HIL_V1",
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "build": self.build or None,
            "target_mask": self.args.target_mask,
            "records": self.records,
        }
        (self.out_dir / "report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
        lines = [
            "# REFMEM N3.2.1 transport diagnostics",
            "",
            f"- build: `{report['build']}`",
            f"- target_mask: `0x{report['target_mask']:02X}`",
            "",
            "| case | attempt | success | inject_ok | launched | dropped | repeated_seq | frag_complete | verdict |",
            "|---|---|---|---|---|---|---|---|---|",
        ]
        for record in self.records:
            source = record["source"]
            lines.append(
                f"| {record['case']} | {record['attempt']} | "
                f"{'yes' if record['success'] else 'no'} | "
                f"{source['inject_ok']} | {source['inject_launched']} | "
                f"{source['inject_dropped_unlaunched']} | "
                f"{source['inject_repeated_sequence']} | "
                f"{record['target_frag_complete']} | "
                f"{'; '.join(record['verdict']) or 'no loss signature'} |")
        (self.out_dir / "REPORT.md").write_text("\n".join(lines) + "\n",
                                                encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="N3.2.1 maintenance-stream diagnostics HIL")
    parser.add_argument("--matrix", type=Path,
                        default=ROOT / "out/HardwareAcceptance/20261022/"
                        "p3-173429/trn03-matrix.json")
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--source-port", default="COM5")
    parser.add_argument("--target-mask", type=lambda value: int(value, 0),
                        default=0x0E)
    parser.add_argument("--repeat-7", type=int, default=1)
    parser.add_argument("--drop", type=int, default=0,
                        help="drop N maintenance chunks before the ring starts")
    parser.add_argument("--only", default=None,
                        help="run only one case by name (raw16/raw56/raw56_x2/exec196)")
    parser.add_argument("--reset-settle-s", type=float, default=8.0)
    parser.add_argument("--ring-timeout-s", type=float, default=180.0)
    parser.add_argument("--status-settle-s", type=float, default=3.0)
    args = parser.parse_args()
    if args.out_dir is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        args.out_dir = ROOT / "out" / "refmem_transport_diag" / stamp
    hil = DiagHil(args)
    try:
        cases = [case for case in CASES
                 if args.only is None or case["name"] == args.only]
        if not cases:
            raise SystemExit(f"unknown case: {args.only}")
        for case in cases:
            repeats = args.repeat_7 if case["name"] == "exec196" else 1
            for attempt in range(1, repeats + 1):
                hil.run_case(case, attempt)
    except Exception as exc:  # noqa: BLE001 - report partial evidence
        hil.write_report()
        print(json.dumps({"passed": False, "error": f"{type(exc).__name__}: {exc}",
                          "report": str(hil.out_dir / "report.json")}))
        return 1
    hil.write_report()
    print(json.dumps({"passed": True,
                      "report": str(hil.out_dir / "report.json"),
                      "markdown": str(hil.out_dir / "REPORT.md")}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
