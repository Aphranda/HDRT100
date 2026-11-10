#!/usr/bin/env python3
"""Transports, dispatcher and TCP pass-through for the trigger debug demo.

The GUI and the local TCP server share exactly one transport session: one
serial port, or one USB TMC / PyVISA resource.  A single dispatcher thread owns
the transport and serializes every command, so the two clients can never write
to the instrument concurrently.
"""

from __future__ import annotations

from dataclasses import dataclass
import queue
import random
import re
import struct
import threading
import time
from typing import Callable

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - host dependency
    serial = None
    list_ports = None

try:
    import pyvisa
except ImportError:  # pragma: no cover - optional host dependency
    pyvisa = None


class TransportError(RuntimeError):
    pass


@dataclass(frozen=True)
class Result:
    request_id: int
    command: str
    response: str
    error: str
    elapsed_ms: float
    source: str


def available_ports() -> list[str]:
    if list_ports is None:
        return []
    found = []
    for info in list_ports.comports():
        device = str(info.device)
        match = re.fullmatch(r"COM(\d+)", device, re.IGNORECASE)
        number = int(match.group(1)) if match else 0x7FFFFFFF
        # Prefer the Raspberry Pi VID used by DHRT100, then natural COM order.
        found.append((0 if getattr(info, "vid", None) == 0x2E8A else 1,
                      number, device, info.description or ""))
    found.sort()
    return [f"{device}  {description}".strip()
            for _vendor, _number, device, description in found]


def port_device(label: str) -> str:
    return label.split("  ", 1)[0].strip()


def available_visa_resources() -> list[str]:
    """List USB TMC instruments known to the local VISA runtime."""
    if pyvisa is None:
        return []
    try:
        manager = pyvisa.ResourceManager()
        try:
            resources = [str(item) for item in manager.list_resources("USB?*::INSTR")]
        except Exception:
            resources = [str(item) for item in manager.list_resources()]
        finally:
            manager.close()
    except Exception:
        return []
    usb = [item for item in resources if item.upper().startswith("USB")]
    # DHRT100 uses the Raspberry Pi VID; put it first when present.
    usb.sort(key=lambda item: (0 if "0X2E8A" in item.upper() else 1, item))
    return usb


# --------------------------------------------------------------------- serial

class SerialTransport:
    """One explicitly owned CDC port."""

    def __init__(self, port: str, baud: int = 115200, timeout_s: float = 2.0) -> None:
        if serial is None:
            raise TransportError("pyserial 未安装：python -m pip install pyserial")
        self.port = port
        self.baud = baud
        self.timeout_s = timeout_s
        self.ser: serial.Serial | None = None

    def open(self) -> None:
        if self.ser is not None:
            return
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.1,
                                     write_timeout=self.timeout_s)
            time.sleep(0.15)
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
        except Exception as exc:
            self.close()
            raise TransportError(f"打开 {self.port} 失败：{exc}") from exc

    def close(self) -> None:
        if self.ser is None:
            return
        port = self.ser
        self.ser = None
        try:
            port.flush()
        except Exception:
            pass
        try:
            port.close()
        except Exception:
            pass

    def execute(self, command: str) -> str:
        if self.ser is None:
            raise TransportError("串口未连接")
        try:
            self.ser.reset_input_buffer()
            self.ser.write((command + "\n").encode("ascii"))
            self.ser.flush()
        except Exception as exc:
            raise TransportError(f"发送失败：{exc}") from exc
        return self._read_response(command)

    def _read_response(self, command: str) -> str:
        assert self.ser is not None
        deadline = time.monotonic() + self.timeout_s
        query = "?" in command.split(maxsplit=1)[0]
        while time.monotonic() < deadline:
            try:
                line = self.ser.readline().decode("utf-8", errors="replace").strip()
            except Exception as exc:
                raise TransportError(f"读取失败：{exc}") from exc
            if not line:
                if query:
                    continue
                return "OK"
            if _is_log_line(line):
                continue
            return line
        return "<timeout>"

    def execute_bytes(self, payload: bytes, expect_response: bool = True) -> str:
        """Send one SCPI message whose body is an arbitrary block."""
        if self.ser is None:
            raise TransportError("串口未连接")
        try:
            self.ser.reset_input_buffer()
            self.ser.write(payload + b"\n")
            self.ser.flush()
        except Exception as exc:
            raise TransportError(f"发送二进制块失败：{exc}") from exc
        if not expect_response:
            return "OK"
        return self._read_response(payload.split(b"#", 1)[0].decode(
            "ascii", errors="replace"))


def _is_log_line(line: str) -> bool:
    text = line.strip().lstrip('"')
    return (not text or text.startswith("[") or text.startswith("log:")
            or " initialized" in text or " service initialized" in text)


# ---------------------------------------------------------------------- VISA

class VisaTransport:
    """One USB TMC / PyVISA session.

    Firmware answers most write handlers with a numeric result (``1`` or
    ``1,0``), so this transport attempts one read after a write.  A pure write
    that answers nothing yields ``OK``; a query that answers nothing yields
    ``<timeout>``.  The next normal command therefore does not inherit a stale
    response.
    """

    def __init__(self, resource: str, timeout_s: float = 2.0) -> None:
        if pyvisa is None:
            raise TransportError("pyvisa 未安装：python -m pip install pyvisa")
        self.resource = resource
        self.timeout_s = timeout_s
        self.manager = None
        self.instrument = None

    def open(self) -> None:
        if self.instrument is not None:
            return
        try:
            self.manager = pyvisa.ResourceManager()
            instrument = self.manager.open_resource(self.resource)
            instrument.timeout = max(100, int(self.timeout_s * 1000))
            instrument.write_termination = "\n"
            instrument.read_termination = "\n"
            self.instrument = instrument
        except Exception as exc:
            self.close()
            raise TransportError(f"打开 VISA 资源 {self.resource} 失败：{exc}") from exc

    def close(self) -> None:
        instrument, manager = self.instrument, self.manager
        self.instrument = None
        self.manager = None
        if instrument is not None:
            try:
                instrument.close()
            except Exception:
                pass
        if manager is not None:
            try:
                manager.close()
            except Exception:
                pass

    def execute(self, command: str) -> str:
        if self.instrument is None:
            raise TransportError("VISA 会话未连接")
        query = "?" in command.split(maxsplit=1)[0]
        try:
            self.instrument.write(command)
        except Exception as exc:
            raise TransportError(f"VISA 写失败：{exc}") from exc
        # Write handlers normally answer promptly.  Do not spend the full query
        # timeout on a write that intentionally has no response.
        previous_timeout = getattr(self.instrument, "timeout", 2000)
        self.instrument.timeout = max(100, int(self.timeout_s * 1000)) if query else 250
        try:
            response = self.instrument.read()
        except Exception:
            return "<timeout>" if query else "OK"
        finally:
            try:
                self.instrument.timeout = previous_timeout
            except Exception:
                pass
        text = str(response).strip()
        return text if text else ("<timeout>" if query else "OK")

    def execute_bytes(self, payload: bytes, expect_response: bool = True) -> str:
        if self.instrument is None:
            raise TransportError("VISA 会话未连接")
        try:
            if hasattr(self.instrument, "write_raw"):
                self.instrument.write_raw(payload + b"\n")
            else:
                self.instrument.write(payload + b"\n")
        except Exception as exc:
            raise TransportError(f"VISA 写二进制块失败：{exc}") from exc
        if not expect_response:
            return "OK"
        previous_timeout = getattr(self.instrument, "timeout", 2000)
        self.instrument.timeout = max(1000, int(self.timeout_s * 1000))
        try:
            if hasattr(self.instrument, "read_raw"):
                response = self.instrument.read_raw()
            else:
                response = self.instrument.read()
        except Exception:
            return "<timeout>"
        finally:
            try:
                self.instrument.timeout = previous_timeout
            except Exception:
                pass
        return str(response).strip()


# ------------------------------------------------------------------ simulator

BOARD_ROLES = ("A0 扫描/时间", "A1 DUT", "A2 馈源", "A3 网关")

CATALOG: tuple[tuple[int, str, int, int, int, int, int], ...] = (
    (1, "REFMEM_LOAD_BOARD", 15, 0, 9, 0, 0),
    (2, "REFMEM_LOAD_NODE", 14, 0, 7, 0, 0),
    (3, "REFMEM_LOAD_SD", 16, 0, 4, 0, 0),
    (4, "REFMEM_MAINTENANCE_SELF_CHECK", 13, 0, 1, 0, 0),
    (5, "REFMEM_LEASE_TAKE", 18, 0, 2, 0, 0),
    (6, "REFMEM_LEASE_RELEASE", 19, 0, 1, 0, 0),
    (20, "TDMA_RING_TOPOLOGY", 20, 1, 2, 0, 0),
    (21, "TDMA_SEAT_ROTATION", 21, 1, 3, 0, 0),
    (22, "TDMA_PERIOD", 22, 1, 1, 0, 0),
    (23, "TDMA_LOAD_MASK", 23, 1, 1, 0, 0),
    (24, "DPLL_ROLE", 24, 4, 2, 0, 0),
    (25, "DPLL_TUNE", 25, 4, 5, 0, 0),
    (26, "DPLL_OVERRIDE", 26, 4, 1, 0, 0),
    (27, "VDC_REFERENCE_CONFIG", 27, 4, 5, 0, 0),
    (28, "VDC_OUTPUT_DELAY", 28, 4, 1, 0, 0),
    (29, "VDC_OUTPUT_TIMING", 29, 4, 3, 0, 0),
    (30, "T2_CONFIGURE", 30, 2, 13, 0, 0),
    (31, "T2_MOUNT", 31, 2, 3, 0, 0),
    (32, "T2_UNMOUNT", 32, 2, 0, 0, 0),
    (33, "VDC_STOP", 33, 4, 0, 0, 0),
    (34, "VDC_START", 34, 4, 0, 0, 0),
    (36, "TRIGGER_START", 36, 5, 4, 0, 0),
    (37, "TRIGGER_STOP", 37, 5, 0, 0, 0),
    (38, "TRIGGER_PAUSE", 38, 5, 0, 0, 0),
    (39, "TRIGGER_CONTINUE", 39, 5, 0, 0, 0),
    (40, "TRIGGER_ABORT", 40, 5, 0, 0, 0),
    (45, "TRIGGER_SWITCH", 45, 5, 2, 0, 0),
    (35, "TRIGGER_MODE", 35, 5, 1, 0, 0),
    (41, "TRIGGER_CONFIGURE", 41, 5, 4, 0, 0),
    (42, "TRIGGER_ANGLE_SWEEP", 42, 5, 3, 0, 0),
    (43, "TRIGGER_ANGLE_PULSE", 43, 5, 3, 0, 0),
    (44, "TRIGGER_ANGLE_BREAKPOINT", 44, 5, 2, 0, 0),
    (46, "TRIGGER_ANGLE_BREAKPOINT_CLEAR", 46, 5, 0, 0, 0),
    (47, "TRIGGER_SEQUENCE_ACTIVE", 47, 5, 4, 0, 0),
    (48, "TRIGGER_SEQUENCE_PLAN", 48, 5, 16, 0, 0),
    (49, "TRIGGER_ANGLE_SPEED", 49, 5, 1, 0, 0),
)

CATALOG_BY_NAME = {entry[1]: entry for entry in CATALOG}
CATALOG_BY_ID = {entry[0]: entry for entry in CATALOG}

CMD_STATE = {0: "IDLE", 1: "POSTED", 2: "TAKEN", 3: "BUSY", 4: "ACKED",
             5: "NACKED", 6: "TIMED_OUT"}


def _bits_to_float(word: int) -> float:
    return struct.unpack("<f", struct.pack("<I", word & 0xFFFFFFFF))[0]


class SimulatedTransport:
    """A four-board coordinator simulator with REFMEM aggregate ACK."""

    def __init__(self, ack_delay_s: float = 0.25, seed: int = 7) -> None:
        self.ack_delay_s = ack_delay_s
        self.random = random.Random(seed)
        self.opened = False
        self.mode = 0
        self.run_state = 0          # 0 IDLE 1 RUN 2 PAUSE
        self.plan_id = "PLAN_A"
        self.plan_states = [0, 1, 2, 3, 4, 5]
        self.plan_generation = 1
        self.plan_crc = 0x02030405
        self.active_plan = "PLAN_A"
        self.active_valid = 1
        self.chan, self.pol, self.freq, self.wave = 8, 2, 5, 1
        self.sweep = (0.0, 360.0, 1.0)
        self.pulse = (0, 10, 5000)
        self.breakpoint = (180.0, 1, 0)
        self.speed = 10.0
        self.switch1 = (1, 1)
        self.switch2 = 0
        self.repeat = 1
        self.ota_state = "IDLE"
        self.ota_size = 0
        self.ota_crc = 0
        self.ota_received = 0
        self.ota_active = 1
        self.ota_pending = 0
        self.ota_confirmed = 1
        self.ota_target = 2
        self.ota_boot_attempts = 0
        self.ota_rollback = 0
        self.ota_package = False
        self.ota_last_result = 0
        self.command_seq = 100
        self.aggregate = {
            "seq": 0, "state": 0, "command_seq": 0, "target_mask": 0,
            "required_mask": 0, "ack_flags": 0, "nack_flags": 0,
            "busy_flags": 0, "timeout_flags": 0, "last_reason": 0,
            "last_reason_slot": 0, "last_completed_seq": 0,
        }
        self.boards = [self._new_board(slot) for slot in range(4)]
        self.pending_deadline = 0.0
        self.pending_targets = 0
        self.pending_required = 0

    @staticmethod
    def _new_board(slot: int) -> dict:
        return {
            "slot": slot, "online": 1, "command_seq": 0, "state": 0,
            "command_type": 0, "target_mask": 0, "required_mask": 0,
            "ack_flags": 0, "nack_flags": 0, "last_reason": 0,
        }

    def open(self) -> None:
        self.opened = True

    def close(self) -> None:
        self.opened = False

    # ------------------------------------------------------------- transport
    def execute(self, command: str) -> str:
        text = command.strip()
        if not text:
            raise TransportError("空命令")
        header, _, rest = text.partition(" ")
        upper = header.upper()
        args = [item.strip() for item in rest.split(",") if item.strip()] if rest else []
        self._tick()
        handler = getattr(self, "_cmd_" + _slug(upper), None)
        if handler is not None:
            return handler(args)
        if upper.endswith("?"):
            return "0"
        return "1"

    def execute_bytes(self, payload: bytes, expect_response: bool = True) -> str:
        text = payload.decode("ascii", errors="replace")
        header = text.split("#", 1)[0].strip()
        upper = header.upper()
        if upper.startswith("SYSTEM:OTA:DATA") or upper.startswith("SYST:OTA:DATA"):
            block = payload[payload.find(b"#"):] if b"#" in payload else b""
            count = _scpi_block_length(block)
            self.ota_received = min(self.ota_size, self.ota_received + count)
            return "OK"
        return self.execute(header)

    # ------------------------------------------------------------- helpers
    def _tick(self) -> None:
        if self.pending_deadline and time.monotonic() >= self.pending_deadline:
            self._complete_pending()

    def _complete_pending(self) -> None:
        self.pending_deadline = 0.0
        for slot in range(4):
            board = self.boards[slot]
            if self.pending_targets & (1 << slot):
                board["state"] = 4
                board["ack_flags"] = 1 << slot
                board["nack_flags"] = 0
        self.aggregate["state"] = 4
        self.aggregate["ack_flags"] = self.pending_targets
        self.aggregate["busy_flags"] = 0
        self.aggregate["last_completed_seq"] = self.aggregate["seq"]

    def _submit(self, name: str, params: list[int], target: int, required: int) -> str:
        if name not in CATALOG_BY_NAME:
            raise TransportError(f"目录中没有命令 {name}")
        entry = CATALOG_BY_NAME[name]
        if len(params) != entry[4]:
            raise TransportError(f"{name} 需要 {entry[4]} 个参数，实际 {len(params)}")
        if target == 0 or target & ~0xF:
            raise TransportError("目标掩码必须是 1..15")
        if required & ~target:
            raise TransportError("必需掩码必须是目标掩码的子集")
        self.command_seq += 1
        seq = self.command_seq
        self.aggregate.update({
            "seq": seq, "state": 3, "command_seq": seq, "target_mask": target,
            "required_mask": required, "ack_flags": 0, "nack_flags": 0,
            "busy_flags": target, "timeout_flags": 0, "last_reason": 0,
            "last_reason_slot": 0,
        })
        for slot in range(4):
            board = self.boards[slot]
            if target & (1 << slot):
                board.update({"command_seq": seq, "state": 3,
                              "command_type": entry[2], "target_mask": target,
                              "required_mask": required, "ack_flags": 0,
                              "nack_flags": 0, "last_reason": 0})
        self.pending_targets = target
        self.pending_required = required
        self.pending_deadline = time.monotonic() + self.ack_delay_s
        if self.ack_delay_s <= 0:
            self._complete_pending()
        return f"QUEUED,{seq}"

    def _apply_triggers(self, name: str, params: list[int]) -> None:
        if name == "TRIGGER_MODE":
            self.mode = params[0]
        elif name == "TRIGGER_START":
            self.run_state = 1
            plan = _decode_plan(params[:4])
            if plan:
                self.active_plan = plan
                self.active_valid = 1
        elif name in ("TRIGGER_STOP", "TRIGGER_ABORT"):
            self.run_state = 0
        elif name == "TRIGGER_PAUSE":
            self.run_state = 2
        elif name == "TRIGGER_CONTINUE":
            self.run_state = 1
        elif name == "TRIGGER_CONFIGURE":
            self.chan, self.pol, self.freq, self.wave = params[:4]
        elif name == "TRIGGER_ANGLE_SWEEP":
            self.sweep = (_bits_to_float(params[0]), _bits_to_float(params[1]),
                          _bits_to_float(params[2]))
        elif name == "TRIGGER_ANGLE_PULSE":
            self.pulse = tuple(params[:3])
        elif name == "TRIGGER_ANGLE_BREAKPOINT":
            self.breakpoint = (_bits_to_float(params[1]), params[0], 0)
        elif name == "TRIGGER_ANGLE_BREAKPOINT_CLEAR":
            self.breakpoint = (self.breakpoint[0], 0, 0)
        elif name == "TRIGGER_ANGLE_SPEED":
            self.speed = _bits_to_float(params[0])
        elif name == "TRIGGER_SWITCH":
            if params[0] == 1:
                self.switch1 = (params[1], 1)
            else:
                self.switch2 = params[1]
        elif name == "TRIGGER_SEQUENCE_PLAN":
            count = params[4]
            self.plan_id = _decode_plan(params[:4])
            self.plan_states = params[5:5 + count]
            self.plan_generation += 1
        elif name == "TRIGGER_SEQUENCE_ACTIVE":
            self.active_plan = _decode_plan(params[:4])
            self.active_valid = 1

    # ------------------------------------------------------------- commands
    def _cmd_star_idn(self, args: list[str]) -> str:
        return "DHRT100,SIMULATOR,DEMO-0001,0.1.0"

    def _cmd_system_fw_build(self, args: list[str]) -> str:
        return "20261102153700"

    def _cmd_system_board_no(self, args: list[str]) -> str:
        return "1"

    def _cmd_system_board_id(self, args: list[str]) -> str:
        return "SIM-BOARD-0001"

    def _cmd_system_error(self, args: list[str]) -> str:
        return '0,"No error"'

    def _cmd_system_fault_last(self, args: list[str]) -> str:
        return "0,0,NONE"

    def _cmd_system_tdma_ring_status(self, args: list[str]) -> str:
        return "1,0,0,0,4,0,0,0,0,0"

    def _cmd_read_trigger_state(self, args: list[str]) -> str:
        return ",".join(str(value) for value in (
            '"TRIG"', '"RUN"' if self.run_state == 1 else '"IDLE"',
            self.aggregate["seq"], f'"{self.active_plan}"', int(self.sweep[0]),
            0, max(0, int((self.sweep[1] - self.sweep[0]) / self.sweep[2]) + 1),
            self.command_seq % 8, len(self.plan_states), self.command_seq,
            0, 0, 0, '"OK"', 0, 1, 1, 1, self.aggregate["ack_flags"],
            self.aggregate["ack_flags"], 0, self.aggregate["ack_flags"],
            self.aggregate["target_mask"]))

    def _cmd_read_trigger_parameter(self, args: list[str]) -> str:
        return ",".join(map(str, (self.chan, self.pol, self.freq, self.wave, 80,
                                  self.plan_crc, 0x22223333, 0x33334444, 0x44445555)))

    def _cmd_read_sequence(self, args: list[str]) -> str:
        states = (self.plan_states + [0] * 6)[:6]
        return ",".join([f'"{self.plan_id}"', str(self.plan_generation), str(self.plan_crc),
                         str(len(self.plan_states)), "1"] + [str(value) for value in states])

    def _cmd_read_sequence_map(self, args: list[str]) -> str:
        return "0,1,0,0,0,0"

    def _cmd_read_sequence_active(self, args: list[str]) -> str:
        return ",".join([f'"{self.active_plan}"', str(self.plan_crc),
                         str(len(self.plan_states)), str(self.active_valid),
                         '"PASS"', '"OK"'])

    def _cmd_read_sequence_check(self, args: list[str]) -> str:
        return ",".join([f'"{self.plan_id}"', str(len(self.plan_states)),
                         str(self.plan_crc)] + ["1"] * 6 + ["0", "0", '"NONE"'])

    def _cmd_read_angle_sweep(self, args: list[str]) -> str:
        count = max(0, int((self.sweep[1] - self.sweep[0]) / self.sweep[2]) + 1)
        return f"{self.sweep[0]},{self.sweep[1]},{self.sweep[2]},{count},0"

    def _cmd_read_angle_pulse(self, args: list[str]) -> str:
        return ",".join(map(str, (self.pulse[0], self.pulse[1], self.pulse[2],
                                  0, 0, 0, '"OK"', 1, self.speed / max(self.sweep[2], 1e-9))))

    def _cmd_read_angle_breakpoint(self, args: list[str]) -> str:
        return f"{self.breakpoint[0]},{int(bool(self.breakpoint[1]))},{self.breakpoint[2]},0"

    def _cmd_read_angle_position(self, args: list[str]) -> str:
        return ",".join([str(value) for value in (
            "DTC_SWEEP", 0, max(0, int((self.sweep[1] - self.sweep[0]) / self.sweep[2]) + 1),
            int(self.sweep[0]), int(self.sweep[0]) + int(self.sweep[2]), 0, 0,
            1 if self.run_state == 1 else 0, 1 if self.run_state == 2 else 0, 0)])

    def _cmd_read_switch1(self, args: list[str]) -> str:
        return f"1,1,1,{self.switch1[0]},{self.switch1[0]},0"

    def _cmd_read_switch2(self, args: list[str]) -> str:
        return f"2,1,1,{self.switch2},{self.switch2},0"

    def _cmd_system_ctrl_owner(self, args: list[str]) -> str:
        return "0,1,0,1"

    def _cmd_system_ctrl_status(self, args: list[str]) -> str:
        a = self.aggregate
        return ",".join(map(str, (a["seq"], 0, a["state"], a["command_seq"],
                                  a["target_mask"], a["required_mask"], a["ack_flags"],
                                  a["nack_flags"], a["busy_flags"], a["timeout_flags"],
                                  a["last_reason"], a["last_reason_slot"],
                                  a["last_completed_seq"])))

    def _cmd_system_refmem_cmd_reply(self, args: list[str]) -> str:
        a = self.aggregate
        return ",".join(map(str, (a["seq"], a["ack_flags"], 0, 0, 0,
                                  a["target_mask"], a["seq"], a["seq"], a["state"],
                                  a["state"], a["ack_flags"], a["nack_flags"], 0,
                                  a["state"], a["last_reason"])))

    def _cmd_system_command_status(self, args: list[str]) -> str:
        a = self.aggregate
        acked = bin(a["ack_flags"]).count("1")
        return ",".join(map(str, (4, a["state"], 0, 0, acked, 0, 0,
                                  a["state"], a["last_reason"], a["last_reason_slot"],
                                  a["last_completed_seq"])))

    def _cmd_system_command_target(self, args: list[str]) -> str:
        slot = int(args[0]) if args else 0
        if not 0 <= slot < 4:
            raise TransportError("槽位必须是 0..3")
        b = self.boards[slot]
        return ",".join(map(str, (slot, b["command_seq"], b["state"], b["command_type"],
                                  b["target_mask"], b["required_mask"], b["ack_flags"],
                                  b["nack_flags"], b["last_reason"])))

    def _cmd_system_ctrl_catalog(self, args: list[str]) -> str:
        offset = int(args[0]) if args else 0
        count = min(int(args[1]) if len(args) > 1 else 16, 16)
        values: list[str] = []
        for entry in CATALOG[offset:offset + count]:
            values.extend(map(str, entry))
        return ",".join(values)

    def _cmd_system_refmem_nodes(self, args: list[str]) -> str:
        offset = int(args[0]) if args else 0
        count = min(int(args[1]) if len(args) > 1 else 4, 8)
        values: list[str] = []
        for slot in range(offset, min(offset + count, 4)):
            values.extend(map(str, (slot, 1, 100 + slot, 1, 1000, 0, 0, 0, 0, 0,
                                    1, 1, 0, 0, 0)))
        return ",".join(values)

    OBS_SCHEMA_WORDS = {1: 2, 2: 2, 3: 3, 4: 7, 5: 5, 6: 6, 7: 3, 8: 4}

    def _obs_words(self, key: int) -> list[int]:
        if key == 1:
            return [self.switch1[0], self.switch1[1]]
        if key == 2:
            return [self.switch2, self.switch2]
        if key == 3:
            return [self.run_state, 1 if self.run_state == 1 else 0, 0]
        if key == 4:
            return [1, self.plan_generation, 0, 0, 0, 0, 0]
        if key == 5:
            return [1, 0, 1, 0, 2]
        if key == 6:
            return [1, 1, 1, 1, 4, 0]
        if key == 7:
            return [1, self.plan_generation, 0]
        return [1, 100, 0, 0]

    def _cmd_system_refmem_obs(self, args: list[str]) -> str:
        """REFTMEM-READ-01 per-slot rows; slot 0 is the local board."""
        key = int(args[0], 0) if args else 0
        offset = int(args[1], 0) if len(args) > 1 else 0
        count = int(args[2], 0) if len(args) > 2 else 4
        schema_words = self.OBS_SCHEMA_WORDS.get(key)
        if schema_words is None:
            return '-200,"OBS_KEY"'
        if offset < 0 or offset >= 4:
            return '-200,"OBS_OFFSET"'
        if count <= 0 or count > 4 - offset:
            count = 4 - offset
        values = [key, offset, count]
        for slot in range(offset, offset + count):
            if slot == 0:
                values += [slot, 1, 1, self.command_seq, 0, 0]
                values += self._obs_words(key)
            else:
                values += [slot, 0, 0, 0, 0xFFFFFFFF, 1] + [0] * schema_words
        return ",".join(str(value) for value in values)

    def _cmd_system_refmem_node_control(self, args: list[str]) -> str:
        slot = int(args[0]) if args else 0
        b = self.boards[slot] if 0 <= slot < 4 else self.boards[0]
        return ",".join(map(str, (slot, b["command_seq"], 0, b["state"],
                                  b["command_seq"], 0, b["target_mask"],
                                  b["required_mask"], b["command_type"], 5,
                                  b["ack_flags"], b["nack_flags"], 0, 0,
                                  b["last_reason"], 0, 0, b["command_seq"], 1, 1, 0)))

    def _cmd_system_refmem_cmdframe(self, args: list[str]) -> str:
        return "12,0,0,0,12"

    def _cmd_system_refmem_cmd_status(self, args: list[str]) -> str:
        return ",".join(map(str, (1, 0, 0, 0, 0, self.command_seq, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0)))

    def _cmd_system_ctrl_execute(self, args: list[str]) -> str:
        if len(args) < 3:
            raise TransportError("CTRL:EXECute 需要 command,target,required")
        name = args[0]
        target = int(args[1], 10)
        required = int(args[2], 10)
        try:
            params = [int(item, 10) for item in args[3:]]
        except ValueError as exc:
            raise TransportError("参数必须是十进制 u32 word") from exc
        self._submit(name, params, target, required)
        self._apply_triggers(name, params)
        return f"QUEUED,{self.aggregate['seq']}"

    def _cmd_system_refmem_cmd_submit(self, args: list[str]) -> str:
        if not args:
            raise TransportError("CMD:SUBmit 需要 hex 帧")
        hex_text = args[0].strip()
        if len(hex_text) != 112:
            raise TransportError("命令帧必须正好 56 字节（112 hex 字符）")
        try:
            bytes.fromhex(hex_text)
        except ValueError as exc:
            raise TransportError("命令帧不是合法 hex") from exc
        self.command_seq += 1
        return "QUEUED"

    # --------------------------------------------------------------- OTA demo
    def _cmd_system_ota_begin(self, args: list[str]) -> str:
        if len(args) < 2:
            raise TransportError("OTA:BEGIN 需要 size,crc32")
        self.ota_size = int(args[0], 10)
        self.ota_crc = int(args[1], 10)
        self.ota_received = 0
        self.ota_package = False
        self.ota_target = 3 - self.ota_active if self.ota_active in (1, 2) else 2
        self.ota_state = "RECEIVING"
        return "1"

    def _cmd_system_ota_pbegin(self, args: list[str]) -> str:
        self._cmd_system_ota_begin(args)
        self.ota_package = True
        return "1"

    def _cmd_system_ota_data(self, args: list[str]) -> str:
        return "OK"

    def _cmd_system_ota_end(self, args: list[str]) -> str:
        if self.ota_state != "RECEIVING":
            return "0"
        self.ota_state = "READY_TO_REBOOT"
        self.ota_pending = self.ota_target
        return "1"

    def _cmd_system_ota_abort(self, args: list[str]) -> str:
        self.ota_state = "ABORTED"
        self.ota_received = 0
        return "1"

    def _cmd_system_ota_boot(self, args: list[str]) -> str:
        if self.ota_state != "READY_TO_REBOOT":
            return "0"
        self.ota_state = "REBOOTING"
        self.ota_boot_attempts += 1
        return "1"

    def _cmd_system_ota_commit(self, args: list[str]) -> str:
        self.ota_confirmed = self.ota_target
        self.ota_active = self.ota_target
        self.ota_pending = 0
        self.ota_state = "APPLIED"
        self.ota_last_result = 0
        return "1"

    def _cmd_system_ota_status(self, args: list[str]) -> str:
        return f'"{self.ota_state}",{self.ota_target},"NONE",{self.ota_last_result}'

    def _cmd_system_ota_progress(self, args: list[str]) -> str:
        permille = (self.ota_received * 1000 // self.ota_size
                    if self.ota_size else 0)
        return f"{self.ota_received},{self.ota_size},{permille},0"

    def _cmd_system_ota_result(self, args: list[str]) -> str:
        return (f"{self.ota_last_result},NONE,OK,{self.ota_target},"
                f"{self.ota_size},{self.ota_crc}")

    def _cmd_system_ota_slot(self, args: list[str]) -> str:
        return (f"{self.ota_active},{self.ota_pending},{self.ota_confirmed},"
                f"{self.ota_boot_attempts},{self.ota_rollback}")

    def _cmd_system_ota_target(self, args: list[str]) -> str:
        return str(3 - self.ota_active if self.ota_active in (1, 2) else 2)

    def _cmd_system_ota_mode(self, args: list[str]) -> str:
        return "DIRECT_AB,1"

    def _cmd_system_ota_capability(self, args: list[str]) -> str:
        return "2"

    def _cmd_system_ota_journal(self, args: list[str]) -> str:
        return ",".join(map(str, (1, 0, 1, 1, 1, self.ota_crc, 1,
                                  self.ota_received, self.ota_size,
                                  self.ota_crc, self.ota_crc, 0, 0)))

    def _cmd_system_ota_txn(self, args: list[str]) -> str:
        return "0,0,0,0,0,0,0,0"

    def _cmd_system_ota_bcb_health(self, args: list[str]) -> str:
        return "2,4,1,1,1,0,0"

    def _cmd_system_ota_bcb_wear(self, args: list[str]) -> str:
        return "0,0"

    def _cmd_system_ota_stream_status(self, args: list[str]) -> str:
        return "0,0,0,0,0"

    def _cmd_system_ota_stream_capability(self, args: list[str]) -> str:
        return "4096,7,1"

    def _cmd_system_ota_tdma_diag(self, args: list[str]) -> str:
        return ",".join(["0"] * 26)

    # ------------------------------------------------------------- writes
    def _cmd_trigger_mode(self, args: list[str]) -> str:
        self.mode = int(args[0]) if args else 0
        return "1"

    def _cmd_trigger_start(self, args: list[str]) -> str:
        self.run_state = 1
        if args:
            self.active_plan = args[0].strip('"')
        return "1"

    def _cmd_trigger_stop(self, args: list[str]) -> str:
        self.run_state = 0
        return "1"

    def _cmd_trigger_pause(self, args: list[str]) -> str:
        self.run_state = 2
        return "1"

    def _cmd_trigger_continue(self, args: list[str]) -> str:
        self.run_state = 1
        return "1"

    def _cmd_trigger_abort(self, args: list[str]) -> str:
        self.run_state = 0
        return "1"

    def _cmd_configure_trigger(self, args: list[str]) -> str:
        values = [int(item) for item in args[:4]]
        if len(values) == 4:
            self.chan, self.pol, self.freq, self.wave = values
        return "1"

    def _cmd_configure_angle_sweep(self, args: list[str]) -> str:
        if len(args) >= 3:
            self.sweep = tuple(float(item) for item in args[:3])
        return "1"

    def _cmd_configure_angle_pulse(self, args: list[str]) -> str:
        if len(args) >= 3:
            self.pulse = (int(args[0]), int(args[1]), int(args[2]))
        return "1"

    def _cmd_configure_angle_breakpoint(self, args: list[str]) -> str:
        if len(args) >= 2:
            self.breakpoint = (float(args[1]), int(args[0]), 0)
        return "1"

    def _cmd_configure_angle_breakpoint_clear(self, args: list[str]) -> str:
        self.breakpoint = (self.breakpoint[0], 0, 0)
        return "1"

    def _cmd_configure_angle_speed(self, args: list[str]) -> str:
        if args:
            self.speed = float(args[0])
        return "1"

    def _cmd_configure_switch1(self, args: list[str]) -> str:
        if len(args) >= 2:
            self.switch1 = (int(args[0]), int(args[1]))
        return "1"

    def _cmd_configure_switch2(self, args: list[str]) -> str:
        if args:
            self.switch2 = int(args[0])
        return "1"

    def _cmd_configure_sequence_active(self, args: list[str]) -> str:
        if args:
            self.active_plan = args[0].strip('"')
            self.active_valid = 1
        return "1"

    def _cmd_configure_sequence_repeat(self, args: list[str]) -> str:
        if args:
            self.repeat = int(args[0])
        return "1"

    def _cmd_configure_sequence(self, args: list[str]) -> str:
        if args:
            self.plan_id = args[0].strip('"')
            self.plan_states = [int(item) for item in args[1:]]
        return "1"


def _scpi_block_length(block: bytes) -> int:
    if len(block) < 2 or block[0:1] != b"#":
        return 0
    digits = block[1] - 0x30
    if not 1 <= digits <= 9 or len(block) < 2 + digits:
        return 0
    try:
        return int(block[2:2 + digits])
    except ValueError:
        return 0


def _binary_label(payload: bytes) -> str:
    prefix = payload.split(b"#", 1)[0].decode("ascii", errors="replace").strip()
    return f"{prefix} <{len(payload)} B>"


def _slug(header: str) -> str:
    return (header.replace("*", "star_").replace(":", "_")
            .replace("?", "").replace("-", "_").lower().strip("_"))


def _decode_plan(words: list[int]) -> str:
    raw = b"".join(int(word).to_bytes(4, "little") for word in words[:4])
    return raw.split(b"\0", 1)[0].decode("ascii", errors="replace")


# ------------------------------------------------------------------ dispatcher

@dataclass
class _SyncRequest:
    event: threading.Event
    result: "Result | None" = None


class Dispatcher(threading.Thread):
    """Serialize GUI and TCP commands on the single port.

    ``submit`` is asynchronous and is used by the Tk UI.  ``request`` blocks
    until the same dispatcher thread has executed the command, which is what
    the TCP pass-through needs.  There is exactly one transport owner, so a TCP
    client can never interleave bytes with a GUI command.
    """

    def __init__(self, transport, on_result: Callable[[Result], None]) -> None:
        super().__init__(name="trigger-demo-dispatcher", daemon=True)
        self.transport = transport
        self.on_result = on_result
        self.requests: queue.Queue = queue.Queue()
        self._halt = threading.Event()
        self._id_lock = threading.Lock()
        self._next_id = 0

    def _allocate(self) -> int:
        with self._id_lock:
            self._next_id += 1
            return self._next_id

    def submit(self, command: str, source: str = "") -> int:
        request_id = self._allocate()
        self.requests.put((request_id, command, source, None, None))
        return request_id

    def request(self, command: str, source: str = "tcp",
                timeout_s: float = 3.0) -> Result:
        request_id = self._allocate()
        slot = _SyncRequest(threading.Event())
        self.requests.put((request_id, command, source, slot, None))
        if not slot.event.wait(timeout_s):
            return Result(request_id, command, "", "等待设备响应超时",
                          timeout_s * 1000.0, source)
        assert slot.result is not None
        return slot.result

    def request_bytes(self, payload: bytes, source: str = "ota",
                      timeout_s: float = 15.0,
                      expect_response: bool = True) -> Result:
        """Send one raw SCPI message (used by OTA arbitrary blocks)."""
        request_id = self._allocate()
        slot = _SyncRequest(threading.Event())
        self.requests.put((request_id, payload, source, slot, expect_response))
        if not slot.event.wait(timeout_s):
            return Result(request_id, _binary_label(payload), "",
                          "等待设备响应超时", timeout_s * 1000.0, source)
        assert slot.result is not None
        return slot.result

    def stop(self) -> None:
        self._halt.set()
        self.requests.put((0, "", "", None, None))

    def run(self) -> None:
        while not self._halt.is_set():
            request_id, payload, source, slot, expect_response = self.requests.get()
            if request_id == 0:
                break
            started = time.monotonic()
            try:
                if isinstance(payload, bytes):
                    response = self.transport.execute_bytes(
                        payload, expect_response=True if expect_response is None
                        else bool(expect_response))
                    label = _binary_label(payload)
                else:
                    response = self.transport.execute(payload)
                    label = payload
                error = ""
            except Exception as exc:
                response = ""
                error = str(exc)
                label = (_binary_label(payload) if isinstance(payload, bytes)
                         else payload)
            elapsed = (time.monotonic() - started) * 1000.0
            result = Result(request_id, label, response, error, elapsed, source)
            try:
                self.on_result(result)
            finally:
                if slot is not None:
                    slot.result = result
                    slot.event.set()


# Backwards-compatible name for callers that only use submit().
Worker = Dispatcher


class DispatcherTransport:
    """Transport view that serializes a worker's reads through the Dispatcher.

    OBS/ControlPlane reads issued from a page worker must not touch the port
    directly: the Dispatcher stays the only transport owner, so a read can
    never interleave bytes with a queued write (CONSOLE-SESSION-01).
    """

    def __init__(self, dispatcher: Dispatcher, timeout_s: float = 5.0) -> None:
        self.dispatcher = dispatcher
        self.timeout_s = timeout_s

    def execute(self, command: str) -> str:
        result = self.dispatcher.request(command, "obs", self.timeout_s)
        if result.error:
            raise TransportError(result.error)
        return result.response

    def close(self) -> None:
        return None


# ------------------------------------------------------------------ TCP bridge

class TcpBridge:
    """Local line-oriented SCPI pass-through server.

    One SCPI command per line; one response line.  Commands are routed through
    the same :class:`Dispatcher` as the GUI, so the single serial/VISA session
    has exactly one writer.
    """

    def __init__(self, dispatcher: Dispatcher, host: str = "127.0.0.1",
                 port: int = 5025, on_event: Callable[[str, str], None] | None = None,
                 timeout_s: float = 3.0,
                 can_accept: Callable[[], bool] | None = None) -> None:
        self.dispatcher = dispatcher
        self.host = host
        self.port = port
        self.on_event = on_event or (lambda _kind, _text: None)
        self.timeout_s = timeout_s
        self.can_accept = can_accept
        self.server = None
        self.thread: threading.Thread | None = None
        self._clients = 0
        self._client_lock = threading.Lock()
        self._stop_event = threading.Event()
        self._stop_event.set()

    @property
    def running(self) -> bool:
        return self.server is not None

    @property
    def actual_port(self) -> int:
        if self.server is None:
            return self.port
        return int(self.server.server_address[1])

    @property
    def clients(self) -> int:
        with self._client_lock:
            return self._clients

    def start(self) -> None:
        if self.server is not None:
            return
        import socketserver

        bridge = self

        class Handler(socketserver.StreamRequestHandler):
            def handle(self) -> None:
                bridge._client_open()
                try:
                    while True:
                        line = self.rfile.readline()
                        if not line:
                            break
                        command = line.decode("utf-8", errors="replace").strip()
                        if not command:
                            continue
                        if command.upper() in ("QUIT", "EXIT"):
                            break
                        if bridge.can_accept is not None and not bridge.can_accept():
                            self.wfile.write(b"BUSY OTA\n")
                            self.wfile.flush()
                            continue
                        bridge.on_event("TCP>", command)
                        result = bridge.dispatcher.request(
                            command, "tcp", bridge.timeout_s)
                        response = result.error or result.response or "OK"
                        bridge.on_event("TCP<", response)
                        self.wfile.write(response.replace("\n", " ").encode(
                            "utf-8", errors="replace") + b"\n")
                        self.wfile.flush()
                except (ConnectionError, OSError):
                    pass
                finally:
                    bridge._client_close()

        class Server(socketserver.ThreadingTCPServer):
            daemon_threads = True
            allow_reuse_address = True

        server = Server((self.host, self.port), Handler)
        server.bridge = self  # type: ignore[attr-defined]
        self.server = server
        self._stop_event.clear()
        self.thread = threading.Thread(target=server.serve_forever,
                                       name="trigger-demo-tcp", daemon=True)
        self.thread.start()
        self.on_event("INFO", f"TCP 监听 {self.host}:{self.actual_port}")

    def stop(self) -> None:
        """Synchronous stop for tests and shutdown paths."""
        self.stop_async()
        self.wait_stopped(5.0)

    def stop_async(self) -> None:
        """Stop without blocking the Tk main thread.

        ``socketserver.shutdown()`` waits for ``serve_forever`` to leave its
        loop, so the UI must not call it directly.  The server is detached
        first so new clients are refused immediately.
        """
        server = self.server
        if server is None:
            self._stop_event.set()
            return
        self.server = None
        threading.Thread(target=self._stop_server, args=(server,),
                         name="trigger-demo-tcp-stop", daemon=True).start()

    def wait_stopped(self, timeout: float | None = None) -> bool:
        return self._stop_event.wait(timeout)

    def _stop_server(self, server) -> None:
        try:
            server.shutdown()
        except Exception:
            pass
        try:
            server.server_close()
        except Exception:
            pass
        self.thread = None
        self._stop_event.set()
        self.on_event("INFO", "TCP 已停止")

    def _client_open(self) -> None:
        with self._client_lock:
            self._clients += 1
        self.on_event("INFO", f"TCP 客户端接入（当前 {self.clients}）")

    def _client_close(self) -> None:
        with self._client_lock:
            self._clients = max(0, self._clients - 1)
        self.on_event("INFO", f"TCP 客户端断开（当前 {self.clients}）")
