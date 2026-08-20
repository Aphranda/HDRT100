from argparse import Namespace

import pytest

from tools.ota_multi_update.ota_multi_update import validate_cli_args


def make_args(**overrides):
    args = {
        "send_only": False,
        "commit_only": False,
        "block_size": 512,
        "max_workers": 0,
        "expected_board_count": 4,
        "serial_number": ["A", "B", "C", "D"],
    }
    args.update(overrides)
    return Namespace(**args)


def test_four_board_all_worker_configuration_is_valid():
    validate_cli_args(make_args())


@pytest.mark.parametrize("block_size", [0, 513, 1024])
def test_invalid_block_size_fails_before_workers(block_size):
    with pytest.raises(ValueError, match="block size"):
        validate_cli_args(make_args(block_size=block_size))


def test_duplicate_idn_address_is_rejected():
    with pytest.raises(ValueError, match="must be unique"):
        validate_cli_args(make_args(serial_number=["A", "B", "B", "D"]))


@pytest.mark.parametrize("count", [0, 9])
def test_expected_board_count_is_limited_to_product_ring(count):
    with pytest.raises(ValueError, match="expected-board-count"):
        validate_cli_args(make_args(expected_board_count=count))
