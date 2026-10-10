#!/usr/bin/env python3
"""兼容转发到 tools/trigger/model_turntable_load_validate/model_turntable_load_validate.py（迁移期保留）。"""
import pathlib, runpy, sys
_TARGET = pathlib.Path(__file__).resolve().parents[1] / "trigger" / "model_turntable_load_validate" / "model_turntable_load_validate.py"
sys.argv[0] = str(_TARGET)
runpy.run_path(str(_TARGET), run_name="__main__")
