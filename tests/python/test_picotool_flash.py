from __future__ import annotations

import argparse
import sys

from tools.picotool_flash.picotool_flash import (
    build_full_erase_args,
    build_full_erase_range_args,
    build_device_selection,
    bootsel_device_visible,
    parse_args,
    selected_application_device_visible,
)


def test_full_erase_keeps_bootsel_mounted() -> None:
    assert build_full_erase_args(["--ser", "839E1AE79EA20F31"]) == [
        "erase", "-a", "-F", "--ser", "839E1AE79EA20F31"
    ]


def test_full_erase_option_is_explicit(monkeypatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        ["picotool_flash.py", "candidate.uf2", "--full-erase", "--retries", "3"],
    )
    args = parse_args()
    assert args.full_erase is True
    assert args.retries == 3
    assert args.command_timeout == 180.0


def test_full_erase_range_uses_flash_geometry() -> None:
    assert build_full_erase_range_args(["--ser", "abc"], 0x01000000) == [
        "erase", "-r", "0x10000000", "0x11000000", "--ser", "abc", "-F"
    ]


def test_forced_load_fallback_requires_selected_application_device() -> None:
    output = (
        "No accessible RP-series devices in BOOTSEL mode were found\n"
        "RP2350 device appears to have a USB serial connection, not in BOOTSEL mode.\n"
    )
    assert selected_application_device_visible(output, "839E1AE79EA20F31")
    assert not selected_application_device_visible(output, None)
    assert not selected_application_device_visible("unrelated failure", "serial")


def test_bootsel_device_visible_detects_existing_rom_mode() -> None:
    assert bootsel_device_visible("boot type:              bootsel\n")
    assert not bootsel_device_visible(
        "appears to have a USB serial connection, not in BOOTSEL mode"
    )


def test_explicit_usb_selector_is_complete_and_stable() -> None:
    args = argparse.Namespace(serial_number=None, bus=2, address=59)
    assert build_device_selection(args) == ["--bus", "2", "--address", "59"]
