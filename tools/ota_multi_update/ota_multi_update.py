#!/usr/bin/env python3
"""Discover DHRT100 boards and update them in parallel over USB CDC."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.scpi_common.scpi_serial import read_scpi_response  # noqa: E402


PACKAGE_MAGIC = 0x474B5054
PACKAGE_HEADER_SIZE = 512
PACKAGE_BUILD_ID_OFFSET = 112
PACKAGE_BUILD_ID_SIZE = 32
DEFAULT_IDN_FILTERS = ("DHRT100",)
MAX_BOARD_COUNT = 8
MAX_STREAM_BLOCK_SIZE = 4096
MAX_LEGACY_BLOCK_SIZE = 512
OTA_BLOCK_SIZE_GRANULARITY = 256
SUPPORTED_BLOCK_SIZES = (256, 512, 1024, 2048, 4096)
STREAM_REQUIRED_CAPABILITIES = (1 << 0) | (1 << 1)


@dataclass(frozen=True)
class BoardProbe:
    port: str
    idn: str
    build_id: str
    serial_number: str
    description: str
    hwid: str
    map_version: int = 0
    max_data_block_size: int = MAX_LEGACY_BLOCK_SIZE
    stream_capabilities: int = 0
    capability_reported: bool = False


@dataclass
class StepResult:
    port: str
    step: str
    passed: bool
    returncode: int
    command: list[str]
    stdout: str
    stderr: str
    elapsed_s: float


@dataclass
class BoardUpdateResult:
    board: BoardProbe
    send: StepResult | None
    commit: StepResult | None
    forced_continue: bool = False

    @property
    def passed(self) -> bool:
        send_passed = self.send is None or self.send.passed
        if self.forced_continue:
            send_passed = self.commit is not None and self.commit.passed
        return send_passed and (self.commit is None or self.commit.passed)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="unified OTA package or raw firmware image")
    parser.add_argument("--ports", nargs="*", help="optional explicit serial ports; default scans all COM ports")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--map-version", type=int, choices=(1, 2),
        help="deployed Flash map version passed to the stream sender",
    )
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--begin-timeout", type=float, default=90.0)
    parser.add_argument("--reopen-timeout", type=float, default=30.0)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--boot-wait", type=float, default=3.0)
    parser.add_argument("--block-size", type=int, default=MAX_STREAM_BLOCK_SIZE)
    parser.add_argument("--transport", choices=("auto", "stream", "legacy"),
                        default="auto",
                        help="auto uses legacy for v1 and stream for V2")
    parser.add_argument("--progress-every", type=int, default=16,
                        help="stream status query interval in blocks")
    parser.add_argument("--idn-filter", action="append",
                        help="substring accepted in *IDN?; may be repeated; default DHRT100")
    parser.add_argument("--serial-number", action="append",
                        help="exact *IDN? serial/address whitelist; may be repeated")
    parser.add_argument("--expected-build",
                        help="expected build after commit; default reads unified package build id")
    parser.add_argument("--max-workers", type=int, default=0,
                        help="parallel update workers; 0 means one worker per discovered board")
    parser.add_argument("--expected-board-count", type=int,
                        help="require this many unique *IDN? boards before any write (1..8)")
    parser.add_argument("--send-only", action="store_true", help="send OTA payload but do not boot/commit")
    parser.add_argument("--commit-only", action="store_true", help="skip send and only boot/commit pending image")
    parser.add_argument("--dry-run", action="store_true", help="only discover boards and print the update plan")
    parser.add_argument(
        "--diagnostic-continue", action="store_true",
        help=("debug only: retain a failed legacy sender result, verify the "
              "post-reset build/state, and continue when that bounded check passes"),
    )
    parser.add_argument("--verbose", action="store_true", help="print child tool stdout/stderr for each board")
    parser.add_argument("--out-dir", type=Path, help="output directory for summary and per-board logs")
    return parser.parse_args()


def validate_cli_args(args: argparse.Namespace) -> None:
    if args.send_only and args.commit_only:
        raise ValueError("--send-only and --commit-only are mutually exclusive")
    if args.block_size not in SUPPORTED_BLOCK_SIZES:
        raise ValueError(
            "block size must be one of "
            f"{', '.join(str(size) for size in SUPPORTED_BLOCK_SIZES)}")
    if args.progress_every <= 0:
        raise ValueError("progress-every must be positive")
    if args.max_workers < 0 or args.max_workers > MAX_BOARD_COUNT:
        raise ValueError(
            f"max-workers must be in range 0..{MAX_BOARD_COUNT}")
    if (args.expected_board_count is not None and
            (args.expected_board_count < 1 or
             args.expected_board_count > MAX_BOARD_COUNT)):
        raise ValueError(
            f"expected-board-count must be in range 1..{MAX_BOARD_COUNT}")
    serial_numbers = list(args.serial_number or [])
    if len(serial_numbers) > MAX_BOARD_COUNT:
        raise ValueError(
            f"at most {MAX_BOARD_COUNT} serial-number values are supported")
    if len(set(serial_numbers)) != len(serial_numbers):
        raise ValueError("serial-number values must be unique")


def resolved_transport(args: argparse.Namespace,
                       board: BoardProbe | None = None) -> str:
    if args.transport != "auto":
        return args.transport
    if board is not None and not board.capability_reported:
        # Older deployed firmware exposes the v2 catalog through the Flash
        # diagnostic query even while the live deployment remains v1.  Only
        # the OTA capability response identifies the live transport safely.
        return "stream" if args.map_version == 2 else "legacy"
    map_version = args.map_version
    if map_version is None and board is not None:
        map_version = board.map_version
    if map_version != 2:
        return "legacy"
    if board is None:
        return "stream"
    return (
        "stream"
        if (board.stream_capabilities & STREAM_REQUIRED_CAPABILITIES) ==
        STREAM_REQUIRED_CAPABILITIES
        else "legacy"
    )


def command(ser: serial.Serial, text: str, timeout_s: float) -> str:
    ser.reset_input_buffer()
    ser.write((text + "\n").encode("ascii"))
    ser.flush()
    return read_scpi_response(ser, text, timeout_s, require_match=True)


def read_package_build_id(path: Path) -> str:
    data = path.read_bytes()[:PACKAGE_HEADER_SIZE]
    if len(data) < PACKAGE_HEADER_SIZE:
        return ""
    if int.from_bytes(data[0:4], byteorder="little") != PACKAGE_MAGIC:
        return ""
    raw = data[PACKAGE_BUILD_ID_OFFSET:PACKAGE_BUILD_ID_OFFSET + PACKAGE_BUILD_ID_SIZE]
    text = raw.split(b"\x00", 1)[0].split(b"\xFF", 1)[0]
    return text.decode("ascii", errors="ignore").strip()


def parse_serial_from_idn(idn: str) -> str:
    fields = [field.strip().strip('"') for field in idn.split(",")]
    return fields[2] if len(fields) >= 3 else ""


def parse_stream_capability(response: str) -> tuple[int, int, int]:
    fields = tuple(int(field.strip(), 0) for field in response.split(","))
    if len(fields) != 3 or fields[0] not in SUPPORTED_BLOCK_SIZES or \
            fields[0] > MAX_STREAM_BLOCK_SIZE or fields[2] not in (1, 2):
        raise ValueError(f"invalid OTA stream capability: {response!r}")
    return fields  # type: ignore[return-value]


def explicit_ports(args: argparse.Namespace) -> list[str]:
    if not args.ports:
        return []
    ports: list[str] = []
    for value in args.ports:
        for item in value.split(","):
            port = item.strip()
            if port:
                ports.append(port)
    return ports


def candidate_ports(args: argparse.Namespace) -> list[tuple[str, str, str]]:
    requested = explicit_ports(args)
    discovered = {port.device: port for port in list_ports.comports()}
    if requested:
        return [
            (
                port,
                discovered.get(port).description if port in discovered else "",
                discovered.get(port).hwid if port in discovered else "",
            )
            for port in requested
        ]
    return [(port.device, port.description, port.hwid) for port in discovered.values()]


def probe_port(port: str,
               description: str,
               hwid: str,
               *,
               baud: int,
               timeout_s: float,
               idn_filters: tuple[str, ...]) -> BoardProbe | None:
    try:
        with serial.Serial(port, baud, timeout=0.1, write_timeout=timeout_s) as ser:
            time.sleep(0.2)
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            idn = command(ser, "*IDN?", timeout_s)
            if idn == "<timeout>" or not any(text in idn for text in idn_filters):
                return None
            build = command(ser, "SYSTem:FW:BUILD?", timeout_s).strip('"')
            map_version = 0
            try:
                map_version = int(command(
                    ser, "SYST:DIAG:FLASH:MAP? 0", timeout_s
                ).split(",", 1)[0].strip(), 0)
            except ValueError:
                pass
            max_data_block_size = MAX_LEGACY_BLOCK_SIZE
            stream_capabilities = 0
            capability_reported = False
            try:
                max_data_block_size, stream_capabilities, map_version = \
                    parse_stream_capability(command(
                        ser, "SYST:OTA:STREAM:CAP?", timeout_s))
                capability_reported = True
            except ValueError:
                pass
            return BoardProbe(
                port=port,
                idn=idn,
                build_id=build,
                serial_number=parse_serial_from_idn(idn),
                description=description,
                hwid=hwid,
                map_version=map_version,
                max_data_block_size=max_data_block_size,
                stream_capabilities=stream_capabilities,
                capability_reported=capability_reported,
            )
    except (OSError, serial.SerialException):
        return None


def discover_boards(args: argparse.Namespace,
                    timing: dict | None = None) -> list[BoardProbe]:
    discovery_started = time.monotonic()
    ports = candidate_ports(args)
    if timing is not None:
        timing["port_discovery_s"] = time.monotonic() - discovery_started
        timing["candidate_ports"] = [port for port, _, _ in ports]
    idn_filters = tuple(args.idn_filter or DEFAULT_IDN_FILTERS)
    if not ports:
        return []
    workers = min(len(ports), max(1, args.max_workers or len(ports)))
    boards: list[BoardProbe] = []
    def timed_probe(port: str, description: str,
                    hwid: str) -> tuple[BoardProbe | None, float]:
        started = time.monotonic()
        board = probe_port(
            port, description, hwid, baud=args.baud,
            timeout_s=args.timeout, idn_filters=idn_filters)
        return board, time.monotonic() - started

    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = {
            executor.submit(timed_probe, port, description, hwid): port
            for port, description, hwid in ports
        }
        probe_timings: list[dict[str, object]] = []
        for future in as_completed(futures):
            board, elapsed = future.result()
            probe_timings.append({
                "port": futures[future],
                "elapsed_s": elapsed,
                "matched": board is not None,
            })
            if board is not None:
                boards.append(board)
    if timing is not None:
        timing["serial_probes"] = sorted(
            probe_timings, key=lambda item: str(item["port"]))
        timing["serial_probe_wall_s"] = max(
            (float(item["elapsed_s"]) for item in probe_timings),
            default=0.0,
        )
    boards.sort(key=lambda item: item.port)
    return boards


def run_child(port: str, step: str, command_line: list[str], out_dir: Path) -> StepResult:
    started = time.monotonic()
    completed = subprocess.run(command_line,
                               cwd=ROOT,
                               text=True,
                               encoding="utf-8",
                               errors="replace",
                               capture_output=True)
    elapsed = time.monotonic() - started
    stdout = completed.stdout or ""
    stderr = completed.stderr or ""
    port_dir = out_dir / port
    port_dir.mkdir(parents=True, exist_ok=True)
    safe_step = step.replace("/", "_")
    (port_dir / f"{safe_step}.stdout.txt").write_text(stdout, encoding="utf-8")
    (port_dir / f"{safe_step}.stderr.txt").write_text(stderr, encoding="utf-8")
    return StepResult(
        port=port,
        step=step,
        passed=completed.returncode == 0,
        returncode=completed.returncode,
        command=command_line,
        stdout=stdout,
        stderr=stderr,
        elapsed_s=elapsed,
    )


def console_safe_text(value: str, stream: object = sys.stdout) -> str:
    """Replace characters unsupported by the active Windows console codec."""
    encoding = getattr(stream, "encoding", None) or "utf-8"
    return value.encode(encoding, errors="replace").decode(
        encoding, errors="replace")


def console_print(*values: object, file: object | None = None,
                  sep: str = " ", end: str = "\n", flush: bool = False) -> None:
    """Print text that is representable by the active console codec.

    ``print`` performs encoding only after formatting, so a single non-GBK
    character in an IDN or child diagnostic can otherwise abort ``--verbose``
    on Windows.  Convert the complete rendered line before handing it to the
    stream and keep the stream lookup dynamic for redirected output/tests.
    """
    if file is None:
        file = sys.stdout
    rendered = sep.join(str(value) for value in values)
    safe = console_safe_text(rendered, file)
    print(safe, file=file, end=end, flush=flush)


def update_board(args: argparse.Namespace,
                 board: BoardProbe,
                 image: Path,
                 expected_build: str,
                 out_dir: Path) -> BoardUpdateResult:
    send_result: StepResult | None = None
    commit_result: StepResult | None = None
    transport = resolved_transport(args, board)

    if not args.commit_only:
        if transport == "stream":
            send_cmd = [
                sys.executable,
                str(ROOT / "tools" / "ota_stream_send" / "ota_stream_send.py"),
                str(image),
                "--port", board.port,
                "--baud", str(args.baud),
                "--block-size", str(args.block_size),
                "--timeout", str(args.timeout),
                "--begin-timeout", str(args.begin_timeout),
                "--progress-every", str(args.progress_every),
                "--out-dir", str(out_dir / board.port / "ota_stream_send"),
            ]
            if args.map_version is not None:
                send_cmd.extend(["--map-version", str(args.map_version)])
            if read_package_build_id(image):
                send_cmd.append("--package")
            if expected_build:
                send_cmd.extend(["--expected-build", expected_build])
            if not args.send_only:
                send_cmd.append("--boot-and-commit")
            send_result = run_child(board.port, "ota_stream_send", send_cmd,
                                    out_dir)
        else:
            send_cmd = [
                sys.executable,
                str(ROOT / "tools" / "ota_send" / "ota_send.py"),
                board.port,
                str(image),
                "--baud", str(args.baud),
                "--block-size", str(args.block_size),
                "--timeout", str(args.timeout),
                "--begin-timeout", str(args.begin_timeout),
                "--expect-final-state", "READY_TO_REBOOT",
                "--out-dir", str(out_dir / board.port / "ota_send"),
            ]
            send_result = run_child(board.port, "ota_send", send_cmd, out_dir)
        if not send_result.passed:
            if (args.diagnostic_continue and transport == "legacy" and
                    not args.send_only):
                post_reset_cmd = [
                    sys.executable,
                    str(ROOT / "tools" / "ota_boot_commit" /
                        "ota_boot_commit.py"),
                    board.port,
                    "--baud", str(args.baud),
                    "--timeout", str(args.timeout),
                    "--reopen-timeout", str(args.reopen_timeout),
                    "--settle", str(args.settle),
                    "--skip-boot",
                    "--out-dir", str(
                        out_dir / board.port / "ota_post_reset_verify"),
                ]
                if expected_build:
                    post_reset_cmd.extend(["--expected-build", expected_build])
                post_reset_result = run_child(
                    board.port, "ota_post_reset_verify", post_reset_cmd,
                    out_dir)
                return BoardUpdateResult(
                    board=board, send=send_result,
                    commit=post_reset_result,
                    forced_continue=post_reset_result.passed)
            return BoardUpdateResult(board=board, send=send_result, commit=None)

        if transport == "stream":
            return BoardUpdateResult(board=board, send=send_result, commit=None)

    if not args.send_only:
        commit_cmd = [
            sys.executable,
            str(ROOT / "tools" / "ota_boot_commit" / "ota_boot_commit.py"),
            board.port,
            "--baud", str(args.baud),
            "--timeout", str(args.timeout),
            "--reopen-timeout", str(args.reopen_timeout),
            "--settle", str(args.settle),
            "--boot-wait", str(args.boot_wait),
            "--out-dir", str(out_dir / board.port / "ota_boot_commit"),
        ]
        if expected_build:
            commit_cmd.extend(["--expected-build", expected_build])
        commit_result = run_child(board.port, "ota_boot_commit", commit_cmd, out_dir)

    return BoardUpdateResult(board=board, send=send_result, commit=commit_result)


def write_summary(out_dir: Path,
                  boards: list[BoardProbe],
                  results: list[BoardUpdateResult],
                  *,
                  image: Path,
                  expected_build: str,
                  dry_run: bool,
                  elapsed_s: float,
                  timing: dict | None = None,
                  transport: str = "") -> None:
    passed = all(result.passed for result in results) if not dry_run else bool(boards)
    summary = {
        "passed": passed,
        "dry_run": dry_run,
        "image": str(image),
        "expected_build": expected_build,
        "board_count": len(boards),
        "updated_count": len(results),
        "failed_count": sum(0 if result.passed else 1 for result in results),
        "elapsed_s": elapsed_s,
        "transport": transport,
        "timing": timing or {},
        "boards": [asdict(board) for board in boards],
        "results": [
            {
                "board": asdict(result.board),
                "passed": result.passed,
                "forced_continue": result.forced_continue,
                "send": asdict(result.send) if result.send is not None else None,
                "commit": asdict(result.commit) if result.commit is not None else None,
            }
            for result in results
        ],
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    lines = [
        f"passed={passed}",
        f"dry_run={dry_run}",
        f"board_count={len(boards)}",
        f"updated_count={len(results)}",
        f"failed_count={summary['failed_count']}",
        f"expected_build={expected_build}",
        f"elapsed_s={elapsed_s:.3f}",
    ]
    for board in boards:
        lines.append(f"{board.port} serial={board.serial_number} build={board.build_id} idn={board.idn}")
    (out_dir / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    try:
        validate_cli_args(args)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    image = args.image.resolve()
    if not image.exists():
        raise SystemExit(f"image not found: {image}")

    out_dir = args.out_dir or (
        ROOT / "out" / "ota" /
        f"ota_multi_update_{datetime.now().strftime('%Y%m%d%H%M%S')}"
    )
    expected_build = args.expected_build or read_package_build_id(image)
    started = time.monotonic()

    discovery_timing: dict = {}
    boards = discover_boards(args, discovery_timing)
    transport = resolved_transport(args)
    wanted_serials = set(args.serial_number or [])
    if wanted_serials:
        boards = [board for board in boards
                  if board.serial_number in wanted_serials]
        missing_serials = wanted_serials - {
            board.serial_number for board in boards}
        if missing_serials:
            console_print("missing_serial_numbers=" + ",".join(sorted(missing_serials)),
                          file=sys.stderr)
            write_summary(out_dir, boards, [], image=image,
                          expected_build=expected_build,
                          dry_run=args.dry_run, elapsed_s=0.0,
                          timing=discovery_timing, transport=transport)
            return 2
    if len(boards) > MAX_BOARD_COUNT:
        console_print(f"too_many_boards={len(boards)} max={MAX_BOARD_COUNT}",
                      file=sys.stderr)
        write_summary(out_dir, boards, [], image=image,
                      expected_build=expected_build,
                      dry_run=args.dry_run, elapsed_s=0.0,
                      timing=discovery_timing, transport=transport)
        return 2
    if (args.expected_board_count is not None and
            len(boards) != args.expected_board_count):
        console_print(f"board_count_mismatch={len(boards)} "
                      f"expected={args.expected_board_count}", file=sys.stderr)
        write_summary(out_dir, boards, [], image=image,
                      expected_build=expected_build,
                      dry_run=args.dry_run, elapsed_s=0.0)
        return 2
    console_print(f"discovered_boards={len(boards)}")
    for board in boards:
        console_print(f"{board.port}: serial={board.serial_number} build={board.build_id} idn={board.idn}")
    if not boards:
        write_summary(out_dir, [], [], image=image, expected_build=expected_build, dry_run=args.dry_run, elapsed_s=0.0)
        return 2

    workers = min(len(boards), max(1, args.max_workers or len(boards)))
    console_print(f"workers={workers}")
    console_print(f"image={image}")
    console_print(f"expected_build={expected_build or '<not-checked>'}")
    if args.dry_run:
        elapsed = time.monotonic() - started
        write_summary(out_dir, boards, [], image=image,
                      expected_build=expected_build, dry_run=True,
                      elapsed_s=elapsed, timing=discovery_timing,
                      transport=transport)
        console_print(f"summary={out_dir}")
        return 0

    results: list[BoardUpdateResult] = []
    with ThreadPoolExecutor(max_workers=workers) as executor:
        future_map = {
            executor.submit(update_board, args, board, image, expected_build, out_dir): board
            for board in boards
        }
        for future in as_completed(future_map):
            result = future.result()
            results.append(result)
            status = "PASS" if result.passed else "FAIL"
            console_print(f"{status} {result.board.port} serial={result.board.serial_number}")
            if args.verbose:
                for step in (result.send, result.commit):
                    if step is None:
                        continue
                    console_print(f"--- {result.board.port} {step.step} stdout ---")
                    console_print(step.stdout.rstrip())
                    if step.stderr:
                        console_print(f"--- {result.board.port} {step.step} stderr ---",
                                      file=sys.stderr)
                        console_print(step.stderr.rstrip(), file=sys.stderr)

    results.sort(key=lambda item: item.board.port)
    elapsed = time.monotonic() - started
    write_summary(out_dir, boards, results, image=image,
                  expected_build=expected_build, dry_run=False,
                  elapsed_s=elapsed, timing=discovery_timing,
                  transport=transport)
    console_print(f"summary={out_dir}")
    return 0 if all(result.passed for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
