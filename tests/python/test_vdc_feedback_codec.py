"""Compile the production raw-feedback codec and exercise protocol boundaries."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def codec_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-feedback-codec")
    # Independent wire oracle: Python standard-library pack and IEEE CRC.
    body = struct.pack("<BBBBIIQIIIQQQI", 1, 1, 0, 7, 0x11223344, 0x55667788,
                       0x0102030405060708, 0x10203040, 0x90A0B0C0, 125000000,
                       0x0123456789ABCDEF, 0x123456789ABCDEF0,
                       0x2233445566778899, 0x10203040)
    assert len(body) == 60
    wire = body + struct.pack("<I", zlib.crc32(body))
    source = directory / "codec_test.c"
    source.write_text(HARNESS.replace("WIRE_ORACLE", ",".join(str(x) for x in wire)),
                      encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = directory / ("codec_test.exe" if os.name == "nt" else "codec_test")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/distributed_refmem/inc"), str(source),
               str(ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"),
               "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("scenario", ["wire", "validation", "interleaved", "restart",
                                    "conflict", "expiry", "complete", "order"])
def test_production_feedback_codec(codec_executable, scenario):
    result = subprocess.run([str(codec_executable), scenario], capture_output=True,
                            text=True, timeout=10)
    (codec_executable.parent / (scenario + ".log")).write_text(
        result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "feedback codec: passed" in result.stdout


HARNESS = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "refmem_sync_vdc_feedback.h"

static const uint8_t oracle[64] = {WIRE_ORACLE};
typedef refmem_sync_vdc_feedback_assembly_t assembly_t;
typedef refmem_sync_vdc_feedback_record_t record_t;
#define ACTIVE(a) (((a).state & REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE) != 0u)

static record_t specimen(uint8_t source)
{
    return (record_t){
        .source_arm_epoch = UINT64_C(0x0102030405060708),
        .rx_elapsed_cycles = UINT64_C(0x0123456789abcdef),
        .tx_elapsed_cycles = UINT64_C(0x123456789abcdef0),
        .timer1_enable_before = UINT64_C(0x2233445566778899),
        .timer1_enable_after = UINT64_C(0x223344557697b8d9),
        .source_clock_epoch_id = 0x11223344u, .source_clock_run_id = 0x55667788u,
        .observer_epoch = 0x10203040u, .measurement_sequence = 0x90a0b0c0u,
        .tick_hz = 125000000u, .schema_version = 1u, .source_slot = source,
        .target_slot = 0u, .domain_flags = 7u,
    };
}

static void wire_for(uint8_t source, uint8_t *wire)
{
    record_t r = specimen(source);
    assert(refmem_sync_vdc_feedback_encode(&r, 6u, wire));
}

static void fix_crc(uint8_t *wire)
{
    uint32_t crc = refmem_sync_vdc_feedback_crc32(wire, 60u);
    for (unsigned i = 0; i < 4; ++i) wire[60+i] = (uint8_t)(crc >> (8u*i));
}

static uint32_t seq_at(uint32_t first, unsigned index)
{
    while (index--) first = first == UINT32_MAX ? 1u : first + 1u;
    return first;
}

static refmem_sync_vdc_feedback_result_t push(assembly_t *a, uint8_t source,
    uint32_t first, unsigned index, const uint8_t *wire, uint32_t now, uint8_t *out)
{
    return refmem_sync_vdc_feedback_push(a, source, 0u, 6u, seq_at(first, index),
        (uint8_t)index, 16u, wire + index*4u, now, out);
}

static void expect_decode_false(const uint8_t *wire, uint32_t nodes, uint32_t src, uint32_t target)
{
    record_t out, before;
    memset(&out, 0xa5, sizeof(out)); before = out;
    assert(!refmem_sync_vdc_feedback_decode(wire, nodes, src, target, &out));
    assert(memcmp(&out, &before, sizeof(out)) == 0);
}

static void test_wire(void)
{
    assert(sizeof(assembly_t) == 80 && sizeof(record_t) == 64);
    assert(refmem_sync_vdc_feedback_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926u);
    assert(refmem_sync_vdc_feedback_crc32(NULL, 0) == 0);
    assert(refmem_sync_vdc_feedback_crc32(NULL, 1) == 0);
    uint8_t wire[64]; wire_for(1, wire);
    assert(memcmp(wire, oracle, 64) == 0);
    record_t r, expected = specimen(1);
    assert(refmem_sync_vdc_feedback_decode(wire, 6, 1, 0, &r));
    assert(memcmp(&r, &expected, sizeof(r)) == 0);
    for (unsigned byte = 0; byte < 64; ++byte) {
        uint8_t damaged[64]; memcpy(damaged, wire, 64); damaged[byte] ^= 1;
        expect_decode_false(damaged, 6, 1, 0);
    }
    assert(refmem_sync_vdc_feedback_next_sequence(0) == 1);
    assert(refmem_sync_vdc_feedback_next_sequence(UINT32_MAX) == 1);
    assert(refmem_sync_vdc_feedback_next_sequence(65535) == 65536);
}

static void test_validation(void)
{
    record_t r = specimen(1); uint8_t wire[64], saved[64];
    memset(wire, 0xa5, 64); memcpy(saved, wire, 64);
    r.timer1_enable_after = r.timer1_enable_before - 1u;
    assert(!refmem_sync_vdc_feedback_encode(&r, 6, wire));
    assert(memcmp(wire, saved, 64) == 0);
    r.timer1_enable_after = r.timer1_enable_before + UINT64_C(0x100000000);
    assert(!refmem_sync_vdc_feedback_encode(&r, 6, wire));
    r.timer1_enable_after = r.timer1_enable_before + UINT32_MAX;
    assert(refmem_sync_vdc_feedback_encode(&r, 6, wire));
    record_t decoded; assert(refmem_sync_vdc_feedback_decode(wire, 6, 1, 0, &decoded));
    assert(decoded.timer1_enable_after == r.timer1_enable_after);
    r = specimen(1); r.timer1_enable_after = r.timer1_enable_before;
    r.source_clock_epoch_id = r.source_clock_run_id = r.measurement_sequence = 0;
    r.tick_hz = 123u; assert(refmem_sync_vdc_feedback_encode(&r, 6, wire));
    assert(refmem_sync_vdc_feedback_decode(wire, 6, 1, 0, &decoded));
    r.tick_hz = 0; assert(!refmem_sync_vdc_feedback_encode(&r, 6, wire));
    r = specimen(1); r.source_arm_epoch = 0;
    assert(!refmem_sync_vdc_feedback_encode(&r, 6, wire));
    r = specimen(1); r.observer_epoch = 0;
    assert(!refmem_sync_vdc_feedback_encode(&r, 6, wire));
    r = specimen(1); r.schema_version = 2;
    assert(!refmem_sync_vdc_feedback_encode(&r, 6, wire));
    for (unsigned flags = 0; flags < 256; ++flags) {
        r = specimen(1); r.domain_flags = (uint8_t)flags;
        assert(refmem_sync_vdc_feedback_encode(&r, 6, wire) == (flags == 7));
    }
    wire_for(1, wire);
    expect_decode_false(wire, 1, 1, 0); expect_decode_false(wire, 9, 1, 0);
    expect_decode_false(wire, 6, 6, 0); expect_decode_false(wire, 6, 1, 1);
    expect_decode_false(wire, 6, 2, 0); expect_decode_false(wire, 6, 1, 2);
    expect_decode_false(NULL, 6, 1, 0);
    assert(!refmem_sync_vdc_feedback_decode(wire, 6, 1, 0, NULL));
    assert(!refmem_sync_vdc_feedback_encode(NULL, 6, wire));
    assert(!refmem_sync_vdc_feedback_encode(&r, 6, NULL));
    /* Valid CRC but overflowing reconstructed after tick is still rejected. */
    memset(wire + 48, 0xff, 8); wire[56] = 1; wire[57] = wire[58] = wire[59] = 0;
    fix_crc(wire); expect_decode_false(wire, 6, 1, 0);
    wire_for(1, wire); wire[3] = 0x87; fix_crc(wire); expect_decode_false(wire, 6, 1, 0);
}

static void test_interleaved(void)
{
    assembly_t a[3] = {0}; uint8_t wire[3][64], out[3][64], saved[64];
    const uint32_t starts[3] = {100u, UINT32_MAX-7u, 65530u};
    memset(out, 0xa5, sizeof(out)); memcpy(saved, out[0], 64);
    for (unsigned source=0; source<3; ++source) wire_for((uint8_t)(source+1), wire[source]);
    for (unsigned i=0; i<16; ++i) {
        for (unsigned source=0; source<3; ++source) {
            unsigned result = push(&a[source], (uint8_t)(source+1), starts[source], i,
                                   wire[source], 500u+i*20u, out[source]);
            assert(result == (i==15 ? REFMEM_VDC_FEEDBACK_COMPLETE : REFMEM_VDC_FEEDBACK_PROGRESS));
            assert(memcmp(out[source], i==15 ? wire[source] : saved, 64) == 0);
            assembly_t before = a[source];
            assert(push(&a[source], (uint8_t)(source+1), starts[source], i,
                        wire[source], 501u+i*20u, out[source]) == REFMEM_VDC_FEEDBACK_DUPLICATE);
            assert(memcmp(&a[source], &before, sizeof(before)) == 0);
        }
    }
    for (unsigned source=0; source<3; ++source) {
        assert(!ACTIVE(a[source]) && a[source].next_fragment == 16);
        assert(!refmem_sync_vdc_feedback_expire(&a[source], 5000));
    }
}

static void test_restart(void)
{
    assembly_t a = {0}; uint8_t wire[64], out[64]; wire_for(1, wire);
    assert(push(&a,1,100,0,wire,10,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,1,100,1,wire,20,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assembly_t saved = a;
    assert(push(&a,1,100,0,wire,30,out) == REFMEM_VDC_FEEDBACK_DUPLICATE);
    assert(memcmp(&a,&saved,sizeof(a)) == 0);
    assert(push(&a,1,90,0,wire,31,out) == REFMEM_VDC_FEEDBACK_STALE);
    assert(memcmp(&a,&saved,sizeof(a)) == 0);
    assert(push(&a,1,101u+0x80000000u,0,wire,32,out) == REFMEM_VDC_FEEDBACK_STALE);
    assert(memcmp(&a,&saved,sizeof(a)) == 0);
    assert(push(&a,1,200,0,wire,40,out) == REFMEM_VDC_FEEDBACK_RESTARTED);
    saved = a;
    assert(push(&a,1,100,0,wire,45,out) == REFMEM_VDC_FEEDBACK_STALE);
    assert(push(&a,1,100,1,wire,46,out) == REFMEM_VDC_FEEDBACK_STALE);
    assert(memcmp(&a,&saved,sizeof(a)) == 0);
    for (unsigned i=1; i<16; ++i)
        assert(push(&a,1,200,i,wire,50+i,out) ==
               (i==15 ? REFMEM_VDC_FEEDBACK_COMPLETE : REFMEM_VDC_FEEDBACK_PROGRESS));
    assert(memcmp(wire,out,64) == 0);
    refmem_sync_vdc_feedback_reset(&a);
    assert(a.state == 0 && a.last_sequence == 0);
    assert(push(&a,1,1,0,wire,100,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
}

static void test_conflict(void)
{
    assembly_t a={0}, other={0}; uint8_t wire[64], remote[64], out[64], bad[64];
    wire_for(1,wire); wire_for(2,remote); memcpy(bad,wire,64); bad[4]^=1;
    assert(push(&a,1,100,0,wire,0,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,1,100,1,wire,1,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&other,2,50,0,remote,0,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assembly_t saved_other=other;
    assert(push(&a,1,100,1,bad,2,out) == REFMEM_VDC_FEEDBACK_CONFLICT);
    assert(!ACTIVE(a) && memcmp(&other,&saved_other,sizeof(other)) == 0);
    assert(push(&a,1,100,0,wire,3,out) == REFMEM_VDC_FEEDBACK_STALE);
    assert(push(&a,1,200,0,wire,4,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,1,200,2,wire,5,out) == REFMEM_VDC_FEEDBACK_BAD_SEQUENCE);
    assert(!ACTIVE(a));
    assert(push(&a,1,300,0,wire,6,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,1,301,1,wire,7,out) == REFMEM_VDC_FEEDBACK_BAD_SEQUENCE);
    assert(!ACTIVE(a));
    refmem_sync_vdc_feedback_reset(&a);
    assert(refmem_sync_vdc_feedback_push(&a,6,0,6,10,0,16,wire,0,out) == REFMEM_VDC_FEEDBACK_BAD_IDENTITY);
    assert(refmem_sync_vdc_feedback_push(&a,1,0,6,0,0,16,wire,0,out) == REFMEM_VDC_FEEDBACK_BAD_SEQUENCE);
    assert(refmem_sync_vdc_feedback_push(&a,1,0,6,10,0,15,wire,0,out) == REFMEM_VDC_FEEDBACK_BAD_FRAGMENT);
    assert(refmem_sync_vdc_feedback_push(&a,1,0,6,10,16,16,wire,0,out) == REFMEM_VDC_FEEDBACK_BAD_FRAGMENT);
    assert(refmem_sync_vdc_feedback_push(&a,1,0,6,10,0,16,NULL,0,out) == REFMEM_VDC_FEEDBACK_BAD_ARGUMENT);
    assert(push(NULL,1,10,0,wire,0,out) == REFMEM_VDC_FEEDBACK_BAD_ARGUMENT);
    assert(push(&a,1,10,0,wire,0,NULL) == REFMEM_VDC_FEEDBACK_BAD_ARGUMENT);
    assert(push(&a,1,10,0,wire,0,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,2,20,0,remote,1,out) == REFMEM_VDC_FEEDBACK_BAD_IDENTITY);
    assert(!ACTIVE(a));
}

static void test_expiry(void)
{
    assembly_t a={0}; uint8_t wire[64],out[64]; wire_for(1,wire);
    assert(push(&a,1,100,0,wire,10,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,1,100,0,wire,1009,out) == REFMEM_VDC_FEEDBACK_DUPLICATE);
    assert(a.first_ms == 10 && ACTIVE(a));
    assert(refmem_sync_vdc_feedback_expire(&a,1010));
    assert(!ACTIVE(a) && !refmem_sync_vdc_feedback_expire(&a,1011));
    assert(push(&a,1,100,0,wire,1012,out) == REFMEM_VDC_FEEDBACK_STALE);
    assert(push(&a,1,200,0,wire,2000,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,1,300,0,wire,3000,out) == REFMEM_VDC_FEEDBACK_EXPIRED_RESTARTED);
    assert(a.first_ms == 3000 && a.first_sequence == 300 && ACTIVE(a));
    assert(push(&a,1,300,1,wire,4000,out) == REFMEM_VDC_FEEDBACK_EXPIRED);
    assert(!ACTIVE(a));
    refmem_sync_vdc_feedback_reset(&a);
    assert(push(&a,1,100,0,wire,UINT32_MAX-500u,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(!refmem_sync_vdc_feedback_expire(&a,498));
    assert(refmem_sync_vdc_feedback_expire(&a,499));
    assert(!refmem_sync_vdc_feedback_expire(NULL,0));
    refmem_sync_vdc_feedback_reset(&a);
    for (unsigned i=0;i<15;++i)
        assert(push(&a,1,100,i,wire,i*60,out) == REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,1,100,15,wire,1000,out) == REFMEM_VDC_FEEDBACK_EXPIRED);
}

static void test_complete(void)
{
    uint8_t wire[64],out[64],sentinel[64]; wire_for(1,wire);
    memset(sentinel,0xa5,64);
    for (unsigned mode=0; mode<3; ++mode) {
        assembly_t a={0}; uint8_t damaged[64]; memcpy(damaged,wire,64); memcpy(out,sentinel,64);
        if (mode==0) damaged[36]^=1; /* Mixed payload/CRC fails. */
        if (mode==1) { damaged[1]=2; fix_crc(damaged); } /* Whole source mismatch. */
        if (mode==2) { damaged[2]=3; fix_crc(damaged); } /* Whole target mismatch. */
        for (unsigned i=0;i<16;++i)
            assert(push(&a,1,100,i,damaged,i,out) ==
                   (i==15 ? REFMEM_VDC_FEEDBACK_BAD_RECORD : REFMEM_VDC_FEEDBACK_PROGRESS));
        assert(!ACTIVE(a) && memcmp(out,sentinel,64) == 0);
    }
    assembly_t a={0}; memcpy(out,sentinel,64);
    for (unsigned i=0;i<16;++i)
        assert(push(&a,1,100,i,wire,i,out) ==
               (i==15 ? REFMEM_VDC_FEEDBACK_COMPLETE : REFMEM_VDC_FEEDBACK_PROGRESS));
    uint8_t retained[64]; memcpy(retained,out,64);
    for (unsigned i=0;i<16;++i)
        assert(push(&a,1,200,i,wire,100+i,out) ==
               (i==15 ? REFMEM_VDC_FEEDBACK_COMPLETE : REFMEM_VDC_FEEDBACK_PROGRESS));
    assert(refmem_sync_vdc_feedback_compare(out,retained,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_DUPLICATE);
    refmem_sync_vdc_feedback_reset(&a); /* Caller history is untouched by cancellation. */
    assert(memcmp(retained,wire,64) == 0);
}

static void test_order(void)
{
    record_t r=specimen(1); uint8_t old[64],next[64];
    r.measurement_sequence=100; assert(refmem_sync_vdc_feedback_encode(&r,6,old));
    assert(refmem_sync_vdc_feedback_compare(old,NULL,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_FIRST);
    assert(refmem_sync_vdc_feedback_compare(old,old,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_DUPLICATE);
    r.rx_elapsed_cycles++; assert(refmem_sync_vdc_feedback_encode(&r,6,next));
    assert(refmem_sync_vdc_feedback_compare(next,old,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_CONFLICT);
    r.measurement_sequence=101; assert(refmem_sync_vdc_feedback_encode(&r,6,next));
    assert(refmem_sync_vdc_feedback_compare(next,old,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_NEWER);
    r.measurement_sequence=99; assert(refmem_sync_vdc_feedback_encode(&r,6,next));
    assert(refmem_sync_vdc_feedback_compare(next,old,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_STALE);
    for (unsigned field=0;field<4;++field) {
        r=specimen(1); r.measurement_sequence=0;
        if(field==0) ++r.source_clock_epoch_id;
        if(field==1) ++r.source_clock_run_id;
        if(field==2) ++r.source_arm_epoch;
        if(field==3) ++r.observer_epoch;
        r.tick_hz=250000000;
        assert(refmem_sync_vdc_feedback_encode(&r,6,next));
        assert(refmem_sync_vdc_feedback_compare(next,old,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_NEW_NAMESPACE);
    }
    r=specimen(1); r.measurement_sequence=UINT32_MAX;
    assert(refmem_sync_vdc_feedback_encode(&r,6,old));
    r.measurement_sequence=0; assert(refmem_sync_vdc_feedback_encode(&r,6,next));
    assert(refmem_sync_vdc_feedback_compare(next,old,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_STALE);
    r.measurement_sequence=100; assert(refmem_sync_vdc_feedback_encode(&r,6,old));
    for (unsigned field=0;field<3;++field) {
        r=specimen(1); r.measurement_sequence=101;
        if(field==0) ++r.tick_hz;
        if(field==1) ++r.timer1_enable_before;
        if(field==2) ++r.timer1_enable_after;
        assert(refmem_sync_vdc_feedback_encode(&r,6,next));
        assert(refmem_sync_vdc_feedback_compare(next,old,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_CONFLICT);
    }
    next[60]^=1;
    assert(refmem_sync_vdc_feedback_compare(next,old,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_INVALID);
    assert(refmem_sync_vdc_feedback_compare(old,next,6,1,0) == REFMEM_VDC_FEEDBACK_ORDER_INVALID);
    assert(refmem_sync_vdc_feedback_compare(old,NULL,6,2,0) == REFMEM_VDC_FEEDBACK_ORDER_INVALID);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"wire")) test_wire();
    else if(!strcmp(argv[1],"validation")) test_validation();
    else if(!strcmp(argv[1],"interleaved")) test_interleaved();
    else if(!strcmp(argv[1],"restart")) test_restart();
    else if(!strcmp(argv[1],"conflict")) test_conflict();
    else if(!strcmp(argv[1],"expiry")) test_expiry();
    else if(!strcmp(argv[1],"complete")) test_complete();
    else if(!strcmp(argv[1],"order")) test_order();
    else return 2;
    puts("feedback codec: passed"); return 0;
}
'''
