from __future__ import annotations

import sys

from tools.picotool_reboot.picotool_reboot import build_reboot_args, parse_args


def test_application_reboot_does_not_enter_bootsel() -> None:
    assert build_reboot_args("839E1AE79EA20F31") == [
        "reboot", "--ser", "839E1AE79EA20F31", "-f"
    ]


def test_reboot_parser_requires_serial_and_keeps_retry_policy(monkeypatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        ["picotool_reboot.py", "--serial-number", "839E1AE79EA20F31", "--retries", "3", "--reopen-timeout", "20"],
    )
    args = parse_args()
    assert args.serial_number == "839E1AE79EA20F31"
    assert args.retries == 3
    assert args.reopen_timeout == 20.0
