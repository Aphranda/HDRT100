"""First-entry CS guard on real PIO words and C-generated PASS commands.

Input times are already synchronized PIO instruction slots. This checks the
instruction path, not cross-board GPIO skew, DMA latency, or frame identity.
Disabled-SM pulses, STOP/reARM and target forced-instruction PC recovery are
not modeled here; those remain separate owner/target validation obligations.
"""
import ctypes as C
import re

import pytest

from tdma_flight_pio_model import Machine, bytes_to_bits
from test_tdma_flight_bit_pipeline import ROOT, Plan, bound_words, engine
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body


def production_gate(source):
    arm = c_definition_body(source, "tdma_pio_spi_phys_arm")
    # Bind the modeled opcode and placement to the production ARM path. In
    # particular a later forced PULL/JMP or restart must not cancel the wait.
    match = re.search(
        r"if \(process_follower\)\s*\{\s*/\*[\s\S]*?\*/\s*"
        r"pio_sm_exec\(tdma_pio_spi_phys_data_pio\(phys\),\s*"
        r"tdma_pio_spi_phys_data_sm\(phys\),\s*"
        r"pio_encode_wait_gpio\(false, phys->rx_csn_pin\)\);\s*\}\s*"
        r"tdma_pio_spi_phys_enable_sm_pair\(phys\);\s*phys->armed = true;",
        arm,
    )
    assert match is not None, "initial CS wait must be the final capture instruction before enable"
    assert arm.rindex("tdma_pio_spi_phys_tx_clock_latch_rearm(phys)") < match.start()
    enable = c_definition_body(source, "tdma_pio_spi_phys_enable_sm_pair")
    assert not re.search(r"pio_sm_(exec|restart|init)|prepare_sm_pair", enable)
    return 0x2000  # WAIT 0 GPIO 0: model maps physical RXCS to pin 0.


class InitialCsMachine(Machine):
    def __init__(self, *args, lead, noise, gate, **kwargs):
        super().__init__(*args, **kwargs)
        self.starts = [400, 400 + self.frame_bits * self.period + 200]
        self.cs_fall = self.starts[0] - lead
        self.noise = [10 + i * self.period for i in range(noise)]
        self.pending = gate
        self.gate_completions = []

    def wait(self, pin, level):
        if pin == 0 and level == 0:
            self.time = max(self.time, self.cs_fall)
            self.gate_completions.append(self.time)
            return
        if pin == 1 and self.time < self.starts[0]:
            for rising in self.noise:
                if self.time < rising + self.high:
                    self.time = max(self.time, rising) if level else rising + self.high
                    return
        super().wait(pin, level)


class CheckedSamples(list):
    def __init__(self, machine, delay):
        super().__init__()
        self.machine, self.delay = machine, delay

    def append(self, sample):
        frame, ordinal, tick, _ = sample
        assert len(self) == frame * self.machine.frame_bits + ordinal, "unexpected physical bit"
        assert tick == (self.machine.starts[frame] + ordinal * self.machine.period
                        + self.delay + 1), "unexpected sample phase"
        super().append(sample)


def run_first_frames(engine, *, lead=2, noise=7, gate=True, late=False):
    lib, instructions, final, wrap = engine
    installed, delay = 4, 15
    program = [0] * installed + [
        (word & ~31) | ((word & 31) + installed) if word >> 13 == 0 else word
        for word in instructions]
    for i, pos in enumerate(i for i, word in enumerate(program) if word == 0x2081):
        program[pos] |= (delay - 2 if i == 0 else delay) << 8
    plan = Plan()
    assert lib.tdma_flight_overlay_build_pass_plan(lib.physical_bytes, final + installed, C.byref(plan))
    words = bound_words(lib, plan, final + installed)
    tokens = [half for word in words for half in (word >> 16, word & 65535)]
    physical = [bytes((i * 73 + offset) % 256 for i in range(lib.physical_bytes))
                for offset in (0xA5, 0x39)]
    opcode = production_gate((ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8"))
    machine = InitialCsMachine(physical, [tokens, tokens], program=program, entry=installed,
                              wrap=wrap + installed, final=final + installed,
                              lead=lead, noise=noise, gate=opcode if gate else None)
    machine.samples = CheckedSamples(machine, delay)
    if late:
        machine.time = machine.starts[0] + machine.period + 2
    machine.run()
    assert [lib.normalize_rx(word, 3) for word in machine.pushes] == list(b"".join(physical))
    assert [bit for _, bit in machine.output] == (
        [0] * 8 + bytes_to_bits(physical[0])[:-8]
        + bytes_to_bits(physical[0][-1:]) + bytes_to_bits(physical[1])[:-8])
    for index, (frame, ordinal, sample, _) in enumerate(machine.samples):
        assert index == frame * machine.frame_bits + ordinal
        assert sample == machine.starts[frame] + ordinal * machine.period + delay + 1
    if gate:
        assert machine.gate_completions == [machine.cs_fall]


@pytest.mark.parametrize("noise", range(8))
@pytest.mark.parametrize("lead", [2, 8, 11])
def test_first_byte_and_second_frame_ignore_idle_cs_clocks(engine, lead, noise):
    run_first_frames(engine, lead=lead, noise=noise)


def test_missing_guard_attempts_data_sample_before_first_cs(engine):
    with pytest.raises(AssertionError, match="DATA sampled outside a physical frame"):
        run_first_frames(engine, gate=False, noise=1)


def test_insufficient_cs_setup_changes_first_bit_sample_phase(engine):
    with pytest.raises(AssertionError, match="unexpected sample phase"):
        run_first_frames(engine, lead=1, noise=0)


def test_cs_already_low_does_not_recover_first_frame(engine):
    with pytest.raises(AssertionError, match="unexpected physical bit"):
        run_first_frames(engine, noise=0, late=True)


@pytest.mark.parametrize("replacement", [
    "pio_encode_wait_gpio(true, phys->rx_csn_pin)",
    "pio_encode_wait_gpio(false, phys->rx_sck_pin)",
])
def test_wrong_guard_instruction_is_rejected(replacement):
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    mutated = source.replace("pio_encode_wait_gpio(false, phys->rx_csn_pin)", replacement)
    with pytest.raises(AssertionError, match="initial CS wait"):
        production_gate(mutated)
