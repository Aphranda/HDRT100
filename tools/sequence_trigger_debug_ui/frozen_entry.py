"""Frozen GUI entry and allowlisted OTA subprocess dispatcher."""
from __future__ import annotations

import json
from pathlib import Path
import runpy
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

TOOL_MODULES = {
    name + ".py": "tools." + name + "." + name
    for name in ("visa_ota_send", "ota_multi_update", "ota_send", "ota_stream_send",
                 "ota_boot_commit", "picotool_reboot", "visa_ota_update", "usb_runtime_switch")
}


def self_test(destination: Path) -> None:
    import libusb_package
    import pyvisa
    import pyvisa_py
    import serial
    from tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui import SequenceUi

    # Packaging verification must never open a board or issue a command.
    SequenceUi.refresh_ports = lambda self, **_kwargs: None
    window = SequenceUi(settings_path=None)
    window.withdraw()
    try:
        window.update_idletasks()
        tabs = [window.mode_notebook.tab(tab, "text") for tab in window.mode_notebook.tabs()]
        assert len(tabs) == 5, tabs
        assert getattr(window, "_window_icon", None) is not None, "window icon unavailable"
        backend = libusb_package.get_libusb1_backend()
        assert backend is not None, "bundled libusb backend unavailable"
        report = dict(passed=True, frozen=bool(getattr(sys, "frozen", False)),
                      python=sys.version, serial=serial.__version__, visa=pyvisa.__version__,
                      visa_python=pyvisa_py.__version__, libusb=str(libusb_package.get_library_path()),
                      tk=window.tk.call("info", "patchlevel"), tabs=tabs)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    finally:
        window.close()


def main() -> int:
    # Prime PyUSB's shared libusb backend so PyVISA-py can use the packaged DLL.
    import libusb_package
    libusb_package.get_libusb1_backend()
    if len(sys.argv) > 1:
        if sys.argv[1] == "--self-test" and len(sys.argv) == 3:
            self_test(Path(sys.argv[2]))
            return 0
        module = TOOL_MODULES.get(Path(sys.argv[1]).name)
        if module is None:
            raise SystemExit("Unsupported bundled tool: " + sys.argv[1])
        sys.argv = sys.argv[1:]
        runpy.run_module(module, run_name="__main__")
        return 0
    from tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui import SequenceUi
    SequenceUi().mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
