from pathlib import Path
import struct

import pytest

from tools.flash_lockout_hil_validate.flash_lockout_hil_validate import (
    package_probe_block,
    parse_flash_transaction_probe,
)


def test_parse_flash_transaction_probe(tmp_path: Path) -> None:
    log = tmp_path / "ota.log"
    log.write_text(
        "progress=512,850184,0\n"
        "flash_transaction_probe="
        "9,7,1,2,2,512,512,512,1,3,0,7,4,1,0,0,0,7,7,0,0,1,0,0,0,10,11\n",
        encoding="utf-8",
    )

    transaction = parse_flash_transaction_probe(log)

    assert transaction["state"] == 9
    assert transaction["requester"] == 1
    assert transaction["partition_id"] == 2
    assert transaction["requested_bytes"] == 512
    assert transaction["processed_bytes"] == 512
    assert transaction["verified_bytes"] == 512


def test_parse_flash_transaction_probe_requires_marker(tmp_path: Path) -> None:
    log = tmp_path / "ota.log"
    log.write_text("progress=512,850184,0\n", encoding="utf-8")

    with pytest.raises(ValueError, match="probe missing"):
        parse_flash_transaction_probe(log)


def test_package_probe_block_selects_target_image(tmp_path: Path) -> None:
    package = bytearray(512)
    struct.pack_into("<I", package, 20, 2)
    struct.pack_into("<II", package, 192, 1, 512)
    struct.pack_into("<II", package, 224, 2, 4096)
    path = tmp_path / "update.pkg"
    path.write_bytes(package)

    assert package_probe_block(path, 1) == 2
    assert package_probe_block(path, 2) == 9


def test_package_probe_block_rejects_missing_slot(tmp_path: Path) -> None:
    package = bytearray(512)
    struct.pack_into("<I", package, 20, 1)
    struct.pack_into("<II", package, 192, 1, 512)
    path = tmp_path / "update.pkg"
    path.write_bytes(package)

    with pytest.raises(ValueError, match="no image for slot 2"):
        package_probe_block(path, 2)
