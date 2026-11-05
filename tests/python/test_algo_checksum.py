"""Pin the shared checksum primitives against independent oracles.

The header-only kernels are compiled into a host shared library and compared
with zlib / pure-Python references over golden vectors, every byte value,
arbitrary chunking and unaligned buffers.  The same values are the migration
baseline for call sites that must stay byte-identical.
"""
from pathlib import Path
import ctypes
import os
import shutil
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]

SHIM = r'''
#include "algo_checksum.h"

uint32_t test_crc32_update(uint32_t c, const uint8_t *p, size_t n)
{ return algo_crc32_iso_hdlc_update(c, p, n); }
uint32_t test_crc32_compute(const uint8_t *p, size_t n)
{ return algo_crc32_iso_hdlc_compute(p, n); }
uint32_t test_crc32_raw(uint32_t r, const uint8_t *p, size_t n)
{ return algo_crc32_raw_update(r, p, n); }
uint16_t test_ccitt_update(uint16_t c, const uint8_t *p, size_t n)
{ return algo_crc16_ccitt_false_update(c, p, n); }
uint16_t test_ccitt_compute(const uint8_t *p, size_t n)
{ return algo_crc16_ccitt_false_compute(p, n); }
uint32_t test_fnv_update(uint32_t h, const uint8_t *p, size_t n)
{ return algo_fnv1a32_update(h, p, n); }
uint32_t test_fnv_bytes(const uint8_t *p, size_t n)
{ return algo_fnv1a32_bytes(p, n); }
uint32_t test_fnv_str(const char *s)
{ return algo_fnv1a32_str(s); }
uint32_t test_fnv_u32(uint32_t h, uint32_t v)
{ return algo_fnv1a32_u32_le(h, v); }
uint32_t test_fnv_word(uint32_t h, uint32_t v)
{ return algo_fnv1a32_word(h, v); }
'''


def _load(tmp_path: Path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "Host C compiler required"
    shim = tmp_path / "algo_shim.c"
    shim.write_text(SHIM, encoding="utf-8")
    library = tmp_path / ("algo.dll" if os.name == "nt" else "algo.so")
    command = [compiler, "-std=c11", "-O3", "-Wall", "-Wextra", "-Wshadow",
               "-Werror", "-shared",
               "-I" + str(ROOT / "components/algo/inc"),
               str(shim), "-o", str(library)]
    if os.name != "nt":
        command.insert(1, "-fPIC")
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (tmp_path / "compile.log").write_text(result.stdout + result.stderr,
                                          encoding="utf-8")
    assert result.returncode == 0, result.stderr
    dll = ctypes.CDLL(str(library))

    def bind(name, restype, argtypes):
        function = getattr(dll, name)
        function.argtypes = argtypes
        function.restype = restype
        return function

    return {
        "crc32_update": bind("test_crc32_update", ctypes.c_uint32,
                             [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]),
        "crc32_compute": bind("test_crc32_compute", ctypes.c_uint32,
                              [ctypes.c_void_p, ctypes.c_size_t]),
        "crc32_raw": bind("test_crc32_raw", ctypes.c_uint32,
                          [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]),
        "ccitt_update": bind("test_ccitt_update", ctypes.c_uint16,
                             [ctypes.c_uint16, ctypes.c_void_p, ctypes.c_size_t]),
        "ccitt_compute": bind("test_ccitt_compute", ctypes.c_uint16,
                              [ctypes.c_void_p, ctypes.c_size_t]),
        "fnv_update": bind("test_fnv_update", ctypes.c_uint32,
                           [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]),
        "fnv_bytes": bind("test_fnv_bytes", ctypes.c_uint32,
                          [ctypes.c_void_p, ctypes.c_size_t]),
        "fnv_str": bind("test_fnv_str", ctypes.c_uint32, [ctypes.c_char_p]),
        "fnv_u32_le": bind("test_fnv_u32", ctypes.c_uint32,
                           [ctypes.c_uint32, ctypes.c_uint32]),
        "fnv_word": bind("test_fnv_word", ctypes.c_uint32,
                         [ctypes.c_uint32, ctypes.c_uint32]),
    }


def _crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _fnv1a32(data: bytes) -> int:
    digest = 2166136261
    for byte in data:
        digest ^= byte
        digest = (digest * 16777619) & 0xFFFFFFFF
    return digest


def test_golden_vectors(tmp_path):
    api = _load(tmp_path)
    empty = ctypes.create_string_buffer(b"", 1)
    assert api["crc32_compute"](b"123456789", 9) == 0xCBF43926
    assert api["crc32_compute"](None, 0) == 0
    assert api["ccitt_compute"](b"123456789", 9) == 0x29B1
    assert api["ccitt_compute"](None, 0) == 0xFFFF
    assert api["fnv_bytes"](b"123456789", 9) == 0xBB86B11C
    assert api["fnv_bytes"](empty, 0) == 2166136261
    assert api["fnv_str"](b"123456789") == 0xBB86B11C
    assert api["fnv_str"](None) == 2166136261


def test_crc32_matches_zlib_for_every_byte_and_chunking(tmp_path):
    api = _load(tmp_path)
    update = api["crc32_update"]
    compute = api["crc32_compute"]
    for byte in range(256):
        assert compute(bytes([byte]), 1) == zlib.crc32(bytes([byte]))
    data = bytes(range(256)) * 17
    buffer = ctypes.create_string_buffer(data)
    address = ctypes.addressof(buffer)
    assert update(0, address, len(data)) == zlib.crc32(data)
    for split in (0, 1, 2, 255, 256, 257, len(data) - 1, len(data)):
        stream = update(0, address, split)
        stream = update(stream, address + split, len(data) - split)
        assert stream == zlib.crc32(data)


def test_crc32_raw_relation_and_unaligned_input(tmp_path):
    api = _load(tmp_path)
    data = b"\x00\x01\x02\x03\xfe\xff" * 37
    buffer = ctypes.create_string_buffer(b"pad" + data + b"tail")
    address = ctypes.addressof(buffer) + 3
    assert api["crc32_compute"](address, len(data)) == zlib.crc32(data)
    assert api["crc32_raw"](0xFFFFFFFF, address, len(data)) == (~zlib.crc32(data)) & 0xFFFFFFFF
    # The persisted raw variant starts its register at zero.
    assert api["crc32_raw"](0, address, len(data)) == \
        (~api["crc32_update"](0xFFFFFFFF, address, len(data))) & 0xFFFFFFFF


def test_ccitt_false_matches_python_oracle(tmp_path):
    api = _load(tmp_path)
    data = bytes(range(256)) * 9
    assert api["ccitt_compute"](data, len(data)) == _crc16_ccitt_false(data)
    stream = api["ccitt_update"](0xFFFF, data, 7)
    rest = data[7:]
    assert api["ccitt_update"](stream, rest, len(rest)) == _crc16_ccitt_false(data)


def test_fnv1a_matches_python_oracle(tmp_path):
    api = _load(tmp_path)
    assert api["fnv_bytes"](b"", 0) == 2166136261
    data = bytes(range(256)) * 3
    assert api["fnv_bytes"](data, len(data)) == _fnv1a32(data)
    # u32_le must be exactly the four little-endian bytes of the value.
    stream = api["fnv_update"](2166136261, b"abc", 3)
    for value in (0, 1, 0x12345678, 0xFFFFFFFF):
        assert api["fnv_u32_le"](stream, value) == \
            _fnv1a32(b"abc" + value.to_bytes(4, "little"))


def test_crc32_raw_zero_seed_golden_and_fnv_word(tmp_path):
    api = _load(tmp_path)
    # Persisted claim/frame payloads start the raw register at zero.
    assert api["crc32_raw"](0, b"123456789", 9) == 0x2DFD2D88
    # Word fold consumes one 32-bit symbol per step (not four bytes).
    first = api["fnv_word"](2166136261, 0x12345678)
    assert first == ((2166136261 ^ 0x12345678) * 16777619) & 0xFFFFFFFF
    second = api["fnv_word"](first, 2)
    assert second == ((first ^ 2) * 16777619) & 0xFFFFFFFF
