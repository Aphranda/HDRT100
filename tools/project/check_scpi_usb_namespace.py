from pathlib import Path
import runpy


if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parent / "checks" / "check_scpi_usb_namespace.py"), run_name="__main__")
