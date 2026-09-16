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
    for name in ("config_valid", "logical_index_for_transfer", "receive_word", "account_input"):
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
    for name in ("ingress", "executor", "counter", "gateway", "finite_ingress"):
        body = text.split(f"sequence_{name}_program_instructions[] = {{", 1)[1].split("};", 1)[0]
        parsed[name] = [int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{4}),", body)]
    assert [len(parsed[name]) for name in parsed] == [6, 18, 5, 6, 8]
    assert sum(len(parsed[name]) for name in ("ingress", "executor", "counter")) + 1 <= 32
    assert sum(len(parsed[name]) for name in ("gateway", "executor", "counter")) + 1 <= 32
    assert sum(len(parsed[name]) for name in ("finite_ingress", "executor", "counter")) + 1 == 32
    return parsed


def test_production_hot_load_and_pause_boundaries(tmp_path):
    backend = (ROOT / "components/sync_io/src/sync_io_sequence.c").read_text(encoding="utf-8")
    production = "\n".join(function(backend, name) for name in (
        "sm_pc", "clear_owned_flags", "safe_low", "stop_hardware", "cleanup", "load_hardware",
        "stop_hook", "read_sm_register", "stop_counter", "finish_ingress",
        "produced_receipts", "stop_receipt_dma", "resume_receipt_dma", "logical_index_for_transfer",
        "receive_word", "drain_receipts", "pending_request", "drain_idle_executor", "account_input",
        "mark_initial_status_ready",
        "gateway_service", "gateway_cancel",
        "sync_io_sequence_service",
        "sync_io_sequence_gateway_fire", "sync_io_sequence_software_step",
        "sync_io_sequence_pause"))
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
            if arg & 64:
                self.owner.clear_flags |= 1 << (arg & 7)
            else:
                self.owner.set_flags |= 1 << (arg & 7)
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

    def tick(self, flags):
        if self.enabled:
            self.execute(self.words[self.pc], flags)


class Sequence:
    def __init__(self, programs, values, *, settle=2, pulse=1, falling=False,
                 status_mask=8, mode="PULSE", max_steps=None):
        self.time, self.flags, self.pads = 0, 0, values[0]
        self.input = falling
        self.status_mask = status_mask
        self.writes, self.rises, self.falls, self.receipts = [], [], [], []
        self.ingress = Machine(programs["finite_ingress" if max_steps is not None else "ingress"], self, "ingress")
        self.executor = Machine(programs["executor"], self, "executor")
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
                while self.counter.rx:
                    self.latest_edge = self.counter.rx.popleft()
            self.time += 1

    def edge(self, *, falling=False, gap=80):
        self.input = not falling
        self.tick(8)
        self.input = falling
        self.tick(gap)

    def pause(self):
        self.ingress.enabled = False
        self.ingress.pc = 0

    def resume(self):
        self.ingress.pc = 0
        self.ingress.enabled = True


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


@pytest.mark.parametrize("output_mask", [1, 2, 4, 8])
@pytest.mark.parametrize("falling", [False, True])
@pytest.mark.parametrize("pulse_us", [1, 10, 100])
def test_gateway_pio_pulse_captures_ready_without_advancing_dut(programs, output_mask, falling, pulse_us):
    owner = SimpleNamespace(input=falling, pads=15 ^ output_mask, status_mask=output_mask,
                            time=0, rises=[], falls=[], set_flags=0, clear_flags=0)
    pulse = Machine(programs["gateway"], owner, "gateway")
    counter = Machine(programs["counter"], owner, "counter")
    counter.x = UINT32
    if falling:
        counter.words[0] ^= 128
        counter.words[1] ^= 128
    # A stale active level does not produce READY until the next full edge.
    owner.input = not falling
    for _ in range(10):
        counter.tick(0)
    assert not counter.rx
    owner.input = falling
    counter.tick(0)
    pulse.tx.append(pulse_us * 10 - 2)
    for tick in range(pulse_us * 10 + 20):
        owner.time = tick
        # READY can be shorter than a CPU poll interval and finish while the
        # output pulse is still high. Counter/DMA retains its receipt.
        owner.input = not falling if 4 <= tick < 7 else falling
        pulse.tick(0)
        counter.tick(0)
        assert (owner.pads & ~output_mask) == (15 ^ output_mask)
    assert owner.rises == [2]
    assert owner.falls == [pulse_us * 10 + 2]
    assert list(counter.rx) == [1]
    assert owner.pads == 15 ^ output_mask
    assert owner.set_flags == owner.clear_flags == 0  # no sequence IRQ request
    assert pulse.pc == 0 and not pulse.tx
    assert len(pulse.rx) == 1  # completion only after the physical low restore
