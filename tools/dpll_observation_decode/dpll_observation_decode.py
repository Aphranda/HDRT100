#!/usr/bin/env python3
"""兼容转发（迁移期保留）。"""
import pathlib, runpy, sys
_T = pathlib.Path(__file__).resolve().parents[1] / "vdc" / "dpll_observation_decode" / "dpll_observation_decode.py"
if __name__ == "__main__":
    sys.argv[0] = str(_T)
    runpy.run_path(str(_T), run_name="__main__")
else:  # imported: run the target inside this module so every loader sees it
    __file__ = str(_T)
    if _T.name == "__init__.py":
        __path__ = [str(_T.parent)]
    exec(compile(_T.read_text(encoding="utf-8"), str(_T), "exec"))
