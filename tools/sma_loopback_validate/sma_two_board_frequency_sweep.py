#!/usr/bin/env python3
"""兼容转发（迁移期保留）。"""
import pathlib, runpy, sys
_T = pathlib.Path(__file__).resolve().parents[1] / "calibration" / "sma_loopback_validate" / "sma_two_board_frequency_sweep.py"
sys.argv[0] = str(_T)
runpy.run_path(str(_T), run_name="__main__")
