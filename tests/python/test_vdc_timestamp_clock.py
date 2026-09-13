"""Verify real device-path arithmetic against unbounded integer nanoseconds."""
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
MASK = (1 << 64) - 1


@pytest.fixture(scope="module")
def clock_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-timestamp-clock")
    (directory / "pico.h").write_text(
        "#define __not_in_flash_func(name) name\n", encoding="utf-8")
    hardware = directory / "hardware"
    hardware.mkdir()
    (hardware / "clocks.h").write_text(
        "#include <stdint.h>\nenum { clk_sys = 5 };\n"
        "uint32_t clock_get_hz(int clock);\n", encoding="utf-8")
    (hardware / "timer.h").write_text(
        "#include <stdint.h>\n"
        "typedef struct { volatile uint32_t pause, source, timelw, timehw, timerawl, timerawh; } test_timer_hw_t;\n"
        "extern test_timer_hw_t test_timer;\n"
        "#define timer1_hw (&test_timer)\n"
        "#define TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS 1u\n", encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / ("clock.exe" if os.name == "nt" else "clock")
    subprocess.run([
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-DPICO_ON_DEVICE=1",
        "-I" + str(directory), "-I" + str(ROOT / "components/vdc_domain/inc"),
        str(ROOT / "tests/unit/test_vdc_timestamp_clock.c"), "-o", str(executable),
    ], check=True, capture_output=True, text=True, timeout=60)
    return executable


def verify_cases(executable, frequencies):
    generator = random.Random(0x48414F46)
    cases = []
    for hz in frequencies:
        # Cross second boundaries, 32-bit timer rollover, the first ns-output
        # overflow, and the full uint64 input domain.
        overflow = ((1 << 64) * hz + 999_999_999) // 1_000_000_000 if hz else 0
        ticks = {0, 1, 2, hz, max(0, hz - 1), hz + 1, (1 << 32) - 1,
                 1 << 32, (1 << 32) + 1, MASK - 1, MASK}
        ticks.update(t for t in (overflow - 1, overflow, overflow + 1) if 0 <= t <= MASK)
        ticks.update(generator.getrandbits(64) for _ in range(24))
        cases.extend((hz, t) for t in sorted(ticks))
    result = subprocess.run([str(executable)],
        input="".join(f"{hz} {ticks}\n" for hz, ticks in cases),
        capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stderr
    output = result.stdout.splitlines()
    assert len(output) == len(cases)
    for (hz, ticks), row in zip(cases, output):
        converted, now, resolution = map(int, row.split())
        expected = (ticks * 1_000_000_000 // hz) & MASK if hz else 0
        assert converted == now == expected, (hz, ticks, row, expected)
        assert resolution == ((1_000_000_000 + hz - 1) // hz if hz else 0)


def test_exact_periods_and_uint64_wrap(clock_executable):
    # Every integer-nanosecond frequency, including clk_sys and host defaults.
    frequencies = sorted({2**a * 5**b for a in range(10) for b in range(10)})
    verify_cases(clock_executable, frequencies)


def test_fractional_periods_keep_floor_and_zero_clock(clock_executable):
    generator = random.Random(0x54444D41)
    frequencies = {0, 3, 7, 12_000_000, 133_000_000, 249_999_999,
                   250_000_001, 999_999_999, 1_000_000_001, (1 << 32) - 1}
    frequencies.update(generator.randrange(1, 1 << 32) for _ in range(512))
    verify_cases(clock_executable, sorted(frequencies))
