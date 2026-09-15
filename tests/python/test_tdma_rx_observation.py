"""Execute the real RX scanner and hardware-count reader on an advancing DMA bus."""
import os
import json
from pathlib import Path
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def build_scanner(directory, asynchronous=False, instrumented=False, physical=False):
    live = (ROOT/"components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    capture = Path(os.environ.get("TDMA_RX_CAPTURE_SOURCE", ROOT/"components/tdma/src/tdma_pio_spi_phys.c")).read_text(encoding="utf-8")
    definitions = [
        ("uint64_t", "tdma_pio_spi_phys_rx_produced_words", "tdma_pio_spi_phys_t *phys"),
        ("uint8_t", "tdma_pio_spi_phys_rx_ring_reversed_byte", "uint64_t produced"),
        ("uint8_t", "tdma_pio_spi_phys_rx_ring_byte", "uint64_t produced"),
        ("uint8_t", "tdma_pio_spi_phys_rx_ring_aligned_byte", "uint64_t produced, uint32_t bit_shift"),
        ("void", "tdma_pio_spi_phys_rx_ring_copy", "uint8_t *destination, uint64_t produced, uint32_t count, uint32_t bit_shift"),
        ("bool", "tdma_pio_spi_phys_transport_header_matches", "uint64_t packet_start, uint32_t bit_shift, uint16_t frame_size"),
    ]
    code = ""
    for kind, name, signature in definitions:
        symbol = "real_ring_copy" if name.endswith("rx_ring_copy") else name
        code += f"static {kind} __attribute__((unused)) {symbol}({signature}) {{" + c_definition_body(live, name) + "}\n"
    code += """
static void tdma_pio_spi_phys_rx_ring_copy(uint8_t *out, uint64_t p, uint32_t n, uint32_t s) {
    copy_phase = true;
    real_ring_copy(out, p, n, s);
#ifdef TDMA_TEST_RX_COPY_AFTER
    TDMA_TEST_RX_COPY_AFTER(p, n, s);
#endif
}
"""
    if physical:
        name = "tdma_pio_spi_phys_capture_words_legacy"
        code += "static bool " + name + "(tdma_pio_spi_phys_t *phys, size_t max_words, size_t *received_words) {" + c_definition_body(live, name) + "}\n"
    if asynchronous:
        code += '#include "tdma_pio_spi_phys_rx_scan.inc"\n'
        body = (c_definition_body(live, "tdma_pio_spi_phys_capture_words") if physical else
                'return tdma_pio_spi_phys_capture_words_async(phys, max_words, received_words);')
    else:
        name = ("tdma_pio_spi_phys_capture_words_legacy" if "tdma_pio_spi_phys_capture_words_legacy(" in capture
                else "tdma_pio_spi_phys_capture_words")
        body = c_definition_body(capture, name)
    code += "static bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys, size_t max_words, size_t *received_words) {" + body + "}\n"
    if physical:
        # Execute actual same-call delivery and DMA ARM. Only SDK operations,
        # non-DMA timestamps and the separate autonomous backend are facades.
        source = (ROOT/"components/tdma/src/tdma_pio_spi_phys_flight_io.inc").read_text(encoding="utf-8")
        routines = [
            (live, "bool", "tdma_pio_spi_phys_capture_words_ex", "tdma_pio_spi_phys_t *phys, size_t max_words, size_t *received_words, tdma_rx_capture_t *capture"),
            (live, "bool", "tdma_pio_spi_phys_rx_arm", "tdma_pio_spi_phys_t *phys"),
            (source, "bool", "tdma_pio_spi_phys_rx_ex", "void *context, uint8_t *packet, size_t packet_capacity, size_t *packet_size, uint64_t *rx_timestamp_ns, tdma_rx_capture_t *capture"),
            (source, "bool", "tdma_pio_spi_phys_rx", "void *context, uint8_t *packet, size_t packet_capacity, size_t *packet_size, uint64_t *rx_timestamp_ns"),
        ]
        for text, kind, name, signature in routines:
            code += f"static {kind} {name}({signature}) {{" + c_definition_body(text, name) + "}\n"
    (directory/"capture_routines.inc").write_text(code, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = directory/"rx_observation.exe"
    command = [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I"+str(directory), "-I"+str(ROOT/"components/tdma/inc"), "-I"+str(ROOT/"components/tdma/src"),
        "-I"+str(ROOT/"components/vdc_domain/inc"),
        str(ROOT/"tests/unit"/("test_tdma_rx_acquire_timing.c" if instrumented else
            "test_tdma_rx_scan.c" if asynchronous else "test_tdma_rx_observation.c")),
        str(ROOT/"components/tdma/src/tdma_rx_sequence.c"),
        str(ROOT/"components/tdma/src/tdma_rx_scan.c"),
        str(ROOT/"components/tdma/src/tdma_transport_frame.c"), "-o", str(exe)]
    if asynchronous:
        command.insert(1, "-DTDMA_TEST_ASYNC_RX=1")
    if physical:
        command.insert(1, "-DTDMA_TEST_PHYSICAL_RX=1")
    result = subprocess.run(command, capture_output=True, text=True)
    (directory/"compile.log").write_text(result.stdout+result.stderr, encoding="utf-8")
    (directory/"compile.json").write_text(json.dumps({"command":command, "returncode":result.returncode}, indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    return exe


@pytest.fixture(scope="module")
def scanner(tmp_path_factory):
    return build_scanner(tmp_path_factory.mktemp("rx-observation"))


@pytest.fixture(scope="module")
def async_scanner(tmp_path_factory):
    return build_scanner(tmp_path_factory.mktemp("rx-scan-async"), asynchronous=True)


@pytest.fixture(scope="module")
def physical_scanner(tmp_path_factory):
    return build_scanner(tmp_path_factory.mktemp("rx-capture-physical"), asynchronous=True, physical=True)


@pytest.mark.parametrize("case", ["capture_coordinates", "capture_failures", "capture_delivery",
    "capture_arm", "capture_exhaustion", "capture_legacy"])
def test_same_call_physical_rx_capture_provenance(physical_scanner, case):
    command = [str(physical_scanner), case]
    result = subprocess.run(command, capture_output=True, text=True, timeout=10)
    (physical_scanner.parent/f"{case}.log").write_text(result.stdout+result.stderr, encoding="utf-8")
    (physical_scanner.parent/f"{case}.json").write_text(json.dumps({"command":command, "returncode":result.returncode}, indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr


@pytest.mark.parametrize("case", ["stable", "overwrite", "counter_alias", "bounded", "prefix", "incomplete", "wrong_mode", "idle_gap", "phase_gap", "copy_window"])
def test_real_scanner_dma_interleavings(scanner, case):
    result = subprocess.run([str(scanner), case], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout+result.stderr


@pytest.mark.parametrize("case", ["phases", "worker_pause", "live_overwrite", "live_epoch",
    "snapshot_overwrite", "snapshot_epoch", "cancel", "resync", "incomplete", "geometry", "age", "epoch",
    "persona", "config", "false_magic", "capacity", "private_header"])
def test_async_discovery_and_live_dma_copy(async_scanner, case):
    result = subprocess.run([str(async_scanner), case], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout+result.stderr


@pytest.mark.parametrize("case", ["refresh_high_rate", "refresh_busy", "refresh_incomplete",
    "refresh_bad_pair", "refresh_epoch", "refresh_stop", "refresh_copy_recheck",
    "refresh_retry_bound", "refresh_metadata", "refresh_maximum", "refresh_no_candidate"])
def test_async_unlocked_training_refresh(async_scanner, case):
    command = [str(async_scanner), case]
    result = subprocess.run(command, capture_output=True, text=True, timeout=10)
    (async_scanner.parent/f"{case}.log").write_text(result.stdout+result.stderr, encoding="utf-8")
    (async_scanner.parent/f"{case}.json").write_text(json.dumps({"command":command, "returncode":result.returncode}, indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr


def test_async_acquisition_timing_keeps_failed_copy_evidence(tmp_path):
    exe = build_scanner(tmp_path, asynchronous=True, instrumented=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=3)
    assert result.returncode == 0, result.stdout+result.stderr
