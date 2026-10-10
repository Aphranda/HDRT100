#!/usr/bin/env python3
"""兼容转发到 tools/ota/visa_ota_send/visa_ota_send.py（迁移期保留）。"""
import pathlib, runpy, sys
_TARGET = pathlib.Path(__file__).resolve().parents[1] / "ota" / "visa_ota_send" / "visa_ota_send.py"
sys.argv[0] = str(_TARGET)
runpy.run_path(str(_TARGET), run_name="__main__")
