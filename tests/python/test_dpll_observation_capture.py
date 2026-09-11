"""Host-side contracts for the DPLL SD capture orchestration tool."""

from __future__ import annotations

from argparse import Namespace
from pathlib import Path

import pytest

from tools.dpll_observation_capture.dpll_observation_capture import (
    parse_save,
    parse_board,
    parse_refmem_vdc_follower_rx,
    parse_role_status,
    refmem_vdc_follower_rx_delta,
    role_status_delta,
    summarize_follower_observation,
    summarize_follower_transport,
)
import tools.dpll_observation_capture.dpll_observation_capture as capture
from tools.dpll_servo_tune.dpll_servo_tune import Profile, parse_profile
from tools.dpll_residual_analyze.dpll_residual_analyze import render_combined_svg
from tools.dpll_residual_analyze.dpll_residual_analyze import ResidualPoint
from tools.scpi_common.scpi_serial import scpi_response_matches_command


def test_trace_save_response_is_kept_as_composite_tuple() -> None:
    response = '"QUEUED",17,"/traces/run/no5_20260829.bin",1435'
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TRACe:SAVE", response
    )
    assert parse_save(response) == (17, "/traces/run/no5_20260829.bin", 1435)


def test_trace_arm_and_stop_keep_ok_tuple() -> None:
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TRACe:ARM", '"OK",0,8000'
    )
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TRACe:STOP", '"OK",8000,0'
    )


def test_trace_save_response_accepts_abbreviated_header() -> None:
    assert scpi_response_matches_command(
        "SYST:SYNC:VDC:DPLL:TRACE:SAVE",
        "QUEUED,3,\"/traces/run/no1.bin\",0",
    )


def test_debug_dpll_tune_and_filter_responses_preserve_signed_values() -> None:
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TUNE",
        '"OK",4,-65536,4096,0,0,4294967295,1234',
    )
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:COEFficient?",
        "-65536,4096,0,0,4294967295,1234,4,4,0",
    )
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:FILTer?",
        "4,17,-2,300,-400,-500,3,1",
    )
    assert parse_profile("-65536,4096,0,0,4294967295,1234,4,4,0") == Profile(
        -65536, 4096, 0, 0, 4294967295)


def test_debug_admission_override_is_explicit_and_mailbox_bound() -> None:
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:OVERRide", '"OK",1,7'
    )
    assert scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:OVERRide?",
        '1,"DEBUG_ADMISSION","ACTIVE",3,11,2,99,7,7',
    )
    manager = Path(
        "components/vdc_dpll_manager/src/vdc_dpll_manager.c"
    ).read_text(encoding="utf-8")
    domain = Path("components/vdc_domain/src/vdc_domain.c").read_text(
        encoding="utf-8"
    )
    refmem = Path(
        "components/distributed_refmem/src/distributed_refmem.c"
    ).read_text(encoding="utf-8")
    assert "s_debug_continue_requested_generation" in manager
    assert "vdc_dpll_manager_apply_pending_debug_continue" in manager
    assert "vdc_domain_set_debug_continue" in manager
    assert "vdc_domain_debug_gate_recoverable" in domain
    assert "vdc_domain_record_debug_continue" in domain
    assert "snapshot->dpll.debug_continue_enabled == 0u" in refmem


def test_trace_save_response_rejects_non_capture_tuple() -> None:
    assert not scpi_response_matches_command(
        "SYSTem:SYNC:VDC:DPLL:TRACe:SAVE", '"OK",3,"/traces/run/no1.bin"'
    )


def test_debug_admission_and_follower_observation_share_service_path() -> None:
    manager = Path(
        "components/vdc_dpll_manager/src/vdc_dpll_manager.c"
    ).read_text(encoding="utf-8")
    service_start = manager.index(
        "void VDC_DPLL_MANAGER_TIME_CRITICAL(sync_dpll_fb_service)(void)")
    service_end = manager.index(
        "void vdc_dpll_manager_dpll_service(void)", service_start)
    service = manager[service_start:service_end]
    assert service.index("vdc_dpll_manager_apply_pending_debug_continue()") < service.index(
        "vdc_dpll_manager_get_dpll_role_status")
    assert "role_snapshot.control.profile.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER" not in service
    assert service.index("vdc_dpll_manager_apply_pending_debug_servo_tune()") < service.index(
        "vdc_dpll_manager_prepare_ring_evidence()")
    assert "DPLL_CAPTURE_KIND_FOLLOWER_EVIDENCE" in manager
    with pytest.raises(ValueError):
        parse_save('"OK",3,"/traces/run/no1.bin"')


def test_board_parser_keeps_physical_no_identity() -> None:
    assert parse_board(" no5 = COM25 ").name == "NO5"
    with pytest.raises(ValueError):
        parse_board("slot5=COM25")


def test_follower_status_is_complete_and_counter_delta_is_wrap_safe() -> None:
    before = parse_role_status(
        "1,0,7,7,0,4294967295,3,4,5,6,7,0,7,9,10,11,12"
    )
    after = parse_role_status("1,0,7,7,0,1,5,4,6,6,7,0,7,10,10,11,12")
    delta = role_status_delta(before, after)
    assert before["mode"] == 1
    assert delta["follower_apply_count"] == 2
    assert delta["follower_no_command_count"] == 2
    assert delta["follower_stale_command_count"] == 1


def test_follower_status_rejects_partial_scpi_response() -> None:
    with pytest.raises(ValueError):
        parse_role_status("1,0,7,7,0")


def test_refmem_follower_rx_snapshot_is_fixed_and_wrap_safe() -> None:
    before = parse_refmem_vdc_follower_rx(
        "1,7,99,98,4294967295,5,9,2,3,4,5,6,7,8,0,4294967295"
    )
    after = parse_refmem_vdc_follower_rx(
        "1,7,100,99,1,8,11,2,3,4,5,6,7,8,0,1"
    )
    delta = refmem_vdc_follower_rx_delta(before, after)
    assert delta["submitted_count"] == 2
    assert delta["frame_ready_count"] == 3
    assert delta["accepted_count"] == 2
    with pytest.raises(ValueError):
        parse_refmem_vdc_follower_rx("1,7,99")


def test_follower_transport_requires_refmem_acceptance_and_expected_source() -> None:
    role = parse_role_status("1,2,7,7,0,0,0,0,0,0,0,0,0,0,0,0,0")
    before = parse_refmem_vdc_follower_rx(
        "1,7,99,98,10,10,10,0,0,0,0,0,0,0,2,99"
    )
    after = parse_refmem_vdc_follower_rx(
        "1,7,100,99,12,12,12,0,0,0,0,0,0,0,2,101"
    )
    summary = summarize_follower_transport(
        role, before, after, refmem_vdc_follower_rx_delta(before, after))
    assert summary["transport_verified"] is True
    assert summary["source_matches_configured_master"] is True
    assert summary["command_sequence_advanced"] is True

    wrong_source = dict(after)
    wrong_source["last_source_slot"] = 3
    rejected = summarize_follower_transport(
        role, before, wrong_source,
        refmem_vdc_follower_rx_delta(before, wrong_source))
    assert rejected["transport_verified"] is False
    assert rejected["reason"] == "refmem_receive_not_verified"


def test_refmem_receive_telemetry_is_distinct_from_tx_envelope() -> None:
    header = Path("middleware/scpi_port/inc/scpi_system_snapshot_commands.h").read_text(
        encoding="utf-8")
    source = Path("middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(
        encoding="utf-8")
    assert 'SYSTem:REFMEM:SYNC:TDMA:VDC:RX?' in header
    handler = source.split("scpi_cmd_refmem_sync_tdma_vdc_rx_q", 1)[1].split(
        "scpi_cmd_refmem_sync_tdma_abort", 1)[0]
    assert "distributed_refmem_get_vdc_follower_rx" in handler
    assert "distributed_refmem_build_realtime_tdma_vdc_envelope" not in handler


def test_follower_observation_requires_ordered_multi_point_apply_records() -> None:
    delta = {
        "follower_apply_count": 2,
        "follower_no_command_count": 0,
        "follower_wrong_source_count": 0,
        "follower_stale_command_count": 0,
        "follower_invalid_command_count": 0,
        "follower_local_evidence_bypass_count": 0,
    }
    samples = [
        {"capture_kind": "follower_applied_command", "follower_command": {
            "applied": True, "source_slot_id": 0, "control_generation": 9,
            "command_seq": 10, "effective_vdc_time_ns": 1000,
        }},
        {"capture_kind": "follower_applied_command", "follower_command": {
            "applied": True, "source_slot_id": 0, "control_generation": 9,
            "command_seq": 11, "effective_vdc_time_ns": 2000,
        }},
    ]
    summary = summarize_follower_observation(samples, delta, True)
    assert summary["multi_point_ready"] is True
    assert summary["capture_records_covered_by_apply_counter"] is True
    assert summary["source_slots"] == [0]
    assert summary["last_command_seq"] == 11


def test_follower_observation_requires_ordered_local_evidence_records() -> None:
    delta = {
        "follower_apply_count": 0,
        "follower_no_command_count": 0,
        "follower_wrong_source_count": 0,
        "follower_stale_command_count": 0,
        "follower_invalid_command_count": 0,
        "follower_local_evidence_bypass_count": 2,
    }
    samples = [
        {"capture_kind": "follower_local_evidence", "follower_observation": {
            "source_slot_id": 2, "sample_seq": 31,
            "phase_error_ns": -12, "gate_reject_code": 0,
        }},
        {"capture_kind": "follower_local_evidence", "follower_observation": {
            "source_slot_id": 2, "sample_seq": 32,
            "phase_error_ns": 8, "gate_reject_code": 0,
        }},
    ]
    summary = summarize_follower_observation(samples, delta, True)
    assert summary["mode"] == "follower_local_evidence"
    assert summary["multi_point_ready"] is True
    assert summary["accepted_observation_sample_count"] == 2
    assert summary["source_slots"] == [2]


def test_capture_tool_is_explicitly_off_realtime_path() -> None:
    source = Path(
        "tools/dpll_observation_capture/dpll_observation_capture.py"
    ).read_text(encoding="utf-8")
    assert "realtime_path_untouched" in source
    assert "StorageAO" in source
    assert "ROLE:STATus?" in source


def test_zero_sample_master_keeps_all_node_evidence_and_writes_failure_summary(
        monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """A diagnostic reject must not make NO2--NO4 evidence disappear."""
    role = "0,0,7,7,0,0,0,0,0,0,0,0,0,0,0,0,0"
    refmem = "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"
    status = "0,0,0,0,0,0,0,0"
    queries: list[tuple[str, str]] = []

    def fake_query(board: object, text: str, args: Namespace) -> str:
        del args
        name = getattr(board, "name")
        queries.append((name, text))
        if text.endswith("ROLE:STATus?"):
            return role
        if text.endswith("TDMA:VDC:RX?"):
            return refmem
        if text.endswith("TRACe:STATus?"):
            return status
        if text.endswith("TRACe:ARM") or text.endswith("TRACe:STOP"):
            return '"OK",0,0'
        raise AssertionError(f"unexpected query {name}: {text}")

    monkeypatch.setattr(capture, "query", fake_query)
    monkeypatch.setattr(capture.time, "sleep", lambda _: None)
    result = capture.run(Namespace(
        board=["NO1=COM1", "NO2=COM2", "NO3=COM3", "NO4=COM4"],
        duration_s=0.01, baud=115200, timeout=1.0, settle=0.0,
        out_dir=tmp_path, skip_capture=False,
    ))

    assert result["passed"] is False
    assert result["missing_master_boards"] == ["NO1", "NO2", "NO3", "NO4"]
    assert [item["board"] for item in result["boards"]] == [
        "NO1", "NO2", "NO3", "NO4"]
    assert all(item["trace_absent_reason"] ==
               "master_dpll_update_not_published_in_capture_window"
               for item in result["boards"])
    assert all("trace_status_raw" in item and
               "refmem_vdc_follower_rx_after_raw" in item
               for item in result["boards"])
    assert {name for name, text in queries if text.endswith("TRACe:STATus?")} == {
        "NO1", "NO2", "NO3", "NO4"}
    summary = (tmp_path / "summary.json").read_text(encoding="utf-8")
    assert '"passed": false' in summary


def test_combined_convergence_svg_keys_nodes_and_marks_missing() -> None:
    points = [
        ResidualPoint("NO1", 0.0, 10, 0, 0, 0, 5, 0, 0, 0, 1),
        ResidualPoint("NO1", 1.0, 20, 0, 0, 0, 5, 0, 0, 0, 2),
    ]
    points[0].centered_residual_ns = -5
    points[1].centered_residual_ns = 5
    svg = render_combined_svg(
        {"NO1": points},
        {"NO1": {"sample_count": 2, "analysis_confidence": "low_sample_count"}},
        lock_threshold_ns=1000)
    assert "NO1–NO4 DPLL convergence" in svg
    assert "NO1 samples=2" in svg
    assert 'class="trendline"' in svg
    assert "dashed: linear convergence trend" in svg
    assert "MISSING DATA: NO2, NO3, NO4" in svg
