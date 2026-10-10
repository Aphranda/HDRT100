#!/usr/bin/env python3
"""兼容转发到 tools/storage/sd_raw_clear/sd_raw_clear.py（迁移期保留）。"""
import pathlib, runpy, sys
_TARGET = pathlib.Path(__file__).resolve().parents[1] / "storage" / "sd_raw_clear" / "sd_raw_clear.py"
sys.argv[0] = str(_TARGET)
runpy.run_path(str(_TARGET), run_name="__main__")
