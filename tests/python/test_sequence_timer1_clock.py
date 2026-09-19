"""Exercise checked TIMER1 reads, including changes during a raw sample."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_timestamp_clock.c"

#if PICO_ON_DEVICE
static test_timer_hw_t test_timer;
static unsigned register_reads, mutate_read, clock_reads;
static unsigned mutation;
static uint32_t reported_hz = 250000000u;
static bool change_hz_after_sample;

test_timer_hw_t *test_timer_access(void)
{
    ++register_reads;
    if (register_reads == mutate_read) {
        if (mutation == 1u) ++test_timer.timerawh;
        if (mutation == 2u) test_timer.source = 0u;
        if (mutation == 3u) test_timer.pause = 1u;
    }
    return &test_timer;
}

uint32_t clock_get_hz(int clock)
{
    assert(clock == clk_sys);
    ++clock_reads;
    return change_hz_after_sample && clock_reads > 1u ? 150000000u : reported_hz;
}
#endif

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *mode = argv[1];
    const uint64_t sentinel = UINT64_C(0xfeedbeefdeadbeef);
    uint64_t result = sentinel;
#if PICO_ON_DEVICE
    test_timer.source = 99u;
    test_timer.pause = 9u;
    test_timer.timelw = 123u;
    test_timer.timehw = 456u;
    if (strcmp(mode, "uninitialized") == 0) {
        assert(!vdc_timestamp_clock_is_current(reported_hz));
        assert(!vdc_timestamp_clock_try_read_ticks64(reported_hz, &result));
        assert(result == sentinel && register_reads == 0u && clock_reads == 0u);
        assert(test_timer.source == 99u && test_timer.pause == 9u);
        assert(test_timer.timelw == 123u && test_timer.timehw == 456u);
        return 0;
    }
    if (strcmp(mode, "zero_init_rate") == 0) {
        reported_hz = 0u;
        assert(!vdc_timestamp_clock_init());
        reported_hz = 250000000u;
        assert(!vdc_timestamp_clock_is_current(reported_hz));
        assert(!vdc_timestamp_clock_try_read_ticks64(reported_hz, &result));
        assert(result == sentinel);
        return 0;
    }
    assert(vdc_timestamp_clock_init());
    assert(vdc_timestamp_clock_is_current(reported_hz));
    test_timer.timerawh = 7u;
    test_timer.timerawl = 0xfffffffeu;
    register_reads = clock_reads = 0u;
    uint32_t expected_hz = reported_hz;
    if (strcmp(mode, "null") == 0) {
        assert(!vdc_timestamp_clock_try_read_ticks64(expected_hz, NULL));
        assert(register_reads == 0u && clock_reads == 0u);
        return 0;
    }
    if (strcmp(mode, "zero_expected") == 0) expected_hz = 0u;
    if (strcmp(mode, "wrong_expected") == 0) expected_hz = 150000000u;
    if (strcmp(mode, "wrong_rate") == 0) reported_hz = 150000000u;
    if (strcmp(mode, "paused") == 0) test_timer.pause = 1u;
    if (strcmp(mode, "wrong_source") == 0) test_timer.source = 0u;
    if (strcmp(mode, "rollover") == 0) { mutate_read = 5u; mutation = 1u; }
    if (strcmp(mode, "source_change") == 0) { mutate_read = 6u; mutation = 2u; }
    if (strcmp(mode, "pause_change") == 0) { mutate_read = 6u; mutation = 3u; }
    if (strcmp(mode, "rate_change") == 0) change_hz_after_sample = true;
    const bool success = vdc_timestamp_clock_try_read_ticks64(expected_hz, &result);
    if (strcmp(mode, "success") == 0) {
        assert(success && result == UINT64_C(0x7fffffffe));
        assert(register_reads == 7u && clock_reads == 2u);
    } else {
        assert(!success && result == sentinel);
        /* One attempt only, including the counter high-word rollover. */
        assert(register_reads <= 7u && clock_reads <= 2u);
    }
#else
    assert(strcmp(mode, "host") == 0);
    assert(!vdc_timestamp_clock_is_current(1000000u));
    assert(!vdc_timestamp_clock_try_read_ticks64(1000000u, &result));
    assert(!s_vdc_timestamp_clock_initialized && result == sentinel);
    assert(vdc_timestamp_clock_init());
    assert(!vdc_timestamp_clock_is_current(1000000u));
    assert(!vdc_timestamp_clock_try_read_ticks64(1000000u, &result));
    assert(result == sentinel);
    assert(vdc_timestamp_clock_read_ticks64() == 1u);
    assert(vdc_timestamp_clock_read_ticks64() == 2u);
#endif
    return 0;
}
'''


@pytest.fixture(scope="module", params=[True, False], ids=["device", "host"])
def timer1_executable(request, tmp_path_factory):
    device = request.param
    directory = tmp_path_factory.mktemp("sequence-timer1")
    (directory / "pico.h").write_text(
        "#define __not_in_flash_func(name) name\n", encoding="utf-8")
    hardware = directory / "hardware"
    hardware.mkdir()
    (hardware / "clocks.h").write_text(
        "#include <stdint.h>\nenum { clk_sys = 5 };\n"
        "uint32_t clock_get_hz(int clock);\n", encoding="utf-8")
    (hardware / "timer.h").write_text(
        "#include <stdint.h>\n"
        "typedef struct { volatile uint32_t pause, source, timelw, timehw, timerawl, timerawh; } test_timer_hw_t;\n"
        "test_timer_hw_t *test_timer_access(void);\n"
        "#define timer1_hw test_timer_access()\n"
        "#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 1u\n", encoding="utf-8")
    harness = directory / "checked-clock.c"
    harness.write_text(HARNESS, encoding="utf-8")
    executable = directory / ("clock.exe" if os.name == "nt" else "clock")
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPICO_ON_DEVICE={int(device)}", "-I" + str(directory),
               "-I" + str(ROOT / "components/vdc_domain/inc"),
               "-I" + str(ROOT / "components/vdc_domain/src"),
               str(harness), "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable, device


@pytest.mark.parametrize("case", [
    "uninitialized", "zero_init_rate", "null", "zero_expected", "wrong_expected",
    "wrong_rate", "paused", "wrong_source", "rollover", "source_change",
    "pause_change", "rate_change", "success", "host",
])
def test_checked_timer1_read(timer1_executable, case):
    executable, device = timer1_executable
    if device == (case == "host"):
        pytest.skip("case targets the other clock implementation")
    result = subprocess.run([str(executable), case], capture_output=True,
                            text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
