"""Production receipt logic and assembled PIO instructions, without hardware."""
import os
from collections import deque
from pathlib import Path
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
UINT32 = (1 << 32) - 1


def function(source, name):
    start = re.search(rf"(?m)^(?:static )?(?:bool|void|uint32_t) {name}\(", source)
    assert start, name
    end = source.index("\n}\n", start.start()) + 3
    return source[start.start():end]


def test_sequence_backend_receipts_and_admission(tmp_path):
    backend = (ROOT / "components/sync_io/src/sync_io_sequence.c").read_text(encoding="utf-8")
    harness = '#include "sync_io_sequence_fake.h"\n'
    for name in ("config_valid", "receive_word", "account_input"):
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
    for name in ("ingress", "executor", "counter"):
        body = text.split(f"sequence_{name}_program_instructions[] = {{", 1)[1].split("};", 1)[0]
        parsed[name] = [int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{4}),", body)]
    assert [len(parsed[name]) for name in parsed] == [6, 17, 5]
    assert sum(map(len, parsed.values())) + 1 <= 32
    return parsed


def test_production_hot_load_and_pause_boundaries(tmp_path):
    backend = (ROOT / "components/sync_io/src/sync_io_sequence.c").read_text(encoding="utf-8")
    production = "\n".join(function(backend, name) for name in (
        "sm_pc", "clear_owned_flags", "safe_low", "stop_hardware", "cleanup", "load_hardware",
        "stop_hook", "read_sm_register", "stop_counter", "finish_ingress",
        "produced_receipts", "stop_receipt_dma", "resume_receipt_dma", "receive_word", "drain_receipts",
        "pending_request", "drain_idle_executor", "account_input", "sync_io_sequence_service",
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
        elif major == 5:
            dest, operation, source = arg >> 5, (arg >> 3) & 3, arg & 7
            value = {1: self.x, 2: self.y, 3: 0, 5: UINT32 if flags & 16 else 0,
                     6: self.isr, 7: self.osr}[source]
            assert operation in (0, 1)
            if operation:
                value ^= UINT32
            if dest == 0:
                self.owner.pads = value & 15
                self.owner.writes.append((self.owner.time, value))
            elif dest == 1:
                self.x = value
            elif dest == 2:
                self.y = value
            elif dest == 6:
                self.isr = value
            else:
                raise AssertionError(("MOV", dest))
        elif major == 6:
            if arg & 64:
                self.owner.clear_flags |= 1 << (arg & 7)
            else:
                self.owner.set_flags |= 1 << (arg & 7)
        elif major == 7:
            assert arg >> 5 == 0
            value = arg & 31
            if value:
                self.owner.pads |= 1 << self.owner.completion
                self.owner.rises.append(self.owner.time)
            else:
                self.owner.pads &= ~(1 << self.owner.completion)
                self.owner.falls.append(self.owner.time)
        else:
            raise AssertionError(hex(instruction))
        if not injected or major == 0:
            self.pc = following

    def tick(self, flags):
        if self.enabled:
            self.execute(self.words[self.pc], flags)


class Sequence:
    def __init__(self, programs, values, *, settle=2, pulse=1, falling=False, completion=3):
        self.time, self.flags, self.pads = 0, 0, 0
        self.input = falling
        self.completion = completion
        self.writes, self.rises, self.falls, self.receipts = [], [], [], []
        self.ingress = Machine(programs["ingress"], self, "ingress")
        self.executor = Machine(programs["executor"], self, "executor")
        self.counter = Machine(programs["counter"], self, "counter")
        self.counter.x = UINT32
        if falling:
            for sm in (self.ingress, self.counter):
                sm.words[0] ^= 128
                sm.words[1] ^= 128
        self.words = []
        for index, value in enumerate(values):
            self.words += [value | (index << 4), 0 if settle == 0 else settle * 10 - 4,
                           pulse * 10 - 3]
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
    expected = [values[i % len(values)] | ((i % len(values)) << 4)
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
    assert [tag & 15 for _, tag in machine.writes] == [1, 2]


def test_preload_is_not_execution_and_receipt_backpressure_is_visible(programs):
    machine = Sequence(programs, [1, 2, 3])
    machine.tick(1000)
    assert machine.cursor != 0
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
    machine = Sequence(programs, [value, value], completion=completion)
    machine.tick(20)
    machine.edge()
    machine.edge()
    assert machine.pads == value
    assert machine.receipts == [value, value ^ UINT32, value | 16, (value | 16) ^ UINT32]
