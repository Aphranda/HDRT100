from pathlib import Path

from tools.state_machine_resource_check import state_machine_resource_check


ROOT = Path(__file__).resolve().parents[2]


def test_directional_resource_contract_is_valid() -> None:
    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
    )
    assert failures == []


def test_directional_contract_rejects_mixed_data_program(tmp_path: Path) -> None:
    board = tmp_path / "board_config.h"
    board.write_text(
        (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(encoding="utf-8")
    pio.write_text(
        pio_text.replace(
            ".program tdma_pio_spi_directional_data_rx\n.wrap_target\n"
            "    wait 1 gpio 0\n    in pins, 1",
            ".program tdma_pio_spi_directional_data_rx\n.wrap_target\n"
            "    wait 1 gpio 0\n    out pins, 1",
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert "directional DATA RX contains out pins" in failures


def test_directional_contract_rejects_crossed_role_as_legacy_alias(
    tmp_path: Path,
) -> None:
    board = tmp_path / "board_config.h"
    board_text = (
        ROOT / "boards/rp2350_trig/inc/board_config.h"
    ).read_text(encoding="utf-8")
    board.write_text(
        board_text.replace(
            "#define BOARD_TDMA_TX_DATA_SM 1u",
            "#define BOARD_TDMA_TX_DATA_SM 2u",
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(
        board,
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
    )
    assert "BOARD_TDMA_TX_DATA_SM: expected 1, got 2" in failures


def test_flight_claim_reserves_directional_runtime_resources() -> None:
    phys = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8"
    )
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")

    assert "TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK" in phys
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


def test_normal_adapter_contract_models_parallel_clock_tx_and_data_rx() -> None:
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")

    for token in (
        "TDMA_STATE_MACHINE_NORMAL_COMM_RESOURCE_MASK",
        "tdma_state_machine_normal_comm_contract_t",
        ".clock_tx_pio",
        ".sync_tx_pio",
        ".data_rx_pio",
        ".clock_tx_sm",
        ".sync_tx_sm",
        ".data_rx_sm",
        ".data_rx_clock_pin",
        ".data_rx_sync_pin",
        ".clock_tx_dma",
        ".data_rx_dma",
    ):
        assert token in resources

    assert "TDMA_STATE_MACHINE_TX_PIO_RESOURCE" in resources
    assert "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE" in resources
    assert "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_SYNC_EDGE" in resources
    assert "tdma_adapter_comm_fsm" in (
        ROOT / "components/tdma/src/tdma_adapter_comm_fsm.c"
    ).read_text(encoding="utf-8")


def test_normal_adapter_contract_does_not_introduce_data_tx_or_serial_join():
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")
    fsm = (
        ROOT / "components/tdma/inc/tdma_adapter_comm_fsm.h"
    ).read_text(encoding="utf-8")

    normal_contract = resources.split(
        "typedef struct {", 1
    )[1].split(
        "tdma_state_machine_normal_comm_contract", 1
    )[0]
    assert "data_tx" not in normal_contract.lower()
    assert "full_duplex" not in normal_contract.lower()
    assert "CLOCK_TX_STARTED" in fsm
    assert "DATA_RX_STARTED" in fsm
    assert "CLOCK_TX_COMPLETED" in fsm
    assert "DATA_RX_COMPLETED" in fsm


def test_flight_claim_is_released_on_arm_failure_and_stop() -> None:
    phys = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8"
    )
    arm = phys.split("bool tdma_pio_spi_phys_arm(void *context", 1)[1].split(
        "void tdma_pio_spi_phys_disarm", 1
    )[0]
    disarm = phys.split("void tdma_pio_spi_phys_disarm", 1)[1].split(
        "static bool tdma_pio_spi_phys_tx_put", 1
    )[0]

    claim = arm.index("tdma_pio_spi_phys_claim_flight_resources")
    select = arm.index("tdma_pio_spi_phys_select_program_persona")
    assert claim < select
    assert arm.count("tdma_pio_spi_phys_release_flight_resources(phys)") == 6
    assert "tdma_pio_spi_phys_release_flight_resources(phys)" in disarm


def test_calibration_releases_completed_flight_admission() -> None:
    service = (
        ROOT / "components/tdma/src/tdma_pio_spi_phys_cal_service.inc"
    ).read_text(encoding="utf-8")

    assert service.count("const bool unloading_flight") == 2
    assert service.count("complete && unloading_flight") == 1
    assert service.count("tdma_pio_spi_phys_release_flight_resources(phys)") == 2
