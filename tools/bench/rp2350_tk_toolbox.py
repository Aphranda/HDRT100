#!/usr/bin/env python3
"""Tkinter bench toolbox for RP2350_TRIG.

The GUI wraps existing tools for OTA/release flows and uses small SCPI helpers
for quick trigger/status operations.
"""

from __future__ import annotations

import os
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path
from tkinter import BOTH, END, LEFT, RIGHT, X, BooleanVar, IntVar, StringVar, Tk, filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUILD_DIR = ROOT / "build"
DEFAULT_PACKAGE = DEFAULT_BUILD_DIR / "DHRT100_UPDATE.pkg"
DEFAULT_FACTORY = DEFAULT_BUILD_DIR / "DHRT100_FACTORY.uf2"
DEFAULT_SD_DIR = DEFAULT_BUILD_DIR / "sdcard"
PYTHON = sys.executable
PACKAGE_MAGIC = 0x474B5054
PACKAGE_HEADER_SIZE = 512

STATUS_COMMANDS = [
    "*IDN?",
    "SYST:FW:BUILD?",
    "SYST:BOOT:VERS?",
    "SYST:BOOT:CAP?",
    "SYST:OTA:MODE?",
    "SYST:OTA:STAT?",
    "SYST:OTA:PROG?",
    "SYST:OTA:SLOT?",
    "SYST:OTA:RES?",
    "SYST:OTA:TXN?",
    "SYST:SD:STAT?",
    "SYST:SD:INFO?",
    "STAT:TRIG?",
    "STAT:SYNC?",
]


def serial_module():
    try:
        import serial  # type: ignore

        return serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required: python -m pip install pyserial") from exc


def list_serial_ports() -> list[str]:
    try:
        from serial.tools import list_ports  # type: ignore

        ports = [port.device for port in list_ports.comports()]
        if ports:
            return ports
    except ImportError:
        pass
    return [f"COM{i}" for i in range(1, 17)] if os.name == "nt" else []


class ScpiSession:
    def __init__(self, port: str, timeout: float = 0.8):
        serial = serial_module()
        self.ser = serial.Serial(port, 115200, timeout=timeout)

    def close(self) -> None:
        self.ser.close()

    def drain(self) -> None:
        time.sleep(0.03)
        try:
            self.ser.reset_input_buffer()
        except Exception:
            pass

    def write_line(self, command: str) -> None:
        self.ser.write((command + "\n").encode("ascii"))

    def read_response(self, timeout: float | None = None) -> str:
        old_timeout = self.ser.timeout
        if timeout is not None:
            self.ser.timeout = timeout
        try:
            for _ in range(32):
                raw = self.ser.readline()
                if not raw:
                    return ""
                line = raw.decode("ascii", errors="replace").strip()
                if not line or line.startswith("[") or line == '"OK"':
                    continue
                while line.startswith('"OK"'):
                    line = line[len('"OK"') :].strip()
                if line:
                    return line
            return ""
        finally:
            self.ser.timeout = old_timeout

    def command(self, command: str, delay: float = 0.05, timeout: float | None = None) -> str:
        self.drain()
        self.write_line(command)
        time.sleep(delay)
        return self.read_response(timeout)


class CommandRunner:
    def __init__(self, output_queue: queue.Queue[tuple[str, str]]):
        self.output_queue = output_queue
        self.process: subprocess.Popen[str] | None = None
        self.thread: threading.Thread | None = None

    def busy(self) -> bool:
        return self.thread is not None and self.thread.is_alive()

    def run(self, label: str, command: list[str], cwd: Path = ROOT) -> bool:
        if self.busy():
            self.output_queue.put(("log", "A command is already running.\n"))
            return False
        self.thread = threading.Thread(target=self._worker, args=(label, command, cwd), daemon=True)
        self.thread.start()
        return True

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.output_queue.put(("log", "Terminating running command...\n"))
            self.process.terminate()

    def _worker(self, label: str, command: list[str], cwd: Path) -> None:
        self.output_queue.put(("state", "busy"))
        self.output_queue.put(("log", f"\n$ {' '.join(command)}\n"))
        try:
            creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
            self.process = subprocess.Popen(
                command,
                cwd=cwd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                creationflags=creationflags,
            )
            assert self.process.stdout is not None
            for line in self.process.stdout:
                self.output_queue.put(("log", line))
            code = self.process.wait()
            self.output_queue.put(("log", f"[{label}] exit_code={code}\n"))
        except Exception as exc:
            self.output_queue.put(("log", f"[{label}] failed: {exc}\n"))
        finally:
            self.process = None
            self.output_queue.put(("state", "idle"))


class ToolboxApp:
    def __init__(self, root: Tk):
        self.root = root
        self.root.title("RP2350_TRIG Toolbox")
        self.root.geometry("1120x760")
        self.output_queue: queue.Queue[tuple[str, str]] = queue.Queue()
        self.runner = CommandRunner(self.output_queue)
        self.serial_busy = False

        self.port_var = StringVar(value=self._default_port())
        self.build_dir_var = StringVar(value=str(DEFAULT_BUILD_DIR))
        self.package_var = StringVar(value=str(DEFAULT_PACKAGE))
        self.factory_var = StringVar(value=str(DEFAULT_FACTORY))
        self.sd_dir_var = StringVar(value=str(DEFAULT_SD_DIR))
        self.timeout_var = StringVar(value="8")
        self.skip_flash_var = BooleanVar(value=True)
        self.skip_negative_var = BooleanVar(value=True)

        self.seq_len_var = IntVar(value=256)
        self.seq_width_var = IntVar(value=4)
        self.mode_var = StringVar(value="SEQ_STEP")
        self.source_pin_var = IntVar(value=16)
        self.edge_var = StringVar(value="RISING")
        self.gate_var = BooleanVar(value=False)
        self.safe_var = StringVar(value="ZERO")
        self.trigger_width_var = IntVar(value=10)
        self.pulse_width_var = IntVar(value=10)
        self.rj45_trigger_width_var = IntVar(value=10)
        self.sample_rate_var = IntVar(value=1000000)
        self.clock_freq_var = IntVar(value=1000)
        self.clock_state_var = BooleanVar(value=False)
        self.measure_gate_var = IntVar(value=500)
        self.timing_duration_var = StringVar(value="20")
        self.signal_hz_var = StringVar(value="1000")
        self.custom_scpi_var = StringVar(value="STAT:TRIG?")

        self.status_labels: dict[str, StringVar] = {}
        self._build_ui()
        self._poll_output()

    def _default_port(self) -> str:
        ports = list_serial_ports()
        return "COM5" if "COM5" in ports else (ports[0] if ports else "COM5")

    def _build_ui(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)

        top = ttk.Frame(self.root, padding=8)
        top.grid(row=0, column=0, sticky="ew")
        top.columnconfigure(7, weight=1)

        ttk.Label(top, text="Port").grid(row=0, column=0, sticky="w")
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=12, values=list_serial_ports())
        self.port_combo.grid(row=0, column=1, sticky="w", padx=(4, 12))
        ttk.Button(top, text="Refresh", command=self.refresh_ports).grid(row=0, column=2, padx=(0, 12))
        ttk.Label(top, text="Build Dir").grid(row=0, column=3, sticky="w")
        ttk.Entry(top, textvariable=self.build_dir_var, width=44).grid(row=0, column=4, sticky="ew", padx=(4, 4))
        ttk.Button(top, text="Browse", command=self.browse_build_dir).grid(row=0, column=5, padx=(0, 12))
        ttk.Button(top, text="Query Status", command=self.query_status).grid(row=0, column=6, padx=(0, 4))
        ttk.Button(top, text="Stop Task", command=self.runner.stop).grid(row=0, column=7, sticky="w")

        panes = ttk.PanedWindow(self.root, orient="horizontal")
        panes.grid(row=1, column=0, sticky="nsew", padx=8, pady=(0, 8))

        left = ttk.Frame(panes)
        right = ttk.Frame(panes)
        panes.add(left, weight=1)
        panes.add(right, weight=2)

        notebook = ttk.Notebook(left)
        notebook.pack(fill=BOTH, expand=True)

        self._build_status_tab(notebook)
        self._build_ota_tab(notebook)
        self._build_trigger_tab(notebook)
        self._build_scpi_tab(notebook)

        right.rowconfigure(1, weight=1)
        right.columnconfigure(0, weight=1)
        log_bar = ttk.Frame(right)
        log_bar.grid(row=0, column=0, sticky="ew", pady=(0, 4))
        ttk.Label(log_bar, text="Output").pack(side=LEFT)
        ttk.Button(log_bar, text="Clear", command=self.clear_log).pack(side=RIGHT)
        self.output = ScrolledText(right, height=30, wrap="word")
        self.output.grid(row=1, column=0, sticky="nsew")
        self.status_var = StringVar(value="Idle")
        ttk.Label(right, textvariable=self.status_var).grid(row=2, column=0, sticky="ew", pady=(4, 0))

    def _build_status_tab(self, notebook: ttk.Notebook) -> None:
        frame = ttk.Frame(notebook, padding=8)
        notebook.add(frame, text="Status")
        frame.columnconfigure(1, weight=1)
        for row, command in enumerate(STATUS_COMMANDS):
            ttk.Label(frame, text=command).grid(row=row, column=0, sticky="w", pady=2)
            var = StringVar(value="-")
            self.status_labels[command] = var
            ttk.Label(frame, textvariable=var).grid(row=row, column=1, sticky="ew", padx=(8, 0), pady=2)
        ttk.Button(frame, text="Refresh Status", command=self.query_status).grid(
            row=len(STATUS_COMMANDS), column=0, columnspan=2, sticky="ew", pady=(8, 0)
        )

    def _build_ota_tab(self, notebook: ttk.Notebook) -> None:
        frame = ttk.Frame(notebook, padding=8)
        notebook.add(frame, text="OTA")
        frame.columnconfigure(1, weight=1)

        ttk.Label(frame, text="Unified Package").grid(row=0, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.package_var).grid(row=0, column=1, sticky="ew", padx=4)
        ttk.Button(frame, text="Browse", command=self.browse_package).grid(row=0, column=2)

        ttk.Label(frame, text="Factory UF2").grid(row=1, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.factory_var).grid(row=1, column=1, sticky="ew", padx=4)
        ttk.Button(frame, text="Browse", command=self.browse_factory).grid(row=1, column=2)

        ttk.Label(frame, text="SD FS Dir").grid(row=2, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.sd_dir_var).grid(row=2, column=1, sticky="ew", padx=4)
        ttk.Button(frame, text="Browse", command=self.browse_sd_dir).grid(row=2, column=2)

        ttk.Label(frame, text="Timeout").grid(row=3, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.timeout_var, width=8).grid(row=3, column=1, sticky="w", padx=4)

        ttk.Checkbutton(frame, text="Skip factory flash in validation", variable=self.skip_flash_var).grid(
            row=4, column=0, columnspan=3, sticky="w", pady=(6, 0)
        )
        ttk.Checkbutton(frame, text="Skip negative OTA tests", variable=self.skip_negative_var).grid(
            row=5, column=0, columnspan=3, sticky="w"
        )

        buttons = ttk.Frame(frame)
        buttons.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(10, 0))
        for i in range(2):
            buttons.columnconfigure(i, weight=1)
        ttk.Button(buttons, text="Build Release", command=self.build_release).grid(row=0, column=0, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Release Check", command=self.release_check).grid(row=0, column=1, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Send OTA Package", command=self.send_ota).grid(row=1, column=0, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Full Board Validate", command=self.board_validate).grid(row=1, column=1, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Build SD FS", command=self.build_sd_fs).grid(row=2, column=0, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="OTA BOOT", command=lambda: self.scpi_one("SYST:OTA:BOOT")).grid(
            row=2, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buttons, text="OTA COMMIT", command=lambda: self.scpi_one("SYST:OTA:COMM")).grid(
            row=3, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buttons, text="OTA ABORT", command=lambda: self.scpi_one("SYST:OTA:ABOR")).grid(
            row=3, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buttons, text="OTA Status", command=self.query_status).grid(row=4, column=0, columnspan=2, sticky="ew", padx=2, pady=2)

    def _build_trigger_tab(self, notebook: ttk.Notebook) -> None:
        frame = ttk.Frame(notebook, padding=8)
        notebook.add(frame, text="Trigger")
        frame.columnconfigure(1, weight=1)

        fields = [
            ("Mode", ttk.Combobox(frame, textvariable=self.mode_var, values=["IDLE", "SEQ_STEP", "ENC_COUNT"], state="readonly")),
            ("Source Pin", ttk.Entry(frame, textvariable=self.source_pin_var)),
            ("Edge", ttk.Combobox(frame, textvariable=self.edge_var, values=["RISING", "FALLING"], state="readonly")),
            ("Seq Length", ttk.Entry(frame, textvariable=self.seq_len_var)),
            ("Seq Width", ttk.Entry(frame, textvariable=self.seq_width_var)),
            ("Trigger Width us", ttk.Entry(frame, textvariable=self.trigger_width_var)),
            ("Pulse Width us", ttk.Entry(frame, textvariable=self.pulse_width_var)),
            ("RJ45 Width us", ttk.Entry(frame, textvariable=self.rj45_trigger_width_var)),
            ("Sample Rate Hz", ttk.Entry(frame, textvariable=self.sample_rate_var)),
            ("Clock Freq Hz", ttk.Entry(frame, textvariable=self.clock_freq_var)),
            ("Safe State", ttk.Combobox(frame, textvariable=self.safe_var, values=["ZERO", "ONE"], state="readonly")),
        ]
        for row, (label, widget) in enumerate(fields):
            ttk.Label(frame, text=label).grid(row=row, column=0, sticky="w", pady=2)
            widget.grid(row=row, column=1, sticky="ew", padx=(8, 0), pady=2)

        ttk.Checkbutton(frame, text="Gate Enable", variable=self.gate_var).grid(row=11, column=0, sticky="w", pady=2)
        ttk.Checkbutton(frame, text="Clock Output Enable", variable=self.clock_state_var).grid(
            row=11, column=1, sticky="w", padx=(8, 0), pady=2
        )

        buttons = ttk.Frame(frame)
        buttons.grid(row=12, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        for i in range(3):
            buttons.columnconfigure(i, weight=1)
        ttk.Button(buttons, text="Apply Config", command=self.apply_trigger_config).grid(row=0, column=0, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="ARM", command=lambda: self.scpi_one("TRIG:ARM")).grid(row=0, column=1, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="DISARM", command=lambda: self.scpi_one("TRIG:DIS")).grid(row=0, column=2, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Trigger Now", command=lambda: self.scpi_one("TRIG:IMM")).grid(row=1, column=0, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Pulse Now", command=lambda: self.scpi_one("PULS:IMM")).grid(row=1, column=1, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="RJ45 Now", command=lambda: self.scpi_one("RJ45:TRIG:IMM")).grid(row=1, column=2, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Trigger Status", command=lambda: self.scpi_one("STAT:TRIG?")).grid(
            row=2, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buttons, text="Measure Freq", command=self.measure_frequency).grid(row=2, column=1, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Run Meas Tool", command=self.run_meas_tool).grid(row=2, column=2, sticky="ew", padx=2, pady=2)
        ttk.Button(buttons, text="Run Timing Test", command=self.run_timing_test).grid(
            row=3, column=0, columnspan=3, sticky="ew", padx=2, pady=2
        )

        meas = ttk.Frame(frame)
        meas.grid(row=13, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        ttk.Label(meas, text="Measure Gate ms").pack(side=LEFT)
        ttk.Entry(meas, textvariable=self.measure_gate_var, width=8).pack(side=LEFT, padx=(6, 12))
        ttk.Label(meas, text="Timing Duration s").pack(side=LEFT)
        ttk.Entry(meas, textvariable=self.timing_duration_var, width=8).pack(side=LEFT, padx=(6, 12))
        ttk.Label(meas, text="Signal Hz").pack(side=LEFT)
        ttk.Entry(meas, textvariable=self.signal_hz_var, width=8).pack(side=LEFT, padx=(6, 0))

    def _build_scpi_tab(self, notebook: ttk.Notebook) -> None:
        frame = ttk.Frame(notebook, padding=8)
        notebook.add(frame, text="SCPI")
        frame.columnconfigure(0, weight=1)
        ttk.Entry(frame, textvariable=self.custom_scpi_var).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(frame, text="Send", command=self.send_custom_scpi).grid(row=0, column=1)

        common = ttk.LabelFrame(frame, text="Common Queries", padding=6)
        common.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        for i in range(3):
            common.columnconfigure(i, weight=1)
        commands = [
            "*IDN?",
            "SYST:FW:BUILD?",
            "SYST:OTA:STAT?",
            "SYST:OTA:PROG?",
            "STAT:TRIG?",
            "STAT:SYNC?",
            "TRIG:MODE?",
            "TRIG:SOUR?",
            "TRIG:EDGE?",
        ]
        for index, command in enumerate(commands):
            ttk.Button(common, text=command, command=lambda c=command: self.scpi_one(c)).grid(
                row=index // 3, column=index % 3, sticky="ew", padx=2, pady=2
            )

    def refresh_ports(self) -> None:
        ports = list_serial_ports()
        self.port_combo["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(ports[0])
        self.log(f"Ports: {', '.join(ports) if ports else 'none'}\n")

    def browse_build_dir(self) -> None:
        path = filedialog.askdirectory(initialdir=str(ROOT))
        if path:
            self.build_dir_var.set(path)
            self.package_var.set(str(Path(path) / "DHRT100_UPDATE.pkg"))
            self.factory_var.set(str(Path(path) / "DHRT100_FACTORY.uf2"))
            self.sd_dir_var.set(str(Path(path) / "sdcard"))

    def browse_package(self) -> None:
        path = filedialog.askopenfilename(initialdir=str(ROOT), filetypes=[("Unified OTA package", "*.pkg"), ("All", "*.*")])
        if path:
            self.package_var.set(path)

    def browse_factory(self) -> None:
        path = filedialog.askopenfilename(initialdir=str(ROOT), filetypes=[("UF2", "*.uf2"), ("All", "*.*")])
        if path:
            self.factory_var.set(path)

    def browse_sd_dir(self) -> None:
        path = filedialog.askdirectory(initialdir=str(ROOT))
        if path:
            self.sd_dir_var.set(path)

    def build_release(self) -> None:
        self.run_command(
            "build",
            [
                PYTHON,
                "tools/cmake_build_auto/cmake_build_auto.py",
                "--preset",
                "pico2-release",
                "--build-dir",
                self.build_dir_var.get(),
            ],
        )

    def release_check(self) -> None:
        self.run_command(
            "release_check",
            [
                PYTHON,
                "tools/release_check/release_check.py",
                "--preset",
                "pico2-release",
                "--build-dir",
                self.build_dir_var.get(),
            ],
        )

    def build_sd_fs(self) -> None:
        build_dir = self.build_dir_var.get()
        if not self._require_path(build_dir, "build directory"):
            return
        package = self.package_var.get()
        factory = self.factory_var.get()
        if not self._require_path(package, "OTA package"):
            return
        if not self._require_unified_package(package):
            return
        if not self._require_path(factory, "factory UF2"):
            return
        self.run_command(
            "sd_fs_build",
            [
                PYTHON,
                "tools/sd_fs_build/sd_fs_build.py",
                "--build-dir",
                build_dir,
                "--output-dir",
                self.sd_dir_var.get(),
                "--package",
                package,
                "--factory",
                factory,
            ],
        )

    def send_ota(self) -> None:
        package = self.package_var.get()
        if not self._require_path(package, "OTA package"):
            return
        if not self._require_unified_package(package):
            return
        self.run_command(
            "ota_send",
            [
                PYTHON,
                "tools/ota_send/ota_send.py",
                self.port_var.get(),
                package,
                "--timeout",
                self.timeout_var.get(),
                "--expect-final-state",
                "READY_TO_REBOOT",
            ],
        )

    def board_validate(self) -> None:
        build_dir = self.build_dir_var.get()
        if not self._require_path(build_dir, "build directory"):
            return
        command = [
            PYTHON,
            "tools/ota_board_validate/ota_board_validate.py",
            self.port_var.get(),
            build_dir,
            "--timeout",
            self.timeout_var.get(),
        ]
        package = self.package_var.get()
        factory = self.factory_var.get()
        if package:
            if not self._require_unified_package(package):
                return
            command.extend(["--package", package])
        if factory:
            command.extend(["--factory", factory])
        if self.skip_flash_var.get():
            command.append("--skip-flash")
        if self.skip_negative_var.get():
            command.append("--skip-negative")
        self.run_command("ota_board_validate", command)

    def run_meas_tool(self) -> None:
        self.run_command(
            "trigger_meas",
            [
                PYTHON,
                "tools/trigger_meas/trigger_meas.py",
                self.port_var.get(),
                "--gate",
                str(self.measure_gate_var.get()),
                "--runs",
                "3",
            ],
        )

    def run_timing_test(self) -> None:
        self.run_command(
            "trigger_timing_test",
            [
                PYTHON,
                "tools/trigger_timing_test/trigger_timing_test.py",
                self.port_var.get(),
                "--seq-len",
                str(self.seq_len_var.get()),
                "--seq-width",
                str(self.seq_width_var.get()),
                "--duration",
                self.timing_duration_var.get(),
                "--signal-hz",
                self.signal_hz_var.get(),
            ],
        )

    def query_status(self) -> None:
        if self._busy():
            self.log("A serial command or tool task is already running.\n")
            return
        self.serial_busy = True

        def worker() -> None:
            self.output_queue.put(("state", "busy"))
            try:
                session = ScpiSession(self.port_var.get(), timeout=0.8)
                try:
                    self.output_queue.put(("log", "\n[status]\n"))
                    for command in STATUS_COMMANDS:
                        response = session.command(command)
                        self.output_queue.put(("status", f"{command}\t{response}"))
                        self.output_queue.put(("log", f"{command} -> {response}\n"))
                finally:
                    session.close()
            except Exception as exc:
                self.output_queue.put(("log", f"[status] failed: {exc}\n"))
            finally:
                self.serial_busy = False
                self.output_queue.put(("state", "idle"))

        threading.Thread(target=worker, daemon=True).start()

    def apply_trigger_config(self) -> None:
        mode = {"IDLE": 0, "SEQ_STEP": 1, "ENC_COUNT": 2}[self.mode_var.get()]
        edge = 0 if self.edge_var.get() == "RISING" else 1
        safe = 0 if self.safe_var.get() == "ZERO" else 1
        commands = [
            "TRIG:DIS",
            f"TRIG:SOUR {self.source_pin_var.get()}",
            f"TRIG:EDGE {edge}",
            f"TRIG:GATE {1 if self.gate_var.get() else 0}",
            f"TRIG:SAFE {safe}",
            f"TRIG:SEQ:LENG {self.seq_len_var.get()}",
            f"TRIG:SEQ:WIDT {self.seq_width_var.get()}",
            f"TRIG:WIDT {self.trigger_width_var.get()}",
            f"PULS:WIDT {self.pulse_width_var.get()}",
            f"RJ45:TRIG:WIDT {self.rj45_trigger_width_var.get()}",
            f"SAMP:RATE {self.sample_rate_var.get()}",
            f"OUTP:CLOC:FREQ {self.clock_freq_var.get()}",
            f"OUTP:CLOC:STAT {1 if self.clock_state_var.get() else 0}",
            f"TRIG:MODE {mode}",
            "STAT:TRIG?",
            "STAT:SYNC?",
        ]
        self.scpi_many(commands)

    def measure_frequency(self) -> None:
        gate = self.measure_gate_var.get()
        self.scpi_one(f"MEAS:FREQ? {gate}", delay=gate / 1000.0 + 0.1, timeout=gate / 1000.0 + 1.0)

    def send_custom_scpi(self) -> None:
        command = self.custom_scpi_var.get().strip()
        if command:
            self.scpi_one(command)

    def scpi_one(self, command: str, delay: float = 0.05, timeout: float | None = None) -> None:
        self.scpi_many([command], delay=delay, timeout=timeout)

    def scpi_many(self, commands: list[str], delay: float = 0.05, timeout: float | None = None) -> None:
        if self._busy():
            self.log("A serial command or tool task is already running.\n")
            return
        self.serial_busy = True

        def worker() -> None:
            self.output_queue.put(("state", "busy"))
            try:
                session = ScpiSession(self.port_var.get(), timeout=0.8)
                try:
                    self.output_queue.put(("log", "\n[scpi]\n"))
                    for command in commands:
                        response = session.command(command, delay=delay, timeout=timeout)
                        self.output_queue.put(("log", f"{command} -> {response}\n"))
                finally:
                    session.close()
            except Exception as exc:
                self.output_queue.put(("log", f"[scpi] failed: {exc}\n"))
            finally:
                self.serial_busy = False
                self.output_queue.put(("state", "idle"))

        threading.Thread(target=worker, daemon=True).start()

    def run_command(self, label: str, command: list[str]) -> None:
        if self._busy():
            self.log("A serial command or tool task is already running.\n")
            return
        self.runner.run(label, command)

    def _busy(self) -> bool:
        return self.serial_busy or self.runner.busy()

    def _require_path(self, path_text: str, label: str) -> bool:
        if not path_text or not Path(path_text).exists():
            messagebox.showerror("Missing path", f"{label} does not exist:\n{path_text}")
            return False
        return True

    def _require_unified_package(self, path_text: str) -> bool:
        path = Path(path_text)
        try:
            header = path.read_bytes()[:PACKAGE_HEADER_SIZE]
        except OSError as exc:
            messagebox.showerror("Package read failed", f"Cannot read OTA package:\n{path}\n\n{exc}")
            return False
        if len(header) < PACKAGE_HEADER_SIZE or int.from_bytes(header[0:4], byteorder="little") != PACKAGE_MAGIC:
            messagebox.showerror(
                "Invalid OTA package",
                "OTA operations in this GUI expect the unified package:\n"
                "DHRT100_UPDATE.pkg\n\n"
                f"Selected file is not a unified package:\n{path}",
            )
            return False
        return True

    def _poll_output(self) -> None:
        try:
            while True:
                kind, text = self.output_queue.get_nowait()
                if kind == "log":
                    self.log(text)
                elif kind == "state":
                    self.status_var.set("Running" if text == "busy" else "Idle")
                elif kind == "status":
                    command, response = text.split("\t", 1)
                    if command in self.status_labels:
                        self.status_labels[command].set(response or "<timeout>")
        except queue.Empty:
            pass
        self.root.after(80, self._poll_output)

    def log(self, text: str) -> None:
        self.output.insert(END, text)
        self.output.see(END)

    def clear_log(self) -> None:
        self.output.delete("1.0", END)


def main() -> int:
    root = Tk()
    style = ttk.Style(root)
    if "vista" in style.theme_names():
        style.theme_use("vista")
    app = ToolboxApp(root)
    root.protocol("WM_DELETE_WINDOW", lambda: on_close(root, app))
    root.mainloop()
    return 0


def on_close(root: Tk, app: ToolboxApp) -> None:
    if app.runner.busy():
        if not messagebox.askyesno("Quit", "A command is still running. Stop it and quit?"):
            return
        app.runner.stop()
    root.destroy()


if __name__ == "__main__":
    raise SystemExit(main())
