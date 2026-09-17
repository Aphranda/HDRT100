"""Execute the actual Core1 release loop with a deterministic SDK clock.

This checks release semantics, not measured hardware jitter or phase WCET.
No alarm-pool/sleep mock is provided: restoring sleep_until must fail to build.
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
typedef struct { uint64_t us; } absolute_time_t;
static uint64_t now_us;
static unsigned ready_spins, limit, iteration;
static bool initialized;
static uint32_t period_cycles[16];
static uint64_t poll_cost[16], apply_cost[16], service_cost[16];
static jmp_buf finished;
static void event(const char *name, uint64_t value) {
    printf("%s,%" PRIu64 ",%" PRIu64 "\n", name, now_us, value);
}
static bool app_is_ready(void) {
    event("ready", ready_spins == 0);
    return ready_spins == 0;
}
static void tight_loop_contents(void) {
    event("spin", 0);
    if (!initialized) {
        assert(ready_spins > 0);
        --ready_spins;
        now_us += 100;
    } else if (iteration == limit) {
        longjmp(finished, 1);
    }
}
static void app_realtime_cycle_counter_init(void) {
    assert(!initialized);
    initialized = true;
    event("init", 0);
}
static absolute_time_t get_absolute_time(void) {
    event("now", now_us);
    return (absolute_time_t){now_us};
}
static uint64_t to_us_since_boot(absolute_time_t time) { return time.us; }
static absolute_time_t from_us_since_boot(uint64_t us) {
    return (absolute_time_t){us};
}
static void busy_wait_until(absolute_time_t deadline) {
    assert(initialized && iteration < limit);
    event("wait", deadline.us);
    if (now_us < deadline.us) now_us = deadline.us;
}
static void drv_flash_core1_lockout_poll(void) {
    event("poll", 0);
    now_us += poll_cost[iteration];
}
static bool app_realtime_apply_pending_profile_core1(void) {
    event("apply", period_cycles[iteration] != 0);
    now_us += apply_cost[iteration];
    return period_cycles[iteration] != 0;
}
static uint32_t app_realtime_cycle_cycles_core1(void) {
    assert(period_cycles[iteration] != 0);
    event("profile", period_cycles[iteration]);
    return period_cycles[iteration];
}
static void app_realtime_run_once(void) {
    event("run", iteration);
    now_us += service_cost[iteration];
    ++iteration;
}
'''

MAIN = r'''
int main(void) {
    assert(scanf("%" SCNu64 " %u %u", &now_us, &ready_spins, &limit) == 3);
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
        [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I" + str(ROOT / "config"), str(test_source), "-o", str(executable)],
        capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


# Inputs model SDK time and owner decisions. Expected deadlines and service
# instants are explicit scenario outcomes, not a second scheduler algorithm.
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
    data = f"{initial} {ready_spins} {len(steps)}\n" + "\n".join(
        " ".join(map(str, step)) for step in steps) + "\n"
    result = subprocess.run([str(release_executable)], input=data,
                            capture_output=True, text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr
    events = [(name, int(time), int(value)) for name, time, value in
              (line.split(",") for line in result.stdout.splitlines())]
    names = []
    for _ in range(ready_spins):
        names += ["ready", "spin"]
    names += ["ready", "init", "now"]
    for period, _, _, _ in steps:
        names += ["wait", "poll", "apply"]
        if period:
            names += ["profile", "now"]
        names += ["run", "spin"]
    assert [event[0] for event in events] == names

    def selected(name):
        return [(time, value) for kind, time, value in events if kind == name]

    assert selected("wait") == list(zip(wait_entries, deadlines))
    assert selected("run") == list(zip(service_times, range(len(steps))))
    assert selected("init") == [(initial + ready_spins * 100, 0)]
    assert selected("ready") == [
        (initial + i * 100, int(i == ready_spins))
        for i in range(ready_spins + 1)]
    assert selected("profile") == [
        (service_times[i], step[0]) for i, step in enumerate(steps) if step[0]]
    assert selected("now") == [
        (initial + ready_spins * 100, initial + ready_spins * 100)] + [
        (service_times[i], service_times[i])
        for i, step in enumerate(steps) if step[0]]
    assert selected("poll") == [
        (service_times[i] - step[1] - step[2], 0)
        for i, step in enumerate(steps)]
    assert selected("apply") == [
        (service_times[i] - step[2], int(step[0] != 0))
        for i, step in enumerate(steps)]
