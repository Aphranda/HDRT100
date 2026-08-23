#!/usr/bin/env python3
"""Reset a running DHRT100 application through Pico SDK picotool.

This is deliberately distinct from the factory flashing tool: it never enters
BOOTSEL and never erases or loads Flash.  It is used by no-confirm/revert HIL
to create a real boot boundary while preserving the staged image and BCB.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PICOTOOL = (
    Path(os.environ.get("USERPROFILE", "")) /
    ".pico-sdk" / "picotool" / "2.2.0-a4" / "picotool" / "picotool.exe"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--picotool", type=Path, default=DEFAULT_PICOTOOL)
    parser.add_argument("--serial-number", required=True,
                        help="expected RP2350 unique serial")
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--reopen-timeout", type=float, default=15.0,
                        help="seconds to wait for the application USB device")
    parser.add_argument("--retries", type=int, default=2)
    parser.add_argument("--out", type=Path, help="UTF-8 transcript path")
    return parser.parse_args()


def run(picotool: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(picotool), *args], cwd=ROOT, text=True, encoding="utf-8",
        errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        check=False,
    )


def build_reboot_args(serial_number: str) -> list[str]:
    """Build an application reset command; ``-u`` is intentionally absent."""
    return ["reboot", "--ser", serial_number, "-f"]


def wait_for_application(picotool: Path, serial_number: str,
                         timeout_s: float, settle_s: float,
                         records: list[str]) -> bool:
    deadline = time.monotonic() + timeout_s
    selection = ["--ser", serial_number]
    while time.monotonic() < deadline:
        time.sleep(settle_s)
        final = run(picotool, ["info", "-a", *selection])
        records.append(f"$ picotool info -a {' '.join(selection)}\n{final.stdout}")
        if final.returncode == 0 or "USB serial" in final.stdout:
            return True
    return False


def main() -> int:
    args = parse_args()
    picotool = args.picotool if args.picotool.is_absolute() else ROOT / args.picotool
    if not picotool.is_file():
        raise SystemExit(f"picotool not found: {picotool}")

    records: list[str] = []
    selection = ["--ser", args.serial_number]
    info = run(picotool, ["info", "-a", *selection])
    records.append(f"$ picotool info -a {' '.join(selection)}\n{info.stdout}")
    if info.returncode != 0 and "USB serial" not in info.stdout:
        return finish(records, args.out, info.returncode)

    reboot_args = build_reboot_args(args.serial_number)
    for attempt in range(1, max(1, args.retries) + 1):
        reboot = run(picotool, reboot_args)
        records.append(
            f"$ picotool {' '.join(reboot_args)} attempt={attempt}\n{reboot.stdout}"
        )
        # Application reboot may close the USB transport before picotool can
        # print its usual acknowledgement.  A zero exit code plus successful
        # post-reset enumeration is the authoritative result; do not require
        # a textual acknowledgement from the disappearing device.  Never send
        # a second reset while waiting for re-enumeration: that would alter the
        # boot-attempt count under test.
        if reboot.returncode == 0:
            if wait_for_application(
                picotool, args.serial_number, args.reopen_timeout,
                args.settle, records
            ):
                return finish(records, args.out, 0)
        time.sleep(args.settle)
    return finish(records, args.out, 1)


def finish(records: list[str], output: Path | None, code: int) -> int:
    text = "\n\n".join(records) + "\n"
    print(text, end="")
    if output is not None:
        path = output if output.is_absolute() else ROOT / output
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
