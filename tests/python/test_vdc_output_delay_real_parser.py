"""Actual libscpi SCPI_Input -> production DELay callback -> manager boundary.

Only the manager owner and transport writes are stubbed. The lexer, numeric
tokenization, error FIFO, command dispatch and callback body are real.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess

import pytest

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'AGENTS.md').exists())


def extract_callback(source, name):
    start = source.index('scpi_result_t ' + name + '(')
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


@pytest.fixture(scope='module')
def parser_host(tmp_path_factory):
    directory = tmp_path_factory.mktemp('output-delay-real-parser')
    library = ROOT / 'third_party/scpi-parser/libscpi'
    callback_file = ROOT / 'middleware/scpi_port/src/scpi_sync_commands.c'
    callback = extract_callback(callback_file.read_text(encoding='utf-8'), 'scpi_cmd_vdc_output_delay')
    unit = directory / 'parser_host.c'
    unit.write_text(HARNESS + '\n' + callback + '\n' + MAIN, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler
    executable = directory / 'parser_host.exe'
    sources = sorted((library / 'src').glob('*.c'))
    flags = ['-Wno-error=attributes'] if os.name == 'nt' else ['-lm']
    command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DSCPI_USER_CONFIG=1', '-I' + str(library / 'inc'),
               '-I' + str(ROOT / 'middleware/scpi_port/inc'), str(unit),
               *map(str, sources), *flags, '-o', str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    report = dict(command=command, returncode=result.returncode,
                  stdout=result.stdout, stderr=result.stderr,
                  callback_sha256=hashlib.sha256(callback.encode()).hexdigest(),
                  sources={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in [callback_file, *sources, ROOT / 'middleware/scpi_port/inc/scpi_user_config.h']})
    (directory / 'compile.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize('text,value', [
    ('0', 0), ('+0', 0), ('-0', 0), ('1', 1), ('-1', -1), ('+125', 125),
    ('-125', -125), ('2147483647', 2147483647), ('+2147483647', 2147483647),
    ('-2147483648', -2147483648), ('000000000000000001', 1),
    ('-000000000000000001', -1), ('  123  ', 123),
])
def test_real_parser_accepts_exact_signed_decimal(parser_host, text, value):
    result = subprocess.run([str(parser_host), text, 'accept', str(value)],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('text', [
    '', '+', '-', 'abc', '2147483648', '+2147483648', '-2147483649',
    '4294967295', '-4294967296', '9223372036854775807', '-9223372036854775808',
    '9223372036854775808', '-9223372036854775809', '18446744073709551615',
    '99999999999999999999999999999999999999',
    '-99999999999999999999999999999999999999',
    '1.5', '-1.5', '1e12', '-1e12', '1E+1', '1ns',
    '#HFFFFFFFFFFFFFFFF', '#H1', '#H123', '#Q1', '#B1', '#B10', '"1"', "'1'", "'125'",
    '1,2', '-1,2', '1,', '1 2', '1e', '1x',
])
def test_real_parser_rejects_without_ram_mutation(parser_host, text):
    result = subprocess.run([str(parser_host), text, 'reject', '0'],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_real_parser_stop_rejection_preserves_ram(parser_host):
    result = subprocess.run([str(parser_host), '-125', 'stopped-reject', '0'],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scpi/scpi.h"
#undef assert
#define assert(condition) do { if (!(condition)) { fprintf(stderr, "CHECK FAILED line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static int32_t requested = 321;
static unsigned manager_calls;
static bool manager_allowed = true;
static char output[2048];
static size_t output_size;
static size_t write_output(scpi_t *context, const char *data, size_t length)
{
    (void)context;
    assert(output_size + length < sizeof(output));
    memcpy(output + output_size, data, length);
    output_size += length; output[output_size] = 0;
    return length;
}
static scpi_result_t flush_output(scpi_t *context)
{ (void)context; return SCPI_RES_OK; }
static int record_error(scpi_t *context, int_fast16_t error)
{ (void)context; (void)error; return 0; }
bool vdc_dpll_manager_set_output_delay_ns(int32_t value)
{
    ++manager_calls;
    if (!manager_allowed) return false;
    requested = value;
    return true;
}
static void scpi_port_push_exec_error(scpi_t *context, const char *message)
{ (void)message; SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); }
'''

MAIN = r'''
static const scpi_command_t commands[] = {
    { .pattern = "SYSTem:VDC:OUTPut:DELay", .callback = scpi_cmd_vdc_output_delay },
    SCPI_CMD_LIST_END
};
int main(int argc, char **argv)
{
    assert(argc == 4);
    const bool accepted = strcmp(argv[2], "accept") == 0;
    const bool stopped_reject = strcmp(argv[2], "stopped-reject") == 0;
    manager_allowed = !stopped_reject;
    char input[1024], command[1024];
    scpi_t context;
    scpi_error_t errors[16];
    scpi_interface_t interface = { .write=write_output, .flush=flush_output, .error=record_error };
    SCPI_Init(&context, commands, &interface, scpi_units_def,
              "vendor", "model", "serial", "version", input, sizeof(input), errors, 16);
    int length = snprintf(command, sizeof(command), "SYSTem:VDC:OUTPut:DELay %s\n", argv[1]);
    assert(length > 0 && (size_t)length < sizeof(command));
    const scpi_bool_t parsed = SCPI_Input(&context, command, (int)length);
    const int32_t error_count = SCPI_ErrorCount(&context);
    fprintf(stderr, "accepted=%u parsed=%u manager_calls=%u requested=%ld errors=%ld output=%s\n",
            (unsigned)accepted, (unsigned)parsed, manager_calls, (long)requested, (long)error_count, output);
    if (accepted) {
        const int32_t expected = (int32_t)strtoll(argv[3], NULL, 10);
        char response[64];snprintf(response, sizeof(response), "%ld", (long)expected);
        assert(parsed && manager_calls == 1u && requested == expected && error_count == 0);
        /* libscpi action responses may have no parser-added line ending. */
        output[strcspn(output, "\r\n")] = 0;
        assert(!strcmp(output, response));
    } else {
        assert(manager_calls == (stopped_reject ? 1u : 0u));
        assert(requested == 321 && error_count > 0 && output_size == 0u);
    }
    return 0;
}
'''
