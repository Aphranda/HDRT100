"""Run the production exact reader with real history/observer lifecycle bodies.

The enabled harness inherits only the existing MMIO/clock simulations, and
instruments atomic loads/fences to check bounded cost and failed-copy isolation.
The disabled harness compiles the complete production include under the real
feature switch. No lookup algorithm is reimplemented by these fixtures.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from test_tdma_event_recovery import recovery_source
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]
EVENT = ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc"


def owner_wrapper() -> str:
    source = (ROOT / "components/tdma/src/tdma_runtime_owner.c").read_text(encoding="utf-8")
    return """
static bool s_tdma_runtime_owner_initialized;
#define s_tdma_pio_spi_phys physical
tdma_event_exact_result_t tdma_runtime_owner_copy_event_history_exact(
    uint64_t expected_arm_epoch, uint32_t expected_observer_epoch,
    uint32_t source_sequence, tdma_pio_spi_event_exact_t *out) {
""" + c_definition_body(source, "tdma_runtime_owner_copy_event_history_exact") + "}\n"


def execute(directory: Path, source: str, *, enabled: bool, short_enums: bool) -> str:
    unit = directory / "event_exact.c"
    unit.write_text(source, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / "event_exact.exe"
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/tdma/inc"), str(unit)]
    if short_enums:
        command.append("-fshort-enums")
    if enabled:
        command += [str(ROOT / "components/tdma/src" / name) for name in
                    ("tdma_event_observer.c", "tdma_event_history.c", "tdma_rx_sequence.c")]
    command += ["-o", str(executable)]
    for stage, call in (("compile", command), ("run", [str(executable)])):
        result = subprocess.run(call, capture_output=True, text=True)
        (directory / f"event_exact-{stage}.json").write_text(json.dumps({
            "command": call, "returncode": result.returncode}, indent=2), encoding="utf-8")
        (directory / f"event_exact-{stage}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


@pytest.mark.parametrize("short_enums", [False, True])
def test_exact_live_history_and_owner(tmp_path: Path, short_enums: bool) -> None:
    source = recovery_source(tmp_path)
    pos = source.rindex("int main(void)")
    source = source[:pos] + source[pos:].replace("int main(void)", "int recovery_regression_main(void)", 1)
    production = EVENT.read_text(encoding="utf-8")
    block = production[production.index("/* EVENT_HISTORY_EXACT_BEGIN"):
                       production.index("/* EVENT_HISTORY_EXACT_END */")]
    block = block.replace("__atomic_thread_fence(__ATOMIC_ACQ_REL);", "exact_read_fence();")
    output = execute(tmp_path, source + EXACT_FIXTURE + block +
                     "\n#undef __atomic_load_n\n" + owner_wrapper() + EXACT_CASES,
                     enabled=True, short_enums=short_enums)
    assert "event exact: 9 production groups passed; record loads <= 10; no IRQ mask/MMIO" in output


EXACT_FIXTURE = r'''
#undef __atomic_load_n
#undef __atomic_store_n
#undef __atomic_thread_fence
#define PICO_ON_DEVICE 1
#define __not_in_flash(name)
static unsigned exact_core = 1u, exact_record_loads, exact_fences;
static unsigned get_core_num(void) { return exact_core; }
static void (*exact_hook)(void);
static void exact_note_load(const void *address) {
    const uintptr_t p = (uintptr_t)address;
    if (p >= (uintptr_t)s_tdma_event_history.records &&
        p < (uintptr_t)(s_tdma_event_history.records + TDMA_EVENT_HISTORY_CAPACITY))
        ++exact_record_loads;
}
static void exact_read_fence(void) {
    ++exact_fences;
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (exact_hook != NULL) exact_hook();
}
#define __atomic_load_n(p, order) (exact_note_load(p), __atomic_load_n(p, order))
'''


EXACT_CASES = r'''
static void exact_setup(void) {
    exact_core = 1u; exact_hook = NULL; exact_record_loads = exact_fences = 0u;
    live_clock_available = true; live_tick_fail_at = live_clock_fail_at = 0u;
    live_tick_step = 5u; live_tick_now = UINT64_C(0x1234567800000000);
    recovery_setup();
    memset(s_tdma_event_live_words, 0, sizeof(s_tdma_event_live_words));
    s_tdma_event_live_guard = 0u;
    s_tdma_runtime_owner_initialized = true;
}
static tdma_pio_spi_event_exact_t exact_call(uint64_t arm, uint32_t epoch, uint32_t seq,
                                           tdma_event_exact_result_t expected) {
    tdma_pio_spi_event_exact_t out, sentinel;
    memset(&sentinel, 0xa5, sizeof(sentinel)); out = sentinel;
    const unsigned checks = live_clock_checks, ticks = live_tick_reads;
    const unsigned hz = hz_reads, gpio = gpio_reads, fifo_count = fifo_reads, levels = fifo_level_reads;
    const unsigned saves = interrupt_saves, restores = interrupt_restores, times = time_reads;
    const uint32_t mask = interrupt_mask;
    const uint64_t before_time = clock_us;
    const tdma_event_history_reason_t query_reason = s_tdma_event_history.query_reason;
    exact_record_loads = exact_fences = 0u;
    assert(tdma_runtime_owner_copy_event_history_exact(arm, epoch, seq, &out) == expected);
    assert(exact_record_loads <= 10u && exact_fences <= 1u);
    if (!exact_hook) {
        assert(checks == live_clock_checks && ticks == live_tick_reads && hz == hz_reads && gpio == gpio_reads);
        assert(fifo_count == fifo_reads && levels == fifo_level_reads && clock_us == before_time);
        assert(saves == interrupt_saves && restores == interrupt_restores && times == time_reads);
        assert(mask == interrupt_mask && query_reason == s_tdma_event_history.query_reason);
    }
    if (expected != TDMA_EVENT_EXACT_OK) assert(memcmp(&out, &sentinel, sizeof(out)) == 0);
    else {
        assert(exact_record_loads == 10u && out.record.sequence == seq);
        assert(out.arm_epoch == arm && out.observer_epoch == epoch && out.tick_hz == s_tdma_event_hz);
        assert(out.flags == (TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID));
        assert(out.timer1_enable_before != s_tdma_event_history.initial_start.lo);
        assert(out.timer1_enable_before <= out.timer1_enable_after);
    }
    return out;
}
static tdma_pio_spi_event_exact_t exact_current(uint32_t seq, tdma_event_exact_result_t expected) {
    return exact_call(s_tdma_pio_spi_rx_arm_epoch, s_tdma_event_history.epoch, seq, expected);
}
static void test_exact_arguments_and_core(void) {
    exact_setup(); accept_event(0u, 0u);
    (void)exact_current(0u, TDMA_EVENT_EXACT_OK);
    (void)exact_call(0u, s_tdma_event_history.epoch, 0u, TDMA_EVENT_EXACT_BAD_ARGUMENT);
    (void)exact_call(s_tdma_pio_spi_rx_arm_epoch, 0u, 0u, TDMA_EVENT_EXACT_BAD_ARGUMENT);
    assert(tdma_runtime_owner_copy_event_history_exact(1u, 1u, 0u, NULL) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    tdma_pio_spi_event_exact_t out, sentinel; memset(&sentinel, 0x39, sizeof(sentinel)); out = sentinel;
    assert(tdma_pio_spi_phys_event_copy_history_exact(NULL, 1u, 1u, 0u, &out) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    assert(memcmp(&out, &sentinel, sizeof(out)) == 0);
    exact_core = 0u;
    (void)exact_current(0u, TDMA_EVENT_EXACT_BAD_ARGUMENT);
    assert(tdma_pio_spi_phys_event_copy_history_exact(&physical, 1u, 1u, 0u, &out) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    s_tdma_runtime_owner_initialized = false;
    (void)exact_current(0u, TDMA_EVENT_EXACT_BAD_ARGUMENT);
    exact_core = 1u;
    (void)exact_current(0u, TDMA_EVENT_EXACT_RETIRED);
}
static void test_exact_index_and_anchor(void) {
    exact_setup(); accept_event(0u, 70000u);
    for (uint32_t i = 1u; i < 40u; ++i) {
        retain(70000u + i);
        const uint32_t oldest = i < TDMA_EVENT_HISTORY_CAPACITY ? 0u : i + 1u - TDMA_EVENT_HISTORY_CAPACITY;
        for (uint32_t ordinal = oldest; ordinal <= i; ++ordinal) {
            const tdma_pio_spi_event_exact_t out = exact_current(70000u + ordinal, TDMA_EVENT_EXACT_OK);
            assert(out.record.ordinal == ordinal);
            if (ordinal != 0u) assert(out.record.raw_rx == 100u + ordinal);
        }
    }
    /* LIVE still names event 70000; exact lookup uses history, borrowing only its anchor. */
    tdma_pio_spi_event_live_snapshot_t live;
    assert(tdma_pio_spi_phys_event_get_live_snapshot(&physical, &live));
    assert(live.record.sequence == 70000u);
    const tdma_pio_spi_event_exact_t out = exact_current(70035u, TDMA_EVENT_EXACT_OK);
    assert(out.timer1_enable_before == live.timer1_enable_before);
    assert(out.timer1_enable_after == live.timer1_enable_after);
    assert(out.record.rx_elapsed_cycles == UINT64_C(36) * 50000u);
    (void)exact_current(70000u, TDMA_EVENT_EXACT_EVICTED);
    (void)exact_current(70040u, TDMA_EVENT_EXACT_PENDING);
    (void)exact_current(70035u - 65536u, TDMA_EVENT_EXACT_EVICTED);
    (void)exact_current(70035u + 65536u, TDMA_EVENT_EXACT_PENDING);
}
static void test_exact_pending_then_harvest(void) {
    exact_setup(); accept_event(0u, 100u);
    (void)exact_current(101u, TDMA_EVENT_EXACT_PENDING);
    (void)exact_current(99u, TDMA_EVENT_EXACT_EVICTED);
    accept_event(1u, 101u);
    const tdma_pio_spi_event_exact_t out = exact_current(101u, TDMA_EVENT_EXACT_OK);
    assert(out.record.ordinal == 1u && out.record.rx_elapsed_cycles > 0u);
    const tdma_event_history_t saved = s_tdma_event_history;
    s_tdma_event_history.count = s_tdma_event_history.next = 0u;
    s_tdma_event_history.oldest_ordinal = UINT32_MAX;
    (void)exact_current(102u, TDMA_EVENT_EXACT_PENDING);
    s_tdma_event_history = saved;
}
static void test_exact_lifetime_and_recovery(void) {
    exact_setup(); accept_event(0u, 100u);
    const tdma_pio_spi_event_exact_t old = exact_current(100u, TDMA_EVENT_EXACT_OK);
    (void)exact_call(old.arm_epoch + UINT64_C(0x100000000), old.observer_epoch, 100u, TDMA_EVENT_EXACT_RETIRED);
    (void)exact_call(old.arm_epoch, old.observer_epoch + 1u, 100u, TDMA_EVENT_EXACT_RETIRED);
    tdma_pio_spi_phys_event_stop(&physical);
    (void)exact_call(old.arm_epoch, old.observer_epoch, 100u, TDMA_EVENT_EXACT_RETIRED);
    recovery_setup(); accept_event(0u, 100u);
    (void)exact_call(old.arm_epoch, old.observer_epoch, 100u, TDMA_EVENT_EXACT_RETIRED);
    (void)exact_current(100u, TDMA_EVENT_EXACT_OK);
    /* Real fault retirement followed by the existing two-step observer-only recovery. */
    load_event(1u, 100u); tdma_pio_spi_phys_event_service(&physical);
    (void)exact_current(100u, TDMA_EVENT_EXACT_RETIRED);
    reset_only_phase(); enable_only_phase(); accept_event(0u, 70001u);
    (void)exact_current(70001u, TDMA_EVENT_EXACT_OK);
    live_clock_available = false; tdma_event_live_monitor();
    (void)exact_current(70001u, TDMA_EVENT_EXACT_RETIRED);
    live_clock_available = true;
}
static unsigned exact_mutation;
static void exact_mutate(void) {
    if (exact_mutation == 0u) retain(101u);
    else if (exact_mutation == 1u) tdma_event_history_retire(&s_tdma_event_history);
    else if (exact_mutation == 2u) tdma_event_live_retire(TDMA_EVENT_LIVE_STOP);
    else { s_tdma_event_history.publication_guard += 2u; s_tdma_event_live_guard += 2u; }
}
static void test_exact_guards(void) {
    for (exact_mutation = 0u; exact_mutation < 4u; ++exact_mutation) {
        exact_setup(); accept_event(0u, 100u);
        exact_hook = exact_mutate;
        (void)exact_current(100u, TDMA_EVENT_EXACT_BUSY);
        assert(exact_fences == 1u);
        exact_hook = NULL;
    }
    exact_setup(); accept_event(0u, 100u);
    s_tdma_event_history.publication_guard |= 1u;
    (void)exact_current(100u, TDMA_EVENT_EXACT_BUSY);
    s_tdma_event_history.publication_guard = 0u; s_tdma_event_live_guard |= 1u;
    (void)exact_current(100u, TDMA_EVENT_EXACT_BUSY);
    s_tdma_event_live_guard = UINT32_MAX - 1u;
    tdma_event_live_record(&s_tdma_event_records[0]); assert(s_tdma_event_live_guard == 0u);
    s_tdma_event_history.publication_guard = UINT32_MAX - 1u;
    retain(101u); assert(s_tdma_event_history.publication_guard == 0u);
    (void)exact_current(100u, TDMA_EVENT_EXACT_OK);
}
static void test_exact_corrupt_history(void) {
    for (unsigned mode = 0u; mode < 10u; ++mode) {
        exact_setup(); accept_event(0u, 100u); retain(101u);
        if (mode == 0u) s_tdma_event_history.count = TDMA_EVENT_HISTORY_CAPACITY + 1u;
        if (mode == 1u) s_tdma_event_history.next = TDMA_EVENT_HISTORY_CAPACITY;
        if (mode == 2u) s_tdma_event_history.pio_hz = 0u;
        if (mode == 3u) s_tdma_event_history.oldest_ordinal = UINT32_MAX;
        if (mode == 4u) s_tdma_event_history.records[1].ordinal = 4u;
        if (mode == 5u) s_tdma_event_history.records[0].sequence = 99u;
        if (mode == 6u) s_tdma_event_history.records[0].ordinal = 1u;
        if (mode == 7u) s_tdma_event_history.records[0].rx_elapsed_cycles = 0u;
        if (mode == 8u) s_tdma_event_history.records[0].tx_elapsed_cycles = 0u;
        if (mode == 9u) s_tdma_event_history.count = 0u;
        (void)exact_current(100u, TDMA_EVENT_EXACT_BAD_STATE);
    }
}
static void test_exact_anchor_faults(void) {
    for (unsigned mode = 0u; mode < 15u; ++mode) {
        exact_setup(); accept_event(0u, 100u);
        union { tdma_pio_spi_event_live_snapshot_t snapshot; uint32_t words[24]; } live;
        memcpy(live.words, s_tdma_event_live_words, sizeof(live.words));
        tdma_event_exact_result_t expected = TDMA_EVENT_EXACT_RETIRED;
        if (mode == 0u) ++live.snapshot.record.epoch;
        if (mode == 1u) ++live.snapshot.tick_hz;
        if (mode == 2u) live.snapshot.arm_epoch += UINT64_C(0x100000000);
        if (mode == 3u) live.snapshot.flags &= ~TDMA_EVENT_LIVE_RETAINED;
        if (mode == 4u) live.snapshot.flags &= ~TDMA_EVENT_LIVE_ACTIVE;
        if (mode == 5u) live.snapshot.flags &= ~TDMA_EVENT_LIVE_ANCHOR_VALID;
        if (mode == 6u) live.snapshot.flags |= TDMA_EVENT_LIVE_STOP;
        if (mode == 7u) live.snapshot.flags |= TDMA_EVENT_LIVE_INVALID;
        if (mode == 8u) live.snapshot.flags |= TDMA_EVENT_LIVE_ARM;
        if (mode == 9u) live.snapshot.flags |= TDMA_EVENT_LIVE_CLOCK;
        if (mode == 10u) live.snapshot.flags |= TDMA_EVENT_LIVE_BINDING;
        if (mode == 11u) live.snapshot.flags |= TDMA_EVENT_LIVE_ANCHOR_UNAVAILABLE;
        if (mode == 12u) { live.snapshot.timer1_enable_after = UINT64_MAX; expected = TDMA_EVENT_EXACT_BAD_STATE; }
        if (mode == 13u) { live.snapshot.timer1_enable_before = live.snapshot.timer1_enable_after + 1u; expected = TDMA_EVENT_EXACT_BAD_STATE; }
        if (mode == 14u) physical.armed = false;
        memcpy(s_tdma_event_live_words, live.words, sizeof(live.words));
        (void)exact_current(100u, expected);
    }
}
static void test_exact_terminal_boundaries(void) {
    exact_setup(); accept_event(0u, UINT32_MAX - 1u); accept_event(1u, UINT32_MAX);
    (void)exact_current(UINT32_MAX, TDMA_EVENT_EXACT_OK);
    (void)exact_current(UINT32_MAX - 1u, TDMA_EVENT_EXACT_OK);
    load_event(2u, 0u); tdma_pio_spi_phys_event_service(&physical);
    (void)exact_current(UINT32_MAX, TDMA_EVENT_EXACT_RETIRED);
    /* Generic history permits wrap, but the production exact facade must not
     * admit this impossible observer lifetime or alias UINT32_MAX onto it. */
    exact_setup(); accept_event(0u, UINT32_MAX); retain(0u);
    (void)exact_current(UINT32_MAX, TDMA_EVENT_EXACT_BAD_STATE);
    (void)exact_current(0u, TDMA_EVENT_EXACT_BAD_STATE);
    exact_setup(); accept_event(0u, 100u);
    s_tdma_event_history.accepted = UINT32_MAX;
    s_tdma_event_history.oldest_ordinal = UINT32_MAX - 1u;
    s_tdma_event_history.records[0].ordinal = UINT32_MAX - 1u;
    tdma_event_record_t terminal = s_tdma_event_records[0];
    terminal.ordinal = UINT32_MAX; terminal.sequence = 101u;
    terminal.rx_elapsed_cycles += 100u; terminal.tx_elapsed_cycles += 100u;
    assert(tdma_event_history_append(&s_tdma_event_history, &terminal, 1u, (uint64_t)UINT32_MAX + 1u));
    const tdma_pio_spi_event_exact_t out = exact_current(101u, TDMA_EVENT_EXACT_OK);
    assert(out.record.ordinal == UINT32_MAX);
    (void)exact_current(100u, TDMA_EVENT_EXACT_OK);
}
static void test_exact_irq_mask_independence(void) {
    exact_setup(); accept_event(0u, 100u);
    for (uint32_t irq = 0u; irq < 2u; ++irq) {
        interrupt_mask = irq; copying = true; time_reads = 0u; read_delay = UINT64_MAX;
        (void)exact_current(100u, TDMA_EVENT_EXACT_OK);
        assert(interrupt_mask == irq && time_reads == 0u);
    }
    copying = false; read_delay = 0u; interrupt_mask = 0u;
}
int main(void) {
    test_exact_arguments_and_core(); test_exact_index_and_anchor();
    test_exact_pending_then_harvest(); test_exact_lifetime_and_recovery();
    test_exact_guards(); test_exact_corrupt_history(); test_exact_anchor_faults();
    test_exact_terminal_boundaries(); test_exact_irq_mask_independence();
    puts("event exact: 9 production groups passed; record loads <= 10; no IRQ mask/MMIO");
    return 0;
}
'''


@pytest.mark.parametrize("short_enums", [False, True])
def test_exact_feature_disabled(tmp_path: Path, short_enums: bool) -> None:
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    end = header.index("} tdma_pio_spi_event_snapshot_t;") + len("} tdma_pio_spi_event_snapshot_t;")
    start = header.rfind("typedef struct {", 0, end)
    types_end = header.index("} tdma_pio_spi_event_window_t;") + len("} tdma_pio_spi_event_window_t;")
    types = header[header.index("#define TDMA_PIO_SPI_EVENT_TAP_MAX_DELAY_CYCLES"):types_end]
    source = DISABLED_PREFIX + header[start:end] + "\n" + types + DISABLED_FIXTURE
    source += f'#include "{EVENT.as_posix()}"\n' + owner_wrapper() + DISABLED_CASES
    assert "event exact disabled: complete production include passed" in execute(
        tmp_path, source, enabled=False, short_enums=short_enums)


DISABLED_PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_rx_capture.h"
#include "tdma_rx_event_candidate.h"
#include "tdma_frozen_geometry.h"
#include "tdma_event_history.h"
typedef unsigned uint;
'''
DISABLED_FIXTURE = r'''
typedef struct {
    struct { tdma_pio_spi_event_snapshot_t event; } snapshot;
    tdma_pio_spi_event_snapshot_t flight_event_alternate;
    uint32_t flight_event_guard;
} tdma_pio_spi_phys_t;
typedef struct { uint32_t cycle_period_ns, geometry_generation; } tdma_ring_runtime_config_t;
static tdma_pio_spi_phys_t physical;
static void tdma_geometry_observer_record(uint32_t generation, uint32_t epoch,
    uint32_t state, uint32_t reason, uint32_t prefix) {
    (void)generation; (void)epoch; (void)state; (void)reason; (void)prefix;
}
static unsigned exact_core = 1u;
static unsigned get_core_num(void) { return exact_core; }
static uint64_t time_us_64(void) { return 1u; }
static uint32_t save_and_disable_interrupts(void) { return 0u; }
static void restore_interrupts(uint32_t saved) { assert(saved == 0u); }
#define __dmb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define PICO_ON_DEVICE 1
#define PROJECT_TDMA_EVENT_OBSERVER 0
'''
DISABLED_CASES = r'''
int main(void) {
    tdma_pio_spi_event_exact_t out, sentinel; memset(&sentinel, 0x9b, sizeof(sentinel)); out = sentinel;
    s_tdma_runtime_owner_initialized = true;
    assert(tdma_runtime_owner_copy_event_history_exact(1u, 1u, 123u, &out) == TDMA_EVENT_EXACT_RETIRED);
    assert(tdma_pio_spi_phys_event_copy_history_exact(&physical, 1u, 1u, 123u, &out) == TDMA_EVENT_EXACT_RETIRED);
    assert(tdma_runtime_owner_copy_event_history_exact(0u, 1u, 123u, &out) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    assert(tdma_runtime_owner_copy_event_history_exact(1u, 0u, 123u, &out) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    assert(tdma_pio_spi_phys_event_copy_history_exact(NULL, 1u, 1u, 123u, &out) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    assert(tdma_pio_spi_phys_event_copy_history_exact(&physical, 1u, 1u, 123u, NULL) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    exact_core = 0u;
    assert(tdma_runtime_owner_copy_event_history_exact(1u, 1u, 123u, &out) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    assert(tdma_pio_spi_phys_event_copy_history_exact(&physical, 1u, 1u, 123u, &out) == TDMA_EVENT_EXACT_BAD_ARGUMENT);
    assert(memcmp(&out, &sentinel, sizeof(out)) == 0);
    /* Exercise the full disabled translation unit's static entrypoints too. */
    tdma_rx_start_cut_arm_begin(); tdma_rx_start_cut_disarmed();
    tdma_ring_runtime_config_t config = {0};
    tdma_rx_first_window_admit(&config); tdma_rx_first_window_arm_failed();
    tdma_rx_first_window_retire(TDMA_RX_FIRST_STOP_BEFORE_COMPLETE, true);
    assert(tdma_pio_spi_phys_event_prelaunch(&physical, &config));
    assert(!tdma_pio_spi_phys_event_selected(&physical));
    const tdma_rx_capture_t capture = {.capture_id = 7u, .arm_epoch = 9u};
    tdma_pio_spi_phys_rx_event_query(&physical, &capture, 0u, 0u, 64u, 8u);
    tdma_pio_spi_phys_event_service(&physical); tdma_pio_spi_phys_event_stop(&physical);
    tdma_pio_spi_phys_event_prepare(&physical, NULL);
    tdma_pio_spi_event_snapshot_t snapshot;
    assert(tdma_pio_spi_phys_event_copy(&physical, &snapshot));
    puts("event exact disabled: complete production include passed"); return 0;
}
'''
