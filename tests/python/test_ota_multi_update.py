from argparse import Namespace
import io
from pathlib import Path
from unittest.mock import patch

import pytest

from tools.ota_multi_update.ota_multi_update import (
    BoardProbe,
    ROOT,
    console_print,
    console_safe_text,
    parse_args,
    parse_stream_capability,
    resolved_transport,
    run_child,
    update_board,
    validate_cli_args,
)


def make_args(**overrides):
    args = {
        "send_only": False,
        "commit_only": False,
        "block_size": 512,
        "transport": "auto",
        "progress_every": 16,
        "max_workers": 0,
        "expected_board_count": 4,
        "serial_number": ["A", "B", "C", "D"],
        "baud": 115200,
        "map_version": None,
        "timeout": 3.0,
        "begin_timeout": 90.0,
        "reopen_timeout": 30.0,
        "settle": 1.0,
        "boot_wait": 3.0,
    }
    args.update(overrides)
    return Namespace(**args)


def test_four_board_all_worker_configuration_is_valid():
    validate_cli_args(make_args())


@pytest.mark.parametrize("block_size", [0, 513, 768, 4097])
def test_invalid_block_size_fails_before_workers(block_size):
    with pytest.raises(ValueError, match="block size"):
        validate_cli_args(make_args(block_size=block_size))


@pytest.mark.parametrize("block_size", [256, 512, 1024, 2048, 4096])
def test_supported_block_sizes_pass_before_workers(block_size):
    validate_cli_args(make_args(block_size=block_size))


def test_probe_capability_uses_three_field_live_contract():
    assert parse_stream_capability("4096,3,2") == (4096, 3, 2)
    assert parse_stream_capability("256,3,2") == (256, 3, 2)
    with pytest.raises(ValueError, match="invalid OTA stream capability"):
        parse_stream_capability("4096,3")


def test_duplicate_idn_address_is_rejected():
    with pytest.raises(ValueError, match="must be unique"):
        validate_cli_args(make_args(serial_number=["A", "B", "B", "D"]))


def test_invalid_progress_interval_is_rejected():
    with pytest.raises(ValueError, match="progress-every"):
        validate_cli_args(make_args(progress_every=0))


@pytest.mark.parametrize("count", [0, 9])
def test_expected_board_count_is_limited_to_product_ring(count):
    with pytest.raises(ValueError, match="expected-board-count"):
        validate_cli_args(make_args(expected_board_count=count))


def test_child_output_is_utf8_and_none_safe(tmp_path: Path):
    completed = Namespace(returncode=1, stdout=None, stderr=None)
    with patch("tools.ota_multi_update.ota_multi_update.subprocess.run",
               return_value=completed) as mocked:
        result = run_child("COM1", "step", ["child"], tmp_path)
    assert result.passed is False
    assert result.stdout == ""
    assert result.stderr == ""
    assert (tmp_path / "COM1" / "step.stdout.txt").read_text(
        encoding="utf-8") == ""
    assert mocked.call_args.kwargs["encoding"] == "utf-8"
    assert mocked.call_args.kwargs["errors"] == "replace"


def test_verbose_console_output_is_safe_for_windows_gbk():
    stream = io.TextIOWrapper(io.BytesIO(), encoding="gbk")
    rendered = console_safe_text("reset: \ufffd", stream)
    rendered.encode("gbk")
    assert rendered == "reset: ?"


def test_console_print_is_safe_for_windows_gbk():
    raw = io.BytesIO()
    stream = io.TextIOWrapper(raw, encoding="gbk")
    console_print("idn=设备\U0001f4a5", file=stream)
    stream.flush()
    assert raw.getvalue().decode("gbk") == "idn=设备?\r\n"


def test_default_transport_auto_uses_v1_legacy(monkeypatch, tmp_path: Path):
    image = tmp_path / "image.pkg"
    image.write_bytes(b"pkg")
    monkeypatch.setattr("sys.argv", ["ota_multi_update.py", str(image)])
    args = parse_args()
    assert args.transport == "auto"
    assert args.progress_every == 16
    assert resolved_transport(args) == "legacy"


def test_auto_transport_uses_legacy_when_old_firmware_has_no_capability():
    board = BoardProbe(
        "COM7",
        "GTS,DHRT100,839E1AE79EA20F31,0.1.0",
        "old",
        "839E1AE79EA20F31",
        "",
        "",
        map_version=2,
        capability_reported=False,
    )
    assert resolved_transport(make_args(), board) == "legacy"


def test_stream_update_uses_one_combined_sender(tmp_path: Path):
    board = BoardProbe("COM7", "GTS,DHRT100,NO5,0.1.0", "old", "NO5", "", "")
    image = tmp_path / "image.pkg"
    header = bytearray(512)
    header[0:4] = (0x474B5054).to_bytes(4, "little")
    header[112:126] = b"20260903131732"
    image.write_bytes(header)
    args = make_args(send_only=False, commit_only=False, map_version=2)

    with patch("tools.ota_multi_update.ota_multi_update.run_child") as mocked:
        mocked.return_value = Namespace(passed=True)
        result = update_board(args, board, image, "20260903131732", tmp_path)

    assert result.passed
    assert mocked.call_count == 1
    command = mocked.call_args.args[2]
    assert str(ROOT / "tools/ota_stream_send/ota_stream_send.py") in command
    assert "--package" in command
    assert "--boot-and-commit" in command
    assert command[command.index("--expected-build") + 1] == "20260903131732"


def test_stream_send_only_omits_boot_and_commit(tmp_path: Path):
    board = BoardProbe("COM7", "GTS,DHRT100,NO5,0.1.0", "old", "NO5", "", "")
    image = tmp_path / "image.bin"
    image.write_bytes(b"raw")
    args = make_args(send_only=True, commit_only=False)

    with patch("tools.ota_multi_update.ota_multi_update.run_child") as mocked:
        mocked.return_value = Namespace(passed=True)
        update_board(args, board, image, "", tmp_path)

    command = mocked.call_args.args[2]
    assert "--boot-and-commit" not in command
    assert "--package" not in command


def test_legacy_transport_keeps_separate_send_and_commit(tmp_path: Path):
    board = BoardProbe("COM7", "GTS,DHRT100,NO5,0.1.0", "old", "NO5", "", "")
    image = tmp_path / "image.pkg"
    image.write_bytes(b"pkg")
    args = make_args(transport="legacy", send_only=False, commit_only=False)

    with patch("tools.ota_multi_update.ota_multi_update.run_child") as mocked:
        mocked.return_value = Namespace(passed=True)
        update_board(args, board, image, "build", tmp_path)

    assert mocked.call_count == 2
    assert "ota_send.py" in mocked.call_args_list[0].args[2][1]
    assert "ota_boot_commit.py" in mocked.call_args_list[1].args[2][1]
