from pathlib import Path

from tools.state_machine_resource_check import state_machine_resource_check


ROOT = Path(__file__).resolve().parents[2]


def test_directional_resource_contract_is_valid() -> None:
    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
    )
    assert failures == []


def test_directional_contract_rejects_missing_crossed_direction(tmp_path: Path) -> None:
    board = tmp_path / "board_config.h"
    board.write_text(
        (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(encoding="utf-8")
    pio.write_text(
        pio_text.replace(
            ".program tdma_pio_spi_directional_tx\n.wrap_target\n"
            "    pull block\n    out pins, 2\n    wait 1 gpio 0\n    in pins, 1",
            ".program tdma_pio_spi_directional_tx\n.wrap_target\n"
            "    pull block\n    out pins, 2\n    wait 1 gpio 0",
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert "directional TX is missing in pins" in failures


def test_flight_forward_fifo_cannot_become_capture_endpoint(
    tmp_path: Path,
) -> None:
    board = tmp_path / "board_config.h"
    board.write_text(
        (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
            encoding="utf-8"),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    pio.write_text(
        pio_text.replace(
            "    mov osr, isr\n    out null, 24",
            "    mov osr, isr\n    push noblock\n    out null, 24",
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert "flight raw follower must not push its forward FIFO" in failures


def test_flight_capture_requires_independent_push_path(tmp_path: Path) -> None:
    board = tmp_path / "board_config.h"
    board.write_text(
        (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
            encoding="utf-8"),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    pio.write_text(
        pio_text.replace("    push noblock\n    jmp capture_byte", "    jmp capture_byte"),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert "flight capture is missing push" in failures


def test_flight_capture_patches_actual_wait_instructions() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    init = source.split(
        "static inline void tdma_pio_spi_flight_data_capture_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "instr_mem[offset + 0u]" in init
    assert "instr_mem[offset + 2u]" in init
    assert "instr_mem[offset + 4u]" in init
    assert "instr_mem[offset + 3u]" not in init
    assert "instr_mem[offset + 5u]" not in init


def test_calibration_transition_unloads_flight_programs_from_declared_pios() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8")
    transition = source.split(
        "static bool tdma_pio_spi_phys_cal_unload_source_step", 1,
    )[1].split("static bool tdma_pio_spi_phys_cal_load_p3_step", 1)[0]
    assert "count = 5u" in transition
    assert "count = 4u" in transition
    assert "pio_remove_program(BOARD_TDMA_TX_PIO" in transition
    assert "pio_remove_program(BOARD_TDMA_RX_PIO" in transition
    assert "pio_remove_program(BOARD_TDMA_SPI_PIO,\n                               &tdma_pio_spi_flight" not in transition


def test_calibration_switch_ready_has_directional_flight_gate() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8")
    ready = source.split(
        "static bool tdma_pio_spi_phys_cal_persona_switch_ready", 1,
    )[1].split("static bool tdma_pio_spi_phys_cal_unload_source_step", 1)[0]
    assert "tdma_pio_spi_phys_is_flight_persona()" in ready
    assert "BOARD_TDMA_TX_PIO->ctrl" in ready
    assert "BOARD_TDMA_RX_PIO->ctrl" in ready
