"""Independent schema-2 wire/CRC oracle and real codec/assembly boundaries.

This proves representation and local ordering only, not model authenticity,
admitted control sessions, input eligibility or physical output consumption.
"""
import random
import struct
import subprocess
import zlib

import pytest

from test_vdc_command_owner import ROOT, compile_executable


FIELDS = ("schema source target flags clock_epoch clock_run arm observer sequence tick_hz "
          "output_lo output_hi model_token command_seq control_session").split()
DEFAULT = dict(zip(FIELDS, (2, 1, 0, 15, 0x11223344, 0x55667788, 0x0102030405060708,
    0x10203040, 100, 250000000, 0x123456789ABCDEF0, 0x123456789ABCDEFF,
    0x87654321, 0x12345678, 0xFEDCBA98), strict=True))
BODY = struct.Struct("<BBBBIIQIIIQQIII")


def wire(**changes):
    row = DEFAULT | changes
    body = BODY.pack(*(row[key] for key in FIELDS))
    assert len(body) == 60
    return body + struct.pack("<I", zlib.crc32(body))


@pytest.fixture(scope="module")
def model_codec(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-model-feedback")
    return compile_executable(directory, "model_codec", HARNESS,
        [ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"])


def run(model_codec, *args):
    result = subprocess.run([str(model_codec), *map(str, args)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def test_schema2_encoder_matches_independent_wire_oracle(model_codec):
    assert bytes.fromhex(run(model_codec, "encode")) == wire()


@pytest.mark.parametrize("changes", [
    {}, dict(output_lo=0, output_hi=0, command_seq=0, sequence=0, clock_epoch=0, clock_run=0),
    dict(output_lo=(1 << 64) - 1, output_hi=(1 << 64) - 1, model_token=0xFFFFFFFF,
         command_seq=0xFFFFFFFF, control_session=0xFFFFFFFF),
    dict(output_lo=0, output_hi=(1 << 64) - 1), dict(tick_hz=1), dict(tick_hz=0xFFFFFFFF),
])
def test_schema2_decoder_limits_and_reserved_zero(model_codec, changes):
    decoded = list(map(int, run(model_codec, "decode", wire(**changes).hex()).split(',')))
    row = DEFAULT | changes
    assert decoded == [row[key] for key in FIELDS]


def test_schema2_random_oracle_roundtrips(model_codec):
    rng = random.Random(16091602)
    for _ in range(80):
        lo, hi = sorted((rng.getrandbits(64), rng.getrandbits(64)))
        changes = dict(output_lo=lo, output_hi=hi, model_token=rng.randint(1, 0xFFFFFFFF),
            command_seq=rng.getrandbits(32), control_session=rng.randint(1, 0xFFFFFFFF),
            arm=rng.randint(1, (1 << 64) - 1), sequence=rng.getrandbits(32))
        row = DEFAULT | changes
        decoded = list(map(int, run(model_codec, "decode", wire(**changes).hex()).split(',')))
        assert decoded == [row[key] for key in FIELDS]


@pytest.mark.parametrize("changes", [dict(schema=0), dict(schema=3), dict(schema=1),
    dict(flags=7), dict(flags=0), dict(flags=0x8F), dict(model_token=0),
    dict(control_session=0), dict(output_lo=11, output_hi=10), dict(arm=0),
    dict(observer=0), dict(tick_hz=0), dict(source=0), dict(source=6), dict(target=1),
])
def test_valid_crc_invalid_record_preserves_decoder_output(model_codec, changes):
    assert run(model_codec, "decode", wire(**changes).hex()) == 'INVALID'


def test_every_corrupt_wire_byte_is_rejected(model_codec):
    original = wire()
    for index in range(64):
        damaged = bytearray(original)
        damaged[index] ^= 1
        assert run(model_codec, "decode", damaged.hex()) == 'INVALID'


@pytest.mark.parametrize("kind", range(12))
def test_encode_rejects_invalid_without_overwriting_output(model_codec, kind):
    assert run(model_codec, "bad_encode", kind) == 'preserved'


ORDER_FIRST, ORDER_NAMESPACE, ORDER_NEWER, ORDER_DUPLICATE, ORDER_STALE, ORDER_CONFLICT, ORDER_INVALID = range(7)


@pytest.mark.parametrize("changes,expected", [
    ({}, ORDER_DUPLICATE),
    (dict(output_hi=DEFAULT['output_hi'] + 1), ORDER_CONFLICT),
    (dict(model_token=DEFAULT['model_token'] + 1), ORDER_CONFLICT),
    (dict(command_seq=DEFAULT['command_seq'] + 1), ORDER_CONFLICT),
    (dict(sequence=101), ORDER_NEWER),
    (dict(sequence=99), ORDER_STALE),
    (dict(sequence=101, model_token=DEFAULT['model_token'] + 1), ORDER_NEWER),
    (dict(sequence=101, command_seq=DEFAULT['command_seq'] + 1), ORDER_NEWER),
    (dict(sequence=101, output_lo=10, output_hi=20), ORDER_NEWER),
    (dict(sequence=101, model_token=DEFAULT['model_token'] - 1), ORDER_STALE),
    (dict(sequence=101, command_seq=DEFAULT['command_seq'] - 1), ORDER_STALE),
    (dict(sequence=101, model_token=DEFAULT['model_token'] + 1,
          command_seq=DEFAULT['command_seq'] - 1), ORDER_STALE),
    (dict(sequence=101, tick_hz=125000000), ORDER_CONFLICT),
    (dict(sequence=0, control_session=1, model_token=1, command_seq=0), ORDER_NAMESPACE),
    (dict(sequence=0, arm=1, tick_hz=1), ORDER_NAMESPACE),
    (dict(sequence=0, observer=1), ORDER_NAMESPACE),
    (dict(sequence=0, clock_epoch=1), ORDER_NAMESPACE),
    (dict(sequence=0, clock_run=1), ORDER_NAMESPACE),
    (dict(model_token=0), ORDER_INVALID), (dict(control_session=0), ORDER_INVALID),
])
def test_schema2_ordering_boundaries(model_codec, changes, expected):
    assert int(run(model_codec, "compare", wire(**changes).hex(), wire().hex())) == expected


@pytest.mark.parametrize("field", ['sequence', 'model_token', 'command_seq'])
def test_measurement_and_model_counters_never_wrap_within_namespace(model_codec, field):
    before = dict(sequence=100) | {field: 0xFFFFFFFF}
    after = dict(sequence=101) | {field: 1}
    assert int(run(model_codec, "compare", wire(**after).hex(), wire(**before).hex())) == ORDER_STALE


def test_schema_change_is_namespace_not_raw_anchor_or_model_equivalence(model_codec):
    # Independent schema-1 raw wire with the exact same acquisition identity.
    raw_body = struct.pack('<BBBBIIQIIIQQQI', 1, 1, 0, 7,
        DEFAULT['clock_epoch'], DEFAULT['clock_run'], DEFAULT['arm'], DEFAULT['observer'],
        DEFAULT['sequence'], DEFAULT['tick_hz'], 100, 200, 300, 10)
    raw = raw_body + struct.pack('<I', zlib.crc32(raw_body))
    for candidate, previous in ((wire(), raw), (raw, wire())):
        assert int(run(model_codec, 'compare', candidate.hex(), previous.hex())) == ORDER_NAMESPACE
    assert int(run(model_codec, 'compare', wire().hex(), 'NONE')) == ORDER_FIRST
    damaged = bytearray(wire()); damaged[63] ^= 1
    assert int(run(model_codec, 'compare', wire().hex(), damaged.hex())) == ORDER_INVALID


@pytest.mark.parametrize("case", ['complete', 'duplicate', 'expired', 'reordered', 'conflict',
                                  'mixed_schema', 'schema_restart', 'bad_record'])
def test_schema2_real_fragment_assembly(model_codec, case):
    assert run(model_codec, 'assembly', case, wire().hex()) == 'assembly passed'


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "refmem_sync_vdc_feedback.h"
typedef refmem_sync_vdc_feedback_record_t record_t;
typedef refmem_sync_vdc_feedback_assembly_t assembly_t;
static record_t specimen(void)
{
    return (record_t){.source_arm_epoch=UINT64_C(0x0102030405060708),
        .model={.output_ns_lo=UINT64_C(0x123456789abcdef0),.output_ns_hi=UINT64_C(0x123456789abcdeff),
            .model_token=0x87654321,.applied_command_seq=0x12345678,.control_session=0xfedcba98},
        .source_clock_epoch_id=0x11223344,.source_clock_run_id=0x55667788,
        .observer_epoch=0x10203040,.measurement_sequence=100,.tick_hz=250000000,
        .schema_version=REFMEM_VDC_FEEDBACK_MODEL_SCHEMA,.source_slot=1,.target_slot=0,
        .domain_flags=REFMEM_VDC_FEEDBACK_MODEL_DOMAIN_FLAGS};
}
static void fromhex(const char *text,uint8_t *out)
{ assert(strlen(text)==128);for(unsigned i=0;i<64;i++){unsigned v;assert(sscanf(text+i*2,"%2x",&v)==1);out[i]=(uint8_t)v;} }
static void hex(const uint8_t *data)
{ for(unsigned i=0;i<64;i++)printf("%02x",data[i]);puts(""); }
static void fix_crc(uint8_t *data)
{ uint32_t crc=refmem_sync_vdc_feedback_crc32(data,60);for(unsigned i=0;i<4;i++)data[60+i]=(uint8_t)(crc>>(i*8)); }
static unsigned push(assembly_t *a,unsigned first,unsigned index,const uint8_t *wire,unsigned ms,uint8_t *out)
{
    uint32_t sequence=first;for(unsigned i=0;i<index;i++)sequence=refmem_sync_vdc_feedback_next_sequence(sequence);
    return refmem_sync_vdc_feedback_push(a,1,0,6,sequence,(uint8_t)index,16,wire+index*4,ms,out);
}
static void assembly(const char *mode,const uint8_t *wire)
{
    assembly_t a={0};uint8_t out[64],saved[64],changed[64];
    memset(out,0xa5,64);memcpy(saved,out,64);memcpy(changed,wire,64);
    if(!strcmp(mode,"complete") || !strcmp(mode,"duplicate")) {
        for(unsigned i=0;i<16;i++) {
            assert(push(&a,UINT32_MAX-7,i,wire,10+i,out)==(i==15?REFMEM_VDC_FEEDBACK_COMPLETE:REFMEM_VDC_FEEDBACK_PROGRESS));
            assert(!memcmp(out,i==15?wire:saved,64));
            if(!strcmp(mode,"duplicate")) {
                const assembly_t before=a;
                assert(push(&a,UINT32_MAX-7,i,wire,20+i,out)==REFMEM_VDC_FEEDBACK_DUPLICATE);
                assert(!memcmp(&a,&before,sizeof(a)));
            }
        }
    } else if(!strcmp(mode,"expired")) {
        assert(push(&a,100,0,wire,10,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
        assert(push(&a,100,0,wire,1009,out)==REFMEM_VDC_FEEDBACK_DUPLICATE);
        assert(push(&a,100,1,wire,1010,out)==REFMEM_VDC_FEEDBACK_EXPIRED);
        assert(!memcmp(out,saved,64) && a.first_ms==10);
        assert(push(&a,100,0,wire,1011,out)==REFMEM_VDC_FEEDBACK_STALE);
    } else if(!strcmp(mode,"reordered")) {
        assert(push(&a,100,0,wire,10,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
        assert(push(&a,100,2,wire,11,out)==REFMEM_VDC_FEEDBACK_BAD_SEQUENCE);
        assert(!memcmp(out,saved,64));
    } else if(!strcmp(mode,"conflict")) {
        assert(push(&a,100,0,wire,10,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
        assert(push(&a,100,1,wire,11,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
        changed[4]^=1;
        assert(push(&a,100,1,changed,12,out)==REFMEM_VDC_FEEDBACK_CONFLICT);
        assert(!memcmp(out,saved,64));
    } else if(!strcmp(mode,"schema_restart")) {
        record_t raw=specimen();raw.schema_version=1;raw.domain_flags=7;
        raw.rx_elapsed_cycles=100;raw.tx_elapsed_cycles=200;
        raw.timer1_enable_before=300;raw.timer1_enable_after=310;
        assert(refmem_sync_vdc_feedback_encode(&raw,6,changed));
        assert(push(&a,100,0,changed,10,out)==REFMEM_VDC_FEEDBACK_PROGRESS);
        assert(push(&a,200,0,wire,11,out)==REFMEM_VDC_FEEDBACK_RESTARTED);
        assert(push(&a,100,1,changed,12,out)==REFMEM_VDC_FEEDBACK_STALE);
        for(unsigned i=1;i<16;i++)assert(push(&a,200,i,wire,12+i,out)==
            (i==15?REFMEM_VDC_FEEDBACK_COMPLETE:REFMEM_VDC_FEEDBACK_PROGRESS));
        assert(!memcmp(out,wire,64));
    } else {
        if(!strcmp(mode,"mixed_schema")){changed[0]=1;changed[3]=7;}
        else {assert(!strcmp(mode,"bad_record"));memset(changed+48,0,4);fix_crc(changed);}
        for(unsigned i=0;i<16;i++)assert(push(&a,100,i,changed,10+i,out)==
            (i==15?REFMEM_VDC_FEEDBACK_BAD_RECORD:REFMEM_VDC_FEEDBACK_PROGRESS));
        assert(!memcmp(out,saved,64));
    }
    puts("assembly passed");
}
int main(int argc,char **argv)
{
    assert(argc>=2 && sizeof(record_t)==64 && sizeof(assembly_t)==80);
    uint8_t encoded[64];record_t r=specimen();
    if(!strcmp(argv[1],"encode")) {
        assert(refmem_sync_vdc_feedback_encode(&r,6,encoded));hex(encoded);
    } else if(!strcmp(argv[1],"decode")) {
        assert(argc==3);uint8_t input[64];fromhex(argv[2],input);
        record_t output,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));output=sentinel;
        if(!refmem_sync_vdc_feedback_decode(input,6,1,0,&output)) {
            assert(!memcmp(&output,&sentinel,sizeof(output)));puts("INVALID");return 0;
        }
        assert(output.schema_version==2 && output.model.reserved==0);
        assert(refmem_sync_vdc_feedback_encode(&output,6,encoded) && !memcmp(input,encoded,64));
        printf("%u,%u,%u,%u,%"PRIu32",%"PRIu32",%"PRIu64",%"PRIu32",%"PRIu32",%"PRIu32
            ",%"PRIu64",%"PRIu64",%"PRIu32",%"PRIu32",%"PRIu32"\n",
            output.schema_version,output.source_slot,output.target_slot,output.domain_flags,
            output.source_clock_epoch_id,output.source_clock_run_id,output.source_arm_epoch,
            output.observer_epoch,output.measurement_sequence,output.tick_hz,
            output.model.output_ns_lo,output.model.output_ns_hi,output.model.model_token,
            output.model.applied_command_seq,output.model.control_session);
    } else if(!strcmp(argv[1],"bad_encode")) {
        assert(argc==3);unsigned kind=(unsigned)atoi(argv[2]);
        if(kind==0)r.model.reserved=1;
        if(kind==1)r.model.model_token=0;
        if(kind==2)r.model.control_session=0;
        if(kind==3)r.model.output_ns_lo=r.model.output_ns_hi+1;
        if(kind==4)r.domain_flags=7;
        if(kind==5)r.schema_version=3;
        if(kind==6)r.source_arm_epoch=0;
        if(kind==7)r.observer_epoch=0;
        if(kind==8)r.tick_hz=0;
        if(kind==9)r.source_slot=0;
        if(kind==10)r.source_slot=6;
        if(kind==11)r.target_slot=6;
        memset(encoded,0xa5,64);uint8_t saved[64];memcpy(saved,encoded,64);
        assert(!refmem_sync_vdc_feedback_encode(&r,6,encoded) && !memcmp(encoded,saved,64));puts("preserved");
    } else if(!strcmp(argv[1],"compare")) {
        assert(argc==4);uint8_t candidate[64],previous[64];fromhex(argv[2],candidate);
        uint8_t *old=NULL;if(strcmp(argv[3],"NONE")){fromhex(argv[3],previous);old=previous;}
        printf("%u\n",refmem_sync_vdc_feedback_compare(candidate,old,6,1,0));
    } else if(!strcmp(argv[1],"assembly")) {
        assert(argc==4);uint8_t input[64];fromhex(argv[3],input);assembly(argv[2],input);
    } else assert(!"unknown mode");
    return 0;
}
'''
