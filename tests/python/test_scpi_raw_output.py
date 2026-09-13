"""Execute the production SCPI writer against raw/capture/custom transports."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


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
typedef struct { int unused; } scpi_t;
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
    unit.write_text(prefix + "\nstatic size_t scpi_port_write(scpi_t *context, const char *data, size_t len) {" +
                    body + "}\n" + suffix, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / "writer.exe"
    subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(unit), "-o", str(executable)], check=True, timeout=60)
    return executable


@pytest.mark.parametrize("mode", range(4), ids=["raw_binary_fragments", "empty", "capture_priority", "custom_partial_result"])
def test_scpi_writer_transport_bytes_and_routing(writer, mode):
    subprocess.run([str(writer), str(mode)], check=True, timeout=10)
