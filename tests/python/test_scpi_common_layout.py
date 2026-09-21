"""Compatibility contracts for the classified shared SCPI package."""

from __future__ import annotations

import importlib
import itertools
from pathlib import Path
import subprocess
import sys
import tempfile

import pytest

from tools.common.scpi import board_identity, ota_timing, scpi_serial


ROOT = Path(__file__).resolve().parents[2]


def test_tools_package_compatibility_modules_preserve_object_identity():
    assert importlib.import_module("tools.scpi_common.board_identity") is board_identity
    assert importlib.import_module("tools.scpi_common.ota_timing") is ota_timing
    assert importlib.import_module("tools.scpi_common.scpi_serial") is scpi_serial


def test_bare_compatibility_modules_preserve_object_identity():
    tools_dir = str(ROOT / "tools")
    sys.path.insert(0, tools_dir)
    try:
        assert importlib.import_module("scpi_common.board_identity") is board_identity
        assert importlib.import_module("scpi_common.ota_timing") is ota_timing
        assert importlib.import_module("scpi_common.scpi_serial") is scpi_serial
    finally:
        sys.path.remove(tools_dir)


def test_compatibility_symbols_are_the_canonical_objects():
    from tools.scpi_common.board_identity import parse_idn_response
    from tools.scpi_common.ota_timing import OtaTiming
    from tools.scpi_common.scpi_serial import SerialSession

    assert parse_idn_response is board_identity.parse_idn_response
    assert OtaTiming is ota_timing.OtaTiming
    assert SerialSession is scpi_serial.SerialSession


@pytest.mark.parametrize("order", [
    ("tools.common.scpi.scpi_serial", "tools.scpi_common.scpi_serial",
     "scpi_common.scpi_serial"),
    ("tools.scpi_common.scpi_serial", "scpi_common.scpi_serial",
     "tools.common.scpi.scpi_serial"),
    ("scpi_common.scpi_serial", "tools.common.scpi.scpi_serial",
     "tools.scpi_common.scpi_serial"),
])
def test_scpi_serial_identity_is_import_order_independent(order):
    source = (
        "import importlib,sys\n"
        f"sys.path=[{str(ROOT / 'tools')!r},{str(ROOT)!r}] + [p for p in sys.path if p]\n"
        f"names={order!r}\n"
        "modules=[importlib.import_module(name) for name in names]\n"
        "assert modules[0] is modules[1] is modules[2]\n"
        "assert modules[0].SerialSession is modules[1].SerialSession is modules[2].SerialSession\n"
    )
    with tempfile.TemporaryDirectory() as outside:
        result = subprocess.run(
            [sys.executable, "-c", source], cwd=outside,
            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


def test_bare_compatibility_package_bootstraps_canonical_outside_repo():
    source = (
        "import importlib,sys\n"
        f"sys.path=[{str(ROOT / 'tools')!r}] + [p for p in sys.path if p]\n"
        "first=importlib.import_module('scpi_common')\n"
        "canonical=importlib.import_module('tools.common.scpi')\n"
        "legacy=importlib.import_module('tools.scpi_common')\n"
        "bare=importlib.import_module('scpi_common')\n"
        "assert first is canonical is legacy is bare\n"
    )
    with tempfile.TemporaryDirectory() as outside:
        result = subprocess.run(
            [sys.executable, "-c", source], cwd=outside,
            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("order", list(itertools.permutations([
    "tools.common.scpi", "tools.scpi_common", "scpi_common",
])))
def test_package_aliases_support_normal_import_in_every_order(order):
    source = (
        "import sys\n"
        f"sys.path=[{str(ROOT / 'tools')!r},{str(ROOT)!r}] + [p for p in sys.path if p]\n"
        + "\n".join(f"import {name}" for name in order) + "\n"
        "import tools\n"
        "canonical=sys.modules['tools.common.scpi']\n"
        "assert tools.scpi_common is canonical\n"
        "assert sys.modules['tools.scpi_common'] is canonical\n"
        "assert sys.modules['scpi_common'] is canonical\n"
    )
    with tempfile.TemporaryDirectory() as outside:
        result = subprocess.run(
            [sys.executable, "-c", source], cwd=outside,
            capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
