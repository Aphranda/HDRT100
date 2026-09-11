"""Run App's actual export routing prefix with controlled StorageAO outcomes."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def test_pending_legacy_batch_keeps_buffer_until_storage_done(tmp_path):
    source = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    prefix = source.split("static void app_analyzer_storage_service(void)\n{", 1)[1]
    prefix = prefix.split("    sync_io_logic_analyzer_status_t analyzer;", 1)[0]
    harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
enum { STORAGE_MANAGER_JOB_STATE_BUSY, STORAGE_MANAGER_JOB_STATE_DONE,
       STORAGE_MANAGER_JOB_STATE_FAILED };
typedef struct { uint32_t id; int state; } storage_manager_job_result_t;
static bool s_analyzer_storage_job_inflight, s_analyzer_storage_pending;
static uint32_t s_analyzer_storage_job_id, s_analyzer_storage_segment_index;
static unsigned burst_calls, legacy_calls;
static storage_manager_job_result_t result;
static void storage_manager_get_job_result(storage_manager_job_result_t *p) { *p=result; }
static bool app_analyzer_burst_storage_service(void) { ++burst_calls; return true; }
static void service(void) {
''' + prefix + r'''
    ++legacy_calls;
}
int main(void) {
    /* Legacy data has been drained from the owner but write admission was busy. */
    s_analyzer_storage_pending=true;
    service();
    assert(legacy_calls==1 && burst_calls==0);
    /* The later write is accepted: protect the union while its job runs. */
    s_analyzer_storage_pending=false;
    s_analyzer_storage_job_inflight=true;
    s_analyzer_storage_job_id=11;
    result=(storage_manager_job_result_t){11, STORAGE_MANAGER_JOB_STATE_BUSY};
    service();
    assert(legacy_calls==1 && burst_calls==0);
    /* A different completed job cannot release our buffer. */
    result=(storage_manager_job_result_t){12, STORAGE_MANAGER_JOB_STATE_DONE};
    service();
    assert(s_analyzer_storage_job_inflight && burst_calls==0);
    /* Failure returns to the legacy retry path with the original data intact. */
    result=(storage_manager_job_result_t){11, STORAGE_MANAGER_JOB_STATE_FAILED};
    service();
    assert(s_analyzer_storage_pending && legacy_calls==2 && burst_calls==0);
    s_analyzer_storage_pending=false;
    s_analyzer_storage_job_inflight=true;
    result=(storage_manager_job_result_t){11, STORAGE_MANAGER_JOB_STATE_DONE};
    service();
    assert(!s_analyzer_storage_job_inflight && burst_calls==1);
    assert(s_analyzer_storage_segment_index==1);
}
'''
    path = tmp_path / "handoff.c"
    path.write_text(harness, encoding="utf-8")
    gcc = shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "handoff.exe"
    subprocess.run([gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(path),
                    "-o", str(executable)], check=True, capture_output=True, text=True)
    subprocess.run([str(executable)], check=True, capture_output=True, text=True)


def test_failed_burst_segment_retries_without_advancing_or_releasing(tmp_path):
    source = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    start = source.index("typedef struct __attribute__((packed))", source.index("#define s_analyzer_storage_records"))
    body = source[start:source.index("static void app_analyzer_storage_service(void)")]
    harness = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "sync_io_analyzer_burst.h"
#define STORAGE_MANAGER_FILE_WRITE_MAX_BYTES 16384u
enum { STORAGE_MANAGER_JOB_STATE_RUNNING, STORAGE_MANAGER_JOB_STATE_DONE,
       STORAGE_MANAGER_JOB_STATE_FAILED };
typedef struct { uint32_t id, error; int state; } storage_manager_job_result_t;
typedef struct { uint32_t error; } storage_manager_write_snapshot_t;
static const char *g_project_build_id="20260911135924";
static struct { uint32_t burst_words[SYNC_IO_ANALYZER_BURST_COPY_WORDS]; } s_analyzer_storage_buffer;
static sync_io_analyzer_burst_snapshot_t facts;
static storage_manager_job_result_t job;
static uint32_t commits, releases, retry, failures, failed_job, failed_error;
static uint32_t first_words[16], copies;
static bool commit_ok=true;
bool sync_io_analyzer_burst_get_snapshot(sync_io_analyzer_burst_snapshot_t *p) { *p=facts; return true; }
void sync_io_analyzer_burst_begin_export_core0(uint32_t seq) { assert(seq==facts.capture_sequence); }
void sync_io_analyzer_burst_export_failed_core0(uint32_t id,uint32_t error) {
    ++failures; failed_job=id; failed_error=error;
}
bool sync_io_analyzer_burst_take_export_retry_core0(uint32_t seq) {
    if (retry!=seq) return false;
    retry=0; return true;
}
size_t sync_io_analyzer_burst_copy_core0(uint32_t seq,uint32_t first,uint32_t *words,uint32_t capacity) {
    assert(seq==facts.capture_sequence && first<=facts.captured_words);
    first_words[copies++]=first;
    uint32_t count=facts.captured_words-first;
    if (count>capacity) count=capacity;
    memset(words,0,count*sizeof(*words));
    return count;
}
static bool sync_io_logic_analyzer_request_burst_release(uint32_t seq) {
    assert(seq==facts.capture_sequence && job.state==STORAGE_MANAGER_JOB_STATE_DONE);
    ++releases; return true;
}
static void storage_manager_get_job_result(storage_manager_job_result_t *p) { *p=job; }
static void storage_manager_get_write_snapshot(storage_manager_write_snapshot_t *p) { p->error=77; }
static uint32_t ota_crc32_compute(const uint8_t *p,size_t n) { (void)p;(void)n;return 0; }
static uint32_t ota_crc32_update(uint32_t crc,const uint8_t *p,size_t n) { (void)crc;return ota_crc32_compute(p,n); }
static bool storage_manager_begin_evidence_write(const char *path,uint32_t n,uint32_t crc,uint32_t *txn) {
    (void)path;(void)n;(void)crc;*txn=1;return true;
}
static bool storage_manager_write_file_chunk(uint32_t txn,uint32_t offset,const uint8_t *p,size_t n) {
    (void)txn;(void)offset;(void)p;(void)n;return true;
}
static bool storage_manager_commit_file_write(uint32_t txn,uint32_t *id) {
    (void)txn;if (!commit_ok) return false;
    job=(storage_manager_job_result_t){.id=++commits,.state=STORAGE_MANAGER_JOB_STATE_RUNNING};
    *id=job.id;return true;
}
static bool storage_manager_abort_file_write(uint32_t txn) { (void)txn;return true; }
''' + body + r'''
int main(void) {
    facts=(sync_io_analyzer_burst_snapshot_t){.state=SYNC_IO_ANALYZER_BURST_FROZEN,
        .capture_sequence=42,.capture_tag=9,.captured_words=2050};
    assert(app_analyzer_burst_storage_service());
    assert(commits==1 && releases==0);
    job.state=STORAGE_MANAGER_JOB_STATE_DONE;
    app_analyzer_burst_storage_service();
    assert(commits==2 && s_analyzer_burst_storage.next_word==1024);
    job.state=STORAGE_MANAGER_JOB_STATE_FAILED; job.error=6;
    app_analyzer_burst_storage_service();
    assert(failures==1 && failed_job==2 && failed_error==6);
    app_analyzer_burst_storage_service();
    assert(commits==2 && failures==1 && releases==0);
    retry=41; app_analyzer_burst_storage_service();
    assert(commits==2 && releases==0);
    retry=42; app_analyzer_burst_storage_service();
    assert(commits==3 && failures==1 && releases==0);
    assert(s_analyzer_burst_storage.next_word==1024);
    job.state=STORAGE_MANAGER_JOB_STATE_DONE;
    app_analyzer_burst_storage_service();
    assert(commits==4 && s_analyzer_burst_storage.words==2 && releases==0);
    job.state=STORAGE_MANAGER_JOB_STATE_DONE;
    app_analyzer_burst_storage_service();
    assert(releases==1 && s_analyzer_burst_storage.next_word==2050);
    assert(copies==4 && first_words[0]==0 && first_words[1]==1024 &&
           first_words[2]==1024 && first_words[3]==2048);
    /* Failed commit before a job exists has its own retained error evidence. */
    facts.capture_sequence=43; facts.captured_words=1; commit_ok=false;
    app_analyzer_burst_storage_service();
    assert(failures==2 && failed_job==0 && failed_error==77);
    commit_ok=true; retry=43; app_analyzer_burst_storage_service();
    assert(commits==5 && s_analyzer_burst_storage.next_word==0);
    /* A retry arriving before Core0 notices FAILED still retries that segment. */
    job.state=STORAGE_MANAGER_JOB_STATE_FAILED;job.error=6;retry=43;
    app_analyzer_burst_storage_service();
    assert(commits==6 && failures==3 && s_analyzer_burst_storage.next_word==0);
    job.state=STORAGE_MANAGER_JOB_STATE_DONE;app_analyzer_burst_storage_service();
    assert(releases==2);
    /* A zero-sample timeout exports exactly one header before release. */
    facts.capture_sequence=44;facts.captured_words=0;
    app_analyzer_burst_storage_service();assert(commits==7 && releases==2);
    job.state=STORAGE_MANAGER_JOB_STATE_DONE;app_analyzer_burst_storage_service();
    assert(releases==3);
}
'''
    path = tmp_path / "burst_storage.c"
    path.write_text(harness, encoding="utf-8")
    gcc = shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "burst_storage.exe"
    subprocess.run([gcc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "components/sync_io/inc"), str(path),
                    "-o", str(executable)], check=True, capture_output=True, text=True)
    subprocess.run([str(executable)], check=True, capture_output=True, text=True)
