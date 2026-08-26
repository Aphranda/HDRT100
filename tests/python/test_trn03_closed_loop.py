from __future__ import annotations

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
    counter_deltas,
    expected_flight_phase,
    parse_active_profile,
    parse_snapshot,
    u32_delta,
    validate_flight_phase_readback,
    validate_node,
    validate_fifo_reset,
    validate_tx_seed,
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


def test_raw_flight_mode_query_accepts_scalar_one() -> None:
    assert scpi_response_matches_command(
        "SYSTem:TDMA:FLIGHT:MODE?", "1")


def test_follower_wait_patch_preserves_sck_sideset() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    init = source.split(
        "static inline void tdma_pio_spi_flight_follower_program_init", 1
    )[1].split("static inline void", 1)[0]
    assert "pio_encode_wait_gpio(true, rx_sck_pin) |" in init
    assert "pio_encode_sideset(1u, 0u)" in init
    assert "pio_encode_wait_gpio(false, rx_sck_pin) |" in init
    assert "pio_encode_sideset(1u, 1u)" in init
    assert "pio->instr_mem[offset + 7u]" in init


def test_follower_samples_data_on_rising_edge_before_falling_edge() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    program = source.split(
        ".program tdma_pio_spi_flight_follower", 1
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
    assert "data_sample_delay_cycles" in init
    assert "sck_phase_delay_cycles + data_residual_delay_cycles + 1u" in init
    assert "instr_mem[offset + 13u]" in init
    assert "pio_encode_wait_gpio(false, rx_sck_pin)" in init
    bit_loop = program.split("flight_process_bit:", 1)[1].split(
        "mov y, isr", 1)[0]
    assert "wait 0 gpio 1" in bit_loop
    assert "out pins, 1" in bit_loop
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
    assert "pull block" in control_program
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

    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    assert "control_bits - 1u" in phys
    assert "phys->tx_sm,\n                            control_bits - 1u" in phys


def test_raw_sck_capture_starts_at_first_sck_edge() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi.pio").read_text(encoding="utf-8")
    program = source.split(
        ".program tdma_pio_spi_flight_sck_capture", 1
    )[1].split(".program", 1)[0]
    assert "wait 0 gpio 0" in program
    assert "wait 1 gpio 0" in program
    assert program.index("wait 0 gpio 0") < program.index("wait 1 gpio 0")
    assert "in pins, 1" in program

    capture_init = source.split(
        "static inline void tdma_pio_spi_flight_sck_capture_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "pio_encode_wait_gpio(false, rx_sck_pin)" in capture_init
    assert "pio_encode_wait_gpio(true, rx_sck_pin)" in capture_init
    assert "rx_csn_pin" not in capture_init
    assert "PIO_FIFO_JOIN_RX" in capture_init


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


def test_process_follower_recovers_pass_script_after_bad_frame() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    service = phys.split(
        "bool tdma_pio_spi_phys_service_process_overlay_boundary", 1
    )[1].split("bool tdma_pio_spi_phys_set_process_image_mode", 1)[0]
    assert "pio_interrupt_get(BOARD_TDMA_SPI_PIO, 3u)" in service
    assert "pio_interrupt_clear(BOARD_TDMA_SPI_PIO, 3u)" in service
    assert "phys->flight_overlay_next_prepared" in service
    assert "tdma_pio_spi_phys_prepare_pass_overlay(phys)" in service
    assert "overlay_pass_recovery_count++" in service
    assert "flight_overlay_pass_committed = true" in service

    adapter = (ROOT / "components" / "tdma" / "src" /
               "tdma_pio_spi_ring_adapter.c").read_text(encoding="utf-8")
    assert "phys_service_overlay_boundary" in adapter


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


def test_overlay_script_waits_for_dma_before_mutating_shared_buffer() -> None:
    phys = (ROOT / "components" / "tdma" / "src" /
            "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    pass_overlay = phys.split(
        "static bool tdma_pio_spi_phys_prepare_pass_overlay", 1
    )[1].split(
        "bool tdma_pio_spi_phys_service_process_overlay_boundary", 1
    )[0]
    pass_wait = pass_overlay.index(
        "tdma_pio_spi_phys_wait_overlay_dma_idle")
    pass_write = pass_overlay.index(
        "memset(s_tdma_pio_spi_flight_overlay_script")
    assert pass_wait < pass_write

    prepare = phys.split(
        "bool tdma_pio_spi_phys_prepare_process_overlay", 1
    )[1].split("static void tdma_pio_spi_phys_set_line_drivers", 1)[0]
    prepare_wait = prepare.index(
        "if (!tdma_pio_spi_phys_wait_overlay_dma_idle")
    overlay_build = prepare.index("if (!tdma_flight_overlay_build(")
    assert prepare_wait < overlay_build
    assert "Waiting after\n     * tdma_flight_overlay_build() is too late" in prepare


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

    setter = phys_source.split(
        "bool tdma_pio_spi_phys_set_flight_offsets", 1
    )[1].split("bool tdma_pio_spi_phys_prepare_process_overlay", 1)[0]
    assert "data_phase_delay_cycles <= sck_phase_delay_cycles" in setter
    arm = phys_source.split("bool tdma_pio_spi_phys_arm", 1)[1].split(
        "void tdma_pio_spi_phys_disarm", 1)[0]
    assert "TDMA_PIO_SPI_FLIGHT_SCK_REARM_CYCLES" in arm
    assert "half_period_cycles" in arm
    assert "TDMA_PIO_SPI_FLIGHT_DATA_REARM_CYCLES" in arm
    assert "period_cycles" in arm


def test_closed_loop_stops_calibration_personas_before_ring_staging() -> None:
    source = (ROOT / "tools" / "calibration_ring_validate" /
              "trn03_closed_loop.py").read_text(encoding="utf-8")
    bias_stop = source.index('"CALibration:BIAS:STOP"')
    loopback_stop = source.index('"CALibration:LOOPback:STOP"', bias_stop)
    ring_stop = source.index('"SYSTem:TDMA:RING:STOP"', loopback_stop)
    topology = source.index('f"SYSTem:TDMA:RING:TOPology', ring_stop)
    assert bias_stop < loopback_stop < ring_stop < topology


def test_origin_queues_frame_byte_count_before_payload_dma() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    function = source.split(
        "static bool tdma_pio_spi_phys_flight_origin_tx", 1
    )[1].split("bool tdma_pio_spi_phys_tx", 1)[0]
    count_put = function.index(
        "pio_sm_put_blocking(BOARD_TDMA_SPI_PIO, phys->tx_sm, wire_bytes - 1u)")
    dma_start = function.index("dma_start_channel_mask", count_put)
    assert count_put < dma_start


def test_shifted_rx_scanner_preserves_shared_raw_boundary_word() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    capture = source.split(
        "static bool tdma_pio_spi_phys_capture_words", 1
    )[1].split("bool tdma_pio_spi_phys_arm", 1)[0]
    assert ("s_tdma_pio_spi_rx_scan_produced = candidate + total_words;"
            in capture)
    assert "candidate + total_words + alignment_extra;" not in capture


def test_p3_reference_capture_uses_persona_loaded_program_offset() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    service = source.split(
        "void tdma_pio_spi_phys_cal_loopback_service", 1
    )[1].split("static uint32_t tdma_pio_spi_cal_sample_byte", 1)[0]
    capture_init = service.split(
        "tdma_pio_spi_cal_loopback_capture_program_init", 1
    )[1].split(");", 1)[0]
    assert "BOARD_TDMA_SPI_CAPTURE_SM" in service
    assert "s_tdma_pio_spi_p3_capture_offset" in capture_init
    assert "s_tdma_pio_spi_cal_capture_offset" not in capture_init
    assert "TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL" in service


def test_process_rx_reconstructs_absolute_fixed_frame_sequence() -> None:
    source = (ROOT / "components" / "tdma" / "src" /
              "tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    produced = source.split(
        "static uint64_t tdma_pio_spi_phys_rx_produced_words", 1
    )[1].split("static uint32_t tdma_pio_spi_phys_rx_ring_word", 1)[0]
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


def test_u32_delta_wraps() -> None:
    assert u32_delta(0xFFFFFFFE, 1) == 3
    assert counter_deltas({"x": 8}, {"x": 11}, ("x",)) == {"x": 3}


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
    for field in ("configured", "active", "local_node",
                  "map_reject_count", "length_reject_count"):
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
