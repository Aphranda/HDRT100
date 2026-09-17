from types import SimpleNamespace

import pytest

from tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui import (
    ROLE_SEQUENCE,
    ROLE_STATUS,
    ROLE_GATEWAY,
    MODE_INDEPENDENT,
    MODE_RJ45,
    MODE_TURNTABLE,
    RING_STATUS_QUERY,
    SequenceUi,
    build_configuration_commands,
    build_mode_configuration,
    build_start_commands,
    execute_command_batch,
    format_link_status,
    format_counter_status,
    format_counter_history,
    build_ota_command,
    discover_serial_ports,
    discover_visa_resources,
    format_switch_position,
    parse_usb_mode,
)


def turntable_configuration(**changes):
    args = dict(mode=MODE_TURNTABLE, plan="SP8T", codes=list(range(8)), source="MANUAL",
        edge="RIS", settle_us=25, pulse_us=10, sequence_mask=7, status_mask=0,
        status_mode="NONE", ready_input="IN2", repeat_count=10)
    return build_mode_configuration(**(args | changes))


def test_turntable_configures_three_roles_and_physical_transport_without_starting():
    commands = turntable_configuration(counter_threshold=1234, counter_slot=4)
    assert "CONF:SEQ:NODE:ROLE 4,2,COUNTER" in commands
    assert commands.index("CONF:SEQ:NODE:ROLE 4,2,COUNTER") < commands.index("CONF:SEQ:NODE:ACT")
    assert "CONF:SEQ:NODE:ROLE 2,5,DUT" in commands
    assert "CONF:SEQ:NODE:ROLE 3,7,VNA" in commands
    assert "CONF:SEQ:LINK POSITION,4,2,3,IN1,1234,IN2,OUT4,10,5000,RIS" in commands
    assert "CONF:SEQ:OUTPUT 7,0,NONE,25,0" in commands
    assert "CONF:SEQ:REPEAT 10" in commands
    assert "READ:SEQ:COUNTER?" in commands
    assert not any(command.endswith(":START") for command in commands)
    assert build_start_commands(MODE_TURNTABLE) == build_start_commands(MODE_RJ45)


@pytest.mark.parametrize("changes", [dict(counter_threshold=0), dict(counter_threshold=2**32),
    dict(counter_slot=2), dict(vna_slot=2), dict(counter_slot=-1), dict(counter_slot=8),
    dict(counter_input="MANUAL"), dict(counter_input="IN5"), dict(ready_input="IN1"),
    dict(status_mode="PULSE", status_mask=8), dict(gateway_output="OUT1")])
def test_turntable_invalid_wiring_and_threshold_rejected(changes):
    with pytest.raises(ValueError):
        turntable_configuration(**changes)


def test_turntable_counter_guard_boundary_matches_firmware():
    commands = turntable_configuration(counter_threshold=0xffffffde)
    assert any(",4294967262," in command for command in commands)
    with pytest.raises(ValueError):
        turntable_configuration(counter_threshold=0xffffffdf)


@pytest.mark.parametrize("phase,label", [(9, "等待计数阈值"), (10, "等待计数通知回环"),
                                        (11, "等待下一位置重新武装")])
def test_turntable_wait_phases_are_named(phase, label):
    assert label in format_counter_status(f"1,1,1,1000,0,0,0,0,0,0,{phase},0")
    row = [1, phase, 0] + [0] * 20
    assert label in format_link_status(",".join(map(str, row)), 8)


def test_turntable_counter_readback_shows_progress_and_faults():
    text = format_counter_status("1,1,1,1000,2045,2,45,0,16,16,9,0")
    assert "2045" in text and "45/1000" in text and "16/16" in text
    with pytest.raises(ValueError):
        format_counter_status("1,1")


def test_turntable_history_decodes_outcomes_and_labels_request_snapshot():
    text = format_counter_history("65,3,9,10,7,10000,10012,7")
    assert "记录 65" in text and "序列索引 7" in text
    assert "请求时计数快照 10012" in text
    assert "已请求切换 / 切换完成 / 采样完成" in text
    assert "物理边沿测量" in text
    assert "采样完成" not in format_counter_history("1,1,1,1,0,1000,1000,3")
    for bad in ["1,2", "1,1,1,1,0,1,-1,7", "1,1,1,1,0,1,1,4294967296"]:
        with pytest.raises(ValueError):
            format_counter_history(bad)


def test_output_codes_are_mapped_to_positional_state_ids():
    commands = build_configuration_commands("PLAN", [1, 2, 4], "MANUAL", "RIS", 10, 5)

    assert "CONF:TRIG 3,0,1,1" in commands
    assert "CONF:SEQ PLAN,0,1,2" in commands
    assert "CONF:SEQ:CODE 0,1" in commands
    assert "CONF:SEQ:CODE 1,2" in commands
    assert "CONF:SEQ:CODE 2,4" in commands
    assert "CONF:SEQ:OUTPUT 7,8,PULSE,10,5" in commands
    assert "CONF:SEQ PLAN,1,2,4" not in commands


@pytest.mark.parametrize("codes", [[], [-1], [8]])
def test_output_codes_reject_empty_or_unrepresentable_values(codes):
    with pytest.raises(ValueError):
        build_configuration_commands("PLAN", codes, "MANUAL", "RIS", 10, 5)


def test_output_roles_build_level_mode_and_validate_physical_exclusion():
    commands = build_configuration_commands(
        "PLAN", [0, 1, 2, 3], "MANUAL", "RIS", 10, 99,
        sequence_output_mask=3, status_output_mask=12, status_mode="电平")

    assert "CONF:SEQ:OUTPUT 3,12,LEVEL,10,0" in commands

    with pytest.raises(ValueError, match="互不重叠"):
        build_configuration_commands(
            "PLAN", [0, 1], "MANUAL", "RIS", 10, 5,
            sequence_output_mask=3, status_output_mask=2)

    with pytest.raises(ValueError, match="掩码"):
        build_configuration_commands(
            "PLAN", [4], "MANUAL", "RIS", 10, 5,
            sequence_output_mask=3, status_output_mask=12)


@pytest.mark.parametrize("mode", ["无", "NONE", "none"])
def test_dut_only_commands_do_not_claim_status_output(mode):
    commands = build_configuration_commands(
        "SP8T", list(range(8)), "IN1", "RIS", 10, 99,
        sequence_output_mask=7, status_output_mask=0, status_mode=mode)

    assert [command for command in commands if command.startswith("CONF:SEQ:OUTPUT")] == [
        "CONF:SEQ:OUTPUT 7,0,NONE,10,0"]
    assert "CONF:SEQ:SOUR IN1,RIS" in commands
    assert "CONF:SEQ:CODE 7,7" in commands


@pytest.mark.parametrize("mask", [1, 8, -1, 16])
def test_dut_only_rejects_contradictory_status_mask(mask):
    with pytest.raises(ValueError, match="状态掩码必须为 0"):
        build_configuration_commands(
            "SP8T", [0], "MANUAL", "RIS", 10, 0,
            status_output_mask=mask, status_mode="NONE")


@pytest.mark.parametrize("mode", ["PULSE", "LEVEL"])
def test_legacy_status_modes_require_output_assignment(mode):
    with pytest.raises(ValueError, match="至少一个状态输出"):
        build_configuration_commands(
            "SP8T", [0], "MANUAL", "RIS", 10, 5,
            status_output_mask=0, status_mode=mode)


@pytest.mark.parametrize("mask", [0, -1, 16])
def test_dut_only_rejects_invalid_sequence_mask(mask):
    with pytest.raises(ValueError, match="OUT1–OUT4"):
        build_configuration_commands(
            "SP8T", [0], "MANUAL", "RIS", 10, 0,
            sequence_output_mask=mask, status_output_mask=0, status_mode="NONE")


def test_dut_only_can_use_all_outputs_for_encoding():
    commands = build_configuration_commands(
        "PLAN", [0, 15], "IN4", "FALL", 0, 0,
        sequence_output_mask=15, status_output_mask=0, status_mode="NONE")
    assert "CONF:SEQ:OUTPUT 15,0,NONE,0,0" in commands


def test_none_mode_clears_and_disables_status_assignments_until_reselected():
    class Variable:
        def __init__(self, value):
            self.value = value

        def get(self):
            return self.value

        def set(self, value):
            self.value = value

    class Widget:
        def state(self, states):
            self.disabled = "disabled" in states

    ui = SimpleNamespace(
        status_mode=Variable("无"), output_hint=Variable(""),
        pulse_entry=Widget(), out_checkbuttons=[Widget() for _ in range(4)],
        out_enabled=[Variable(True) for _ in range(4)],
        out_roles=[Variable(ROLE_SEQUENCE) for _ in range(3)] + [Variable(ROLE_STATUS)],
    )
    SequenceUi._update_status_mode(ui)
    assert SequenceUi._output_role_masks(ui) == (7, 0)
    assert ui.out_checkbuttons[3].disabled
    assert not ui.out_checkbuttons[0].disabled
    assert ui.pulse_entry.disabled
    assert "DUT" in ui.output_hint.get()

    ui.status_mode.set("脉冲")
    SequenceUi._update_status_mode(ui)
    assert not ui.out_checkbuttons[3].disabled
    assert not ui.pulse_entry.disabled
    assert SequenceUi._output_role_masks(ui) == (7, 0)
    ui.out_enabled[3].set(True)
    assert SequenceUi._output_role_masks(ui) == (7, 8)

    ui.status_mode.set("无")
    SequenceUi._update_status_mode(ui)
    ui.out_roles[3].set(ROLE_SEQUENCE)
    SequenceUi._update_status_mode(ui)
    assert not ui.out_checkbuttons[3].disabled
    ui.out_enabled[3].set(True)
    assert SequenceUi._output_role_masks(ui) == (15, 0)


def test_switch_position_uses_one_based_operator_position():
    response = '"READY",1,1,8,0,0,1,4294967295,4294967295,4294967295,4294967295,0,0,0,0,0,0,0,"NONE",0,0,0,0,0'

    assert format_switch_position(response) == "当前开关：第 1/8 位 （状态 0，运行态 READY）"


def test_switch_position_reports_released_output_while_idle():
    response = '"IDLE",1,1,8,1,1,2,1,1,1,1,0,1,1,0,0,0,0,"NONE",0,0,0,0,0'

    assert format_switch_position(response) == "当前开关：未运行（输出已释放）"


def test_serial_discovery_prefers_product_vid_and_natural_port_order():
    candidates = [
        SimpleNamespace(device="COM12", vid=0x1234),
        SimpleNamespace(device="COM10", vid=0x2e8a),
        SimpleNamespace(device="COM8", vid=0x2e8a),
    ]

    assert discover_serial_ports(candidates) == ["COM8", "COM10", "COM12"]


def test_ota_command_limits_update_to_selected_single_board(tmp_path):
    image = tmp_path / "firmware.pkg"
    out_dir = tmp_path / "evidence"

    command = build_ota_command(image, "COM8", out_dir)

    assert command[2] == str(image)
    assert command[command.index("--ports") + 1] == "COM8"
    assert command[command.index("--expected-board-count") + 1] == "1"
    assert command[command.index("--max-workers") + 1] == "1"
    assert command[command.index("--out-dir") + 1] == str(out_dir)


def test_tmc_ota_command_uses_existing_visa_sender(tmp_path):
    image = tmp_path / "firmware.pkg"

    command = build_ota_command(
        image, "USB0::0xCAFE::0x4030::SERIAL::INSTR",
        tmp_path / "evidence", "USB TMC")

    assert command[1].endswith("tools\\visa_ota_send\\visa_ota_send.py")
    assert command[2] == "USB0::0xCAFE::0x4030::SERIAL::INSTR"
    assert command[3] == str(image)
    assert command[-1] == "--boot"


def test_visa_discovery_filters_and_sorts_usb_instruments():
    class ResourceManager:
        def list_resources(self, query):
            assert query == "USB?*::INSTR"
            return ("USB0::B::INSTR", "USB0::A::INSTR")

    assert discover_visa_resources(ResourceManager()) == [
        "USB0::A::INSTR", "USB0::B::INSTR"]


@pytest.mark.parametrize("response, expected", [
    ('"CDC"', "CDC"), ('"USBTMC"', "USBTMC")])
def test_usb_mode_parser_accepts_runtime_switch_responses(response, expected):
    assert parse_usb_mode(response) == expected


@pytest.mark.parametrize("response", ["", "<timeout>", "1"])
def test_usb_mode_parser_explains_unsupported_firmware(response):
    with pytest.raises(ValueError, match="PROJECT_ENABLE_USB_RUNTIME_SWITCH"):
        parse_usb_mode(response)


@pytest.mark.parametrize("repeat", [0, 1, 10, 100, 10000])
def test_two_modes_preserve_explicit_repeat_and_do_not_start_during_configuration(repeat):
    independent = build_mode_configuration(MODE_INDEPENDENT, "SP8T", list(range(8)),
        "IN3", "FALL", 10, 20, 7, 8, "PULSE", repeat_count=repeat)
    combined = build_mode_configuration(MODE_RJ45, "SP8T", list(range(8)),
        "IN3", "FALL", 10, 20, 7, 0, "NONE", "IN4", 6000, repeat, "OUT4")
    for commands in [independent, combined]:
        assert commands[:3] == ["TRIG:STOP", "SYST:TDMA:RING:STOP", "CONF:SEQ:LINK OFF"]
        assert f"CONF:SEQ:REPEAT {repeat}" in commands
        assert not any(command.endswith(":START") for command in commands)
    assert "CONF:SEQ:SOUR IN3,FALL" in independent
    assert "CONF:SEQ:OUTPUT 7,8,PULSE,10,20" in independent
    assert "CONF:SEQ:SOUR MANUAL,FALL" in combined
    assert "CONF:SEQ:OUTPUT 7,0,NONE,10,0" in combined
    dut = combined.index("CONF:SEQ:NODE:ROLE 2,5,DUT")
    vna = combined.index("CONF:SEQ:NODE:ROLE 3,7,VNA")
    activation = combined.index("CONF:SEQ:NODE:ACT")
    topology = combined.index("SYST:TDMA:RING:TOPOLOGY 2,0,0")
    probe = combined.index("CAL:TOPOLOGY:PROBE 1,10")
    flight_mode = combined.index("SYST:TDMA:FLIGHT:MODE 1")
    link = combined.index("CONF:SEQ:LINK LOOPBACK,2,3,IN4,OUT4,20,6000,FALL")
    assert dut < vna < activation < topology < probe < flight_mode < link
    assert combined.index("SYST:TDMA:OPMODE:APPLY") < topology
    assert "SYST:TDMA:RING:ARM" not in combined


def test_combined_start_arms_and_trains_tdma_before_first_sequence_state():
    assert build_start_commands(MODE_INDEPENDENT) == ["TRIG:START"]
    assert build_start_commands(MODE_RJ45) == [
        "SYST:TDMA:RING:STOP", "SYST:TDMA:RING:ARM", "SYST:TDMA:RING:TRAIN 4096",
        "SYST:TDMA:RING:START", "TRIG:START"]


@pytest.mark.parametrize("changes", [dict(ready_input="BUS"), dict(timeout_ms=0),
    dict(timeout_ms=0x80000000), dict(pulse_us=0), dict(repeat_count=-1),
    dict(repeat_count=0x100000000), dict(repeat_count=536870912)])
def test_combined_invalid_configuration_rejected_before_any_io(changes):
    args = dict(mode=MODE_RJ45, plan="SP8T", codes=list(range(8)), source="IN1",
        edge="RIS", settle_us=10, pulse_us=10, sequence_mask=7, status_mask=0,
        status_mode="NONE", gateway_output="OUT4")
    with pytest.raises(ValueError):
        build_mode_configuration(**(args | changes))


@pytest.mark.parametrize("changes", [dict(gateway_output="OUT5"), dict(status_mask=8),
    dict(status_mode="PULSE"), dict(sequence_mask=15)])
def test_combined_rejects_invalid_or_overlapping_out_attributes(changes):
    args = dict(mode=MODE_RJ45, plan="SP8T", codes=list(range(8)), source="MANUAL",
        edge="RIS", settle_us=10, pulse_us=10, sequence_mask=7, status_mask=0,
        status_mode="NONE", gateway_output="OUT4")
    with pytest.raises(ValueError, match="OUT 属性"):
        build_mode_configuration(**(args | changes))


def test_combined_uses_explicit_out_attributes_for_encoding_and_vna_trigger():
    commands = build_mode_configuration(
        MODE_RJ45, "SP8T", [0, 1, 2, 3, 8, 9, 10, 11], "MANUAL", "RIS",
        25, 30, 11, 0, "NONE", "MANUAL", 5000, 1, "OUT3")

    assert "CONF:SEQ:OUTPUT 11,0,NONE,25,0" in commands
    assert "CONF:SEQ:LINK LOOPBACK,2,3,MANUAL,OUT3,30,5000,RIS" in commands


def test_maximum_eight_state_repeat_count_is_accepted():
    commands = build_mode_configuration(MODE_INDEPENDENT, "SP8T", list(range(8)),
        "IN1", "RIS", 10, 10, 7, 8, "PULSE", repeat_count=536870911)
    assert "CONF:SEQ:REPEAT 536870911" in commands


def test_link_display_counts_all_measurements_including_first_and_done():
    row = [1, 8, 0, 1, 2, 3, 4, 7, 48, 16, 0, 8, 8, 7, 2, 3, 0, 8, 10, 5000, 0, 1, 99]
    text = format_link_status(",".join(map(str, row)), 8)
    assert "已完成" in text and "轮次 1/1" in text and "切换完成 7" in text and "交换 99" in text
    with pytest.raises(ValueError):
        format_link_status("1,8,0", 8)


def test_executor_waits_for_stopped_ack_and_rejects_unactivated_roles():
    calls = []
    states = iter(['"BUSY",1,1,8', '"IDLE",1,1,8'])
    def exchange(command):
        calls.append(command)
        return {"TRIG:STOP": "1", "SYST:ERR?": '0,"No error"',
                "CONF:SEQ:NODE:ACT": '"REJECTED"'}.get(command) or next(states)
    with pytest.raises(RuntimeError, match="未激活"):
        execute_command_batch(["TRIG:STOP", "CONF:SEQ:NODE:ACT", "TRIG:START"], exchange,
                              lambda *_: None, sleep=lambda _: None)
    assert calls[:4] == ["TRIG:STOP", "SYST:ERR?", "TRIG:SEQ:NEXT?", "TRIG:SEQ:NEXT?"]
    assert "TRIG:START" not in calls


@pytest.mark.parametrize("stage_response", ['"STAGED"', '"REJECTED"', "1"])
def test_role_staging_uses_real_response_and_stops_on_rejection(stage_response):
    calls = []
    def exchange(command):
        calls.append(command)
        if command == "SYST:ERR?":
            return '0,"No error"'
        if command == "CONF:SEQ:NODE:ROLE 2,5,DUT":
            return stage_response
        return '"ACTIVE",1'
    commands = ["CONF:SEQ:NODE:ROLE 2,5,DUT", "CONF:SEQ:NODE:ACT"]
    if stage_response == '"STAGED"':
        execute_command_batch(commands, exchange, lambda *_: None)
        assert "CONF:SEQ:NODE:ACT" in calls
    else:
        with pytest.raises(RuntimeError, match="未暂存"):
            execute_command_batch(commands, exchange, lambda *_: None)
        assert "CONF:SEQ:NODE:ACT" not in calls


def test_executor_ring_ack_only_requires_verified_state_and_never_exempts_trigger_timeout():
    from tools.tdma_ring_monitor.tdma_field_parse import RUNTIME_FIELDS as FIELDS
    row = dict.fromkeys(FIELDS, 0)
    row.update(ring_config_seq=3, ring_applied_config_seq=2)
    calls = []
    def exchange(command):
        calls.append(command)
        if command == RING_STATUS_QUERY:
            response = ",".join(str(row[key]) for key in FIELDS)
            row["ring_applied_config_seq"] = 3
            return response
        return '0,"No error"' if command == "SYST:ERR?" else "<timeout>"
    execute_command_batch(["SYST:TDMA:RING:STOP"], exchange, lambda *_: None, sleep=lambda _: None)
    assert calls == ["SYST:TDMA:RING:STOP", "SYST:ERR?", RING_STATUS_QUERY, RING_STATUS_QUERY]
    with pytest.raises(RuntimeError, match="超时"):
        execute_command_batch(["TRIG:START", "TRIG:SEQ:NEXT"], exchange, lambda *_: None)
    assert "TRIG:SEQ:NEXT" not in calls


@pytest.mark.parametrize("link_row,message", [
    ([1, 3, 0, *([0] * 13), 1, *([0] * 6)], "不是 MANUAL"),
    ([1, 7, 2, *([0] * 20)], "尚不能接受 NEXT"),
])
def test_executor_preflights_link_before_next(link_row, message):
    calls = []
    def exchange(command):
        calls.append(command)
        if command == "READ:SEQ:LINK?":
            return ",".join(map(str, link_row))
        return "1"
    with pytest.raises(RuntimeError, match=message):
        execute_command_batch(["TRIG:SEQ:NEXT"], exchange, lambda *_: None)
    assert calls == ["READ:SEQ:LINK?"]


def test_visa_sequence_setter_reads_its_numeric_response(monkeypatch):
    import queue
    import sys
    calls = []
    class Instrument:
        def query(self, command):
            calls.append(("query", command))
            if command == "SYST:ERR?":
                return '0,"No error"'
            if command == "READ:SEQ:LINK?":
                return "0," + ",".join("0" for _ in range(22))
            return "1"
        def write(self, command):
            calls.append(("write", command))
        def close(self):
            pass
    instrument = Instrument()
    manager = SimpleNamespace(open_resource=lambda _: instrument, close=lambda: None)
    monkeypatch.setitem(sys.modules, "pyvisa", SimpleNamespace(ResourceManager=lambda: manager))
    ui = SimpleNamespace(_ui_events=queue.Queue())
    assert SequenceUi.run_commands(ui, "USB TMC", "USB::test", ["TRIG:SEQ:NEXT"])
    assert calls == [("query", "READ:SEQ:LINK?"), ("query", "TRIG:SEQ:NEXT"),
                     ("query", "SYST:ERR?")]


def test_combined_next_sends_unified_command():
    sent = []
    ui = SimpleNamespace(run_mode=SimpleNamespace(get=lambda: MODE_RJ45),
        source=SimpleNamespace(get=lambda: "MANUAL"),
        gateway_ready_input=SimpleNamespace(get=lambda: "MANUAL"), _device_mode=MODE_RJ45,
        log=lambda *args: None, enqueue_commands=sent.append)
    SequenceUi.command(ui, "TRIG:SEQ:NEXT")
    assert sent and sent[0][0] == "TRIG:SEQ:NEXT"
