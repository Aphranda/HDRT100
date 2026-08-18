#!/usr/bin/env python3
"""Validate two-board RefMem NodeLoad auto sync over TDMA/PIO transport.

The PC configures AUTO mode and submits local LOAD:NODE intents only. It does
not read frame hex from one board or inject frame data into the peer.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]

AUTO_FIELD_COUNT = 26
LOAD_STATUS_FIELD_COUNT = 24
TABLE_FIELD_COUNT = 18
CLAIM_FIELD_COUNT = 30
TDMA_STATUS_MIN_FIELD_COUNT = 24
AUTO_INTENT_RX_WINDOW = 2
ADAPTER_DUPLEX_HALF = 1
NODE_LOAD_TABLE_ID = 3
NODE_LOAD_TABLE_MASK = 1 << NODE_LOAD_TABLE_ID
NODE_LOAD_DELTA_FRAME_TYPE = 3
MIN_SYSTEM_ADAPTER_PROFILE = "min-system-gpio16-24"
IO_PREFLIGHT_ADAPTER_PROFILE = "io-preflight"


@dataclass(frozen=True)
class IoProfile:
    input_base: int
    input_count: int
    output_base: int
    output_count: int
    trig_in_pin: int
    rj45_in_pin: int
    trig_out_pin: int
    rj45_out_pin: int


@dataclass(frozen=True)
class AdapterPins:
    rx: int
    sck: int
    tx: int


@dataclass(frozen=True)
class AdapterPlan:
    uplink_duplex_mode: int
    uplink_adapter: AdapterPins | None
    downlink_duplex_mode: int
    downlink_adapter: AdapterPins | None


@dataclass
class LoadSpec:
    node_id: int
    instance_id: int
    role_mask: int
    persona_mask: int
    enabled: int
    required: int
    load_order: int

    def command(self) -> str:
        return (
            "SYSTem:REFMEM:LOAD:NODE "
            f"{self.node_id},{self.instance_id},{self.role_mask},"
            f"{self.persona_mask},{self.enabled},{self.required},{self.load_order}"
        )


@dataclass
class Record:
    board: str
    command: str
    response: str
    status: str
    reason: str


def read_serial_line(ser: serial.Serial, timeout_s: float) -> str:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        raw = bytearray()
        while time.monotonic() < deadline:
            ch = ser.read(1)
            if not ch:
                continue
            raw.extend(ch)
            if ch == b"\n":
                break
        if not raw:
            continue
        line = bytes(raw).decode("utf-8", errors="replace").strip()
        maybe_log = line[1:] if line.startswith('"[') else line
        if not line or maybe_log.startswith("[") or maybe_log.startswith("log:"):
            continue
        if line in {'"OK"', "OK", 'OK"'} or line.startswith('"OK[') or line.startswith("OK["):
            return '"OK"'
        return re.sub(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+.*$', "", line).strip()
    return "<timeout>"


def query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return read_serial_line(ser, timeout_s)


def try_query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    try:
        return query(ser, command, timeout_s)
    except serial.SerialException as exc:
        return f"<serial-error:{exc}>"


def parse_csv(response: str) -> list[str]:
    try:
        return next(csv.reader([response], skipinitialspace=True))
    except Exception:
        return []


def parse_int_fields(response: str, expected_count: int, label: str) -> list[int]:
    fields = parse_csv(response)
    if len(fields) != expected_count:
        raise AssertionError(f"{label} field_count={len(fields)} expected={expected_count}: {response!r}")
    try:
        return [int(field.strip().strip('"'), 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"{label} contains non-integer field: {response!r}") from exc


def parse_ok(response: str, label: str) -> None:
    fields = parse_csv(response)
    if not fields or fields[0].strip('"') != "OK":
        raise AssertionError(f"{label} did not return OK: {response!r}")


def parse_load_response(response: str, expected: LoadSpec) -> list[str]:
    fields = parse_csv(response)
    if len(fields) != LOAD_STATUS_FIELD_COUNT + 1:
        raise AssertionError(f"LOAD:NODE field_count={len(fields)} expected=25: {response!r}")
    if fields[0].strip('"') != "STAGED":
        raise AssertionError(f"LOAD:NODE was not staged: {response!r}")
    values = [int(field.strip().strip('"'), 0) for field in fields[1:23]]
    if values[14] != expected.node_id or values[15] != expected.instance_id:
        raise AssertionError(
            f"LOAD:NODE staged node/instance {values[14]}/{values[15]} "
            f"expected {expected.node_id}/{expected.instance_id}: {response!r}"
        )
    if values[21] != 0:
        raise AssertionError(f"LOAD:NODE last_error is non-zero: {response!r}")
    return fields


def load_status_values(response: str) -> list[int]:
    fields = parse_csv(response)
    if len(fields) != LOAD_STATUS_FIELD_COUNT:
        raise AssertionError(f"LOAD:STATus field_count={len(fields)} expected=24: {response!r}")
    return [int(field.strip().strip('"'), 0) for field in fields[:22]]


def auto_values(response: str) -> list[int]:
    return parse_int_fields(response, AUTO_FIELD_COUNT, "AUTO?")


def table_values(response: str) -> list[int]:
    return parse_int_fields(response, TABLE_FIELD_COUNT, "TABle?")


def claim_values(response: str) -> list[int]:
    return parse_int_fields(response, CLAIM_FIELD_COUNT, "CLAIM?")


def tdma_status_values(response: str) -> list[int]:
    fields = parse_csv(response)
    if len(fields) < TDMA_STATUS_MIN_FIELD_COUNT:
        raise AssertionError(
            f"TDMA:STATus field_count={len(fields)} expected>={TDMA_STATUS_MIN_FIELD_COUNT}: {response!r}"
        )
    try:
        return [int(field.strip().strip('"'), 0) for field in fields]
    except ValueError as exc:
        raise AssertionError(f"TDMA:STATus contains non-integer field: {response!r}") from exc


def check_no_error(response: str) -> None:
    fields = parse_csv(response)
    if len(fields) < 2 or fields[0].strip('"') != "0":
        raise AssertionError(f"SCPI error queue not clean: {response!r}")


def parse_loads(text: str) -> list[LoadSpec]:
    loads: list[LoadSpec] = []
    for item in text.split(";"):
        item = item.strip()
        if not item:
            continue
        parts = [int(part.strip(), 0) for part in item.split(":")]
        if len(parts) != 7:
            raise SystemExit(f"load spec must have 7 ':' separated integers: {item!r}")
        loads.append(LoadSpec(*parts))
    if not loads:
        raise SystemExit("at least one load spec is required")
    return loads


def parse_adapter_pins(text: str) -> AdapterPins | None:
    if not text:
        return None
    values = [int(part.strip(), 0) for part in text.split(",")]
    if len(values) != 3:
        raise SystemExit(f"adapter pins must be rx,sck,tx: {text!r}")
    return AdapterPins(values[0], values[1], values[2])


def parse_line_map(text: str) -> list[int]:
    values = [int(part.strip(), 0) for part in text.split(",")]
    if len(values) != 4 or sorted(values) != [0, 1, 2, 3]:
        raise SystemExit(f"line remap must be a permutation of 0..3: {text!r}")
    return values


def explicit_line_map(text: str) -> list[int] | None:
    if text.strip().lower() in {"auto", "detect"}:
        return None
    return parse_line_map(text)


def output_index_for_target_input(remap: list[int], target_input_index: int) -> int:
    return remap.index(target_input_index)


def read_io_profile(ser: serial.Serial, timeout_s: float) -> IoProfile:
    values = parse_int_fields(query(ser, "REALtime:IO:PROFile?", timeout_s), 8, "IO:PROFile?")
    return IoProfile(*values[:8])


def master_pins(sender_profile: IoProfile, sender_to_receiver: list[int]) -> AdapterPins:
    return AdapterPins(
        rx=sender_profile.input_base,
        sck=sender_profile.output_base + output_index_for_target_input(sender_to_receiver, 2),
        tx=sender_profile.output_base + output_index_for_target_input(sender_to_receiver, 0),
    )


def slave_pins(receiver_profile: IoProfile, receiver_to_sender: list[int]) -> AdapterPins:
    return AdapterPins(
        rx=receiver_profile.input_base,
        sck=receiver_profile.input_base + 2,
        tx=receiver_profile.output_base + output_index_for_target_input(receiver_to_sender, 0),
    )


def min_system_adapter_plan(board: str) -> AdapterPlan:
    if board == "A":
        return AdapterPlan(
            uplink_duplex_mode=ADAPTER_DUPLEX_HALF,
            uplink_adapter=AdapterPins(16, 18, 23),
            downlink_duplex_mode=ADAPTER_DUPLEX_HALF,
            downlink_adapter=AdapterPins(16, 22, 23),
        )
    if board == "B":
        return AdapterPlan(
            uplink_duplex_mode=ADAPTER_DUPLEX_HALF,
            uplink_adapter=AdapterPins(16, 18, 23),
            downlink_duplex_mode=ADAPTER_DUPLEX_HALF,
            downlink_adapter=AdapterPins(16, 21, 23),
        )
    raise ValueError(f"unknown board {board!r}")


def write_io_preflight_result(out_dir: Path,
                              io_preflight: dict[str, object] | None) -> None:
    if io_preflight is None:
        return
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "io_preflight_result.json").write_text(
        json.dumps(io_preflight, indent=2),
        encoding="utf-8",
    )


def run_io_preflight(args: argparse.Namespace) -> dict[str, object]:
    quiesce_records: list[dict[str, str]] = []
    for port in (args.port_a, args.port_b):
        try:
            with serial.Serial(port, args.baud, timeout=0.05, write_timeout=1.0) as ser:
                time.sleep(0.2)
                for command in (
                        "SYSTem:REFMEM:SYNC:AUTO 0",
                        "REALtime:IO:OUTPut:MASK 0",
                        "REALtime:IO:OUTPut:RELease"):
                    response = try_query(ser, command, args.timeout_s)
                    quiesce_records.append({
                        "port": port,
                        "command": command,
                        "response": response,
                    })
        except serial.SerialException as exc:
            quiesce_records.append({
                "port": port,
                "command": "open",
                "response": f"<serial-error:{exc}>",
            })

    preflight_dir = args.out_dir / "io_preflight"
    command = [
        sys.executable,
        str(ROOT / "tools" / "two_board_io_validate" / "two_board_io_validate.py"),
        "--port-a", args.port_a,
        "--port-b", args.port_b,
        "--name-a", "A",
        "--name-b", "B",
        "--expect-a-to-b", args.line_remap_a_to_b,
        "--expect-b-to-a", args.line_remap_b_to_a,
        "--out-dir", str(preflight_dir),
        "--timeout", str(args.timeout_s),
    ]
    preflight_timeout_s = max(10.0, args.timeout_s * 8.0)
    try:
        completed = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=preflight_timeout_s,
        )
        returncode = completed.returncode
        stdout = completed.stdout
        stderr = completed.stderr
    except subprocess.TimeoutExpired as exc:
        returncode = 124
        stdout = exc.stdout or ""
        stderr = (exc.stderr or "") + f"\n<timeout:{preflight_timeout_s:.1f}s>"
    summary_path = preflight_dir / "summary.json"
    summary: dict[str, object] = {}
    if summary_path.exists():
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    return {
        "command": command,
        "returncode": returncode,
        "stdout": stdout,
        "stderr": stderr,
        "summary_path": str(summary_path),
        "summary": summary,
        "quiesce": quiesce_records,
    }


def read_observed_map(io_preflight: dict[str, object],
                      source: str,
                      target: str) -> list[int]:
    summary = io_preflight.get("summary")
    if not isinstance(summary, dict):
        raise RuntimeError("IO preflight summary is missing")
    observed_maps = summary.get("observed_maps")
    if not isinstance(observed_maps, dict):
        raise RuntimeError("IO preflight observed_maps is missing")
    key = f"{source}_to_{target}"
    values = observed_maps.get(key)
    if not isinstance(values, list):
        raise RuntimeError(f"IO preflight observed map {key} is missing")
    text = ",".join(str(value) for value in values)
    return parse_line_map(text)


def run_checked(records: list[Record],
                board: str,
                ser: serial.Serial,
                command: str,
                timeout_s: float,
                checker) -> str:
    response = query(ser, command, timeout_s)
    try:
        checker(response)
        record = Record(board, command, response, "PASS", "ok")
    except AssertionError as exc:
        record = Record(board, command, response, "FAIL", str(exc))
    records.append(record)
    print(f"{record.status} {board} {command} => {response}", flush=True)
    if record.status != "PASS":
        raise SystemExit(1)
    return response


def configure_auto(records: list[Record],
                   board: str,
                   ser: serial.Serial,
                   *,
                   enabled: int,
                   local_slot: int,
                   target_mask: int,
                   baud_hz: int,
                   deadline_us: int,
                   adapter_plan: AdapterPlan,
                   timeout_s: float) -> None:
    command = (
        f"SYSTem:REFMEM:SYNC:AUTO {enabled},{local_slot},{target_mask},"
        f"{baud_hz},{deadline_us},"
        f"{adapter_plan.uplink_duplex_mode},"
        f"{adapter_plan.uplink_adapter.rx},{adapter_plan.uplink_adapter.sck},"
        f"{adapter_plan.uplink_adapter.tx},"
        f"{adapter_plan.downlink_duplex_mode},"
        f"{adapter_plan.downlink_adapter.rx},"
        f"{adapter_plan.downlink_adapter.sck},{adapter_plan.downlink_adapter.tx}"
    )
    run_checked(records, board, ser, command, timeout_s, lambda response: parse_ok(response, "AUTO"))


def wait_auto_rx(records: list[Record],
                 board: str,
                 ser: serial.Serial,
                 *,
                 query_timeout_s: float,
                 sync_timeout_s: float,
                 poll_s: float) -> list[int]:
    deadline = time.monotonic() + sync_timeout_s
    last_response = "<not-polled>"
    while time.monotonic() < deadline:
        last_response = query(ser, "SYSTem:REFMEM:SYNC:AUTO?", query_timeout_s)
        values = auto_values(last_response)
        if values[0] == 1 and values[14] == AUTO_INTENT_RX_WINDOW:
            records.append(Record(board, "SYSTem:REFMEM:SYNC:AUTO?", last_response, "PASS", "rx window active"))
            print(f"PASS {board} SYSTem:REFMEM:SYNC:AUTO? => {last_response}", flush=True)
            return values
        time.sleep(poll_s)
    records.append(Record(board, "SYSTem:REFMEM:SYNC:AUTO?", last_response, "FAIL", "rx window timeout"))
    print(f"FAIL {board} SYSTem:REFMEM:SYNC:AUTO? => {last_response}", flush=True)
    raise SystemExit(1)


def wait_auto_maintenance(records: list[Record],
                          board: str,
                          ser: serial.Serial,
                          *,
                          query_timeout_s: float,
                          sync_timeout_s: float,
                          poll_s: float,
                          maintenance_s: float) -> None:
    first_auto = wait_auto_rx(records,
                              board,
                              ser,
                              query_timeout_s=query_timeout_s,
                              sync_timeout_s=sync_timeout_s,
                              poll_s=poll_s)
    first_tdma_response = query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", query_timeout_s)
    first_tdma = tdma_status_values(first_tdma_response)
    records.append(Record(board,
                          "SYSTem:REFMEM:SYNC:TDMA:STATus?",
                          first_tdma_response,
                          "PASS",
                          "maintenance baseline"))
    print(f"PASS {board} SYSTem:REFMEM:SYNC:TDMA:STATus? => {first_tdma_response}", flush=True)

    time.sleep(maintenance_s)

    deadline = time.monotonic() + sync_timeout_s
    last_auto_response = "<not-polled>"
    last_tdma_response = "<not-polled>"
    while time.monotonic() < deadline:
        last_auto_response = query(ser, "SYSTem:REFMEM:SYNC:AUTO?", query_timeout_s)
        last_auto = auto_values(last_auto_response)
        last_tdma_response = query(ser, "SYSTem:REFMEM:SYNC:TDMA:STATus?", query_timeout_s)
        last_tdma = tdma_status_values(last_tdma_response)
        rx_progress = last_auto[18] > first_auto[18]
        core1_progress = last_tdma[3] > first_tdma[3] or last_tdma[5] > first_tdma[5]
        if (last_auto[0] == 1 and
                last_auto[13] == 0 and
                last_auto[20] == first_auto[20] and
                last_auto[21] == first_auto[21] and
                last_auto[25] == 0 and
                (rx_progress or core1_progress)):
            records.append(Record(board,
                                  "SYSTem:REFMEM:SYNC:AUTO?",
                                  last_auto_response,
                                  "PASS",
                                  "auto maintenance progressed"))
            records.append(Record(board,
                                  "SYSTem:REFMEM:SYNC:TDMA:STATus?",
                                  last_tdma_response,
                                  "PASS",
                                  "core1 tdma progressed"))
            print(f"PASS {board} SYSTem:REFMEM:SYNC:AUTO? => {last_auto_response}", flush=True)
            print(f"PASS {board} SYSTem:REFMEM:SYNC:TDMA:STATus? => {last_tdma_response}", flush=True)
            return
        time.sleep(poll_s)

    records.append(Record(board,
                          "SYSTem:REFMEM:SYNC:AUTO?",
                          last_auto_response,
                          "FAIL",
                          "auto maintenance did not progress cleanly"))
    records.append(Record(board,
                          "SYSTem:REFMEM:SYNC:TDMA:STATus?",
                          last_tdma_response,
                          "FAIL",
                          "core1 tdma did not progress cleanly"))
    print(f"FAIL {board} SYSTem:REFMEM:SYNC:AUTO? => {last_auto_response}", flush=True)
    print(f"FAIL {board} SYSTem:REFMEM:SYNC:TDMA:STATus? => {last_tdma_response}", flush=True)
    raise SystemExit(1)


def wait_sync_applied(records: list[Record],
                      board: str,
                      ser: serial.Serial,
                      *,
                      base_applied: int,
                      expected_count: int,
                      expected_source_slot: int,
                      query_timeout_s: float,
                      sync_timeout_s: float,
                      poll_s: float) -> list[int]:
    deadline = time.monotonic() + sync_timeout_s
    last_response = "<not-polled>"
    while time.monotonic() < deadline:
        last_response = query(ser, "SYSTem:REFMEM:SYNC:AUTO?", query_timeout_s)
        values = auto_values(last_response)
        if (values[19] >= base_applied + expected_count and
                values[22] == 0 and
                values[23] == NODE_LOAD_DELTA_FRAME_TYPE and
                values[24] == expected_source_slot and
                values[25] == 0):
            records.append(Record(board, "SYSTem:REFMEM:SYNC:AUTO?", last_response, "PASS", "node load applied"))
            print(f"PASS {board} SYSTem:REFMEM:SYNC:AUTO? => {last_response}", flush=True)
            return values
        time.sleep(poll_s)
    records.append(Record(board, "SYSTem:REFMEM:SYNC:AUTO?", last_response, "FAIL", "apply timeout"))
    print(f"FAIL {board} SYSTem:REFMEM:SYNC:AUTO? => {last_response}", flush=True)
    raise SystemExit(1)


def check_node_load_table_staging(response: str) -> list[int]:
    values = table_values(response)
    if (values[3] & NODE_LOAD_TABLE_MASK) == 0:
        raise AssertionError(f"NodeLoad table is not staged: {response!r}")
    if values[12] == 0:
        raise AssertionError(f"NodeLoad staging CRC is zero: {response!r}")
    if values[15] != 0:
        raise AssertionError(f"NodeLoad table last_result is non-zero: {response!r}")
    return values


def verify_peer_state(records: list[Record],
                      board: str,
                      ser: serial.Serial,
                      expected_last: LoadSpec,
                      timeout_s: float) -> int:
    load_response = run_checked(
        records,
        board,
        ser,
        "SYSTem:REFMEM:LOAD:STATus?",
        timeout_s,
        lambda response: load_status_values(response),
    )
    status = load_status_values(load_response)
    if status[14] != expected_last.node_id or status[15] != expected_last.instance_id or status[21] != 0:
        raise SystemExit(
            f"{board} LOAD:STATus did not mirror last node {expected_last.node_id}/"
            f"{expected_last.instance_id}: {load_response!r}"
        )

    table_response = run_checked(
        records,
        board,
        ser,
        f"SYSTem:REFMEM:TABle? {NODE_LOAD_TABLE_ID}",
        timeout_s,
        check_node_load_table_staging,
    )
    table = table_values(table_response)
    for slot_id in (expected_last.node_id,):
        run_checked(records,
                    board,
                    ser,
                    f"SYSTem:REFMEM:CLAIM? {slot_id}",
                    timeout_s,
                    lambda response: claim_values(response))
    return table[12]


def run_direction(records: list[Record],
                  source_name: str,
                  source: serial.Serial,
                  source_slot: int,
                  peer_name: str,
                  peer: serial.Serial,
                  loads: list[LoadSpec],
                  timeout_s: float,
                  sync_timeout_s: float,
                  poll_s: float) -> None:
    peer_auto = wait_auto_rx(records,
                             peer_name,
                             peer,
                             query_timeout_s=timeout_s,
                             sync_timeout_s=sync_timeout_s,
                             poll_s=poll_s)
    source_auto = auto_values(query(source, "SYSTem:REFMEM:SYNC:AUTO?", timeout_s))
    base_source_tx = source_auto[17]
    base_peer_applied = peer_auto[19]

    for load in loads:
        run_checked(records,
                    source_name,
                    source,
                    load.command(),
                    timeout_s,
                    lambda response, expected=load: parse_load_response(response, expected))
        time.sleep(0.05)

    wait_sync_applied(records,
                      peer_name,
                      peer,
                      base_applied=base_peer_applied,
                      expected_count=len(loads),
                      expected_source_slot=source_slot,
                      query_timeout_s=timeout_s,
                      sync_timeout_s=sync_timeout_s,
                      poll_s=poll_s)

    source_after = auto_values(query(source, "SYSTem:REFMEM:SYNC:AUTO?", timeout_s))
    if source_after[17] < base_source_tx + len(loads):
        raise SystemExit(
            f"{source_name} AUTO submitted_tx_count={source_after[17]} "
            f"expected >= {base_source_tx + len(loads)}"
        )

    source_crc = verify_peer_state(records, source_name, source, loads[-1], timeout_s)
    peer_crc = verify_peer_state(records, peer_name, peer, loads[-1], timeout_s)
    if source_crc != peer_crc:
        raise SystemExit(
            f"NodeLoad staging CRC mismatch {source_name}=0x{source_crc:08X} "
            f"{peer_name}=0x{peer_crc:08X}"
        )


def drain_errors(records: list[Record], board: str, ser: serial.Serial, timeout_s: float) -> None:
    for _ in range(8):
        response = query(ser, "SYSTem:ERRor?", timeout_s)
        fields = parse_csv(response)
        if len(fields) >= 2 and fields[0].strip('"') == "0":
            records.append(Record(board, "SYSTem:ERRor?", response, "PASS", "error queue clean"))
            print(f"PASS {board} SYSTem:ERRor? => {response}", flush=True)
            return
        records.append(Record(board, "SYSTem:ERRor?", response, "INFO", "stale error drained"))
        print(f"INFO {board} SYSTem:ERRor? => {response}", flush=True)
        time.sleep(0.05)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-a", default="COM5")
    parser.add_argument("--port-b", default="COM6")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--slot-a", type=int, default=0)
    parser.add_argument("--slot-b", type=int, default=1)
    parser.add_argument("--tdma-baud", type=int, default=25_000_000)
    parser.add_argument("--deadline-us", type=int, default=1_000_000)
    parser.add_argument("--timeout-s", type=float, default=2.0)
    parser.add_argument("--sync-timeout-s", type=float, default=10.0)
    parser.add_argument("--poll-s", type=float, default=0.05)
    parser.add_argument("--maintenance-s", type=float, default=2.5)
    parser.add_argument("--uplink-adapter-a", default="")
    parser.add_argument("--downlink-adapter-a", default="")
    parser.add_argument("--uplink-adapter-b", default="")
    parser.add_argument("--downlink-adapter-b", default="")
    parser.add_argument(
        "--adapter-profile",
        choices=(MIN_SYSTEM_ADAPTER_PROFILE, IO_PREFLIGHT_ADAPTER_PROFILE),
        default=MIN_SYSTEM_ADAPTER_PROFILE,
        help=(
            "default uses the current GPIO16-24 TDMA comm loop; "
            "io-preflight derives pins from REALtime:IO overlay wiring"
        ),
    )
    parser.add_argument("--line-remap-a-to-b", default="auto")
    parser.add_argument("--line-remap-b-to-a", default="auto")
    parser.add_argument("--skip-io-preflight", action="store_true")
    parser.add_argument(
        "--loads-a",
        default="6:10:192:16:1:0:0;5:9:160:16:1:0:1",
        help="semicolon list of node:instance:role:persona:enabled:required:order",
    )
    parser.add_argument(
        "--loads-b",
        default="6:10:192:16:1:0:0;5:9:160:16:1:0:1",
        help="semicolon list of node:instance:role:persona:enabled:required:order",
    )
    parser.add_argument("--out-dir", default="")
    args = parser.parse_args()
    if not args.out_dir:
        args.out_dir = str(Path("build-rtos-multicore-smoke") / (
            "refmem_node_load_auto_hil_" + datetime.now().strftime("%Y%m%d%H%M%S")
        ))
    args.out_dir = Path(args.out_dir)

    loads_a = parse_loads(args.loads_a)
    loads_b = parse_loads(args.loads_b)
    explicit_a = AdapterPlan(
        uplink_duplex_mode=ADAPTER_DUPLEX_HALF,
        uplink_adapter=parse_adapter_pins(args.uplink_adapter_a),
        downlink_duplex_mode=ADAPTER_DUPLEX_HALF,
        downlink_adapter=parse_adapter_pins(args.downlink_adapter_a),
    )
    explicit_b = AdapterPlan(
        uplink_duplex_mode=ADAPTER_DUPLEX_HALF,
        uplink_adapter=parse_adapter_pins(args.uplink_adapter_b),
        downlink_duplex_mode=ADAPTER_DUPLEX_HALF,
        downlink_adapter=parse_adapter_pins(args.downlink_adapter_b),
    )
    if ((explicit_a.uplink_adapter is None) != (explicit_a.downlink_adapter is None) or
            (explicit_b.uplink_adapter is None) != (explicit_b.downlink_adapter is None) or
            (explicit_a.uplink_adapter is None) != (explicit_b.uplink_adapter is None)):
        raise SystemExit("explicit adapter pins must provide uplink/downlink for both boards")
    mask_a = 1 << args.slot_a
    mask_b = 1 << args.slot_b
    records: list[Record] = []
    io_preflight: dict[str, object] | None = None
    remap_a_to_b = explicit_line_map(args.line_remap_a_to_b)
    remap_b_to_a = explicit_line_map(args.line_remap_b_to_a)
    use_io_preflight = (
        explicit_a.uplink_adapter is None and
        args.adapter_profile == IO_PREFLIGHT_ADAPTER_PROFILE
    )
    if use_io_preflight and not args.skip_io_preflight:
        io_preflight = run_io_preflight(args)
    if use_io_preflight and (remap_a_to_b is None or remap_b_to_a is None):
        if io_preflight is None or io_preflight["returncode"] != 0:
            write_io_preflight_result(args.out_dir, io_preflight)
            if io_preflight is not None:
                print(io_preflight.get("stdout", ""), end="", flush=True)
                print(io_preflight.get("stderr", ""), end="", file=sys.stderr, flush=True)
            raise SystemExit("IO preflight is required for auto line remap")
        if remap_a_to_b is None:
            remap_a_to_b = read_observed_map(io_preflight, "A", "B")
        if remap_b_to_a is None:
            remap_b_to_a = read_observed_map(io_preflight, "B", "A")

    with serial.Serial(args.port_a, args.baud, timeout=0.05, write_timeout=1.0) as ser_a, \
            serial.Serial(args.port_b, args.baud, timeout=0.05, write_timeout=1.0) as ser_b:
        time.sleep(0.2)
        drain_errors(records, "A", ser_a, args.timeout_s)
        drain_errors(records, "B", ser_b, args.timeout_s)

        if explicit_a.uplink_adapter is not None and explicit_b.uplink_adapter is not None:
            plan_a = explicit_a
            plan_b = explicit_b
        elif args.adapter_profile == MIN_SYSTEM_ADAPTER_PROFILE:
            plan_a = min_system_adapter_plan("A")
            plan_b = min_system_adapter_plan("B")
        else:
            profile_a = read_io_profile(ser_a, args.timeout_s)
            profile_b = read_io_profile(ser_b, args.timeout_s)
            plan_a = AdapterPlan(
                uplink_duplex_mode=ADAPTER_DUPLEX_HALF,
                uplink_adapter=slave_pins(profile_a, remap_a_to_b),
                downlink_duplex_mode=ADAPTER_DUPLEX_HALF,
                downlink_adapter=master_pins(profile_a, remap_a_to_b),
            )
            plan_b = AdapterPlan(
                uplink_duplex_mode=ADAPTER_DUPLEX_HALF,
                uplink_adapter=slave_pins(profile_b, remap_b_to_a),
                downlink_duplex_mode=ADAPTER_DUPLEX_HALF,
                downlink_adapter=master_pins(profile_b, remap_b_to_a),
            )

        configure_auto(records,
                       "A",
                       ser_a,
                       enabled=1,
                       local_slot=args.slot_a,
                       target_mask=mask_b,
                       baud_hz=args.tdma_baud,
                       deadline_us=args.deadline_us,
                       adapter_plan=plan_a,
                       timeout_s=args.timeout_s)
        configure_auto(records,
                       "B",
                       ser_b,
                       enabled=1,
                       local_slot=args.slot_b,
                       target_mask=mask_a,
                       baud_hz=args.tdma_baud,
                       deadline_us=args.deadline_us,
                       adapter_plan=plan_b,
                       timeout_s=args.timeout_s)

        run_direction(records,
                      "A",
                      ser_a,
                      args.slot_a,
                      "B",
                      ser_b,
                      loads_a,
                      args.timeout_s,
                      args.sync_timeout_s,
                      args.poll_s)
        run_direction(records,
                      "B",
                      ser_b,
                      args.slot_b,
                      "A",
                      ser_a,
                      loads_b,
                      args.timeout_s,
                      args.sync_timeout_s,
                      args.poll_s)

        wait_auto_maintenance(records,
                              "A",
                              ser_a,
                              query_timeout_s=args.timeout_s,
                              sync_timeout_s=args.sync_timeout_s,
                              poll_s=args.poll_s,
                              maintenance_s=args.maintenance_s)
        wait_auto_maintenance(records,
                              "B",
                              ser_b,
                              query_timeout_s=args.timeout_s,
                              sync_timeout_s=args.sync_timeout_s,
                              poll_s=args.poll_s,
                              maintenance_s=args.maintenance_s)

        run_checked(records, "A", ser_a, "SYSTem:ERRor?", args.timeout_s, check_no_error)
        run_checked(records, "B", ser_b, "SYSTem:ERRor?", args.timeout_s, check_no_error)

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "records.json").write_text(
        json.dumps([asdict(record) for record in records], indent=2),
        encoding="utf-8",
    )
    (out_dir / "adapter_plan.json").write_text(
        json.dumps({
            "io_preflight": io_preflight,
            "A": asdict(plan_a),
            "B": asdict(plan_b),
            "semantics": {
                "uplink_adapter": "receive data from previous board",
                "downlink_adapter": "publish data to next board",
            },
        }, indent=2),
        encoding="utf-8",
    )
    print(f"PASS RefMem NodeLoad AUTO HIL records: {out_dir / 'records.json'}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
