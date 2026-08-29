from __future__ import annotations

import copy
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = ROOT / "tools" / "calibration_ring_validate"
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

import trn03_closed_loop as trn03  # noqa: E402
from tools.scpi_common.scpi_serial import scpi_response_matches_command  # noqa: E402
from trn03_closed_loop import (  # noqa: E402
    arm_with_evidence,
    checked_stopped_ring_action,
    counter_deltas,
    expected_flight_phase,
    parse_active_profile,
    parse_snapshot,
    resolve_profile_level,
    running_handoff_errors,
    scalar_readback_with_retry,
    startup_barrier_interval_errors,
    u32_delta,
    validate_flight_phase_readback,
    validate_dpll_schedule,
    validate_node,
    validate_fifo_reset,
    validate_soak_timeline,
    validate_tx_seed,
)


def dpll_schedule(*, run_count: int, max_runtime_cycles: int = 28000,
                  overrun_count: int = 0, deadline_miss_count: int = 0,
                  quarantined_mask: int = 0,
                  vdc_max_runtime_cycles: int = 40000,
                  vdc_overrun_count: int = 0,
                  refmem_max_runtime_cycles: int = 22000,
                  refmem_overrun_count: int = 0) -> dict:
    dpll_phase = {
        "start_cycle": 143000,
        "end_cycle": 177000,
        "wcet_cycles": 34000,
        "last_start_cycle": 143000,
        "last_runtime_cycles": max_runtime_cycles,
        "max_runtime_cycles": max_runtime_cycles,
        "run_count": run_count,
        "skip_count": 0,
        "start_miss_count": 0,
        "overrun_count": overrun_count,
        "deadline_miss_count": deadline_miss_count,
    }
    vdc_phase = dict(dpll_phase)
    vdc_phase.update({
        "start_cycle": 100000,
        "end_cycle": 143000,
        "wcet_cycles": 42000,
        "last_start_cycle": 100000,
        "last_runtime_cycles": vdc_max_runtime_cycles,
        "max_runtime_cycles": vdc_max_runtime_cycles,
        "overrun_count": vdc_overrun_count,
        "deadline_miss_count": 0,
    })
    refmem_phase = dict(dpll_phase)
    refmem_phase.update({
        "start_cycle": 197000,
        "end_cycle": 222000,
        "wcet_cycles": 24000,
        "last_start_cycle": 197000,
        "last_runtime_cycles": refmem_max_runtime_cycles,
        "max_runtime_cycles": refmem_max_runtime_cycles,
        "overrun_count": refmem_overrun_count,
        "deadline_miss_count": 0,
    })
    return {
        "quarantined_mask": quarantined_mask,
        "phases": [{}, vdc_phase, dpll_phase, {}, {}, refmem_phase],
    }


def test_dpll_schedule_gate_accepts_bounded_advancing_phase() -> None:
    result = validate_dpll_schedule(
        dpll_schedule(run_count=10), dpll_schedule(run_count=1010))
    assert result["passed"] is True
    assert result["errors"] == []
    assert result["phases"]["vdc"]["deltas"]["run_count"] == 1000
    assert result["phases"]["dpll"]["deltas"]["run_count"] == 1000
    assert result["phases"]["refmem"]["deltas"]["run_count"] == 1000


def test_dpll_schedule_gate_rejects_wcet_and_quarantine_regression() -> None:
    result = validate_dpll_schedule(
        dpll_schedule(run_count=10),
        dpll_schedule(run_count=11, max_runtime_cycles=35000,
                      overrun_count=1, deadline_miss_count=1,
                      quarantined_mask=1 << 1))
    assert result["passed"] is False
    assert result["errors"] == [
        "dpll_overrun_count_grew",
        "dpll_deadline_miss_count_grew",
        "dpll_load_quarantined",
        "dpll_max_runtime_exceeded_wcet",
    ]


def test_dpll_schedule_gate_rejects_vdc_evidence_producer_failure() -> None:
    result = validate_dpll_schedule(
        dpll_schedule(run_count=10),
        dpll_schedule(run_count=11, vdc_max_runtime_cycles=43000,
                      vdc_overrun_count=1, quarantined_mask=1 << 0))
    assert result["passed"] is False
    assert result["errors"] == [
        "vdc_overrun_count_grew",
        "vdc_load_quarantined",
        "vdc_max_runtime_exceeded_wcet",
    ]


def test_dpll_schedule_gate_rejects_refmem_publication_failure() -> None:
    result = validate_dpll_schedule(
        dpll_schedule(run_count=10),
        dpll_schedule(run_count=11, refmem_max_runtime_cycles=25000,
                      refmem_overrun_count=1,
                      quarantined_mask=1 << 4))
    assert result["passed"] is False
    assert result["errors"] == [
        "refmem_overrun_count_grew",
        "refmem_load_quarantined",
        "refmem_max_runtime_exceeded_wcet",
    ]


def test_dpll_refmem_realtime_hot_paths_are_sram_resident() -> None:
    manager = (ROOT / "components" / "vdc_dpll_manager" / "src" /
               "vdc_dpll_manager.c").read_text(encoding="utf-8")
    refmem = (ROOT / "components" / "distributed_refmem" / "src" /
              "distributed_refmem.c").read_text(encoding="utf-8")
    vectors = (ROOT / "components" / "distributed_refmem" / "src" /
               "refmem_vector_table.c").read_text(encoding="utf-8")
    for symbol in (
        "vdc_dpll_manager_get_snapshot",
        "vdc_dpll_manager_published_update_seq",
    ):
        assert f"VDC_DPLL_MANAGER_TIME_CRITICAL({symbol})" in manager or (
            "VDC_DPLL_MANAGER_TIME_CRITICAL(\n"
            f"    {symbol})" in manager)
    assert ("DISTRIBUTED_REFMEM_TIME_CRITICAL(\n"
            "    distributed_refmem_realtime_run_once)" in refmem)
    for symbol in (
        "refmem_vdc_vector_payload_crc",
        "refmem_dpll_vector_payload_crc",
        "refmem_vector_fast_crc32",
    ):
        assert f"REFMEM_VECTOR_TIME_CRITICAL({symbol})" in vectors


def test_refmem_runtime_vectors_are_mirrored_in_separate_beats() -> None:
    refmem = (ROOT / "components" / "distributed_refmem" / "src" /
              "distributed_refmem.c").read_text(encoding="utf-8")
    service = refmem.split(
        "distributed_refmem_realtime_run_once)(void)", 1
    )[1].split("void distributed_refmem_service", 1)[0]
    vdc_publish = service.index(
        "distributed_refmem_publish_vdc_vector_payload")
    split_return = service.index("return;", vdc_publish)
    dpll_publish = service.index(
        "distributed_refmem_publish_dpll_vector_payload")
    assert vdc_publish < split_return < dpll_publish
    assert "s_vdc_vector_source_update_seq" in service
    assert "s_dpll_vector_source_update_seq" in service


def test_running_handoff_requires_complete_healthy_physical_ring() -> None:
    healthy = {
        "ring_enabled": 1,
        "ring_node_count": 4,
        "ring_local_node": 2,
        "ring_reference_node": 0,
        "ring_adapter_started": 1,
        "ring_up_running": 1,
        "ring_down_running": 1,
    }
    assert running_handoff_errors(healthy, 2, 4) == []

    unhealthy = dict(healthy, ring_down_running=0)
    assert running_handoff_errors(unhealthy, 2, 4) == [
        "ring_down_running"]


def pio_instruction_count(source: str, program_name: str) -> int:
    """Count assembled instructions in one source-level PIO program."""
    program = source.split(f".program {program_name}", 1)[1].split(
        ".program", 1)[0]
    assembly = program.split("% c-sdk", 1)[0]
    return sum(
        1 for raw in assembly.splitlines()
        if (line := raw.strip()) and not line.startswith((";", "."))
        and not line.endswith(":")
    )


class FakeBoard:
    address = "node0"


def test_arm_with_evidence_requires_explicit_success(monkeypatch) -> None:
    responses = {
        "SYSTem:TDMA:RING:ARM": "<timeout>",
        "SYSTem:TDMA:RING:ARM:STATus?": "1",
        "SYSTem:ERR?": '0,"No error"',
    }
    monkeypatch.setattr(trn03, "drain_errors", lambda board, args: [
        '0,"No error"'])
    monkeypatch.setattr(
        trn03, "board_command",
        lambda board, command, args: responses[command])
    evidence = arm_with_evidence(FakeBoard(), object())
    assert evidence["arm_result"] == 1
    assert evidence["error_after"] == '0,"No error"'


def test_arm_status_query_accepts_scalar_success() -> None:
    assert scpi_response_matches_command(
        "SYSTem:TDMA:RING:ARM:STATus?", "1")


def test_wait_runtime_stopped_requires_core1_generation_ack(monkeypatch) -> None:
    snapshots = [
        {"ring_enabled": 0, "ring_adapter_started": 0,
         "ring_config_seq": 8, "ring_applied_config_seq": 7},
        {"ring_enabled": 0, "ring_adapter_started": 0,
         "ring_config_seq": 8, "ring_applied_config_seq": 8},
    ]
    monkeypatch.setattr(
        trn03, "runtime_snapshot",
        lambda board, args, node_index: snapshots.pop(0))
    args = type("Args", (), {"arm_wait": 1.0})()
    observed = trn03.wait_runtime_stopped(FakeBoard(), args, 0)
    assert observed["passed"] == 1
    assert observed["ring_applied_config_seq"] == 8
    assert not snapshots


def test_stopped_owner_action_retries_with_readback_evidence(monkeypatch) -> None:
    attempts = []
    stopped = []

    def checked(board, action, command, args):
        attempts.append(command)
        if len(attempts) < 3:
            raise RuntimeError(f"transient-{len(attempts)}")
        return {"node": board.address, "action": action,
                "command": command, "error_after": '0,"No error"'}

    monkeypatch.setattr(trn03, "checked_ring_action", checked)
    monkeypatch.setattr(
        trn03, "wait_runtime_stopped",
        lambda board, args, node_index: stopped.append(node_index) or {
            "ring_enabled": 0, "ring_adapter_started": 0,
            "ring_config_seq": 9, "ring_applied_config_seq": 9,
            "passed": 1})
    monkeypatch.setattr(trn03.time, "sleep", lambda delay: None)
    args = type("Args", (), {"owner_action_retries": 3})()
    evidence = checked_stopped_ring_action(
        FakeBoard(), "TOPOLOGY", "topology", args, 0)
    assert evidence["attempt_count"] == 3
    assert len(evidence["rejected_attempts"]) == 2
    assert stopped == [0, 0, 0]


def test_stopped_owner_action_does_not_hide_persistent_rejection(
        monkeypatch) -> None:
    monkeypatch.setattr(
        trn03, "checked_ring_action",
        lambda board, action, command, args: (_ for _ in ()).throw(
            RuntimeError("persistent")))
    monkeypatch.setattr(
        trn03, "wait_runtime_stopped",
        lambda board, args, node_index: {"passed": 1})
    monkeypatch.setattr(trn03.time, "sleep", lambda delay: None)
    args = type("Args", (), {"owner_action_retries": 3})()
    with pytest.raises(RuntimeError, match="after 3 STOPPED attempts"):
        checked_stopped_ring_action(
            FakeBoard(), "PROFILE_APPLY", "apply", args, 0)


def test_scalar_readback_retries_timeout_and_mismatch(monkeypatch) -> None:
    responses = iter(("<timeout>", "0", "1"))
    monkeypatch.setattr(
        trn03, "board_command",
        lambda board, command, args: next(responses))
    monkeypatch.setattr(trn03.time, "sleep", lambda delay: None)
    args = type("Args", (), {"owner_action_retries": 3})()
    evidence = scalar_readback_with_retry(
        FakeBoard(), "SYSTem:TDMA:FLIGHT:CLOCK:EVIDence?", args, 1,
        "clock evidence")
    assert evidence["value"] == 1
    assert evidence["attempt_count"] == 3
    assert evidence["rejected_readbacks"] == [
        {"attempt": 1, "response": "<timeout>",
         "error": "non-integer scalar"},
        {"attempt": 2, "response": "0", "observed": 0,
         "expected": 1, "error": "readback mismatch"},
    ]


def test_scalar_readback_reports_all_bounded_failures(monkeypatch) -> None:
    monkeypatch.setattr(
        trn03, "board_command",
        lambda board, command, args: "<timeout>")
    monkeypatch.setattr(trn03.time, "sleep", lambda delay: None)
    args = type("Args", (), {"owner_action_retries": 2})()
    with pytest.raises(RuntimeError, match=(
            "clock evidence readback did not reach 1 after 2 attempts")):
        scalar_readback_with_retry(
            FakeBoard(), "SYSTem:TDMA:FLIGHT:CLOCK:EVIDence?", args, 1,
            "clock evidence")


def test_raw_flight_mode_query_accepts_scalar_one() -> None:
    assert scpi_response_matches_command(
        "SYSTem:TDMA:FLIGHT:MODE?", "1")


def test_clock_evidence_query_accepts_scalar_one() -> None:
    assert scpi_response_matches_command(
        "SYSTem:TDMA:FLIGHT:CLOCK:EVIDence?", "1")


def test_origin_data_dma_covers_complete_physical_tail() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys_transport.c").read_text(encoding="utf-8")
    origin = source.split(
        "static bool tdma_pio_spi_phys_flight_origin_tx", 1
    )[1].split("bool tdma_pio_spi_phys_tx", 1)[0]
    assert "clock_bytes != phys->flight_physical_byte_count" in origin
    assert "index < clock_bytes" in origin
    assert "s_tdma_pio_spi_flight_tx_words,\n        clock_bytes," in origin
    assert "pio_get_dreq(data_pio, data_sm, true)" in origin
    assert "pio_sm_put(data_pio, data_sm, clock_bits - 1u)" in origin
    assert "wire_bytes - 1u" not in origin


def test_data_follower_wait_patch_does_not_own_sck_output() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    init = source.split(
        "static inline void tdma_pio_spi_flight_data_follower_program_init", 1
    )[1].split("static inline void", 1)[0]
    assert "pio_encode_wait_gpio(true, rx_sck_pin) |" in init
    assert "pio_encode_delay(data_phase_delay_cycles - 1u)" in init
    assert "pio_encode_sideset" not in init
    assert "pio->instr_mem[offset + 7u]" in init


def test_follower_samples_data_on_rising_edge_before_falling_edge() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    program = source.split(
        ".program tdma_pio_spi_flight_data_follower", 1
    )[1].split(".program", 1)[0]
    high = program.index("wait 1 gpio 0")
    sample = program.index("in pins, 1")
    low = program.index("wait 0 gpio 0")
    forward = program.index("out pins, 1")
    assert high < sample < low < forward


def test_process_follower_retains_elastic_byte_across_frame_boundary() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    program = source.split(
        ".program tdma_pio_spi_flight_process_follower", 1
    )[1].split(".program", 1)[0]
    assert "flight_process_pass:\n    ; Y retains" in program
    assert "mov osr, y" in program
    assert "mov y, isr" in program
    end_wait_high = program.index("wait 1 gpio 0")
    boundary_irq = program.index("irq set 3", end_wait_high)
    command_jump = program.index("jmp flight_process_command", boundary_irq)
    pass_label = program.index("flight_process_pass:", command_jump)
    boundary = program[end_wait_high:boundary_irq]
    assert "Keep Y across CS boundaries" in program
    assert "set y, 0" not in boundary
    assert "wait 0 gpio 0" not in boundary
    assert end_wait_high < boundary_irq < command_jump < pass_label
    assert "hardware\n    ; wrap occurs only after the final PUSH" in program
    assert "set y, 0" not in program

    init = source.split(
        "static inline void tdma_pio_spi_flight_process_follower_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "instr_mem[offset + 5u]" in init
    assert "pio_encode_wait_gpio(true, rx_csn_pin)" in init
    assert "instr_mem[offset + 6u]" not in init
    assert "instr_mem[offset + 11u]" in init
    assert "pio_encode_wait_gpio(true, rx_sck_pin)" in init
    assert "pio_encode_delay(data_phase_delay_cycles - 1u)" in init
    assert "instr_mem[offset + 13u]" in init
    assert "pio_encode_wait_gpio(false, rx_sck_pin)" in init
    bit_loop = program.split("flight_process_bit:", 1)[1].split(
        "mov y, isr", 1)[0]
    assert "wait 0 gpio 1" in bit_loop
    assert "out pins, 1" in bit_loop
    assert "nop" not in bit_loop
    assert bit_loop.index("wait 1 gpio 1") < bit_loop.index("in pins, 1")
    assert bit_loop.index("in pins, 1") < bit_loop.index("wait 0 gpio 1")
    assert "mov isr, null" not in program

    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    configure = phys.split(
        "static bool tdma_pio_spi_phys_configure_flight", 1
    )[1].split("static bool", 1)[0]
    assert "pio_encode_set(pio_y, 0u)" in configure

    overlay = (ROOT / "components" / "tdma" / "src" /
               "tdma_flight_overlay.c").read_text(encoding="utf-8")
    assert "alignment_byte_shift + 1u +" in overlay


def test_process_follower_forwards_control_on_independent_pio_sm() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    data_program = source.split(
        ".program tdma_pio_spi_flight_process_follower", 1
    )[1].split(".program", 1)[0]
    control_program = source.split(
        ".program tdma_pio_spi_flight_control_forward", 1
    )[1].split(".program", 1)[0]
    assert ".side_set" not in data_program
    assert "set pins" not in data_program
    assert "pull block" not in control_program
    assert "mov x, osr" in control_program
    assert "jmp x-- flight_control_bit" in control_program
    assert "wait 0 gpio 0" in control_program
    assert "wait 1 gpio 0" in control_program
    assert "wait 1 gpio 1" in control_program
    assert "wait 0 gpio 1" in control_program

    control_init = source.split(
        "static inline void tdma_pio_spi_flight_control_forward_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "pio_encode_delay(marker_phase_delay_cycles)" in control_init
    assert control_init.count(
        "pio_encode_delay(sck_phase_delay_cycles)") == 2
    assert "data_phase_delay_cycles" not in control_init
    assert "sm_config_set_set_pins(&c, tx_sck_pin, 2u)" in control_init
    # The control program no longer has a leading PULL. Keep the four runtime
    # GPIO patches on WAIT instructions 0/3/5/8; the stale 1/4/6/9 indices
    # overwrite SET pins and stop physical forwarding after the first node.
    for instruction in (0, 3, 5, 8):
        assert f"instr_mem[offset + {instruction}u]" in control_init
    for instruction in (1, 4, 6, 9):
        assert f"instr_mem[offset + {instruction}u]" not in control_init

    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    assert "control_bits - 1u" in phys
    assert ("tdma_pio_spi_phys_control_sm(phys),\n"
            "                            control_bits - 1u" in phys)
    assert "pio_encode_pull(false, true)" in phys


def test_flight_personas_fit_shared_pio_instruction_memory() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    shared = pio_instruction_count(
        source, "tdma_pio_spi_flight_control_forward")
    capture = pio_instruction_count(
        source, "tdma_pio_spi_flight_clock_latch")
    data_capture = pio_instruction_count(
        source, "tdma_pio_spi_flight_data_capture")
    origin_control = pio_instruction_count(
        source, "tdma_pio_spi_flight_origin_clock_rx")
    origin_rtt = pio_instruction_count(
        source, "tdma_pio_spi_flight_origin_rtt")
    raw_data = pio_instruction_count(
        source, "tdma_pio_spi_flight_data_follower")
    process_data = pio_instruction_count(
        source, "tdma_pio_spi_flight_process_follower")
    assert shared + capture + raw_data <= 32
    assert shared + capture + process_data <= 32
    assert shared + data_capture <= 32
    assert origin_control + data_capture + origin_rtt + capture <= 32


def test_product_clock_latch_captures_first_csn_edge() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    program = source.split(
        ".program tdma_pio_spi_flight_clock_latch", 1
    )[1].split(".program", 1)[0]
    assert "jmp pin flight_clock_latch_high" in program
    assert "mov isr, x" in program
    assert "push noblock" in program
    assert "jmp x-- flight_clock_latch_loop" in program

    capture_init = source.split(
        "static inline void tdma_pio_spi_flight_clock_latch_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "sm_config_set_jmp_pin(&c, csn_pin)" in capture_init
    assert "gpio_pull_up(csn_pin)" in capture_init


def test_process_follower_disarm_releases_overlay_tx_dma() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    disarm = phys.split("void tdma_pio_spi_phys_disarm", 1)[1].split(
        "static bool tdma_pio_spi_phys_tx_put", 1)[0]
    tx_abort = disarm.index(
        "dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel)")
    rx_abort = disarm.index(
        "dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel)")
    disable = disarm.index("pio_sm_set_enabled")
    assert tx_abort < disable
    assert rx_abort < disable


def test_partial_arm_disarm_cannot_return_before_hardware_cleanup() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    disarm = phys.split("void tdma_pio_spi_phys_disarm", 1)[1].split(
        "static bool tdma_pio_spi_phys_tx_put", 1)[0]
    first_dma_abort = disarm.index("dma_channel_abort")
    first_sm_disable = disarm.index("pio_sm_set_enabled")
    assert "if (!phys->armed)" not in disarm[:first_dma_abort]
    assert first_dma_abort < first_sm_disable


def test_clock_training_quiesces_complete_flight_persona() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    train = phys.split("bool tdma_pio_spi_phys_train_clock", 1)[1].split(
        "void tdma_pio_spi_phys_train_clock_service", 1)[0]
    select = train.index("tdma_pio_spi_phys_select_program_persona")
    assert train.index(
        "dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel)") < select
    assert train.index(
        "dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel)") < select
    assert train.index("phys->flight_clock_latch_armed = false") < select
    for state_machine in (
        "BOARD_TDMA_SPI_MASTER_SM",
        "BOARD_TDMA_SPI_SLAVE_SM",
        "BOARD_TDMA_SPI_CAPTURE_SM",
        "BOARD_TDMA_SPI_RTT_SM",
    ):
        assert f"pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, {state_machine})" in train
        assert f"pio_sm_restart(BOARD_TDMA_SPI_PIO, {state_machine})" in train


def test_process_follower_recovers_pass_script_after_bad_frame() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    service = phys.split(
        "bool tdma_pio_spi_phys_service_process_overlay_boundary", 1
    )[1].split("bool tdma_pio_spi_phys_set_process_image_mode", 1)[0]
    assert "const PIO data_pio = tdma_pio_spi_phys_data_pio(phys);" in service
    assert "pio_interrupt_get(data_pio, 3u)" in service
    assert "pio_interrupt_clear(data_pio, 3u)" in service
    assert "phys->flight_overlay_next_prepared" in service
    assert "phys->flight_overlay_boundary_pending = true" in service
    assert "TDMA_PIO_SPI_OVERLAY_GRACE_SERVICE_PASSES" in service
    assert "tdma_pio_spi_phys_prepare_pass_overlay(phys)" in service
    assert "overlay_pass_recovery_count++" in service
    assert "flight_overlay_pass_committed = true" in service
    grace = service.index("phys->flight_overlay_grace_remaining != 0u")
    fallback = service.index("tdma_pio_spi_phys_prepare_pass_overlay(phys)")
    assert grace < fallback

    adapter = (ROOT / "components" / "tdma" / "src" /
               "tdma_pio_spi_ring_adapter.c").read_text(encoding="utf-8")
    assert "phys_service_overlay_boundary" in adapter


def test_process_follower_defers_pass_until_parser_grace_expires() -> None:
    header = (ROOT / "components" / "tdma" / "inc" /
              "tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    assert "#define TDMA_PIO_SPI_OVERLAY_GRACE_SERVICE_PASSES 1u" in header
    assert "bool flight_overlay_boundary_pending;" in header
    assert "uint32_t flight_overlay_grace_remaining;" in header

    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    service = phys.split(
        "bool tdma_pio_spi_phys_service_process_overlay_boundary", 1
    )[1].split("bool tdma_pio_spi_phys_set_process_image_mode", 1)[0]
    boundary = service.index("if (boundary_observed)")
    pending = service.index("phys->flight_overlay_boundary_pending = true")
    prepared = service.index("if (phys->flight_overlay_next_prepared)")
    grace = service.index("if (phys->flight_overlay_grace_remaining != 0u)")
    fallback = service.index("tdma_pio_spi_phys_prepare_pass_overlay(phys)")
    assert boundary < pending < prepared < grace < fallback


def test_rx_scanner_retains_complete_shifted_outer_header_prefix() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    transport = (ROOT / "components" / "tdma" / "src" /
                 "tdma_pio_spi_phys_transport.c").read_text(encoding="utf-8")
    capture = phys.split(
        "bool tdma_pio_spi_phys_capture_words", 1
    )[1].split("bool tdma_pio_spi_phys_arm", 1)[0]
    assert "TDMA_PIO_SPI_PACKET_HEADER_SIZE + 1u" in capture
    assert "produced - retain_words" in capture
    assert "produced - 2u" not in capture


def test_core1_services_tdma_before_bounded_dpll_load() -> None:
    app = (ROOT / "application" / "src" / "app.c").read_text(
        encoding="utf-8")
    realtime = app.split("void app_realtime_run_once", 1)[1]
    tdma = realtime.index("APP_REALTIME_PHASE_TDMA")
    vdc = realtime.index("APP_REALTIME_PHASE_VDC")
    dpll = realtime.index("APP_REALTIME_PHASE_DPLL")
    refmem = realtime.index("APP_REALTIME_PHASE_REFMEM")
    assert tdma < vdc < dpll < refmem
    assert "app_realtime_run_phase" in realtime

    runtime = (ROOT / "application" / "src" /
               "app_runtime.c").read_text(encoding="utf-8")
    assert "PROJECT_CORE1_CYCLE_CYCLES" in runtime
    assert "app_realtime_cycle_counter_init();" in runtime
    assert "APP_REALTIME_PHASE_TABLE(APP_REALTIME_ASSERT_PHASE)" in runtime


def test_core1_overrun_quarantines_only_the_faulting_load() -> None:
    app = (ROOT / "application" / "src" / "app.c").read_text(
        encoding="utf-8")
    bounded = app.split("static bool app_realtime_run_phase", 1)[1]
    bounded = bounded.split("static void app_realtime_tdma_phase", 1)[0]
    assert "runtime_cycles > contract->wcet_cycles" in bounded
    assert "phase_overrun_count[phase_id]++" in bounded
    assert "__atomic_fetch_or(&s_realtime_load_quarantined_mask" in bounded
    assert "optional_load" in bounded
    assert "APP_REALTIME_LOAD_ALL_MASK" not in bounded
    assert "PROJECT_CORE1_SCHEDULE_WARMUP_CYCLES" in bounded
    assert "optional_load && !warmup_cycle" in bounded
    assert "inherited_lateness" in bounded
    assert "own_deadline_missed" in bounded
    before_service = bounded.split(
        "const uint32_t start_counter = app_realtime_cycle_now();", 1
    )[0]
    after_service = bounded.split(
        "const uint32_t start_counter = app_realtime_cycle_now();", 1
    )[1]
    assert "__atomic_fetch_or(&s_realtime_load_quarantined_mask" not in (
        before_service
    )
    assert "__atomic_fetch_or(&s_realtime_load_quarantined_mask" in (
        after_service
    )

    commands = (ROOT / "middleware" / "scpi_port" / "inc" /
                "scpi_system_snapshot_commands.h").read_text(encoding="utf-8")
    assert 'SYSTem:TDMA:LOAD:MASK?' in commands
    assert 'SYSTem:TDMA:SCHEDule?' in commands


def test_load_mask_releases_only_newly_enabled_loads() -> None:
    app = (ROOT / "application" / "src" / "app.c").read_text(
        encoding="utf-8")
    setter = app.split("bool app_realtime_set_load_mask", 1)[1]
    setter = setter.split("bool app_realtime_get_schedule_snapshot", 1)[0]
    assert "__atomic_exchange_n" in setter
    assert "newly_enabled = enabled_mask & ~previous_mask" in setter
    assert "~newly_enabled" in setter
    assert "__atomic_store_n(&s_realtime_load_quarantined_mask" not in setter


def test_ring_capture_ends_its_bounded_calibration_service_pass() -> None:
    manager = (ROOT / "components" / "calibration_manager" / "src" /
               "calibration_manager.c").read_text(encoding="utf-8")
    service = manager.split("void calibration_manager_service_core1", 1)[1]
    service = service.split("bool calibration_manager_stage_training", 1)[0]
    capture = service.rsplit(
        "calibration_manager_ring_capture_publish(&captured);", 1)[1]
    assert "s_ring_capture_consumed_sequence" in capture
    assert capture.index("return;") < capture.index(
        "calibration_pio_loopback_service_core1")


def test_calibration_core1_never_waits_on_resource_arbiter() -> None:
    manager = (ROOT / "components" / "calibration_manager" / "src" /
               "calibration_manager.c").read_text(encoding="utf-8")
    service = manager.split("void calibration_manager_service_core1", 1)[1]
    service = service.split("bool calibration_manager_stage_training", 1)[0]
    assert "resource_arbiter_" not in service
    assert "__atomic_store_n(&s_training_activity_core1" in service

    core0 = manager.split("void calibration_manager_service(void)", 1)[1]
    core0 = core0.split("bool calibration_manager_start_loopback", 1)[0]
    assert "calibration_manager_sync_training_activity_core0();" in core0


def test_ring_capture_uses_request_scoped_raw_sck_persona() -> None:
    header = (ROOT / "components" / "tdma" / "inc" /
              "tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    transport = (ROOT / "components" / "tdma" / "src" /
                 "tdma_pio_spi_phys_transport.c").read_text(encoding="utf-8")
    begin = phys.split(
        "bool tdma_pio_spi_phys_begin_ring_waveform_capture", 1
    )[1].split(
        "tdma_pio_spi_phys_service_ring_waveform_capture", 1)[0]
    assert "TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_REQUESTED" in begin
    service = phys.split(
        "tdma_pio_spi_phys_service_ring_waveform_capture", 1
    )[1].split("void tdma_pio_spi_phys_flight_origin_recover", 1)[0]
    assert "TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_PATCHED" in service
    assert "pio_encode_wait_gpio(false, phys->rx_csn_pin)" in service
    assert "pio_encode_in(pio_pins, 1u)" in service
    assert "sm_config_set_wrap(&config, offset + 1u, offset + 1u)" in service
    assert "sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX)" in service
    assert "sm_config_set_in_shift(&config, true, true, 32u)" in service

    copy_capture = transport.split(
        "tdma_pio_spi_phys_copy_normal_capture", 1)[1]
    assert "TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY" in copy_capture
    assert "tdma_pio_spi_phys_restore_clock_latch(phys, true)" in copy_capture
    assert "TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_CHUNK_BYTES" in copy_capture
    assert "#define TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_CHUNK_BYTES 4u" in header
    assert "TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING" in copy_capture
    assert "TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_READY" in copy_capture
    assert "adjacent Node's immutable physical RX capture" in copy_capture
    assert "s_tdma_pio_spi_tx_last_frame[index]" not in copy_capture


def test_ring_capture_manager_waits_for_raw_sck_before_copy() -> None:
    manager = (ROOT / "components" / "calibration_manager" / "src" /
               "calibration_manager.c").read_text(encoding="utf-8")
    service = manager.split("void calibration_manager_service_core1", 1)[1]
    service = service.split("bool calibration_manager_stage_training", 1)[0]
    begin = service.index(
        "tdma_runtime_owner_begin_ring_waveform_capture_core1")
    poll = service.index(
        "tdma_runtime_owner_service_ring_waveform_capture_core1")
    copy_capture = service.index(
        "tdma_runtime_owner_copy_normal_capture_core1")
    assert begin < poll < copy_capture
    assert "CALIBRATION_RING_CAPTURE_PENDING" in service
    assert "s_ring_capture_physical_work" in service
    assert "TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING" in service
    pending_branch = service.split(
        "TDMA_PIO_SPI_NORMAL_CAPTURE_COPY_PENDING", 1)[1].split(
        "copied =", 1)[0]
    assert "calibration_manager_ring_capture_publish" not in pending_branch
    assert service.index(
        "captured.physical = s_ring_capture_physical_work") > service.index(
            "copied = copy_result")

    request = manager.split(
        "bool calibration_manager_request_ring_capture", 1
    )[1].split(
        "bool calibration_manager_get_ring_capture_snapshot", 1)[0]
    assert "tdma_runtime_owner_get_ring_snapshot(&ring)" in request
    assert "tdma_runtime_owner_get_calibration_stage" in request
    assert "s_ring_capture_intent.node = ring.local_slot_id" in request
    capture_end = service.index("calibration_pio_loopback_service_core1")
    ring_snapshot = service.index("tdma_runtime_owner_get_ring_snapshot(&ring)")
    assert ring_snapshot < capture_end
    final_capture_return = service.rindex("return;", 0, ring_snapshot)
    assert final_capture_return < ring_snapshot


def test_core1_boots_with_only_vdc_publication_foundation_loads() -> None:
    header = (ROOT / "application" / "inc" / "app.h").read_text(
        encoding="utf-8")
    foundation = header.split(
        "#define APP_REALTIME_LOAD_FOUNDATION_MASK", 1
    )[1].split("#define APP_REALTIME_LOAD_SECONDARY_MASK", 1)[0]
    for load in (
        "APP_REALTIME_LOAD_VDC",
        "APP_REALTIME_LOAD_DPLL",
        "APP_REALTIME_LOAD_SYNC_CAPTURE",
        "APP_REALTIME_LOAD_REFMEM",
        "APP_REALTIME_LOAD_SYNC_TRIGGER",
    ):
        assert load in foundation
    for load in (
        "APP_REALTIME_LOAD_CALIBRATION",
        "APP_REALTIME_LOAD_MODEL",
        "APP_REALTIME_LOAD_TRIGGER_MEASURE",
    ):
        assert load not in foundation

    app = (ROOT / "application" / "src" / "app.c").read_text(
        encoding="utf-8")
    assert "s_realtime_load_enabled_mask =\n    " \
           "APP_REALTIME_LOAD_FOUNDATION_MASK;" in app


def test_core1_static_phase_schedule_fits_with_tdma_and_guard() -> None:
    config = (ROOT / "config" / "project_config.h").read_text(
        encoding="utf-8")
    for symbol in (
        "PROJECT_CORE1_CYCLE_CYCLES",
        "PROJECT_CORE1_PHASE_TDMA_START_CYCLE",
        "PROJECT_CORE1_PHASE_TDMA_END_CYCLE",
        "PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES",
        "PROJECT_CORE1_PHASE_VDC_WCET_CYCLES",
        "PROJECT_CORE1_PHASE_DPLL_WCET_CYCLES",
        "PROJECT_CORE1_PHASE_GUARD_END_CYCLE",
    ):
        assert symbol in config
    runtime = (ROOT / "application" / "src" /
               "app_runtime.c").read_text(encoding="utf-8")
    assert "APP_REALTIME_WIRE_MAX_CYCLES" in runtime
    assert "WCET must fit its own phase" in runtime
    assert "phases must be ordered, disjoint, and fill the cycle" in runtime


def test_process_follower_coalesces_late_overlay_behind_committed_pass() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    prepare = phys.split(
        "bool tdma_pio_spi_phys_prepare_process_overlay", 1
    )[1].split("static void tdma_pio_spi_phys_set_line_drivers", 1)[0]
    committed = prepare.index("phys->flight_overlay_pass_committed")
    dma_busy = prepare.index("dma_channel_is_busy", committed)
    idle_high = prepare.index("gpio_get(phys->rx_csn_pin)", dma_busy)
    coalesced = prepare.index("overlay_late_coalesce_count++", idle_high)
    overlay_build = prepare.index("tdma_flight_overlay_build", coalesced)
    assert committed < dma_busy < idle_high < coalesced < overlay_build
    assert "return true;" in prepare[coalesced:overlay_build]


def test_overlay_script_uses_nonblocking_double_buffer_submission() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    pass_overlay = phys.split(
        "static bool tdma_pio_spi_phys_prepare_pass_overlay", 1
    )[1].split(
        "bool tdma_pio_spi_phys_service_process_overlay_boundary", 1
    )[0]
    assert "tdma_pio_spi_phys_wait_overlay_dma_idle" not in pass_overlay
    pass_buffer = pass_overlay.index(
        "tdma_pio_spi_phys_overlay_free_buffer")
    pass_write = pass_overlay.index("memset(script")
    pass_queue = pass_overlay.index(
        "tdma_pio_spi_phys_queue_overlay_script")
    assert pass_buffer < pass_write < pass_queue

    prepare = phys.split(
        "bool tdma_pio_spi_phys_prepare_process_overlay", 1
    )[1].split("static void tdma_pio_spi_phys_set_line_drivers", 1)[0]
    assert "tdma_pio_spi_phys_wait_overlay_dma_idle" not in prepare
    prepare_buffer = prepare.index(
        "tdma_pio_spi_phys_overlay_free_buffer")
    overlay_build = prepare.index("if (!tdma_flight_overlay_build(")
    prepare_queue = prepare.index(
        "tdma_pio_spi_phys_queue_overlay_script")
    assert prepare_buffer < overlay_build < prepare_queue
    assert "flight_overlay_pending" in prepare


def test_origin_data_waits_csn_once_per_counted_frame() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    program = source.split(
        ".program tdma_pio_spi_flight_origin_data_tx", 1
    )[1].split(".program", 1)[0]
    assert program.index(".wrap_target") < program.index("wait 1 gpio 0")
    assert program.index("wait 1 gpio 0") < program.index("wait 0 gpio 0")
    assert program.index("wait 0 gpio 0") < program.index("mov y, osr")
    assert program.index("mov y, osr") < program.index(
        "flight_origin_data_byte:")
    assert "jmp y-- flight_origin_data_byte" in program


def test_origin_data_rx_consumes_staged_sck_and_data_phase() -> None:
    pio_source = (ROOT / "components" / "tdma" / "src" /
                  "tdma_pio_spi.pio").read_text(encoding="utf-8")
    init = pio_source.split(
        "static inline void tdma_pio_spi_flight_origin_data_tx_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "FLIGHT_ORIGIN_DATA_WAIT_SCK_HIGH_INSTRUCTION = 8u" in init
    assert "FLIGHT_ORIGIN_DATA_PHASE_DELAY_INSTRUCTION = 9u" in init
    assert "pio_encode_wait_gpio(true, rx_sck_pin)" in init
    assert "pio_encode_delay(sck_phase_delay_cycles)" in init
    assert "pio_encode_nop()" in init
    assert "tdma_pio_spi_flight_data_residual_delay_cycles" in init
    assert "pio_encode_delay(data_residual_delay_cycles)" in init


def test_flight_preserves_sck_and_advances_serial_data_one_cycle() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    follower_program = source.split(
        ".program tdma_pio_spi_flight_data_follower", 1
    )[1].split(".program tdma_pio_spi_flight_process_follower", 1)[0]
    assert ".side_set" not in follower_program
    assert "out pins, 1" in follower_program
    assert "jmp x-- flight_follower_bit" in follower_program
    follower = source.split(
        "static inline void tdma_pio_spi_flight_data_follower_program_init", 1
    )[1].split("static inline void", 1)[0]
    process = source.split(
        "static inline void tdma_pio_spi_flight_process_follower_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    control = source.split(
        "static inline void tdma_pio_spi_flight_control_forward_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    origin = source.split(
        "static inline void tdma_pio_spi_flight_origin_data_tx_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    helper = source.split(
        "static inline uint32_t "
        "tdma_pio_spi_flight_data_residual_delay_cycles", 1
    )[1].split("static inline void", 1)[0]
    assert "data_phase_delay_cycles - sck_phase_delay_cycles - 2u" in helper
    assert "pio_encode_delay(data_phase_delay_cycles - 1u)" in follower
    assert "tx_sck_pin" not in follower
    assert "sm_config_set_sideset_pins" not in follower
    assert "pio_encode_delay(data_phase_delay_cycles - 1u)" in process
    assert control.count(
        "pio_encode_delay(sck_phase_delay_cycles)") == 2
    assert "pio_encode_delay(sck_phase_delay_cycles)" in origin
    assert "pio_encode_delay(data_residual_delay_cycles)" in origin

    phys_source = (ROOT / "components" / "tdma" / "src" /
                   "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    configure = phys_source.split(
        "static bool tdma_pio_spi_phys_configure_flight", 1
    )[1].split("static bool", 1)[0]
    origin_call = configure.split(
        "tdma_pio_spi_flight_origin_data_tx_program_init", 1
    )[1].split(");", 1)[0]
    assert "phys->flight_sck_phase_delay_cycles" in origin_call
    assert "phys->flight_data_phase_delay_cycles" in origin_call

    arm = phys_source.split("bool tdma_pio_spi_phys_arm", 1)[1].split(
        "void tdma_pio_spi_phys_disarm", 1)[0]
    assert "TDMA_PIO_SPI_FLIGHT_SCK_REARM_CYCLES" in arm
    assert "half_period_cycles" in arm
    assert "TDMA_PIO_SPI_FLIGHT_DATA_REARM_CYCLES" in arm
    assert "period_cycles" in arm
    assert "phys->flight_physical_byte_count * 8u" in arm
    # RX edge latching is common to reference and follower nodes so the
    # reference loop-return observation does not use extraction-time jitter.
    assert "tdma_pio_spi_phys_clock_latch_read_and_rearm" in phys_source
    assert "/* The latch is the common local-RX edge timestamp" in phys_source


def test_closed_loop_stops_calibration_personas_before_ring_staging() -> None:
    source = (ROOT / "tools" / "calibration_ring_validate" /
              "trn03_closed_loop.py").read_text(encoding="utf-8")
    bias_stop = source.index('"CALibration:BIAS:STOP"')
    loopback_stop = source.index('"CALibration:LOOPback:STOP"', bias_stop)
    ring_stop = source.index('"SYSTem:TDMA:RING:STOP"', loopback_stop)
    topology = source.index('f"SYSTem:TDMA:RING:TOPology', ring_stop)
    assert bias_stop < loopback_stop < ring_stop < topology


def test_origin_queues_physical_byte_count_before_payload_dma_without_waiting() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys_transport.c").read_text(encoding="utf-8")
    function = source.split(
        "static bool tdma_pio_spi_phys_flight_origin_tx", 1
    )[1].split("bool tdma_pio_spi_phys_tx", 1)[0]
    count_put = function.index(
        "pio_sm_put(control_pio, control_sm, clock_bytes - 1u)")
    dma_start = function.index("dma_start_channel_mask", count_put)
    assert count_put < dma_start
    assert "while (" not in function
    assert "busy_wait_us_32" not in function


def test_product_flight_arm_uses_configured_process_image_payload() -> None:
    """The adapter map and PIO burst must share one frozen payload length."""
    runtime = (ROOT / "components" / "tdma" / "src" /
               "tdma_runtime_owner.c").read_text(encoding="utf-8")
    arm = runtime.split(
        "static bool tdma_runtime_owner_flight_phys_arm", 1
    )[1].split("static bool tdma_runtime_owner_flight_phys_timestamp_ready", 1)[0]
    snapshot = arm.index("tdma_flight_engine_get_snapshot")
    payload_set = arm.index("tdma_pio_spi_phys_set_flight_payload_size")
    product_gate = arm.index("stage->enabled != 0u && !have_flight_map")
    phys_arm = arm.index("tdma_pio_spi_phys_arm(context, config)")
    assert snapshot < payload_set < phys_arm
    assert product_gate < phys_arm

    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    setter = phys.split(
        "bool tdma_pio_spi_phys_set_flight_payload_size", 1
    )[1].split("bool tdma_pio_spi_phys_set_flight_offsets", 1)[0]
    assert "phys->armed" in setter
    assert "TDMA_TRANSPORT_SHORT_PAYLOAD_MAX" in setter


def test_flight_origin_control_edges_are_owned_by_one_pio_sm() -> None:
    pio_source = (ROOT / "components" / "tdma" / "src" /
                  "tdma_pio_spi.pio").read_text(encoding="utf-8")
    program = pio_source.split(
        ".program tdma_pio_spi_flight_origin_clock_rx", 1
    )[1].split(".program", 1)[0]
    assert ".side_set" not in program
    assert "pull block" in program
    assert "mov x, osr" in program
    assert "set pins, 0 [1]" in program
    assert "set pins, 1 [2]" in program
    assert "set pins, 2" in program

    init = pio_source.split(
        "static inline void tdma_pio_spi_flight_origin_clock_rx_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "sm_config_set_set_pins(&c, tx_sck_pin, 2u)" in init

    rtt_program = pio_source.split(
        ".program tdma_pio_spi_flight_origin_rtt", 1
    )[1].split(".program", 1)[0]
    assert "wait 1 gpio 0" not in rtt_program
    assert "wait 0 gpio 0" in rtt_program

    latch_init = pio_source.split(
        "static inline void tdma_pio_spi_flight_clock_latch_program_init", 1
    )[1].split("static inline void", 1)[0]
    assert "pio_sm_set_consecutive_pindirs" not in latch_init
    assert "sm_config_set_jmp_pin(&c, csn_pin)" in latch_init
    assert ("pio_sm_set_consecutive_pindirs(pio, sm, tx_sck_pin, 2u, true)"
            in init)

    phys_source = (ROOT / "components" / "tdma" / "src" /
                   "tdma_pio_spi_phys_transport.c").read_text(encoding="utf-8")
    flight_tx = phys_source.split(
        "static bool tdma_pio_spi_phys_flight_origin_tx", 1
    )[1].split("bool tdma_pio_spi_phys_tx", 1)[0]
    assert "gpio_put(phys->tx_csn_pin" not in flight_tx
    assert "pio_sm_put(data_pio, data_sm, clock_bits - 1u)" in flight_tx


def test_shifted_rx_scanner_preserves_shared_raw_boundary_word() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    capture = source.split(
        "bool tdma_pio_spi_phys_capture_words", 1
    )[1].split("bool tdma_pio_spi_phys_arm", 1)[0]
    assert ("s_tdma_pio_spi_rx_scan_produced = candidate + total_words;"
            in capture)
    assert "candidate + total_words + alignment_extra;" not in capture


def test_p3_reference_capture_uses_persona_loaded_program_offset() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    service = source.split(
        "void tdma_pio_spi_phys_cal_loopback_service", 1
    )[1].split("static void tdma_pio_spi_phys_marker_write_begin", 1)[0]
    capture_init = service.split(
        "tdma_pio_spi_cal_loopback_capture_program_init", 1
    )[1].split(");", 1)[0]
    assert "BOARD_TDMA_SPI_CAPTURE_SM" in service
    assert "s_tdma_pio_spi_p3_capture_offset" in capture_init
    assert "s_tdma_pio_spi_cal_capture_offset" not in capture_init
    assert "TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL" in source
    assert "tdma_pio_spi_phys_cal_load_normal_step" in service


def test_calibration_loopback_intent_has_a_dedicated_realtime_beat() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_runtime_owner.c").read_text(encoding="utf-8")
    service = source.split(
        "bool tdma_runtime_owner_cal_loopback_service", 1
    )[1].split("bool tdma_runtime_owner_get_cal_loopback_snapshot", 1)[0]
    consume = service.split(
        "intent.sequence != __atomic_load_n", 1
    )[1].split("tdma_pio_spi_phys_cal_loopback_service", 1)[0]
    assert "return true;" in consume


def test_calibration_loopback_persona_transition_is_split_across_beats() -> None:
    header = (ROOT / "components" / "tdma" / "inc" /
              "tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    start = source.split(
        "bool tdma_pio_spi_phys_cal_loopback_start", 1
    )[1].split("void tdma_pio_spi_phys_cal_loopback_stop", 1)[0]
    service = source.split(
        "void tdma_pio_spi_phys_cal_loopback_service", 1
    )[1].split("static void tdma_pio_spi_phys_marker_write_begin", 1)[0]

    states = [
        "START_UNLOAD",
        "START_LOAD",
        "START_CONFIGURE_TX",
        "START_CONFIGURE_RESPONDER",
        "START_CONFIGURE_CAPTURE",
        "START_CONFIGURE_DMA",
        "START_ARM",
        "CAPTURE_FREEZE",
        "CAPTURE_DECODE",
        "CAPTURE_CLEANUP",
        "CAPTURE_PUBLISH",
        "STOP_FREEZE",
        "STOP_CLEANUP",
        "STOP_UNLOAD",
        "STOP_LOAD",
    ]
    for state in states:
        assert f"TDMA_PIO_SPI_CAL_TRANSITION_{state}" in header
        assert f"TDMA_PIO_SPI_CAL_TRANSITION_{state}" in service or state == "START_UNLOAD"

    assert "tdma_pio_spi_phys_select_program_persona" not in start
    assert "tdma_pio_spi_phys_select_program_persona" not in service
    assert "tdma_pio_spi_phys_unload_programs();" not in service
    assert "tdma_pio_spi_phys_load_programs(" not in service
    assert "tdma_pio_spi_phys_cal_unload_source_step" in service
    assert "tdma_pio_spi_phys_cal_load_p3_step" in service
    assert "tdma_pio_spi_phys_cal_unload_p3_step" in service
    assert "tdma_pio_spi_phys_cal_load_normal_step" in service
    assert "tdma_pio_spi_phys_cal_decode_step" in service
    assert service.count("return;") >= len(states)


def test_p3_responder_returns_and_measures_complete_data_burst() -> None:
    pio_source = (ROOT / "components" / "tdma" / "src" /
                  "tdma_pio_spi.pio").read_text(encoding="utf-8")
    program = pio_source.split(
        ".program tdma_pio_spi_p3_responder", 1
    )[1].split(".program tdma_pio_spi_p3_responder_capture", 1)[0]
    assert "pull block" in program
    assert "mov x, osr" in program
    assert "p3_data_loop:" in program
    assert "jmp x-- p3_data_loop" in program

    phys_source = (ROOT / "components" / "tdma" / "src" /
                   "tdma_pio_spi_phys_p3.c").read_text(encoding="utf-8")
    start = phys_source.split(
        "bool tdma_pio_spi_phys_p3_start", 1
    )[1].split("void tdma_pio_spi_phys_p3_stop", 1)[0]
    assert "request->pulse_count - 1u" in start
    p3_decode_source = (ROOT / "components" / "tdma" / "src" /
                        "tdma_pio_spi_phys_p3_decode.c").read_text(
                            encoding="utf-8")
    decode = p3_decode_source
    assert "data_high_sum += timestamp - data_rise" in decode
    assert "phys->p3.data_pulse_count = data_high_count" in decode
    assert "data_high_sum + data_high_count / 2u" in decode


def test_process_rx_reconstructs_absolute_fixed_frame_sequence() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys_rx_ring.c").read_text(encoding="utf-8")
    produced = source.split(
        "uint64_t tdma_pio_spi_phys_rx_produced_words", 1
    )[1].split("uint32_t tdma_pio_spi_phys_rx_ring_word", 1)[0]
    assert "snapshot.overlay_frame_boundary_count" in produced
    assert "snapshot.tx_count" in produced
    assert "tdma_rx_sequence_observe" in produced
    assert "phys->flight_physical_byte_count : 0u" in produced
    assert "sequence_floor" not in produced
    assert "transfer_count" not in produced


def test_process_rx_only_restores_a_proven_complete_ring() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_rx_sequence.c").read_text(encoding="utf-8")
    assert "modulo_delta" in source
    assert "complete_rings" in source
    assert "error <= fixed_frame_words" in source
    assert "tracker->produced_words += observed_delta" in source


def test_arm_with_evidence_exposes_rejection_stage(monkeypatch) -> None:
    responses = {
        "SYSTem:TDMA:RING:ARM": "<timeout>",
        "SYSTem:TDMA:RING:ARM:STATus?": "7",
        "SYSTem:ERR?": '-200,"Execution error"',
    }
    monkeypatch.setattr(trn03, "drain_errors", lambda board, args: [
        '0,"No error"'])
    monkeypatch.setattr(
        trn03, "board_command",
        lambda board, command, args: responses[command])
    with pytest.raises(RuntimeError, match="arm_result=7"):
        arm_with_evidence(FakeBoard(), object())


def runtime() -> dict[str, int]:
    return {
        "ring_enabled": 1,
        "ring_node_count": 4,
        "ring_local_node": 2,
        "ring_reference_node": 0,
        "ring_up_running": 1,
        "ring_down_running": 1,
        "ring_seq": 10,
        "ring_last_error": 0,
        "ring_adapter_started": 1,
        "ring_adapter_service_count": 100,
        "ring_up_tx_sequence": 20,
        "ring_down_rx_sequence": 20,
        "ring_up_tx_frame_crc32": 0x1234,
        "ring_down_rx_frame_crc32": 0x1234,
        "ring_idle_beacon_tx_count": 10,
        "ring_idle_beacon_rx_count": 10,
        "ring_adapter_last_error": 0,
        "ring_adapter_tx_count": 20,
        "ring_adapter_rx_count": 20,
        "ring_adapter_rx_bad_count": 0,
        "ring_adapter_rx_transport_bad_count": 0,
        "ring_adapter_rx_schedule_bad_count": 0,
        "ring_adapter_rx_profile_bad_count": 0,
        "ring_adapter_last_bad_transport_result": 0,
        "ring_adapter_last_bad_sequence": 0,
        "ring_adapter_last_bad_schedule_crc32": 0,
        "ring_adapter_last_bad_profile_crc32": 0,
        "ring_adapter_last_bad_header_diff_count": 0,
        "ring_adapter_last_bad_header_first_diff_offset": 0xFFFFFFFF,
        "ring_adapter_last_bad_header_expected_byte": 0,
        "ring_adapter_last_bad_header_observed_byte": 0,
        "ring_config_seq": 7,
        "ring_applied_config_seq": 7,
        "node_index": 2,
    }


def flight() -> dict:
    return {
        "process": {
            "configured": 1, "active": 1, "local_node": 2,
            "map_apply_count": 5, "input_bytes": 100,
            "output_bytes": 100, "map_reject_count": 0,
            "length_reject_count": 0, "tx_unavailable_count": 0,
            "rx_bitmap_scan_count": 5, "rx_bitmap_hit_count": 5,
            "rx_bitmap_duplicate_count": 0,
            "rx_bitmap_present_count": 5,
            "rx_bitmap_incomplete_count": 0,
            "map_generation": 1, "payload_size": 256,
            "receive_version": 1, "receive_configured": 1,
            "receive_state": 1, "receive_last_reason": 0,
            "receive_last_transport_result": 0,
            "receive_quality_flags": 0x8000003f,
            "receive_accepted_count": 5,
            "receive_rejected_count": 0, "receive_missing_count": 0,
            "receive_consecutive_failure_count": 0,
            "receive_image_generation": 5,
            "receive_accepted_sequence": 5,
            "receive_accepted_identity_crc32": 1,
            "receive_accepted_schedule_crc32": 1,
            "receive_accepted_profile_crc32": 1,
            "receive_accepted_map_generation": 1,
            "receive_accepted_segment_mask": 1,
            "receive_expected_segment_mask": 1,
            "receive_accepted_wkc": 1, "receive_expected_wkc": 1,
            "receive_accepted_payload_size": 256,
            "receive_last_accept_timestamp_ns": 1,
            "receive_last_observation_timestamp_ns": 1,
            "receive_stale_age_ns": 0,
        },
        "fifo": {
            "tx_publish_count": 1, "tx_publish_reject_count": 0,
            "tx_acquire_count": 5, "tx_image_stale_count": 0,
            "tx_reuse_count": 4, "tx_release_count": 5,
            "tx_ready_count": 0, "tx_active_buffer": 0,
            "tx_active_generation": 1,
            "rx_publish_count": 5, "rx_mirror_drop_count": 0,
            "rx_publish_drop_count": 0, "rx_acquire_count": 5,
            "rx_release_count": 5, "rx_queued_count": 0,
            "rx_parse_count": 0,
        },
    }


def increment(value: dict, amount: int = 1) -> dict:
    return {key: item + amount for key, item in value.items()}


def soak_config(node_count: int = 2) -> dict:
    return {
        "node_count": node_count,
        "links": [{
            "marker_destination_node": (link + 1) % node_count,
            "data_destination_node": link,
            "marker_offset_sample_count": 0,
            "sck_offset_sample_count": 0,
            "data_offset_sample_count": 0,
            "marker_phase_delay_cycles": 10,
            "sck_phase_delay_cycles": 10,
            "data_phase_delay_cycles": 10,
        } for link in range(node_count)],
    }


def soak_snapshot(node_index: int, step: int) -> dict:
    current_runtime = runtime()
    current_runtime.update({
        "ring_node_count": 2,
        "ring_local_node": node_index,
        "node_index": node_index,
        "ring_seq": 10 + step,
        "ring_adapter_service_count": 100 + step,
        "ring_up_tx_sequence": 20 + step,
        "ring_down_rx_sequence": 20 + step,
        "ring_adapter_tx_count": 20 + step,
        "ring_adapter_rx_count": 20 + step,
        "ring_last_error": 0,
    })
    current_flight = copy.deepcopy(flight())
    current_flight["process"]["local_node"] = node_index
    for field in ("map_apply_count", "input_bytes", "output_bytes",
                  "rx_bitmap_scan_count", "rx_bitmap_hit_count",
                  "rx_bitmap_present_count", "receive_accepted_count"):
        current_flight["process"][field] += step
    for field in ("tx_acquire_count", "tx_reuse_count",
                  "rx_publish_count", "rx_acquire_count",
                  "rx_release_count"):
        current_flight["fifo"][field] += step
    physical = {field: 0 for field in trn03.PHYS_FIELDS}
    physical.update({
        "program_persona": 11 if node_index == 0 else 13,
        "overlay_prepare_count": step + 1,
        "overlay_replacement_byte_count": step + 1,
        "flight_marker_offset_sample_count": 0,
        "flight_sck_offset_sample_count": 0,
        "flight_data_offset_sample_count": 0,
        "flight_marker_phase_delay_cycles": 10,
        "flight_sck_phase_delay_cycles": 10,
        "flight_data_phase_delay_cycles": 10,
    })
    return {
        "runtime": current_runtime,
        "flight": current_flight,
        "physical": physical,
    }


def test_u32_delta_wraps() -> None:
    assert u32_delta(0xFFFFFFFE, 1) == 3
    assert counter_deltas({"x": 8}, {"x": 11}, ("x",)) == {"x": 3}


def test_startup_barrier_requires_complete_advancing_process_image() -> None:
    previous = soak_snapshot(0, 0)
    current = soak_snapshot(0, 1)
    current["flight"]["process"]["receive_accepted_sequence"] += 1
    assert startup_barrier_interval_errors(
        previous, current, node_index=0, node_count=2,
        require_process_image=True) == []


def test_startup_barrier_resets_on_pipeline_reject_growth() -> None:
    previous = soak_snapshot(0, 0)
    current = soak_snapshot(0, 1)
    current["flight"]["process"]["receive_accepted_sequence"] += 1
    current["flight"]["process"]["rx_bitmap_incomplete_count"] += 1
    errors = startup_barrier_interval_errors(
        previous, current, node_index=0, node_count=2,
        require_process_image=True)
    assert "rx_bitmap_incomplete_count_grew" in errors


def test_closed_loop_uses_explicit_startup_barrier_not_fixed_sleep() -> None:
    source = (ROOT / "tools" / "calibration_ring_validate" /
              "trn03_closed_loop.py").read_text(encoding="utf-8")
    main = source.split("def main() -> int:", 1)[1]
    assert "wait_startup_barrier(" in main
    assert "time.sleep(args.start_wait)" not in main


def test_parse_snapshot_requires_exact_field_count() -> None:
    assert parse_snapshot("1,2", ("a", "b"), "x") == {"a": 1, "b": 2}


def test_physical_snapshot_has_one_canonical_complete_phase_schema() -> None:
    phase_fields = (
        "flight_marker_offset_sample_count",
        "flight_sck_offset_sample_count",
        "flight_data_offset_sample_count",
        "flight_marker_phase_delay_cycles",
        "flight_sck_phase_delay_cycles",
        "flight_data_phase_delay_cycles",
    )
    start = trn03.PHYS_FIELDS.index(phase_fields[0])
    assert trn03.PHYS_FIELDS[start:start + len(phase_fields)] == phase_fields
    source = (ROOT / "middleware" / "scpi_port" / "src" /
              "scpi_sync_commands.c").read_text(encoding="utf-8")
    query = source.split("scpi_cmd_sync_vdc_tdma_phys_q", 1)[1].split(
        "scpi_cmd_sync_vdc_path_delay_q", 1)[0]
    positions = [query.index(f"snapshot.{field}") for field in phase_fields]
    assert positions == sorted(positions)


def test_expected_flight_phase_uses_incoming_control_and_reverse_data_links(
        ) -> None:
    config = {
        "node_count": 4,
        "links": [{
            "marker_destination_node": (link + 1) % 4,
            "data_destination_node": link,
            "marker_offset_sample_count": 10 + link,
            "sck_offset_sample_count": 20 + link,
            "data_offset_sample_count": 30 + link,
            "marker_phase_delay_cycles": 40 + link,
            "sck_phase_delay_cycles": 50 + link,
            "data_phase_delay_cycles": 60 + link,
        } for link in range(4)],
    }
    assert expected_flight_phase(config, 0) == {
        "flight_marker_offset_sample_count": 13,
        "flight_sck_offset_sample_count": 23,
        "flight_data_offset_sample_count": 30,
        "flight_marker_phase_delay_cycles": 43,
        "flight_sck_phase_delay_cycles": 53,
        "flight_data_phase_delay_cycles": 60,
    }
    assert expected_flight_phase(config, 2) == {
        "flight_marker_offset_sample_count": 11,
        "flight_sck_offset_sample_count": 21,
        "flight_data_offset_sample_count": 32,
        "flight_marker_phase_delay_cycles": 41,
        "flight_sck_phase_delay_cycles": 51,
        "flight_data_phase_delay_cycles": 62,
    }


def test_expected_flight_phase_follows_frozen_eight_node_topology() -> None:
    marker_destinations = [5, 6, 7, 4, 0, 2, 3, 1]
    config = {
        "node_count": 8,
        "links": [{
            "marker_destination_node": destination,
            "data_destination_node": source,
            "marker_offset_sample_count": 10 + source,
            "sck_offset_sample_count": 20 + source,
            "data_offset_sample_count": 30 + source,
            "marker_phase_delay_cycles": 40 + source,
            "sck_phase_delay_cycles": 50 + source,
            "data_phase_delay_cycles": 60 + source,
        } for source, destination in enumerate(marker_destinations)],
    }
    assert expected_flight_phase(config, 1) == {
        "flight_marker_offset_sample_count": 17,
        "flight_sck_offset_sample_count": 27,
        "flight_data_offset_sample_count": 31,
        "flight_marker_phase_delay_cycles": 47,
        "flight_sck_phase_delay_cycles": 57,
        "flight_data_phase_delay_cycles": 61,
    }


def test_expected_flight_phase_rejects_missing_node_endpoint() -> None:
    config = {
        "node_count": 2,
        "links": [{
            "marker_destination_node": 0,
            "data_destination_node": 0,
        }, {
            "marker_destination_node": 0,
            "data_destination_node": 1,
        }],
    }
    with pytest.raises(ValueError, match="exactly one MARK and DATA input"):
        expected_flight_phase(config, 0)


@pytest.mark.parametrize("field", (
    "flight_marker_offset_sample_count",
    "flight_sck_offset_sample_count",
    "flight_data_offset_sample_count",
    "flight_marker_phase_delay_cycles",
    "flight_sck_phase_delay_cycles",
    "flight_data_phase_delay_cycles",
))
def test_every_loaded_offset_and_phase_mismatch_fails_closed(field: str) -> None:
    expected = {name: index for index, name in enumerate(
        trn03.PHYS_FIELDS) if name.startswith("flight_") and
        ("offset_sample_count" in name or "phase_delay_cycles" in name)}
    physical = dict(expected)
    physical[field] += 1
    assert validate_flight_phase_readback(physical, expected) == [
        field.removeprefix("flight_") + "_mismatch"]


def test_runtime_status_exposes_bad_frame_classification() -> None:
    expected = (
        "ring_adapter_rx_transport_bad_count",
        "ring_adapter_rx_schedule_bad_count",
        "ring_adapter_rx_profile_bad_count",
        "ring_adapter_last_bad_transport_result",
        "ring_adapter_last_bad_sequence",
        "ring_adapter_last_bad_schedule_crc32",
        "ring_adapter_last_bad_profile_crc32",
        "ring_adapter_last_bad_header_diff_count",
        "ring_adapter_last_bad_header_first_diff_offset",
        "ring_adapter_last_bad_header_expected_byte",
        "ring_adapter_last_bad_header_observed_byte",
        "ring_config_seq",
        "ring_applied_config_seq",
    )
    assert trn03.RUNTIME_FIELDS[-len(expected):] == expected
    source = (ROOT / "middleware" / "scpi_port" / "src" /
              "scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    status = source.split(
        "scpi_cmd_system_tdma_ring_status_q", 1
    )[1].split("scpi_cmd_system_tdma_ring_train", 1)[0]
    for field in expected:
        assert f"snapshot.{field.removeprefix('ring_')}" in status


def test_parse_active_profile_accepts_extended_status() -> None:
    profile = parse_active_profile(
        "7,10000000,1000000,4096,3,1234,99,98,97,96", "x")
    assert profile["level"] == 7
    assert profile["profile_crc32"] == 1234


def test_profile_override_cannot_detach_frozen_matrix_identity() -> None:
    assert resolve_profile_level(7, None) == 7
    assert resolve_profile_level(7, 7) == 7
    with pytest.raises(ValueError, match="conflicts with frozen config"):
        resolve_profile_level(7, 9)


def test_validate_node_accepts_growing_runtime_and_fifo() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count",
                  "ring_adapter_rx_transport_bad_count",
                  "ring_adapter_rx_schedule_bad_count",
                  "ring_adapter_rx_profile_bad_count",
                  "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {
        "process": increment(before_flight["process"], 3),
        "fifo": increment(before_flight["fifo"], 3),
    }
    for field in ("configured", "active", "local_node", "map_generation",
                  "payload_size",
                  "map_reject_count", "length_reject_count",
                  "rx_bitmap_incomplete_count", "receive_version",
                  "receive_configured", "receive_state",
                  "receive_last_reason", "receive_last_transport_result",
                  "receive_quality_flags", "receive_rejected_count",
                  "receive_missing_count",
                  "receive_consecutive_failure_count",
                  "receive_accepted_map_generation",
                  "receive_accepted_segment_mask",
                  "receive_expected_segment_mask",
                  "receive_accepted_wkc", "receive_expected_wkc",
                  "receive_accepted_payload_size"):
        after_flight["process"][field] = before_flight["process"][field]
    for field in ("tx_publish_reject_count", "rx_mirror_drop_count",
                  "rx_publish_drop_count"):
        after_flight["fifo"][field] = before_flight["fifo"][field]
    # Publication is proven in the pre-ARM seed phase. Release only occurs
    # when a newer TX image replaces ACTIVE, while rx_parse_count is a gauge.
    for field in ("tx_publish_count", "tx_release_count", "rx_parse_count"):
        after_flight["fifo"][field] = before_flight["fifo"][field]
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight,
        [], {"tx_publish_count": 1, "tx_publish_reject_count": 0})
    assert errors == []


def test_validate_tx_seed_uses_pre_arm_publish_delta() -> None:
    before = flight()
    after = {group: dict(values) for group, values in before.items()}
    after["fifo"]["tx_publish_count"] += 1
    errors, deltas = validate_tx_seed(before, after)
    assert errors == []
    assert deltas == {
        "tx_publish_count": 1,
        "tx_publish_reject_count": 0,
    }


def test_validate_tx_seed_reports_rejected_or_missing_publish() -> None:
    before = flight()
    after = {group: dict(values) for group, values in before.items()}
    after["fifo"]["tx_publish_reject_count"] += 1
    errors, _ = validate_tx_seed(before, after)
    assert errors == ["fifo_tx_not_published", "fifo_tx_seed_rejected"]


def test_validate_fifo_reset_requires_all_session_owners_reclaimed() -> None:
    clean = flight()
    clean["fifo"].update({
        "tx_ready_count": 0,
        "tx_active_buffer": 0xFFFFFFFF,
        "tx_active_generation": 0,
        "rx_queued_count": 0,
        "rx_parse_count": 0,
    })
    assert validate_fifo_reset(clean) == []

    dirty = {group: dict(values) for group, values in clean.items()}
    dirty["fifo"].update({
        "tx_ready_count": 1,
        "tx_active_buffer": 0,
        "tx_active_generation": 9,
        "rx_queued_count": 2,
        "rx_parse_count": 1,
    })
    assert validate_fifo_reset(dirty) == [
        "fifo_reset_tx_ready",
        "fifo_reset_tx_active",
        "fifo_reset_tx_generation",
        "fifo_reset_rx_queued",
        "fifo_reset_rx_parse",
    ]


def test_validate_node_reports_stalled_receive_path() -> None:
    before_runtime = runtime()
    after_runtime = dict(before_runtime)
    after_runtime["ring_down_running"] = 0
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight)
    assert "down_not_running" in errors
    assert "adapter_rx_not_growing" in errors
    assert "fifo_rx_not_published" in errors


def test_validate_raw_flight_accepts_no_fifo_exchange() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count",
                  "ring_adapter_rx_transport_bad_count",
                  "ring_adapter_rx_schedule_bad_count",
                  "ring_adapter_rx_profile_bad_count",
                  "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight,
        require_process_image=False, physical_after={"program_persona": 12})
    assert errors == []


def test_reference_accepts_one_in_flight_sequence_without_false_crc_mismatch(
        ) -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count",
                  "ring_adapter_rx_transport_bad_count",
                  "ring_adapter_rx_schedule_bad_count",
                  "ring_adapter_rx_profile_bad_count",
                  "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_local_node"] = 0
    after_runtime["ring_up_tx_sequence"] = 105
    after_runtime["ring_down_rx_sequence"] = 104
    after_runtime["ring_up_tx_frame_crc32"] = 0x1234
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    errors, deltas = validate_node(
        0, 4, before_runtime, after_runtime, before_flight, after_flight,
        require_process_image=False, physical_after={"program_persona": 11})
    assert "crc_mismatch" not in errors
    assert "feedback_sequence_out_of_window" not in errors
    assert deltas["feedback_identity"] == {
        "tx_sequence": 105,
        "rx_sequence": 104,
        "sequence_gap": 1,
        "crc_comparable": False,
        "crc_match": True,
    }


def test_validate_raw_flight_ignores_process_image_backpressure() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count",
                  "ring_adapter_rx_transport_bad_count",
                  "ring_adapter_rx_schedule_bad_count",
                  "ring_adapter_rx_profile_bad_count",
                  "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    after_flight["fifo"]["tx_publish_reject_count"] += 100
    after_flight["fifo"]["rx_mirror_drop_count"] += 10
    after_flight["process"]["map_reject_count"] += 5
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight,
        require_process_image=False, physical_after={"program_persona": 12})
    assert errors == []


def test_validate_raw_flight_requires_role_persona() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count",
                  "ring_adapter_rx_transport_bad_count",
                  "ring_adapter_rx_schedule_bad_count",
                  "ring_adapter_rx_profile_bad_count",
                  "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {group: dict(values)
                    for group, values in before_flight.items()}
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight,
        require_process_image=False, physical_after={"program_persona": 1})
    assert "physical_flight_persona_mismatch" in errors


def test_validate_node_requires_rx_acquire_and_release() -> None:
    before_runtime = runtime()
    after_runtime = increment(before_runtime, 4)
    for field in ("ring_enabled", "ring_node_count", "ring_local_node",
                  "ring_reference_node", "ring_up_running",
                  "ring_down_running", "ring_adapter_started",
                  "ring_adapter_rx_bad_count",
                  "ring_adapter_rx_transport_bad_count",
                  "ring_adapter_rx_schedule_bad_count",
                  "ring_adapter_rx_profile_bad_count",
                  "ring_adapter_last_error"):
        after_runtime[field] = before_runtime[field]
    after_runtime["ring_up_tx_frame_crc32"] = 0x5678
    after_runtime["ring_down_rx_frame_crc32"] = 0x5678
    before_flight = flight()
    after_flight = {
        "process": increment(before_flight["process"], 3),
        "fifo": increment(before_flight["fifo"], 3),
    }
    for field in ("configured", "active", "local_node",
                  "map_reject_count", "length_reject_count"):
        after_flight["process"][field] = before_flight["process"][field]
    for field in ("tx_publish_reject_count", "rx_mirror_drop_count",
                  "rx_publish_drop_count", "rx_acquire_count",
                  "rx_release_count"):
        after_flight["fifo"][field] = before_flight["fifo"][field]
    errors, _ = validate_node(
        2, 4, before_runtime, after_runtime, before_flight, after_flight)
    assert "fifo_rx_not_acquired" in errors
    assert "fifo_rx_not_released" in errors


def test_soak_timeline_gates_every_interval() -> None:
    board_ids = ["node0", "node1"]
    timeline = [{
        "sample_index": step,
        "elapsed_s": float(step),
        "errors": {},
        "nodes": {
            address: soak_snapshot(node_index, step)
            for node_index, address in enumerate(board_ids)
        },
    } for step in range(3)]
    result = validate_soak_timeline(
        timeline, board_ids, soak_config(), require_process_image=True)
    assert result["passed"] is True
    assert result["timeline_sample_count"] == 3
    for node in result["nodes"].values():
        assert node["interval_count"] == 2
        assert node["unhealthy_sample_count"] == 0
        assert node["observation_failure_count"] == 0
        assert node["runtime_unhealthy_sample_count"] == 0
        assert node["down_event_count"] == 0
        assert node["recovery_count"] == 0
        quality = node["receive_quality"]
        assert quality["rx_good_frame_count"] == 2
        assert quality["rx_bad_frame_count"] == 0
        assert quality["rx_total_frame_count"] == 2
        assert quality["observed_frame_error_rate"] == 0.0
        assert quality["zero_error_95_upper_bound_frame_error_rate"] == 1.0
    assert result["worst_receive_quality"]["board_id"] == "node0"


def test_soak_timeline_retains_sticky_start_reason_without_false_down() -> None:
    board_ids = ["node0", "node1"]
    timeline = [{
        "sample_index": step,
        "elapsed_s": float(step),
        "errors": {},
        "nodes": {
            address: soak_snapshot(node_index, step)
            for node_index, address in enumerate(board_ids)
        },
    } for step in range(2)]
    timeline[0]["nodes"]["node0"]["runtime"]["ring_last_error"] = 2
    timeline[1]["nodes"]["node0"]["runtime"]["ring_last_error"] = 2
    timeline[0]["nodes"]["node1"]["runtime"]["ring_last_error"] = 5
    timeline[1]["nodes"]["node1"]["runtime"]["ring_last_error"] = 5
    result = validate_soak_timeline(
        timeline, board_ids, soak_config(), require_process_image=True)
    assert result["passed"] is True


def test_soak_timeline_retains_down_and_recovery() -> None:
    board_ids = ["node0", "node1"]
    timeline = [{
        "sample_index": step,
        "elapsed_s": float(step),
        "errors": {},
        "nodes": {
            address: soak_snapshot(node_index, step)
            for node_index, address in enumerate(board_ids)
        },
    } for step in range(3)]
    timeline[1]["nodes"]["node1"]["runtime"]["ring_down_running"] = 0
    result = validate_soak_timeline(
        timeline, board_ids, soak_config(), require_process_image=True)
    node = result["nodes"]["node1"]
    assert result["passed"] is False
    assert node["unhealthy_sample_count"] == 1
    assert node["observation_failure_count"] == 0
    assert node["runtime_unhealthy_sample_count"] == 1
    assert node["down_event_count"] == 1
    assert node["recovery_count"] == 1
    assert node["errors"] == [
        "unhealthy_periodic_sample",
        "periodic_interval_gate_failed",
        "runtime_down_event",
        "runtime_recovery_observed",
    ]


def test_soak_timeline_reports_worst_node_frame_error_rate() -> None:
    board_ids = ["node0", "node1"]
    timeline = [{
        "sample_index": step,
        "elapsed_s": float(step),
        "errors": {},
        "nodes": {
            address: soak_snapshot(node_index, step)
            for node_index, address in enumerate(board_ids)
        },
    } for step in range(3)]
    timeline[1]["nodes"]["node1"]["runtime"][
        "ring_adapter_rx_bad_count"] += 1
    timeline[1]["nodes"]["node1"]["runtime"][
        "ring_adapter_rx_transport_bad_count"] += 1
    timeline[2]["nodes"]["node1"]["runtime"][
        "ring_adapter_rx_bad_count"] += 1
    timeline[2]["nodes"]["node1"]["runtime"][
        "ring_adapter_rx_transport_bad_count"] += 1
    result = validate_soak_timeline(
        timeline, board_ids, soak_config(), require_process_image=True)
    quality = result["nodes"]["node1"]["receive_quality"]
    assert quality["rx_good_frame_count"] == 2
    assert quality["rx_bad_frame_count"] == 1
    assert quality["rx_total_frame_count"] == 3
    assert quality["observed_frame_error_ppm"] == pytest.approx(
        1_000_000 / 3)
    assert quality["mean_frames_per_error"] == 3
    assert result["worst_receive_quality"]["board_id"] == "node1"


def test_soak_timeline_retains_per_node_sampling_failure() -> None:
    board_ids = ["node0", "node1"]
    timeline = [{
        "sample_index": step,
        "elapsed_s": float(step),
        "errors": {},
        "nodes": {
            address: soak_snapshot(node_index, step)
            for node_index, address in enumerate(board_ids)
        },
    } for step in range(3)]
    del timeline[1]["nodes"]["node0"]
    timeline[1]["errors"]["node0"] = "TimeoutError: query timeout"
    result = validate_soak_timeline(
        timeline, board_ids, soak_config(), require_process_image=True)
    node = result["nodes"]["node0"]
    assert result["passed"] is False
    assert node["observation_failure_count"] == 1
    assert node["runtime_unhealthy_sample_count"] == 0
    assert node["down_event_count"] == 0
    assert node["recovery_count"] == 0
    assert node["errors"] == [
        "periodic_observation_missing",
        "periodic_interval_gate_failed",
    ]
    assert node["samples"][1]["errors"] == [
        "sample_transport:TimeoutError: query timeout"]
    assert [interval["errors"] for interval in node["intervals"]] == [
        ["sample_missing"], ["sample_missing"]]
