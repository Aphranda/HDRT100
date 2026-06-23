"""Run the RP2350_TRIG OTA board validation loop.

The runner keeps every step output in a validation directory and judges pass/fail
from those files, so a failed bench run can be reviewed after the serial output
has scrolled away.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover - bench dependency
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]


QUERY_COMMANDS = [
    "*IDN?",
    "SYST:FW:BUILD?",
    "SYST:BOOT:VERS?",
    "SYST:BOOT:CAP?",
    "SYST:OTA:MODE?",
    "SYST:OTA:STAT?",
    "SYST:OTA:SLOT?",
    "SYST:OTA:RES?",
    "SYST:OTA:TXN?",
]


NEGATIVE_TESTS = [
    ("transport-crc", ["--corrupt-crc"], "CRC"),
    ("image-crc", ["--package-negative", "image-crc"], "CRC"),
    ("image-vector", ["--package-negative", "image-vector"], "VECTOR"),
    ("header-magic", ["--package-negative", "header-magic"], "BAD_HEADER"),
    ("header-version", ["--package-negative", "header-version"], "BAD_HEADER"),
    ("header-size", ["--package-negative", "header-size"], "BAD_HEADER"),
    ("slot", ["--package-negative", "slot"], "BAD_HEADER"),
    ("run-offset", ["--package-negative", "run-offset"], "IMAGE_TOO_LARGE"),
]


def parse_args() -> argparse.Namespace:
    default_picotool = (
        Path(os.environ.get("USERPROFILE", ""))
        / ".pico-sdk"
        / "picotool"
        / "2.2.0-a4"
        / "picotool"
        / "picotool.exe"
    )
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, for example COM4")
    parser.add_argument("build_dir", type=Path, help="CMake build directory")
    parser.add_argument("--preset", default="pico2-release", help="release_check preset")
    parser.add_argument("--factory", type=Path, help="factory UF2 path")
    parser.add_argument("--package", type=Path, help="unified OTA package path")
    parser.add_argument("--picotool", type=Path, default=default_picotool)
    parser.add_argument("--begin-timeout", type=float, default=90.0)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--skip-flash", action="store_true")
    parser.add_argument("--skip-release-check", action="store_true")
    parser.add_argument("--skip-negative", action="store_true")
    parser.add_argument("--keep-going", action="store_true", help="continue after a failed negative case")
    parser.add_argument("--out-dir", type=Path, help="validation output directory")
    return parser.parse_args()


def resolve_build_path(path: Path) -> Path:
    return path if path.is_absolute() else ROOT / path


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run_command(name: str, command: list[str], out_dir: Path, timeout: float | None = None) -> tuple[int, Path]:
    log_path = out_dir / "logs" / f"{name}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    started = datetime.now().isoformat(timespec="seconds")
    with log_path.open("w", encoding="utf-8", newline="\n") as log:
        log.write(f"$ {' '.join(command)}\n")
        log.write(f"started={started}\n\n")
        log.flush()
        process = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
        log.write(process.stdout)
        log.write(f"\nexit_code={process.returncode}\n")
    return process.returncode, log_path


def wait_for_serial(port: str, timeout_s: float = 15.0) -> None:
    deadline = time.time() + timeout_s
    last_error: Exception | None = None
    while time.time() < deadline:
        try:
            with serial.Serial(port, 115200, timeout=0.1):
                return
        except Exception as exc:  # pragma: no cover - hardware timing
            last_error = exc
            time.sleep(0.25)
    raise RuntimeError(f"{port} did not become available: {last_error}")


def read_response(ser: serial.Serial, timeout_s: float = 2.0) -> str:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", errors="replace").strip()
        if not text or text.startswith("[") or text == "OK":
            continue
        return text
    return "<timeout>"


def query_serial(port: str, commands: Iterable[str], output: Path) -> dict[str, str]:
    wait_for_serial(port)
    results: dict[str, str] = {}
    with serial.Serial(port, 115200, timeout=0.1) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        for command in commands:
            ser.write((command + "\n").encode("utf-8"))
            ser.flush()
            results[command] = read_response(ser)
            time.sleep(0.1)
    lines = [f"{command} -> {response}" for command, response in results.items()]
    write_text(output, "\n".join(lines) + "\n")
    return results


def run_boot_commit(port: str, output: Path) -> dict[str, str]:
    lines: list[str] = []
    wait_for_serial(port)
    with serial.Serial(port, 115200, timeout=0.1) as ser:
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.write(b"SYST:OTA:BOOT\n")
        ser.flush()
        lines.append(f"SYST:OTA:BOOT -> {read_response(ser, 2.0)}")

    time.sleep(4.0)
    wait_for_serial(port)
    results: dict[str, str] = {}
    with serial.Serial(port, 115200, timeout=0.1) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        for command in ["SYST:FW:BUILD?", "SYST:OTA:RES?", "SYST:OTA:TXN?", "SYST:OTA:STAT?", "SYST:OTA:SLOT?"]:
            ser.write((command + "\n").encode("utf-8"))
            ser.flush()
            results[command] = read_response(ser)
            lines.append(f"{command} -> {results[command]}")
            time.sleep(0.1)
        ser.write(b"SYST:OTA:COMM\n")
        ser.flush()
        results["SYST:OTA:COMM"] = read_response(ser)
        lines.append(f"SYST:OTA:COMM -> {results['SYST:OTA:COMM']}")
        time.sleep(0.2)
        for command in ["SYST:OTA:STAT?", "SYST:OTA:SLOT?", "SYST:OTA:RES?", "SYST:OTA:TXN?"]:
            ser.write((command + "\n").encode("utf-8"))
            ser.flush()
            results[f"after:{command}"] = read_response(ser)
            lines.append(f"after {command} -> {results[f'after:{command}']}")
            time.sleep(0.1)

    write_text(output, "\n".join(lines) + "\n")
    return results


def contains_file(path: Path, *needles: str) -> bool:
    text = path.read_text(encoding="utf-8", errors="replace")
    return all(needle in text for needle in needles)


def append_step(summary: dict, name: str, passed: bool, artifact: Path | None = None, detail: str = "") -> None:
    summary["steps"].append(
        {
            "name": name,
            "passed": passed,
            "artifact": str(artifact.relative_to(summary["out_dir"])) if artifact else None,
            "detail": detail,
        }
    )


def main() -> int:
    args = parse_args()
    build_dir = resolve_build_path(args.build_dir)
    factory = resolve_build_path(args.factory) if args.factory else build_dir / "RP2350_TRIG_FACTORY.uf2"
    package = resolve_build_path(args.package) if args.package else build_dir / "RP2350_TRIG_UPDATE.pkg"
    out_dir = (
        resolve_build_path(args.out_dir)
        if args.out_dir
        else build_dir / f"ota_validation_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    summary: dict = {
        "out_dir": out_dir,
        "port": args.port,
        "build_dir": str(build_dir),
        "factory": str(factory),
        "package": str(package),
        "steps": [],
    }

    failed = False

    if not args.skip_release_check:
        code, log = run_command(
            "release_check",
            [sys.executable, "tools/release_check/release_check.py", "--preset", args.preset, "--build-dir", str(build_dir)],
            out_dir,
            timeout=120.0,
        )
        passed = code == 0 and contains_file(log, "release_check=OK")
        append_step(summary, "release_check", passed, log)
        failed = failed or not passed
        if failed and not args.keep_going:
            return finish(summary, out_dir)

    if not args.skip_flash:
        code, log = run_command(
            "flash_factory",
            [str(args.picotool), "load", "-f", "-v", "-x", str(factory)],
            out_dir,
            timeout=120.0,
        )
        passed = code == 0 and contains_file(log, "The device was rebooted to start the application.")
        append_step(summary, "flash_factory", passed, log)
        failed = failed or not passed
        if failed and not args.keep_going:
            return finish(summary, out_dir)

    baseline_path = out_dir / "baseline.txt"
    baseline = query_serial(args.port, QUERY_COMMANDS, baseline_path)
    baseline_passed = (
        baseline.get("SYST:OTA:MODE?") == '"COPY_TO_ACTIVE",0'
        and baseline.get("SYST:OTA:TXN?") == "0,0,0,0,0,0,0,0"
        and (args.skip_flash or baseline.get("SYST:OTA:STAT?") == '"IDLE",2,"NONE",0')
    )
    append_step(summary, "baseline_query", baseline_passed, baseline_path)
    failed = failed or not baseline_passed
    if failed and not args.keep_going:
        return finish(summary, out_dir)

    code, positive_log = run_command(
        "positive_ota",
        [
            sys.executable,
            "tools/ota_send/ota_send.py",
            args.port,
            str(package),
            "--begin-timeout",
            str(args.begin_timeout),
            "--timeout",
            str(args.timeout),
            "--expect-final-state",
            "READY_TO_REBOOT",
        ],
        out_dir,
        timeout=240.0,
    )
    positive_passed = code == 0 and contains_file(positive_log, '"READY_TO_REBOOT",2,"NONE",2')
    append_step(summary, "positive_ota", positive_passed, positive_log)
    failed = failed or not positive_passed
    if failed and not args.keep_going:
        return finish(summary, out_dir)

    boot_path = out_dir / "boot_commit.txt"
    boot_results = run_boot_commit(args.port, boot_path)
    boot_passed = (
        '"APPLIED"' in boot_results.get("SYST:OTA:RES?", "")
        and boot_results.get("after:SYST:OTA:STAT?") == '"COMMITTED",2,"NONE",5'
        and boot_results.get("after:SYST:OTA:TXN?") == "0,0,0,0,0,0,0,0"
    )
    append_step(summary, "boot_commit", boot_passed, boot_path)
    failed = failed or not boot_passed
    if failed and not args.keep_going:
        return finish(summary, out_dir)

    if not args.skip_negative:
        for name, extra_args, expected_error in NEGATIVE_TESTS:
            code, log = run_command(
                f"negative_{name}",
                [
                    sys.executable,
                    "tools/ota_send/ota_send.py",
                    args.port,
                    str(package),
                    *extra_args,
                    "--begin-timeout",
                    str(args.begin_timeout),
                    "--timeout",
                    str(args.timeout),
                    "--expect-final-state",
                    "FAILED",
                    "--expect-error",
                    expected_error,
                ],
                out_dir,
                timeout=240.0,
            )
            passed = code == 0 and contains_file(log, '"FAILED",2,', f'"{expected_error}"')
            append_step(summary, f"negative_{name}", passed, log)
            failed = failed or not passed
            if failed and not args.keep_going:
                return finish(summary, out_dir)

    final_path = out_dir / "final.txt"
    final = query_serial(args.port, ["SYST:FW:BUILD?", "SYST:OTA:STAT?", "SYST:OTA:SLOT?", "SYST:OTA:RES?", "SYST:OTA:TXN?", "SYST:OTA:MODE?"], final_path)
    expected_final_stat = '"FAILED",2,"IMAGE_TOO_LARGE",4' if not args.skip_negative else '"COMMITTED",2,"NONE",5'
    final_passed = (
        final.get("SYST:OTA:SLOT?") == "1,0,1,0,0"
        and final.get("SYST:OTA:TXN?") == "0,0,0,0,0,0,0,0"
        and final.get("SYST:OTA:MODE?") == '"COPY_TO_ACTIVE",0'
        and final.get("SYST:OTA:STAT?") == expected_final_stat
    )
    append_step(summary, "final_safe_state", final_passed, final_path)
    failed = failed or not final_passed
    summary["passed"] = not failed
    return finish(summary, out_dir)


def finish(summary: dict, out_dir: Path) -> int:
    summary.setdefault("passed", all(step["passed"] for step in summary["steps"]))
    serializable = dict(summary)
    serializable["out_dir"] = str(out_dir)
    write_text(out_dir / "summary.json", json.dumps(serializable, ensure_ascii=False, indent=2) + "\n")
    write_text(
        out_dir / "summary.txt",
        "\n".join(
            [f"passed={serializable['passed']}", f"out_dir={out_dir}"]
            + [f"{step['name']}: {'PASS' if step['passed'] else 'FAIL'} {step.get('artifact') or ''}" for step in summary["steps"]]
        )
        + "\n",
    )
    print((out_dir / "summary.txt").read_text(encoding="utf-8"), end="")
    return 0 if serializable["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
