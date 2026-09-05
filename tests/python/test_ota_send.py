from types import SimpleNamespace
from unittest.mock import MagicMock, patch

import pytest

from tools.ota_send.ota_send import (
    closed_loop_expected_state,
    effective_block_size,
    parse_flash_transaction_state,
    parse_ota_state,
    parse_transfer_capability,
    reopen_after_end_reset,
    transfer_chunks,
    validate_block_size,
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


@pytest.mark.parametrize("block_size", [256, 512, 1024, 2048, 4096])
def test_legacy_sender_supports_configured_block_sizes(block_size: int) -> None:
    validate_block_size(block_size)
    assert effective_block_size(block_size, 4096) == block_size
    chunks = transfer_chunks(bytes(8192), block_size, False)
    assert max(len(chunk) for _, chunk in chunks) == block_size


def test_legacy_sender_rejects_non_catalog_block_size() -> None:
    with pytest.raises(ValueError, match="one of"):
        validate_block_size(768)


def test_legacy_sender_uses_three_field_capability_contract() -> None:
    assert parse_transfer_capability("4096,0,1") == (4096, 0, 1)
    assert parse_transfer_capability("256,0,1") == (256, 0, 1)
    with pytest.raises(ValueError, match="invalid OTA transfer capability"):
        parse_transfer_capability("4096,0")


def test_end_reset_closes_stale_handle_before_reopening() -> None:
    stale = MagicMock()
    reopened = MagicMock()
    reopened.__enter__.return_value = reopened
    serial_module = SimpleNamespace(
        Serial=MagicMock(return_value=reopened),
        SerialException=RuntimeError,
    )
    args = SimpleNamespace(
        port="COM7", baud=115200, timeout=3.0, begin_timeout=90.0)

    with patch("tools.ota_send.ota_send.query_final_status",
               return_value='"IDLE",1,"NONE",0'):
        status = reopen_after_end_reset(serial_module, stale, args)

    stale.close.assert_called_once_with()
    serial_module.Serial.assert_called_once_with(
        "COM7", 115200, timeout=3.0, write_timeout=3.0)
    assert status.startswith('"IDLE"')
