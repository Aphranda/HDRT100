"""Exercise the real no-init TIMER1 reader with MMIO changes at exact reads."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("device", [True, False], ids=["device", "no-device"])
def test_bounded_raw_reader(tmp_path: Path, device: bool) -> None:
    (tmp_path / "pico.h").write_text("#define __not_in_flash_func(name) name\n", encoding="utf-8")
    hardware = tmp_path / "hardware"
    hardware.mkdir()
    (hardware / "clocks.h").write_text(
        "#include <stdint.h>\nenum { clk_sys=5 };\nuint32_t clock_get_hz(int);\n", encoding="utf-8")
    (hardware / "timer.h").write_text(
        "#include <stdint.h>\n"
        "typedef struct { volatile uint32_t pause,source,timelw,timehw,timerawl,timerawh; } test_timer_hw_t;\n"
        "test_timer_hw_t *test_timer_pointer(void);\n"
        "#define timer1_hw test_timer_pointer()\n"
        "#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 1u\n", encoding="utf-8")
    source = tmp_path / "clock_bounded.c"
    source.write_text(DEVICE if device else HOST, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / ("clock_bounded.exe" if os.name == "nt" else "clock_bounded")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPICO_ON_DEVICE={int(device)}", "-I" + str(tmp_path),
               "-I" + str(ROOT / "components/vdc_domain/inc"),
               "-I" + str(ROOT / "components/vdc_domain/src"), str(source), "-o", str(executable)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (tmp_path / "compile.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    (tmp_path / "run.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "bounded clock: passed" in result.stdout


DEVICE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdc_timestamp_clock.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 99; } } while(0)
static test_timer_hw_t bank;
static uint32_t actual_hz = 125000000u;
static unsigned mmio_reads, clock_reads, mutate_at, mutation, change_clock_at;
test_timer_hw_t *test_timer_pointer(void) {
    ++mmio_reads;
    if (mmio_reads == mutate_at) {
        if (mutation == 1u) { ++bank.timerawh; bank.timerawl = 0u; }
        if (mutation == 2u) bank.source = 9u;
        if (mutation == 3u) bank.pause = 1u;
    }
    return &bank;
}
uint32_t clock_get_hz(int clock) {
    (void)clock; ++clock_reads;
    return actual_hz + (change_clock_at && clock_reads >= change_clock_at ? 1u : 0u);
}
static void clean(void) {
    memset(&bank, 0, sizeof(bank)); bank.source = TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS;
    bank.timerawh = 0x12345678u; bank.timerawl = 0x9abcdef0u;
    mmio_reads = clock_reads = mutate_at = mutation = change_clock_at = 0u;
    actual_hz = 125000000u;
}
int main(void) {
    const uint64_t sentinel = UINT64_C(0x7654321089abcdef);
    uint64_t value = sentinel;
    clean();
    CHECK(!vdc_timestamp_clock_try_read_ticks64(actual_hz, &value));
    CHECK(!vdc_timestamp_clock_is_current(actual_hz));
    CHECK(value == sentinel && mmio_reads == 0u && clock_reads == 0u);
    CHECK(!s_vdc_timestamp_clock_initialized && !s_vdc_timestamp_clock_ready);
    CHECK(vdc_timestamp_clock_init());
    CHECK(s_vdc_timestamp_clock_initialized && s_vdc_timestamp_clock_ready);
    clean(); CHECK(vdc_timestamp_clock_try_read_ticks64(actual_hz, &value));
    CHECK(value == UINT64_C(0x123456789abcdef0) && mmio_reads == 7u && clock_reads == 2u);
    for (unsigned i = 0u; i < 2u; ++i) {
        clean(); bank.timerawh = bank.timerawl = i ? UINT32_MAX : 0u;
        CHECK(vdc_timestamp_clock_try_read_ticks64(actual_hz, &value));
        CHECK(value == (i ? UINT64_MAX : 0u));
    }
    clean(); value = sentinel;
    CHECK(!vdc_timestamp_clock_try_read_ticks64(actual_hz, NULL));
    CHECK(!vdc_timestamp_clock_try_read_ticks64(0u, &value));
    CHECK(!vdc_timestamp_clock_try_read_ticks64(actual_hz + 1u, &value));
    CHECK(value == sentinel && mmio_reads == 0u && clock_reads == 0u);
    for (unsigned which = 0u; which < 3u; ++which) {
        clean(); value = sentinel;
        if (which == 0u) bank.source = 9u;
        if (which == 1u) bank.pause = 1u;
        if (which == 2u) ++actual_hz;
        CHECK(!vdc_timestamp_clock_try_read_ticks64(125000000u, &value));
        CHECK(value == sentinel && mmio_reads <= 2u);
    }
    for (unsigned which = 0u; which < 5u; ++which) {
        clean(); value = sentinel;
        if (which == 0u) { mutate_at = 4u; mutation = 1u; }
        if (which == 1u) { mutate_at = 6u; mutation = 2u; }
        if (which == 2u) { mutate_at = 7u; mutation = 3u; }
        if (which == 3u) change_clock_at = 2u;
        if (which == 4u) { mutate_at = 4u; mutation = 2u; }
        CHECK(!vdc_timestamp_clock_try_read_ticks64(125000000u, &value));
        CHECK(value == sentinel && mmio_reads <= 7u && clock_reads <= 2u);
        if (which == 0u) CHECK(mmio_reads == 5u); /* Exactly one triplet, no rollover retry. */
    }
    clean(); s_vdc_timestamp_clock_initialized = false; actual_hz = 0u;
    CHECK(!vdc_timestamp_clock_init() && !s_vdc_timestamp_clock_ready);
    mmio_reads = clock_reads = 0u; value = sentinel;
    CHECK(!vdc_timestamp_clock_try_read_ticks64(125000000u, &value));
    CHECK(value == sentinel && mmio_reads == 0u && clock_reads == 0u);
    puts("bounded clock: passed device MMIO and failure-output cases"); return 0;
}
'''

HOST = r'''
#include <stdio.h>
#include "vdc_timestamp_clock.c"
int main(void) {
    uint64_t value = UINT64_C(0x1234567887654321);
    if (vdc_timestamp_clock_try_read_ticks64(1000000u, &value) ||
        s_vdc_timestamp_clock_initialized || vdc_timestamp_clock_is_current(1000000u)) return 1;
    if (!vdc_timestamp_clock_init()) return 2;
    if (vdc_timestamp_clock_try_read_ticks64(1000000u, &value) ||
        vdc_timestamp_clock_is_current(1000000u) || value != UINT64_C(0x1234567887654321)) return 3;
    puts("bounded clock: passed unavailable host raw clock"); return 0;
}
'''
