#!/usr/bin/env python3
"""REFTMEM_CMD reply return-path HIL: target ACK/NACK back to the origin.

One command is queued before the process-image ring starts (SCPI is not needed
while the ring owns the wire), the ring carries the fragmented command to every
target, each target publishes its terminal result on its own process-image ACK
seat, and the origin folds those replies into its REFMEM node control record.

Verification (positive mode):
  * every selected target NODE:CONTrol? shows ACKED for the queued seq;
  * the origin CTRL:STATus? shows ACKED with acked_mask covering required;
  * the origin CMD:REPLy? reports rx/applied counters.

Verification (--expect-nack):
  * every selected target shows NACKED with the RUN config-gate reason;
  * the origin aggregate shows NACKED with the same reason.
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
    ("COM5", "0010071E65B5CB38", "reference", 0),
    ("COM4", "FB276192BEF9CCE1", "target", 1),
    ("COM3", "2BD5090FE009FA2A", "target", 2),
    ("COM6", "A1E549202D18ED6A", "target", 3),
]
SLOT_BY_PORT = {port: slot for port, _, _, slot in BOARDS}

COMMAND_STATE_IDLE = 0
COMMAND_STATE_POSTED = 1
COMMAND_STATE_TAKEN = 2
COMMAND_STATE_BUSY = 3
COMMAND_STATE_ACKED = 4
COMMAND_STATE_NACKED = 5
COMMAND_STATE_TIMED_OUT = 6
STATE_NAMES = {
    COMMAND_STATE_IDLE: "IDLE",
    COMMAND_STATE_POSTED: "POSTED",
    COMMAND_STATE_TAKEN: "TAKEN",
    COMMAND_STATE_BUSY: "BUSY",
    COMMAND_STATE_ACKED: "ACKED",
    COMMAND_STATE_NACKED: "NACKED",
    COMMAND_STATE_TIMED_OUT: "TIMED_OUT",
}
NACK_CONFIG_VALIDATION_FAILED = 13

CTRL_STATUS_FIELDS = [
    "control_seq", "control_crc32", "state", "command_seq", "target_mask",
    "required_mask", "ack_flags", "nack_flags", "busy_flags", "timeout_flags",
    "last_reason", "last_reason_slot", "last_completed_seq",
]
NODE_CONTROL_FIELDS = [
    "node_id", "control_seq", "control_crc32", "state", "command_seq",
    "source_node", "target_mask", "required_mask", "command_type",
    "command_class", "ack_flags", "nack_flags", "busy_flags", "timeout_flags",
    "last_reason", "last_reason_slot", "evidence_index", "last_completed_seq",
    "run_id", "issue_epoch", "payload_crc32",
]
CMD_REPLY_FIELDS = [
    "rx_count", "applied_count", "stale_count", "bad_target_count",
    "duplicate_count", "armed_count", "published_count", "publish_remaining",
    "pending_seq", "aggregate_state", "acked_mask", "nacked_mask",
    "last_slot", "last_state", "last_reason",
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


def scpi_retry(port: str, command: str, attempts: int = 5,
               delay_s: float = 0.6) -> str:
    last = ""
    for attempt in range(attempts):
        try:
            last = scpi(port, command)
            if last and last != "<timeout>":
                return last
        except Exception as exc:  # noqa: BLE001 - retry transient boot/port
            last = f"<error:{exc}>"
        time.sleep(delay_s)
    raise RuntimeError(f"{port}: no response for {command!r}: {last}")


def parse_fields(text: str, fields: list[str]) -> dict[str, int]:
    values = [int(item.strip(), 0) for item in text.split(",")]
    if len(values) < len(fields):
        raise ValueError(f"expected {len(fields)} fields, got {len(values)}: {text}")
    return {name: values[index] for index, name in enumerate(fields)}


def read_node_control(port: str) -> dict[str, int] | None:
    """Read the per-board control record; None means "no valid record yet".

    A fresh node has control_seq=0 and the firmware answers the query with a
    SCPI error (no data), which the query tool reports as <timeout>.  That is
    a legitimate empty state, not a transport failure, so it must not be
    retried as if the board were unresponsive."""
    text = scpi(port, "SYSTem:REFMEM:NODE:CONTrol?", timeout=3.0)
    if not text or text == "<timeout>" or "error" in text.lower():
        return None
    return parse_fields(text, NODE_CONTROL_FIELDS)


def read_ctrl_status(port: str) -> dict[str, int] | None:
    text = scpi(port, "SYSTem:CTRL:STATus?", timeout=3.0)
    if not text or text == "<timeout>" or "error" in text.lower():
        return None
    return parse_fields(text, CTRL_STATUS_FIELDS)


def read_cmd_reply(port: str) -> dict[str, int]:
    return parse_fields(scpi_retry(port, "SYSTem:REFMEM:CMD:REPLy?", attempts=3),
                        CMD_REPLY_FIELDS)


def state_name(value: int) -> str:
    return STATE_NAMES.get(value, f"UNKNOWN({value})")


class Workflow:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.out_dir = args.out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.steps: list[dict] = []
        self.build = ""
        self.queued_seq = 0
        self.pre: dict[str, dict[str, int]] = {}
        self.post: dict[str, dict[str, int]] = {}
        self.reply: dict[str, dict[str, int]] = {}

    def step(self, name, fn):
        started = time.time()
        entry = {"name": name, "started_at_utc":
                 datetime.now(timezone.utc).isoformat(), "passed": False,
                 "evidence": None, "error": None}
        try:
            entry["evidence"] = fn()
            entry["passed"] = True
            print(f"PASS {name}")
        except Exception as exc:  # noqa: BLE001 - report then re-raise
            entry["error"] = f"{type(exc).__name__}: {exc}"
            print(f"FAIL {name}: {entry['error']}")
            self.steps.append(entry)
            self.write_report()
            raise
        finally:
            entry["elapsed_s"] = round(time.time() - started, 3)
            if not self.steps or self.steps[-1] is not entry:
                self.steps.append(entry)

    def reset_boards(self):
        if not PICOTOOL.is_file():
            raise FileNotFoundError(f"picotool not found: {PICOTOOL}")
        result = {}
        for _, serial, _, _ in BOARDS:
            out = run([str(PICOTOOL), "reboot", "-f", "--ser", serial], 30.0)
            result[serial] = out.stdout.strip()
        time.sleep(self.args.reset_settle_s)
        return result

    def discover(self):
        found = {}
        for port, expected, role, slot in BOARDS:
            idn = scpi_retry(port, "*IDN?")
            build = scpi_retry(port, "SYSTem:FW:BUILD?")
            if expected not in idn:
                raise RuntimeError(f"{port}: expected {expected}, got {idn}")
            found[expected] = {"port": port, "role": role, "slot": slot,
                               "idn": idn, "build": build.strip('"')}
        builds = {item["build"] for item in found.values()}
        if len(builds) != 1:
            raise RuntimeError(f"mixed builds: {builds}")
        self.build = next(iter(builds))
        return found

    def seat(self):
        return {port: scpi(port, "SYSTem:TDMA:SEAT:ROTation 1,2,7")
                for port, _, _, _ in BOARDS}

    def target_ports(self):
        return [port for port, _, role, slot in BOARDS
                if role == "target"
                and (self.args.target_mask & (1 << slot))]

    def queue(self):
        for port, _, _, _ in BOARDS:
            try:
                self.pre[port] = {
                    "node": read_node_control(port),
                    "reply": read_cmd_reply(port),
                }
            except Exception as exc:  # noqa: BLE001 - pre-state is evidence only
                self.pre[port] = {"error": str(exc)}
        if self.args.expect_nack:
            # The same staging candidate succeeds while STOPPED; in RUN the
            # target config gate must deny it (reason 13) and that NACK has to
            # reach the origin through the reply path.
            command = (
                "SYSTem:CTRL:EXECute REFMEM_LOAD_NODE,"
                f"{self.args.target_mask},{self.args.required_mask},"
                "0,1,1,1,1,0,0")
        else:
            command = (
                "SYSTem:CTRL:EXECute REFMEM_MAINTENANCE_SELF_CHECK,"
                f"{self.args.target_mask},{self.args.required_mask},0")
        response = scpi(self.args.source_port, command)
        pieces = [piece.strip().strip('"') for piece in response.split(",")]
        if not pieces or pieces[0] != "QUEUED":
            raise RuntimeError(f"command not queued: {response}")
        self.queued_seq = int(pieces[1], 0) if len(pieces) > 1 else 0
        return {"command": command, "response": response,
                "queued_seq": self.queued_seq}

    def start_ring(self):
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

    def read_statuses(self):
        time.sleep(self.args.status_settle_s)
        for port, _, _, _ in BOARDS:
            self.post[port] = {
                "node": read_node_control(port),
                "ctrl": read_ctrl_status(port),
                "reply": read_cmd_reply(port),
            }
        return {port: self.post[port] for port, _, _, _ in BOARDS}

    def verify(self):
        target_ports = self.target_ports()
        source = self.post[self.args.source_port]
        expected = (COMMAND_STATE_NACKED if self.args.expect_nack
                    else COMMAND_STATE_ACKED)
        errors = []
        if self.queued_seq == 0:
            errors.append("queued sequence not reported")
        if not source["ctrl"]:
            errors.append("origin has no control record")
            return {"passed": False, "errors": errors,
                    "expected_state": state_name(expected),
                    "targets": target_ports, "queued_seq": self.queued_seq}
        if source["ctrl"]["command_seq"] != self.queued_seq:
            errors.append(
                f"origin command_seq {source['ctrl']['command_seq']} "
                f"!= queued {self.queued_seq}")
        if source["ctrl"]["state"] != expected:
            errors.append(
                f"origin state {state_name(source['ctrl']['state'])} "
                f"!= {state_name(expected)}")
        required = self.args.required_mask
        if not self.args.expect_nack:
            if (source["ctrl"]["ack_flags"] & required) != required:
                errors.append(
                    f"origin acked_mask {source['ctrl']['ack_flags']:#x} "
                    f"does not cover required {required:#x}")
            if source["reply"]["rx_count"] < len(target_ports):
                errors.append(
                    f"origin rx_count {source['reply']['rx_count']} < "
                    f"targets {len(target_ports)}")
            if source["reply"]["applied_count"] < len(target_ports):
                errors.append(
                    f"origin applied_count {source['reply']['applied_count']} "
                    f"< targets {len(target_ports)}")
        else:
            if source["ctrl"]["last_reason"] != NACK_CONFIG_VALIDATION_FAILED:
                errors.append(
                    f"origin reason {source['ctrl']['last_reason']} "
                    f"!= {NACK_CONFIG_VALIDATION_FAILED}")
            if (source["ctrl"]["nack_flags"] & required) != required:
                errors.append(
                    f"origin nacked_mask {source['ctrl']['nack_flags']:#x} "
                    f"does not cover required {required:#x}")
            if source["reply"]["applied_count"] < len(target_ports):
                errors.append(
                    f"origin applied_count {source['reply']['applied_count']} "
                    f"< targets {len(target_ports)}")

        for port in target_ports:
            row = self.post[port]["node"]
            if not row:
                errors.append(f"{port}: no control record")
                continue
            if row["command_seq"] != self.queued_seq:
                errors.append(
                    f"{port}: command_seq {row['command_seq']} != "
                    f"{self.queued_seq}")
            own_bit = 1 << SLOT_BY_PORT[port]
            if self.args.expect_nack:
                if row["state"] != COMMAND_STATE_NACKED:
                    errors.append(
                        f"{port}: state {state_name(row['state'])} "
                        f"!= NACKED")
                if row["last_reason"] != NACK_CONFIG_VALIDATION_FAILED:
                    errors.append(
                        f"{port}: reason {row['last_reason']} "
                        f"!= {NACK_CONFIG_VALIDATION_FAILED}")
            elif len(target_ports) == 1:
                # Single required target: its own record must reach ACKED.
                if row["state"] != COMMAND_STATE_ACKED:
                    errors.append(
                        f"{port}: state {state_name(row['state'])} "
                        f"!= ACKED")
                if (row["ack_flags"] & own_bit) == 0:
                    errors.append(f"{port}: own ack bit not set")
            else:
                # Multi-target: the target record is the per-command view, so
                # it reports its own ACK while remaining TAKEN until all
                # required targets complete; the origin owns the aggregate.
                if (row["ack_flags"] & own_bit) == 0:
                    errors.append(f"{port}: own ack bit not set")
                if row["state"] not in (COMMAND_STATE_ACKED,
                                        COMMAND_STATE_TAKEN):
                    errors.append(
                        f"{port}: state {state_name(row['state'])} "
                        f"is not ACKED/TAKEN")
        return {
            "passed": not errors,
            "errors": errors,
            "expected_state": state_name(expected),
            "targets": target_ports,
            "queued_seq": self.queued_seq,
            "source_ctrl": source["ctrl"],
            "source_reply": source["reply"],
            "target_nodes": {port: self.post[port]["node"]
                             for port in target_ports},
        }

    def write_report(self):
        verification = {}
        for entry in self.steps:
            if entry["name"].startswith("verify"):
                verification = entry.get("evidence") or {}
        report = {
            "schema": "HAOFV_REFTMEM_CMD_REPLY_HIL_V1",
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "build": self.build or None,
            "mode": "nack" if self.args.expect_nack else "ack",
            "target_mask": self.args.target_mask,
            "required_mask": self.args.required_mask,
            "queued_seq": self.queued_seq,
            "steps": self.steps,
            "pre": self.pre,
            "post": self.post,
            "verification": verification,
            "passed": bool(verification.get("passed")) and all(
                step["passed"] for step in self.steps),
        }
        (self.out_dir / "report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
        lines = [
            "# REFMEM_CMD reply return-path HIL",
            "",
            f"- build: `{report['build']}`",
            f"- mode: **{report['mode']}**",
            f"- target_mask: `0x{report['target_mask']:02X}`",
            f"- required_mask: `0x{report['required_mask']:02X}`",
            f"- queued_seq: `{report['queued_seq']}`",
            f"- passed: **{report['passed']}**",
            "",
            "## Steps",
            "",
            "| step | result | elapsed_s |",
            "|---|---|---|",
        ]
        for step in self.steps:
            lines.append(f"| {step['name']} | "
                         f"{'PASS' if step['passed'] else 'FAIL'} | "
                         f"{step.get('elapsed_s', 0)} |")
        lines += ["", "## Origin", ""]
        if self.post.get(self.args.source_port):
            row = self.post[self.args.source_port]
            if row.get("ctrl"):
                lines.append("- ctrl: " + ", ".join(
                    f"{key}={value}" for key, value in row["ctrl"].items()))
            else:
                lines.append("- ctrl: no record")
            if row.get("reply"):
                lines.append("- reply: " + ", ".join(
                    f"{key}={value}" for key, value in row["reply"].items()))
            else:
                lines.append("- reply: no data")
        lines += ["", "## Targets", ""]
        for port in verification.get("targets", []):
            row = self.post[port]["node"]
            if row:
                lines.append(f"- `{port}`: " + ", ".join(
                    f"{key}={value}" for key, value in row.items()))
            else:
                lines.append(f"- `{port}`: no control record")
        if verification.get("errors"):
            lines += ["", "## Errors", ""]
            lines += [f"- {error}" for error in verification["errors"]]
        (self.out_dir / "REPORT.md").write_text("\n".join(lines) + "\n",
                                                encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="REFTMEM_CMD reply return-path four-board HIL")
    parser.add_argument("--matrix", type=Path,
                        default=ROOT / "out/HardwareAcceptance/20261022/"
                        "p3-173429/trn03-matrix.json")
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--source-port", default="COM5")
    parser.add_argument("--target-mask", type=lambda value: int(value, 0),
                        default=0x0E)
    parser.add_argument("--required-mask", type=lambda value: int(value, 0),
                        default=0x0E)
    parser.add_argument("--expect-nack", action="store_true")
    parser.add_argument("--no-reset", action="store_true")
    parser.add_argument("--reset-settle-s", type=float, default=8.0)
    parser.add_argument("--ring-timeout-s", type=float, default=180.0)
    parser.add_argument("--status-settle-s", type=float, default=3.0)
    args = parser.parse_args()
    if args.out_dir is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        args.out_dir = ROOT / "out" / "refmem_cmd_reply" / stamp
    if args.target_mask == 0 or (args.required_mask & ~args.target_mask) != 0:
        raise SystemExit("invalid masks")
    workflow = Workflow(args)
    try:
        if not args.no_reset:
            workflow.step("reset_boards", workflow.reset_boards)
        workflow.step("discover_boards", workflow.discover)
        workflow.step("set_second_class_seat", workflow.seat)
        workflow.step("queue_control_command", workflow.queue)
        workflow.step("start_process_image_ring", workflow.start_ring)
        workflow.step("read_statuses", workflow.read_statuses)
        workflow.step("verify_reply_return", workflow.verify)
    except Exception:  # noqa: BLE001 - report is already written
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
