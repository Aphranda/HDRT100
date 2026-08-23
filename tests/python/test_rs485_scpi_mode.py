from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "middleware/scpi_port/inc/scpi_communication_uart_commands.h"
SOURCE = ROOT / "middleware/scpi_port/src/scpi_communication_uart_commands.c"


def test_usb_scpi_mode_commands_are_registered_for_uart_rs485_channel():
    header = HEADER.read_text(encoding="utf-8")
    assert '"COMMunication:SERial:UART#:MODE"' in header
    assert '"COMMunication:SERial:UART#:MODE?"' in header
    assert "scpi_cmd_uart_mode" in header


def test_mode_selection_is_explicit_and_defaults_to_scpi():
    source = SOURCE.read_text(encoding="utf-8")
    assert "SCPI_UART_MODE_SCPI = 0u" in source
    assert "s_uart_mode = SCPI_UART_MODE_SCPI" in source
    assert 'scpi_uart_text_equal(value, length, "MODBUS")' in source
    assert 'scpi_uart_text_equal(value, length, "SCPI")' in source
    assert 'SCPI_ResultText(context, scpi_uart_mode_text())' in source


def test_mode_does_not_claim_backend_ready():
    docs = (ROOT / "docs/communication/COMMUNICATION_RS485_ARCHITECTURE.md").read_text(
        encoding="utf-8"
    )
    todo = (ROOT / "docs/communication/COMMUNICATION_RS485_TODO.md").read_text(
        encoding="utf-8"
    )
    assert "不得宣称 RS485 数据面已 ready" in docs
    assert "PENDING_BACKEND" in todo
