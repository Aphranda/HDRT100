#!/usr/bin/env python3
"""兼容转发（迁移期保留）。"""
import pathlib, runpy, sys
_T = pathlib.Path(__file__).resolve().parents[1] / "vdc" / "vdc_lock_readiness_validate" / "vdc_lock_readiness_validate.py"
sys.argv[0] = str(_T)
runpy.run_path(str(_T), run_name="__main__")
