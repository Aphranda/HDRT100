#!/usr/bin/env python3
"""Build, OTA, and verify one DHRT100 board over USB CDC.

The hardware gates cover the four-channel SMA trigger loopback and the
single-board TDMA RJ45 loopback.  Connect OUT1..4 to IN1..4 and connect the
TDMA output RJ45 to the input RJ45 before running this tool.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit(
        "pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.ota_multi_update.ota_multi_update import (  # noqa: E402
    read_package_build_id,
)
from tools.scpi_common.scpi_serial import SerialSession  # noqa: E402


class AcceptanceError(RuntimeError):
    """Raised when a single-board acceptance gate fails."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--serial-number", required=True,
                        help="exact unique board serial from *IDN? field 3")
    parser.add_argument("--port", help="optional COM port used for initial discovery")
    parser.add_argument("--package", type=Path,
                        help="reuse an existing DHRT100_UPDATE.pkg and skip build")
    parser.add_argument("--build-dir", type=Path,
                        help="CMake build directory; default is timestamped")
    parser.add_argument("--out-dir", type=Path,
                        help="evidence directory; default is timestamped")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--reopen-timeout", type=float, default=30.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--block-size", type=int, default=4096,
                        choices=(256, 512, 1024, 2048, 4096))
    parser.add_argument("--sma-line-settle", type=float, default=0.05)
    parser.add_argument("--tdma-duration-s", type=float, default=15.0)
    parser.add_argument("--tdma-poll-interval-s", type=float, default=0.5)
    parser.add_argument("--tdma-operating-level", type=int, default=7)
    parser.add_argument("--tdma-node-count", type=int, default=2)
    parser.add_argument("--tdma-local-slot", type=int, default=0)
    parser.add_argument("--tdma-reference-slot", type=int, default=0)
    parser.add_argument("--tdma-probe-phase-cycles", type=int, default=10)
    parser.add_argument("--tdma-train-cycles", type=int, default=4096)
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def resolve_under_root(root: Path, value: Path) -> Path:
    return value.resolve() if value.is_absolute() else (root / value).resolve()


def console_safe_text(value: str, stream: object) -> str:
    encoding = getattr(stream, "encoding", None) or "utf-8"
    return value.encode(encoding, errors="replace").decode(
        encoding, errors="replace")


def run_step(command: list[str], root: Path, log_path: Path,
             *, verbose: bool) -> None:
    started = time.monotonic()
    completed = subprocess.run(
        command,
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    elapsed_s = time.monotonic() - started
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        "$ " + subprocess.list2cmdline(command) + "\n"
        f"returncode={completed.returncode}\n"
        f"elapsed_s={elapsed_s:.3f}\n\n"
        "[stdout]\n" + (completed.stdout or "") +
        "\n[stderr]\n" + (completed.stderr or ""),
        encoding="utf-8",
    )
    if verbose:
        if completed.stdout:
            print(console_safe_text(completed.stdout, sys.stdout), end="")
        if completed.stderr:
            print(console_safe_text(completed.stderr, sys.stderr),
                  end="", file=sys.stderr)
    if completed.returncode != 0:
        raise AcceptanceError(
            f"command failed ({completed.returncode}); see {log_path}")


def candidate_ports(preferred_port: str | None) -> list[str]:
    discovered = sorted({item.device for item in list_ports.comports()})
    if preferred_port is None:
        return discovered
    return [preferred_port] + [port for port in discovered
                               if port.casefold() != preferred_port.casefold()]


def query_identity(port: str, args: argparse.Namespace) -> dict[str, str]:
    with SerialSession(
            port, args.baud, args.timeout, args.settle) as session:
        return {
            "idn": session.execute("*IDN?").strip(),
            "board_no": session.execute("SYSTem:BOARD:NO?").strip(),
            "build_id": session.execute("SYSTem:FW:BUILD?").strip().strip('"'),
            "error": session.execute("SYSTem:ERR?").strip(),
        }


def find_board(args: argparse.Namespace) -> tuple[str, dict[str, str]]:
    deadline = time.monotonic() + args.reopen_timeout
    last_errors: dict[str, str] = {}
    while time.monotonic() < deadline:
        for port in candidate_ports(args.port):
            try:
                identity = query_identity(port, args)
            except Exception as exc:  # Serial ports can disappear during reboot.
                last_errors[port] = str(exc)
                continue
            fields = parse_idn(identity["idn"])
            if len(fields) >= 3 and fields[2] == args.serial_number:
                return port, identity
        time.sleep(0.5)
    detail = ", ".join(f"{port}: {error}"
                       for port, error in sorted(last_errors.items()))
    raise AcceptanceError(
        f"board {args.serial_number} did not reconnect"
        + (f" ({detail})" if detail else ""))


def parse_idn(value: str) -> list[str]:
    return [field.strip().strip('"') for field in value.split(",")]


def validate_identity(identity: dict[str, str], expected_serial: str,
                      expected_build: str) -> int:
    fields = parse_idn(identity["idn"])
    if len(fields) != 4:
        raise AcceptanceError(
            f"*IDN? must return four fields, got {identity['idn']!r}")
    try:
        board_no = int(identity["board_no"], 10)
    except ValueError as exc:
        raise AcceptanceError(
            f"invalid SYSTem:BOARD:NO? response: {identity['board_no']!r}") from exc
    if not 0 <= board_no <= 8:
        raise AcceptanceError(f"board NO out of range: {board_no}")
    if fields[0] != f"NO.{board_no}":
        raise AcceptanceError(
            f"*IDN? field 1 {fields[0]!r} does not match Flash NO.{board_no}")
    if fields[1] != "DHRT100":
        raise AcceptanceError(f"unexpected *IDN? model: {fields[1]!r}")
    if fields[2] != expected_serial:
        raise AcceptanceError(
            f"unexpected *IDN? serial: {fields[2]!r}")
    if identity["build_id"] != expected_build:
        raise AcceptanceError(
            f"live build {identity['build_id']!r} != package {expected_build!r}")
    if identity["error"] != '0,"No error"':
        raise AcceptanceError(
            f"SCPI error queue is not empty: {identity['error']!r}")
    return board_no


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_passed_summary(path: Path, label: str) -> dict[str, object]:
    if not path.is_file():
        raise AcceptanceError(f"{label} did not write summary.json")
    result = json.loads(path.read_text(encoding="utf-8"))
    if result.get("passed") is not True:
        failures = result.get("failures")
        detail = f": {failures}" if failures else ""
        raise AcceptanceError(f"{label} did not pass{detail}")
    return result


def main() -> int:
    args = parse_args()
    if args.sma_line_settle < 0.0:
        raise SystemExit("--sma-line-settle must be non-negative")
    if args.tdma_duration_s <= 0.0 or args.tdma_poll_interval_s <= 0.0:
        raise SystemExit("TDMA duration and poll interval must be positive")
    if args.tdma_operating_level < 0:
        raise SystemExit("--tdma-operating-level must be non-negative")
    if args.tdma_node_count < 2:
        raise SystemExit("--tdma-node-count must be at least 2")
    if not 0 <= args.tdma_local_slot < args.tdma_node_count:
        raise SystemExit("--tdma-local-slot must be within the logical topology")
    if not 0 <= args.tdma_reference_slot < args.tdma_node_count:
        raise SystemExit(
            "--tdma-reference-slot must be within the logical topology")
    if not 1 <= args.tdma_probe_phase_cycles <= 31:
        raise SystemExit("--tdma-probe-phase-cycles must be in [1, 31]")
    if (args.tdma_train_cycles < 0 or args.tdma_train_cycles > 65536 or
            (args.tdma_train_cycles != 0 and
             args.tdma_train_cycles % 8 != 0)):
        raise SystemExit(
            "--tdma-train-cycles must be 0 or an 8-cycle multiple up to 65536")
    root = args.root.resolve()
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = resolve_under_root(
        root,
        args.out_dir or Path(
            f"out/HardwareAcceptance/{stamp[:8]}/single-board-{stamp[9:]}")
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    summary_path = out_dir / "summary.json"
    summary: dict[str, object] = {
        "schema": "DHRT100_SINGLE_BOARD_HARDWARE_ACCEPTANCE_V2",
        "passed": False,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "serial_number": args.serial_number,
        "required_wiring": {
            "sma_trigger": [
                "OUT1->IN1", "OUT2->IN2", "OUT3->IN3", "OUT4->IN4",
            ],
            "tdma": "output RJ45 -> input RJ45",
        },
    }

    try:
        if args.package is None:
            build_dir = resolve_under_root(
                root,
                args.build_dir or Path(f"out/build/single-board-{stamp}"),
            )
            print(f"Single-board acceptance: build -> {build_dir}", flush=True)
            run_step([
                sys.executable,
                str(root / "tools/cmake_build_auto/cmake_build_auto.py"),
                "--root", str(root),
                "--build-dir", str(build_dir),
            ], root, out_dir / "build.log", verbose=args.verbose)
            package = build_dir / "DHRT100_UPDATE.pkg"
        else:
            package = resolve_under_root(root, args.package)
            build_dir = package.parent
        if not package.is_file():
            raise AcceptanceError(f"firmware package missing: {package}")

        build_id = read_package_build_id(package)
        if not build_id:
            raise AcceptanceError(f"package has no valid build ID: {package}")
        ota_dir = out_dir / "ota"
        print(
            f"Single-board acceptance: OTA serial={args.serial_number} "
            f"build={build_id}",
            flush=True,
        )
        ota_command = [
            sys.executable,
            str(root / "tools/ota_multi_update/ota_multi_update.py"),
            str(package),
            "--expected-board-count", "1",
            "--serial-number", args.serial_number,
            "--expected-build", build_id,
            "--out-dir", str(ota_dir),
            "--block-size", str(args.block_size),
            "--baud", str(args.baud),
            "--timeout", str(args.timeout),
            "--reopen-timeout", str(args.reopen_timeout),
            "--settle", str(args.settle),
        ]
        if args.port:
            ota_command.extend(["--ports", args.port])
        if args.verbose:
            ota_command.append("--verbose")
        run_step(ota_command, root, out_dir / "ota.log", verbose=args.verbose)

        ota_summary_path = ota_dir / "summary.json"
        ota_summary = json.loads(ota_summary_path.read_text(encoding="utf-8"))
        if ota_summary.get("passed") is not True or \
                ota_summary.get("board_count") != 1:
            raise AcceptanceError("single-board OTA summary did not pass")

        port, initial_identity = find_board(args)
        board_no = validate_identity(
            initial_identity, args.serial_number, build_id)

        sma_dir = out_dir / "sma-trigger-loopback"
        sma_summary_path = sma_dir / "summary.json"
        summary["sma_trigger_summary"] = str(sma_summary_path)
        print("Single-board acceptance: SMA trigger loopback OUT1..4 -> IN1..4",
              flush=True)
        run_step([
            sys.executable,
            str(root / "tools/sma_loopback_validate/sma_single_board_loopback.py"),
            port,
            "--baud", str(args.baud),
            "--timeout", str(args.timeout),
            "--settle", str(args.settle),
            "--line-settle", str(args.sma_line_settle),
            "--expected-build", build_id,
            "--out-dir", str(sma_dir),
        ], root, out_dir / "sma-trigger-loopback.log", verbose=args.verbose)
        sma_summary = load_passed_summary(
            sma_summary_path, "SMA trigger loopback")

        tdma_dir = out_dir / "tdma-single-board-loopback"
        tdma_summary_path = tdma_dir / "summary.json"
        summary["tdma_loopback_summary"] = str(tdma_summary_path)
        print("Single-board acceptance: TDMA RJ45 loopback", flush=True)
        run_step([
            sys.executable,
            str(root / "tools/tdma_ring_monitor/tdma_single_board_loopback.py"),
            port,
            "--baud", str(args.baud),
            "--timeout", str(args.timeout),
            "--settle", str(args.settle),
            "--duration-s", str(args.tdma_duration_s),
            "--poll-interval-s", str(args.tdma_poll_interval_s),
            "--expected-build", build_id,
            "--operating-level", str(args.tdma_operating_level),
            "--node-count", str(args.tdma_node_count),
            "--local-slot", str(args.tdma_local_slot),
            "--reference-slot", str(args.tdma_reference_slot),
            "--probe-phase-cycles", str(args.tdma_probe_phase_cycles),
            "--train-cycles", str(args.tdma_train_cycles),
            "--out-dir", str(tdma_dir),
        ], root, out_dir / "tdma-single-board-loopback.log",
           verbose=args.verbose)
        tdma_summary = load_passed_summary(
            tdma_summary_path, "TDMA single-board loopback")

        final_port, identity = find_board(args)
        final_board_no = validate_identity(
            identity, args.serial_number, build_id)
        if final_board_no != board_no:
            raise AcceptanceError(
                f"board NO changed during acceptance: {board_no} -> {final_board_no}")
        summary.update({
            "passed": True,
            "port": final_port,
            "board_no": board_no,
            "idn": identity["idn"],
            "build_id": build_id,
            "scpi_error": identity["error"],
            "sma_trigger_passed": True,
            "sma_trigger_step_count": len(sma_summary.get("steps", [])),
            "tdma_loopback_passed": True,
            "tdma_counter_deltas": tdma_summary.get("counter_deltas", {}),
            "firmware_package": str(package),
            "firmware_package_sha256": sha256_file(package),
            "build_dir": str(build_dir),
            "ota_summary": str(ota_summary_path),
        })
        print(
            f"PASS single-board acceptance port={final_port} "
            f"NO.{board_no} build={build_id} idn={identity['idn']}",
            flush=True,
        )
        return_code = 0
    except (AcceptanceError, OSError, ValueError, KeyError,
            json.JSONDecodeError, subprocess.SubprocessError) as exc:
        summary["error"] = str(exc)
        print(f"FAIL single-board acceptance: {exc}", file=sys.stderr)
        return_code = 1
    finally:
        summary_path.write_text(
            json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"summary={summary_path}", flush=True)
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
