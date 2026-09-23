"""Execute the actual Core1 release loop with a deterministic cycle clock.

This checks release semantics, not measured hardware jitter or phase WCET.
No microsecond/alarm-pool mock is provided: losing cycle precision fails to build.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "project_config.h"
static uint64_t now_cycles;
static unsigned ready_spins, limit, iteration;
static bool initialized, just_ran;
static uint32_t active_cycles;
static uint32_t period_cycles[16];
static uint64_t poll_cost[16], apply_cost[16], service_cost[16];
static jmp_buf finished;
static void event(const char *name, uint64_t value) {
    printf("%s,%" PRIu64 ",%" PRIu64 "\n", name, now_cycles, value);
}
static bool app_is_ready(void) {
    event("ready", ready_spins == 0);
    return ready_spins == 0;
}
static void tight_loop_contents(void) {
    if (!initialized) {
        event("spin", 0);
        assert(ready_spins > 0);
        --ready_spins;
        now_cycles += 100 * (BOARD_SYS_CLOCK_HZ / 1000000u);
    } else if (just_ran) {
        just_ran = false;
        event("spin", 0);
        if (iteration == limit) longjmp(finished, 1);
    } else {
        /* One simulated hardware cycle per polling spin. No per-cycle log. */
        ++now_cycles;
    }
}
static void app_realtime_cycle_counter_init(void) {
    assert(!initialized);
    initialized = true;
    event("init", 0);
}
static uint64_t vdc_timestamp_clock_read_ticks64(void) {
    assert(initialized);
    return now_cycles;
}
static void drv_flash_core1_lockout_poll(void) {
    event("poll", 0);
    now_cycles += poll_cost[iteration];
}
static bool app_realtime_apply_pending_profile_core1(void) {
    event("apply", period_cycles[iteration] != 0);
    now_cycles += apply_cost[iteration];
    if (period_cycles[iteration]) active_cycles = period_cycles[iteration];
    return period_cycles[iteration] != 0;
}
static uint32_t app_realtime_cycle_cycles_core1(void) {
    assert(active_cycles != 0);
    event("profile", active_cycles);
    return active_cycles;
}
static void app_realtime_run_once(void) {
    event("run", iteration);
    now_cycles += service_cost[iteration];
    ++iteration;
    just_ran = true;
}
'''

MAIN = r'''
int main(void) {
    assert(scanf("%" SCNu64 " %u %u %" SCNu32,
        &now_cycles, &ready_spins, &limit, &active_cycles) == 4);
    assert(limit > 0 && limit <= 16);
    for (unsigned i = 0; i < limit; ++i) {
        assert(scanf("%" SCNu32 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                     &period_cycles[i], &poll_cost[i], &apply_cost[i],
                     &service_cost[i]) == 4);
    }
    if (setjmp(finished) == 0) core1_realtime_entry();
    assert(iteration == limit);
    return 0;
}
'''


@pytest.fixture(scope="module")
def release_executable(tmp_path_factory):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    source = (ROOT / "application/src/app_runtime.c").read_text(encoding="utf-8")
    # Select the complete production function without replacing its body.
    start = source.index("static void core1_realtime_entry(void)")
    end = source.index("\nvoid app_runtime_start_realtime_core(void)", start)
    board = (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
        encoding="utf-8")
    board_clock = re.search(r"^#define\s+BOARD_SYS_CLOCK_HZ\s+([^\r\n]+)",
                           board, re.MULTILINE)
    assert board_clock
    directory = tmp_path_factory.mktemp("core1_release")
    test_source = directory / "release.c"
    test_source.write_text(
        "#define BOARD_SYS_CLOCK_HZ " + board_clock.group(1) + "\n" +
        HARNESS + source[start:end] + MAIN, encoding="utf-8")
    executable = directory / ("release.exe" if os.name == "nt" else "release")
    result = subprocess.run(
        [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-I" + str(ROOT / "config"), str(test_source), "-o", str(executable)],
        capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


# These existing scenarios use microseconds for readability and are converted
# to cycles before execution. Fractional-period cases below use raw cycles.
CASES = [
    pytest.param(
        100, 0,
        [(0, 0, 0, 100), (0, 0, 0, 1200), (0, 0, 0, 0)],
        [1600, 3100, 4600], [1600, 3100, 4600], [100, 1700, 4300],
        id="service-time-does-not-drift-absolute-release"),
    pytest.param(
        0, 0,
        [(0, 0, 0, 5000), (0, 0, 0, 100), (0, 0, 0, 0), (0, 0, 0, 0)],
        [1500, 3000, 4500, 6000], [1500, 6500, 6600, 6600],
        [0, 6500, 6600, 6600],
        id="late-service-keeps-existing-catch-up-semantics"),
    pytest.param(
        0, 0,
        [(0, 2500, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0)],
        [1500, 3000, 4500], [4000, 4000, 4500], [0, 4000, 4000],
        id="flash-poll-before-profile-and-service"),
    pytest.param(
        0, 0, [(0, 0, 200, 0), (0, 0, 0, 0)],
        [1500, 3000], [1700, 3000], [0, 1700],
        id="owner-declines-handoff-without-rebase"),
    pytest.param(
        100, 3, [(0, 0, 0, 0), (0, 0, 0, 0)],
        [1900, 3400], [1900, 3400], [400, 1900],
        id="initial-app-ready-gates-counter-and-first-deadline"),
    pytest.param(
        4294966546, 0, [(0, 0, 0, 100), (0, 0, 0, 0), (0, 0, 0, 0)],
        [4294968046, 4294969546, 4294971046],
        [4294968046, 4294969546, 4294971046],
        [4294966546, 4294968146, 4294969546],
        id="absolute-time-preserves-high-word-across-low32-wrap"),
    pytest.param(
        0, 0,
        [(1250000, 0, 200, 0), (2500000, 30, 70, 0),
         (3750000, 0, 0, 0), (375000, 0, 0, 0), (0, 0, 0, 0)],
        [1500, 6700, 16800, 31800, 33300],
        [1700, 6800, 16800, 31800, 33300],
        [0, 1700, 6800, 16800, 31800],
        id="stopped-owner-profile-handoffs-rebase-all-periods"),
    pytest.param(
        0, 0,
        [(0, 0, 0, 5000), (1250000, 0, 200, 100), (0, 0, 0, 0)],
        [1500, 3000, 11700], [1500, 6700, 11700], [0, 6500, 6800],
        id="late-profile-handoff-rebases-from-current-time"),
]


@pytest.mark.parametrize(
    "initial,ready_spins,steps,deadlines,service_times,wait_entries", CASES)
def test_core1_release_loop(release_executable, initial, ready_spins, steps,
                            deadlines, service_times, wait_entries):
    cycles_per_us = 250
    initial *= cycles_per_us
    steps = [(period, poll * cycles_per_us, apply * cycles_per_us,
              service * cycles_per_us) for period, poll, apply, service in steps]
    deadlines = [value * cycles_per_us for value in deadlines]
    service_times = [value * cycles_per_us for value in service_times]
    wait_entries = [value * cycles_per_us for value in wait_entries]
    _assert_release(release_executable, initial, ready_spins, 375000, steps,
                    deadlines, service_times, wait_entries)


def _assert_release(executable, initial, ready_spins, boot_cycles, steps,
                    deadlines, service_times, wait_entries):
    data = f"{initial} {ready_spins} {len(steps)} {boot_cycles}\n" + "\n".join(
        " ".join(map(str, step)) for step in steps) + "\n"
    result = subprocess.run([str(executable)], input=data,
                            capture_output=True, text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr
    events = [(name, int(time), int(value)) for name, time, value in
              (line.split(",") for line in result.stdout.splitlines())]
    names = []
    for _ in range(ready_spins):
        names += ["ready", "spin"]
    names += ["ready", "profile", "init"]
    for period, _, _, _ in steps:
        names += ["poll", "apply"]
        if period:
            names += ["profile"]
        names += ["run", "spin"]
    assert [event[0] for event in events] == names

    def selected(name):
        return [(time, value) for kind, time, value in events if kind == name]

    assert selected("run") == list(zip(service_times, range(len(steps))))
    assert selected("init") == [(initial + ready_spins * 25000, 0)]
    assert selected("ready") == [
        (initial + i * 25000, int(i == ready_spins))
        for i in range(ready_spins + 1)]
    assert selected("profile") == [
        (initial + ready_spins * 25000, boot_cycles)] + [
        (service_times[i], step[0]) for i, step in enumerate(steps) if step[0]]
    # The first action after each polling release sees either its exact
    # deadline, or the current time if the previous service was already late.
    assert selected("poll") == [
        (max(entry, deadline), 0)
        for entry, deadline in zip(wait_entries, deadlines)]
    assert selected("poll") == [
        (service_times[i] - step[1] - step[2], 0)
        for i, step in enumerate(steps)]
    assert selected("apply") == [
        (service_times[i] - step[2], int(step[0] != 0))
        for i, step in enumerate(steps)]


@pytest.mark.parametrize("boot_cycles,steps,deadlines,service_times,wait_entries", [
    pytest.param(
        500000, [(0, 0, 0, 17), (0, 0, 0, 0), (0, 0, 0, 0)],
        [500000, 1000000, 1500000], [500000, 1000000, 1500000],
        [0, 500017, 1000000], id="flash-restored-cycle-controls-first-release"),
    pytest.param(
        375001, [(0, 0, 0, 17), (0, 0, 0, 0), (0, 0, 0, 0)],
        [375001, 750002, 1125003], [375001, 750002, 1125003],
        [0, 375018, 750002], id="fractional-us-boot-profile-never-truncated"),
    pytest.param(
        375000, [(375001, 3, 5, 7), (0, 0, 0, 0), (0, 0, 0, 0)],
        [375000, 750009, 1125010], [375008, 750009, 1125010],
        [0, 375015, 750009], id="fractional-us-handoff-never-truncated"),
])
def test_cycle_precision_and_boot_restore(release_executable, boot_cycles, steps,
                                          deadlines, service_times, wait_entries):
    _assert_release(release_executable, 0, 0, boot_cycles, steps,
                    deadlines, service_times, wait_entries)
