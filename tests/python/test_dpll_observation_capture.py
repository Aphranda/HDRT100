"""Host-side contracts for the DPLL SD capture orchestration tool."""

from __future__ import annotations

from pathlib import Path

import pytest

from tools.dpll_observation_capture.dpll_observation_capture import (
    parse_save,
    parse_board,
    parse_role_status,
    role_status_delta,
)
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


def test_debug_admission_is_applied_before_follower_early_return() -> None:
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
    assert service.index("vdc_dpll_manager_apply_pending_debug_servo_tune()") > service.index(
        "role_snapshot.control.profile.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER")
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


def test_capture_tool_is_explicitly_off_realtime_path() -> None:
    source = Path(
        "tools/dpll_observation_capture/dpll_observation_capture.py"
    ).read_text(encoding="utf-8")
    assert "realtime_path_untouched" in source
    assert "StorageAO" in source
    assert "ROLE:STATus?" in source


def test_combined_convergence_svg_keys_nodes_and_marks_missing() -> None:
    point = ResidualPoint("NO1", 0.0, 10, 0, 0, 0, 5, 0, 0, 0, 1)
    svg = render_combined_svg(
        {"NO1": [point]},
        {"NO1": {"sample_count": 1, "analysis_confidence": "low_sample_count"}},
        lock_threshold_ns=1000)
    assert "NO1–NO4 DPLL convergence" in svg
    assert "NO1 samples=1" in svg
    assert "MISSING DATA: NO2, NO3, NO4" in svg
