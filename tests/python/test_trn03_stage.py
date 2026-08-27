from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = ROOT / "tools" / "calibration_ring_validate"
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

import trn03_stage as stage_module  # noqa: E402
from trn03_stage import (  # noqa: E402
    load_config,
    parse_query,
    persistence_matches,
    prepare_board_context,
    runtime_is_armed,
    runtime_is_stopped,
    runtime_status_for_node,
    stage_begin_command,
    stage_link_command,
)
from trn03_negative_gates import (  # noqa: E402
    DIAGNOSTIC_ONLY_FLAG,
    REQUIRED_EVIDENCE_FLAGS,
    error_is_clear,
    error_is_rejection,
    mutated_replay_begin_command,
    stage_begin_command as negative_stage_begin_command,
    stage_link_command as negative_stage_link_command,
)


def matrix() -> dict[str, object]:
    links = []
    for link_index in range(4):
        links.append({
            "link_index": link_index,
            "evidence_flags": 0x1F,
            "pio_persona": 1,
            "clkdiv_q16": 65536,
            "clk_sys_hz": 150_000_000,
            "instruction_period_ns": 17,
            "bit_cycles": 25,
            "marker_to_data_cycles": 10,
            "forward_residence_cycles": 5,
            "rx_arm_lead_cycles": 2,
            "codeword_cycles": 20,
            "guard_cycles": 2,
            "link_budget_cycles": 48,
            "loop_delay_cycles": 8,
            "marker_offset_sample_count": 0,
            "sck_offset_sample_count": 0,
            "data_offset_sample_count": 5,
            "sample_period_ns": 4,
            "link_base_delay_ns": 40,
            "marker_phase_delay_cycles": 10,
            "sck_phase_delay_cycles": 10,
            "data_phase_delay_cycles": 5,
            "marker_source_node": link_index,
            "marker_destination_node": (link_index + 1) % 4,
            "data_source_node": (link_index + 1) % 4,
            "data_destination_node": link_index,
        })
    return {
        "node_count": 4,
        "profile_level": 7,
        "baud_hz": 10_000_000,
        "evidence_flags": 0x1F,
        "calibration_generation": 88,
        "topology_generation": 61,
        "topology_crc32": 0x1234,
        "profile_crc32": 0x5678,
        "schedule_crc32": 0x9ABC,
        "offset_matrix": {
            "sample_period_ns": 4,
            "full_matrix_row_count": 1,
            "active_row_id": 0,
            "rows": [{
                "row_id": 0,
                "marker_offset_sample_counts_by_node": [1, -1, 0, 1],
                "sck_offset_sample_counts_by_node": [0, 0, 0, 0],
                "data_offset_sample_counts_by_node": [5, 5, 5, 5],
            }],
        },
        "links": links,
    }


def write_matrix(tmp_path: Path, value: dict[str, object]) -> Path:
    path = tmp_path / "matrix.json"
    path.write_text(json.dumps(value), encoding="utf-8")
    return path


def test_load_config_and_commands_use_node_link_terms(tmp_path: Path) -> None:
    config = load_config(write_matrix(tmp_path, matrix()))
    assert config["node_count"] == 4
    assert stage_begin_command(config).startswith(
        "CALibration:TRAINing:STAGe:BEGin 4,31,88,61,")
    command = stage_link_command(config["links"][2])
    assert command.startswith("CALibration:TRAINing:STAGe:LINK 2,")
    assert "slot" not in command.lower()
    assert config["links"][0]["marker_phase_delay_cycles"] == 9
    assert config["links"][0]["sck_phase_delay_cycles"] == 10
    assert config["links"][0]["data_phase_delay_cycles"] == 15


def test_load_config_replaces_stale_values_for_every_node_and_signal(
        tmp_path: Path) -> None:
    value = matrix()
    value["offset_matrix"]["rows"][0][
        "sck_offset_sample_counts_by_node"] = [0, -1, 0, -1]
    value["offset_matrix"]["rows"][0][
        "data_offset_sample_counts_by_node"] = [5, 6, 4, 5]
    for link in value["links"]:
        link["marker_offset_sample_count"] = 9
        link["sck_offset_sample_count"] = 9
        link["data_offset_sample_count"] = 9
        link["marker_phase_delay_cycles"] = 19
        link["sck_phase_delay_cycles"] = 19
        link["data_phase_delay_cycles"] = 19

    config = load_config(write_matrix(tmp_path, value))
    observed = [(
        link["marker_offset_sample_count"],
        link["sck_offset_sample_count"],
        link["data_offset_sample_count"],
        link["marker_phase_delay_cycles"],
        link["sck_phase_delay_cycles"],
        link["data_phase_delay_cycles"],
    ) for link in config["links"]]
    assert observed == [
        (-1, -1, 5, 9, 9, 15),
        (0, 0, 6, 10, 10, 16),
        (1, -1, 4, 11, 9, 14),
        (1, 0, 5, 11, 10, 15),
    ]


def test_load_config_rejects_missing_node(tmp_path: Path) -> None:
    value = matrix()
    value["links"] = value["links"][:-1]
    with pytest.raises(ValueError, match="node_count"):
        load_config(write_matrix(tmp_path, value))


def test_load_config_rejects_expired_link_budget(tmp_path: Path) -> None:
    value = matrix()
    value["links"][2]["link_budget_cycles"] = 46
    with pytest.raises(ValueError, match="expires"):
        load_config(write_matrix(tmp_path, value))


def test_load_config_rejects_diagnostic_only_evidence(tmp_path: Path) -> None:
    value = matrix()
    value["evidence_flags"] |= 1 << 31
    with pytest.raises(ValueError, match="not TRN-03 eligible"):
        load_config(write_matrix(tmp_path, value))


@pytest.mark.parametrize("data_offset", [0, 1, 10])
def test_load_config_keeps_data_phase_independent_from_sck(
        tmp_path: Path, data_offset: int) -> None:
    value = matrix()
    value["offset_matrix"]["rows"][0][
        "data_offset_sample_counts_by_node"] = [data_offset] * 4
    loaded = load_config(write_matrix(tmp_path, value))
    assert [link["data_phase_delay_cycles"]
            for link in loaded["links"]] == [10 + data_offset] * 4


def test_load_config_rejects_sck_phase_that_cannot_rearm(tmp_path: Path) -> None:
    value = matrix()
    value["offset_matrix"]["rows"][0][
        "sck_offset_sample_counts_by_node"] = [1, 1, 1, 1]
    with pytest.raises(ValueError, match="SCK replay phase cannot re-arm"):
        load_config(write_matrix(tmp_path, value))


@pytest.mark.parametrize(
    ("signal", "node"), (("marker", 1), ("sck", 1), ("data", 0)))
def test_load_config_rejects_zero_cycle_active_phase(
        tmp_path: Path, signal: str, node: int) -> None:
    value = matrix()
    value["offset_matrix"]["rows"][0][
        f"{signal}_offset_sample_counts_by_node"][node] = -10
    with pytest.raises(ValueError, match="phase"):
        load_config(write_matrix(tmp_path, value))


def test_load_config_rejects_node_offset_loaded_on_wrong_link(
        tmp_path: Path) -> None:
    value = matrix()
    value["links"][1]["marker_destination_node"] = 3
    with pytest.raises(ValueError, match="topology"):
        load_config(write_matrix(tmp_path, value))


def test_load_config_rejects_duplicate_or_partial_matrix_rows(
        tmp_path: Path) -> None:
    value = matrix()
    duplicate = dict(value["offset_matrix"]["rows"][0])
    duplicate["row_id"] = 1
    value["offset_matrix"]["rows"].append(duplicate)
    value["offset_matrix"]["full_matrix_row_count"] = 2
    with pytest.raises(ValueError, match="duplicate row"):
        load_config(write_matrix(tmp_path, value))


def test_load_config_supports_eight_nodes(tmp_path: Path) -> None:
    value = matrix()
    value["node_count"] = 8
    marker_next = [5, 6, 7, 4, 0, 2, 3, 1]
    value["links"] = [
        {**value["links"][index % 4], "link_index": index,
         "marker_source_node": index,
         "marker_destination_node": marker_next[index],
         "data_source_node": marker_next[index],
         "data_destination_node": index}
        for index in range(8)
    ]
    value["offset_matrix"]["rows"][0][
        "marker_offset_sample_counts_by_node"] = [1, -1, 0, 1] * 2
    value["offset_matrix"]["rows"][0][
        "sck_offset_sample_counts_by_node"] = [0] * 8
    value["offset_matrix"]["rows"][0][
        "data_offset_sample_counts_by_node"] = [5] * 8
    config = load_config(write_matrix(tmp_path, value))
    assert len(config["links"]) == 8
    assert [link["marker_destination_node"]
            for link in config["links"]] == marker_next


def test_negative_gate_commands_are_node_link_scoped() -> None:
    identity = {
        "calibration_generation": 1,
        "topology_generation": 2,
        "topology_crc32": 3,
        "profile_crc32": 4,
        "schedule_crc32": 5,
    }
    begin = negative_stage_begin_command(
        4, REQUIRED_EVIDENCE_FLAGS | DIAGNOSTIC_ONLY_FLAG, identity)
    link = negative_stage_link_command(2, REQUIRED_EVIDENCE_FLAGS)
    assert begin.startswith("CALibration:TRAINing:STAGe:BEGin 4,2147483679,")
    assert link.startswith("CALibration:TRAINing:STAGe:LINK 2,2,3,3,2,31,")
    assert "slot" not in (begin + link).lower()


def test_negative_gate_expired_budget_is_one_cycle_short() -> None:
    values = negative_stage_link_command(
        0, REQUIRED_EVIDENCE_FLAGS, expired=True).split(maxsplit=1)[1]
    fields = [int(value) for value in values.split(",")]
    required = sum(fields[index] for index in (11, 12, 13, 14, 15, 17))
    assert fields[16] == required - 1


@pytest.mark.parametrize(
    ("signal", "offset_field", "phase_field", "offset", "phase"),
    (("marker", 18, 23, -1, 10),
     ("sck", 19, 24, -1, 10),
     ("data", 20, 25, 4, 15)),
)
def test_negative_gate_can_inject_each_offset_phase_mismatch(
        signal: str, offset_field: int, phase_field: int,
        offset: int, phase: int) -> None:
    values = negative_stage_link_command(
        0, REQUIRED_EVIDENCE_FLAGS,
        offset_phase_mismatch=signal).split(maxsplit=1)[1]
    fields = [int(value) for value in values.split(",")]
    assert fields[offset_field] == offset
    assert fields[phase_field] == phase


def test_negative_gate_mutates_real_replay_identity_only(
        tmp_path: Path) -> None:
    config = load_config(write_matrix(tmp_path, matrix()))
    begin = mutated_replay_begin_command(
        config, profile_crc32=config["profile_crc32"] ^ 1)
    begin_fields = [int(value) for value in begin.split(maxsplit=1)[1].split(",")]
    assert begin_fields[5] == config["profile_crc32"] ^ 1
    assert begin_fields[6] == config["schedule_crc32"]


def test_error_is_clear_parses_scpi_queue_response() -> None:
    assert error_is_clear('0,"No error"')
    assert not error_is_clear('-200,"Execution error"')
    assert error_is_rejection('-200,"Execution error"')


def test_runtime_status_is_exposed_with_node_terms() -> None:
    raw = {
        "ring_enabled": 1,
        "ring_node_count": 4,
        "ring_local_slot_id": 2,
        "ring_reference_slot_id": 0,
        "ring_adapter_started": 1,
        "ring_up_running": 0,
        "ring_down_running": 0,
    }
    armed = runtime_status_for_node(raw, 2)
    assert runtime_is_armed(armed, 4, 2)
    assert "slot" not in " ".join(armed).lower()
    armed["ring_enabled"] = 0
    armed["ring_adapter_started"] = 0
    assert runtime_is_stopped(armed)


def test_persistence_status_requires_identity_crc_and_loaded_state() -> None:
    config = matrix()
    raw = (
        '"TRN03NVS",1,1,0,0,88,2,305419896,88,61,4660,22136,39612')
    status = parse_query(
        raw, stage_module.PERSISTENCE_QUERY_FIELDS, "TRN03NVS")
    assert persistence_matches(status, config, loaded=True)
    status["loaded"] = 0
    status["restore_pending"] = 1
    status["reject_reason"] = 7
    assert persistence_matches(status, config, loaded=False)
    status["profile_crc32"] ^= 1
    assert not persistence_matches(status, config, loaded=False)


def test_prepare_context_restores_profile_and_topology(
        monkeypatch: pytest.MonkeyPatch) -> None:
    config = matrix()
    config["profile_level"] = 7
    config["baud_hz"] = 10_000_000
    commands: list[str] = []
    board = type("Board", (), {"address": "node2"})()
    args = type("Args", (), {"arm_wait": 1.0})()

    monkeypatch.setattr(
        stage_module, "checked_action",
        lambda board, command, args: commands.append(command) or {
            "command": command, "passed": True})
    monkeypatch.setattr(
        stage_module, "wait_stopped",
        lambda board, args, node_index: {
            "node_index": node_index, "ring_enabled": 0,
            "ring_adapter_started": 0})
    monkeypatch.setattr(
        stage_module, "board_command",
        lambda board, command, args:
            ('1' if command.endswith('FLIGHT:MODE?') else
             f'7,10000000,100,1,0,{config["profile_crc32"]},0,0,0,0,0,0'))
    monkeypatch.setattr(
        stage_module, "ring_status",
        lambda board, args: {
            "ring_node_count": 4,
            "ring_local_slot_id": 2,
            "ring_reference_slot_id": 0,
            "ring_profile_crc32": config["profile_crc32"],
            "ring_schedule_crc32": config["schedule_crc32"],
        })

    context = prepare_board_context(board, config, 2, args)
    assert context["passed"] is True
    assert commands == [
        "SYSTem:TDMA:RING:STOP",
        "SYSTem:TDMA:FLIGHT:MODE 0",
        "SYSTem:TDMA:OPMode:STAGe 7",
        "SYSTem:TDMA:OPMode:APPLy",
        "SYSTem:TDMA:RING:TOPology 4,2,0",
    ]


def test_prepare_context_rejects_non_raw_flight_mode(
        monkeypatch: pytest.MonkeyPatch) -> None:
    config = matrix()
    config["profile_level"] = 7
    config["baud_hz"] = 10_000_000
    board = type("Board", (), {"address": "node0"})()
    args = type("Args", (), {"arm_wait": 1.0})()
    monkeypatch.setattr(
        stage_module, "checked_action",
        lambda board, command, args: {"command": command})
    monkeypatch.setattr(
        stage_module, "wait_stopped",
        lambda board, args, node_index: {})
    monkeypatch.setattr(
        stage_module, "board_command",
        lambda board, command, args:
            ('2' if command.endswith('FLIGHT:MODE?') else
             f'7,10000000,100,1,0,{config["profile_crc32"]},0,0,0,0,0,0'))
    with pytest.raises(RuntimeError, match="raw-flight mode not active"):
        prepare_board_context(board, config, 0, args)
