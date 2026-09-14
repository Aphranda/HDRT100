"""Exercise STOP barriers and the real SCPI serialization of a frozen cut."""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from tools.calibration_ring_validate import tdma_rx_start_cut as cut
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def response(**changes):
    values = dict.fromkeys(cut.FIELDS, 0)
    values.update(schema=1, arm_epoch=0x100000001, produced_before=0x200000000,
                  produced_after=0x200000100, sample_before_ticks=0x123456789,
                  sample_after_ticks=0x1234567FF, **changes)
    return '"RXSTARTCUT",' + ','.join(str(values[name]) for name in cut.FIELDS)


def test_decoder_preserves_wide_values_and_failed_bracket():
    decoded = cut.decode_start_cut(response(retire_reasons=0xFFFFFFFF))
    assert decoded["arm_epoch"] == 0x100000001
    assert decoded["produced_after"] - decoded["produced_before"] == 256
    assert decoded["retire_reasons"] == 0xFFFFFFFF
    # An inconsistent raw interval remains evidence; it is not repaired.
    raw = response().replace(str(0x1234567FF), "0")
    assert cut.decode_start_cut(raw)["sample_after_ticks"] == 0
    assert cut.decode_start_cut('"UNAVAILABLE"') is None


@pytest.mark.parametrize("raw", ["", "<timeout>", '"UNAVAILABLE",0',
                                 response().replace('"RXSTARTCUT",1,', '"RXSTARTCUT",2,', 1),
                                 response(flags=-1), response(flags=1 << 32),
                                 response(capture_fifo_before=256), response()+",0"])
def test_malformed_response_is_rejected(raw):
    with pytest.raises((ValueError, StopIteration)):
        cut.decode_start_cut(raw)


class Backend:
    def __init__(self, barrier_failure=None, export_failure=None):
        self.barrier_failure = barrier_failure
        self.export_failure = export_failure
        self.ack = set()
        self.commands = []

    def query(self, uid, command):
        self.commands.append((uid, command))
        if command == "*IDN?":
            return f'"test,{uid}"'
        if command == "SYSTem:FW:BUILD?":
            return "20260914184059"
        if command == "SYSTem:TDMA:RING:STATus?":
            fields = [0] * 40
            fields[38:] = [8, 8]
            if uid == self.barrier_failure:
                fields[0] = 1
            else:
                self.ack.add(uid)
            return ','.join(map(str, fields))
        assert command == cut.COMMAND and len(self.ack) == 4
        if uid == self.export_failure:
            return "<timeout>"
        return '"UNAVAILABLE"' if uid == "NO1" else response()


def export(tmp_path, backend):
    return cut.export_frozen_cuts({f"NO{i}": f"NO{i}" for i in range(1, 5)},
        backend.query, tmp_path / "cuts.json", expected_build="20260914184059")


def test_all_ack_precede_single_cut_reads(tmp_path):
    backend = Backend()
    report = export(tmp_path, backend)
    assert report["export_passed"] and not report["identity_qualified"]
    assert not report["timestamp_qualified"] and not report["dpll_qualified"]
    assert len(report["boards"]) == 4 and report["boards"][0]["cut"] is None
    assert [uid for uid, cmd in backend.commands if cmd == cut.COMMAND] == ["NO1", "NO2", "NO3", "NO4"]
    assert json.loads((tmp_path / "cuts.json").read_text(encoding="utf-8")) == report
    count = len(backend.commands)
    with pytest.raises(FileExistsError):
        export(tmp_path, backend)
    assert len(backend.commands) == count


def test_one_failed_stop_prevents_every_cut_read(tmp_path):
    backend = Backend(barrier_failure="NO3")
    report = export(tmp_path, backend)
    assert not report["export_passed"] and "barrier:NO3" in report["errors"]
    assert not report["boards"] and not any(cmd == cut.COMMAND for _, cmd in backend.commands)


def test_bad_export_keeps_raw_and_other_boards_without_retry(tmp_path):
    backend = Backend(export_failure="NO3")
    report = export(tmp_path, backend)
    assert not report["export_passed"] and "export:NO3" in report["errors"]
    assert len(report["boards"]) == 4
    assert report["boards"][2]["commands"][0]["raw"] == "<timeout>"
    assert report["boards"][3]["passed"]
    assert sum(cmd == cut.COMMAND for _, cmd in backend.commands) == 4


def test_real_scpi_callback_roundtrip(tmp_path):
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    body = c_definition_body(source, "scpi_cmd_system_tdma_rx_start_cut_q")
    generated = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "tdma_rx_start_cut.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
static bool available;
bool tdma_pio_spi_phys_get_rx_start_cut(tdma_rx_start_cut_t *out) {
    memset(out, 0xa5, sizeof(*out));
    out->schema = 1;
    out->produced_before = UINT64_C(0x123456789abcdef0);
    out->produced_after = UINT64_C(0x123456789abcdf42);
    out->observation_epoch_after = UINT64_MAX;
    out->capture_fifo_after = 8;
    out->capture_pc_before = 31;
    out->dma_count_before = 0x12345678u;
    out->retire_reasons = UINT32_MAX;
    return available;
}
static void SCPI_ResultText(scpi_t *ctx, const char *s) {
    if ((*ctx)++) putchar(',');
    printf("\"%s\"", s);
}
static void SCPI_ResultUInt64(scpi_t *ctx, uint64_t value) {
    if ((*ctx)++) putchar(',');
    printf("%" PRIu64, value);
}
static void SCPI_ResultUInt32(scpi_t *ctx, uint32_t value) { SCPI_ResultUInt64(ctx, value); }
'''
    generated += "static scpi_result_t callback(scpi_t *context) {" + body + "}\n"
    generated += "int main(void) { scpi_t ctx=0; callback(&ctx); puts(\"\"); available=true; ctx=0; callback(&ctx); puts(\"\"); return 0; }\n"
    path = tmp_path / "scpi_cut.c"
    path.write_text(generated, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    exe = tmp_path / ("scpi_cut.exe" if os.name == "nt" else "scpi_cut")
    cmd = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
           "-I" + str(ROOT / "components/tdma/inc"), str(path), "-o", str(exe)]
    result = subprocess.run(cmd, text=True, capture_output=True, timeout=60)
    (tmp_path / "compile.log").write_text(result.stdout+result.stderr, encoding="utf-8")
    (tmp_path / "compile.json").write_text(json.dumps(dict(command=cmd, exit_code=result.returncode)), encoding="utf-8")
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(exe)], text=True, capture_output=True, timeout=10)
    (tmp_path / "run.log").write_text(result.stdout+result.stderr, encoding="utf-8")
    assert result.returncode == 0
    absent, raw = result.stdout.splitlines()
    assert cut.decode_start_cut(absent) is None
    values = cut.decode_start_cut(raw)
    assert values["arm_epoch"] == 0xA5A5A5A5A5A5A5A5
    assert values["produced_before"] == 0x123456789ABCDEF0
    assert values["produced_after"] == 0x123456789ABCDF42
    assert values["observation_epoch_after"] == (1 << 64)-1
    assert values["dma_count_before"] == 0x12345678
    assert values["capture_fifo_after"] == 8 and values["capture_pc_before"] == 31
    assert values["retire_reasons"] == (1 << 32)-1
