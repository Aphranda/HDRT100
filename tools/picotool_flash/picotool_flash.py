#!/usr/bin/env python3
"""Closed-loop DHRT100 UF2 flashing through Pico SDK picotool."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PICOTOOL = (
    Path(os.environ.get("USERPROFILE", "")) /
    ".pico-sdk" / "picotool" / "2.2.0-a4" / "picotool" / "picotool.exe"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path, help="UF2 artifact to load")
    parser.add_argument("--picotool", type=Path, default=DEFAULT_PICOTOOL)
    parser.add_argument("--serial-number", help="expected RP2350 unique serial")
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument(
        "--command-timeout", type=float, default=180.0,
        help="seconds before a picotool subprocess is treated as stalled",
    )
    parser.add_argument("--retries", type=int, default=2)
    parser.add_argument(
        "--full-erase",
        action="store_true",
        help="erase the complete external flash and keep BOOTSEL mounted before load",
    )
    parser.add_argument(
        "--flash-size",
        type=lambda value: int(value, 0),
        help="known external flash size in bytes; fallback for picotool -a size probe",
    )
    parser.add_argument("--out", type=Path, help="UTF-8 transcript path")
    return parser.parse_args()


def run(picotool: Path, args: list[str], timeout_s: float) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            [str(picotool), *args], cwd=ROOT, text=True, encoding="utf-8",
            errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False, timeout=timeout_s,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        return subprocess.CompletedProcess(
            [str(picotool), *args], 124, stdout=f"{output}[picotool timeout after {timeout_s}s]\n"
        )


def build_full_erase_args(selection: list[str]) -> list[str]:
    """Return the destructive erase command used by the factory workflow."""
    return ["erase", "-a", "-F", *selection]


def build_full_erase_range_args(selection: list[str], flash_size: int) -> list[str]:
    """Return a geometry-bound erase fallback when JEDEC size is unavailable."""
    if flash_size <= 0:
        raise ValueError("flash_size must be positive")
    xip_base = 0x10000000
    return [
        "erase", "-r", f"0x{xip_base:X}",
        f"0x{xip_base + flash_size:X}", *selection, "-F",
    ]


def selected_application_device_visible(output: str, serial_number: str | None) -> bool:
    """Accept forced load fallback only for an explicitly selected device."""
    return bool(
        serial_number and
        "appears to have a USB serial" in output and
        "No accessible RP-series devices" in output
    )


def bootsel_device_visible(output: str) -> bool:
    """Return true when the selected probe is already in ROM BOOTSEL."""
    normalized = output.lower()
    return "boot type:" in normalized and "bootsel" in normalized


def main() -> int:
    args = parse_args()
    artifact = args.artifact if args.artifact.is_absolute() else ROOT / args.artifact
    picotool = args.picotool if args.picotool.is_absolute() else ROOT / args.picotool
    if not artifact.is_file():
        raise SystemExit(f"artifact not found: {artifact}")
    if not picotool.is_file():
        raise SystemExit(f"picotool not found: {picotool}")

    records: list[str] = []
    selection = ["--ser", args.serial_number] if args.serial_number else []
    info = run(picotool, ["info", "-a", *selection], args.command_timeout)
    records.append(f"$ picotool info -a {' '.join(selection)}\n{info.stdout}")
    if info.returncode != 0 and "USB serial" not in info.stdout:
        return finish(records, args.out, info.returncode)

    if bootsel_device_visible(info.stdout):
        # Do not reboot an already mounted BOOTSEL device: a second reboot can
        # detach the USB target while the subsequent load is being prepared.
        bootsel_probe = info
        records.append("selected device is already in BOOTSEL; skipping reboot\n")
    else:
        reboot_args = ["reboot", *selection, "-f", "-u"]
        reboot = run(picotool, reboot_args, args.command_timeout)
        records.append(f"$ picotool {' '.join(reboot_args)}\n{reboot.stdout}")
        time.sleep(args.settle)

        # picotool may wait forever for the USB handle it just reset.  A
        # timeout is acceptable only when a fresh BOOTSEL probe succeeds;
        # otherwise the workflow remains fail-closed and does not erase.
        bootsel_probe = run(picotool, ["info", "-a", *selection], args.command_timeout)
        records.append(
            f"$ picotool info -a {' '.join(selection)} after reboot\n{bootsel_probe.stdout}"
        )
    app_fallback = selected_application_device_visible(
        bootsel_probe.stdout, args.serial_number
    )
    if bootsel_probe.returncode != 0 and not args.full_erase and not app_fallback:
        return finish(records, args.out, bootsel_probe.returncode)
    if bootsel_probe.returncode != 0:
        reason = (
            "the selected application device is still visible and load -f will "
            "perform the forced BOOTSEL transition"
            if app_fallback else
            "the explicit full-erase path uses picotool -F with the selected serial number"
        )
        records.append(f"BOOTSEL probe did not enumerate; continuing because {reason}.\n")

    if args.full_erase:
        # Keep the ROM BOOTSEL device mounted after erase.  A normal erase
        # reboots immediately; that would leave a blank device with no
        # application to drive the next force-reboot step and makes a failed
        # factory migration hard to diagnose.
        erase_args = build_full_erase_args(selection)
        erased = run(picotool, erase_args, args.command_timeout)
        records.append(f"$ picotool {' '.join(erase_args)}\n{erased.stdout}")
        if (erased.returncode != 0 and args.flash_size and
                "Cannot determine the flash size" in erased.stdout):
            range_args = build_full_erase_range_args(selection, args.flash_size)
            erased = run(picotool, range_args, args.command_timeout)
            records.append(f"$ picotool {' '.join(range_args)}\n{erased.stdout}")
        if erased.returncode != 0:
            return finish(records, args.out, erased.returncode)

    load_args = ["load", *selection, "-f", "-v", "-x", str(artifact)]
    for attempt in range(1, max(1, args.retries) + 1):
        loaded = run(picotool, load_args, args.command_timeout)
        records.append(f"$ picotool {' '.join(load_args)} attempt={attempt}\n{loaded.stdout}")
        if loaded.returncode == 0 and "The device was rebooted to start the application." in loaded.stdout:
            final = run(picotool, ["info", "-a", *selection], args.command_timeout)
            records.append(f"$ picotool info -a {' '.join(selection)}\n{final.stdout}")
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
