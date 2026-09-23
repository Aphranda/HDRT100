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

/* Injected kernel-tick audit model. The audit itself has no hardware
 * dependency, so the same cases run for both clock implementations. */
static uint32_t audit_clk_hz = 250000000u;
static uint32_t audit_rate_hz = 1000u;
static uint32_t audit_reload = 249999u;
static uint32_t audit_control = 0x5u;
static uint32_t audit_tick_ms;
static uint64_t audit_time_ns;
static bool audit_time_ok = true;
static uint32_t audit_samples_seen;

static uint32_t audit_port_clk(void) { return audit_clk_hz; }
static uint32_t audit_port_rate(void) { return audit_rate_hz; }
static uint32_t audit_port_tick_ms(void) { return audit_tick_ms; }
static uint32_t audit_port_reload(void) { return audit_reload; }
static uint32_t audit_port_control(void) { return audit_control; }

static bool audit_port_time(uint64_t *out)
{
    ++audit_samples_seen;
    if (!audit_time_ok || out == NULL) {
        return false;
    }
    *out = audit_time_ns;
    return true;
}

static const vdc_tick_audit_port_t audit_port = {
    .clk_sys_hz = audit_port_clk,
    .tick_rate_hz = audit_port_rate,
    .tick_ms = audit_port_tick_ms,
    .systick_reload = audit_port_reload,
    .systick_control = audit_port_control,
    .time_ns = audit_port_time,
};

static void audit_reset(void)
{
    audit_clk_hz = 250000000u;
    audit_rate_hz = 1000u;
    audit_reload = 249999u;
    audit_control = 0x5u;
    audit_tick_ms = 0u;
    audit_time_ns = 0u;
    audit_time_ok = true;
    audit_samples_seen = 0u;
}

static int run_tick_audit_case(const char *mode)
{
    vdc_tick_audit_t audit;
    audit_reset();

    if (strcmp(mode, "audit_before_sample") == 0) {
        /* Missing data must never read as a passing audit. */
        assert(!vdc_tick_audit_get(&audit));
        assert(audit.version == VDC_TICK_AUDIT_VERSION);
        assert(audit.samples == 0u && audit.static_flags == 0u);
        assert(audit.dynamic_flags == 0u && audit.ratio_ppm == 0);
        return 0;
    }
    if (strcmp(mode, "audit_incomplete_port") == 0) {
        vdc_tick_audit_port_t incomplete = audit_port;
        incomplete.time_ns = NULL;
        assert(!vdc_tick_audit_sample(&incomplete));
        incomplete = audit_port;
        incomplete.clk_sys_hz = NULL;
        assert(!vdc_tick_audit_sample(&incomplete));
        incomplete = audit_port;
        incomplete.tick_ms = NULL;
        assert(!vdc_tick_audit_sample(&incomplete));
        assert(!vdc_tick_audit_sample(NULL));
        assert(!vdc_tick_audit_get(&audit) && audit.samples == 0u);
        assert(audit_samples_seen == 0u);
        return 0;
    }
    if (strcmp(mode, "audit_bad_clock") == 0) {
        audit_clk_hz = 250000001u;
        assert(!vdc_tick_audit_sample(&audit_port));
        audit_clk_hz = 0u;
        assert(!vdc_tick_audit_sample(&audit_port));
        audit_clk_hz = 250000000u;
        audit_rate_hz = 0u;
        assert(!vdc_tick_audit_sample(&audit_port));
        assert(!vdc_tick_audit_get(&audit) && audit.samples == 0u);
        return 0;
    }

    const bool stale = strcmp(mode, "audit_stale_reload") == 0;
    const bool slow = strcmp(mode, "audit_slow_tick") == 0;
    const bool unarmed = strcmp(mode, "audit_unarmed") == 0;
    const bool clksource_off = strcmp(mode, "audit_clksource_off") == 0;
    const bool untimed = strcmp(mode, "audit_untimed") == 0;
    const bool rolls = strcmp(mode, "audit_window_rolls") == 0;
    assert(stale || slow || unarmed || clksource_off || untimed || rolls);

    if (stale) audit_reload = 149999u;            /* Installed at the 150 MHz boot clock. */
    if (unarmed) audit_control = 0x4u;
    if (clksource_off) audit_control = 0x1u;

    uint32_t expected_static = VDC_TICK_AUDIT_STATIC_OK;
    if (stale) expected_static &= ~VDC_TICK_AUDIT_STATIC_RELOAD;
    if (unarmed) expected_static &= ~VDC_TICK_AUDIT_STATIC_ARMED;
    if (clksource_off) expected_static &= ~VDC_TICK_AUDIT_STATIC_CLKSOURCE;

    /* The first sample only opens the window. */
    assert(vdc_tick_audit_sample(&audit_port));
    assert(vdc_tick_audit_get(&audit) && audit.samples == 1u);
    assert(audit.clk_sys_hz == 250000000u && audit.tick_rate_hz == 1000u);
    assert(audit.expected_reload == 249999u && audit.actual_reload == audit_reload);
    assert(audit.static_flags == expected_static);
    assert(audit.dynamic_flags == 0u && audit.window_nominal_ms == 0u);
    assert(audit.ratio_ppm == 0);

    if (untimed) {
        audit_time_ok = false;
        audit_tick_ms = 10000u;
        assert(vdc_tick_audit_sample(&audit_port));
        assert(vdc_tick_audit_get(&audit) && audit.samples == 2u);
        assert(audit.dynamic_flags == VDC_TICK_AUDIT_DYNAMIC_NO_TIMEBASE);
        assert(audit.window_nominal_ms == 0u && audit.window_real_ms == 0u);
        assert(audit.ratio_ppm == 0);
        /* The untimed sample must not leave the old origin in place. */
        audit_time_ok = true;
        audit_time_ns = 40000000000u;
        assert(vdc_tick_audit_sample(&audit_port));
        assert(vdc_tick_audit_get(&audit) && audit.dynamic_flags == 0u);
        audit_tick_ms = 14000u;
        audit_time_ns = 44000000000u;
        assert(vdc_tick_audit_sample(&audit_port));
        assert(vdc_tick_audit_get(&audit));
        assert(audit.window_nominal_ms == 4000u && audit.window_real_ms == 4000u);
        assert(audit.ratio_ppm == 0);
        /* A later untimed sample clears the published window. */
        audit_time_ok = false;
        audit_tick_ms = 20000u;
        assert(vdc_tick_audit_sample(&audit_port));
        assert(vdc_tick_audit_get(&audit) && audit.samples == 5u);
        assert(audit.dynamic_flags == VDC_TICK_AUDIT_DYNAMIC_NO_TIMEBASE);
        assert(audit.window_nominal_ms == 0u && audit.window_real_ms == 0u);
        assert(audit.ratio_ppm == 0);
        assert(audit.static_flags == VDC_TICK_AUDIT_STATIC_OK);
        return 0;
    }

    /* Close one full nominal window. */
    const uint64_t real_ns = stale ? UINT64_C(2400000000) :
                             (slow ? UINT64_C(5000000000) : UINT64_C(4000000000));
    const uint32_t expected_real_ms = stale ? 2400u : (slow ? 5000u : 4000u);
    const int32_t expected_ppm = stale ? 666666 : (slow ? -200000 : 0);
    audit_tick_ms = 4000u;
    audit_time_ns = real_ns;
    assert(vdc_tick_audit_sample(&audit_port));
    assert(vdc_tick_audit_get(&audit) && audit.samples == 2u);
    assert(audit.static_flags == expected_static);
    assert(audit.dynamic_flags == VDC_TICK_AUDIT_DYNAMIC_VALID);
    assert(audit.window_nominal_ms == 4000u && audit.window_real_ms == expected_real_ms);
    assert(audit.ratio_ppm == expected_ppm);
    assert(audit.samples == 2u);

    if (rolls) {
        audit_tick_ms = 8000u;
        audit_time_ns = 8000000000u;
        assert(vdc_tick_audit_sample(&audit_port));
        assert(vdc_tick_audit_get(&audit) && audit.samples == 3u);
        assert(audit.window_nominal_ms == 4000u && audit.window_real_ms == 4000u);
        assert(audit.ratio_ppm == 0 && audit.static_flags == VDC_TICK_AUDIT_STATIC_OK);
    }
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *mode = argv[1];
    const uint64_t sentinel = UINT64_C(0xfeedbeefdeadbeef);
    uint64_t result = sentinel;
    if (strncmp(mode, "audit_", 6u) == 0) {
        return run_tick_audit_case(mode);
    }
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
    "audit_before_sample", "audit_incomplete_port", "audit_bad_clock",
    "audit_stale_reload", "audit_slow_tick", "audit_unarmed",
    "audit_clksource_off", "audit_untimed", "audit_window_rolls",
])
def test_checked_timer1_read(timer1_executable, case):
    executable, device = timer1_executable
    # The audit is port-injected with no hardware dependency, so its cases run
    # against both clock implementations; the read cases stay implementation
    # specific.
    if case.startswith("audit_"):
        pass
    elif device == (case == "host"):
        pytest.skip("case targets the other clock implementation")
    result = subprocess.run([str(executable), case], capture_output=True,
                            text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
