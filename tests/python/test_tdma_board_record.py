"""Run the production recorder against bounded storage and a controlled board clock."""
import importlib.util
import hashlib
import json
import re
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


@pytest.mark.parametrize("version,words,digest", [
    (1, 375, "cf4955ea815ecb22a6b0b044b9aa8fe835228ef18bf701b1fbbe69a4036e0bf0"),
    (2, 410, "6218599a86dfab094ac86198e991299b2ed9a2bec8d59d4fe69ca8e6e80933b4"),
    (3, 453, "f0a2b616af635195a0dccfba72fa90d33cd29662fd6debca8683cedf1ecccf6c"),
    (4, 477, "c187d56df41ba7944eb675f01324055724ae770510be42ccdb463ae0ae385a86"),
])
def test_historical_schema_order_and_types_are_immutable(version, words, digest):
    # Frozen from c2eab9c / provenance source-r3, independently of the new
    # schema selector. A rename, type change or reordered old field must fail.
    schema = decoder.field_schema(version)
    assert sum(2 if kind == "U64" else 1 for kind, _, _ in schema) == words
    encoded = json.dumps(schema, separators=(",", ":")).encode()
    assert hashlib.sha256(encoded).hexdigest() == digest


@pytest.mark.parametrize("version", [1, 2, 3, 4, 5])
def test_versioned_record_keeps_legacy_evidence_and_observer_values(version):
    schema = decoder.field_schema(version)
    values = []
    for kind, group, name in schema:
        value = 0
        if group == "event":
            value = {"state": 2, "fault_bits": 1, "joined": 9, "published": 8,
                     "rx_elapsed_cycles": 0x123456789ABCDEF}.get(name, 0)
        if group == "candidate":
            value = {"capture_id": 0xDEADBEEF12345678, "query_count": 18,
                     "matched_count": 7, "reason": 1, "flags": 0xFF,
                     "event_sequence": 0, "event_ordinal": 23,
                     "start_hi_cycles": 0x123456789ABCDEF}.get(name, 0)
        if group == "origin_first":
            value = {"available": 1, "published_version": 2,
                     "epoch": 42, "sequence": 0, "sequence_end": 0,
                     "identity": 0xABCDEFFF, "format": 2,
                     "flags": 0, "rtt_present": 0,
                     "arm_before_hi0": 1, "arm_before_lo": 0xFFFFFFFF,
                     "arm_before_hi1": 2}.get(name, 0)
        if group == "rx_first":
            value = {"available": 1, "schema": 1, "raw_count": 173,
                     "raw_capacity": 243, "raw_reason": 5, "first_reason": 5,
                     "arm_epoch": 0xA123456789ABCDEF, "flags": 0,
                     "raw_word_00": 0xFEDCBA98, "raw_word_76": 0,
                     "event_word_mask": 1, "event_word_0": 0xFFFFFFFF}.get(name, 0)
        values.append(value & 0xFFFFFFFF)
        if kind == "U64":
            values.append(value >> 32)
    count = len(values)
    bitmap = [sum(1 << bit for bit in range(min(32, count - start)))
              for start in range(0, count, 32)]
    header = [decoder.MAGIC, version, 16, count, 1000, 1, 123,
              1, 0, 2, 0, 1000000, len(bitmap), 0, 0, 0]
    packet = [decoder.SAMPLE, 12 + len(bitmap) + count, 0xFFFFFFFF, 0x3F,
              100, 0, 100, 0, 110, 0, 0, 0] + bitmap + values
    body = struct.pack(f"<{len(header + packet)}I", *(header + packet))
    footer = [decoder.END, 16, 1, 0, 0, 1, 100, 0, 110, 0,
              len(body), zlib.crc32(body), 123, 0, 0, 0]
    data = body + struct.pack("<16I", *footer)
    result = decoder.decode_record(data, expected_build=1, expected_board=2, expected_epoch=123)
    assert result["schema"] == f"HAOFV_TDMA_BOARD_RECORD_V{version}"
    snapshot = result["baseline"]["snapshot"]
    if version == 1:
        assert "event" not in snapshot
    else:
        assert snapshot["event"]["rx_elapsed_cycles"] == 0x123456789ABCDEF
        assert snapshot["event"]["published"] == 8
        assert snapshot["event"]["joined"] == 9
        assert snapshot["event"]["state"] == 2
    if version < 3:
        assert "candidate" not in snapshot
    else:
        candidate = snapshot["candidate"]
        assert candidate["capture_id"] == 0xDEADBEEF12345678
        assert candidate["query_count"] == 18 and candidate["matched_count"] == 7
        assert candidate["event_sequence"] == 0 and candidate["event_ordinal"] == 23
        assert candidate["start_hi_cycles"] == 0x123456789ABCDEF
        assert candidate["flags"] == 0xFF  # Retired historical match, no authority bits.
    if version < 4:
        assert "origin_first" not in snapshot
    else:
        first = snapshot["origin_first"]
        assert first["available"] == 1 and first["epoch"] == 42
        assert first["published_version"] == 2 and first["format"] == 2
        assert first["sequence"] == first["sequence_end"] == 0
        assert first["identity"] == 0xABCDEFFF
        assert first["flags"] == first["rtt_present"] == 0
        assert first["arm_before_hi0"] == 1 and first["arm_before_hi1"] == 2
    if version < 5:
        assert "rx_first" not in snapshot
    else:
        rx = snapshot["rx_first"]
        assert rx["available"] == 1 and rx["flags"] == 0
        assert rx["raw_count"] == 173 and rx["raw_capacity"] == 243
        assert rx["arm_epoch"] == 0xA123456789ABCDEF
        assert rx["first_reason"] == rx["raw_reason"] == 5
        assert rx["event_word_mask"] == 1 and rx["event_word_0"] == 0xFFFFFFFF
        assert rx["raw_word_00"] == 0xFEDCBA98 and rx["raw_word_76"] == 0
    # Merely relabelling a file cannot reinterpret a different-sized schema.
    foreign = bytearray(data)
    struct.pack_into("<I", foreign, 4, version % 3 + 1)
    struct.pack_into("<I", foreign, len(body) + 44, zlib.crc32(foreign[:len(body)]))
    with pytest.raises(ValueError, match="unknown record schema"):
        decoder.decode_record(foreign)


@pytest.mark.parametrize("capacity", [6, 8])
def test_app_first_archive_retirement_is_encoded_in_native_deltas(tmp_path, capacity):
    # Execute the actual application snapshot body plus production recorder.
    # Unrelated health getters are mocked; only their field expressions are
    # replaced with zero. The new group's expressions and raw TDMA types are
    # compiled unchanged, so a failed final guard copy must really be cleared.
    app = (ROOT / "application/src/app_tdma_record.c").read_text(encoding="utf-8")
    start = app.index("static uint32_t app_record_rx_word(")
    end = app.index("\n}\n", app.index("static uint32_t app_record_snapshot(")) + 3
    snapshot_body = app[start:end].replace('"diagnostics_tdma_record_fields.def"',
                                            '"test_record_fields.def"')
    fields = decoder.FIELDS_PATH.read_text(encoding="utf-8")
    fields = re.sub(r"^(RECORD_(?:U32|I32|U64)\((\w+), \w+, ).*$",
                    lambda m: m[0] if m[2] in ("origin_first", "rx_first") else m[1] + "0u)",
                    fields, flags=re.M)
    (tmp_path / "test_record_fields.def").write_text(fields, encoding="utf-8")
    mocks = r'''
#include "tdma_origin_plan.h"
#include "tdma_rx_first_window.h"
typedef struct { unsigned unused; } tdma_ring_runtime_snapshot_t;
typedef tdma_ring_runtime_snapshot_t tdma_pio_spi_ring_adapter_snapshot_t;
typedef tdma_ring_runtime_snapshot_t tdma_pio_spi_phys_snapshot_t;
typedef tdma_ring_runtime_snapshot_t app_realtime_schedule_snapshot_t;
typedef tdma_ring_runtime_snapshot_t tdma_service_service_t;
typedef tdma_ring_runtime_snapshot_t tdma_pio_spi_ring_adapter_t;
enum { APP_REALTIME_PHASE_COUNT=10 };
static tdma_service_service_t owner;
static tdma_pio_spi_ring_adapter_t adapter;
static tdma_service_service_t *tdma_runtime_owner_get(void) { return &owner; }
static tdma_pio_spi_ring_adapter_t *tdma_runtime_owner_get_ring_adapter(void) { return &adapter; }
static bool tdma_runtime_owner_get_ring_snapshot(void *p) { (void)p; return true; }
static bool tdma_service_get_flight_engine_snapshot(void *p,void *v) { (void)p;(void)v;return true; }
static bool tdma_service_get_flight_fifo_snapshot(void *p,void *v) { (void)p;(void)v;return true; }
static bool tdma_runtime_owner_get_phys_snapshot(void *p) { (void)p;return true; }
static bool tdma_pio_spi_ring_adapter_get_snapshot(void *p,void *v) { (void)p;(void)v;return true; }
static bool app_realtime_get_schedule_snapshot(void *p) { (void)p;return true; }
static bool tdma_runtime_owner_get_origin_first_record(tdma_origin_first_record_t *p) {
    unsigned n=reads++; now+=10u;
    memset(p,0xA5,sizeof(*p));
    if(n==0 || n==2) return false; /* Output touched before failed revalidation. */
    memset(p,0,sizeof(*p));
    p->published_version=2;
    p->record.epoch=n==1 ? 101 : 102;
    p->record.observation.sequence=n==1 ? 0 : 7;
    p->record.sequence_end=p->record.observation.sequence;
    p->record.observation.identity=n==1 ? 0xAABBCCDD : 0x11223344;
    p->record.format=TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME;
    p->record.raw_time.arm_before[0]=1;
    p->record.raw_time.arm_before[1]=UINT32_MAX;
    p->record.raw_time.arm_before[2]=2;
    return true; /* No transport/edge success: still retain this raw record. */
}
static bool tdma_runtime_owner_get_rx_first_window(tdma_rx_first_window_t *p) {
    const unsigned n=reads-1u;
    memset(p,0xA5,sizeof(*p));
    if(n==0u || n==2u) return false;
    memset(p,0,sizeof(*p));
    p->schema=1u;
    p->arm_epoch=n==1u ? UINT64_C(0x100000065) : UINT64_C(0x200000066);
    p->observation_epoch=UINT64_C(0x123456789abcdef);
    p->raw_count=TDMA_RX_FIRST_WINDOW_RAW_CAPACITY;
    p->first_reason=p->raw_reason=5u; /* Failed raw copy retains attempted bytes. */
    p->event_word_mask=7u; /* Partial tuple stays partial, even in a complete file. */
    p->event_words[0]=UINT32_MAX;
    p->event_words[1]=0x12345678u;
    p->event_words[2]=0xABCDEF98u;
    for(unsigned i=0;i<TDMA_RX_FIRST_WINDOW_RAW_CAPACITY;i++)
        p->raw[i]=(uint8_t)(i*7u+(n==1u ? 1u : 3u));
    return true;
}
'''
    old_start = HARNESS.index("static uint32_t snapshot(")
    old_end = HARNESS.index("\n}\n", old_start) + 3
    source = HARNESS[:old_start] + mocks + snapshot_body + HARNESS[old_end:]
    source = source.replace("save,result,snapshot,clock_now", "save,result,app_record_snapshot,clock_now")
    path = tmp_path / "app_record.c"
    path.write_text(source, encoding="utf-8")
    exe = tmp_path / "app_record.exe"
    includes = ["boards/rp2350_trig/inc", "components/tdma/inc",
                "components/diagnostics/inc", "components/ota_manager/inc",
                "third_party/portable_ota/include"]
    compiler = shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    compiled = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    f"-DPROJECT_NODE_CAPACITY={capacity}", *["-I" + str(ROOT / p) for p in includes],
                    str(path), str(ROOT / "third_party/portable_ota/src/pota_crc32.c"),
                    str(ROOT / "components/diagnostics/src/diagnostics_tdma_record.c"),
                    "-o", str(exe)], capture_output=True, text=True)
    assert compiled.returncode == 0, compiled.stderr
    binary = tmp_path / "native.bin"
    subprocess.run([str(exe), "8", str(binary)], check=True, capture_output=True, text=True)
    record = decoder.decode_record(binary.read_bytes())
    assert record["collection_passed"]
    assert record["schema"] == "HAOFV_TDMA_BOARD_RECORD_V5"
    first = [s["snapshot"]["origin_first"] for s in [record["baseline"], *record["samples"]]]
    assert [s["available"] for s in first] == [0, 1, 0, 1, 1]
    assert all(value == 0 for i in (0, 2) for value in first[i].values())
    assert [s["epoch"] for s in first] == [0, 101, 0, 102, 102]
    assert first[1]["sequence"] == first[1]["sequence_end"] == 0
    assert first[1]["identity"] == 0xAABBCCDD
    assert first[3] == first[4] and first[3]["sequence"] == 7
    assert first[3]["flags"] == first[3]["rtt_present"] == 0
    rx = [s["snapshot"]["rx_first"] for s in [record["baseline"], *record["samples"]]]
    assert [s["available"] for s in rx] == [0, 1, 0, 1, 1]
    assert all(value == 0 for i in (0, 2) for value in rx[i].values())
    assert rx[1]["arm_epoch"] == 0x100000065
    assert rx[3]["arm_epoch"] == 0x200000066 and rx[3] == rx[4]
    for i, seed in ((1, 1), (3, 3)):
        sample = rx[i]
        assert sample["observation_epoch"] == 0x123456789ABCDEF
        assert sample["raw_count"] == sample["raw_capacity"]
        assert sample["first_reason"] == sample["raw_reason"] == 5
        assert sample["flags"] == 0 and sample["event_word_mask"] == 7
        assert [sample[f"event_word_{n}"] for n in range(5)] == [
            0xFFFFFFFF, 0x12345678, 0xABCDEF98, 0, 0]
        raw = struct.pack("<77I", *(sample[f"raw_word_{n:02d}"] for n in range(77)))
        assert raw[:sample["raw_count"]] == bytes(
            (n * 7 + seed) & 255 for n in range(sample["raw_count"]))
        assert raw[sample["raw_count"]:] == bytes(len(raw) - sample["raw_count"])


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
