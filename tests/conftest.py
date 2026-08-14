from __future__ import annotations

from dataclasses import dataclass
import sys
import time
from pathlib import Path
from typing import Iterator

import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[1]

if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))


@dataclass(frozen=True)
class HilConfig:
    port: str
    baud: int
    timeout: float
    settle: float


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--run-hil",
        action="store_true",
        default=False,
        help="run hardware-in-the-loop tests that open the board serial port",
    )
    parser.addoption(
        "--hil-port",
        action="store",
        default=None,
        help="USB CDC serial port for HIL tests, for example COM4",
    )
    parser.addoption(
        "--hil-timeout",
        action="store",
        type=float,
        default=5.0,
        help="per-command HIL timeout in seconds",
    )
    parser.addoption(
        "--hil-settle",
        action="store",
        type=float,
        default=1.5,
        help="settle time after opening the HIL serial port",
    )
    parser.addoption(
        "--hil-baud",
        action="store",
        type=int,
        default=115200,
        help="serial baud rate for HIL tests",
    )


@pytest.fixture(scope="session")
def hil_config(pytestconfig: pytest.Config) -> HilConfig:
    if not pytestconfig.getoption("--run-hil"):
        pytest.skip("HIL tests disabled; pass --run-hil --hil-port COMx to enable")

    port = pytestconfig.getoption("--hil-port")
    if not port:
        pytest.skip("HIL port not configured; pass --hil-port COMx")

    return HilConfig(
        port=port,
        baud=int(pytestconfig.getoption("--hil-baud")),
        timeout=float(pytestconfig.getoption("--hil-timeout")),
        settle=float(pytestconfig.getoption("--hil-settle")),
    )


def start_serial_lifecycle(serial_module, config: HilConfig):
    """Open and normalize a board serial session for one HIL test."""
    ser = serial_module.Serial(
        config.port,
        config.baud,
        timeout=0.3,
        write_timeout=config.timeout,
    )
    time.sleep(config.settle)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


def stop_serial_lifecycle(ser) -> None:
    """Best-effort flush/close for board serial sessions."""
    try:
        ser.flush()
    finally:
        ser.close()


@pytest.fixture
def hil_serial(hil_config: HilConfig) -> Iterator:
    serial_module = pytest.importorskip("serial")
    ser = start_serial_lifecycle(serial_module, hil_config)
    try:
        yield ser
    finally:
        stop_serial_lifecycle(ser)
