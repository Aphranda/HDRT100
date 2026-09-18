"""Execute actual INSTALL/READY/release and common STOP against controlled MMIO.

Construction before INSTALL and electrical propagation are separate gates.
The extracted poll prefix and INSTALL-to-return suffix are verbatim production
code; every READY/release predicate and complete disarm body is compiled.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def launch_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("origin-launch-physical")
    origin_path = ROOT / "components/tdma/src/tdma_pio_spi_phys_origin.inc"
    physical_path = ROOT / "components/tdma/src/tdma_pio_spi_phys.c"
    origin = origin_path.read_text(encoding="utf-8")
    physical = physical_path.read_text(encoding="utf-8")
    definitions = [
        (origin, "tdma_pio_spi_phys_origin_reject", "static bool", "tdma_pio_spi_phys_t *phys, tdma_origin_reject_reason_t reason, uint32_t detail, uint64_t observed, uint64_t expected"),
        (origin, "tdma_pio_spi_phys_origin_expect", "static bool", "tdma_pio_spi_phys_t *phys, tdma_origin_reject_reason_t reason, uint32_t detail, uint64_t observed, uint64_t expected"),
        (origin, "tdma_pio_spi_phys_origin_config_current", "static bool", "tdma_pio_spi_phys_t *phys"),
        (origin, "tdma_pio_spi_phys_origin_pause", "static void", "void"),
        (origin, "tdma_pio_spi_phys_origin_resources_current", "static bool", "tdma_pio_spi_phys_t *phys"),
        (origin, "tdma_pio_spi_phys_origin_loader_config", "static dma_channel_config", "const tdma_state_machine_origin_dma_contract_t *dma"),
        (origin, "tdma_pio_spi_phys_origin_quiescent", "static bool", "tdma_pio_spi_phys_t *phys"),
        (origin, "tdma_pio_spi_phys_origin_plan_ready", "static bool", "tdma_pio_spi_phys_t *phys"),
        (origin, "tdma_pio_spi_phys_origin_prepare_ready", "bool", "void *context"),
        (origin, "tdma_pio_spi_phys_origin_release", "bool", "void *context, uint64_t expires_ticks, bool (*authorized)(void)"),
    ]
    bodies = {}
    for source, name, result, args in definitions:
        # The shared extractor recognizes project *_t return types. Normalize
        # only this SDK return token for discovery; the body remains exact.
        parsed = source.replace("static dma_channel_config ", "static dma_channel_config_t ")
        bodies[name] = f"{result} {name}({args}) {{\n" + c_definition_body(parsed, name) + "\n}\n"
    poll = c_definition_body(origin, "tdma_pio_spi_phys_origin_poll")
    switch = poll.index("    switch ((tdma_origin_prepare_stage_t)request->stage)")
    prefix = poll[:switch]
    # The earlier construction cases are outside this hardware launch slice.
    # Keep the actual common guard, INSTALL/READY cases, and failure cleanup.
    suffix = poll[poll.index("    case TDMA_ORIGIN_PREPARE_INSTALL:"):]
    bodies["launch_poll"] = ("tdma_origin_build_result_t tdma_pio_spi_phys_origin_poll(void *context) {\n" +
        prefix + "    (void)active_mask;\n    switch ((tdma_origin_prepare_stage_t)request->stage) {\n" + suffix + "\n}\n")
    for name, result, args in (
        ("tdma_pio_spi_phys_stop_command_dma", "static bool", "tdma_pio_spi_phys_t *phys"),
        ("tdma_pio_spi_phys_disarm", "bool", "void *context"),
    ):
        bodies[name] = f"{result} {name}({args}) {{\n" + c_definition_body(physical, name) + "\n}\n"
    (build / "origin_launch_impl.inc").write_text("\n".join(bodies.values()), encoding="utf-8")
    hardware = build / "hardware"
    hardware.mkdir()
    (hardware / "pio.h").write_text(PIO_HEADER, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "A host C compiler is required"
    exe = build / ("launch.exe" if os.name == "nt" else "launch")
    includes = ["tests/unit/host_stubs", "boards/rp2350_trig/inc", "components/tdma/inc",
                "components/tdma/src", "components/vdc_domain/inc", "components/resource_arbiter/inc"]
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DPROJECT_NODE_CAPACITY=6", "-I" + str(build)]
    command += ["-I" + str(ROOT / path) for path in includes]
    command += [str(ROOT / "tests/support/tdma_origin_launch_physical.c"),
                str(ROOT / "components/tdma/src/tdma_origin_build_job.c"), "-o", str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (build / "compile.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (build / "compile.stderr.txt").write_text(result.stderr, encoding="utf-8")
    (build / "integration.json").write_text(json.dumps({
        "command": command,
        "scope": "verbatim INSTALL/READY poll prefix+suffix, complete physical release predicates, release, stop_command_dma and disarm; construction/MMIO/resource-release seams",
        "body_sha256": {name: hashlib.sha256(body.encode()).hexdigest() for name, body in bodies.items()},
        "sources": [{"observed_path": p.relative_to(ROOT).as_posix(),
                     "observed_sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                    for p in (origin_path, physical_path, ROOT / "components/tdma/inc/tdma_pio_spi_phys.h",
                              ROOT / "components/tdma/inc/tdma_origin_handoff.h")],
    }, indent=2) + "\n", encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


CASES = ["hold", "release", "expiry", "expiry_during_enable", "clock_during_enable", "stop", "stop_retry",
         "skip_records", "install_dirty", "null", "phase_numbers", "revoke", "stop_after_trigger",
         "first_reject", "builder_reject", "loader_reject", "post_install_reject", "zero_expiry",
         "continuous_revoke", "continuous_rewind",
         "reload_before_trigger", "reload_with_residual_count", "reload_zero", "reload_short",
         "reload_long", "reload_self_trigger", "reload_endless",
         *[f"mutation_{i}" for i in range(80)]]


@pytest.mark.parametrize("case", CASES)
def test_only_exact_ready_can_release_once(launch_exe, case):
    result = subprocess.run([str(launch_exe), case], capture_output=True, text=True, timeout=5)
    (launch_exe.parent / f"{case}.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (launch_exe.parent / f"{case}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


PIO_HEADER = r'''
#ifndef HOST_STUB_HARDWARE_PIO_H
#define HOST_STUB_HARDWARE_PIO_H
#include <stdbool.h>
#include <stdint.h>
typedef struct host_pio_hw {
    uint32_t ctrl, fstat, pc[4], tx_words[4], rx_words[4];
    bool claimed[4], irq;
} *PIO;
extern struct host_pio_hw host_pios[3];
#define pio0 (&host_pios[0])
#define pio1 (&host_pios[1])
#define pio2 (&host_pios[2])
#define PIO_FSTAT_TXEMPTY_LSB 24u
#define PIO_FSTAT_RXEMPTY_LSB 8u
#endif
'''
