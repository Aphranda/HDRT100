"""Schema3 boundary-command wire and typed assembly against real production C."""
import struct
import subprocess
import zlib

import pytest

from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope='module')
def boundary_codec_executable(tmp_path_factory):
    body = struct.pack('<BBBBIIIIIQIIIIiQ', 3, 0, 1, 1,
                       0xABCDEF12, 9, 0x12345678, 0, 0x11223344,
                       0x0102030405060708, 0x34567890, 0, 7, 5,
                       -1234567, 0x1020304050607080)
    assert len(body) == 60
    wire = body + struct.pack('<I', zlib.crc32(body))
    return compile_executable(tmp_path_factory.mktemp('boundary-codec'), 'boundary_codec',
        HARNESS.replace('WIRE_ORACLE', ','.join(map(str, wire))),
        [ROOT / 'components/distributed_refmem/src/refmem_sync_vdc_feedback.c'])


@pytest.mark.parametrize('scenario', ['wire', 'invalid', 'typed', 'sequence', 'expiry', 'mixing'])
def test_boundary_command_codec(boundary_codec_executable, scenario):
    result = subprocess.run([str(boundary_codec_executable), scenario], text=True,
                            capture_output=True, timeout=10)
    (boundary_codec_executable.parent / (scenario + '.log')).write_text(
        result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "refmem_sync_vdc_feedback.h"

typedef refmem_sync_vdc_boundary_command_t command_t;
typedef refmem_sync_vdc_feedback_assembly_t assembly_t;
static const uint8_t oracle[64] = {WIRE_ORACLE};
_Static_assert(sizeof(command_t) == 64u, "decoded command64");
_Static_assert(offsetof(command_t, schema_version) == 60u, "shared type location");
_Static_assert(sizeof(assembly_t) == 80u, "assembly does not grow");
#define ACTIVE(a) (((a).state & REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE) != 0u)

static command_t specimen(void)
{
    return (command_t){
        .target_arm_epoch=UINT64_C(0x0102030405060708),
        .basis_source_output_ns_lo=UINT64_C(0x1020304050607080),
        .control_session=0xabcdef12u, .command_seq=9u, .schedule_crc32=0x12345678u,
        .target_clock_epoch_id=0u, .target_clock_run_id=0x11223344u,
        .target_observer_epoch=0x34567890u, .basis_measurement_sequence=0u,
        .expected_target_model_token=7u, .expected_applied_command_seq=5u,
        .signed_delta_rate_ppb=-1234567, .reserved=0u,
        .schema_version=3u, .source_slot=0u, .target_slot=1u, .flags=1u};
}

static uint32_t seq_at(uint32_t first, unsigned index)
{
    while (index--) first=refmem_sync_vdc_feedback_next_sequence(first);
    return first;
}

static refmem_sync_vdc_feedback_result_t push(assembly_t *a, uint32_t first,
    unsigned index, const uint8_t *wire, uint32_t now, uint8_t *out)
{
    return refmem_sync_vdc_boundary_command_push(a,0,1,6,seq_at(first,index),
        (uint8_t)index,16,wire+index*4u,now,out);
}

static void unchanged_decode(const uint8_t *wire,uint32_t nodes,uint32_t source,uint32_t target)
{
    command_t out, before;
    memset(&out,0xa5,sizeof(out)); memcpy(&before,&out,sizeof(out));
    assert(!refmem_sync_vdc_boundary_command_decode(wire,nodes,source,target,&out));
    assert(memcmp(&out,&before,sizeof(out))==0);
}

static void wire_test(void)
{
    command_t cmd=specimen(), decoded;
    uint8_t wire[64];
    assert(refmem_sync_vdc_boundary_command_encode(&cmd,6,wire));
    assert(memcmp(wire,oracle,64)==0);
    memset(&decoded,0xa5,sizeof(decoded));
    assert(refmem_sync_vdc_boundary_command_decode(wire,6,0,1,&decoded));
    assert(memcmp(&cmd,&decoded,sizeof(cmd))==0);
    cmd.flags=REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS;
    assert(refmem_sync_vdc_boundary_command_encode(&cmd,6,wire));
    assert(wire[3]==3u && refmem_sync_vdc_boundary_command_decode(wire,6,0,1,&decoded));
    assert(memcmp(&cmd,&decoded,sizeof(cmd))==0);
    const int32_t rates[]={INT32_MIN,-1,0,1,INT32_MAX};
    for(unsigned i=0;i<sizeof(rates)/sizeof(rates[0]);++i) {
        cmd.signed_delta_rate_ppb=rates[i];
        assert(refmem_sync_vdc_boundary_command_encode(&cmd,6,wire));
        assert(refmem_sync_vdc_boundary_command_decode(wire,6,0,1,&decoded));
        assert(decoded.signed_delta_rate_ppb==rates[i]);
    }
    cmd=specimen();cmd.command_seq=UINT32_MAX;cmd.expected_applied_command_seq=UINT32_MAX-1u;
    cmd.target_clock_run_id=0;cmd.basis_source_output_ns_lo=UINT64_MAX;
    assert(refmem_sync_vdc_boundary_command_encode(&cmd,6,wire));
    assert(refmem_sync_vdc_boundary_command_decode(wire,6,0,1,&decoded));
    assert(memcmp(&cmd,&decoded,sizeof(cmd))==0);
}

static void invalid_test(void)
{
    command_t cmd;
    uint8_t output[64],before[64];memset(before,0xa5,sizeof(before));
#define INVALID(change) do { cmd=specimen(); change; memcpy(output,before,64); \
    assert(!refmem_sync_vdc_boundary_command_encode(&cmd,6,output)); \
    assert(memcmp(output,before,64)==0); } while(0)
    INVALID(cmd.schema_version=1u);
    INVALID(cmd.schema_version=2u);
    INVALID(cmd.schema_version=4u);
    INVALID(cmd.flags=0u);
    INVALID(cmd.flags=2u);
    INVALID(cmd.source_slot=6u);
    INVALID(cmd.target_slot=6u);
    INVALID(cmd.target_slot=cmd.source_slot);
    INVALID(cmd.control_session=0u);
    INVALID(cmd.command_seq=0u);
    INVALID(cmd.command_seq=cmd.expected_applied_command_seq);
    INVALID(cmd.expected_applied_command_seq=cmd.command_seq+1u);
    INVALID(cmd.target_arm_epoch=0u);
    INVALID(cmd.target_observer_epoch=0u);
    INVALID(cmd.expected_target_model_token=0u);
    INVALID(cmd.reserved=1u);
#undef INVALID
    cmd=specimen();memcpy(output,before,64);
    assert(!refmem_sync_vdc_boundary_command_encode(NULL,6,output));
    assert(!refmem_sync_vdc_boundary_command_encode(&cmd,6,NULL));
    assert(!refmem_sync_vdc_boundary_command_encode(&cmd,1,output));
    assert(!refmem_sync_vdc_boundary_command_encode(&cmd,9,output));
    assert(memcmp(output,before,64)==0);
    unchanged_decode(NULL,6,0,1);
    unchanged_decode(oracle,1,0,1);
    unchanged_decode(oracle,9,0,1);
    unchanged_decode(oracle,6,2,1);
    unchanged_decode(oracle,6,0,2);
    assert(!refmem_sync_vdc_boundary_command_decode(oracle,6,0,1,NULL));
    for(unsigned i=0;i<64;++i) {
        uint8_t corrupt[64];memcpy(corrupt,oracle,64);corrupt[i]^=1u;
        unchanged_decode(corrupt,6,0,1);
    }
}

static void feedback_wire(uint8_t schema,uint8_t *wire)
{
    refmem_sync_vdc_feedback_record_t f={.source_arm_epoch=1u,
        .source_clock_epoch_id=2u,.source_clock_run_id=3u,.observer_epoch=4u,
        .measurement_sequence=5u,.tick_hz=250000000u,
        .schema_version=schema,.source_slot=0u,.target_slot=1u,
        .domain_flags=schema==1u?7u:15u};
    if(schema==1u) {f.timer1_enable_before=1u;f.timer1_enable_after=3u;}
    else {f.model.output_ns_lo=4u;f.model.output_ns_hi=5u;
          f.model.model_token=6u;f.model.control_session=7u;}
    assert(refmem_sync_vdc_feedback_encode(&f,6,wire));
}

static void typed_test(void)
{
    refmem_sync_vdc_feedback_record_t feedback, before;
    memset(&feedback,0xa5,sizeof(feedback));memcpy(&before,&feedback,sizeof(before));
    assert(!refmem_sync_vdc_feedback_decode(oracle,6,0,1,&feedback));
    assert(memcmp(&feedback,&before,sizeof(before))==0);
    for(uint8_t schema=1;schema<=2;schema++) {
        uint8_t wire[64];feedback_wire(schema,wire);unchanged_decode(wire,6,0,1);
    }
    uint8_t out[64],sentinel[64];memset(sentinel,0xa5,64);memcpy(out,sentinel,64);
    assembly_t a;refmem_sync_vdc_feedback_reset(&a);
    for(unsigned i=0;i<16;i++) {
        const refmem_sync_vdc_feedback_result_t r=push(&a,100,i,oracle,20+i,out);
        assert(r==(i==15?REFMEM_VDC_FEEDBACK_COMPLETE:REFMEM_VDC_FEEDBACK_PROGRESS));
        assert(memcmp(out,i==15?oracle:sentinel,64)==0);
    }
    assert(!ACTIVE(a));
    command_t cmd;assert(refmem_sync_vdc_boundary_command_decode(out,6,0,1,&cmd));
    assert(cmd.command_seq==9u);
}

static void sequence_test(void)
{
    assembly_t a;refmem_sync_vdc_feedback_reset(&a);
    uint8_t out[64],before[64];memset(out,0xa5,64);memcpy(before,out,64);
    for(unsigned i=0;i<16;i++) {
        assert(push(&a,UINT32_MAX-7u,i,oracle,10+i,out)==
               (i==15?REFMEM_VDC_FEEDBACK_COMPLETE:REFMEM_VDC_FEEDBACK_PROGRESS));
        if(i<15) assert(push(&a,UINT32_MAX-7u,i,oracle,11+i,out)==REFMEM_VDC_FEEDBACK_DUPLICATE);
    }
    assert(memcmp(out,oracle,64)==0);
    refmem_sync_vdc_feedback_reset(&a);memcpy(out,before,64);
    assert(push(&a,100,0,oracle,10,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,100,1,oracle,11,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,100,0,oracle,12,out)==REFMEM_VDC_FEEDBACK_DUPLICATE);
    assert(push(&a,90,0,oracle,13,out)==REFMEM_VDC_FEEDBACK_STALE);
    assert(push(&a,200,0,oracle,14,out)==REFMEM_VDC_FEEDBACK_RESTARTED);
    assert(push(&a,100,1,oracle,15,out)==REFMEM_VDC_FEEDBACK_STALE);
    assert(ACTIVE(a) && a.first_sequence==200u);
    assert(push(&a,200,2,oracle,16,out)==REFMEM_VDC_FEEDBACK_BAD_SEQUENCE);
    assert(!ACTIVE(a) && memcmp(out,before,64)==0);
    assert(push(&a,300,0,oracle,17,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,300,1,oracle,18,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    uint8_t changed[64];memcpy(changed,oracle,64);changed[4]^=1u;
    assert(push(&a,300,1,changed,19,out)==REFMEM_VDC_FEEDBACK_CONFLICT);
    assert(!ACTIVE(a) && memcmp(out,before,64)==0);
}

static void expiry_test(void)
{
    assembly_t a;refmem_sync_vdc_feedback_reset(&a);
    uint8_t out[64],before[64];memset(out,0xa5,64);memcpy(before,out,64);
    assert(push(&a,100,0,oracle,10,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,100,0,oracle,1009,out)==REFMEM_VDC_FEEDBACK_DUPLICATE);
    assert(a.first_ms==10u);
    assert(push(&a,100,1,oracle,1010,out)==REFMEM_VDC_FEEDBACK_EXPIRED);
    assert(!ACTIVE(a) && memcmp(out,before,64)==0);
    assert(push(&a,100,0,oracle,1011,out)==REFMEM_VDC_FEEDBACK_STALE);
    assert(push(&a,200,0,oracle,1012,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(refmem_sync_vdc_feedback_expire(&a,2012));
    assert(!ACTIVE(a));
    refmem_sync_vdc_feedback_reset(&a);
    assert(push(&a,300,0,oracle,UINT32_MAX-10u,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,400,0,oracle,990,out)==REFMEM_VDC_FEEDBACK_EXPIRED_RESTARTED);
    assert(a.first_sequence==400u && a.first_ms==990u);
}

static void mixing_test(void)
{
    uint8_t raw[64],out[64],before[64];feedback_wire(2u,raw);
    memset(out,0xa5,64);memcpy(before,out,64);
    assembly_t a;refmem_sync_vdc_feedback_reset(&a);
    assert(push(&a,100,0,oracle,10,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(refmem_sync_vdc_feedback_push(&a,0,1,6,101,1,16,raw+4,11,out)!=REFMEM_VDC_FEEDBACK_COMPLETE);
    assert(!ACTIVE(a) && memcmp(out,before,64)==0);
    refmem_sync_vdc_feedback_reset(&a);
    assert(refmem_sync_vdc_feedback_push(&a,0,1,6,200,0,16,raw,12,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
    assert(push(&a,200,1,oracle,13,out)!=REFMEM_VDC_FEEDBACK_COMPLETE);
    assert(!ACTIVE(a) && memcmp(out,before,64)==0);
    refmem_sync_vdc_feedback_reset(&a);
    assert(push(&a,300,0,raw,14,out)!=REFMEM_VDC_FEEDBACK_COMPLETE);
    assert(!ACTIVE(a));
    refmem_sync_vdc_feedback_reset(&a);
    assert(refmem_sync_vdc_feedback_push(&a,0,1,6,400,0,16,oracle,15,out)!=REFMEM_VDC_FEEDBACK_COMPLETE);
    assert(!ACTIVE(a));
    /* Same class but wrong record bytes cannot bypass the final CRC. */
    refmem_sync_vdc_feedback_reset(&a);
    for(unsigned i=0;i<16;i++) {
        const uint8_t *fragment=i==7?raw:oracle;
        const refmem_sync_vdc_feedback_result_t r=push(&a,500,i,fragment,20+i,out);
        assert(r==(i==15?REFMEM_VDC_FEEDBACK_BAD_RECORD:REFMEM_VDC_FEEDBACK_PROGRESS));
    }
    assert(!ACTIVE(a) && memcmp(out,before,64)==0);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"wire")) wire_test();
    else if(!strcmp(argv[1],"invalid")) invalid_test();
    else if(!strcmp(argv[1],"typed")) typed_test();
    else if(!strcmp(argv[1],"sequence")) sequence_test();
    else if(!strcmp(argv[1],"expiry")) expiry_test();
    else if(!strcmp(argv[1],"mixing")) mixing_test();
    else assert(0);
    puts("boundary command codec: passed");return 0;
}
'''
