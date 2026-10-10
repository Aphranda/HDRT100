#!/usr/bin/env python3
"""兼容转发到 tools/project/pdf_text_extract/pdf_text_extract.py（迁移期保留）。"""
import pathlib, runpy, sys
_TARGET = pathlib.Path(__file__).resolve().parents[1] / "project" / "pdf_text_extract" / "pdf_text_extract.py"
sys.argv[0] = str(_TARGET)
runpy.run_path(str(_TARGET), run_name="__main__")
