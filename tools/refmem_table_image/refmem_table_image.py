"""Build canonical RefMem table image payloads and RMTP packages."""

from __future__ import annotations

import binascii
import struct
from dataclasses import dataclass


MAGIC = b"RMTP"
FORMAT_VERSION = 1
HEADER_SIZE = 64
TABLE_COUNT = 9
TABLE_NAMES = (
    "ApplicationMap",
    "BoardCapability",
    "GenericNode",
    "NodeLoad",
    "FbInstance",
    "EventLink",
    "DataLink",
    "DeploymentGate",
    "ConnectionQuality",
)

NODE_COUNT = 8
BOARD_CAPABILITY_COUNT = 16

CAP_BOARD = 0x00000001
CAP_FLASH = 0x00000002
CAP_SD = 0x00000004
CAP_USB = 0x00000008
CAP_PIO = 0x00000010
CAP_DMA = 0x00000020
CAP_RJ45 = 0x00000080
CAP_CORE1_RT = 0x00000100
CAP_SMA_IN = 0x00000200
CAP_SMA_OUT = 0x00000400
CAP_LINK_CONTROL = 0x00000800
CAP_BISS_C = 0x00001000
CAP_UART_RS485 = 0x00002000
CAP_REFMEM = 0x00004000
CAP_VDC = 0x00008000
CAP_BASELINE = CAP_BOARD | CAP_REFMEM | CAP_VDC

IO_SMA_IN = 0x00000001
IO_SMA_OUT = 0x00000002
IO_RJ45_SYNC = 0x00000004
IO_LINK_CONTROL = 0x00000008
IO_BISS_C = 0x00000010
IO_UART_RS485 = 0x00000020

IP_PULSE_CAPTURE = 0x00000001
IP_PULSE_FIRE = 0x00000002
IP_LINK_SEQUENCE = 0x00000004
IP_BISS_C_CODEC = 0x00000008
IP_RJ45_SYNC_DELTA = 0x00000010
IP_VDC_DPLL = 0x00000020

PERSONA_TRIGGER_MASTER = 0x00000001
PERSONA_DISTRIBUTED_TRIGGER = 0x00000002
PERSONA_LINK_CONTROL = 0x00000004
PERSONA_GATEWAY = 0x00000008
PERSONA_MODEL_INSTRUMENTS = 0x00000010
PERSONA_SPARE = 0x00000020

CLAIM_STRICT_UUID = 0
CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT = 1
CLAIM_SPARE_DYNAMIC = 2
FAIL_STOP = 0
FAIL_REPORT_ONLY = 3


@dataclass(frozen=True)
class TableEntry:
    table_id: int
    offset: int
    size: int
    crc32: int


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def _pack_u32_table(version: int,
                    count: int,
                    rows: list[tuple[int, ...]],
                    row_capacity: int) -> bytes:
    payload = bytearray(struct.pack("<II", version, count))
    empty = (0,) * 9
    for row in rows[:row_capacity]:
        payload.extend(struct.pack("<IIIIIIIII", *row))
    for _ in range(len(rows), row_capacity):
        payload.extend(struct.pack("<IIIIIIIII", *empty))
    return bytes(payload)


def build_board_capability_payload() -> bytes:
    rows = [
        (0, 0xB0000000, CAP_BASELINE | CAP_PIO | CAP_DMA | CAP_RJ45 |
         CAP_CORE1_RT | CAP_SMA_IN | CAP_SMA_OUT,
         IO_SMA_IN | IO_SMA_OUT | IO_RJ45_SYNC,
         IP_PULSE_CAPTURE | IP_PULSE_FIRE | IP_RJ45_SYNC_DELTA | IP_VDC_DPLL,
         PERSONA_TRIGGER_MASTER, 0, 0, 1),
        (1, 0xB0000001, CAP_BASELINE | CAP_PIO | CAP_DMA | CAP_RJ45 |
         CAP_CORE1_RT | CAP_SMA_IN | CAP_SMA_OUT,
         IO_SMA_IN | IO_SMA_OUT | IO_RJ45_SYNC,
         IP_PULSE_CAPTURE | IP_PULSE_FIRE | IP_RJ45_SYNC_DELTA,
         PERSONA_DISTRIBUTED_TRIGGER, 0, 1, 1),
        (2, 0xB0000002, CAP_BASELINE | CAP_PIO | CAP_DMA | CAP_RJ45 |
         CAP_CORE1_RT | CAP_LINK_CONTROL,
         IO_LINK_CONTROL | IO_RJ45_SYNC,
         IP_PULSE_CAPTURE | IP_LINK_SEQUENCE | IP_RJ45_SYNC_DELTA,
         PERSONA_LINK_CONTROL, 0, 2, 1),
        (3, 0xB0000003, CAP_BASELINE | CAP_FLASH | CAP_SD | CAP_USB |
         CAP_RJ45 | CAP_UART_RS485,
         IO_RJ45_SYNC | IO_UART_RS485,
         IP_RJ45_SYNC_DELTA, PERSONA_GATEWAY, 0, 3, 1),
        (4, 0xB0000004, CAP_BASELINE | CAP_USB | CAP_PIO | CAP_DMA |
         CAP_CORE1_RT | CAP_BISS_C,
         IO_BISS_C, IP_BISS_C_CODEC, PERSONA_MODEL_INSTRUMENTS, 0, 4, 0),
        (5, 0xB0000005, CAP_BASELINE, 0, 0, PERSONA_SPARE, 0, 5, 0),
        (6, 0xB0000006, CAP_BASELINE, 0, 0, PERSONA_SPARE, 0, 6, 0),
        (7, 0xB0000007, CAP_BASELINE, 0, 0, PERSONA_SPARE, 0, 7, 0),
    ]
    return _pack_u32_table(FORMAT_VERSION, NODE_COUNT, rows, BOARD_CAPABILITY_COUNT)


def build_generic_node_payload() -> bytes:
    rows = [
        (0, 0xB0000000, CAP_BASELINE | CAP_PIO | CAP_DMA | CAP_RJ45 |
         CAP_CORE1_RT | CAP_SMA_IN | CAP_SMA_OUT,
         CLAIM_STRICT_UUID, 100, PERSONA_TRIGGER_MASTER, 0, 1, FAIL_STOP),
        (1, 0xB0000001, CAP_BASELINE | CAP_PIO | CAP_DMA | CAP_RJ45 |
         CAP_CORE1_RT | CAP_SMA_IN | CAP_SMA_OUT,
         CLAIM_STRICT_UUID, 90, PERSONA_DISTRIBUTED_TRIGGER, 0, 1, FAIL_STOP),
        (2, 0xB0000002, CAP_BASELINE | CAP_PIO | CAP_DMA | CAP_RJ45 |
         CAP_CORE1_RT | CAP_LINK_CONTROL,
         CLAIM_STRICT_UUID, 80, PERSONA_LINK_CONTROL, 0, 1, FAIL_STOP),
        (3, 0xB0000003, CAP_BASELINE | CAP_FLASH | CAP_SD | CAP_USB |
         CAP_RJ45 | CAP_UART_RS485,
         CLAIM_STRICT_UUID, 70, PERSONA_GATEWAY, 0, 1, FAIL_STOP),
        (4, 0xB0000004, CAP_BASELINE | CAP_USB | CAP_PIO | CAP_DMA |
         CAP_CORE1_RT | CAP_BISS_C,
         CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT, 40,
         PERSONA_MODEL_INSTRUMENTS, 0, 0, FAIL_REPORT_ONLY),
        (5, 0xB0000005, CAP_BASELINE, CLAIM_SPARE_DYNAMIC, 10,
         PERSONA_SPARE, 0, 0, FAIL_REPORT_ONLY),
        (6, 0xB0000006, CAP_BASELINE, CLAIM_SPARE_DYNAMIC, 9,
         PERSONA_SPARE, 0, 0, FAIL_REPORT_ONLY),
        (7, 0xB0000007, CAP_BASELINE, CLAIM_SPARE_DYNAMIC, 8,
         PERSONA_SPARE, 0, 0, FAIL_REPORT_ONLY),
    ]
    return _pack_u32_table(FORMAT_VERSION, NODE_COUNT, rows, NODE_COUNT)


def build_table_payload(table_id: int, name: str) -> bytes:
    if table_id == 1:
        return build_board_capability_payload()
    if table_id == 2:
        return build_generic_node_payload()
    text = f"{name}:placeholder:v{FORMAT_VERSION}:table={table_id}\n".encode("ascii")
    return text.ljust(64, b"\0")


def build_package() -> tuple[bytes, list[TableEntry]]:
    payload = bytearray()
    entries: list[TableEntry] = []
    table_dir_size = TABLE_COUNT * 16
    cursor = HEADER_SIZE + table_dir_size

    for table_id, name in enumerate(TABLE_NAMES):
        data = build_table_payload(table_id, name)
        entries.append(TableEntry(table_id=table_id,
                                  offset=cursor,
                                  size=len(data),
                                  crc32=crc32(data)))
        payload.extend(data)
        cursor += len(data)

    table_dir = bytearray()
    for entry in entries:
        table_dir.extend(struct.pack("<IIII",
                                     entry.table_id,
                                     entry.offset,
                                     entry.size,
                                     entry.crc32))

    payload_crc = crc32(payload)
    total_size = HEADER_SIZE + len(table_dir) + len(payload)
    header = bytearray(HEADER_SIZE)
    struct.pack_into("<4sIIIIII",
                     header,
                     0,
                     MAGIC,
                     FORMAT_VERSION,
                     HEADER_SIZE,
                     total_size,
                     TABLE_COUNT,
                     table_dir_size,
                     payload_crc)
    package = bytearray(header + table_dir + payload)
    struct.pack_into("<I", package, 28, crc32(package))
    return bytes(package), entries
