"""Check the production locator against unbounded integer frame geometry."""
import ctypes as C
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]
U32, U64 = (1 << 32) - 1, (1 << 64) - 1


class Hint(C.Structure):
    _fields_ = [("valid", C.c_bool), ("bit_shift", C.c_uint32),
                ("frame_words", C.c_uint32), ("stride_words", C.c_uint32),
                ("stable_frames", C.c_uint32), ("candidate", C.c_uint64),
                ("observation_epoch", C.c_uint64)]


@pytest.fixture(scope="module")
def locator(tmp_path_factory):
    out = tmp_path_factory.mktemp("rx-cursor")
    source = (ROOT/"components/tdma/src/tdma_rx_scan.c").read_text(encoding="utf-8")
    body = c_definition_body(source, "tdma_rx_scan_locate")
    code = '#include "tdma_rx_scan.h"\n'
    code += "bool tdma_rx_scan_locate(const tdma_rx_scan_hint_t *hint, uint64_t produced, uint64_t cursor, uint64_t *candidate) {" + body + "}\n"
    code += "unsigned minimum(void) { return TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE; }\n"
    code += "unsigned maximum(void) { return TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX; }\n"
    code += "unsigned tail_limit(void) { return TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES; }\n"
    code += "unsigned hint_size(void) { return sizeof(tdma_rx_scan_hint_t); }\n"
    path = out/"locator.c"
    path.write_text(code, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    library = out/("locator.dll" if os.name == "nt" else "locator.so")
    result = subprocess.run([gcc,"-std=c11","-O2","-Wall","-Wextra","-Werror","-shared","-fPIC",
        "-I"+str(ROOT/"components/tdma/inc"),str(path),"-o",str(library)], capture_output=True,text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    api = C.CDLL(str(library))
    api.tdma_rx_scan_locate.argtypes = [C.POINTER(Hint),C.c_uint64,C.c_uint64,C.POINTER(C.c_uint64)]
    api.tdma_rx_scan_locate.restype = C.c_bool
    assert api.hint_size() == C.sizeof(Hint)
    return api


def check(api, anchor, cursor, produced, frame, tail, shift, valid=True):
    hint = Hint(valid,shift,frame,frame+tail,2,anchor,19)
    before = bytes(hint)
    output = C.c_uint64(0xA55A11223344)
    geometry = (valid and 0 <= shift <= 7 and api.minimum() <= frame <= api.maximum()
                and 0 <= tail <= api.tail_limit() and produced >= anchor)
    expected = None
    if geometry:
        # Independent ceiling equation, without machine-width wrapping.
        n = max(0, -((anchor-cursor)//(frame+tail)))
        location = anchor + n*(frame+tail)
        if location + frame + bool(shift) <= produced:
            expected = location
    actual = api.tdma_rx_scan_locate(C.byref(hint),produced,cursor,C.byref(output))
    assert actual == (expected is not None), (anchor,cursor,produced,frame,tail,shift,expected)
    assert output.value == (expected if expected is not None else 0xA55A11223344)
    assert bytes(hint) == before


def test_alignment_and_completion_boundaries(locator):
    for frame in (locator.minimum(),locator.maximum()):
        for tail in range(locator.tail_limit()+1):
            stride = frame+tail
            for shift in range(8):
                for anchor in (0,1,U32-11,U32+1,U64-4096):
                    for delta in (-1,0,1,stride-1,stride,stride+1,3*stride-1):
                        cursor = max(0,anchor+delta)
                        n = max(0,-((anchor-cursor)//stride))
                        end = anchor+n*stride+frame+bool(shift)
                        for produced in (max(0,anchor-1),end-1,end,end+1):
                            check(locator,anchor,cursor,produced,frame,tail,shift)


def test_large_distance_and_unsigned_overflow(locator):
    for frame in (locator.minimum(),locator.maximum()):
        for tail in (0,locator.tail_limit()):
            for shift in (0,7):
                for anchor in (0,1,U32,U64-1024,U64-1,U64):
                    for distance in (0,1,U32-1,U32,U32+1,U32+2,1 << 48,U64-anchor):
                        cursor = anchor+distance
                        if cursor <= U64:
                            for produced in (anchor,cursor,U64):
                                check(locator,anchor,cursor,produced,frame,tail,shift)


def test_random_absolute_counters(locator):
    rng = random.Random(0x54444D41)
    for _ in range(12000):
        frame = rng.randint(locator.minimum(),locator.maximum())
        tail = rng.randint(0,locator.tail_limit())
        anchor, cursor, produced = (rng.getrandbits(64) for _ in range(3))
        check(locator,anchor,cursor,produced,frame,tail,rng.randrange(8))


def test_invalid_hints_and_null_arguments(locator):
    for frame,tail,shift,valid in [(0,0,0,True),(locator.minimum()-1,0,0,True),
        (locator.maximum()+1,0,0,True),(locator.minimum(),-1,0,True),
        (locator.minimum(),locator.tail_limit()+1,0,True),(locator.minimum(),0,8,True),
        (locator.minimum(),0,0,False)]:
        check(locator,0,0,U64,frame,tail,shift,valid)
    output = C.c_uint64(37)
    assert not locator.tdma_rx_scan_locate(None,U64,0,C.byref(output))
    assert output.value == 37
    hint = Hint(True,0,locator.minimum(),locator.minimum(),2,0,1)
    assert not locator.tdma_rx_scan_locate(C.byref(hint),U64,0,None)
