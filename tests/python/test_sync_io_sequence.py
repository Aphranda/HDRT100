"""Production receipt logic and assembled PIO instructions, without hardware."""
import os
from collections import deque
from pathlib import Path
import re
import shutil
import subprocess
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[2]
UINT32 = (1 << 32) - 1


def function(source, name):
    start = re.search(rf"(?m)^(?:static )?(?:bool|void|uint32_t) {name}\([^;{{}}]*\)\s*\{{", source)
    assert start, name
    end = source.index("\n}\n", start.start()) + 3
    return source[start.start():end]


def test_sequence_backend_receipts_and_admission(tmp_path):
    backend = (ROOT / "components/sync_io/src/sync_io_sequence.c").read_text(encoding="utf-8")
    harness = '#include "sync_io_sequence_fake.h"\n'
    for name in ("config_valid", "logical_index_for_transfer", "build_plan", "receive_word", "account_input"):
        harness += function(backend, name)
    harness += (ROOT / "tests/unit/test_sync_io_sequence.c").read_text(encoding="utf-8")
    path = tmp_path / "sequence.c"
    path.write_text(harness, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler, "host C compiler required"
    executable = tmp_path / "sequence.exe"
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
               "-I" + str(ROOT / "tests/unit"),
               "-I" + str(ROOT / "components/sync_io/inc"), str(path), "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert ran.returncode == 0, ran.stdout + ran.stderr


def test_turntable_borrows_only_idle_capture_and_restores_owner(tmp_path):
    # Exercise the real sync_io owner boundary, not a second implementation of
    # its admission rules. Concurrent capture/analyzer work owns this workspace.
    from test_sync_io_workspace import compile_run

    source = (ROOT / "components/sync_io/src/sync_io.c").read_text(encoding="utf-8")
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "sync_io_persona_resources.h"
static struct {
    bool initialized, capture_running, capture_timebase_valid;
    bool capture_dma_write_index_valid;
    unsigned capture_offset;
    uint32_t capture_sample_hz, dropped_capture_words;
    uint32_t capture_dma_read_seq, capture_dma_produced_seq;
    uint32_t capture_dma_last_write_index;
    uint64_t capture_timebase_start_ns;
} s_sync_io;
static unsigned core = 1u, hardware_writes, restores, aborts;
static bool enabled, claimed = true, dma_busy;
#define BOARD_SYNC_PIO_FAST 0u
#define BOARD_SYNC_CAPTURE_SM 0u
#define BOARD_SYNC_INPUT_BASE_PIN 12u
#define BOARD_SYNC_INPUT_PIN_COUNT 4u
#define get_core_num() core
#define sync_io_core_sm_is_enabled(p,s) enabled
#define pio_sm_is_claimed(p,s) claimed
#define dma_channel_is_busy(c) dma_busy
#define pio_sm_set_enabled(p,s,on) (++hardware_writes, enabled = (on))
#define pio_sm_clear_fifos(...) (++hardware_writes)
#define pio_sm_set_clkdiv(...) (++hardware_writes)
#define pio_sm_restart(...) (++hardware_writes)
#define dma_start_channel_mask(...) (++hardware_writes)
#define dma_channel_abort(...) (++aborts)
#define sync_io_model_pulse_schedule_is_running() false
#define sync_io_core_wave_output_persona_active() false
#define sync_io_common_time_now_ns() 1234u
#define osal_critical_enter() ((void)0)
#define osal_critical_exit() ((void)0)
#define sync_io_capture_latch_reset_locked() ((void)0)
#define sync_io_trace(...) ((void)0)
#define sync_io_clkdiv_for_instruction_rate(hz) (hz)
static bool sync_io_capture_dma_configure(void) { return true; }
static void sync_capture_4bit_program_init(unsigned pio, unsigned sm,
    unsigned offset, unsigned base, unsigned count, unsigned rate) {
    assert(pio == 0u && sm == 0u && offset == 17u);
    assert(base == 12u && count == 4u && rate == 123456u);
    ++restores;
    enabled = true; /* restore must explicitly leave the resident SM idle */
}
'''
    for name in ("sync_io_core_capture_sm_lease", "sync_io_core_capture_sm_restore",
                 "sync_io_stop_capture", "sync_io_start_capture"):
        harness += function(source, name)
    harness += r'''
int main(void) {
    static int turntable, analyzer;
    s_sync_io.capture_offset = 17u;
    s_sync_io.capture_sample_hz = 123456u;
    assert(!sync_io_core_capture_sm_lease(&turntable));
    s_sync_io.initialized = true;
    core = 0u;
    assert(!sync_io_core_capture_sm_lease(&turntable));
    core = 1u;
    assert(!sync_io_core_capture_sm_lease(NULL));
    assert(sync_io_workspace_claim(&analyzer));
    assert(!sync_io_core_capture_sm_lease(&turntable));
    assert(sync_io_workspace_held_by(&analyzer));
    assert(sync_io_workspace_release(&analyzer));
    for (unsigned condition = 0u; condition < 4u; ++condition) {
        s_sync_io.capture_running = condition == 0u;
        enabled = condition == 1u;
        claimed = condition != 2u;
        dma_busy = condition == 3u;
        assert(!sync_io_core_capture_sm_lease(&turntable));
        assert(sync_io_workspace_claim(&analyzer));
        assert(sync_io_workspace_release(&analyzer));
    }
    s_sync_io.capture_running = enabled = dma_busy = false;
    claimed = true;
    assert(hardware_writes == 0u && aborts == 0u);
    assert(sync_io_core_capture_sm_lease(&turntable));
    assert(!sync_io_core_capture_sm_lease(&turntable));
    assert(!sync_io_workspace_claim(&analyzer));
    assert(!sync_io_start_capture(1000000u));
    sync_io_stop_capture(); /* idle capture STOP must not touch the lent SM */
    sync_io_core_capture_sm_restore(&analyzer); /* wrong owner cannot restore */
    assert(hardware_writes == 0u && aborts == 0u && restores == 0u);
    core = 0u;
    sync_io_core_capture_sm_restore(&turntable);
    assert(sync_io_workspace_held_by(&turntable));
    core = 1u;
    sync_io_core_capture_sm_restore(&turntable);
    assert(restores == 1u && !enabled && !sync_io_workspace_held_by(&turntable));
    sync_io_core_capture_sm_restore(&turntable);
    assert(restores == 1u);
    assert(sync_io_start_capture(123456u));
    assert(!sync_io_core_capture_sm_lease(&turntable));
    sync_io_stop_capture();
    assert(sync_io_core_capture_sm_lease(&turntable));
    sync_io_core_capture_sm_restore(&turntable);
    assert(restores == 2u && !enabled);
    return 0;
}
'''
    compile_run(tmp_path, harness)


@pytest.fixture(scope="module")
def programs(tmp_path_factory):
    directory = tmp_path_factory.mktemp("sequence-pio")
    assembler = (os.environ.get("PIOASM") or shutil.which("pioasm") or
                 str(Path.home() / ".pico-sdk/tools/2.2.0/pioasm/pioasm.exe"))
    assert Path(assembler).is_file(), "pioasm required to test actual instruction words"
    output = directory / "sequence.h"
    result = subprocess.run([assembler, "-o", "c-sdk",
                             str(ROOT / "components/sync_io/src/sync_io_sequence.pio"),
                             str(output)], capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    text = output.read_text(encoding="utf-8")
    parsed = {}
    for name in ("ingress", "executor", "counter", "gateway", "finite_ingress", "none_executor", "ready_counter"):
        body = text.split(f"sequence_{name}_program_instructions[] = {{", 1)[1].split("};", 1)[0]
        parsed[name] = [int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{4}),", body)]
    assert [len(parsed[name]) for name in parsed] == [6, 18, 5, 7, 8, 12, 6]
    assert sum(len(parsed[name]) for name in ("ingress", "executor", "counter")) + 1 <= 32
    assert sum(len(parsed[name]) for name in ("gateway", "none_executor", "ready_counter")) + 1 == 26
    assert sum(len(parsed[name]) for name in ("gateway", "none_executor", "ready_counter", "counter")) + 1 == 31
    assert sum(len(parsed[name]) for name in ("finite_ingress", "executor", "counter")) + 1 == 32
    return parsed


def test_production_hot_load_and_pause_boundaries(tmp_path):
    backend = (ROOT / "components/sync_io/src/sync_io_sequence.c").read_text(encoding="utf-8")
    production = "\n".join(function(backend, name) for name in (
        "sm_pc", "clear_owned_flags", "safe_low", "stop_hardware", "cleanup", "load_hardware",
        "stop_hook", "feedback_capture_enabled", "configure_feedback_executor",
        "prime_initial_state", "start_hardware", "read_sm_register", "stop_counter", "finish_ingress",
        "produced_receipts", "stop_receipt_dma", "resume_receipt_dma", "logical_index_for_transfer",
        "receive_word", "drain_receipts", "pending_request", "drain_idle_executor", "account_input",
            "account_counter", "account_counter_sources",
            "gateway_start", "gateway_service", "gateway_cancel",
            "sync_io_sequence_service",
            "sync_io_sequence_gateway_fire", "sync_io_sequence_gateway_ready",
            "sync_io_sequence_counter_rearm", "sync_io_sequence_counter_inject",
            "sync_io_sequence_software_step",
            "sync_io_sequence_pause", "sync_io_sequence_stop"))
    template = (ROOT / "tests/unit/test_sync_io_sequence_resources.c").read_text(encoding="utf-8")
    harness = tmp_path / "resources.c"
    harness.write_text(template.replace("/* PRODUCTION_FUNCTIONS */", production), encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    executable = tmp_path / "resources.exe"
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
               "-I" + str(ROOT / "tests/unit/host_stubs"),
               "-I" + str(ROOT / "boards/rp2350_trig/inc"),
               "-I" + str(ROOT / "components/sync_io/inc"),
               "-I" + str(ROOT / "components/resource_arbiter/inc"), str(harness),
               str(ROOT / "components/sync_io/src/sync_io_persona_resources.c"),
               str(ROOT / "components/sync_io/src/sync_io_persona_manager.c"), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


class Machine:
    """Small instruction-word runner, following the repository PIO model pattern.

    Only instructions assembled in the sequence programs are supported. GPIO
    synchronization and AHB contention require hardware verification.
    """
    def __init__(self, words, owner, name):
        self.words = words[:]
        self.owner, self.name = owner, name
        self.pc = self.x = self.y = self.isr = self.osr = 0
        self.tx, self.rx = deque(), deque()
        self.enabled = True
        self.rx_stall = False
        self.delay = 0
        self.irq_waiting = False

    def execute(self, instruction, flags, *, injected=False):
        major, arg = instruction >> 13, instruction & 255
        following = (self.pc + 1) % len(self.words)
        if major == 0:
            condition, target = arg >> 5, arg & 31
            if condition == 0:
                following = target
            elif condition == 1:
                if self.x == 0:
                    following = target
            elif condition == 2:
                branch = self.x != 0
                self.x = (self.x - 1) & UINT32
                if branch:
                    following = target
            elif condition == 4:
                branch = self.y != 0
                self.y = (self.y - 1) & UINT32
                if branch:
                    following = target
            else:
                raise AssertionError(("JMP", condition))
        elif major == 1:
            level, source, index = arg >> 7, (arg >> 5) & 3, arg & 31
            actual = self.owner.input if source == 1 else bool(flags & (1 << index))
            if actual != bool(level):
                return
            if source == 2 and level:
                self.owner.clear_flags |= 1 << index
        elif major == 4:
            pull, block = bool(arg & 128), bool(arg & 32)
            if pull:
                if not self.tx:
                    assert block
                    return
                self.osr = self.tx.popleft()
            else:
                if len(self.rx) >= (8 if self.name == "counter" else 4):
                    if block:
                        self.rx_stall = True
                        return
                else:
                    self.rx.append(self.isr)
                self.isr = 0
        elif major == 2:
            source, count = arg >> 5, arg & 31
            assert source == 2 and count == 0 and self.name == "executor"
            if len(self.rx) >= 4:
                self.rx_stall = True
                return
            self.rx.append(self.y)
            self.isr = 0
        elif major == 3:
            dest, count = arg >> 5, arg & 31
            count = count or 32
            value = self.osr & ((1 << count) - 1)
            self.osr >>= count
            if dest == 0:
                before = self.owner.pads
                self.owner.pads = value & 15
                if not (before & self.owner.status_mask) and (self.owner.pads & self.owner.status_mask):
                    self.owner.rises.append(self.owner.time)
                if (before & self.owner.status_mask) and not (self.owner.pads & self.owner.status_mask):
                    self.owner.falls.append(self.owner.time)
            elif dest != 3:
                raise AssertionError(("OUT", dest))
        elif major == 5:
            dest, operation, source = arg >> 5, (arg >> 3) & 3, arg & 7
            value = {1: self.x, 2: self.y, 3: 0, 5: UINT32 if flags & 16 else 0,
                     6: self.isr, 7: self.osr}[source]
            assert operation in (0, 1)
            if operation:
                value ^= UINT32
            if dest == 0:
                before = self.owner.pads
                self.owner.pads = value & 15
                if self.name == "executor" and self.pc == 6:
                    self.owner.writes.append((self.owner.time, value))
                if not (before & self.owner.status_mask) and (self.owner.pads & self.owner.status_mask):
                    self.owner.rises.append(self.owner.time)
                if (before & self.owner.status_mask) and not (self.owner.pads & self.owner.status_mask):
                    self.owner.falls.append(self.owner.time)
            elif dest == 1:
                self.x = value
            elif dest == 2:
                self.y = value
            elif dest == 6:
                self.isr = value
            elif dest == 7:
                self.osr = value
            else:
                raise AssertionError(("MOV", dest))
        elif major == 6:
            mask = 1 << (arg & 7)
            if arg & 64:
                self.owner.clear_flags |= mask
            elif arg & 32:
                # IRQ WAIT sets exactly once, then stalls until another SM
                # clears it. Instruction delay starts only after completion.
                if not self.irq_waiting:
                    self.owner.set_flags |= mask
                    self.irq_waiting = True
                    return
                if flags & mask:
                    return
                self.irq_waiting = False
            else:
                self.owner.set_flags |= mask
        elif major == 7:
            dest, value = arg >> 5, arg & 31
            assert dest == 0 and self.name == "gateway"
            before = self.owner.pads
            mask = self.owner.status_mask
            self.owner.pads = (before & ~mask) | (mask if value else 0)
            if not (before & mask) and value:
                self.owner.rises.append(self.owner.time)
            if (before & mask) and not value:
                self.owner.falls.append(self.owner.time)
        else:
            raise AssertionError(hex(instruction))
        if not injected or major == 0:
            self.pc = following
        self.delay = (instruction >> 8) & 31

    def tick(self, flags):
        if self.enabled:
            if self.delay:
                self.delay -= 1
            else:
                self.execute(self.words[self.pc], flags)


class Sequence:
    def __init__(self, programs, values, *, settle=2, pulse=1, falling=False,
                 status_mask=8, mode="PULSE", max_steps=None, startup=False, feedback=False):
        self.time, self.flags, self.pads = 0, 0, values[0]
        self.input = falling
        self.status_mask = status_mask
        self.writes, self.rises, self.falls, self.receipts = [], [], [], []
        self.receipt_times = []
        self.ingress = Machine(programs["finite_ingress" if max_steps is not None else "ingress"], self, "ingress")
        self.executor = Machine(programs["executor"], self, "executor")
        self.feedback = feedback and mode == "PULSE" and status_mask != 0 and max_steps != 0
        if self.feedback:
            self.executor.words[4] = 0xA0E2  # MOV OSR, Y, replacing tail regrant
            self.executor.words[9] = 0xC004  # IRQ SET 4, before status OUT
        self.counter = Machine(programs["counter"], self, "counter")
        self.counter.x = UINT32
        self.ingress.y = ((max_steps or 0) - 1) & UINT32
        if max_steps == 0:
            self.ingress.pc = 7
            self.executor.enabled = False
        if mode in {"LEVEL", "NONE"}:
            self.executor.words[13] = 16
        if mode == "LEVEL":
            self.pads |= status_mask
        if falling:
            for sm in (self.ingress, self.counter):
                sm.words[0] ^= 128
                sm.words[1] ^= 128
        self.words = []
        for transfer in range(len(values)):
            index = (transfer + 1) % len(values)
            value = values[index]
            self.words += [value | (index << 4) | ((value | status_mask) << 12),
                           0 if settle == 0 else settle * 10 - 5,
                           pulse * 10 - 4 if mode == "PULSE" else 0]
        self.cursor = 0
        self.latest_edge = 0
        self.drain = True
        # Default fixtures represent an already-primed executor. Startup tests
        # enter the same writing label and preload the same FIFO word as C.
        self.priming = startup
        self.max_steps = max_steps
        self.paused = False
        if startup:
            self.pads = values[0]
            self.executor.enabled = True
            self.executor.y = self.words[-3]
            self.executor.x = self.words[-2]
            if self.feedback:
                self.executor.osr = self.executor.y
            self.executor.tx.append(self.words[-1])
            self.executor.pc = 6
            self.ingress.enabled = self.counter.enabled = self.feedback

    def tick(self, count=1):
        for _ in range(count):
            if len(self.executor.tx) < 4:
                self.executor.tx.append(self.words[self.cursor])
                self.cursor = (self.cursor + 1) % len(self.words)
            self.set_flags = self.clear_flags = 0
            flags = self.flags
            for sm in (self.ingress, self.executor, self.counter):
                sm.tick(flags)
            self.flags = (self.flags & ~self.clear_flags) | self.set_flags
            if self.drain:
                while self.executor.rx:
                    self.receipts.append(self.executor.rx.popleft())
                    self.receipt_times.append(self.time)
                while self.counter.rx:
                    self.latest_edge = self.counter.rx.popleft()
            if self.priming and len(self.receipts) >= 2 and (self.feedback or self.flags & 16):
                self.priming = False
                if self.max_steps != 0:
                    self.counter.enabled = True
                    self.ingress.enabled = not self.paused
            self.time += 1

    def edge(self, *, falling=False, gap=80):
        self.input = not falling
        self.tick(8)
        self.input = falling
        self.tick(gap)

    def tick_until(self, predicate, limit=2000):
        for _ in range(limit):
            if predicate():
                return
            self.tick()
        raise AssertionError("PIO feedback transition did not complete")

    def pause(self):
        self.paused = True
        self.ingress.enabled = False
        self.ingress.pc = 0

    def resume(self):
        self.paused = False
        self.ingress.pc = 0
        self.ingress.enabled = not self.priming or self.feedback


@pytest.mark.parametrize("mode", ["PULSE", "LEVEL", "NONE"])
@pytest.mark.parametrize("settle", [0, 1, 20])
@pytest.mark.parametrize("pulse", [1, 10, 100])
def test_startup_uses_same_delay_and_status_path_without_admission(programs, mode, settle, pulse):
    mask = 0 if mode == "NONE" else 8
    machine = Sequence(programs, [5, 2, 7], mode=mode, status_mask=mask,
                       settle=settle, pulse=pulse, startup=True)
    # Sustained activity during startup must not turn into another state.
    while machine.priming:
        machine.input = bool(machine.time & 1)
        machine.tick()
        assert len(machine.writes) <= 1
        assert machine.latest_edge == 0
    first = 5 | ((5 | mask) << 12)
    assert machine.receipts == [first, first ^ UINT32]
    assert machine.writes == [(0, first)]
    assert machine.pads == 5 | (mask if mode == "LEVEL" else 0)
    if mode != "NONE":
        assert machine.rises == [settle * 10 if settle else 5]
    else:
        assert not machine.rises
    if mode == "PULSE":
        assert machine.falls == [machine.rises[0] + pulse * 10]
    else:
        assert not machine.falls
    machine.input = False
    machine.tick(10)
    machine.edge(gap=settle * 10 + pulse * 10 + 30)
    assert [tag & 15 for _, tag in machine.writes] == [5, 2]
    assert len(machine.receipts) == 4 and machine.latest_edge == 1


@pytest.mark.parametrize("mode", ["PULSE", "LEVEL", "NONE"])
def test_one_state_startup_emits_status_then_remains_finished(programs, mode):
    mask = 0 if mode == "NONE" else 8
    machine = Sequence(programs, [5], mode=mode, status_mask=mask, startup=True, max_steps=0)
    machine.tick(100)
    first = 5 | ((5 | mask) << 12)
    assert machine.receipts == [first, first ^ UINT32]
    assert len(machine.writes) == 1 and not machine.priming
    for _ in range(20):
        machine.edge()
    assert len(machine.writes) == 1 and machine.latest_edge == 0
    assert len(machine.rises) == (mode != "NONE")
    assert len(machine.falls) == (mode == "PULSE")


def test_pause_during_initial_pulse_keeps_admission_closed(programs):
    machine = Sequence(programs, [5, 2], settle=10, pulse=10, startup=True)
    machine.tick(110)
    assert machine.pads == 13
    machine.pause()
    machine.resume()
    assert not machine.ingress.enabled
    machine.pause()
    for _ in range(5):
        machine.edge()
    assert len(machine.writes) == 1 and machine.pads == 5
    assert not machine.priming and not machine.ingress.enabled
    machine.resume()
    machine.tick(10)
    machine.edge(gap=250)
    assert [tag & 15 for _, tag in machine.writes] == [5, 2]


@pytest.mark.parametrize("falling", [False, True])
@pytest.mark.parametrize("max_steps", [None, 1, 7])
@pytest.mark.parametrize("feedback_delay", [0, 1, 7, 101])
def test_feedback_capture_precedes_trigger_and_latches_once(programs, falling, max_steps, feedback_delay):
    machine = Sequence(programs, [5, 2, 7], settle=20, pulse=20, falling=falling,
                       startup=True, max_steps=max_steps, feedback=True)
    target = max_steps if max_steps is not None else 5
    # Noise before the first response window is counted but never queued.
    machine.tick(4)
    machine.edge(falling=falling, gap=4)
    assert not machine.flags & 32 and len(machine.writes) == 1
    for step in range(target):
        machine.tick_until(lambda: len(machine.rises) > step)
        assert machine.ingress.enabled and machine.counter.enabled
        machine.tick(feedback_delay)
        machine.input = not falling
        machine.tick(8)
        machine.input = falling
        assert machine.flags & 32 and not machine.flags & 16
        assert len(machine.writes) == step + 1
        # Further Sweep End pulses during the same output must not queue work.
        machine.tick(4)
        machine.edge(falling=falling, gap=4)
        assert len(machine.writes) == step + 1
        machine.tick_until(lambda: len(machine.writes) > step + 1)
        assert machine.writes[-1][0] > machine.falls[step]
        assert not machine.flags & 16  # no accidental tail regrant
        # A fresh edge during the next code's settling interval is rejected.
        machine.edge(falling=falling, gap=4)
        assert not machine.flags & 32
    machine.tick(450)
    assert [tag & 15 for _, tag in machine.writes] == [5, *([2, 7, 5] * 3)[:target]]
    assert len(machine.receipts) == (target + 1) * 2
    assert all(fall - rise == 200 for rise, fall in zip(machine.rises, machine.falls))
    assert all(rise - write[0] == 200 for rise, write in zip(machine.rises, machine.writes))
    assert not machine.priming
    if max_steps is not None:
        for _ in range(3):
            machine.edge(falling=falling)
        assert len(machine.writes) == max_steps + 1


def test_feedback_pause_drains_one_latched_candidate_then_stays_paused(programs):
    machine = Sequence(programs, [5, 2, 7], settle=1, pulse=20, startup=True,
                       max_steps=2, feedback=True)
    machine.tick(12)
    machine.edge(gap=4)
    assert machine.flags & 32 and machine.priming
    machine.pause()
    for _ in range(10):
        machine.edge(gap=80)
    assert not machine.priming and len(machine.writes) == 2 and len(machine.receipts) == 4
    assert machine.pads == 2 and not machine.ingress.enabled
    machine.input = True
    machine.resume()
    machine.tick(100)
    assert len(machine.writes) == 2  # held level is not a fresh feedback edge
    machine.input = False
    machine.tick(4)
    machine.edge(gap=250)
    assert len(machine.writes) == 3
    for _ in range(5):
        machine.edge(gap=250)
    assert len(machine.writes) == 3  # finite quota was not reset by resume


@pytest.mark.parametrize("settle", [0, 1, 10])
@pytest.mark.parametrize("pulse", [1, 2])
def test_feedback_short_pulse_startup_with_immediate_sweep_end(programs, settle, pulse):
    machine = Sequence(programs, [5, 2], settle=settle, pulse=pulse, startup=True,
                       max_steps=1, feedback=True)
    machine.tick_until(lambda: len(machine.rises) == 1)
    machine.edge(gap=settle * 10 + pulse * 20 + 50)
    assert [value & 15 for _, value in machine.writes] == [5, 2]
    assert len(machine.receipts) == 4 and not machine.priming
    assert machine.writes[1][0] > machine.falls[0]
    assert [fall - rise for rise, fall in zip(machine.rises, machine.falls)] == [pulse * 10] * 2


@pytest.mark.parametrize("falling", [False, True])
def test_feedback_initial_active_requires_new_edge_and_accepts_late_completion(programs, falling):
    machine = Sequence(programs, [0, 3, 6], settle=1, pulse=1, falling=falling,
                       startup=True, max_steps=2, feedback=True)
    machine.input = not falling
    machine.tick(300)
    assert len(machine.writes) == 1 and machine.latest_edge == 0
    assert len(machine.falls) == 1 and machine.flags & 16
    # Holding the old completion level through START cannot advance the plan.
    # A genuine edge long after the outgoing pulse must still be accepted.
    machine.input = falling
    machine.tick(10)
    machine.edge(falling=falling, gap=300)
    assert [tag & 15 for _, tag in machine.writes] == [0, 3]
    assert machine.latest_edge == 1 and len(machine.receipts) == 4
    machine.edge(falling=falling, gap=300)
    assert [tag & 15 for _, tag in machine.writes] == [0, 3, 6]
    assert machine.latest_edge == 2 and len(machine.receipts) == 6
    machine.edge(falling=falling, gap=300)
    assert len(machine.writes) == 3


@pytest.mark.parametrize("feedback_width_us", [10, 100])
def test_feedback_width_and_next_trigger_timing(programs, feedback_width_us):
    machine = Sequence(programs, [0, 1, 2], settle=10, pulse=10,
                       startup=True, max_steps=2, feedback=True)
    machine.tick_until(lambda: len(machine.falls) == 1)
    # Sweep completion is late relative to our outgoing trigger. Keep IN1
    # active for the entire feedback pulse, including any next OUT4 trigger.
    machine.tick(1000)
    feedback_rise = machine.time
    machine.input = True
    machine.tick(feedback_width_us * 10)
    feedback_fall = machine.time
    machine.input = False
    machine.tick(400)
    assert machine.latest_edge == 1
    assert [word & 15 for _, word in machine.writes] == [0, 1]
    assert len(machine.rises) == len(machine.falls) == 2
    assert len(machine.receipts) == 4
    next_code = machine.writes[1][0]
    next_trigger = machine.rises[1]
    assert next_trigger - next_code == 100  # configured settle, 100 ns ticks
    # Current behavior does not wait for feedback deassertion before firing.
    assert (next_trigger < feedback_fall) == (feedback_width_us == 100)
    print(f"feedback_width_us={feedback_width_us} "
          f"feedback_to_code_us={(next_code - feedback_rise) / 10:g} "
          f"feedback_to_trigger_us={(next_trigger - feedback_rise) / 10:g} "
          f"trigger_after_feedback_fall_us={(next_trigger - feedback_fall) / 10:g}")


@pytest.mark.parametrize("pulse_us", [10, 100])
def test_direct_feedback_loop_completes_ten_rounds(programs, pulse_us):
    machine = Sequence(programs, list(range(8)), settle=10, pulse=pulse_us,
                       startup=True, max_steps=79, feedback=True)
    # OUT4 -> IN1, observed on the following tick. No host-injected edges.
    for _ in range(100000):
        machine.input = bool(machine.pads & 8)
        machine.tick()
        if len(machine.falls) == 80:
            break
    assert [tag & 15 for _, tag in machine.writes] == list(range(8)) * 10
    assert len(machine.rises) == len(machine.falls) == 80
    machine.tick(100)
    assert len(machine.receipts) == 160
    assert machine.latest_edge == 80
    assert machine.ingress.pc == 7  # finite ingress has exhausted its quota
    print(f"loopback_pulse_us={pulse_us} outputs=80 advances=79 "
          f"first_rise_to_last_fall_us={(machine.falls[-1] - machine.rises[0]) / 10:g}")


@pytest.mark.parametrize("values", [[0, 1, 2], list(range(8))])
@pytest.mark.parametrize("falling", [False, True])
def test_full_plan_runs_without_cpu_steps(programs, values, falling):
    machine = Sequence(programs, values, falling=falling)
    machine.tick(20)
    for _ in range(len(values) * 3 + 1):
        machine.edge(falling=falling)
    expected = [
        values[(i + 1) % len(values)] |
        (((i + 1) % len(values)) << 4) |
        ((values[(i + 1) % len(values)] | machine.status_mask) << 12)
        for i in range(len(values) * 3 + 1)]
    assert [value for _, value in machine.writes] == expected
    assert machine.receipts == [word for tag in expected for word in (tag, tag ^ UINT32)]
    assert machine.latest_edge == len(expected)
    assert [rise - write[0] for rise, write in zip(machine.rises, machine.writes)] == [20] * len(expected)
    assert [fall - rise for fall, rise in zip(machine.falls, machine.rises)] == [10] * len(expected)


def test_busy_edges_not_replayed_and_pause_finishes_current_step(programs):
    machine = Sequence(programs, [1, 2, 3], settle=20, pulse=5)
    machine.tick(20)
    machine.edge(gap=8)
    assert len(machine.writes) == 1
    for _ in range(3):
        machine.edge(gap=8)
    machine.pause()
    for _ in range(4):
        machine.edge()
    assert len(machine.falls) == 1
    assert len(machine.writes) == 1
    assert machine.latest_edge == 8
    machine.input = True
    machine.resume()
    machine.tick(100)
    assert len(machine.writes) == 1
    machine.input = False
    machine.tick(10)
    machine.edge(gap=300)
    assert [tag & 15 for _, tag in machine.writes] == [2, 3]


def test_preload_is_not_execution_and_receipt_backpressure_is_visible(programs):
    machine = Sequence(programs, [1, 2, 3])
    machine.tick(1000)
    assert machine.cursor != 0
    assert machine.pads == 1
    assert not machine.writes and not machine.receipts
    machine.drain = False
    for _ in range(6):
        machine.edge()
    assert machine.executor.rx_stall
    assert len(machine.executor.rx) == 4
    assert len(machine.falls) <= 3


def test_counter_snapshot_drop_keeps_count_and_pause_push_restore(programs):
    machine = Sequence(programs, [1])
    machine.tick(20)
    machine.drain = False
    for _ in range(15):
        machine.edge()
    assert len(machine.counter.rx) == 8
    assert (~machine.counter.x & UINT32) == 15
    # Production stop_counter reads X via MOV/PUSH, which clears ISR.
    counter = machine.counter
    counter.pc = 4
    counter.isr = (~counter.x) & UINT32
    counter.rx.clear()
    counter.execute(0xA0C9, machine.flags, injected=True)  # MOV ISR, ~X
    counter.execute(0x8000, machine.flags, injected=True)  # PUSH noblock
    assert counter.rx.popleft() == 15 and counter.isr == 0
    counter.execute(0xA0C9, machine.flags, injected=True)  # restore pending PUSH
    counter.tick(machine.flags)
    assert counter.rx.popleft() == 15


@pytest.mark.parametrize("completion", range(4))
def test_completion_pad_does_not_leak_plan_tag(programs, completion):
    value = 15 ^ (1 << completion)
    machine = Sequence(programs, [value, value], status_mask=1 << completion)
    machine.tick(20)
    machine.edge()
    machine.edge()
    assert machine.pads == value
    first = value | 16 | (15 << 12)
    second = value | (15 << 12)
    assert machine.receipts == [first, first ^ UINT32, second, second ^ UINT32]


def test_multiple_status_outputs_pulse_together(programs):
    machine = Sequence(programs, [0, 1, 2], status_mask=12)
    machine.tick(20)
    machine.edge()
    assert machine.pads == 1
    assert machine.rises == [machine.writes[0][0] + 20]
    assert machine.falls == [machine.rises[0] + 10]


def test_level_status_stays_high_until_next_switch(programs):
    machine = Sequence(programs, [0, 1, 2], status_mask=12, mode="LEVEL")
    machine.tick(20)
    machine.edge(gap=80)
    assert machine.pads == 13
    assert machine.falls[0] == machine.writes[0][0]
    assert machine.rises[-1] == machine.writes[0][0] + 20
    tag = 17 | (13 << 12)
    assert machine.receipts == [tag, tag ^ UINT32]


def test_dut_only_keeps_status_low_and_finishes_each_edge(programs):
    machine = Sequence(programs, list(range(8)), status_mask=0, mode="NONE")
    machine.tick(30)
    assert machine.pads == 0 and machine.receipts == []
    for index in range(1, 18):
        machine.edge()
        assert machine.pads == index % 8
        assert len(machine.receipts) == index * 2
        machine.tick(100)
        assert len(machine.receipts) == index * 2  # no DONE-driven advance
    assert machine.rises == machine.falls == []


@pytest.mark.parametrize("max_steps", [0, 1, 7, 15, 256])
@pytest.mark.parametrize("mode", ["PULSE", "NONE"])
def test_finite_pio_quota_never_accepts_after_last_state(programs, max_steps, mode):
    machine = Sequence(programs, list(range(8)), status_mask=8 if mode == "PULSE" else 0,
                       mode=mode, max_steps=max_steps)
    machine.tick(30)
    for _ in range(max_steps + 30):
        machine.edge()
    assert len(machine.writes) == max_steps
    assert len(machine.receipts) == 2 * max_steps
    assert machine.pads == max_steps % 8
    assert machine.flags & (1 << 5) == 0
    assert machine.ingress.pc == 7
    if mode == "PULSE":
        assert len(machine.rises) == len(machine.falls) == max_steps
    # Unlimited source activity after the final completion cannot manufacture
    # an extra admitted request, even if Core1 never services the CPU receipt.
    before = machine.receipts[:]
    for _ in range(20):
        machine.edge()
    assert machine.receipts == before and machine.pads == max_steps % 8


def test_finite_pio_quota_ten_thousand_rounds_without_cpu(programs):
    steps = 8 * 10000 - 1
    machine = Sequence(programs, list(range(8)), status_mask=0, mode="NONE", settle=0,
                       max_steps=steps)
    machine.tick(30)
    for _ in range(steps + 5):
        machine.edge(gap=16)
    assert len(machine.writes) == steps and len(machine.receipts) == steps * 2
    assert machine.pads == 7 and machine.ingress.pc == 7


class Gateway:
    def __init__(self, programs, *, mask=8, falling=False, manual=False):
        self.owner = SimpleNamespace(input=falling, pads=15 ^ mask, status_mask=mask,
                                     time=0, rises=[], falls=[], set_flags=0, clear_flags=0)
        self.flags = 0
        self.pulse = Machine(programs["gateway"], self.owner, "gateway")
        self.counter = Machine(programs["ready_counter"], self.owner, "counter")
        self.counter.x = UINT32
        if falling:
            self.counter.words[1] ^= 128
            self.counter.words[2] ^= 128
        if manual:
            self.pulse.words[2] = 0xA042 | (2 << 8)  # NOP [2], same as production patch
            self.counter.enabled = False

    def tick(self, count=1, *, loopback=False, reverse_order=False):
        for _ in range(count):
            if loopback:
                self.owner.input = bool(self.owner.pads & self.owner.status_mask)
            self.owner.set_flags = self.owner.clear_flags = 0
            pair = (self.counter, self.pulse) if reverse_order else (self.pulse, self.counter)
            for sm in pair:
                sm.tick(self.flags)
            self.flags = (self.flags & ~self.owner.clear_flags) | self.owner.set_flags
            self.owner.time += 1

    def fire(self, pulse_us=1):
        assert not self.pulse.tx
        self.pulse.tx.append(pulse_us * 10 - 2)


@pytest.mark.parametrize("output_mask", [1, 2, 4, 8])
@pytest.mark.parametrize("falling", [False, True])
@pytest.mark.parametrize("pulse_us", [1, 10, 100])
def test_gateway_pio_pulse_captures_ready_without_advancing_dut(programs, output_mask, falling, pulse_us):
    machine = Gateway(programs, mask=output_mask, falling=falling)
    owner = machine.owner
    # Even complete edges before FIRE have no grant and cannot create READY.
    for _ in range(5):
        owner.input = not falling
        machine.tick(4)
        owner.input = falling
        machine.tick(4)
    assert not machine.counter.rx
    machine.fire(pulse_us)
    machine.tick(10)
    assert len(owner.rises) == 1 and not owner.falls
    # One sampled active tick suffices once WAIT active is armed, including
    # an early READY while the long trigger pulse is still high.
    owner.input = not falling
    machine.tick(1)
    owner.input = falling
    machine.tick(pulse_us * 10 + 10)
    assert list(machine.counter.rx) == [1]
    assert owner.falls[0] - owner.rises[0] == pulse_us * 10
    assert owner.pads == 15 ^ output_mask
    assert machine.flags & ((1 << 4) | (1 << 5)) == 0
    assert len(machine.pulse.rx) == 1
    # Further READY edges cannot be credited to the next FIRE.
    for _ in range(10):
        owner.input = not falling
        machine.tick(2)
        owner.input = falling
        machine.tick(2)
    assert list(machine.counter.rx) == [1]
    machine.pulse.rx.clear()
    machine.fire(pulse_us)
    machine.tick(10)
    owner.input = not falling
    machine.tick(1)
    owner.input = falling
    machine.tick(pulse_us * 10 + 10)
    assert list(machine.counter.rx) == [1, 2]
    assert owner.falls[1] - owner.rises[1] == pulse_us * 10


@pytest.mark.parametrize("falling", [False, True])
@pytest.mark.parametrize("reverse_order", [False, True])
def test_gateway_out4_in2_loopback_with_minimum_pulse(programs, falling, reverse_order):
    machine = Gateway(programs, falling=falling)
    for index in range(1, 21):
        machine.fire(1)
        machine.tick(40, loopback=True, reverse_order=reverse_order)
        assert list(machine.counter.rx) == [index]
        assert len(machine.pulse.rx) == 1
        assert machine.owner.falls[-1] - machine.owner.rises[-1] == 10
        machine.counter.rx.clear()
        machine.pulse.rx.clear()


@pytest.mark.parametrize("falling", [False, True])
def test_gateway_already_active_ready_does_not_block_trigger_or_count_stale_level(programs, falling):
    machine = Gateway(programs, falling=falling)
    machine.owner.input = not falling
    machine.fire(1)
    machine.tick(50)
    assert len(machine.owner.rises) == len(machine.owner.falls) == 1
    assert not machine.counter.rx
    machine.owner.input = falling
    machine.tick(3)
    machine.owner.input = not falling
    machine.tick(4)
    assert list(machine.counter.rx) == [1]


def test_gateway_missing_ready_sm_holds_grant_before_output(programs):
    machine = Gateway(programs)
    machine.counter.enabled = False
    machine.fire(1)
    machine.tick(50)
    assert not machine.owner.rises and machine.flags == 1 << 6
    assert machine.pulse.pc == 2
    machine.counter.enabled = True
    machine.tick(40, loopback=True)
    assert len(machine.owner.rises) == 1 and list(machine.counter.rx) == [1]


def test_gateway_manual_ready_and_completion_backpressure(programs):
    machine = Gateway(programs, manual=True)
    machine.pulse.rx.extend([1, 2, 3, 4])
    machine.fire(1)
    machine.tick(40)
    assert len(machine.owner.rises) == len(machine.owner.falls) == 1
    assert machine.owner.falls[0] - machine.owner.rises[0] == 10
    assert machine.flags == 0 and machine.pulse.rx_stall
    assert machine.pulse.pc == 6 and not (machine.owner.pads & 8)
    machine.pulse.rx.clear()
    machine.tick(1)
    assert len(machine.pulse.rx) == 1  # DONE retained instead of dropped


@pytest.mark.parametrize("settle", [0, 1, 20])
def test_compact_executor_preserves_three_word_plan_and_settle_receipts(programs, settle):
    machine = Sequence(programs, [5, 2, 7], mode="NONE", status_mask=0,
                       settle=settle, startup=True)
    machine.executor.words = programs["none_executor"][:]
    machine.tick(settle * 10 + 20)
    first = 5 | (5 << 12)
    assert machine.receipts == [first, first ^ UINT32]
    assert machine.pads == 5 and not machine.rises and not machine.falls
    assert machine.writes == [(0, first)]
    for expected in [2, 7, 5, 2]:
        before = len(machine.receipts)
        machine.input = False
        machine.tick(4)
        machine.input = True
        machine.tick(10)
        machine.input = False
        machine.tick(settle * 10 + 20)
        assert machine.pads == expected and len(machine.receipts) == before + 2
    assert not machine.executor.rx_stall
    expected_delay = settle * 10 if settle else 5
    assert [machine.receipt_times[2 * index + 1] - write[0]
            for index, write in enumerate(machine.writes)] == [expected_delay] * len(machine.writes)
