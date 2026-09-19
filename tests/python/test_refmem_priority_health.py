"""Execute the asynchronous Core0 mirror with the real guarded writer/reader.

Only source snapshots and backing addresses are faked. Source health remains
a sampled diagnostic, independently versioned and outside Core1 work.
"""
import json
import os
import shutil
import subprocess

import pytest

from test_vdc_publication_generation import ROOT, REFMEM, definition
from test_tdma_priority_rx_scpi import callback


@pytest.fixture(scope="module", params=[(6, False), (6, True), (8, False)])
def mirror_exe(request, tmp_path_factory):
    capacity, short_enums = request.param
    directory = tmp_path_factory.mktemp(f"refmem-health-{capacity}-{int(short_enums)}")
    source = PRELUDE
    source += '#define __atomic_load_n guarded_load\n#include "distributed_refmem_priority_health.inc"\n#undef __atomic_load_n\n'
    source += SCENARIOS
    unit = directory / "mirror.c"
    unit.write_text(source, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    exe = directory / ("mirror.exe" if os.name == "nt" else "mirror")
    includes = [ROOT / "tests/unit/host_stubs", ROOT / "config", ROOT / "boards/rp2350_trig/inc",
                ROOT / "components/distributed_refmem/src", *sorted((ROOT / "components").glob("*/inc"))]
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={capacity}", *(["-fshort-enums"] if short_enums else []),
               *[f"-I{p}" for p in includes], str(unit),
               str(ROOT / "components/distributed_refmem/src/refmem_vector_table.c"), "-o", str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.json").write_text(json.dumps(dict(command=command, returncode=result.returncode,
        stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


CASES = ["publish", "health_aging", "retired", "new_request", "disabled", "uninitialized",
         "zero_source", "dedup", "wrap", "layout", "preserve_legacy", "odd_guard", "raced_guard",
         "bad_schema", "bad_size", "bad_writer", "odd_revision", "bad_reserved", "bad_source_reserved",
         "bad_source_schema", "bad_state", "bad_checksum", "null", "failed_source"]


@pytest.mark.parametrize("case", CASES)
def test_refmem_priority_mirror(mirror_exe, case):
    result = subprocess.run([str(mirror_exe), case], capture_output=True, text=True, timeout=5)
    (mirror_exe.parent / f"run-{case}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "refmem_vector_table.h"
#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr,"assertion failed at %s:%d: %s\n",__FILE__,__LINE__,#condition); exit(1); } } while(0)
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) name
static refmem_vector_table_t table;
static bool s_initialized=true, health_ok=true;
static vdc_priority_follow_health_t health;
static uint32_t health_revision=2u, guard_reads, race_at;
static refmem_vdc_vector_region_t *distributed_refmem_vdc_vector_region(void) { return &table.vdc; }
bool vdc_dpll_manager_get_priority_follow_health_revision(vdc_priority_follow_health_t *out,uint32_t *revision) {
    if(!health_ok) return false;
    *out=health; *revision=health_revision; return true;
}
static uint32_t guarded_load(const uint32_t *address,int order) {
    (void)order;
    if(address==&table.vdc.priority.guard && ++guard_reads==race_at) table.vdc.priority.guard+=2u;
    return *address;
}
'''


SCENARIOS = r'''
static void init(void) {
    health=(vdc_priority_follow_health_t){.schema=1u,.state=VDC_PRIORITY_HEALTH_FRESH,
        .request=3u,.session=5u,.generation=7u,.event_sequence=11u,.tick_hz=250000000u,
        .age_us=500u,.freshness_limit_us=120000u,.accepted=17u,.raw_lo=UINT64_C(0x123456789),
        .raw_hi=UINT64_C(0x1234567ff)};
}
static uint32_t sample(void) {
    const uint32_t old=table.vdc.priority.guard;
    const uint32_t vg=table.vdc.seqlock, dg=table.dpll.seqlock;
    distributed_refmem_publish_priority_health_core0();
    assert(vg==table.vdc.seqlock && dg==table.dpll.seqlock);
    return old!=table.vdc.priority.guard;
}
static refmem_vdc_priority_payload_t read_health(void) {
    refmem_vdc_priority_payload_t value; assert(distributed_refmem_get_vdc_priority_snapshot(&value));
    assert(value.schema==1u && value.writer==0u && value.payload_bytes==96u);
    return value;
}
static void same_health(void) {
    const refmem_vdc_priority_payload_t value=read_health();
    assert(value.source_revision==health_revision);
    assert(!memcmp(value.source_words,&health,sizeof(health)));
}
static void reject_read(void) {
    refmem_vdc_priority_payload_t sentinel,out; memset(&sentinel,0xa5,sizeof(sentinel)); out=sentinel;
    assert(!distributed_refmem_get_vdc_priority_snapshot(&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
}
static void reseal(refmem_vdc_priority_payload_t *p) {
    p->payload_crc32=refmem_vector_fast_crc32(p,offsetof(refmem_vdc_priority_payload_t,payload_crc32));
    memcpy(table.vdc.priority.words,p,sizeof(*p));
}
int main(int argc,char **argv) {
    assert(argc==2); const char *name=argv[1]; init();
    if(!strcmp(name,"publish")) {
        assert(sample()); same_health(); assert(!sample());
    } else if(!strcmp(name,"health_aging") || !strcmp(name,"retired") ||
              !strcmp(name,"new_request") || !strcmp(name,"disabled")) {
        assert(sample()); health_revision+=2u;
        if(!strcmp(name,"health_aging")) { health.state=VDC_PRIORITY_HEALTH_STALE;
            health.age_us=120001u; ++health.stale_transitions; }
        else if(!strcmp(name,"retired")) { health.state=VDC_PRIORITY_HEALTH_RETIRED; health.reason=12u; }
        else if(!strcmp(name,"disabled")) { health.state=VDC_PRIORITY_HEALTH_DISABLED; }
        else { health=(vdc_priority_follow_health_t){.schema=1u,
            .state=VDC_PRIORITY_HEALTH_WAITING,.request=31u,.session=51u,.generation=71u}; }
        assert(sample()); same_health(); assert(!sample());
    } else if(!strcmp(name,"uninitialized")) {
        assert(sample()); s_initialized=false; reject_read();
    } else if(!strcmp(name,"zero_source")) {
        health.schema=0u; assert(!sample()); reject_read(); assert(!table.vdc.priority.guard);
    } else if(!strcmp(name,"dedup")) {
        assert(sample()); const uint32_t guard=table.vdc.priority.guard;
        for(unsigned i=0;i<8u;++i) assert(!sample());
        assert(table.vdc.priority.guard==guard); same_health();
    } else if(!strcmp(name,"wrap")) {
        health_revision=UINT32_MAX-1u; assert(sample()); same_health();
        table.vdc.priority.guard=UINT32_MAX-1u; health_revision=0u; health.age_us++;
        assert(sample() && !table.vdc.priority.guard); same_health(); assert(!sample());
        health_revision=2u; health.age_us++; assert(sample()); same_health();
    } else if(!strcmp(name,"layout")) {
        assert(sizeof(table.vdc)==1024u && sizeof(table.dpll)==1024u && sizeof(table)==DISTRIBUTED_REFMEM_TABLE_SIZE);
        assert(offsetof(refmem_vdc_vector_region_t,priority)==512u);
        assert(sizeof(refmem_vdc_priority_region_t)==104u && sizeof(refmem_vdc_priority_payload_t)==96u);
        assert(REFMEM_VDC_VECTOR_LAYOUT_VERSION==1u && REFMEM_DPLL_VECTOR_LAYOUT_VERSION==1u);
        assert(offsetof(refmem_vdc_vector_region_t,payload)==8u && offsetof(refmem_dpll_vector_region_t,payload)==8u);
        assert(offsetof(refmem_vdc_vector_region_t,reserved)<512u);
    } else if(!strcmp(name,"preserve_legacy")) {
        memset(&table,0xa5,sizeof(table)); memset(&table.vdc.priority,0,sizeof(table.vdc.priority));
        refmem_vector_table_t saved=table; assert(sample());
        const size_t start=offsetof(refmem_vector_table_t,vdc)+REFMEM_VDC_PRIORITY_OFFSET;
        const size_t end=start+sizeof(table.vdc.priority);
        assert(!memcmp(&table,&saved,start));
        assert(!memcmp((uint8_t *)&table+end,(uint8_t *)&saved+end,sizeof(table)-end)); same_health();
    } else if(!strcmp(name,"failed_source")) {
        assert(sample()); const refmem_vdc_priority_region_t saved=table.vdc.priority;
        health_ok=false; health_revision+=2u; health.state=VDC_PRIORITY_HEALTH_RETIRED;
        assert(!sample()); assert(!memcmp(&saved,&table.vdc.priority,sizeof(saved)));
        health_ok=true; assert(sample()); same_health();
    } else {
        assert(sample());
        if(!strcmp(name,"null")) { assert(!distributed_refmem_get_vdc_priority_snapshot(NULL)); }
        else if(!strcmp(name,"odd_guard")) { ++table.vdc.priority.guard; reject_read(); }
        else if(!strcmp(name,"raced_guard")) { guard_reads=0u; race_at=2u; reject_read(); }
        else {
            refmem_vdc_priority_payload_t p=read_health();
            if(!strcmp(name,"bad_schema")) ++p.schema;
            else if(!strcmp(name,"bad_size")) p.payload_bytes-=4u;
            else if(!strcmp(name,"bad_writer")) p.writer=1u;
            else if(!strcmp(name,"odd_revision")) p.source_revision|=1u;
            else if(!strcmp(name,"bad_reserved")) p.reserved=1u;
            else if(!strcmp(name,"bad_source_reserved")) p.source_words[13]=1u;
            else if(!strcmp(name,"bad_source_schema")) p.source_words[0]=2u;
            else if(!strcmp(name,"bad_state")) p.source_words[1]=VDC_PRIORITY_HEALTH_RETIRED+1u;
            else if(!strcmp(name,"bad_checksum")) p.source_words[10]^=1u;
            else assert(!"unknown mirror scenario");
            if(strcmp(name,"bad_checksum")) reseal(&p); else memcpy(table.vdc.priority.words,&p,sizeof(p));
            reject_read();
        }
    }
    printf("RefMem typed health passed: %s\n",name); return 0;
}
'''


def test_diagnostic_mirror_has_no_realtime_reservation():
    source = REFMEM.read_text(encoding="utf-8")
    realtime = definition(source, "distributed_refmem_realtime_run_once")
    scheduler = definition(source, "distributed_refmem_publish_runtime_vector_if_pending")
    background = definition(source, "distributed_refmem_service")
    assert "priority_health" not in realtime
    assert "priority_health" not in scheduler
    assert "s_next_runtime_vector = 2" not in scheduler
    assert background.count("distributed_refmem_publish_priority_health_core0();") == 1
    assert background.index("osal_critical_exit();") < background.index("distributed_refmem_publish_priority_health_core0();")
    assert background.index("if (ota_ao_is_active())") < background.index("distributed_refmem_publish_priority_health_core0();")
    manager = (ROOT / "components/vdc_dpll_manager/src/vdc_dpll_manager.c").read_text(encoding="utf-8")
    assert "distributed_refmem_publish_priority_health" not in manager


@pytest.fixture(scope="module")
def mirror_boundary_exe(tmp_path_factory):
    directory = tmp_path_factory.mktemp("refmem-health-boundaries")
    follow = (ROOT / "components/vdc_dpll_manager/src/vdc_priority_follow.inc").read_text(encoding="utf-8")
    scpi = (ROOT / "middleware/scpi_port/src/scpi_sync_commands.c").read_text(encoding="utf-8")
    source = BOUNDARY_PRELUDE + "\n#define __atomic_load_n revision_load\n"
    source += definition(follow, "vdc_dpll_manager_get_priority_follow_health_revision")
    source += "\n#undef __atomic_load_n\n"
    source += callback(scpi, "scpi_cmd_refmem_vdc_priority_q") + BOUNDARY_MAIN
    unit = directory / "boundaries.c"
    unit.write_text(source, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    exe = directory / ("boundaries.exe" if os.name == "nt" else "boundaries")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               *[f"-I{p}" for p in sorted((ROOT / "components").glob("*/inc"))],
               str(unit), "-o", str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.json").write_text(json.dumps(dict(command=command, returncode=result.returncode,
        stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.mark.parametrize("case", ["revision_odd", "revision_changed", "revision_zero",
    "revision_normal", "revision_null_out", "revision_null_revision", "scpi_enabled",
    "scpi_adapter", "scpi_ring_unavailable", "scpi_snapshot_unavailable", "scpi_fields"])
def test_health_revision_and_scpi_boundary(mirror_boundary_exe, case):
    result = subprocess.run([str(mirror_boundary_exe), case], capture_output=True, text=True, timeout=5)
    (mirror_boundary_exe.parent / f"run-{case}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


BOUNDARY_PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "refmem_vdc_vector.h"
static uint32_t s_priority_health_guard=8u;
static uint32_t s_priority_health_words[sizeof(vdc_priority_follow_health_t)/4u];
static uint32_t revision_reads, revision_race;
static void match_load_words(const uint32_t *words,void *out,size_t bytes) { memcpy(out,words,bytes); }
static uint32_t revision_load(const uint32_t *address,int order) {
    (void)order;
    assert(address==&s_priority_health_guard);
    if(++revision_reads==revision_race) s_priority_health_guard+=2u;
    return *address;
}
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool ring_ok=true, mirror_ok=true;
static tdma_ring_clock_snapshot_t ring;
static refmem_vdc_priority_payload_t mirror;
static uint32_t fields[24], results, errors, mirror_gets;
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out) {
    *out=ring; return ring_ok;
}
static bool distributed_refmem_get_vdc_priority_snapshot(refmem_vdc_priority_payload_t *out) {
    ++mirror_gets; *out=mirror; return mirror_ok;
}
static void SCPI_ResultUInt32(scpi_t *c,uint32_t value) {
    (void)c; assert(results<24u); fields[results++]=value;
}
static void scpi_port_push_exec_error(scpi_t *c,const char *message) {
    (void)c; assert(message && *message); ++errors;
}
'''


BOUNDARY_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==2); const char *name=argv[1];
    if(!strncmp(name,"revision_",9u)) {
        for(unsigned i=0;i<sizeof(s_priority_health_words)/4u;++i) s_priority_health_words[i]=100u+i;
        vdc_priority_follow_health_t out, sentinel; memset(&sentinel,0xa5,sizeof(sentinel)); out=sentinel;
        uint32_t revision=0x12345678u;
        if(!strcmp(name,"revision_odd")) s_priority_health_guard|=1u;
        if(!strcmp(name,"revision_changed")) revision_race=2u;
        if(!strcmp(name,"revision_zero")) s_priority_health_guard=0u;
        bool ok;
        if(!strcmp(name,"revision_null_out")) ok=vdc_dpll_manager_get_priority_follow_health_revision(NULL,&revision);
        else if(!strcmp(name,"revision_null_revision")) ok=vdc_dpll_manager_get_priority_follow_health_revision(&out,NULL);
        else ok=vdc_dpll_manager_get_priority_follow_health_revision(&out,&revision);
        if(!strcmp(name,"revision_zero") || !strcmp(name,"revision_normal")) {
            assert(ok && revision==s_priority_health_guard);
            assert(!memcmp(&out,s_priority_health_words,sizeof(out)));
        } else {
            assert(!ok && revision==0x12345678u && !memcmp(&out,&sentinel,sizeof(out)));
        }
    } else {
        scpi_t context=0;
        if(!strcmp(name,"scpi_enabled")) ring.enabled=1u;
        else if(!strcmp(name,"scpi_adapter")) ring.adapter_started=1u;
        else if(!strcmp(name,"scpi_ring_unavailable")) ring_ok=false;
        else if(!strcmp(name,"scpi_snapshot_unavailable")) mirror_ok=false;
        else assert(!strcmp(name,"scpi_fields"));
        mirror.schema=101u; mirror.payload_bytes=102u; mirror.writer=103u; mirror.source_revision=104u;
        for(unsigned i=0;i<18u;++i) mirror.source_words[i]=105u+i;
        mirror.payload_crc32=123u; mirror.reserved=124u;
        const int result=scpi_cmd_refmem_vdc_priority_q(&context);
        if(!strcmp(name,"scpi_fields")) {
            assert(result==SCPI_RES_OK && results==24u && !errors && mirror_gets==1u);
            for(unsigned i=0;i<24u;++i) assert(fields[i]==101u+i);
        } else {
            assert(result==SCPI_RES_ERR && !results && errors==1u);
            assert(mirror_gets==(!strcmp(name,"scpi_snapshot_unavailable") ? 1u : 0u));
        }
    }
    printf("RefMem health boundary passed: %s\n",name); return 0;
}
'''
