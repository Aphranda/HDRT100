"""Execute the finite PIO control flow independently of its C owner.

This models instruction count and halt behavior only; DMA arbitration and
RXSTALL must still be checked on hardware.
"""
from pathlib import Path
import re

import pytest

ROOT = Path(__file__).resolve().parents[2]


def program():
    source = (ROOT / "components/sync_io/src/sync_io.pio").read_text(encoding="utf-8")
    body = source.split(".program logic_analyzer_finite_sample", 1)[1].split(".program", 1)[0]
    instructions, labels = [], {}
    for raw in body.splitlines():
        line = raw.split(";", 1)[0].strip()
        if line.endswith(":"):
            labels[line.removeprefix("public ")[:-1]] = len(instructions)
        elif line and not line.startswith("."):
            instructions.append(line)
    return instructions, labels


@pytest.mark.parametrize("words", [1, 2, 6, 7, 204, 205, 8192])
def test_exact_count_stride_and_permanent_halt(words):
    instructions, labels = program()
    x = words * 5 - 1
    pc, cycle, samples = 0, 0, []
    # Trigger starts low (already in frame), goes idle, then active. The
    # initial partial frame must never be sampled.
    while cycle < words * 10 + 40:
        instruction = instructions[pc]
        next_pc = pc + 1
        level = int(4 <= cycle < 8)
        if instruction.startswith("wait "):
            assert instruction in ("wait 1 gpio 0", "wait 0 gpio 0")
            if level != int(instruction.split()[1]):
                next_pc = pc
        elif instruction == "in pins, 6":
            samples.append(cycle)
        elif instruction.startswith("jmp x-- "):
            if x != 0:
                next_pc = labels[instruction.split()[-1]]
            x = (x - 1) & 0xffffffff
        elif instruction.startswith("jmp "):
            next_pc = labels[instruction.split()[-1]]
        else:
            pytest.fail(f"unsupported sampling instruction: {instruction}")
        cycle, pc = cycle + 1, next_pc
    assert samples[0] == 9
    assert len(samples) == words * 5
    assert all(b - a == 2 for a, b in zip(samples, samples[1:]))
    assert pc == labels["finite_halt"]


def test_budget_wait_patch_labels_and_read_only_instructions():
    instructions, labels = program()
    resources = (ROOT / "components/sync_io/inc/sync_io_persona_resources.h").read_text(encoding="utf-8")
    budget = int(re.search(r"#define SYNC_IO_LOGIC_ANALYZER_INSTRUCTION_WORDS (\d+)u", resources)[1])
    assert len(instructions) <= budget
    assert instructions[labels["trigger_idle"]] == "wait 1 gpio 0"
    assert instructions[labels["trigger_active"]] == "wait 0 gpio 0"
    assert all(line.startswith(("wait ", "in pins, ", "jmp ")) for line in instructions)
