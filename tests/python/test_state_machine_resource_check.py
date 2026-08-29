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
