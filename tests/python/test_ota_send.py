import pytest

from tools.ota_send.ota_send import (
    closed_loop_expected_state,
    parse_flash_transaction_state,
    parse_ota_state,
)


def test_parse_flash_transaction_state_reads_generation() -> None:
    state, generation = parse_flash_transaction_state(
        "9,7,1,2,2,512,512,512,1,3,0,42,4,1,0,0,0,7,7,0,0,1,0,0,0,10,11"
    )

    assert state == 9
    assert generation == 42


def test_parse_flash_transaction_state_rejects_short_vector() -> None:
    with pytest.raises(ValueError, match="incomplete"):
        parse_flash_transaction_state("9,7,1")


def test_parse_ota_state_handles_quoted_scpi_status() -> None:
    assert parse_ota_state('"READY_TO_REBOOT",2,"NONE",2') == "READY_TO_REBOOT"


def test_closed_loop_default_expects_committed_terminal() -> None:
    assert closed_loop_expected_state("READY_TO_REBOOT", True) == "COMMITTED"
    assert closed_loop_expected_state("FAILED", True) == "FAILED"
    assert closed_loop_expected_state("READY_TO_REBOOT", False) == "READY_TO_REBOOT"
