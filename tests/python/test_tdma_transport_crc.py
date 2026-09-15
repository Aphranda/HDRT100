"""Check the production CRC kernel against polynomial and zlib oracles."""
from pathlib import Path
import ctypes
import os
import random
import shutil
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]


def test_transport_crc_all_byte_values_seeds_lengths_and_chunks(tmp_path):
    # Include the actual source to exercise arbitrary incremental kernel seeds;
    # encode/decode/mutation behavior is covered by the transport C suite.
    unit = tmp_path / "crc.c"
    unit.write_text('#include "tdma_transport_frame.c"\n'
                    'uint32_t test_update(uint32_t seed, const uint8_t *p, size_t n) '
                    '{ return tdma_transport_crc32_update(seed, p, n); }\n', encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "Host C compiler required"
    library = tmp_path / ("crc.dll" if os.name == "nt" else "crc.so")
    command = [compiler, "-std=c11", "-O3", "-Wall", "-Wextra", "-Werror", "-shared",
               "-I" + str(ROOT / "components/tdma/inc"),
               "-I" + str(ROOT / "components/tdma/src"), str(unit), "-o", str(library)]
    if os.name != "nt":
        command.insert(1, "-fPIC")
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (tmp_path / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stderr
    dll = ctypes.CDLL(str(library))
    update = dll.test_update
    update.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]
    update.restype = ctypes.c_uint32
    compute = dll.tdma_transport_crc32_compute
    compute.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    compute.restype = ctypes.c_uint32
    assert compute(None, 0) == 0 and compute(None, 1) == 0
    assert compute(b"123456789", 9) == 0xCBF43926
    for seed in [0, 0xFFFFFFFF, *(1 << bit for bit in range(32))]:
        assert update(seed, None, 0) == seed
        for byte in range(256):
            expected = seed ^ byte
            for _ in range(8):
                expected = (expected >> 1) ^ (0xEDB88320 if expected & 1 else 0)
            assert update(seed, bytes([byte]), 1) == expected
    rng = random.Random(0x54444D41)
    lengths = [*range(513), 1023, 1024, 1025, 4095, 4096, 4097, 8191, 8192, 8193,
               *(rng.randrange(0, 16385) for _ in range(128))]
    for size in lengths:
        data = rng.randbytes(size)
        storage = ctypes.create_string_buffer(b"pad" + data + b"tail")
        address = ctypes.addressof(storage) + 3  # Exercise unaligned byte input.
        assert compute(address, size) == zlib.crc32(data)
        seed = rng.getrandbits(32)
        expected = zlib.crc32(data, seed ^ 0xFFFFFFFF) ^ 0xFFFFFFFF
        assert update(seed, address, size) == expected
        split = rng.randrange(size + 1)
        prefix = update(seed, address, split)
        assert update(prefix, address + split, size - split) == expected
