"""Compatibility package for :mod:`tools.common.scpi`."""

from importlib import import_module
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

_CANONICAL_NAME = "tools.common.scpi"
_canonical = import_module(_CANONICAL_NAME)
_tools_package = import_module("tools")
for _child in ("board_identity", "ota_timing", "scpi_serial"):
    _module = import_module(f"{_CANONICAL_NAME}.{_child}")
    sys.modules[f"tools.scpi_common.{_child}"] = _module
    sys.modules[f"scpi_common.{_child}"] = _module
sys.modules["tools.scpi_common"] = _canonical
sys.modules["scpi_common"] = _canonical
setattr(_tools_package, "scpi_common", _canonical)
