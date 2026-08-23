from __future__ import annotations

import sys

from tools.ota_boot_commit.ota_boot_commit import parse_args


def test_no_commit_mode_is_explicit(monkeypatch) -> None:
    monkeypatch.setattr(
        sys,
        "argv",
        ["ota_boot_commit.py", "COM8", "--no-commit"],
    )
    args = parse_args()
    assert args.no_commit is True
