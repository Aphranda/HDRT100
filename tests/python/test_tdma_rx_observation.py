"""Execute the real RX scanner and hardware-count reader on an advancing DMA bus."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def scanner(tmp_path_factory):
    directory = tmp_path_factory.mktemp("rx-observation")
    live = (ROOT/"components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    capture = Path(os.environ.get("TDMA_RX_CAPTURE_SOURCE", ROOT/"components/tdma/src/tdma_pio_spi_phys.c")).read_text(encoding="utf-8")
    definitions = [
        ("uint64_t", "tdma_pio_spi_phys_rx_produced_words", "tdma_pio_spi_phys_t *phys"),
        ("uint8_t", "tdma_pio_spi_phys_rx_ring_byte", "uint64_t produced"),
        ("uint8_t", "tdma_pio_spi_phys_rx_ring_aligned_byte", "uint64_t produced, uint32_t bit_shift"),
        ("bool", "tdma_pio_spi_phys_transport_header_matches", "uint64_t packet_start, uint32_t bit_shift, uint16_t frame_size"),
    ]
    code = ""
    for kind, name, signature in definitions:
        symbol = "real_header_matches" if name.endswith("transport_header_matches") else name
        code += f"static {kind} {symbol}({signature}) {{" + c_definition_body(live, name) + "}\n"
    code += """
static bool tdma_pio_spi_phys_transport_header_matches(uint64_t p, uint32_t s, uint16_t n) {
    const bool result = real_header_matches(p, s, n);
    if (result) copy_phase = true;
    return result;
}
static bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys, size_t max_words, size_t *received_words) {
""" + c_definition_body(capture, "tdma_pio_spi_phys_capture_words") + "}\n"
    (directory/"capture_routines.inc").write_text(code, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = directory/"rx_observation.exe"
    command = [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I"+str(directory), "-I"+str(ROOT/"components/tdma/inc"),
        str(ROOT/"tests/unit/test_tdma_rx_observation.c"),
        str(ROOT/"components/tdma/src/tdma_rx_sequence.c"),
        str(ROOT/"components/tdma/src/tdma_transport_frame.c"), "-o", str(exe)]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout+result.stderr
    return exe


@pytest.mark.parametrize("case", ["stable", "overwrite", "counter_alias", "bounded", "prefix", "incomplete", "wrong_mode", "idle_gap", "phase_gap"])
def test_real_scanner_dma_interleavings(scanner, case):
    result = subprocess.run([str(scanner), case], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout+result.stderr
