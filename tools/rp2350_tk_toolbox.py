from pathlib import Path
import runpy


if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parent / "bench" / "rp2350_tk_toolbox.py"), run_name="__main__")
