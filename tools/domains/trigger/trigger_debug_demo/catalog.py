#!/usr/bin/env python3
"""Node-sequence command catalog for the REFMEM trigger debug demo.

One serial port reaches the coordinator.  Two control channels are exposed:

* ``SYSTem:CTRL:EXECute <name>,<target_mask>,<required_mask>,<params...>``
  -- the REFMEM unified application control plane.  It addresses every board
  through the target/required masks and returns one aggregate ACK fact.
* the node-sequence application SCPI (``TRIGger:*`` / ``CONFigure:*`` /
  ``READ:*``) -- direct, board-local application layer.

The parameter word layouts mirror ``application/src/app_refmem_domain_config.c``
and ``components/distributed_refmem/src/refmem_command_catalog_table.c``.
Do not reorder them: the firmware parses words positionally.
"""

from __future__ import annotations

from dataclasses import dataclass
import csv
import struct
from typing import Mapping


U32_MAX = 0xFFFFFFFF
U64_MAX = 0xFFFFFFFFFFFFFFFF
I32_MIN = -0x80000000
I32_MAX = 0x7FFFFFFF
PLAN_WORDS = 4
PLAN_ID_MAX_CHARS = 16
SEQUENCE_PLAN_WORDS = 16
SEQUENCE_PLAN_MAX_STATES = 11


@dataclass(frozen=True)
class Module:
    key: str
    title: str
    subtitle: str


@dataclass(frozen=True)
class Field:
    name: str
    label: str
    kind: str = "u32"          # u32 | i32 | float | plan | states | enum
    default: str = "0"
    hint: str = ""
    choices: tuple[str, ...] = ()
    optional: bool = False


@dataclass(frozen=True)
class Function:
    key: str
    module: str
    title: str
    description: str
    app_scpi: str | None
    ctl_name: str | None
    ctl_id: int | None
    fields: tuple[Field, ...] = ()
    warning: str = ""
    app_deprecated: bool = False


@dataclass(frozen=True)
class Query:
    key: str
    module: str
    scpi: str
    title: str
    description: str = ""
    args: tuple[Field, ...] = ()
    response_fields: tuple[str, ...] = ()


MODULES: tuple[Module, ...] = (
    Module("overview", "总览", "单端口连接、控制权与板卡在线摘要"),
    Module("run", "运行控制", "TRIGger MODE/START/STOP/PAUSE/CONTINUE/ABORT"),
    Module("trigger_config", "触发配置", "CONFigure:TRIGger 通道/极化/频点/波位"),
    Module("angle", "角度扫描", "SWEEP/PULSE/BREAKPOINT/SPEED 角度域"),
    Module("switch", "开关", "SP8T 通道与 SP2T 极化开关"),
    Module("sequence", "序列计划", "plan 表、active plan 与重复次数"),
    Module("readback", "状态回读", "READ:* 应用层查询"),
    Module("ack", "板卡与 ACK", "目标掩码、逐板 ACK/NACK 与 REFMEM 证据"),
    Module("control", "统一控制面", "CTRL:EXECute、目录、OWNer 与原始帧"),
    Module("diagnostics", "诊断", "错误、故障、链路与命令帧统计"),
    Module("raw", "原始命令", "直接发送任意 SCPI 命令"),
)

MODULE_BY_KEY = {module.key: module for module in MODULES}


def _f(name: str, label: str, kind: str = "u32", default: str = "0",
       hint: str = "", choices: tuple[str, ...] = (),
       optional: bool = False) -> Field:
    return Field(name=name, label=label, kind=kind, default=default,
                 hint=hint, choices=choices, optional=optional)


_PLAN = _f("plan_id", "Plan ID", "plan", "", "最多 16 个 ASCII 字符；空 = active plan", optional=True)
_TARGET = _f("target_mask", "目标掩码", "u32", "15", "bit0..3 = 槽位 0..3；15 = 全部四板")
_REQUIRED = _f("required_mask", "必需掩码", "u32", "15", "必须 ACK 的槽位；不得超出目标掩码")


FUNCTIONS: tuple[Function, ...] = (
    # ------------------------------------------------------------------- run
    Function("run.mode", "run", "触发模式", "设置 TRIGger:MODE（0=IDLE 1=TRIG 2=CAL 3=SYNC 4=SIM）。",
             "TRIGger:MODE", "TRIGGER_MODE", 35,
             fields=(_f("mode", "模式", "enum", "1", "0..4", ("0", "1", "2", "3", "4")),)),
    Function("run.start", "run", "启动", "启动当前 active plan；plan_id 为空时使用 active plan。",
             "TRIGger:STARt", "TRIGGER_START", 36,
             fields=(_PLAN,),
             warning="START 的 plan_id 在应用层为字符串，在统一控制面为 4×u32 打包。"),
    Function("run.stop", "run", "停止", "任意状态安全停止。",
             "TRIGger:STOP", "TRIGGER_STOP", 37),
    Function("run.pause", "run", "暂停", "在下一个安全边界暂停。",
             "TRIGger:PAUSe", "TRIGGER_PAUSE", 38),
    Function("run.continue", "run", "继续", "从暂停角度继续；断点一次性越过。",
             "TRIGger:CONTinue", "TRIGGER_CONTINUE", 39),
    Function("run.abort", "run", "中止", "中止本轮运行（复用 STOP 语义）。",
             "TRIGger:ABORt", "TRIGGER_ABORT", 40),

    # -------------------------------------------------------- trigger config
    Function("trigger.configure", "trigger_config", "触发维度", "按通道/极化/频点/波位自动展开状态表。",
             "CONFigure:TRIGger", "TRIGGER_CONFIGURE", 41,
             fields=(_f("chan", "通道数", "u32", "8", "1..8"),
                     _f("pol", "极化数", "u32", "2", "1..8"),
                     _f("freq", "频点数", "u32", "5"),
                     _f("wave", "波位数", "u32", "1")),
             warning="RUN 中配置会被拒绝；先 STOP。"),

    # ------------------------------------------------------------------ angle
    Function("angle.sweep", "angle", "扫描范围", "配置 start/stop/step（deg，IEEE-754 浮点）。",
             "CONFigure:ANGLe:SWEEp", "TRIGGER_ANGLE_SWEEP", 42,
             fields=(_f("start", "起始角 deg", "float", "0"),
                     _f("stop", "终止角 deg", "float", "360"),
                     _f("step", "步长 deg", "float", "1"))),
    Function("angle.pulse", "angle", "角度脉冲", "配置输入边沿、脉宽与超时。",
             "CONFigure:ANGLe:PULSe", "TRIGGER_ANGLE_PULSE", 43,
             fields=(_f("edge", "边沿", "enum", "0", "0=上升 1=下降", ("0", "1")),
                     _f("width_us", "脉宽 us", "u32", "10"),
                     _f("timeout_ms", "超时 ms", "u32", "5000"))),
    Function("angle.breakpoint", "angle", "断点", "设置断点 presence 与角度（deg 浮点）。",
             "CONFigure:ANGLe:BREAkpoint", "TRIGGER_ANGLE_BREAKPOINT", 44,
             fields=(_f("present", "Present", "enum", "1", "0/1", ("0", "1")),
                     _f("angle", "断点角 deg", "float", "180"))),
    Function("angle.clear", "angle", "清除断点", "只清断点/命中，不自动继续。",
             "CONFigure:ANGLe:BREAkpoint:CLEar", "TRIGGER_ANGLE_BREAKPOINT_CLEAR", 46),
    Function("angle.speed", "angle", "角度速度", "设置转台速度（deg/s）；pulse_hz = speed / step。",
             "CONFigure:ANGLe:SPEEd", "TRIGGER_ANGLE_SPEED", 49,
             fields=(_f("speed", "速度 deg/s", "float", "10"),)),

    # ----------------------------------------------------------------- switch
    Function("switch.channel", "switch", "SP8T 通道", "设置 switch1 的掷向 1..8。",
             "CONFigure:SWITch1", "TRIGGER_SWITCH", 45,
             fields=(_f("number", "通道", "u32", "1", "1..8"),
                     _f("value", "值", "enum", "1", "0/1", ("0", "1"))),
             warning="统一控制面固定发送 [switch_number, value] 两个 word；"
                     "应用层 CONFigure:SWITch1 已废弃（USE_CTRL_EXECUTE）。",
             app_deprecated=True),
    Function("switch.polarization", "switch", "SP2T 极化", "设置 switch2 的 0/1。",
             "CONFigure:SWITch2", "TRIGGER_SWITCH", 45,
             fields=(_f("value", "值", "enum", "0", "0/1", ("0", "1")),),
             warning="统一控制面发送 [2, value]；应用层 CONFigure:SWITch2 已废弃"
                     "（USE_CTRL_EXECUTE）。",
             app_deprecated=True),

    # --------------------------------------------------------------- sequence
    Function("sequence.plan", "sequence", "写 plan 表", "plan_id + 1..11 个 state_id；统一控制面固定 16 words。",
             "CONFigure:SEQuence", "TRIGGER_SEQUENCE_PLAN", 48,
             fields=(_f("plan_id", "Plan ID", "plan", "PLAN_A", "最多 16 个 ASCII 字符"),
                     _f("states", "State IDs", "states", "0,1,2,3,4,5",
                        "逗号分隔，1..11 个 u32")),
             warning="应用层与统一控制面都要求 plan_id 非空；count=0 会被拒绝。"),
    Function("sequence.active", "sequence", "激活 plan", "选择 active plan；RUN 中禁止切换。",
             "CONFigure:SEQuence:ACTive", "TRIGGER_SEQUENCE_ACTIVE", 47,
             fields=(_f("plan_id", "Plan ID", "plan", "PLAN_A", "最多 16 个 ASCII 字符"),)),
    Function("sequence.repeat", "sequence", "重复次数", "整张序列表的重复次数（应用层专用，不在 REFMEM 目录）。",
             "CONFigure:SEQuence:REPeat", None, None,
             fields=(_f("repeat", "重复", "u32", "1", "0..1000"),)),
)


QUERIES: tuple[Query, ...] = (
    Query("overview.idn", "overview", "*IDN?", "设备身份", "厂牌/型号/序列号/版本。"),
    Query("overview.fw", "overview", "SYSTem:FW:BUILD?", "固件 Build", ""),
    Query("overview.board", "overview", "SYSTem:BOARD:NO?", "本板编号", "1 基板号；REFMEM 槽位 = 板号-1。"),
    Query("overview.trigger", "overview", "READ:TRIGger:STATe?", "触发状态", "含 T2 同源观测的 23 字段。",
          response_fields=("mode", "state", "run_id", "plan_id", "current_angle", "angle_hit",
                           "angle_count", "current_index", "count", "accepted", "late",
                           "alarm_late_us", "error", "error_text", "local_role", "t2_valid",
                           "t2_configured", "t2_active", "action_armed", "action_completed",
                           "action_failed", "action_groups_executed", "published_mask")),
    Query("overview.owner", "overview", "SYSTem:CTRL:OWNer?", "控制权", "owner_slot/valid/local_slot/is_owner。",
          response_fields=("owner_slot", "owner_valid", "local_slot", "is_owner")),

    Query("read.trigger_param", "readback", "READ:TRIGger:PARameter?", "触发参数",
          "chan/pol/freq/wave/width/4×CRC 展示值。",
          response_fields=("chan_count", "pol_count", "freq_count", "wave_count", "width",
                           "crc0", "crc1", "crc2", "crc3")),
    Query("read.sequence", "readback", "READ:SEQuence?", "当前 plan 表", "id/generation/crc/count + 6 个 state id。",
          response_fields=("plan_id", "generation", "crc", "count", "valid", "state0", "state1",
                           "state2", "state3", "state4", "state5")),
    Query("read.active", "readback", "READ:SEQuence:ACTive?", "Active plan", "active_plan_id/crc/count/valid/check/reject。",
          response_fields=("active_plan_id", "active_crc", "state_count", "valid",
                           "last_check", "reject_reason")),
    Query("read.map", "readback", "READ:SEQuence:MAP?", "Plan 映射", "state 到 switch/pol/freq/wave 的展开映射。",
          response_fields=("state_id", "switch1_ch", "switch2_sel", "freq_index",
                           "wave_index", "reserved")),
    Query("read.check", "readback", "READ:SEQuence:CHECk?", "Plan 预检", "逐项 range/CRC/duplicate/missing 结论。",
          response_fields=("plan_id", "state_count", "sequence_crc", "state_range_ok", "map_crc_ok",
                           "switch_range_ok", "pol_range_ok", "freq_range_ok", "wave_range_ok",
                           "duplicate_state", "missing_state", "reject_reason")),
    Query("read.sweep", "readback", "READ:ANGLe:SWEEp?", "扫描范围", "start/stop/step/count/0。",
          response_fields=("start_deg", "stop_deg", "step_deg", "positions", "reserved")),
    Query("read.pulse", "readback", "READ:ANGLe:PULSe?", "角度脉冲", "edge/width/timeout + 状态/valid/pulse_hz。",
          response_fields=("edge", "width_us", "timeout_ms", "reserved0", "reserved1", "reserved2",
                           "state", "valid", "pulse_hz")),
    Query("read.breakpoint", "readback", "READ:ANGLe:BREAkpoint?", "断点", "angle/present/hit/reserved。",
          response_fields=("breakpoint_deg", "present", "hit", "reserved")),
    Query("read.position", "readback", "READ:ANGLe:POSition?", "角度位置", "source/index/count/current/next/pulse/flags。",
          response_fields=("source", "index", "count", "current_deg", "next_deg", "pulse_count",
                           "hit_count", "running", "paused", "reserved")),
    Query("read.switch1", "readback", "READ:SWITch1?", "SP8T 状态", "number/valid/applied/value/desired/reserved。",
          response_fields=("number", "valid", "applied", "applied_value", "desired_value", "reserved")),
    Query("read.switch2", "readback", "READ:SWITch2?", "SP2T 状态", "同上。",
          response_fields=("number", "valid", "applied", "applied_value", "desired_value", "reserved")),

    Query("ack.ctrl_status", "ack", "SYSTem:CTRL:STATus?", "聚合命令状态",
          "seq/crc/state/command_seq/target/required/ack/nack/busy/timeout/reason。",
          response_fields=("seq", "crc32", "state", "command_seq", "target_mask", "required_mask",
                           "ack_flags", "nack_flags", "busy_flags", "timeout_flags",
                           "last_reason", "last_reason_slot", "last_completed_seq")),
    Query("ack.cmd_reply", "ack", "SYSTem:REFMEM:CMD:REPLy?", "REFMEM 回复聚合",
          "rx/applied/stale/bad_target/duplicate/armed/published/pending + aggregate/masks。",
          response_fields=("rx_count", "applied_count", "stale_count", "bad_target_count",
                           "duplicate_count", "armed_count", "published_count", "publish_remaining",
                           "pending_seq", "aggregate_state", "acked_mask", "nacked_mask",
                           "last_slot", "last_state", "last_reason")),
    Query("ack.cmd_status", "ack", "SYSTem:COMMand:STATus?", "命令聚合状态",
          "posted/taken/busy/acked/nacked/timed_out/last_*。",
          response_fields=("node_count", "posted", "taken", "busy", "acked", "nacked",
                           "timed_out", "last_state", "last_reason", "last_reason_slot",
                           "last_completed_seq")),
    Query("ack.target", "ack", "SYSTem:COMMand:TARget?", "单板命令记录", "按槽位读取。",
          args=(_f("slot", "槽位", "u32", "0", "0..3"),),
          response_fields=("slot", "command_seq", "state", "command_type", "target_mask",
                           "required_mask", "ack_flags", "nack_flags", "last_reason")),
    Query("ack.nodes", "ack", "SYSTem:REFMEM:NODES?", "节点在线表", "offset/count；每节点 15 字段。",
          args=(_f("offset", "起始", "u32", "0"), _f("count", "数量", "u32", "4", "最多 8")),
          response_fields=("node_id", "state", "heartbeat", "slot_version", "last_update_ms",
                           "age_ms", "stale", "stale_count", "fault_code", "flags",
                           "node_type", "control_state", "ack_flags", "nack_flags", "last_reason")),
    Query("ack.node_control", "ack", "SYSTem:REFMEM:NODE:CONTrol?", "节点命令记录", "可选槽位；空 = 本板。",
          args=(_f("node", "槽位", "u32", "0", "0..3", optional=True),),
          response_fields=("node_id", "seq", "crc32", "state", "command_seq", "source_node",
                           "target_mask", "required_mask", "command_type", "command_class",
                           "ack_flags", "nack_flags", "busy_flags", "timeout_flags",
                           "last_reason", "last_reason_slot", "evidence_index",
                           "last_completed_seq", "run_id", "issue_epoch", "payload_crc32")),
    Query("ack.owner", "ack", "SYSTem:CTRL:OWNer?", "控制权", "owner_slot/valid/local_slot/is_owner。",
          response_fields=("owner_slot", "owner_valid", "local_slot", "is_owner")),
    Query("ack.catalog", "ack", "SYSTem:CTRL:CATalog?", "控制面目录", "每批最多 16 条，每条 7 字段。",
          args=(_f("offset", "起始", "u32", "0"), _f("count", "数量", "u32", "16", "最多 16")),
          response_fields=("id", "name", "command_type", "owner", "param_words", "param_kind",
                           "realtime")),

    Query("diag.err", "diagnostics", "SYSTem:ERRor?", "SCPI 错误", "错误码,文本。"),
    Query("diag.fault", "diagnostics", "SYSTem:FAULT:LAST?", "最近故障", ""),
    Query("diag.ring", "diagnostics", "SYSTem:TDMA:RING:STATus?", "TDMA 环状态", ""),
    Query("diag.cmdframe", "diagnostics", "SYSTem:REFMEM:CMDFRame?", "命令帧统计",
          "calls/apply_fail/target_miss/request_fail/ok。",
          response_fields=("calls", "apply_fail", "target_miss", "request_fail", "ok")),
    Query("diag.cmdload", "diagnostics", "SYSTem:REFMEM:CMD:STATus?", "命令装载统计",
          "load/tx/rx/queue 计数器。"),
)


FUNCTION_BY_KEY = {function.key: function for function in FUNCTIONS}
QUERY_BY_KEY = {query.key: query for query in QUERIES}


def functions_for(module: str) -> tuple[Function, ...]:
    return tuple(function for function in FUNCTIONS if function.module == module)


def queries_for(module: str) -> tuple[Query, ...]:
    return tuple(query for query in QUERIES if query.module == module)


# ------------------------------------------------------------------ encoders

def parse_int(text: str, field: Field) -> int:
    value = text.strip()
    if not value:
        raise ValueError(f"{field.label}不能为空")
    if field.kind == "enum":
        if value not in field.choices:
            raise ValueError(f"{field.label}必须是 {'/'.join(field.choices)} 之一")
        return int(value)
    try:
        number = int(value, 10)
    except ValueError as exc:
        raise ValueError(f"{field.label}必须是十进制整数") from exc
    if field.kind == "u32" and not 0 <= number <= U32_MAX:
        raise ValueError(f"{field.label}超出 uint32 范围")
    if field.kind == "i32" and not I32_MIN <= number <= I32_MAX:
        raise ValueError(f"{field.label}超出 int32 范围")
    return number


def parse_float(text: str, field: Field) -> float:
    value = text.strip()
    if not value:
        raise ValueError(f"{field.label}不能为空")
    try:
        number = float(value)
    except ValueError as exc:
        raise ValueError(f"{field.label}必须是数字") from exc
    if number != number or number in (float("inf"), float("-inf")):
        raise ValueError(f"{field.label}必须是有限数值")
    return number


def float_bits(text: str, field: Field) -> int:
    return struct.unpack("<I", struct.pack("<f", parse_float(text, field)))[0]


def float_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & U32_MAX))[0]


def plan_words(text: str, field: Field, *, allow_empty: bool = False) -> list[int]:
    value = text.strip()
    if not value:
        if allow_empty:
            return [0, 0, 0, 0]
        raise ValueError(f"{field.label}不能为空")
    encoded = value.encode("ascii", errors="strict")
    if len(encoded) > PLAN_ID_MAX_CHARS:
        raise ValueError(f"{field.label}最多 {PLAN_ID_MAX_CHARS} 个 ASCII 字符")
    encoded = encoded.ljust(PLAN_ID_MAX_CHARS, b"\0")
    return [int.from_bytes(encoded[index:index + 4], "little")
            for index in range(0, PLAN_ID_MAX_CHARS, 4)]


def plan_from_words(words: list[int]) -> str:
    raw = b"".join(int(word).to_bytes(4, "little") for word in words[:PLAN_WORDS])
    return raw.split(b"\0", 1)[0].decode("ascii", errors="replace")


def parse_states(text: str, field: Field) -> list[int]:
    raw = [item.strip() for item in text.split(",") if item.strip()]
    if not 1 <= len(raw) <= SEQUENCE_PLAN_MAX_STATES:
        raise ValueError(f"{field.label}需要 1..{SEQUENCE_PLAN_MAX_STATES} 个 state id")
    states: list[int] = []
    for item in raw:
        try:
            number = int(item, 10)
        except ValueError as exc:
            raise ValueError(f"{field.label}包含非整数项：{item}") from exc
        if not 0 <= number <= U32_MAX:
            raise ValueError(f"{field.label}项超出 uint32：{item}")
        states.append(number)
    return states


def _required(value: str, field: Field) -> str:
    text = value.strip()
    if not text:
        if field.optional:
            return ""
        raise ValueError(f"{field.label}不能为空")
    return text


def build_app(function: Function, values: Mapping[str, str]) -> str:
    """Render the application-layer SCPI command for a function."""
    if function.app_deprecated:
        raise ValueError(
            f"{function.title}的应用层入口已废弃（固件返回 USE_CTRL_EXECUTE），"
            "请改用统一控制面")
    if function.app_scpi is None:
        raise ValueError(f"{function.title}没有应用层命令")
    parts: list[str] = []
    for field in function.fields:
        value = _required(values.get(field.name, field.default), field)
        if not value:
            continue
        if field.kind in ("u32", "i32", "enum"):
            parse_int(value, field)
        elif field.kind == "float":
            parse_float(value, field)
        elif field.kind == "plan":
            plan_words(value, field, allow_empty=field.optional)
        elif field.kind == "states":
            parse_states(value, field)
        parts.append(value)
    if function.key == "sequence.plan":
        plan = values.get("plan_id", "").strip()
        plan_words(plan, function.fields[0])
        states = parse_states(values.get("states", ""), function.fields[1])
        return f"{function.app_scpi} {plan}," + ",".join(map(str, states))
    if not parts:
        return function.app_scpi
    return function.app_scpi + " " + ",".join(parts)


def ctl_params(function: Function, values: Mapping[str, str]) -> list[int]:
    """Encode the catalog parameter words for CTRL:EXECute."""
    params: list[int] = []
    for field in function.fields:
        value = _required(values.get(field.name, field.default), field)
        if field.kind in ("u32", "i32", "enum"):
            params.append(parse_int(value, field))
        elif field.kind == "float":
            params.append(float_bits(value, field))
        elif field.kind == "plan":
            params.extend(plan_words(value, field, allow_empty=field.optional))
        elif field.kind == "states":
            states = parse_states(value, field)
            params.append(len(states))
            params.extend(states + [0] * (SEQUENCE_PLAN_MAX_STATES - len(states)))
    if function.key == "switch.polarization":
        return [2, parse_int(values.get("value", "0"), function.fields[0])]
    if function.key == "sequence.plan":
        plan = plan_words(values.get("plan_id", ""), function.fields[0])
        states = parse_states(values.get("states", ""), function.fields[1])
        return plan + [len(states)] + states + [0] * (SEQUENCE_PLAN_MAX_STATES - len(states))
    if function.key == "run.start" and not params:
        return [0, 0, 0, 0]
    expected = _expected_words(function)
    if len(params) != expected:
        raise ValueError(f"内部错误：{function.key} 期望 {expected} words，实际 {len(params)}")
    return params


def _expected_words(function: Function) -> int:
    return {
        "run.mode": 1, "run.start": 4, "run.stop": 0, "run.pause": 0,
        "run.continue": 0, "run.abort": 0, "trigger.configure": 4,
        "angle.sweep": 3, "angle.pulse": 3, "angle.breakpoint": 2,
        "angle.clear": 0, "angle.speed": 1, "switch.channel": 2,
        "switch.polarization": 2, "sequence.plan": SEQUENCE_PLAN_WORDS,
        "sequence.active": 4,
    }[function.key]


def build_ctl(function: Function, values: Mapping[str, str],
              target_mask: int, required_mask: int) -> str:
    if function.ctl_name is None:
        raise ValueError(f"{function.title}不在 REFMEM 统一控制面目录中")
    if target_mask == 0:
        raise ValueError("目标掩码不能为 0：至少选择一块板")
    if required_mask & ~target_mask:
        raise ValueError("必需掩码必须是目标掩码的子集")
    params = ctl_params(function, values)
    head = f"SYSTem:CTRL:EXECute {function.ctl_name},{target_mask},{required_mask}"
    return head + ("," + ",".join(map(str, params)) if params else "")


def build_query(query: Query, values: Mapping[str, str]) -> str:
    parts: list[str] = []
    for field in query.args:
        value = _required(values.get(field.name, field.default), field)
        if not value:
            continue
        parse_int(value, field)
        parts.append(value)
    return query.scpi + (" " + ",".join(parts) if parts else "")


def example_values(fields: tuple[Field, ...]) -> dict[str, str]:
    return {field.name: field.default for field in fields}


def mask_from_slots(slots: set[int]) -> int:
    return sum(1 << slot for slot in slots if 0 <= slot < 32)


def slots_from_mask(mask: int) -> list[int]:
    return [slot for slot in range(32) if mask & (1 << slot)]


def split_response(response: str) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    """Split an SCPI response into display rows.

    ``CTRL:STATus?`` and the READ queries return a bare CSV, while several
    trigger handlers prefix ``1,<result>``.  Preserve that prefix when present
    so a non-zero result is not mistaken for a value.
    """
    text = response.strip()
    if not text:
        return [], []
    try:
        tokens = [token.strip() for token in next(csv.reader([text], strict=True))]
    except csv.Error:
        return [], [("raw", text)]
    prefix: list[tuple[str, str]] = []
    values = tokens
    if len(tokens) >= 2 and tokens[0] == "1" and tokens[1].lstrip("-").isdigit():
        prefix = [("status", tokens[0]), ("result", tokens[1])]
        values = tokens[2:]
    return prefix, [(str(index), value) for index, value in enumerate(values)]


def response_label(query: Query, index: int, value: str) -> str:
    if index < len(query.response_fields):
        return query.response_fields[index]
    return f"value[{index}]"
