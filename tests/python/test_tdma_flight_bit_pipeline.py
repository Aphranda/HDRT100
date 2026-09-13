"""Execute real C plans on assembled, relocated PIO words with live neighbors."""
import ctypes as C
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import zlib

import pytest
from tdma_flight_pio_model import Machine, bytes_to_bits, reverse32, LIVE
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


class Run(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("control", "write_address", "transfer_count", "read_address")]


class Plan(C.Structure):
    _fields_ = [("run", Run * 8), ("token", C.c_uint32 * 259)] + [
        (name, C.c_uint32) for name in ("run_count", "token_word_count",
                                      "command_word_count", "replacement_byte_count", "generation")]


class Config(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("physical_byte_count", "outer_header_size", "alignment_byte_shift",
                 "alignment_bit_shift", "local_slot_id", "header_write_mask", "final_bit_pc")]


class Binding(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("pass_control", "token_control", "selection_control", "restart_control",
                 "tx_fifo_address", "live_word_address", "selected_generation_address",
                 "loader_trigger_address", "next_address_address")]


@pytest.fixture(scope="module", params=(6, 8))
def engine(tmp_path_factory, request):
    directory = tmp_path_factory.mktemp("bit-pipeline")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    pioasm = os.environ.get("PIOASM") or shutil.which("pioasm") or str(
        Path.home() / ".pico-sdk/tools/2.2.0/pioasm/pioasm.exe")
    libpath = directory / ("overlay.dll" if os.name == "nt" else "overlay.so")
    adapter = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    normalize = directory / "normalize.c"
    normalize.write_text('''#include <stdint.h>
enum { TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER = 3 };
static unsigned s_tdma_pio_spi_program_persona;
static uint32_t tdma_pio_spi_phys_rx_ring_word(uint64_t produced) { return produced; }
static uint32_t __rev(uint32_t word) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 32; ++i) { result = (result << 1) | (word & 1); word >>= 1; }
    return result;
}
static uint8_t tdma_pio_spi_phys_rx_ring_reversed_byte(uint64_t produced) {
''' + c_definition_body(adapter, "tdma_pio_spi_phys_rx_ring_reversed_byte") + '''
}
static uint8_t tdma_pio_spi_phys_rx_ring_byte(uint64_t produced) {
''' + c_definition_body(adapter, "tdma_pio_spi_phys_rx_ring_byte") + '''
}
uint8_t normalize_rx(uint32_t word, unsigned persona) {
    s_tdma_pio_spi_program_persona = persona;
    return tdma_pio_spi_phys_rx_ring_byte(word);
}
''', encoding="utf-8")
    subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
                    f"-DPROJECT_NODE_CAPACITY={request.param}",
                    "-I" + str(ROOT / "components/tdma/inc"),
                    str(ROOT / "components/tdma/src/tdma_flight_overlay.c"),
                    str(ROOT / "components/tdma/src/tdma_transport_frame.c"),
                    str(normalize),
                    "-o", str(libpath)], check=True, capture_output=True)
    header = directory / "tdma.pio.h"
    subprocess.run([pioasm, "-o", "c-sdk", str(ROOT / "components/tdma/src/tdma_pio_spi.pio"),
                    str(header)], check=True, capture_output=True)
    source = header.read_text(encoding="utf-8")
    program = "tdma_pio_spi_flight_process_follower"
    body = source.split(f"{program}_program_instructions[] = {{", 1)[1].split("};", 1)[0]
    instructions = [int(value, 16) for value in re.findall(r"0x([0-9a-f]{4}),", body)]
    final = int(re.search(rf"#define {program}_offset_final_bit (\d+)u", source)[1])
    wrap = int(re.search(rf"#define {program}_wrap (\d+)", source)[1])
    assert len(instructions) == 28 and instructions[final] == LIVE
    init = source.split(f"static inline void {program}_program_init(", 1)[1].split("\n}", 1)[0]
    assert "sm_config_set_in_shift(&c, true, false, 32u)" in init
    lib = C.CDLL(str(libpath))
    lib.node_capacity = request.param
    lib.packet_size = request.param * 32 + 4 + 32
    lib.physical_bytes = lib.packet_size + 15
    lib.tdma_flight_overlay_build_plan.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t,
        C.c_void_p, C.c_size_t, C.POINTER(Config), C.POINTER(Plan)]
    lib.tdma_flight_overlay_build_plan.restype = C.c_bool
    lib.tdma_flight_overlay_bind_plan.argtypes = [C.POINTER(Plan), C.c_uint32, C.POINTER(Binding)]
    lib.tdma_flight_overlay_bind_plan.restype = C.c_bool
    lib.tdma_flight_overlay_build_pass_plan.argtypes = [C.c_uint32, C.c_uint32, C.POINTER(Plan)]
    lib.tdma_flight_overlay_build_pass_plan.restype = C.c_bool
    lib.tdma_transport_frame_resident_overlay_header_mask.restype = C.c_uint32
    lib.normalize_rx.argtypes = [C.c_uint32, C.c_uint32]
    lib.normalize_rx.restype = C.c_uint8
    for byte in range(256):
        assert lib.normalize_rx(reverse32(byte), 3) == byte
        for persona in (0, 1, 2):
            assert lib.normalize_rx(0x89ABCD00 | byte, persona) == byte
    return lib, instructions, final, wrap


def pack(bits):
    return bytes(sum(bits[i + k] << (7 - k) for k in range(8))
                 for i in range(0, len(bits), 8))


def binding_template():
    # Opaque fake endpoints: the C binder must never access peripheral memory.
    return Binding(0x101, 0x111, 0x201, 0x301, 0x50000010, 0x20001000,
                   0x20001004, 0x50000100, 0x20001008)


def bound_words(lib, plan, final):
    binding = binding_template()
    tokens = bytes(plan.token)
    assert lib.tdma_flight_overlay_bind_plan(C.byref(plan), final, C.byref(binding))
    assert bytes(plan.token) == tokens and plan.generation == 0
    first, last = plan.run[0], plan.run[plan.run_count - 1]
    assert (first.control, first.write_address, first.transfer_count, first.read_address) == (
        binding.selection_control, binding.selected_generation_address, 1,
        (C.addressof(plan) + Plan.generation.offset) & 0xFFFFFFFF)
    assert (last.control, last.write_address, last.transfer_count, last.read_address) == (
        binding.restart_control, binding.loader_trigger_address, 1, binding.next_address_address)
    # Interpret DMA data descriptors independently, resolving only the granted
    # live constant and this plan's token pool; compare their PIO input stream.
    words = []
    token_base = C.addressof(plan.token) & 0xFFFFFFFF
    for run in plan.run[1:plan.run_count - 1]:
        assert run.write_address == binding.tx_fifo_address and run.transfer_count > 0
        if run.control == binding.pass_control:
            assert run.read_address == binding.live_word_address
            words.extend([LIVE << 16 | LIVE] * run.transfer_count)
        else:
            assert run.control == binding.token_control
            offset = (run.read_address - token_base) & 0xFFFFFFFF
            assert offset % 4 == 0 and offset // 4 + run.transfer_count <= plan.token_word_count
            words.extend(plan.token[offset // 4:offset // 4 + run.transfer_count])
    assert len(words) == plan.command_word_count
    frozen = bytes(plan)
    assert not lib.tdma_flight_overlay_bind_plan(C.byref(plan), final, C.byref(binding))
    assert bytes(plan) == frozen  # A second bind cannot shift the live descriptors.
    return words


@pytest.mark.parametrize("damage", ["none", "token", "offset", "count", "runs", "generation",
                                    "endpoint_zero", "endpoint_alignment", "final_pc"])
def test_binding_pass_and_rejection(engine, damage):
    lib, _, final, _ = engine
    plan, binding = Plan(), binding_template()
    assert lib.tdma_flight_overlay_build_pass_plan(307, final, C.byref(plan))
    if damage == "none":
        assert bound_words(lib, plan, final) == [LIVE << 16 | LIVE] * (307 * 4 - 1) + [LIVE << 16 | final]
        return
    if damage == "token": plan.token[0] ^= 0x10000
    if damage == "offset": plan.run[1].read_address = 1
    if damage == "count": plan.run[1].transfer_count = 0xFFFFFFFF
    if damage == "runs": plan.run_count = 8
    if damage == "generation": plan.generation = 1
    if damage == "endpoint_zero": binding.loader_trigger_address = 0
    if damage == "endpoint_alignment": binding.tx_fifo_address |= 1
    if damage == "final_pc": final += 1
    frozen = bytes(plan)
    assert not lib.tdma_flight_overlay_bind_plan(C.byref(plan), final, C.byref(binding))
    assert bytes(plan) == frozen


def case(engine, slot, shift, byte_shift, installed, delay, period, high, rx_drop):
    lib, instructions, final, wrap = engine
    # Exactly the relocation performed by the SDK PIO loader; OUT EXEC tokens
    # are generated by C with the installed final PC, not relocated afterwards.
    program = [0] * installed + [
        (word & ~31) | ((word & 31) + installed) if word >> 13 == 0 else word
        for word in instructions]
    rising = [i for i, word in enumerate(program) if word == 0x2081]
    assert len(rising) == 3
    for i, position in enumerate(rising):
        program[position] |= (delay - 2 if i == 0 else delay) << 8
    rng = random.Random(slot * 8192 + shift * 256 + byte_shift)
    old = bytes(rng.randrange(256) for _ in range(lib.packet_size))
    updated = bytearray(old)
    start, end = 32 + slot * 32, 64 + slot * 32
    updated[start:end] = bytes(rng.randrange(256) for _ in range(32))
    updated[start] = old[start]  # Equal-to-snapshot forced update still owns this byte.
    header_mask = lib.tdma_transport_frame_resident_overlay_header_mask()
    for i in range(32):
        if header_mask & (1 << i): updated[i] ^= 255
    forced = (C.c_uint32 * (lib.node_capacity + 1))()
    forced[slot] = 0xFFFFFFFF
    config = Config(lib.physical_bytes, 4, byte_shift, shift, slot, header_mask, final + installed)
    plan = Plan()
    assert lib.tdma_flight_overlay_build_plan(old, bytes(updated), len(old), forced,
                                             len(forced), C.byref(config), C.byref(plan))
    words = []
    for run in plan.run[:plan.run_count]:
        words.extend(plan.token[run.read_address:run.read_address + run.transfer_count]
                     if run.control else [LIVE << 16 | LIVE] * run.transfer_count)
    assert bound_words(lib, plan, final + installed) == words
    tokens = [half for word in words for half in (word >> 16, word & 65535)]
    physical, expected = [], []
    base = (4 + byte_shift) * 8 + shift
    for frame in range(2):
        live = bytearray(rng.randrange(256) for _ in range(lib.packet_size))
        wire = [rng.randrange(2) for _ in range(lib.physical_bytes * 8)]
        wire[base:base + len(live) * 8] = bytes_to_bits(live)
        physical.append(pack(wire))
        live[start:end] = updated[start:end]
        for i in range(32):
            if header_mask & (1 << i): live[i] ^= old[i] ^ updated[i]
        oracle = wire[:]
        oracle[base:base + len(live) * 8] = bytes_to_bits(live)
        previous = [0] * 8 if not frame else bytes_to_bits(physical[frame - 1][-1:])
        expected.extend(previous + oracle[:-8])
    machine = Machine(physical, [tokens, tokens], program=program, entry=installed,
                      wrap=wrap + installed, final=final + installed, period=period,
                      high=high, rx_drop=rx_drop).run()
    assert [bit for _, bit in machine.output] == expected
    assert [lib.normalize_rx(word, 3) for word in machine.pushes] == (
        [] if rx_drop else list(physical[0] + physical[1]))
    for index, (frame, ordinal, sample, _) in enumerate(machine.samples):
        assert index == frame * machine.frame_bits + ordinal
        assert sample == machine.starts[frame] + ordinal * period + delay + 1
    return machine


@pytest.mark.parametrize("installed,rx_drop", [(0, False), (4, True)])
@pytest.mark.parametrize("byte_shift", [0, 9])
@pytest.mark.parametrize("shift", range(8))
def test_live_neighbor_and_rx_loss(engine, installed, rx_drop, byte_shift, shift):
    for slot in range(engine[0].node_capacity):
        case(engine, slot, shift, byte_shift, installed, 15, 25, 12, rx_drop)


@pytest.mark.parametrize("delay,high", [(2, 4), (2, 8), (15, 12), (31, 16)])
def test_complete_byte_budget_and_negative_control(engine, delay, high):
    bound = max(delay + 2, high) + 7
    case(engine, 3, 5, 0, 4, delay, bound, high, False)
    with pytest.raises(AssertionError):
        case(engine, 3, 5, 0, 4, delay, bound - 1, high, False)


class Build(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        "frame_class", "origin_slot_id", "transport_sequence", "payload_class",
        "flags", "schedule_crc32", "ring_profile_crc32", "hop_limit")] + [
        ("payload", C.c_void_p), ("payload_size", C.c_size_t)]


def transport(lib, sequence, hop):
    payload_size = lib.node_capacity * 32 + 4
    payload = C.create_string_buffer(bytes(range(lib.node_capacity * 32)) + bytes(4))
    build = Build(1, 0, sequence, 7, 5, 0xABCDEF12, 0x12567890, 8,
                  C.cast(payload, C.c_void_p), payload_size)
    packet, size, result = C.create_string_buffer(lib.packet_size), C.c_size_t(), C.c_uint32()
    assert lib.tdma_transport_frame_encode(C.byref(build), packet, lib.packet_size,
                                           C.byref(size), C.byref(result))
    assert size.value == lib.packet_size
    for _ in range(hop):
        assert lib.tdma_transport_frame_advance_hop(packet, lib.packet_size, C.byref(result))
    return packet.raw


def expand(plan):
    words = []
    for run in plan.run[:plan.run_count]:
        words.extend(plan.token[run.read_address:run.read_address + run.transfer_count]
                     if run.control else [LIVE << 16 | LIVE] * run.transfer_count)
    return [half for word in words for half in (word >> 16, word & 65535)]


def syndrome(packet):
    return zlib.crc32(packet[:28] + bytes(4)) ^ int.from_bytes(packet[28:32], "little")


@pytest.mark.parametrize("hop", range(8))
def test_reused_hop_plan_advances_live_sequences_and_preserves_errors(engine, hop):
    lib, instructions, final, wrap = engine
    old, processed = transport(lib, 123, hop), transport(lib, 123, hop + 1)
    plan = Plan()
    config = Config(lib.physical_bytes, 4, 0, 7, 3,
                    lib.tdma_transport_frame_resident_overlay_header_mask(), final)
    assert lib.tdma_flight_overlay_build_plan(old, processed, lib.packet_size, None, 0,
                                             C.byref(config), C.byref(plan))
    tokens = expand(plan)
    # Header deltas from the real builder, executed on both possible live bits.
    # The local mailbox is unchanged, so every token is LIVE or INVERT.
    from tdma_flight_pio_model import INVERT
    base = (config.outer_header_size + 1) * 8 + config.alignment_bit_shift
    mask = bytes(sum((tokens[base + byte * 8 + bit] == INVERT) << (7 - bit)
                     for bit in range(8)) for byte in range(lib.packet_size))
    assert all(token in (LIVE, INVERT, final) for token in tokens)
    assert mask == bytes(a ^ b for a, b in zip(old, processed))
    sequences = [0, 0xFFFFFFFF, 0xFFFFFFFE] + [1 << bit for bit in range(32)]
    for sequence in sequences:
        live = transport(lib, sequence, hop)
        expected = transport(lib, sequence, hop + 1)
        transformed = bytes(a ^ b for a, b in zip(live, mask))
        assert transformed == expected and syndrome(transformed) == 0
        # Every single header bit corruption must retain its exact syndrome,
        # including corrupted hop and transport CRC bits (no silent repair).
        for error_bit in range(256):
            corrupt = bytearray(live)
            corrupt[error_bit // 8] ^= 1 << (error_bit % 8)
            outgoing = bytes(a ^ b for a, b in zip(corrupt, mask))
            assert syndrome(outgoing) == syndrome(corrupt) != 0
    # A predicted CRC used as a constant is a concrete negative control.
    stale = bytearray(transport(lib, 124, hop + 1))
    stale[28:32] = processed[28:32]
    assert syndrome(stale) != 0

    program = list(instructions)
    for i, position in enumerate(j for j, word in enumerate(program) if word == 0x2081):
        program[position] |= (13 if i == 0 else 15) << 8
    physical, expected = [], []
    wire_base = 4 * 8 + 7
    for sequence in [0xFFFFFFFF, 0]:
        incoming, outgoing = transport(lib, sequence, hop), transport(lib, sequence, hop + 1)
        wire = [0] * (lib.physical_bytes * 8)
        wire[wire_base:wire_base + lib.packet_size * 8] = bytes_to_bits(incoming)
        physical.append(pack(wire))
        wire[wire_base:wire_base + lib.packet_size * 8] = bytes_to_bits(outgoing)
        expected.extend([0] * 8 + wire[:-8])
    machine = Machine(physical, [tokens, tokens], program=program, entry=0,
                      wrap=wrap, final=final).run()
    assert [bit for _, bit in machine.output] == expected
    assert [lib.normalize_rx(word, 3) for word in machine.pushes] == list(b"".join(physical))


def test_compiled_overlay_rejects_another_packet_length(engine):
    lib, _, final, _ = engine
    packet = bytes(292)
    other_size = 292 if lib.node_capacity == 6 else 228
    config = Config(307, 4, 0, 0, 0, 0, final)
    plan = Plan()
    assert not lib.tdma_flight_overlay_build_plan(packet, packet, other_size, None, 0,
                                                 C.byref(config), C.byref(plan))
    assert plan.run_count == 0 and plan.generation == 0
