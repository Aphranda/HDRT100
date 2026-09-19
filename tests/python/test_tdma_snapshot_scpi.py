"""Exercise the diagnostic contention path through the real SCPI parser."""
import csv
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scpi/scpi.h"
static unsigned attempts, sleeps, unavailable;
static char output[16384];
static size_t used;
@TYPE@
static bool distributed_refmem_get_realtime_tdma(refmem_realtime_tdma_snapshot_t *s)
{
    ++attempts;
    /* A failed copy may have dirtied every field. Never emit that view. */
    memset(s, attempts <= unavailable ? 0xa5 : 0, sizeof(*s));
    s->service_count = attempts;
    return attempts > unavailable;
}
static void sleep_us(uint64_t us)
{
    assert(us == SCPI_TDMA_SNAPSHOT_BACKOFF_US);
    ++sleeps;
}
@CALLBACK@
static size_t capture(scpi_t *s, const char *data, size_t size)
{
    (void)s;
    assert(used + size < sizeof(output));
    memcpy(output + used, data, size);
    used += size;
    return size;
}
static scpi_result_t flush(scpi_t *s) { (void)s; return SCPI_RES_OK; }
static int error(scpi_t *s, int_fast16_t e) { (void)s; (void)e; return 0; }
static const scpi_command_t commands[] = {
    {"SYSTem:REFMEM:SYNC:TDMA:STATus?", scpi_cmd_refmem_sync_tdma_status_q, 0},
    {"SYSTem:ERRor?", SCPI_SystemErrorNextQ, 0},
    SCPI_CMD_LIST_END
};
int main(void)
{
    scpi_t context;
    char input[512], line[64];
    scpi_error_t errors[16];
    scpi_interface_t interface = {.write=capture, .flush=flush, .error=error};
    SCPI_Init(&context, commands, &interface, scpi_units_def,
              "test", "test", "test", "test", input, sizeof(input), errors, 16);
    while (fgets(line, sizeof(line), stdin)) {
        unavailable = (unsigned)strtoul(line, NULL, 10);
        attempts = sleeps = 0;
        used = 0;
        const char *command = "SYST:REFMEM:SYNC:TDMA:STAT?\n";
        for (size_t i = 0; i < strlen(command); ++i) SCPI_Input(&context, command + i, 1);
        assert(used != 0 && output[used - 1] == '\n');
        while (used && (output[used - 1] == '\r' || output[used - 1] == '\n')) --used;
        printf("%u|%u|", attempts, sleeps);
        fwrite(output, 1, used, stdout);
        putchar('|');
        used = 0;
        command = "SYST:ERR?\n";
        SCPI_Input(&context, command, strlen(command));
        while (used && (output[used - 1] == '\r' || output[used - 1] == '\n')) --used;
        fwrite(output, 1, used, stdout);
        putchar('\n');
    }
    return 0;
}
'''


@pytest.fixture(scope="module")
def snapshot_parser(tmp_path_factory):
    directory = tmp_path_factory.mktemp("tdma-snapshot-scpi")
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    start = source.index("scpi_result_t scpi_cmd_refmem_sync_tdma_status_q(")
    end = source.index("\nscpi_result_t ", start + 1)
    callback = source[start:end]
    defines = "\n".join(re.findall(r"^#define SCPI_TDMA_SNAPSHOT_.*$", source, re.M))
    fields = sorted(set(re.findall(r"snapshot\.(\w+)", callback)))
    arrays = set(re.findall(r"snapshot\.(\w+)\[", callback))
    structure = "typedef struct {\n" + "\n".join(
        f"uint32_t {field}" + ("[TDMA_TRAFFIC_CLASS_COUNT]" if field in arrays else "") + ";"
        for field in fields) + "\n} refmem_realtime_tdma_snapshot_t;"
    generated = directory / "snapshot.c"
    generated.write_text("#define TDMA_TRAFFIC_CLASS_COUNT 4u\n" + defines + "\n" +
                         HARNESS.replace("@TYPE@", structure).replace("@CALLBACK@", callback), encoding="utf-8")
    executable = directory / "snapshot.exe"
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe"
    library = ROOT / "third_party/scpi-parser/libscpi"
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(library / "inc"), str(generated),
               *(str(p) for p in sorted((library / "src").glob("*.c"))),
               *(["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]),
               "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


def run(parser, failures):
    result = subprocess.run([str(parser)], input="\n".join(map(str, failures)) + "\n",
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    return [line.split("|", 3) for line in result.stdout.splitlines()]


@pytest.mark.parametrize("failures", range(8))
def test_transient_contention_emits_only_fresh_complete_snapshot(snapshot_parser, failures):
    attempts, sleeps, response, error = run(snapshot_parser, [failures])[0]
    assert int(attempts) == failures + 1
    assert int(sleeps) == failures
    fields = next(csv.reader([response]))
    assert len(fields) > 100
    assert fields[3] == str(failures + 1)
    assert all(value == "0" for i, value in enumerate(fields) if i != 3)
    assert error == '0,"No error"'


@pytest.mark.parametrize("failures", [8, 1000000])
def test_persistent_contention_responds_and_next_query_can_recover(snapshot_parser, failures):
    failed, recovered = run(snapshot_parser, [failures, 0])
    assert failed[:3] == ["8", "7", '"BUSY"']
    assert failed[3] == '0,"No error"'
    assert recovered[:2] == ["1", "0"]
    assert recovered[3] == '0,"No error"'
