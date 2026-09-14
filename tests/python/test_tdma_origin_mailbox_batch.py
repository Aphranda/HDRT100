"""Run the physical owner's immutable mailbox admission against real CRC bytes."""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("capacity", range(2, 9))
def test_origin_mailbox_batches(tmp_path, capacity):
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys_origin.inc").read_text(encoding="utf-8")
    constant = re.search(r"^#define TDMA_ORIGIN_PREPARE_MAILBOX_BATCH .+$", source, re.M)
    assert constant
    signatures = (
        ("tdma_pio_spi_phys_origin_mailbox_valid",
         "static bool tdma_pio_spi_phys_origin_mailbox_valid(const uint8_t *mailbox, uint32_t slot, uint32_t active_mask)"),
        ("tdma_pio_spi_phys_origin_mailbox_batch",
         "static bool tdma_pio_spi_phys_origin_mailbox_batch(const uint8_t *seed, size_t packet_size, uint32_t node_count, uint32_t *cursor)"),
    )
    implementation = constant.group() + "\n" + "".join(
        signature + "{" + c_definition_body(source, name) + "}\n" for name, signature in signatures)
    (tmp_path / "origin_mailbox_impl.inc").write_text(implementation, encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler
    exe = tmp_path / ("mailbox.exe" if os.name == "nt" else "mailbox")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={capacity}", "-I" + str(tmp_path),
               "-I" + str(ROOT / "components/tdma/inc"),
               str(ROOT / "tests/unit/test_tdma_origin_mailbox_batch.c"), "-o", str(exe)]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
