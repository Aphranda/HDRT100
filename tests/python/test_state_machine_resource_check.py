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


def test_flight_claim_reserves_dma_gpio_irq_and_dreq_classes() -> None:
    source = "\n".join(
        (ROOT / "components/tdma/src" / name).read_text(encoding="utf-8")
        for name in (
            "tdma_pio_spi_phys.c",
            "tdma_pio_spi_phys_persona.c",
        )
    )
    resources = (ROOT / "components/tdma/inc/tdma_state_machine_resources.h").read_text(
        encoding="utf-8")
    assert "TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK" in source
    for token in (
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_OUTPUT",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_FORWARD",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_SYNC_EDGE",
        "RESOURCE_ARBITER_RESOURCE_TDMA_GPIO",
        "RESOURCE_ARBITER_RESOURCE_TDMA_IRQ",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DREQ",
    ):
        assert token in resources


def test_maintenance_claim_uses_same_arbiter_projection() -> None:
    source = "\n".join(
        (ROOT / "components/tdma/src" / name).read_text(encoding="utf-8")
        for name in (
            "tdma_pio_spi_phys.c",
            "tdma_pio_spi_phys_persona.c",
        )
    )
    resources = (ROOT / "components/tdma/inc/tdma_state_machine_resources.h").read_text(
        encoding="utf-8")
    assert "TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK" in resources
    assert "TDMA_MAINTENANCE_RESOURCE_OWNER" in source
    assert "s_tdma_pio_spi_maintenance_resources_claimed" in source
    assert "resource_arbiter_acquire_owned(" in source
    assert "resource_arbiter_release_owned(" in source


def test_maintenance_projection_covers_shared_runtime_classes() -> None:
    resources = (ROOT / "components/tdma/inc/tdma_state_machine_resources.h").read_text(
        encoding="utf-8")
    mask = resources.split(
        "#define TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK", 1
    )[1].split("\n\n", 1)[0]
    for token in (
        "RESOURCE_ARBITER_RESOURCE_PIO2",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_OUTPUT",
        "RESOURCE_ARBITER_RESOURCE_TDMA_IRQ",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DREQ",
        "RESOURCE_ARBITER_RESOURCE_TDMA_GPIO",
    ):
        assert token in mask


def test_flight_arm_transfers_persona_before_claiming_overlapping_resources() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8"
    )
    arm = source.split("bool tdma_pio_spi_phys_arm", 1)[1].split(
        "void tdma_pio_spi_phys_disarm", 1
    )[0]
    assert arm.index("tdma_pio_spi_phys_select_program_persona") < arm.index(
        "tdma_pio_spi_phys_claim_flight_resources"
    )
    assert "TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK" not in arm
