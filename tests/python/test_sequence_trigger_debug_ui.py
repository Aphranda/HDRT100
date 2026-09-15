from types import SimpleNamespace

import pytest

from tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui import (
    build_configuration_commands,
    build_ota_command,
    discover_serial_ports,
    discover_visa_resources,
    format_switch_position,
    parse_usb_mode,
)


def test_output_codes_are_mapped_to_positional_state_ids():
    commands = build_configuration_commands("PLAN", [1, 2, 4], "BUS", "RIS", 10, 5)

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
        build_configuration_commands("PLAN", codes, "BUS", "RIS", 10, 5)


def test_output_roles_build_level_mode_and_validate_physical_exclusion():
    commands = build_configuration_commands(
        "PLAN", [0, 1, 2, 3], "BUS", "RIS", 10, 99,
        sequence_output_mask=3, status_output_mask=12, status_mode="电平")

    assert "CONF:SEQ:OUTPUT 3,12,LEVEL,10,0" in commands

    with pytest.raises(ValueError, match="互不重叠"):
        build_configuration_commands(
            "PLAN", [0, 1], "BUS", "RIS", 10, 5,
            sequence_output_mask=3, status_output_mask=2)

    with pytest.raises(ValueError, match="掩码"):
        build_configuration_commands(
            "PLAN", [4], "BUS", "RIS", 10, 5,
            sequence_output_mask=3, status_output_mask=12)


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
