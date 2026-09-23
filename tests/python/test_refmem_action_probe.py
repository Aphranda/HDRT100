"""Compile the actual diagnostic protocol kernel; no board or GPIO access."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def probe(tmp_path_factory):
    directory = tmp_path_factory.mktemp("refmem-action-probe")
    source = directory / "probe.c"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host C compiler is required"
    executable = directory / ("probe.exe" if os.name == "nt" else "probe")
    result = subprocess.run([
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        f"-I{ROOT / 'components/distributed_refmem/inc'}", str(source), "-o", str(executable),
    ], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


def run(probe, scenario):
    result = subprocess.run([str(probe), scenario], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def test_wire_matches_independent_python_codec(probe):
    body = struct.pack("<IIIIQQIIIIII", 11, 22, 0x12345678, 7,
                       0x1122334455667788, 0x8877665544332211, 0, 2, 3, 1, 0xA1B2C3D4, 0)
    assert len(body) == 56
    wire = body + struct.pack("<I", zlib.crc32(body))
    assert bytes.fromhex(run(probe, "codec")) == wire


@pytest.mark.parametrize("scenario", [
    "roundtrip", "incomplete", "out_of_order", "corrupt", "reserved", "mixed",
    "duplicates", "execute_once", "conflict", "old", "identity", "receipt", "cancel",
    "reconfigure", "source_independent", "counter_role", "invalid",
])
def test_protocol_and_execution_boundaries(probe, scenario):
    run(probe, scenario)


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include "refmem_action_probe.h"

static refmem_action_probe_record_t record(void) {
    const refmem_action_probe_record_t r = {
        .session=11u, .deployment_generation=22u, .deployment_crc32=0x12345678u,
        .request_id=7u, .source_uid=0x1122334455667788ull, .target_uid=0x8877665544332211ull,
        .source_slot=0u, .target_slot=2u, .business_role=3u,
        .opcode=REFMEM_ACTION_PROBE_SET_VALUE, .argument=0xA1B2C3D4u
    }; return r;
}
static refmem_action_probe_config_t config(void) {
    const refmem_action_probe_record_t r=record();
    const refmem_action_probe_config_t c = {.session=r.session,
        .deployment_generation=r.deployment_generation, .deployment_crc32=r.deployment_crc32,
        .local_uid=r.target_uid, .local_slot=r.target_slot, .business_role=r.business_role};
    return c;
}
static refmem_action_probe_result_t push(refmem_action_probe_assembly_t *a,
    const refmem_action_probe_record_t *r, uint8_t kind, uint8_t index,
    refmem_action_probe_record_t *out) {
    uint16_t field; uint32_t value;
    assert(refmem_action_probe_fragment_encode(r,index,&field,&value));
    return refmem_action_probe_assembly_push(a,kind,index,field,value,out);
}
int main(int argc, char **argv) {
    assert(argc==2); const char *s=argv[1];
    refmem_action_probe_record_t r=record(), out={0};
    refmem_action_probe_config_t c=config();
    refmem_action_probe_engine_t e={0};
    refmem_action_probe_assembly_t a={0};
    uint8_t wire[REFMEM_ACTION_PROBE_WIRE_SIZE];
    assert(refmem_action_probe_encode(&r,wire));
    assert(refmem_action_probe_engine_configure(&e,&c));
    if (!strcmp(s,"codec")) {
        for (unsigned i=0;i<sizeof(wire);++i) printf("%02x",wire[i]);
        puts(""); return 0;
    }
    if (!strcmp(s,"roundtrip")) {
        for (uint8_t i=0;i<REFMEM_ACTION_PROBE_FRAGMENT_COUNT;++i)
            assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,i,&out)==
                (i==9?REFMEM_ACTION_PROBE_COMPLETE:REFMEM_ACTION_PROBE_INCOMPLETE));
        assert(refmem_action_probe_equal(&r,&out));
    } else if (!strcmp(s,"incomplete")) {
        for (uint8_t i=0;i<9;++i) assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,i,&out)==REFMEM_ACTION_PROBE_INCOMPLETE);
        assert(out.request_id==0 && e.action_count==0);
    } else if (!strcmp(s,"out_of_order")) {
        assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,0,&out)==REFMEM_ACTION_PROBE_INCOMPLETE);
        assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,2,&out)==REFMEM_ACTION_PROBE_REJECT_FRAGMENT);
        assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,1,&out)==REFMEM_ACTION_PROBE_REJECT_FRAGMENT);
        for(uint8_t i=0;i<10;++i) assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,i,&out)==
            (i==9?REFMEM_ACTION_PROBE_COMPLETE:REFMEM_ACTION_PROBE_INCOMPLETE));
    } else if (!strcmp(s,"corrupt")) {
        for(unsigned byte=0;byte<sizeof(wire);++byte) {
            wire[byte]^=1; assert(!refmem_action_probe_decode(wire,&out)); wire[byte]^=1;
        }
        assert(out.request_id==0);
        for(uint8_t i=0;i<10;++i) {
            uint16_t f; uint32_t v; assert(refmem_action_probe_fragment_encode(&r,i,&f,&v));
            if(i==8) v^=1;
            assert(refmem_action_probe_assembly_push(&a,REFMEM_ACTION_PROBE_REQUEST,i,f,v,&out)==
                (i==9?REFMEM_ACTION_PROBE_REJECT_CRC:REFMEM_ACTION_PROBE_INCOMPLETE));
        }
        assert(out.request_id==0);
    } else if (!strcmp(s,"reserved")) {
        wire[52]=1; refmem_action_probe_put32(wire+56,refmem_action_probe_crc32(wire,56));
        assert(!refmem_action_probe_decode(wire,&out));
    } else if (!strcmp(s,"mixed")) {
        assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,0,&out)==REFMEM_ACTION_PROBE_INCOMPLETE);
        assert(push(&a,&r,REFMEM_ACTION_PROBE_RECEIPT,1,&out)==REFMEM_ACTION_PROBE_REJECT_FRAGMENT);
        for(uint8_t i=0;i<10;++i) assert(push(&a,&r,REFMEM_ACTION_PROBE_RECEIPT,i,&out)==
            (i==9?REFMEM_ACTION_PROBE_COMPLETE:REFMEM_ACTION_PROBE_INCOMPLETE));
    } else if (!strcmp(s,"duplicates")) {
        for(uint8_t i=0;i<10;++i) {
            assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,i,&out)==
                (i==9?REFMEM_ACTION_PROBE_COMPLETE:REFMEM_ACTION_PROBE_INCOMPLETE));
            if(i<9) assert(push(&a,&r,REFMEM_ACTION_PROBE_REQUEST,i,&out)==REFMEM_ACTION_PROBE_INCOMPLETE);
        }
        assert(refmem_action_probe_equal(&r,&out));
    } else if (!strcmp(s,"execute_once")) {
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        assert(e.action_count==1 && e.value==r.argument);
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_DUPLICATE);
        assert(e.action_count==1);
    } else if (!strcmp(s,"conflict")) {
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        ++r.argument;
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_REJECT_CONFLICT);
        assert(e.action_count==1 && e.value!=r.argument);
    } else if (!strcmp(s,"old")) {
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        --r.request_id; assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_REJECT_OLD);
        r.request_id=UINT32_MAX; assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        r.request_id=1; assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_REJECT_OLD);
        assert(e.action_count==2);
    } else if (!strcmp(s,"identity")) {
        for(unsigned i=0;i<6;++i) {
            r=record();
            if(i==0) ++r.session;
            if(i==1) ++r.deployment_generation;
            if(i==2) ++r.deployment_crc32;
            if(i==3) ++r.target_uid;
            if(i==4) ++r.target_slot;
            if(i==5) r.business_role=0;
            assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_REJECT_IDENTITY);
        } assert(e.action_count==0);
    } else if (!strcmp(s,"receipt")) {
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_RECEIPT,&r)==REFMEM_ACTION_PROBE_REJECT_KIND);
        assert(refmem_action_probe_engine_execute(&e,0,&r)==REFMEM_ACTION_PROBE_REJECT_KIND);
        assert(e.action_count==0);
    } else if (!strcmp(s,"cancel")) {
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        refmem_action_probe_engine_cancel(&e);
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_REJECT_INACTIVE);
        assert(!refmem_action_probe_engine_configure(&e,&c));
        ++c.session; assert(refmem_action_probe_engine_configure(&e,&c));
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_REJECT_IDENTITY);
        ++r.session; assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        assert(e.action_count==2);
    } else if (!strcmp(s,"reconfigure")) {
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        assert(refmem_action_probe_engine_configure(&e,&c));
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_DUPLICATE);
        ++c.deployment_generation; assert(!refmem_action_probe_engine_configure(&e,&c));
        --c.session; assert(!refmem_action_probe_engine_configure(&e,&c));
        assert(e.action_count==1);
    } else if (!strcmp(s,"source_independent")) {
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        r.source_slot=1; ++r.source_uid; r.request_id=1;
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_APPLIED);
        assert(e.action_count==2);
    } else if (!strcmp(s,"counter_role")) {
        refmem_action_probe_engine_t counter={0};
        c.business_role=0;
        assert(refmem_action_probe_engine_configure(&counter,&c));
        r.business_role=0;
        assert(refmem_action_probe_encode(&r,wire));
        assert(refmem_action_probe_decode(wire,&out));
        assert(refmem_action_probe_engine_execute(&counter,REFMEM_ACTION_PROBE_REQUEST,&out)==REFMEM_ACTION_PROBE_APPLIED);
        assert(counter.action_count==1);
    } else if (!strcmp(s,"invalid")) {
        r.source_slot=8; assert(!refmem_action_probe_encode(&r,wire));
        r=record(); r.source_uid=r.target_uid; assert(!refmem_action_probe_encode(&r,wire));
        r=record(); r.request_id=0; assert(!refmem_action_probe_encode(&r,wire));
        assert(refmem_action_probe_engine_execute(&e,REFMEM_ACTION_PROBE_REQUEST,&r)==REFMEM_ACTION_PROBE_REJECT_ARGUMENT);
        assert(refmem_action_probe_assembly_push(&a,REFMEM_ACTION_PROBE_REQUEST,10,0,0,&out)==REFMEM_ACTION_PROBE_REJECT_FRAGMENT);
        assert(e.action_count==0);
    } else assert(!"unknown scenario");
    return 0;
}
'''
