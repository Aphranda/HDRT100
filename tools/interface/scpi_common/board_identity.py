"""Compatibility module for :mod:`tools.common.scpi.board_identity`."""

from importlib import import_module
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

_impl = import_module("tools.common.scpi.board_identity")
sys.modules[__name__] = _impl
