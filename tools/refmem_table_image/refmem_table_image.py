"""Build canonical RefMem table image payloads and RMTP packages."""

from __future__ import annotations

import binascii
import struct
from dataclasses import dataclass


MAGIC = b"RMTP"
FORMAT_VERSION = 1
HEADER_SIZE = 64
TABLE_COUNT = 10
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
    "TdmaFoundationProfile",
)

NODE_COUNT = 8
BOARD_CAPABILITY_COUNT = NODE_COUNT
NODE_LOAD_COUNT = 12
FB_INSTANCE_COUNT = 12
EVENT_LINK_COUNT = 11
DATA_LINK_COUNT = 14
DEPLOYMENT_CHECK_COUNT = 12
QUALITY_COUNT = 8
APPLICATION_ID = 1
APPLICATION_VERSION = 1
PROFILE_ID = 1
LAYOUT_VERSION = 1
TARGET_NODE_MASK = 0xFF

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
CAP_TDMA = 0x00010000
CAP_BASELINE = CAP_BOARD | CAP_REFMEM | CAP_VDC | CAP_TDMA

IO_SMA_IN = 0x00000001
IO_SMA_OUT = 0x00000002
IO_RJ45_SYNC = 0x00000004
IO_LINK_CONTROL = 0x00000008
IO_BISS_C = 0x00000010
IO_UART_RS485 = 0x00000020
IO_PIO_SPI_SYNC = 0x00000040

IP_PULSE_CAPTURE = 0x00000001
IP_PULSE_FIRE = 0x00000002
IP_LINK_SEQUENCE = 0x00000004
IP_BISS_C_CODEC = 0x00000008
IP_RJ45_SYNC_DELTA = 0x00000010
IP_VDC_DPLL = 0x00000020
IP_PIO_SPI_SYNC_DELTA = 0x00000040
IP_TDMA_SCHEDULER = 0x00000080

PERSONA_TRIGGER_MASTER = 0x00000001
PERSONA_DISTRIBUTED_TRIGGER = 0x00000002
PERSONA_LINK_CONTROL = 0x00000004
PERSONA_GATEWAY = 0x00000008
PERSONA_MODEL_INSTRUMENTS = 0x00000010
PERSONA_SPARE = 0x00000020

ROLE_BOARD = 0x00000001
ROLE_PULSE_DISTRIBUTOR = 0x00000002
ROLE_LINK_SWITCHER = 0x00000004
ROLE_INSTRUMENT_CONTROLLER = 0x00000008
ROLE_GATEWAY = 0x00000010
ROLE_MODEL_VNA = 0x00000020
ROLE_MODEL_TURNTABLE = 0x00000040
ROLE_TEST_AGENT = 0x00000080

CLAIM_STRICT_UUID = 0
CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT = 1
CLAIM_SPARE_DYNAMIC = 2
FAIL_STOP = 0
FAIL_REPORT_ONLY = 3

DOMAIN_SYSTEM = 0
DOMAIN_TRIG = 1
DOMAIN_CAL = 2
DOMAIN_SYNC = 3
DOMAIN_MEAS = 4
DOMAIN_REFMEM = 5
DOMAIN_GATEWAY = 7
DOMAIN_TDMA = 8

FB_SYSTEM_AO = 0
FB_REFMEM_SYNC = 1
FB_LOOP_ENGINE = 2
FB_TRIGGER_AO = 3
FB_CALIBRATION_AO = 4
FB_VDC_SYNC = 5
FB_DPLL = 6
FB_GATEWAY_AO = 7
FB_MODEL_VNA = 8
FB_MODEL_TURNTABLE = 9
FB_PULSE_COUNTER = 10
FB_INSTRUMENT_CONTROLLER = 11
FB_LINK_SWITCHER = 12
FB_TDMA_SCHEDULER = 13

EVENT_START = 0
EVENT_STOP = 1
EVENT_FIRE_LOAD = 3
EVENT_DONE = 4
EVENT_FAULT = 5
EVENT_ACK = 6
EVENT_CONFIG_STAGE = 8
EVENT_CONFIG_ACTIVATE = 9

TRANSPORT_CORE_IPC = 1
TRANSPORT_COMMAND_SLOT = 2
TRANSPORT_RJ45_SYNC_RING = 3

ACK_NONE = 0
ACK_ALL_REQUIRED = 2
ACK_BITMAP = 3

DATA_U32 = 0
DATA_NS = 2
DATA_TICK = 3
DATA_ENUM = 4
DATA_BITMASK = 5
DATA_CRC = 6

UNIT_NONE = 0
UNIT_NS = 1
UNIT_TICK = 3
UNIT_COUNT = 5

LIFE_ACTIVE = 1
LIFE_RUN = 2
LIFE_TRANSIENT = 3

SNAPSHOT_DIRECT_ATOMIC = 0
SNAPSHOT_SEQLOCK = 1

PERMISSION_READ_ONLY = 0
PERMISSION_COMMAND_WRITE = 1
PERMISSION_CONFIG_STAGE_WRITE = 2

GATE_LAYOUT = 0
GATE_NODE = 1
GATE_INSTANCE = 2
GATE_RESOURCE = 3
GATE_IO = 4
GATE_WRITER = 5
GATE_EVENT = 6
GATE_DATA = 7
GATE_CONFIG = 8
GATE_CAL_SYNC = 9
GATE_QUALITY = 10
GATE_TDMA = 11
GATE_PASS = 0
GATE_REJECT_RUN = 1
GATE_LATCH_FAULT = 3

QUALITY_NODE = 0
QUALITY_RJ45_LINK = 1
QUALITY_EVENT_LINK = 3
QUALITY_DATA_LINK = 4

REGION_HEADER = 0
REGION_SYSTEM = 1
REGION_ROLE = 2
REGION_VDC = 3
REGION_LOOP = 4
REGION_DPLL = 5
REGION_NODE = 6
REGION_TRIGGER = 7
REGION_IO = 8
REGION_CAL = 9
REGION_STATS = 10
REGION_ACK_CMD = 11
REGION_FAULT = 12
REGION_GATEWAY = 13
REGION_SERVICE = 14
REGION_TLV = 15

TDMA_PROFILE_TABLE_VERSION = 1
TDMA_PROFILE_TABLE_COUNT = 1
TDMA_FOUNDATION_PROFILE_WIRE_WORDS = 71
TDMA_ADAPTER_PIO_SPI = 1
TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN = 0x00000001
TDMA_RESOURCE_UNUSED = 0xFFFFFFFF
TDMA_PAYLOAD_MASK = 0x000003FE


@dataclass(frozen=True)
class TableEntry:
    table_id: int
    offset: int
    size: int
    crc32: int


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def _fnv1a_u32(values: tuple[int, ...] | list[int]) -> int:
    value = 2166136261
    for item in values:
        for shift in range(0, 32, 8):
            value ^= (item >> shift) & 0xFF
            value = (value * 16777619) & 0xFFFFFFFF
    return value


def _pack_u32_table(version: int,
                    count: int,
                    rows: list[tuple[int, ...]],
                    row_capacity: int) -> bytes:
    payload = bytearray(struct.pack("<II", version, count))
    row_width = len(rows[0]) if rows else 0
    empty = (0,) * row_width
    for row in rows[:row_capacity]:
        if len(row) != row_width:
            raise ValueError("inconsistent RefMem table row width")
        payload.extend(struct.pack("<" + "I" * row_width, *row))
    for _ in range(len(rows), row_capacity):
        payload.extend(struct.pack("<" + "I" * row_width, *empty))
    return bytes(payload)


def build_application_map_payload() -> bytes:
    return struct.pack("<IIIIII",
                       FORMAT_VERSION,
                       APPLICATION_ID,
                       APPLICATION_VERSION,
                       PROFILE_ID,
                       LAYOUT_VERSION,
                       TARGET_NODE_MASK)


def build_board_capability_payload() -> bytes:
    rows = [
        (0, 0xB0000000, CAP_BASELINE | CAP_PIO | CAP_DMA | CAP_RJ45 |
         CAP_CORE1_RT | CAP_SMA_IN | CAP_SMA_OUT,
         IO_SMA_IN | IO_SMA_OUT | IO_RJ45_SYNC | IO_PIO_SPI_SYNC,
         IP_PULSE_CAPTURE | IP_PULSE_FIRE | IP_RJ45_SYNC_DELTA |
         IP_VDC_DPLL | IP_PIO_SPI_SYNC_DELTA | IP_TDMA_SCHEDULER,
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


def build_node_load_payload() -> bytes:
    rows = [
        (0, APPLICATION_ID, PROFILE_ID, 0, 0,
         ROLE_BOARD, PERSONA_TRIGGER_MASTER, 1, 1, FAIL_STOP, 0),
        (1, APPLICATION_ID, PROFILE_ID, 0, 1,
         ROLE_BOARD | ROLE_PULSE_DISTRIBUTOR, PERSONA_TRIGGER_MASTER, 1, 1, FAIL_STOP, 1),
        (2, APPLICATION_ID, PROFILE_ID, 0, 2,
         ROLE_PULSE_DISTRIBUTOR, PERSONA_TRIGGER_MASTER, 0, 0, FAIL_REPORT_ONLY, 2),
        (3, APPLICATION_ID, PROFILE_ID, 0, 3,
         ROLE_PULSE_DISTRIBUTOR, PERSONA_TRIGGER_MASTER, 0, 0, FAIL_REPORT_ONLY, 3),
        (4, APPLICATION_ID, PROFILE_ID, 1, 4,
         ROLE_BOARD | ROLE_PULSE_DISTRIBUTOR, PERSONA_DISTRIBUTED_TRIGGER, 0, 0,
         FAIL_REPORT_ONLY, 0),
        (5, APPLICATION_ID, PROFILE_ID, 2, 5,
         ROLE_BOARD | ROLE_LINK_SWITCHER, PERSONA_LINK_CONTROL, 0, 0, FAIL_REPORT_ONLY, 0),
        (6, APPLICATION_ID, PROFILE_ID, 3, 6,
         ROLE_BOARD | ROLE_GATEWAY, PERSONA_GATEWAY, 1, 1, FAIL_STOP, 0),
        (7, APPLICATION_ID, PROFILE_ID, 3, 7,
         ROLE_INSTRUMENT_CONTROLLER | ROLE_GATEWAY, PERSONA_GATEWAY, 0, 0,
         FAIL_REPORT_ONLY, 1),
        (8, APPLICATION_ID, PROFILE_ID, 3, 8,
         ROLE_GATEWAY, PERSONA_GATEWAY, 1, 1, FAIL_STOP, 2),
        (9, APPLICATION_ID, PROFILE_ID, 4, 9,
         ROLE_MODEL_VNA | ROLE_TEST_AGENT, PERSONA_MODEL_INSTRUMENTS, 0, 0,
         FAIL_REPORT_ONLY, 0),
        (10, APPLICATION_ID, PROFILE_ID, 4, 10,
         ROLE_MODEL_TURNTABLE | ROLE_TEST_AGENT, PERSONA_MODEL_INSTRUMENTS, 0, 0,
         FAIL_REPORT_ONLY, 1),
        (11, APPLICATION_ID, PROFILE_ID, 0, 11,
         ROLE_BOARD, PERSONA_TRIGGER_MASTER, 1, 1, FAIL_STOP, 4),
    ]
    payload = bytearray(struct.pack("<II", FORMAT_VERSION, NODE_LOAD_COUNT))
    for row in rows:
        payload.extend(struct.pack("<IIIIIIIIIII", *row))
    return bytes(payload)


def _hash_text(text: str) -> int:
    return crc32(text.encode("ascii"))


def build_fb_instance_payload() -> bytes:
    rows = [
        (0, 0, DOMAIN_REFMEM, FB_REFMEM_SYNC, FB_REFMEM_SYNC, _hash_text("B0.RefMemSyncFB"),
         1, 1, 0x00000040, IO_RJ45_SYNC, IP_RJ45_SYNC_DELTA, 1000,
         REGION_GATEWAY, REGION_STATS, 2, 1, 3, 2, 1, 1),
        (1, 0, DOMAIN_TRIG, FB_LOOP_ENGINE, FB_LOOP_ENGINE, _hash_text("B0.LoopEngineAO"),
         1, 1, 0x00000080, IO_SMA_IN | IO_SMA_OUT, IP_VDC_DPLL, 500,
         REGION_LOOP, REGION_STATS, 3, 2, 5, 3, 2, 2),
        (2, 0, DOMAIN_TRIG, FB_PULSE_COUNTER, FB_PULSE_COUNTER,
         _hash_text("Template.PulseCounterAO"), 1, 0, 0x00000098,
         IO_SMA_IN | IO_SMA_OUT, IP_PULSE_CAPTURE | IP_PULSE_FIRE, 200,
         REGION_TRIGGER, REGION_STATS, 5, 1, 8, 1, 0, 2),
        (3, 0, DOMAIN_TRIG, FB_TRIGGER_AO, FB_TRIGGER_AO,
         _hash_text("Template.TriggerMasterAO"), 1, 0, 0x00000098,
         IO_SMA_IN | IO_SMA_OUT, IP_PULSE_CAPTURE | IP_PULSE_FIRE, 200,
         REGION_TRIGGER, REGION_STATS, 5, 1, 8, 1, 3, 2),
        (4, 1, DOMAIN_TRIG, FB_TRIGGER_AO, FB_TRIGGER_AO,
         _hash_text("Template.TriggerAO"), 1, 0, 0x00000098,
         IO_SMA_IN | IO_SMA_OUT | IO_RJ45_SYNC, IP_PULSE_CAPTURE | IP_PULSE_FIRE, 200,
         REGION_TRIGGER, REGION_STATS, 6, 1, 9, 1, 3, 2),
        (5, 2, DOMAIN_TRIG, FB_LINK_SWITCHER, FB_LINK_SWITCHER,
         _hash_text("Template.LinkSwitcherAO"), 1, 0, 0x00000098,
         IO_LINK_CONTROL | IO_RJ45_SYNC, IP_PULSE_CAPTURE | IP_LINK_SEQUENCE, 300,
         REGION_IO, REGION_STATS, 8, 3, 10, 3, 4, 2),
        (6, 3, DOMAIN_SYSTEM, FB_SYSTEM_AO, FB_SYSTEM_AO,
         _hash_text("B3.SystemGatewayAO"), 1, 1, 0x00000007, 0, 0, 1000,
         REGION_SYSTEM, REGION_FAULT, 0, 0, 11, 1, 0, 2),
        (7, 3, DOMAIN_GATEWAY, FB_GATEWAY_AO, FB_INSTRUMENT_CONTROLLER,
         _hash_text("Template.InstrumentControllerAO"), 1, 0, 0x00000040,
         IO_RJ45_SYNC | IO_UART_RS485, IP_RJ45_SYNC_DELTA, 1000,
         REGION_GATEWAY, REGION_STATS, 0, 0, 13, 1, 5, 1),
        (8, 3, DOMAIN_CAL, FB_CALIBRATION_AO, FB_CALIBRATION_AO,
         _hash_text("B3.CalibrationAO"), 1, 1, 0x00000040,
         IO_RJ45_SYNC, IP_RJ45_SYNC_DELTA, 1000,
         REGION_CAL, REGION_STATS, 0, 0, 0, 0, 6, 1),
        (9, 4, DOMAIN_MEAS, FB_MODEL_VNA, FB_MODEL_VNA,
         _hash_text("Template.ModelVnaAO"), 1, 0, 0x00000004, 0, 0, 1000,
         REGION_GATEWAY, REGION_STATS, 0, 0, 0, 0, 7, 1),
        (10, 4, DOMAIN_MEAS, FB_MODEL_TURNTABLE, FB_MODEL_TURNTABLE,
         _hash_text("Template.ModelTurntableAO"), 1, 0, 0x00000098,
         0x00000080, IP_PULSE_FIRE, 500, REGION_IO, REGION_STATS, 0, 0, 0, 0, 8, 1),
        (11, 0, DOMAIN_TDMA, FB_TDMA_SCHEDULER, FB_TDMA_SCHEDULER,
         _hash_text("Foundation.TdmaSchedulerAO"), 1, 1, 0x00000198,
         IO_PIO_SPI_SYNC, IP_PIO_SPI_SYNC_DELTA | IP_TDMA_SCHEDULER, 100,
         REGION_SERVICE, REGION_STATS, 0, 0, 0, 0, 9, 1),
    ]
    return _pack_u32_table(FORMAT_VERSION, FB_INSTANCE_COUNT, rows, FB_INSTANCE_COUNT)


def build_event_link_payload() -> bytes:
    rows = [
        (0, 6, EVENT_CONFIG_STAGE, 0x0F, 0, EVENT_CONFIG_STAGE,
         TRANSPORT_COMMAND_SLOT, 50000, ACK_ALL_REQUIRED, 0, 1, REGION_ACK_CMD),
        (1, 6, EVENT_CONFIG_ACTIVATE, 0x0F, 1, EVENT_CONFIG_ACTIVATE,
         TRANSPORT_COMMAND_SLOT, 50000, ACK_ALL_REQUIRED, 0, 1, REGION_ACK_CMD),
        (2, 0, EVENT_ACK, 0x08, 6, EVENT_ACK,
         TRANSPORT_RJ45_SYNC_RING, 20000, ACK_BITMAP, 0, 1, REGION_ACK_CMD),
        (3, 1, EVENT_START, 0x0F, 3, EVENT_START,
         TRANSPORT_COMMAND_SLOT, 20000, ACK_ALL_REQUIRED, 0, 2, REGION_ACK_CMD),
        (4, 1, EVENT_STOP, 0x0F, 3, EVENT_STOP,
         TRANSPORT_COMMAND_SLOT, 10000, ACK_ALL_REQUIRED, 0, 2, REGION_ACK_CMD),
        (5, 1, EVENT_FIRE_LOAD, 0x01, 2, EVENT_FIRE_LOAD,
         TRANSPORT_CORE_IPC, 1000, ACK_NONE, 0, 3, REGION_TRIGGER),
        (6, 2, EVENT_DONE, 0x01, 1, EVENT_DONE,
         TRANSPORT_RJ45_SYNC_RING, 10000, ACK_BITMAP, 0, 2, REGION_STATS),
        (7, 2, EVENT_FAULT, 0x08, 6, EVENT_FAULT,
         TRANSPORT_RJ45_SYNC_RING, 10000, ACK_BITMAP, 0, 3, REGION_FAULT),
        (8, 1, EVENT_FIRE_LOAD, 0x04, 5, EVENT_FIRE_LOAD,
         TRANSPORT_RJ45_SYNC_RING, 2000, ACK_BITMAP, 0, 3, REGION_IO),
        (9, 5, EVENT_DONE, 0x01, 1, EVENT_DONE,
         TRANSPORT_RJ45_SYNC_RING, 10000, ACK_BITMAP, 0, 2, REGION_STATS),
        (10, 5, EVENT_FAULT, 0x08, 6, EVENT_FAULT,
         TRANSPORT_RJ45_SYNC_RING, 10000, ACK_BITMAP, 0, 3, REGION_FAULT),
    ]
    payload = bytearray(struct.pack("<II", FORMAT_VERSION, EVENT_LINK_COUNT))
    for row in rows:
        payload.extend(struct.pack("<IIIIIIIIIIII", *row))
    return bytes(payload)


def build_data_link_payload() -> bytes:
    rows = [
        (0, _hash_text("SystemRegion.mode"), 6, 0xFF, DATA_ENUM, UNIT_NONE, 1, 0, 8,
         LIFE_ACTIVE, SNAPSHOT_DIRECT_ATOMIC, 10000, 50000, REGION_SYSTEM, PERMISSION_COMMAND_WRITE),
        (1, _hash_text("RoleRegion.node_role"), 6, 0xFF, DATA_BITMASK, UNIT_NONE, 1, 0, 255,
         LIFE_ACTIVE, SNAPSHOT_DIRECT_ATOMIC, 100000, 500000, REGION_ROLE,
         PERMISSION_CONFIG_STAGE_WRITE),
        (2, _hash_text("LoopRegion.active_sequence_crc"), 1, 0xFF, DATA_CRC, UNIT_NONE, 1, 0,
         2147483647, LIFE_ACTIVE, SNAPSHOT_SEQLOCK, 10000, 50000, REGION_LOOP,
         PERMISSION_CONFIG_STAGE_WRITE),
        (3, _hash_text("LoopRegion.run_state"), 1, 0xFF, DATA_ENUM, UNIT_NONE, 1, 0, 16,
         LIFE_RUN, SNAPSHOT_DIRECT_ATOMIC, 1000, 10000, REGION_LOOP, PERMISSION_COMMAND_WRITE),
        (4, _hash_text("VdcRegion.dc_time64_ns"), 0, 0xFF, DATA_NS, UNIT_NS, 1, 0,
         2147483647, LIFE_RUN, SNAPSHOT_SEQLOCK, 1000, 10000, REGION_VDC, PERMISSION_READ_ONLY),
        (5, _hash_text("DpllRegion.lock_state"), 0, 0xFF, DATA_ENUM, UNIT_NONE, 1, 0, 8,
         LIFE_RUN, SNAPSHOT_DIRECT_ATOMIC, 1000, 10000, REGION_DPLL, PERMISSION_READ_ONLY),
        (6, _hash_text("CalibrationRegion.delay_crc"), 8, 0xFF, DATA_CRC, UNIT_NONE, 1, 0,
         2147483647, LIFE_ACTIVE, SNAPSHOT_SEQLOCK, 100000, 500000, REGION_CAL,
         PERMISSION_CONFIG_STAGE_WRITE),
        (7, _hash_text("AckCommandRegion.command_seq"), 6, 0xFF, DATA_U32, UNIT_COUNT, 1, 0,
         2147483647, LIFE_TRANSIENT, SNAPSHOT_DIRECT_ATOMIC, 1000, 50000, REGION_ACK_CMD,
         PERMISSION_COMMAND_WRITE),
        (8, _hash_text("TriggerRegion.fire_seq"), 2, 0x0F, DATA_U32, UNIT_COUNT, 1, 0,
         2147483647, LIFE_RUN, SNAPSHOT_SEQLOCK, 1000, 10000, REGION_TRIGGER, PERMISSION_READ_ONLY),
        (9, _hash_text("TriggerRegion.node_heartbeat"), 4, 0x0F, DATA_U32, UNIT_COUNT, 1, 0,
         2147483647, LIFE_RUN, SNAPSHOT_DIRECT_ATOMIC, 1000, 10000, REGION_NODE,
         PERMISSION_READ_ONLY),
        (10, _hash_text("IoRegion.link_state"), 5, 0xFF, DATA_BITMASK, UNIT_NONE, 1, 0, 65535,
         LIFE_ACTIVE, SNAPSHOT_DIRECT_ATOMIC, 10000, 50000, REGION_IO, PERMISSION_COMMAND_WRITE),
        (11, _hash_text("IoRegion.link_pulse_timestamp"), 5, 0xFF, DATA_TICK, UNIT_TICK, 1, 0,
         2147483647, LIFE_RUN, SNAPSHOT_SEQLOCK, 1000, 10000, REGION_IO, PERMISSION_READ_ONLY),
        (12, _hash_text("IoRegion.link_sequence_state"), 5, 0xFF, DATA_ENUM, UNIT_NONE, 1, 0, 32,
         LIFE_RUN, SNAPSHOT_DIRECT_ATOMIC, 1000, 10000, REGION_IO, PERMISSION_READ_ONLY),
        (13, _hash_text("GatewayRegion.instrument_state"), 7, 0xFF, DATA_ENUM, UNIT_NONE, 1, 0, 32,
         LIFE_ACTIVE, SNAPSHOT_DIRECT_ATOMIC, 10000, 50000, REGION_GATEWAY,
         PERMISSION_READ_ONLY),
    ]
    payload = bytearray(struct.pack("<II", FORMAT_VERSION, DATA_LINK_COUNT))
    for row in rows:
        payload.extend(struct.pack("<IIIIIIIIIIIIIII", *row))
    return bytes(payload)


def build_deployment_gate_payload() -> bytes:
    rows = [
        (GATE_LAYOUT, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_HEADER, 0),
        (GATE_NODE, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_NODE, 0),
        (GATE_INSTANCE, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_TLV, 0),
        (GATE_RESOURCE, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_SERVICE, 0),
        (GATE_IO, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_IO, 0),
        (GATE_WRITER, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_TLV, 0),
        (GATE_EVENT, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_ACK_CMD, 0),
        (GATE_DATA, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_TLV, 0),
        (GATE_CONFIG, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_SYSTEM, 0),
        (GATE_CAL_SYNC, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_CAL, 0),
        (GATE_QUALITY, 1, GATE_LATCH_FAULT, GATE_PASS, 0, 0, 0, REGION_STATS, 0),
        (GATE_TDMA, 1, GATE_REJECT_RUN, GATE_PASS, 0, 0, 0, REGION_SERVICE, 0),
    ]
    return _pack_u32_table(FORMAT_VERSION, DEPLOYMENT_CHECK_COUNT, rows, DEPLOYMENT_CHECK_COUNT)


def build_connection_quality_payload() -> bytes:
    rows = [
        (0, QUALITY_NODE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, QUALITY_NODE, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1),
        (2, QUALITY_NODE, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2),
        (3, QUALITY_NODE, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3),
        (4, QUALITY_RJ45_LINK, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4),
        (5, QUALITY_RJ45_LINK, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5),
        (6, QUALITY_EVENT_LINK, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6),
        (7, QUALITY_DATA_LINK, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7),
    ]
    payload = bytearray(struct.pack("<II", FORMAT_VERSION, QUALITY_COUNT))
    for row in rows:
        payload.extend(struct.pack("<IIIIIIIIIIIIIIII", *row))
    return bytes(payload)


def build_tdma_foundation_profile_payload() -> bytes:
    ring = [
        1,
        TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN,
        NODE_COUNT,
        0,
        0,
        1,
        2,
        NODE_COUNT - 1,
        1,
        0,
    ]
    ring_crc = _fnv1a_u32(ring)
    resource = [
        TDMA_ADAPTER_PIO_SPI,
        0,
        0,
        1,
        0,
        1,
        1,
        IO_PIO_SPI_SYNC,
        IP_PIO_SPI_SYNC_DELTA | IP_TDMA_SCHEDULER,
        292,
        1024,
        TDMA_PAYLOAD_MASK,
        1_000_000,
        1024,
        128,
        32768,
    ]
    traffic = [
        (0, (1 << 1) | (1 << 4) | (1 << 9), 292, 1, 4, 10_000, 0x03, 0),
        (1, (1 << 2) | (1 << 3), 292, 1, 8, 1_000_000, 0x07, 1),
        (2, 1 << 6, 128, 1, 4, 100_000_000, 0x1C, 1),
        (3, (1 << 5) | (1 << 8), 0, 1, 4, 0, 0x1C, 1),
        (4, 1 << 7, 0, 1, 8, 0, 0x18, 2),
    ]
    traffic_words = [item for row in traffic for item in row]
    profile_crc = _fnv1a_u32([1, 1, 11, ring_crc, *resource, *traffic_words])
    profile = [1, 1, 11, *ring, ring_crc, *resource, *traffic_words, profile_crc]
    assert len(profile) == TDMA_FOUNDATION_PROFILE_WIRE_WORDS
    return struct.pack(
        "<" + "I" * (2 + len(profile)),
        TDMA_PROFILE_TABLE_VERSION,
        TDMA_PROFILE_TABLE_COUNT,
        *profile,
    )


def build_table_payload(table_id: int, name: str) -> bytes:
    if table_id == 0:
        return build_application_map_payload()
    if table_id == 1:
        return build_board_capability_payload()
    if table_id == 2:
        return build_generic_node_payload()
    if table_id == 3:
        return build_node_load_payload()
    if table_id == 4:
        return build_fb_instance_payload()
    if table_id == 5:
        return build_event_link_payload()
    if table_id == 6:
        return build_data_link_payload()
    if table_id == 7:
        return build_deployment_gate_payload()
    if table_id == 8:
        return build_connection_quality_payload()
    if table_id == 9:
        return build_tdma_foundation_profile_payload()
    raise ValueError(f"unknown RefMem table {table_id}: {name}")


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
