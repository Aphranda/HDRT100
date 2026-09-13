"""Execute production latch/RTT routines and exact timing arithmetic on a host bus."""
import ctypes as C
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]
U32 = (1 << 32) - 1
U64 = (1 << 64) - 1


@pytest.fixture(scope="module")
def timing(tmp_path_factory):
    out = tmp_path_factory.mktemp("latch-arithmetic")
    phys = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    io = (ROOT / "components/tdma/src/tdma_pio_spi_phys_flight_io.inc").read_text(encoding="utf-8")
    arithmetic = (ROOT / "components/tdma/src/tdma_pio_spi_phys_timing.c").read_text(encoding="utf-8")
    prefix = r'''
#include "tdma_pio_spi_phys_timing.h"
#define TDMA_SERVICE_TIMING_ENABLED 1
#include "tdma_service_timing.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>
typedef unsigned uint;
typedef unsigned PIO;
enum { clk_sys = 5, TDMA_PIO_SPI_ROLE_MASTER = 0, TDMA_PIO_SPI_ROLE_SLAVE = 1,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN = 11,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER = 12,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER = 13,
    TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY = 1,
    TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED = 2 };
typedef struct {
    uint32_t role;
    bool armed, flight_clock_latch_armed, flight_tx_clock_latch_armed;
    uint64_t flight_clock_latch_epoch_ns, flight_tx_clock_latch_epoch_ns;
    uint32_t flight_clock_latch_resolution_ns, flight_tx_clock_latch_resolution_ns;
    struct { uint32_t clock_latch_resolution_ns, clock_latch_miss_count, clock_latch_count; } snapshot;
} tdma_pio_spi_phys_t;
static unsigned s_tdma_pio_spi_program_persona;
static uint32_t hz, clock_calls, event[16], events, endpoint, fifo[2], fifo_cursor;
static const uint64_t epoch = UINT64_C(0x100000001);
static uint32_t s_tdma_pio_spi_flight_clock_latch_offset = 19;
static uint64_t probe_ticks;
static uint32_t probe_calls[TDMA_TIMING_STAGE_COUNT];
uint64_t tdma_service_timing_now(void) { return ++probe_ticks; }
void tdma_service_timing_record(tdma_service_timing_stage_t stage, uint64_t start) {
    assert(start <= probe_ticks && stage < TDMA_TIMING_STAGE_COUNT); ++probe_calls[stage];
}
static void record(uint32_t e) { assert(events < 16); event[events++] = e; }
static void endpoint_check(PIO p, uint sm) { assert(p == endpoint && sm == endpoint + 1); }
static uint32_t clock_get_hz(uint c) { assert(c == clk_sys); ++clock_calls; return hz; }
static uint64_t vdc_timestamp_clock_now_ns(void) { record(8); return epoch; }
static bool tdma_pio_spi_phys_is_flight_persona(void) {
    return (s_tdma_pio_spi_program_persona >= 11 && s_tdma_pio_spi_program_persona <= 13) ||
        s_tdma_pio_spi_program_persona == 16;
}
static PIO tdma_pio_spi_phys_evidence_pio(const tdma_pio_spi_phys_t *p) { (void)p; return 1; }
static uint tdma_pio_spi_phys_latch_sm(const tdma_pio_spi_phys_t *p) { (void)p; return 2; }
static uint tdma_pio_spi_phys_latch_offset(const tdma_pio_spi_phys_t *p) { (void)p; return 17; }
static PIO tdma_pio_spi_phys_tx_latch_pio(const tdma_pio_spi_phys_t *p) { (void)p; return 2; }
static uint tdma_pio_spi_phys_tx_latch_sm(const tdma_pio_spi_phys_t *p) { (void)p; return 3; }
static uint tdma_pio_spi_phys_rtt_sm(const tdma_pio_spi_phys_t *p) { (void)p; return 2; }
static void pio_sm_set_enabled(PIO p, uint sm, bool en) { endpoint_check(p,sm); record(en ? 9 : 1); }
static void pio_sm_clear_fifos(PIO p, uint sm) { endpoint_check(p,sm); record(2); }
static void pio_sm_restart(PIO p, uint sm) { endpoint_check(p,sm); record(3); }
static void pio_sm_put_blocking(PIO p, uint sm, uint32_t word) {
    endpoint_check(p,sm); assert(word == UINT32_MAX && events == 3 && event[1] == 2); record(4);
}
enum { pio_x = 1, pio_osr = 2 };
static uint pio_encode_pull(bool a, bool b) { assert(!a && b); return 5; }
static uint pio_encode_mov(uint a, uint b) { assert(a == pio_x && b == pio_osr); return 6; }
static uint pio_encode_jmp(uint offset) { assert(offset == (endpoint == 1 ? 17 : 19)); return 7; }
static void pio_sm_exec(PIO p, uint sm, uint command) { endpoint_check(p,sm); record(command); }
static bool pio_sm_is_rx_fifo_empty(PIO p, uint sm) { endpoint_check(p,sm); return fifo_cursor == 2; }
static uint32_t pio_sm_get(PIO p, uint sm) { endpoint_check(p,sm); assert(fifo_cursor < 2); return fifo[fifo_cursor++]; }
'''
    routines = "\n".join("static bool " + name + "(tdma_pio_spi_phys_t *phys) {" +
        c_definition_body(phys, name) + "}\n" for name in (
            "tdma_pio_spi_phys_clock_latch_rearm", "tdma_pio_spi_phys_tx_clock_latch_rearm"))
    routines += "static bool tdma_pio_spi_phys_tx_clock_latch_read_and_rearm(tdma_pio_spi_phys_t *phys, uint64_t *timestamp_ns) {" + c_definition_body(phys, "tdma_pio_spi_phys_tx_clock_latch_read_and_rearm") + "}\n"
    routines += "bool tdma_pio_spi_phys_feedback_round_trip(void *context, uint32_t *round_trip_ns, uint32_t *resolution_ns, uint32_t *flags) {" + c_definition_body(io, "tdma_pio_spi_phys_feedback_round_trip") + "}\n"
    # Native ABI and explicitly modeled RP2350 size_t arithmetic execute the
    # same production body. Python supplies an independent unsigned oracle.
    wire = c_definition_body(arithmetic, "tdma_pio_spi_phys_wire_time_ns")
    routines += "uint64_t wire_native(uint32_t baud_hz, size_t packet_size, size_t packet_header_size) {" + wire + "}\n"
    routines += "uint64_t wire_32(uint32_t baud_hz, uint32_t packet_size, uint32_t packet_header_size) {" + wire + "}\n"
    suffix = r'''
uint32_t resolution(uint32_t frequency) { return tdma_pio_spi_phys_latch_resolution_ns(frequency); }
void exercise_tx_read(unsigned mode) {
    tdma_pio_spi_phys_t p = {.role = TDMA_PIO_SPI_ROLE_SLAVE, .flight_tx_clock_latch_armed = true,
        .flight_tx_clock_latch_epoch_ns = mode == 3 ? UINT64_MAX - 1 : 1000,
        .flight_tx_clock_latch_resolution_ns = 8};
    uint64_t timestamp = 9999;
    hz = mode == 4 ? 0 : 250000000; s_tdma_pio_spi_program_persona = 13;
    endpoint = 2; events = clock_calls = 0; fifo_cursor = mode == 1 ? 2 : 0;
    fifo[0] = UINT32_MAX - 2;
    memset(probe_calls, 0, sizeof(probe_calls));
    const bool ok = tdma_pio_spi_phys_tx_clock_latch_read_and_rearm(mode == 0 ? NULL : &p, &timestamp);
    assert(ok == (mode == 2));
    assert(timestamp == ((mode == 2 || mode == 4) ? 1016u : 0u));
    assert(probe_calls[TDMA_TIMING_TX_LATCH_READ] == 1);
    assert(probe_calls[TDMA_TIMING_TX_LATCH_REARM] == (mode >= 2));
    assert(clock_calls == (mode >= 2));
    assert(events == ((mode == 2 || mode == 3) ? 9u : 0u));
    assert(p.snapshot.clock_latch_miss_count == (mode == 1 || mode == 3));
    assert(p.snapshot.clock_latch_count == (mode == 2 || mode == 4));
}
void exercise_rearm(uint32_t frequency, unsigned persona, unsigned role, unsigned tx, unsigned null_phys) {
    tdma_pio_spi_phys_t p = {.role = role};
    const tdma_pio_spi_phys_t before = p;
    hz = frequency; s_tdma_pio_spi_program_persona = persona;
    events = clock_calls = 0; endpoint = tx ? 2 : 1;
    const bool admitted = !null_phys && (tx ? role == TDMA_PIO_SPI_ROLE_SLAVE &&
        tdma_pio_spi_phys_is_flight_persona() : persona >= 11 && persona <= 13);
    const uint32_t expected = frequency ? (uint32_t)((UINT64_C(2000000000) + frequency / 2u) / frequency) : 0;
    const bool ok = tx ? tdma_pio_spi_phys_tx_clock_latch_rearm(null_phys ? NULL : &p)
        : tdma_pio_spi_phys_clock_latch_rearm(null_phys ? NULL : &p);
    assert(ok == (admitted && expected != 0));
    assert(clock_calls == (admitted ? 1u : 0u));
    if (!ok) { assert(events == 0 && memcmp(&p,&before,sizeof(p)) == 0); return; }
    assert(events == 9);
    for (uint i = 0; i < events; ++i) assert(event[i] == i + 1);
    if (tx) {
        assert(p.flight_tx_clock_latch_armed && p.flight_tx_clock_latch_epoch_ns == epoch);
        assert(p.flight_tx_clock_latch_resolution_ns == expected && !p.flight_clock_latch_armed);
        assert(!p.snapshot.clock_latch_resolution_ns);
    } else {
        assert(p.flight_clock_latch_armed && p.flight_clock_latch_epoch_ns == epoch);
        assert(p.flight_clock_latch_resolution_ns == expected && !p.flight_tx_clock_latch_armed);
        assert(p.snapshot.clock_latch_resolution_ns == expected);
    }
}
unsigned exercise_feedback(uint32_t frequency, uint32_t remaining, uint32_t *duration, uint32_t *res, uint32_t *flags) {
    tdma_pio_spi_phys_t p = {.role = TDMA_PIO_SPI_ROLE_MASTER, .armed = true};
    hz = frequency; s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN;
    endpoint = 1; fifo[0] = 123; fifo[1] = remaining; fifo_cursor = clock_calls = 0;
    const bool ok = tdma_pio_spi_phys_feedback_round_trip(&p,duration,res,flags);
    assert(fifo_cursor == 2 && clock_calls == 1); /* Preserve newest-completed interval semantics. */
    return ok;
}
'''
    source = out / "timing.c"
    source.write_text(prefix + routines + suffix, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    library = out / ("timing.dll" if os.name == "nt" else "timing.so")
    run = subprocess.run([gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
        "-I" + str(ROOT / "components/tdma/inc"), str(source), "-o", str(library)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    lib = C.CDLL(str(library))
    lib.resolution.argtypes, lib.resolution.restype = [C.c_uint32], C.c_uint32
    lib.wire_native.argtypes, lib.wire_native.restype = [C.c_uint32, C.c_size_t, C.c_size_t], C.c_uint64
    lib.wire_32.argtypes, lib.wire_32.restype = [C.c_uint32] * 3, C.c_uint64
    lib.exercise_rearm.argtypes, lib.exercise_rearm.restype = [C.c_uint32] * 5, None
    lib.exercise_tx_read.argtypes, lib.exercise_tx_read.restype = [C.c_uint32], None
    lib.exercise_feedback.argtypes = [C.c_uint32] * 2 + [C.POINTER(C.c_uint32)] * 3
    lib.exercise_feedback.restype = C.c_uint
    return lib


def test_tx_latch_probe_preserves_empty_overflow_and_failed_rearm(timing):
    for mode in range(5):
        timing.exercise_tx_read(mode)


def test_latch_resolution_rounding_transitions(timing):
    rng = random.Random(913040)
    clocks = {0, 1, U32, U32 - 1, 125_000_000, 150_000_000, 250_000_000, 4_000_000_000, 4_000_000_001}
    # Boundaries on either side of every low-frequency integer quotient and
    # half-integer rounding boundary, plus full-range random inputs.
    for divisor in range(1, 4096):
        for pivot in (2_000_000_000 // divisor, 4_000_000_000 // (2 * divisor + 1)):
            clocks.update(range(max(1, pivot - 2), min(U32, pivot + 2) + 1))
    clocks.update(rng.randrange(U32 + 1) for _ in range(20000))
    for hz in clocks:
        assert timing.resolution(hz) == ((2_000_000_000 + hz // 2) // hz if hz else 0)


@pytest.mark.parametrize("bits", [32, C.sizeof(C.c_size_t) * 8])
def test_wire_time_fast_and_wrapping_fallback(timing, bits):
    fn = timing.wire_32 if bits == 32 else timing.wire_native
    mask = (1 << bits) - 1
    rng = random.Random(bits)
    sizes = [(0, 0), (292, 4), (296, 0), (536870911, 0), (536870912, 0),
             (U64 // 1_000_000_000 // 8, 4), (mask, 1), (mask - 3, 4), (mask, mask)]
    for _ in range(4000): sizes.append((rng.randrange(mask + 1), rng.randrange(1024)))
    for baud in (0, 1, 3, 1_000_000, 5_000_000, 10_000_000, 20_000_000, 25_000_000,
                 31_250_000, 123_456_789, 1_000_000_000, U32):
        effective = baud or 1_000_000
        for packet, header in sizes:
            count = ((((packet & mask) + (header & mask)) & mask) * 8) & U64
            expected = ((count * 1_000_000_000 + effective - 1) & U64) // effective
            assert fn(baud, packet & mask, header & mask) == expected


@pytest.mark.parametrize("tx", [False, True])
def test_rearm_preserves_rejections_and_pio_epoch_order(timing, tx):
    for hz in (0, 1, 125_000_000, 250_000_000, 4_000_000_000, 4_000_000_001, U32):
        for persona in (0, 11, 12, 13, 16):
            for role in (0, 1):
                for null_phys in (False, True):
                    timing.exercise_rearm(hz, persona, role, tx, null_phys)


@pytest.mark.parametrize("hz", [0, 1, 125_000_000, 250_000_000, 4_000_000_000, U32])
def test_feedback_keeps_rounding_newest_interval_and_overflow_rejection(timing, hz):
    for elapsed in (0, 1, U32 // 8, U32 // 8 + 1, U32):
        duration, res, flags = C.c_uint32(), C.c_uint32(), C.c_uint32()
        ok = timing.exercise_feedback(hz, U32 - elapsed, C.byref(duration), C.byref(res), C.byref(flags))
        resolution = (2_000_000_000 + hz // 2) // hz if hz else 0
        expected = elapsed * resolution
        assert bool(ok) == (0 < expected <= U32)
        assert (duration.value, res.value, flags.value) == ((expected, resolution, 2) if ok else (0, 0, 1))
