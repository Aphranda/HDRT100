"""Execute the production SCPI writer against raw/capture/custom transports."""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def batch_declarations(source):
    return re.search(
        r"#define SCPI_PORT_STDIO_BATCH_BYTES[^\n]+\n\n"
        r"typedef struct \{.*?\} scpi_stdio_batch_t;", source, re.S).group(0)


def c_functions(source, signatures):
    definitions = []
    for name, signature in signatures:
        # Start at the definition, since the source may declare it earlier.
        definition = re.search(r"\b" + name + r"\s*\([^;{}]*\)\s*\{", source)
        start = source.rfind("\n", 0, definition.start()) + 1
        definitions.append(signature + " {" +
                           c_definition_body(source[start:], name) + "}\n")
    return "\n".join(definitions)


@pytest.fixture(scope="module")
def writer(tmp_path_factory):
    directory = tmp_path_factory.mktemp("scpi-raw-output")
    source = (ROOT / "middleware/scpi_port/src/scpi_port.c").read_text(encoding="utf-8")
    body = c_definition_body(source, "scpi_port_write")
    prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
typedef struct { void *user_context; } scpi_t;
static char *s_scpi_capture_buffer;
static size_t s_scpi_capture_capacity, s_scpi_capture_len;
static bool s_scpi_capture_truncated;
static size_t (*s_scpi_stream_write)(const char *, size_t, void *);
static void *s_scpi_stream_context;
static unsigned char output[8192];
static size_t output_size, calls, stream_calls;
static int stdio_put_string(const char *data, int length, bool newline, bool cr_translation) {
    assert(length > 0 && !newline && !cr_translation);
    assert(output_size + (size_t)length <= sizeof(output));
    memcpy(output + output_size, data, (size_t)length);
    output_size += (size_t)length;
    ++calls;
    return length;
}
static size_t custom_write(const char *data, size_t length, void *context) {
    assert(context == output && data != NULL && length == 8);
    ++stream_calls;
    return 3; /* Propagate the custom transport's partial result. */
}
'''
    suffix = r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    const int mode = atoi(argv[1]);
    unsigned char data[4096];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)i;
    if (mode == 0) {
        /* Includes NUL, CR/LF, high-bit bytes and no terminating NUL. */
        assert(scpi_port_write(NULL, (const char *)data, 13) == 13);
        assert(scpi_port_write(NULL, (const char *)data + 13, sizeof(data) - 13) == sizeof(data) - 13);
        assert(output_size == sizeof(data) && memcmp(data, output, sizeof(data)) == 0);
        assert(calls == 2); /* Driver entries follow fragments, not byte count. */
    } else if (mode == 1) {
        assert(scpi_port_write(NULL, NULL, 0) == 0);
        assert(output_size == 0 && calls == 0);
    } else if (mode == 2) {
        char capture[11];
        memset(capture, 0x55, sizeof(capture));
        s_scpi_capture_buffer = capture;
        s_scpi_capture_capacity = 7;
        s_scpi_stream_write = custom_write;
        assert(scpi_port_write(NULL, (const char *)data, 5) == 5);
        assert(!s_scpi_capture_truncated);
        assert(scpi_port_write(NULL, (const char *)data + 5, 8) == 8);
        assert(s_scpi_capture_truncated && s_scpi_capture_len == 7);
        assert(memcmp(capture, data, 7) == 0);
        for (size_t i = 7; i < sizeof(capture); ++i) assert(capture[i] == 0x55);
        assert(calls == 0 && stream_calls == 0);
    } else {
        assert(mode == 3);
        s_scpi_stream_write = custom_write;
        s_scpi_stream_context = output;
        assert(scpi_port_write(NULL, (const char *)data, 8) == 3);
        assert(stream_calls == 1 && calls == 0);
    }
    return 0;
}
'''
    unit = directory / "writer.c"
    helpers = c_functions(source, [
        ("scpi_port_write_raw", "static void scpi_port_write_raw(const char *data, size_t len)"),
        ("scpi_port_stdio_drain", "static void scpi_port_stdio_drain(scpi_t *context)"),
    ])
    unit.write_text(prefix + batch_declarations(source) + helpers +
                    "\nstatic size_t scpi_port_write(scpi_t *context, const char *data, size_t len) {" +
                    body + "}\n" + suffix, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / "writer.exe"
    subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(unit), "-o", str(executable)], check=True, timeout=60)
    return executable


@pytest.mark.parametrize("mode", range(4), ids=["raw_binary_fragments", "empty", "capture_priority", "custom_partial_result"])
def test_scpi_writer_transport_bytes_and_routing(writer, mode):
    subprocess.run([str(writer), str(mode)], check=True, timeout=10)


@pytest.fixture(scope="module")
def parser_writer(tmp_path_factory):
    directory = tmp_path_factory.mktemp("scpi-parser-output")
    source = (ROOT / "middleware/scpi_port/src/scpi_port.c").read_text(encoding="utf-8")
    prefix = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scpi/scpi.h"
#include "scpi_port.h"
static scpi_t s_scpi_context;
static char *s_scpi_capture_buffer;
static size_t s_scpi_capture_capacity, s_scpi_capture_len;
static bool s_scpi_capture_truncated;
static scpi_port_write_fn_t s_scpi_stream_write;
static scpi_port_flush_fn_t s_scpi_stream_flush;
static void *s_scpi_stream_context;
static unsigned char output[8192], custom_output[8192];
static size_t output_size, driver_calls, flush_calls, custom_size, custom_flushes, log_calls;
static int stdio_put_string(const char *data, int length, bool newline, bool cr_translation) {
    assert(length > 0 && !newline && !cr_translation);
    assert(output_size + (size_t)length <= sizeof(output));
    memcpy(output + output_size, data, (size_t)length);
    output_size += (size_t)length;
    ++driver_calls;
    return length;
}
static void stdio_flush(void) { ++flush_calls; }
#define LOG_WARN(tag, format, error) ((void)(tag), (void)(format), (void)(error), ++log_calls)
static void scpi_port_flush_output(void);
'''
    definitions = c_functions(source, [
        ("scpi_port_write_raw", "static void scpi_port_write_raw(const char *data, size_t len)"),
        ("scpi_port_stdio_drain", "static void scpi_port_stdio_drain(scpi_t *context)"),
        ("scpi_port_write", "static size_t scpi_port_write(scpi_t *context, const char *data, size_t len)"),
        ("scpi_port_flush_output", "static void scpi_port_flush_output(void)"),
        ("scpi_port_flush", "static scpi_result_t scpi_port_flush(scpi_t *context)"),
        ("scpi_port_flush_now", "void scpi_port_flush_now(void)"),
        ("scpi_port_error", "static int scpi_port_error(scpi_t *context, int_fast16_t error)"),
        ("scpi_port_input", "static void scpi_port_input(const char *data, int len)"),
        ("scpi_port_feed", "void scpi_port_feed(const char *data, size_t len)"),
        ("scpi_port_set_stream", "void scpi_port_set_stream(scpi_port_write_fn_t write_fn, scpi_port_flush_fn_t flush_fn, void *context)"),
        ("scpi_port_execute", "bool scpi_port_execute(const char *data, size_t len, char *response, size_t response_capacity, size_t *response_len)"),
    ])
    suffix = r'''
static size_t custom_write(const char *data, size_t length, void *context) {
    assert(context == custom_output && custom_size + length <= sizeof(custom_output));
    memcpy(custom_output + custom_size, data, length);
    custom_size += length;
    return length;
}
static void custom_flush(void *context) {
    assert(context == custom_output);
    ++custom_flushes;
}
static scpi_result_t value_query(scpi_t *context) {
    for (uint32_t i = 0; i < 80; ++i) SCPI_ResultUInt32(context, i);
    return SCPI_RES_OK;
}
static scpi_result_t binary_query(scpi_t *context) {
    unsigned char bytes[4096];
    for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = (unsigned char)i;
    SCPI_ResultArbitraryBlock(context, bytes, sizeof(bytes));
    return SCPI_RES_OK;
}
static scpi_result_t flush_query(scpi_t *context) {
    SCPI_ResultText(context, "OK");
    scpi_port_flush_now();
    /* A reset callback must see the acknowledgment delivered before reset. */
    assert(output_size == 4 && memcmp(output, "\"OK\"", 4) == 0);
    assert(flush_calls == 1);
    SCPI_ResultUInt32(context, 7);
    return SCPI_RES_OK;
}
static scpi_result_t route_query(scpi_t *context) {
    scpi_port_write(context, "old", 3);
    scpi_port_set_stream(custom_write, custom_flush, custom_output);
    assert(output_size == 3 && memcmp(output, "old", 3) == 0);
    scpi_port_write(context, "new", 3);
    scpi_port_set_stream(NULL, NULL, NULL);
    scpi_port_write(context, "end", 3);
    /* Direct output has no parser newline, so input-return must drain it. */
    return SCPI_RES_OK;
}
static scpi_result_t error_query(scpi_t *context) {
    scpi_port_write(context, "prefix", 6);
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    assert(output_size == 6 && log_calls > 0);
    return SCPI_RES_OK;
}
static const scpi_command_t commands[] = {
    {.pattern="VALUE?", .callback=value_query},
    {.pattern="BINARY?", .callback=binary_query},
    {.pattern="FLUSH?", .callback=flush_query},
    {.pattern="ROUTE", .callback=route_query},
    {.pattern="ERROR?", .callback=error_query},
    SCPI_CMD_LIST_END
};
int main(int argc, char **argv) {
    assert(argc == 2);
    const int mode = atoi(argv[1]);
    char input[128], expected[512];
    scpi_error_t errors[16];
    scpi_interface_t interface = {.write=scpi_port_write, .flush=scpi_port_flush, .error=scpi_port_error};
    SCPI_Init(&s_scpi_context, commands, &interface, scpi_units_def,
              "vendor", "model", "serial", "version", input, sizeof(input), errors, 16);
    size_t expected_size = 0;
    for (unsigned i = 0; i < 80; ++i) {
        int n = snprintf(expected + expected_size, sizeof(expected) - expected_size,
                         i ? ",%u" : "%u", i);
        assert(n > 0);
        expected_size += (size_t)n;
    }
    memcpy(expected + expected_size, "\r\n", 2);
    expected_size += 2;
    if (mode == 0) {
        scpi_port_feed("VAL", 3);
        assert(output_size == 0 && s_scpi_context.user_context == NULL);
        scpi_port_feed("UE?\n", 4);
        assert(output_size == expected_size && memcmp(output, expected, expected_size) == 0);
        assert(driver_calls > 1 && driver_calls < 8);
        scpi_port_feed("VALUE?\n", 7);
        assert(output_size == 2 * expected_size && memcmp(output + expected_size, expected, expected_size) == 0);
    } else if (mode == 1) {
        scpi_port_feed("BINARY?\n", 8);
        assert(output_size == 6 + 4096 + 2);
        assert(memcmp(output, "#44096", 6) == 0);
        for (size_t i = 0; i < 4096; ++i) assert(output[6+i] == (unsigned char)i);
        assert(memcmp(output + 6 + 4096, "\r\n", 2) == 0);
        assert(driver_calls < 80);
    } else if (mode == 2) {
        scpi_port_feed("FLUSH?\n", 7);
        assert(output_size == 8 && memcmp(output, "\"OK\",7\r\n", 8) == 0);
        assert(flush_calls == 2);
    } else if (mode == 3) {
        char capture[11];
        memset(capture, 0x55, sizeof(capture));
        scpi_port_set_stream(custom_write, custom_flush, custom_output);
        size_t captured = 0;
        assert(!scpi_port_execute("VALUE?\n", 7, capture, 7, &captured));
        assert(captured == 7 && memcmp(capture, expected, 7) == 0);
        for (size_t i = 7; i < sizeof(capture); ++i) assert(capture[i] == 0x55);
        assert(output_size == 0 && custom_size == 0);
        scpi_port_set_stream(NULL, NULL, NULL);
        scpi_port_feed("VALUE?\n", 7);
        assert(output_size == expected_size && memcmp(output, expected, expected_size) == 0);
    } else if (mode == 4) {
        scpi_port_set_stream(custom_write, custom_flush, custom_output);
        scpi_port_feed("VALUE?\n", 7);
        assert(output_size == 0 && custom_size == expected_size);
        assert(memcmp(custom_output, expected, expected_size) == 0 && custom_flushes == 1);
    } else if (mode == 5) {
        scpi_port_feed("ROUTE\n", 6);
        assert(output_size == 6 && memcmp(output, "oldend", 6) == 0);
        assert(custom_size == 3 && memcmp(custom_output, "new", 3) == 0);
    } else if (mode == 6) {
        char overflow[256];
        memset(overflow, 'X', sizeof(overflow));
        scpi_port_feed(overflow, sizeof(overflow));
        assert(log_calls > 0 && s_scpi_context.user_context == NULL);
        scpi_port_feed("ERROR?\n", 7);
        assert(output_size == 6 && memcmp(output, "prefix", 6) == 0);
    } else {
        assert(mode == 7);
        scpi_stdio_batch_t previous = {.count=0};
        s_scpi_context.user_context = &previous;
        scpi_port_feed("VALUE?\n", 7);
        assert(s_scpi_context.user_context == &previous && previous.count == 0);
        assert(output_size == expected_size && memcmp(output, expected, expected_size) == 0);
        s_scpi_context.user_context = NULL;
    }
    assert(s_scpi_context.user_context == NULL);
    return 0;
}
'''
    unit = directory / "parser_writer.c"
    unit.write_text(prefix + batch_declarations(source) + definitions + suffix, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / "parser_writer.exe"
    library = ROOT / "third_party/scpi-parser/libscpi"
    # The vendored parser uses ELF visibility attributes that MinGW ignores.
    # Keep the warning visible without making that platform mismatch fatal.
    platform_flags = ["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]
    subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-DSCPI_USER_CONFIG=1",
                    "-I" + str(library / "inc"), "-I" + str(ROOT / "middleware/scpi_port/inc"),
                    str(unit), *[str(p) for p in sorted((library / "src").glob("*.c"))],
                    *platform_flags, "-o", str(executable)], check=True, timeout=60)
    return executable


@pytest.mark.parametrize("mode", range(8), ids=[
    "split_and_repeated_input", "long_binary_response", "explicit_flush", "capture_then_default",
    "custom_stream", "route_change_and_return_drain", "error_return", "restore_context",
])
def test_real_parser_output_scope_and_routes(parser_writer, mode):
    subprocess.run([str(parser_writer), str(mode)], check=True, timeout=10)
