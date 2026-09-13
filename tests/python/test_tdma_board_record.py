"""Run the production recorder against bounded storage and a controlled board clock."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "tdma_board_record", ROOT / "tools/calibration_ring_validate/tdma_board_record.py")
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diagnostics_tdma_record.h"
#include "ota_crc32.h"
static uint8_t bytes[16384];
static uint32_t used, capacity=sizeof(bytes), reads, save_calls;
static uint64_t now=100u;
static int scenario;
extern uint32_t pota_crc32_update(uint32_t crc,const void *data,size_t size);
uint32_t ota_crc32_update(uint32_t crc,const uint8_t *p,size_t n) {
    return pota_crc32_update(crc,p,n);
}
static bool begin(uint32_t epoch,uint32_t *cap) { assert(epoch==123u); *cap=capacity; return scenario!=6; }
static bool append(uint32_t off,const uint8_t *p,size_t n) {
    assert(off==used && used+n<=capacity); memcpy(bytes+used,p,n); used+=(uint32_t)n; return true;
}
static bool save(uint32_t crc,uint32_t *job) {
    save_calls++; assert(crc==ota_crc32_update(0u,bytes,used));
    *job=77u; return save_calls>1u;
}
static int result(uint32_t job) { assert(job==77u); return 1; }
static uint32_t snapshot(uint32_t *words) {
    memset(words,0,DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS*sizeof(uint32_t));
    words[0]=1u; words[6]=reads++; now+=10u; return scenario==5 && reads>1u ? 0u : DIAGNOSTICS_TDMA_RECORD_VALID;
}
static uint64_t clock_now(void) { return now; }
static void identity(uint64_t *build,uint64_t *board) { *build=1; *board=2; }
static const diagnostics_tdma_record_port_t port={begin,append,save,result,snapshot,clock_now,identity};
int main(int argc,char **argv) {
    assert(argc==3); scenario=atoi(argv[1]);
    diagnostics_tdma_record_status_t st;
    assert(!diagnostics_tdma_record_arm(0,1000,4));
    assert(!diagnostics_tdma_record_arm(123,0,4));
    assert(!diagnostics_tdma_record_arm(123,1000,0));
    assert(!diagnostics_tdma_record_save());
    if(scenario==3) capacity=(16u+12u+(DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS+31u)/32u+
        DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS+16u)*4u+4u;
    assert(diagnostics_tdma_record_arm(123,1000,4));
    assert(!diagnostics_tdma_record_arm(123,1000,4));
    assert(!diagnostics_tdma_record_status(&st));
    diagnostics_tdma_record_service(&port);
    assert(diagnostics_tdma_record_status(&st));
    if(scenario==6) { assert(st.state==TDMA_RECORD_FAILED && used==0); return 0; }
    assert(st.state==TDMA_RECORD_ARMED && reads==1u);
    if(scenario==4) {
        diagnostics_tdma_record_cancel(); diagnostics_tdma_record_service(&port);
    } else {
        now=1000; diagnostics_tdma_record_start(now);
        assert(!diagnostics_tdma_record_status(&st));
        assert(!diagnostics_tdma_record_save());
        if(scenario==2) now=4500;
        if(scenario==7) now=9000;
        for(unsigned i=0;i<4;i++) {
            diagnostics_tdma_record_service(&port);
            if(diagnostics_tdma_record_status(&st)) break;
            now+=1000;
        }
    }
    assert(diagnostics_tdma_record_status(&st) && st.state==TDMA_RECORD_FROZEN);
    assert(save_calls==0u); /* No SD IO occurs during acquisition or freeze. */
    if(scenario==1 || scenario==5) assert(st.written==4 && st.missed==0);
    if(scenario==2) assert(st.written==1 && st.missed==3);
    if(scenario==3) assert(st.reason==TDMA_RECORD_OVERFLOW && st.written==0);
    if(scenario==4) assert(st.reason==TDMA_RECORD_CANCELLED && st.written==0);
    if(scenario==7) assert(st.written==0 && st.missed==4);
    FILE *f=fopen(argv[2],"wb"); assert(f); assert(fwrite(bytes,1,used,f)==used); fclose(f);
    assert(diagnostics_tdma_record_save()); diagnostics_tdma_record_service(&port);
    assert(diagnostics_tdma_record_status(&st) && st.state==TDMA_RECORD_FROZEN);
    assert(diagnostics_tdma_record_save()); diagnostics_tdma_record_service(&port);
    diagnostics_tdma_record_service(&port);
    assert(diagnostics_tdma_record_status(&st) && st.state==TDMA_RECORD_SAVED && st.job_id==77);
    assert(diagnostics_tdma_record_arm(124,1000,1));
}
'''


@pytest.fixture(scope="module")
def recorder(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("tdma-board-record")
    source = tmp / "harness.c"
    source.write_text(HARNESS, encoding="utf-8")
    executable = tmp / "record.exe"
    compiler = shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "components/diagnostics/inc"),
                    "-I", str(ROOT / "components/ota_manager/inc"), str(source),
                    "-I", str(ROOT / "third_party/portable_ota/include"),
                    str(ROOT / "third_party/portable_ota/src/pota_crc32.c"),
                    str(ROOT / "components/diagnostics/src/diagnostics_tdma_record.c"),
                    "-o", str(executable)], check=True, capture_output=True, text=True)
    return executable, tmp


@pytest.mark.parametrize("scenario", range(1, 8))
def test_record_deadlines_capacity_failure_and_deferred_save(recorder, scenario):
    executable, tmp = recorder
    path = tmp / f"scenario-{scenario}.bin"
    subprocess.run([str(executable), str(scenario), str(path)], check=True, capture_output=True, text=True)
    if scenario == 6:
        assert not path.exists()
        return
    result = decoder.decode_record(path.read_bytes(), expected_build=1, expected_board=2, expected_epoch=123)
    assert result["collection_passed"] == (scenario == 1)
    if scenario == 2:
        assert result["samples"][0]["slot"] == 3
        assert result["terminal"]["missed"] == 3


@pytest.mark.parametrize("mutation", ["crc", "truncate", "build", "epoch", "sequence", "bitmap"])
def test_corrupt_or_foreign_evidence_is_rejected(recorder, mutation):
    executable, tmp = recorder
    path = tmp / f"valid-{mutation}.bin"
    subprocess.run([str(executable), "1", str(path)], check=True, capture_output=True, text=True)
    data = bytearray(path.read_bytes())
    kwargs = dict(expected_build=1, expected_board=2, expected_epoch=123)
    if mutation == "crc": data[100] ^= 1
    elif mutation == "truncate": data = data[:-4]
    elif mutation in ("build", "epoch"): kwargs["expected_" + mutation] += 1
    else:
        if mutation == "sequence": struct.pack_into("<I", data, 64 + 8, 1)
        else: struct.pack_into("<I", data, 64 + 48, 0)
        struct.pack_into("<I", data, len(data) - 64 + 44, zlib.crc32(data[:-64]))
    with pytest.raises(ValueError): decoder.decode_record(data, **kwargs)


def test_schema_preserves_the_complete_historical_health_field_sets():
    import sys
    sys.path[:0] = [str(ROOT / "tools/calibration_ring_validate"), str(ROOT / "tools/tdma_ring_monitor"), str(ROOT / "tools")]
    from trn03_closed_loop import RUNTIME_FIELDS, PROCESS_FIELDS, FIFO_FIELDS, PHYS_FIELDS, CRC_DIAGNOSTIC_FIELDS
    schema = decoder.field_schema()
    for group, expected in (("runtime", RUNTIME_FIELDS), ("process", PROCESS_FIELDS),
                            ("fifo", FIFO_FIELDS), ("physical", PHYS_FIELDS),
                            ("crc_diagnostic", CRC_DIAGNOSTIC_FIELDS)):
        assert [name for _, actual, name in schema if actual == group] == list(expected)


def test_storage_evidence_seal_keeps_crc_length_and_lease_checks(tmp_path):
    source = (ROOT / "components/storage_manager/src/storage_manager.c").read_text(encoding="utf-8")
    def function(name):
        start = source.index("bool " + name + "(")
        end = source.index("\n}", start) + 2
        return source[start:end]
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
enum { STORAGE_MANAGER_WRITE_STATE_RECEIVING, STORAGE_MANAGER_WRITE_STATE_READY,
       STORAGE_MANAGER_WRITE_STATE_QUEUED, STORAGE_MANAGER_WRITE_STATE_FAILED };
enum { STORAGE_MANAGER_ERROR_RESOURCE_BUSY=1, STORAGE_MANAGER_ERROR_SEQUENCE,
       STORAGE_MANAGER_ERROR_WRITE_FAILED, STORAGE_MANAGER_ERROR_NONE=0 };
enum { STORAGE_MANAGER_JOB_TYPE_FILE_WRITE=1, STORAGE_MANAGER_JOB_STATE_QUEUED=2 };
static struct { uint32_t txn_id,state,received_size,expected_size,expected_crc32,actual_crc32,error,path_hash; bool direct_write; char path[96]; } s_write_snapshot;
static struct { uint32_t storage_error; } s_storage_vector;
static struct { struct { uint32_t id,type,state,error,size,path_hash; bool is_dir; char path[96]; } result; } s_storage_job;
static uint8_t s_write_buffer[16384];
static uint32_t s_next_job_id=1;
static bool job_active;
static bool storage_job_is_active(void) { return job_active; }
static void storage_copy_field(char *dst,size_t capacity,const char *src) { assert(strlen(src)<capacity); strcpy(dst,src); }
static void storage_publish_job_result(void) { assert(s_write_snapshot.state==STORAGE_MANAGER_WRITE_STATE_QUEUED); }
extern uint32_t pota_crc32_compute(const void *,size_t);
static uint32_t ota_crc32_compute(const void *p,size_t n) { return pota_crc32_compute(p,n); }
''' + function("storage_manager_commit_file_write") + "\n" + function("storage_manager_finish_evidence_write") + "\n" + function("storage_manager_copy_evidence_write") + r'''
int main(void) {
    uint8_t copy[4]={0}; uint32_t job=0;
    memcpy(s_write_buffer,"test",4);
    s_write_snapshot.txn_id=11; s_write_snapshot.state=STORAGE_MANAGER_WRITE_STATE_RECEIVING;
    s_write_snapshot.received_size=4; s_write_snapshot.expected_size=16384;
    strcpy(s_write_snapshot.path,"/logs/a.bin");
    assert(!storage_manager_finish_evidence_write(11,0,&job)); /* upload is not a local lease */
    s_write_snapshot.direct_write=true;
    assert(!storage_manager_copy_evidence_write(10,0,copy,4));
    assert(!storage_manager_copy_evidence_write(11,UINT32_MAX,copy,4));
    assert(!storage_manager_copy_evidence_write(11,3,copy,4));
    assert(storage_manager_copy_evidence_write(11,0,copy,4) && memcmp(copy,"test",4)==0);
    assert(!storage_manager_finish_evidence_write(10,0,&job));
    assert(!storage_manager_finish_evidence_write(11,0,&job)); /* CRC still enforced */
    assert(s_write_snapshot.state==STORAGE_MANAGER_WRITE_STATE_FAILED);
    s_write_snapshot.state=STORAGE_MANAGER_WRITE_STATE_RECEIVING;
    job_active=true;
    uint32_t crc=ota_crc32_compute(s_write_buffer,4);
    assert(!storage_manager_finish_evidence_write(11,crc,&job));
    assert(s_write_snapshot.state==STORAGE_MANAGER_WRITE_STATE_READY);
    job_active=false;
    assert(storage_manager_finish_evidence_write(11,crc,&job));
    assert(job==1 && s_storage_job.result.size==4);
    assert(s_write_snapshot.actual_crc32==crc && s_write_snapshot.expected_size==4);
    assert(!storage_manager_copy_evidence_write(11,0,copy,4)); /* queued writer owns it */
    assert(!storage_manager_finish_evidence_write(11,crc,&job));
}
'''
    path = tmp_path / "storage.c"
    path.write_text(harness, encoding="utf-8")
    executable = tmp_path / "storage.exe"
    compiler = shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(path),
                    "-I", str(ROOT / "third_party/portable_ota/include"),
                    str(ROOT / "third_party/portable_ota/src/pota_crc32.c"),
                    "-o", str(executable)], check=True, capture_output=True, text=True)
    subprocess.run([str(executable)], check=True, capture_output=True, text=True)
