#!/usr/bin/env python3
"""Validate RP2350_TRIG dual-core (RTOS + AMP) smoke on the bench over SCPI USB CDC.

Checks:
- *IDN?, SYST:FW:BUILD? baseline
- SYST:CORE? core1 enabled and heartbeat growing
- SYST:LOOP:STAT? loop engine ready and service counter growing
- SYST:SYNC:VDC:STAT? VDC sync skeleton ready and service counter growing
- SYST:SYNC:VDC:DPLL:STAT? VDC DPLL skeleton ready and service counter growing
- SYST:CFG:STAT? config gate static snapshot ready and service counter growing
- SYST:CFG:ROLE?/LOOP?/ACT?/CAL? static distributed config queries
- SYST:CFG:ACK?, SYST:CFG:NACK?, SYST:SCPI:RUN:ALLOW? ACK reason and RUN whitelist tables
- SYST:CORE:VECT? and SYST:PROT:STAT? owner/protection table snapshots
- SYST:MODE:TAB?, SYST:RESource:TAB?, SYST:FAULT:TAB? system/resource/fault tables
- TRIGger:MODE 1 -> STARt -> STOP product control smoke
- Error queue, LOG STAT, TRACE LAST
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
TASK_RULES = """需要烧录固件前必须提前告知用户烧录对象、原因和预期影响；
若需要用户按 BOOTSEL、断电或复位，必须停下来等待用户操作。"""


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _read_line(ser: serial.Serial, timeout_s: float) -> str:
    """Read one meaningful SCPI response line, skipping logs and empty lines."""
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
        if len(raw) == 0:
            continue
        line = bytes(raw).decode("utf-8", errors="replace").strip()
        maybe_log = line[1:] if line.startswith('"[') else line
        if not line or maybe_log.startswith("[") or maybe_log.startswith("log:"):
            continue
        if line in {'"OK"', "OK", 'OK"'} or line.startswith('"OK[') or line.startswith('OK['):
            return '"OK"'
        line = re.sub(r'(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+.*$', '', line).strip()
        return line
    return "<timeout>"


def _cmd(ser: serial.Serial, command: str, timeout_s: float) -> str:
    """Send a command and read the ACK line."""
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return _read_line(ser, timeout_s)


def _query(ser: serial.Serial, command: str, timeout_s: float) -> str:
    """Send a query and read the response (no ACK)."""
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    return _read_line(ser, timeout_s)


def _parse_ints(response: str) -> list[int]:
    """Parse a CSV integer response like '1,0,95293475,0,68513'."""
    parts = [p.strip() for p in response.split(",")]
    out: list[int] = []
    for p in parts:
        try:
            out.append(int(p))
        except ValueError:
            pass
    return out


def _ack_ok(response: str) -> bool:
    return response in {'"OK"', "OK", '"OK', "OK\"", '"O', "O", "1"}


# ---------------------------------------------------------------------------
# test functions – each returns (passed: bool, detail: str)
# ---------------------------------------------------------------------------

def test_identity(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "*IDN?", timeout)
    if resp == "<timeout>":
        return False, "*IDN? timed out"
    if "GTS" in resp or "RP2350_TRIG" in resp or "SYNC_TRIGGER" in resp:
        return True, resp
    return False, f"unexpected *IDN? response: {resp}"


def test_build_id(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "SYST:FW:BUILD?", timeout)
    if resp == "<timeout>":
        return False, "SYST:FW:BUILD? timed out"
    # Response is a quoted build id like "20260810072841"
    if len(resp) >= 14:
        return True, resp
    return False, f"unexpected build id: {resp}"


def test_core_heartbeat(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read SYST:CORE? 3 times, 1s apart.  core1 must be enabled AND loop count growing."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "SYST:CORE?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 5:
            return False, f"SYST:CORE? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    # check core1 enabled (field 0 == 1)
    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"SYST:CORE? read {i+1}: core1 NOT enabled (field0={fields[0]})"

    # check heartbeat growing (field 2 = core1 loop count)
    lc = [r[2] for r in reads]
    if lc[0] < lc[1] < lc[2]:
        return True, f"core1 enabled, loop counts: {lc[0]} -> {lc[1]} -> {lc[2]} (+{lc[2]-lc[0]})"
    if lc[0] == lc[1] or lc[1] == lc[2]:
        return False, f"core1 loop count STALLED: {lc}"
    return False, f"core1 loop count not monotonic: {lc}"


def test_trigger_seq(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """TRIGger product control smoke: MODE 1 -> START -> STOP with status reads."""
    steps: list[str] = []

    # Make the test repeatable after an interrupted/manual run sequence.
    _cmd(ser, "TRIGger:STOP", timeout)

    ack = _cmd(ser, "TRIGger:MODE 1", timeout)
    if not _ack_ok(ack):
        return False, f"TRIGger:MODE 1 ack: {ack}"
    steps.append("MODE=1")

    mode = _query(ser, "TRIGger:MODE?", timeout)
    mode_fields = _parse_ints(mode)
    if len(mode_fields) < 1 or mode_fields[0] != 1 or "TRIG" not in mode:
        return False, f"TRIGger:MODE? expected TRIG,1, got: {mode}"
    steps.append("mode_query=TRIG")

    state_before = _query(ser, "READ:TRIGger:STATe?", timeout)
    if "TRIG" not in state_before or "PLAN_A" not in state_before:
        return False, f"READ:TRIGger:STATe? missing product fields before START: {state_before}"
    steps.append("state_read_ok")

    ack = _cmd(ser, "TRIGger:STARt", timeout)
    if not _ack_ok(ack):
        return False, f"TRIGger:STARt ack: {ack}"
    steps.append("START accepted")

    state_after_start = _query(ser, "READ:TRIGger:STATe?", timeout)
    if "TRIG" not in state_after_start or "NONE" not in state_after_start:
        return False, f"READ:TRIGger:STATe? missing product fields after START: {state_after_start}"
    steps.append("start_state_read_ok")

    ack = _cmd(ser, "TRIGger:STOP", timeout)
    if not _ack_ok(ack):
        return False, f"TRIGger:STOP ack: {ack}"
    steps.append("STOP accepted")

    err = _query(ser, "SYST:ERR?", timeout)
    if "No error" not in err and not err.startswith("0,"):
        return False, f"SYST:ERR? after trigger control: {err}"
    steps.append("error_queue=0")

    return True, " -> ".join(steps)


def test_loop_status(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read SYST:LOOP:STAT? 3 times, 1s apart. service_count must grow and ready must be true."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "SYST:LOOP:STAT?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 4:
            return False, f"SYST:LOOP:STAT? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"SYST:LOOP:STAT? read {i+1}: loop engine NOT ready (field0={fields[0]})"

    service_counts = [r[1] for r in reads]
    if service_counts[0] < service_counts[1] < service_counts[2]:
        return True, (
            "loop engine ready, service counts: "
            f"{service_counts[0]} -> {service_counts[1]} -> {service_counts[2]}"
        )
    if service_counts[0] == service_counts[1] or service_counts[1] == service_counts[2]:
        return False, f"SYST:LOOP:STAT? service count STALLED: {service_counts}"
    return False, f"SYST:LOOP:STAT? service count not monotonic: {service_counts}"


def test_vdc_status(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read SYST:SYNC:VDC:STAT? 3 times. service_count must grow and ready must be true."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "SYST:SYNC:VDC:STAT?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 6:
            return False, f"SYST:SYNC:VDC:STAT? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"SYST:SYNC:VDC:STAT? read {i+1}: VDC sync NOT ready (field0={fields[0]})"

    service_counts = [r[2] for r in reads]
    sync_seq = [r[5] for r in reads]
    if service_counts[0] < service_counts[1] < service_counts[2] and sync_seq[0] < sync_seq[1] < sync_seq[2]:
        return True, (
            "VDC sync ready, service counts: "
            f"{service_counts[0]} -> {service_counts[1]} -> {service_counts[2]}, "
            f"sync_seq: {sync_seq[0]} -> {sync_seq[1]} -> {sync_seq[2]}, "
            f"lock_state={reads[-1][1]}"
        )
    if service_counts[0] == service_counts[1] or service_counts[1] == service_counts[2]:
        return False, f"SYST:SYNC:VDC:STAT? service count STALLED: {service_counts}"
    if sync_seq[0] == sync_seq[1] or sync_seq[1] == sync_seq[2]:
        return False, f"SYST:SYNC:VDC:STAT? sync_seq STALLED: {sync_seq}"
    return False, f"SYST:SYNC:VDC:STAT? counters not monotonic: service={service_counts}, sync_seq={sync_seq}"


def test_dpll_status(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read SYST:SYNC:VDC:DPLL:STAT? 3 times. service_count must grow and ready must be true."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "SYST:SYNC:VDC:DPLL:STAT?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 6:
            return False, f"SYST:SYNC:VDC:DPLL:STAT? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"SYST:SYNC:VDC:DPLL:STAT? read {i+1}: DPLL NOT ready (field0={fields[0]})"

    service_counts = [r[2] for r in reads]
    update_seq = [r[5] for r in reads]
    if service_counts[0] < service_counts[1] < service_counts[2] and update_seq[0] < update_seq[1] < update_seq[2]:
        return True, (
            "DPLL ready, service counts: "
            f"{service_counts[0]} -> {service_counts[1]} -> {service_counts[2]}, "
            f"update_seq: {update_seq[0]} -> {update_seq[1]} -> {update_seq[2]}, "
            f"state={reads[-1][1]}"
        )
    if service_counts[0] == service_counts[1] or service_counts[1] == service_counts[2]:
        return False, f"SYST:SYNC:VDC:DPLL:STAT? service count STALLED: {service_counts}"
    if update_seq[0] == update_seq[1] or update_seq[1] == update_seq[2]:
        return False, f"SYST:SYNC:VDC:DPLL:STAT? update_seq STALLED: {update_seq}"
    return False, f"SYST:SYNC:VDC:DPLL:STAT? counters not monotonic: service={service_counts}, update_seq={update_seq}"


def test_calibration_status(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read CAL status snapshots. service_count must grow and table snapshots must be valid."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "READ:CALibration:STATe?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 8:
            return False, f"READ:CALibration:STATe? read {i+1}: unparseable response: {resp}"
        if "DONE" not in resp and "IDLE" not in resp:
            return False, f"READ:CALibration:STATe? read {i+1}: missing state text: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    ready_values = [r[4] for r in reads]
    service_counts = [r[6] for r in reads]
    active_crcs = [r[7] for r in reads]
    if any(v != 1 for v in ready_values):
        return False, f"READ:CALibration:STATe? ready field unexpected: {ready_values}"
    if not (service_counts[0] < service_counts[1] < service_counts[2]):
        return False, f"READ:CALibration:STATe? service count not monotonic: {service_counts}"
    if active_crcs[0] == 0:
        return False, f"READ:CALibration:STATe? active CRC missing: {active_crcs}"

    link = _parse_ints(_query(ser, "READ:CALibration:LINK?", timeout))
    if len(link) < 6:
        return False, f"READ:CALibration:LINK? unparseable: {link}"
    if link[3] != active_crcs[-1] or link[5] != 1:
        return False, f"READ:CALibration:LINK? guard/ready unexpected: {link}"

    parameter = _parse_ints(_query(ser, "READ:CALibration:PARameter?", timeout))
    if len(parameter) < 6:
        return False, f"READ:CALibration:PARameter? unparseable: {parameter}"
    if parameter[3] != active_crcs[-1] or parameter[5] != 1:
        return False, f"READ:CALibration:PARameter? guard/ready unexpected: {parameter}"

    health_resp = _query(ser, "READ:CALibration:HEALth?", timeout)
    health = _parse_ints(health_resp)
    if len(health) < 4 or "OK" not in health_resp:
        return False, f"READ:CALibration:HEALth? unparseable: {health_resp}"
    if health[0] < 1 or health[1] < 1 or health[2] < service_counts[-1]:
        return False, f"READ:CALibration:HEALth? unexpected fields: {health_resp}"

    return True, (
        "calibration ready, service counts: "
        f"{service_counts[0]} -> {service_counts[1]} -> {service_counts[2]}, "
        f"active_crc={active_crcs[-1]}, link_seq={link[0]}, parameter_seq={parameter[0]}"
    )


def test_config_gate_status(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    """Read SYST:CFG:STAT? 3 times, 1s apart. service_count must grow and CRCs must exist."""
    reads: list[list[int]] = []
    for i in range(3):
        resp = _query(ser, "SYST:CFG:STAT?", timeout)
        fields = _parse_ints(resp)
        if len(fields) < 24:
            return False, f"SYST:CFG:STAT? read {i+1}: unparseable response: {resp}"
        reads.append(fields)
        if i < 2:
            time.sleep(1.0)

    for i, fields in enumerate(reads):
        if fields[0] != 1:
            return False, f"SYST:CFG:STAT? read {i+1}: config gate NOT ready (field0={fields[0]})"
        if fields[1] != 1:
            return False, f"SYST:CFG:STAT? read {i+1}: gate_state not ready (field1={fields[1]})"
        if fields[12] != 15:
            return False, f"SYST:CFG:STAT? read {i+1}: target_mask unexpected (field12={fields[12]})"
        if fields[13] != 15 or fields[14] != 0:
            return False, f"SYST:CFG:STAT? read {i+1}: ACK/NACK unexpected (ack={fields[13]}, nack={fields[14]})"

    service_counts = [r[2] for r in reads]
    build_crcs = [r[17] for r in reads]
    hw_crcs = [r[18] for r in reads]
    config_crcs = [r[23] for r in reads]
    if (service_counts[0] < service_counts[1] < service_counts[2] and
            build_crcs[0] != 0 and hw_crcs[0] != 0 and config_crcs[0] != 0):
        return True, (
            "config gate ready, service counts: "
            f"{service_counts[0]} -> {service_counts[1]} -> {service_counts[2]}, "
            f"build_crc={build_crcs[0]}, hw_profile_crc={hw_crcs[0]}, "
            f"config_crc={config_crcs[0]}, run_id={reads[-1][6]}"
        )
    if service_counts[0] == service_counts[1] or service_counts[1] == service_counts[2]:
        return False, f"SYST:CFG:STAT? service count STALLED: {service_counts}"
    return False, f"SYST:CFG:STAT? counters not monotonic: service={service_counts}"


def test_config_snapshot_queries(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    checks: list[str] = []

    role = _parse_ints(_query(ser, "SYST:CFG:ROLE? 3", timeout))
    if len(role) < 10:
        return False, f"SYST:CFG:ROLE? unparseable: {role}"
    if role[0] != 1 or role[1] != 4 or role[2] != 15 or role[6] != 3 or role[7] != 3:
        return False, f"SYST:CFG:ROLE? unexpected fields: {role}"
    checks.append(f"role_node={role[6]}/role={role[7]}")

    loop = _parse_ints(_query(ser, "SYST:CFG:LOOP? 3", timeout))
    if len(loop) < 9:
        return False, f"SYST:CFG:LOOP? unparseable: {loop}"
    if loop[0] != 1 or loop[1] != 4 or loop[3] != 4 or loop[5] != 3 or loop[6] != 3:
        return False, f"SYST:CFG:LOOP? unexpected fields: {loop}"
    checks.append(f"loop_layer={loop[5]}/node={loop[6]}/action={loop[7]}")

    action = _parse_ints(_query(ser, "SYST:CFG:ACT? 3", timeout))
    if len(action) < 8:
        return False, f"SYST:CFG:ACT? unparseable: {action}"
    if action[0] != 1 or action[1] != 4 or action[2] != 3 or action[3] != 3:
        return False, f"SYST:CFG:ACT? unexpected fields: {action}"
    checks.append(f"action={action[2]}/delay_us={action[7]}")

    calibration = _parse_ints(_query(ser, "SYST:CFG:CAL? 3", timeout))
    if len(calibration) < 9:
        return False, f"SYST:CFG:CAL? unparseable: {calibration}"
    if calibration[0] != 1 or calibration[1] != 4 or calibration[2] != 3 or calibration[3] != 24000:
        return False, f"SYST:CFG:CAL? unexpected fields: {calibration}"
    checks.append(f"cal_node={calibration[2]}/delta_ns={calibration[3]}")

    return True, ", ".join(checks)


def test_ack_reason_and_run_policy(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    ack = _parse_ints(_query(ser, "SYST:CFG:ACK?", timeout))
    if len(ack) < 12:
        return False, f"SYST:CFG:ACK? unparseable: {ack}"
    if ack[0] != 1 or ack[2] != 15 or ack[3] != 15 or ack[4] != 0:
        return False, f"SYST:CFG:ACK? unexpected flags: {ack}"
    if ack[7] != 0 or ack[9] < 6 or ack[10] == 0 or ack[11] == 0:
        return False, f"SYST:CFG:ACK? missing reason/config CRC: {ack}"

    reason = _query(ser, "SYST:CFG:NACK? 5", timeout)
    reason_fields = _parse_ints(reason)
    if len(reason_fields) < 7:
        return False, f"SYST:CFG:NACK? unparseable: {reason}"
    if reason_fields[0] != 1 or reason_fields[2] != 5 or reason_fields[6] != 1005:
        return False, f"SYST:CFG:NACK? unexpected flash lockout reason: {reason}"
    if "FLASH_LOCKOUT_UNREADY" not in reason:
        return False, f"SYST:CFG:NACK? missing reason name: {reason}"

    policy = _query(ser, "SYST:SCPI:RUN:ALLOW? 3", timeout)
    policy_fields = _parse_ints(policy)
    if len(policy_fields) < 10:
        return False, f"SYST:SCPI:RUN:ALLOW? unparseable: {policy}"
    if (policy_fields[0] != 1 or policy_fields[1] < 7 or policy_fields[2] != 1 or
            policy_fields[3] == 0 or policy_fields[4] != 3 or policy_fields[6] != 0 or
            policy_fields[9] != 2401):
        return False, f"SYST:SCPI:RUN:ALLOW? unexpected trigger config policy: {policy}"
    if "TRIG/PULS/MARK" not in policy:
        return False, f"SYST:SCPI:RUN:ALLOW? missing policy pattern: {policy}"

    return True, (
        f"ack_seq={ack[1]} reason_crc={ack[10]} "
        f"policy_crc={policy_fields[3]} forbid={policy_fields[9]}"
    )


def test_runtime_protection_tables(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    core_vector = _parse_ints(_query(ser, "SYST:CORE:VECT?", timeout))
    if len(core_vector) < 13:
        return False, f"SYST:CORE:VECT? unparseable: {core_vector}"
    if core_vector[0] != 1 or core_vector[2] != 2:
        return False, f"SYST:CORE:VECT? unexpected version/core_count: {core_vector}"
    if core_vector[3] != 0 or core_vector[4] != 1 or core_vector[7] != 2:
        return False, f"SYST:CORE:VECT? owner map unexpected: {core_vector}"
    if core_vector[6] == 0 or core_vector[10] == 0:
        return False, f"SYST:CORE:VECT? missing realtime IRQ mask or guard CRC: {core_vector}"

    protection = _parse_ints(_query(ser, "SYST:PROT:STAT?", timeout))
    if len(protection) < 14:
        return False, f"SYST:PROT:STAT? unparseable: {protection}"
    if protection[0] != 1 or protection[2] != 1 or protection[3] != 1:
        return False, f"SYST:PROT:STAT? protection flags unexpected: {protection}"
    if protection[4] != 1 or protection[8] != 2 or protection[11] == 0:
        return False, f"SYST:PROT:STAT? lockout/entry/guard unexpected: {protection}"

    return True, (
        f"core_vector seq={core_vector[1]} core1_irq_mask={core_vector[6]} "
        f"prot_flags={protection[9]} lockout_online={protection[4]}"
    )


def test_system_resource_fault_tables(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    mode = _query(ser, "SYST:MODE:TAB? 1", timeout)
    mode_fields = _parse_ints(mode)
    if len(mode_fields) < 8:
        return False, f"SYST:MODE:TAB? unparseable: {mode}"
    if mode_fields[0] != 1 or mode_fields[1] != 4 or mode_fields[4] != 1 or mode_fields[5] != 1:
        return False, f"SYST:MODE:TAB? unexpected RUN row: {mode}"
    if "RUN" not in mode:
        return False, f"SYST:MODE:TAB? missing mode name: {mode}"

    resource = _query(ser, "SYST:RESource:TAB? 0", timeout)
    resource_fields = _parse_ints(resource)
    if len(resource_fields) < 10:
        return False, f"SYST:RESource:TAB? unparseable: {resource}"
    if resource_fields[0] != 1 or resource_fields[1] != 10 or resource_fields[6] != 0:
        return False, f"SYST:RESource:TAB? unexpected FLASH row: {resource}"
    if "FLASH" not in resource:
        return False, f"SYST:RESource:TAB? missing resource name: {resource}"

    fault = _query(ser, "SYST:FAULT:TAB? 0", timeout)
    fault_fields = _parse_ints(fault)
    if len(fault_fields) < 9:
        return False, f"SYST:FAULT:TAB? unparseable: {fault}"
    if fault_fields[0] != 1 or fault_fields[1] < 16 or fault_fields[4] != 0:
        return False, f"SYST:FAULT:TAB? unexpected NONE row: {fault}"
    if "NONE" not in fault:
        return False, f"SYST:FAULT:TAB? missing fault name: {fault}"

    return True, (
        f"mode_count={mode_fields[1]} resource_count={resource_fields[1]} "
        f"fault_count={fault_fields[1]}"
    )


def test_error_queue(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "SYST:ERR?", timeout)
    if "No error" in resp or "0" in resp:
        return True, resp
    return False, f"unexpected error: {resp}"


def test_log_stat(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "SYST:LOG:STAT?", timeout)
    if resp == "<timeout>":
        return False, "SYST:LOG:STAT? timed out"
    return True, resp


def test_trace_last(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    resp = _query(ser, "SYST:TRAC:LAST?", timeout)
    if resp == "<timeout>":
        return False, "SYST:TRAC:LAST? timed out"
    return True, resp


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

ALL_TESTS = [
    ("identity", test_identity),
    ("build_id", test_build_id),
    ("core_heartbeat", test_core_heartbeat),
    ("loop_status", test_loop_status),
    ("vdc_status", test_vdc_status),
    ("dpll_status", test_dpll_status),
    ("calibration_status", test_calibration_status),
    ("config_gate_status", test_config_gate_status),
    ("config_snapshot_queries", test_config_snapshot_queries),
    ("ack_reason_and_run_policy", test_ack_reason_and_run_policy),
    ("runtime_protection_tables", test_runtime_protection_tables),
    ("system_resource_fault_tables", test_system_resource_fault_tables),
    ("trigger_seq", test_trigger_seq),
    ("error_queue", test_error_queue),
    ("log_stat", test_log_stat),
    ("trace_last", test_trace_last),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, e.g. COM4")
    parser.add_argument("--timeout", type=float, default=5.0, help="per-command timeout")
    parser.add_argument("--settle", type=float, default=1.5,
                        help="seconds to wait after opening the port")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    parser.add_argument("--tests", nargs="*", choices=[t[0] for t in ALL_TESTS] + ["all"],
                        default=["all"], help="which tests to run (default: all)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    # resolve test list
    if "all" in args.tests:
        test_names = [t[0] for t in ALL_TESTS]
    else:
        test_names = args.tests

    # output dir
    out_dir: Path
    if args.out_dir:
        out_dir = args.out_dir.resolve()
    else:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_dir = ROOT / "build-rtos-multicore-smoke" / f"multicore_validation_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)

    # open port
    print(f"Opening {args.port} …")
    results: list[dict] = []
    passed_all = True

    with serial.Serial(args.port, 115200, timeout=0.3) as ser:
        time.sleep(args.settle)
        print(f"Port open.  Output dir: {out_dir}\n")

        try:
            for name in test_names:
                fn = dict(ALL_TESTS)[name]
                print(f"--- {name} ---")
                try:
                    ok, detail = fn(ser, args.timeout)
                except Exception as exc:
                    ok, detail = False, f"exception: {exc}"
                status = "PASS" if ok else "FAIL"
                print(f"  {status}: {detail}")
                results.append({"test": name, "passed": ok, "detail": detail})
                if not ok:
                    passed_all = False
        finally:
            try:
                ser.reset_input_buffer()
                ser.reset_output_buffer()
            except Exception:
                pass

    # summary
    passed = sum(1 for r in results if r["passed"])
    total = len(results)
    print(f"\n{'='*40}")
    print(f"Result: {passed}/{total} passed")

    summary = {
        "title": "RTOS + Multicore AMP Smoke",
        "timestamp": datetime.now().isoformat(),
        "passed": passed,
        "total": total,
        "overall": "PASS" if passed_all else "FAIL",
        "tests": results,
    }
    summary_path = out_dir / "summary.json"
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
    print(f"Summary: {summary_path}")

    # copy SCPI output transcript to out_dir
    transcript_path = out_dir / "scpi_log.txt"
    with open(transcript_path, "w", encoding="utf-8") as f:
        for r in results:
            f.write(f"[{r['test']}] {'PASS' if r['passed'] else 'FAIL'}: {r['detail']}\n")
    print(f"Transcript: {transcript_path}")

    return 0 if passed_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
