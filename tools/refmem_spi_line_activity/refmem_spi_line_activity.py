#!/usr/bin/env python3
"""兼容转发到 tools/refmem/refmem_spi_line_activity/refmem_spi_line_activity.py（迁移期保留）。"""
import pathlib, runpy, sys
_TARGET = pathlib.Path(__file__).resolve().parents[1] / "refmem" / "refmem_spi_line_activity" / "refmem_spi_line_activity.py"
sys.argv[0] = str(_TARGET)
runpy.run_path(str(_TARGET), run_name="__main__")
