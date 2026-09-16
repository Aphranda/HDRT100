"""Execute maintenance readback callbacks; RUN must not yield partial records."""
import re
import os
import shutil
import subprocess

import pytest

from test_vdc_command_owner import ROOT


def callback(source, name):
    start = source.index('scpi_result_t ' + name + '(')
    end = source.index('\n}\n', start) + 3
    return source[start:end]


@pytest.fixture(scope='module')
def reader(tmp_path_factory):
    source = (ROOT / 'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    body = '\n'.join(callback(source, name) for name in (
        'scpi_cmd_system_tdma_priority_rx_q', 'scpi_cmd_system_tdma_priority_rx_record_q',
        'scpi_cmd_system_tdma_priority_rx_budget_q', 'scpi_cmd_system_tdma_priority_rx_timing_q'))
    directory = tmp_path_factory.mktemp('priority-scpi')
    source = directory / 'reader.c'
    source.write_text('#include "app.h"\n' + HARNESS + body + MAIN, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler, 'A host C compiler is required'
    exe = directory / ('reader.exe' if os.name == 'nt' else 'reader')
    result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        *['-I' + str(ROOT / path) for path in ('application/inc', 'config', 'components/tdma/inc')],
        str(source), '-o', str(exe)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.mark.parametrize('mode', range(11))
def test_snapshot_and_record_stop_identity_guards(reader, mode):
    result = subprocess.run([str(reader), str(mode)], capture_output=True, text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr


def test_commands_are_query_only_and_registered_once():
    source = (ROOT / 'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    commands = re.findall(r'\.pattern = "([^"]*PRIORity[^\"]*)"', source)
    assert commands == ['SYSTem:TDMA:FLIGHT:PRIORity?',
                        'SYSTem:TDMA:FLIGHT:PRIORity:BUDGet?',
                        'SYSTem:TDMA:FLIGHT:PRIORity:TIMing?',
                        'SYSTem:TDMA:FLIGHT:PRIORity:RECord?']


HARNESS = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "tdma_priority_rx.h"
typedef int scpi_result_t;
typedef struct { int unused; } scpi_t;
typedef struct { bool enabled, adapter_started; } tdma_ring_clock_snapshot_t;
#define TRUE 1
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static uint32_t mode, results, reads, error_count, param_count, values[64];
static void SCPI_ResultUInt32(scpi_t *c, uint32_t value) { (void)c; values[results++] = value; }
static void scpi_sync_result_u64_parts(scpi_t *c, uint64_t value)
{ SCPI_ResultUInt32(c, (uint32_t)value); SCPI_ResultUInt32(c, (uint32_t)(value >> 32)); }
static void SCPI_ResultText(scpi_t *c, const char *value)
{
    (void)c; ++results;
    assert(strlen(value) == 64);
    for (uint32_t i = 0; i < 64; ++i) assert(value[i] == (results == 7 ? 'a' : 'b'));
}
static void scpi_port_push_exec_error(scpi_t *c, const char *s)
{ (void)c; (void)s; ++error_count; }
static bool SCPI_ParamUInt32(scpi_t *c, uint32_t *v, int required)
{ (void)c; assert(required); *v = ++param_count; return mode != 8; }
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *s)
{ s->enabled = mode == 1; s->adapter_started = mode == 2; return mode != 3; }
static bool tdma_runtime_owner_get_priority_rx_snapshot(tdma_priority_rx_snapshot_t *s)
{
    memset(s, 0, sizeof(*s)); s->schema = 1; s->active = mode == 4; s->epoch = 1;
    return mode != 5;
}
static bool tdma_runtime_owner_copy_priority_rx(uint32_t epoch, uint32_t seq, tdma_priority_rx_record_t *r)
{
    ++reads; assert(epoch == 1 && seq == 2);
    memset(r, 0, sizeof(*r)); r->epoch = epoch; r->sequence = seq;
    memset(r->header, 0xaa, sizeof(r->header)); memset(r->mailbox, 0xbb, sizeof(r->mailbox));
    return mode != 6;
}
static uint32_t budget_reads;
static uint32_t timing_reads;
static bool tdma_runtime_owner_get_priority_rx_timing(tdma_priority_rx_timing_t *s)
{
    ++timing_reads; s->samples = 42u;
    for (uint32_t i = 0; i < 4; ++i) s->max_cycles[i] = 100u + i;
    return mode != 10;
}
bool app_realtime_get_priority_snapshot(app_realtime_priority_snapshot_t *s)
{
    ++budget_reads; memset(s, 0, sizeof(*s)); s->schema = 1;
    return mode != 9;
}
'''

MAIN = r'''
int main(int argc, char **argv)
{
    assert(argc == 2); mode = (uint32_t)atoi(argv[1]);
    scpi_t context = {0};
    int result = scpi_cmd_system_tdma_priority_rx_q(&context);
    bool rejected = (mode >= 1 && mode <= 3) || mode == 5;
    assert(result == (rejected ? SCPI_RES_ERR : SCPI_RES_OK));
    assert(results == (rejected ? 0u : 23u));
    assert(error_count == (rejected ? 1u : 0u));
    assert(reads == 0);
    results = error_count = 0;
    result = scpi_cmd_system_tdma_priority_rx_record_q(&context);
    rejected = (mode >= 1 && mode <= 6) || mode == 8;
    assert(result == (rejected ? SCPI_RES_ERR : SCPI_RES_OK));
    assert(results == (rejected ? 0u : 8u));
    assert(reads == ((mode == 0 || mode == 6 || mode == 7 || mode == 9 || mode == 10) ? 1u : 0u));
    results = error_count = 0;
    result = scpi_cmd_system_tdma_priority_rx_budget_q(&context);
    rejected = (mode >= 1 && mode <= 5) || mode == 9;
    assert(result == (rejected ? SCPI_RES_ERR : SCPI_RES_OK));
    assert(results == (rejected ? 0u : 6u + 3u * APP_REALTIME_PHASE_COUNT));
    assert(error_count == (rejected ? 1u : 0u));
    assert(budget_reads == ((mode >= 1 && mode <= 5) ? 0u : 1u));
    results = error_count = 0;
    result = scpi_cmd_system_tdma_priority_rx_timing_q(&context);
    rejected = (mode >= 1 && mode <= 5) || mode == 10;
    assert(result == (rejected ? SCPI_RES_ERR : SCPI_RES_OK));
    assert(results == (rejected ? 0u : 6u));
    assert(error_count == (rejected ? 1u : 0u));
    assert(timing_reads == ((mode >= 1 && mode <= 5) ? 0u : 1u));
    if (!rejected) {
        assert(values[0] == 1u && values[1] == 42u);
        for (uint32_t i = 0; i < 4; ++i) assert(values[i + 2u] == 100u + i);
    }
    return 0;
}
'''
