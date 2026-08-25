from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = ROOT / "tools" / "calibration_ring_validate"
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

from trn03_stage import (  # noqa: E402
    load_config,
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
            "marker_destination_node": (link_index + 1) % 4,
            "data_destination_node": link_index,
        })
    return {
        "node_count": 4,
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


def test_load_config_supports_eight_nodes(tmp_path: Path) -> None:
    value = matrix()
    value["node_count"] = 8
    value["links"] = [
        {**value["links"][index % 4], "link_index": index,
         "marker_destination_node": (index + 1) % 8,
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
    assert link.startswith("CALibration:TRAINing:STAGe:LINK 2,31,")
    assert "slot" not in (begin + link).lower()


def test_negative_gate_expired_budget_is_one_cycle_short() -> None:
    values = negative_stage_link_command(
        0, REQUIRED_EVIDENCE_FLAGS, expired=True).split(maxsplit=1)[1]
    fields = [int(value) for value in values.split(",")]
    required = sum(fields[index] for index in (7, 8, 9, 10, 11, 13))
    assert fields[12] == required - 1


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
