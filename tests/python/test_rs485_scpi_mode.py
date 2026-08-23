from __future__ import annotations

from pathlib import Path

from tools.scpi_query.scpi_query import strip_rs485_test_echo


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "middleware/scpi_port/inc/scpi_communication_uart_commands.h"
SOURCE = ROOT / "middleware/scpi_port/src/scpi_communication_uart_commands.c"
DRIVER = ROOT / "drivers/mcu/uart/src/drv_rs485.c"
COMMANDS = ROOT / "docs/interface/SCPI_COMMANDS.md"


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


def test_rx_status_projects_dma_backend_and_echo_health():
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    docs = COMMANDS.read_text(encoding="utf-8")
    assert '"COMMunication:SERial:UART#:RX:STATus?"' in header
    assert "scpi_cmd_uart_rx_status_q" in source
    assert '"DMA_PINGPONG"' in source
    assert "DMA overrun" in docs


def test_mode_does_not_claim_backend_ready():
    docs = (ROOT / "docs/communication/COMMUNICATION_RS485_ARCHITECTURE.md").read_text(
        encoding="utf-8"
    )
    todo = (ROOT / "docs/communication/COMMUNICATION_RS485_TODO.md").read_text(
        encoding="utf-8"
    )
    assert "不得宣称 RS485 数据面已 ready" in docs
    assert "PENDING_BACKEND" in todo


def test_rs485_driver_owns_direction_and_bounded_test_frame():
    source = DRIVER.read_text(encoding="utf-8")
    assert "gpio_put(s_config.de_pin, 1u)" in source
    assert "uart_tx_wait_blocking" in source
    assert "gpio_put(s_config.de_pin, 0u)" in source
    assert "size > 256u" in source
    assert "RS485_ECHO_IDLE_US" in source
    assert "s_echo_discard" in source
    assert "time_us_64" in source
    assert "drv_rs485_write_internal" in source
    assert "s_response_echo" in source
    assert "RS485_DMA_BUFFER_SIZE" in source
    assert "DMA_IRQ_1" in source
    assert "dma_channel_configure" in source
    assert "rs485_dma_partial_limit" in source
    assert "s_dma_last_activity_us" in source


def test_scpi_query_filters_printable_rs485_loopback_payload_before_ack():
    command = "COMMunication:SERial:UART1:TX:TEST 8,85"
    assert strip_rs485_test_echo(command, "UUUUUUUU1") == "1"
    assert strip_rs485_test_echo(command, "1") == "1"
    # Binary payloads are intentionally left untouched by the line helper.
    assert strip_rs485_test_echo(
        "COMMunication:SERial:UART1:TX:TEST 2,255", "��1"
    ) == "��1"
