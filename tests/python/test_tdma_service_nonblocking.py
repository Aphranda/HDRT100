"""Execute the real TDMA service with a frozen clock and interrupted writers."""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import compile_executable


ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def service_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("tdma-service-nonblocking")
    pico = build / "pico"
    pico.mkdir()
    (pico / "time.h").write_text(
        "#include <stdint.h>\nuint64_t time_us_64(void);\n", encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "A host C compiler is required"
    sources = [ROOT / "tests/unit/test_tdma_service_nonblocking.c"]
    sources += [ROOT / f"components/tdma/src/{name}.c" for name in (
        "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map",
        "tdma_ring_runtime", "tdma_traffic_scheduler", "tdma_service_timing")]
    exe = build / ("service.exe" if os.name == "nt" else "service")
    result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DTDMA_SERVICE_TIMING_ENABLED=1",
                    "-I" + str(build), "-I" + str(ROOT / "components/tdma/inc"),
                    "-I" + str(ROOT / "components/vdc_domain/inc"),
                    *map(str, sources), "-o", str(exe)], capture_output=True, text=True, timeout=60)
    (build / "compile.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (build / "compile.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.mark.parametrize("case", [
    "diagnostic_burst", "stopped_update", "writer", "changed", "window", "miss", "abort",
    "resident", "map_admission", "lifecycle", "lifecycle_selected", "lifecycle_pending",
    "borrowed_reset", "reset_stop_ack", "reset_guard", "reset_arm_interleave",
])
def test_service_yields_without_losing_intent(service_exe, case):
    result = subprocess.run([str(service_exe), case], capture_output=True,
                            text=True, timeout=3)
    (service_exe.parent / f"{case}.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (service_exe.parent / f"{case}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.fixture(scope="module")
def scpi_fifo_reset_exe(tmp_path_factory):
    """Execute the real handler; reset results and RTOS scheduling are stubs."""
    build = tmp_path_factory.mktemp("scpi-fifo-reset")
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    handler = ingress_definition(source, "scpi_cmd_system_tdma_flight_fifo_reset")
    wait_limit = re.search(r"(?m)^#define SCPI_TDMA_FIFO_RESET_WAIT_LOOPS\s+\d+u", source)
    assert wait_limit
    harness = r'''
#include <assert.h>
#include <string.h>
#include "tdma_service.h"
typedef struct { int unused; } scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR 0
static tdma_service_service_t owner;
static scpi_t context_storage;
static const char *scenario;
static const char *error_reason;
static uint32_t attempts, delays, errors, results;
static tdma_service_service_t *tdma_runtime_owner_get(void)
{ return !strcmp(scenario, "missing_owner") ? NULL : &owner; }
tdma_service_flight_fifo_reset_result_t tdma_service_reset_flight_fifo_checked(
    tdma_service_service_t *service)
{
    ++attempts;
    /* Retrying without yielding cannot let the current owner return a view. */
    assert(delays == attempts - 1);
    if (service == NULL) return TDMA_SERVICE_FLIGHT_FIFO_RESET_INVALID;
    assert(service == &owner);
    if (!strcmp(scenario, "invalid")) return TDMA_SERVICE_FLIGHT_FIFO_RESET_INVALID;
    if (!strcmp(scenario, "not_stopped")) return TDMA_SERVICE_FLIGHT_FIFO_RESET_NOT_STOPPED;
    if (!strcmp(scenario, "busy_exhausted")) return TDMA_SERVICE_FLIGHT_FIFO_RESET_BUSY;
    if (!strcmp(scenario, "busy_success") && attempts <= 2)
        return TDMA_SERVICE_FLIGHT_FIFO_RESET_BUSY;
    return TDMA_SERVICE_FLIGHT_FIFO_RESET_OK;
}
static void osal_task_delay_ms(uint32_t milliseconds)
{ assert(milliseconds == 1 && !errors && !results); ++delays; }
static void scpi_port_push_exec_error(scpi_t *context, const char *reason)
{ assert(context == &context_storage); ++errors; error_reason = reason; }
static void SCPI_ResultText(scpi_t *context, const char *result)
{ assert(context == &context_storage && !strcmp(result, "OK")); ++results; }
''' + wait_limit.group(0) + "\n" + handler + r'''
int main(int argc, char **argv)
{
    assert(argc == 2); scenario = argv[1];
    const scpi_result_t status = scpi_cmd_system_tdma_flight_fifo_reset(&context_storage);
    if (!strcmp(scenario, "immediate_success") || !strcmp(scenario, "busy_success")) {
        assert(status == SCPI_RES_OK && results == 1 && errors == 0);
        assert(attempts == (!strcmp(scenario, "busy_success") ? 3u : 1u));
        assert(delays == attempts - 1);
    } else {
        assert(status == SCPI_RES_ERR && results == 0 && errors == 1);
        if (!strcmp(scenario, "busy_exhausted")) {
            assert(attempts == SCPI_TDMA_FIFO_RESET_WAIT_LOOPS);
            assert(delays >= attempts - 1 && delays <= attempts);
            assert(!strcmp(error_reason, "TDMA_FLIGHT_FIFO_RESET_BUSY"));
        } else {
            assert(attempts == 1 && delays == 0);
            assert(!strcmp(error_reason, "TDMA_FLIGHT_FIFO_RESET"));
        }
    }
    return 0;
}
'''
    return compile_executable(build, "scpi-reset", harness)


@pytest.mark.parametrize("case", [
    "immediate_success", "busy_success", "busy_exhausted", "not_stopped", "invalid", "missing_owner",
])
def test_scpi_fifo_reset_yields_only_for_busy(scpi_fifo_reset_exe, case):
    result = subprocess.run([str(scpi_fifo_reset_exe), case], capture_output=True,
                            text=True, timeout=3)
    (scpi_fifo_reset_exe.parent / f"{case}.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (scpi_fifo_reset_exe.parent / f"{case}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
