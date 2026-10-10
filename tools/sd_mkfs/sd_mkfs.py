#!/usr/bin/env python3
"""兼容转发到 tools/storage/sd_mkfs/sd_mkfs.py（迁移期保留）。"""
import pathlib, runpy, sys
_TARGET = pathlib.Path(__file__).resolve().parents[1] / "storage" / "sd_mkfs" / "sd_mkfs.py"
if __name__ == "__main__":
    sys.argv[0] = str(_TARGET)
    runpy.run_path(str(_TARGET), run_name="__main__")
else:  # imported: run the target inside this module so every loader sees it
    __file__ = str(_TARGET)
    if _TARGET.name == "__init__.py":
        __path__ = [str(_TARGET.parent)]
    exec(compile(_TARGET.read_text(encoding="utf-8"), str(_TARGET), "exec"))
