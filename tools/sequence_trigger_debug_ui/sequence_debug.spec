import os
from pathlib import Path
import sys

from PyInstaller.utils.hooks import collect_data_files, collect_dynamic_libs, collect_submodules, copy_metadata

tool_dir = Path(SPECPATH).resolve()
root = tool_dir.parents[1]
base_bin = Path(sys.base_prefix) / "Library" / "bin"
base_lib = Path(sys.base_prefix) / "Library" / "lib"
os.environ["PATH"] = str(base_bin) + os.pathsep + os.environ.get("PATH", "")
os.environ["TCL_LIBRARY"] = str(base_lib / "tcl8.6")
os.environ["TK_LIBRARY"] = str(base_lib / "tk8.6")
conda_dlls = [(str(base_bin / name), ".") for name in (
    "tcl86t.dll", "tk86t.dll", "ffi-8.dll", "libbz2.dll", "liblzma.dll")]
modules = [
    "tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui",
    *["tools." + name + "." + name for name in (
        "visa_ota_send", "ota_multi_update", "ota_send", "ota_stream_send",
        "ota_boot_commit", "picotool_reboot", "visa_ota_update", "usb_runtime_switch")],
    *collect_submodules("pyvisa_py"),
    *collect_submodules("serial.tools"),
    *collect_submodules("usb.backend"),
]
data = collect_data_files("libusb_package")
data.append((str(tool_dir / "5711_-_Sync_Event.png"), "."))
for package in ("pyvisa", "pyvisa-py", "pyserial", "pyusb", "libusb-package"):
    data += copy_metadata(package)

a = Analysis([str(tool_dir / "frozen_entry.py")], pathex=[str(root)],
             binaries=conda_dlls + collect_dynamic_libs("libusb_package"), datas=data,
             hiddenimports=modules, excludes=["pytest", "IPython", "matplotlib", "numpy"])
pyz = PYZ(a.pure)
gui = EXE(pyz, a.scripts, [], exclude_binaries=True, name="DHRT100_Sequence_Debug",
          console=False, strip=False, upx=False, icon=str(tool_dir / "5711_-_Sync_Event.png"))
helper = EXE(pyz, a.scripts, [], exclude_binaries=True, name="DHRT100_Tool",
             console=True, strip=False, upx=False)
bundle = COLLECT(gui, helper, a.binaries, a.datas,
                 name="DHRT100_Sequence_Debug", strip=False, upx=False)
