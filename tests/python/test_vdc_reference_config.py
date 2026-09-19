"""Exercise the actual Core0 reference control fragment with owner/gate stubs."""
from pathlib import Path
import os
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def reference_config_host(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-reference-config")
    source = directory / "reference-config.c"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "host C compiler required"
    executable = directory / ("reference-config.exe" if os.name == "nt" else "reference-config")
    includes = [ROOT, ROOT / "components/product_config/inc", ROOT / "components/vdc_dpll_manager/inc",
                ROOT / "components/sync_io/inc"]
    result = subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                             *["-I" + str(p) for p in includes], str(source), "-o", str(executable)],
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("mode", ["discipline", "discipline_config", "discipline_gates",
    "discipline_persistence", "lifecycle", "snapshots", "timeout", "gates", "persistence", "boot"])
def test_real_reference_config_owner(reference_config_host, mode):
    result = subprocess.run([str(reference_config_host), mode], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "product_config.h"
#include "vdc_reference.h"
#include "sync_io_reference_math.h"
#undef assert
#define assert(x) do { if(!(x)){fprintf(stderr,"CHECK %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static unsigned core,session,metadata_calls,maintenance_calls,prepare_calls,cancel_calls,release_calls,read_calls,write_calls;
static bool stopped=true,gate_ok=true,maintenance_ok=true,inside_gate,inside_maintenance;
static bool backend_ok=true,snapshot_ok=true,release_ok=true,read_ok=true,write_ok=true;
static unsigned backend_generation;
static sync_io_reference_snapshot_t backend;
static product_config_vdc_reference_profile_t stored={2,1,1234000,500,1500};
static product_config_vdc_reference_discipline_profile_t discipline_stored={50,8,9000};
static unsigned discipline_read_calls,discipline_write_calls;
static bool discipline_read_ok=true,discipline_write_ok=true;
static int service;
static void *s_vdc_tdma_service=&service;
static unsigned get_core_num(void){return core;}
static unsigned vdc_dpll_manager_feedback_session(void){return session;}
static bool tdma_service_update_stopped_metadata(void *owner,bool(*cb)(void *),void *context){
    assert(owner==&service && core==0 && !inside_gate);++metadata_calls;
    if(!stopped || !gate_ok)return false;
    inside_gate=true;
    sync_io_reference_config_t ignored;
    assert(!vdc_dpll_manager_get_reference_config(&ignored)); /* nested calls cannot enter */
    const bool ok=cb(context);inside_gate=false;return ok;
}
static bool tdma_service_run_stopped_maintenance(void *owner,bool(*cb)(void *),void *context){
    assert(owner==&service && core==0 && !inside_maintenance);++maintenance_calls;
    if(!stopped || !maintenance_ok)return false;
    inside_maintenance=true;const bool ok=cb(context);inside_maintenance=false;return ok;
}
bool sync_io_reference_config_valid(const sync_io_reference_config_t *p){
    uint32_t periods;return sync_io_reference_validate(p,&periods);
}
bool sync_io_reference_prepare(const sync_io_reference_config_t *p,uint32_t *generation){
    assert(core==0 && stopped && inside_gate);++prepare_calls;
    if(!backend_ok)return false;
    *generation=++backend_generation;
    backend=(sync_io_reference_snapshot_t){.schema=SYNC_IO_REFERENCE_SCHEMA,
        .state=SYNC_IO_REFERENCE_PREPARED,.generation=*generation,.config=*p};
    return true;
}
void sync_io_reference_cancel(void){assert(core==0);++cancel_calls;}
bool sync_io_reference_get_snapshot(sync_io_reference_snapshot_t *p){
    assert(core==0);if(!snapshot_ok)return false;*p=backend;return true;
}
bool sync_io_reference_release(uint32_t generation){
    assert(core==0 && generation==backend.generation && backend.state==SYNC_IO_REFERENCE_RETIRED);
    ++release_calls;if(!release_ok)return false;
    backend=(sync_io_reference_snapshot_t){0};return true;
}
bool product_config_get_vdc_reference_profile(product_config_vdc_reference_profile_t *p){
    assert(core==0);++read_calls;if(!read_ok)return false;*p=stored;return true;
}
bool product_config_set_vdc_reference_profile(const product_config_vdc_reference_profile_t *p){
    assert(core==0 && stopped && inside_maintenance);++write_calls;
    if(!write_ok)return false;
    stored=*p;return true;
}
bool product_config_get_vdc_reference_discipline_profile(product_config_vdc_reference_discipline_profile_t *p){
    assert(core==0);++discipline_read_calls;
    if(!discipline_read_ok)return false;
    *p=discipline_stored;return true;
}
bool product_config_set_vdc_reference_discipline_profile(const product_config_vdc_reference_discipline_profile_t *p){
    assert(core==0 && stopped && inside_maintenance);++discipline_write_calls;
    if(!discipline_write_ok)return false;
    discipline_stored=*p;return true;
}
#include "components/vdc_dpll_manager/src/vdc_reference_config.inc"
static const sync_io_reference_config_t candidate={3,1,2000000,250,1200};
static const sync_io_reference_config_t defaults={4,0,10000000,1000,2500};
static bool same(const sync_io_reference_config_t *a,const sync_io_reference_config_t *b){
    return a->input_port==b->input_port && a->edge==b->edge && a->nominal_hz==b->nominal_hz &&
        a->window_ms==b->window_ms && a->timeout_ms==b->timeout_ms;
}
static const vdc_reference_discipline_config_t discipline_defaults={100,4,10000};
static const vdc_reference_discipline_config_t discipline_candidate={50,8,9000};
static void expect_discipline_config(const vdc_reference_discipline_config_t *expected){
    vdc_reference_discipline_config_t got;
    assert(vdc_dpll_manager_get_reference_discipline_config(&got));
    assert(got.slew_ppb_per_s==expected->slew_ppb_per_s &&
        got.filter_divisor==expected->filter_divisor && got.max_ppb==expected->max_ppb);
}
static void discipline_config(void){
    expect_discipline_config(&discipline_defaults);
    assert(vdc_dpll_manager_set_reference_discipline_config(&discipline_candidate));
    expect_discipline_config(&discipline_candidate);
    assert(!discipline_write_calls && !prepare_calls);
    assert(vdc_dpll_manager_set_reference_enabled(true));
    assert(vdc_dpll_manager_set_reference_discipline(true));
    const uint32_t generation=s_reference_discipline_armed[0];
    const uint32_t crc=s_reference_discipline_armed[1];
    assert(generation && crc && s_reference_discipline_armed[2]==50 &&
        s_reference_discipline_armed[3]==8 && s_reference_discipline_armed[4]==9000);
    assert(!vdc_dpll_manager_set_reference_discipline_config(&discipline_defaults));
    assert(!vdc_dpll_manager_default_reference_discipline());
    assert(!vdc_dpll_manager_recall_reference_discipline());
    assert(!vdc_dpll_manager_store_reference_discipline());
    expect_discipline_config(&discipline_candidate);
    assert(s_reference_discipline_armed[0]==generation && s_reference_discipline_armed[1]==crc);
    assert(vdc_dpll_manager_set_reference_discipline(false));
    assert(vdc_dpll_manager_set_reference_discipline_config(&discipline_defaults));
    assert(s_reference_discipline_armed[0]==generation && s_reference_discipline_armed[1]==crc);
    assert(vdc_dpll_manager_set_reference_discipline(true));
    assert(s_reference_discipline_armed[0]>generation && s_reference_discipline_armed[1]!=crc);
    assert(s_reference_discipline_armed[2]==100 && s_reference_discipline_armed[3]==4);
    assert(vdc_dpll_manager_set_reference_discipline(false));
    const vdc_reference_discipline_config_t limits[]={
        {0,0,0},{0,1,UINT32_MAX},{UINT32_MAX,UINT32_MAX,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(limits)/sizeof(limits[0]);++i){
        assert(vdc_dpll_manager_set_reference_discipline_config(limits+i));
        expect_discipline_config(limits+i);
    }
}
static void discipline_gates(void){
    assert(!vdc_dpll_manager_get_reference_discipline_config(NULL));
    assert(!vdc_dpll_manager_set_reference_discipline_config(NULL));
    for(unsigned mode=0;mode<5;++mode){
        if(mode==0)core=1;
        if(mode==1)s_reference_control_gate=1;
        if(mode==2)stopped=false;
        if(mode==3)s_vdc_tdma_service=NULL;
        if(mode==4){gate_ok=false;maintenance_ok=false;}
        assert(!vdc_dpll_manager_set_reference_discipline_config(&discipline_candidate));
        assert(!vdc_dpll_manager_default_reference_discipline());
        assert(!vdc_dpll_manager_recall_reference_discipline());
        assert(!vdc_dpll_manager_store_reference_discipline());
        if(mode<2){
            vdc_reference_discipline_config_t got=discipline_candidate;
            assert(!vdc_dpll_manager_get_reference_discipline_config(&got));
            assert(got.slew_ppb_per_s==50 && got.filter_divisor==8 && got.max_ppb==9000);
        }
        core=0;s_reference_control_gate=0;stopped=true;s_vdc_tdma_service=&service;
        gate_ok=true;maintenance_ok=true;
        expect_discipline_config(&discipline_defaults);
        assert(!discipline_write_calls && !prepare_calls);
    }
    s_reference_discipline_config_generation=UINT32_MAX;
    assert(!vdc_dpll_manager_set_reference_discipline_config(&discipline_candidate));
    expect_discipline_config(&discipline_defaults);
}
static void discipline_persistence(void){
    assert(vdc_dpll_manager_set_reference_discipline_config(&discipline_defaults));
    const uint32_t generation=s_reference_discipline_config_generation;
    maintenance_ok=false;assert(!vdc_dpll_manager_store_reference_discipline());
    assert(!discipline_write_calls);
    maintenance_ok=true;discipline_write_ok=false;
    assert(!vdc_dpll_manager_store_reference_discipline());
    expect_discipline_config(&discipline_defaults);
    assert(s_reference_discipline_config_generation==generation && discipline_stored.slew_ppb_per_s==50);
    discipline_write_ok=true;assert(vdc_dpll_manager_store_reference_discipline());
    assert(discipline_stored.slew_ppb_per_s==100 && discipline_stored.filter_divisor==4 && discipline_stored.max_ppb==10000);
    assert(vdc_dpll_manager_set_reference_discipline_config(&discipline_candidate));
    discipline_read_ok=false;assert(!vdc_dpll_manager_recall_reference_discipline());
    expect_discipline_config(&discipline_candidate);
    discipline_read_ok=true;assert(vdc_dpll_manager_recall_reference_discipline());
    expect_discipline_config(&discipline_defaults);
    assert(vdc_dpll_manager_default_reference_discipline());
    assert(discipline_write_calls==2 && !write_calls);
    assert(vdc_dpll_manager_set_reference_enabled(true));
    assert(!vdc_dpll_manager_store_reference_discipline());
    assert(vdc_dpll_manager_set_reference_enabled(false));
    assert(!vdc_dpll_manager_store_reference_discipline());
    backend.state=SYNC_IO_REFERENCE_RETIRED;reference_release_core0();
    assert(vdc_dpll_manager_store_reference_discipline());
    assert(discipline_write_calls==3);
}
static void discipline(void){
    assert(!vdc_dpll_manager_set_reference_discipline(true));
    assert(vdc_dpll_manager_set_reference_enabled(true));
    assert(vdc_dpll_manager_set_reference_discipline(true));
    const uint32_t first=s_reference_discipline_request;
    assert((first&1u) && s_reference_discipline_capture_generation==backend.generation);
    stopped=false;assert(!vdc_dpll_manager_set_reference_discipline(true));
    assert(s_reference_discipline_request==first);
    assert(vdc_dpll_manager_set_reference_discipline(false));
    assert(!(s_reference_discipline_request&1u));
    stopped=true;assert(vdc_dpll_manager_set_reference_discipline(true));
    assert(s_reference_discipline_request>first);
    assert(vdc_dpll_manager_set_reference_enabled(false));
    assert(!(s_reference_discipline_request&1u));
    s_reference_enabled=true;s_reference_discipline_request=UINT32_MAX-5u;
    assert(vdc_dpll_manager_set_reference_discipline(true));
    assert(vdc_dpll_manager_set_reference_discipline(false));
    assert(!vdc_dpll_manager_set_reference_discipline(true));
    assert(vdc_dpll_manager_set_reference_discipline(false));
}
static void expect_config(const sync_io_reference_config_t *p){
    sync_io_reference_config_t got;assert(vdc_dpll_manager_get_reference_config(&got));assert(same(&got,p));
}
static vdc_reference_status_t status(void){
    vdc_reference_status_t s;assert(vdc_dpll_manager_get_reference_status(&s));return s;
}
static void lifecycle(void){
    expect_config(&defaults);assert(!status().enabled&&!status().resource_held);
    assert(vdc_dpll_manager_set_reference_config(&candidate));
    const uint32_t installed=status().config_generation;
    assert(installed==2u && !prepare_calls && !write_calls);
    assert(vdc_dpll_manager_set_reference_enabled(true));
    assert(status().enabled&&status().resource_held&&prepare_calls==1);
    assert(!vdc_dpll_manager_set_reference_config(&defaults));
    assert(!vdc_dpll_manager_default_reference());assert(!vdc_dpll_manager_recall_reference());
    assert(!vdc_dpll_manager_store_reference());assert(!vdc_dpll_manager_set_reference_enabled(true));
    expect_config(&candidate);assert(status().config_generation==installed);
    assert(prepare_calls==1&&!write_calls);
    stopped=false;backend.state=SYNC_IO_REFERENCE_RUNNING;
    reference_release_core0();assert(!release_calls);
    assert(vdc_dpll_manager_set_reference_enabled(false));
    assert(cancel_calls==1&&!release_calls&&!status().enabled&&status().resource_held);
    assert(backend.state==SYNC_IO_REFERENCE_RUNNING);
    reference_release_core0();assert(!release_calls);
    backend.state=SYNC_IO_REFERENCE_RETIRED;backend.reason=SYNC_IO_REFERENCE_CANCELLED;
    reference_release_core0();assert(release_calls==1&&!status().resource_held);
    assert(status().monitor.reason==SYNC_IO_REFERENCE_CANCELLED);
    assert(!vdc_dpll_manager_set_reference_config(&defaults));
    stopped=true;assert(vdc_dpll_manager_set_reference_config(&defaults));
    assert(vdc_dpll_manager_set_reference_enabled(false));assert(cancel_calls==1);
}
static void snapshots(void){
    assert(vdc_dpll_manager_set_reference_enabled(true));
    backend.state=SYNC_IO_REFERENCE_RETIRED;backend.reason=SYNC_IO_REFERENCE_OK;
    backend.valid=1;backend.sample_seq=29;backend.frequency_error_ppb=-47;
    backend.completed_raw=UINT64_C(9876543210);
    const sync_io_reference_snapshot_t completed=backend;
    snapshot_ok=false;reference_release_core0();assert(!release_calls);
    vdc_reference_status_t got;memset(&got,0xA5,sizeof(got));
    const vdc_reference_status_t before=got;
    assert(!vdc_dpll_manager_get_reference_status(&got));assert(!memcmp(&before,&got,sizeof(got)));
    snapshot_ok=true;++backend.generation;
    reference_release_core0();assert(!release_calls);
    assert(!vdc_dpll_manager_get_reference_status(&got));assert(!memcmp(&before,&got,sizeof(got)));
    backend=completed;release_ok=false;reference_release_core0();
    assert(release_calls==1&&status().resource_held);
    release_ok=true;reference_release_core0();assert(release_calls==2);
    assert(!status().resource_held&&!status().enabled);
    assert(status().monitor.sample_seq==29&&status().monitor.frequency_error_ppb==-47);
    assert(status().monitor.completed_raw==completed.completed_raw);
    snapshot_ok=false;assert(status().monitor.valid==1);snapshot_ok=true;
    assert(vdc_dpll_manager_default_reference());assert(status().monitor.sample_seq==29);
    assert(vdc_dpll_manager_set_reference_enabled(true));assert(status().monitor.sample_seq==0);
    assert(status().monitor.generation!=completed.generation);
}
static void timeout_result(void){
    assert(vdc_dpll_manager_set_reference_enabled(true));
    stopped=false;backend.state=SYNC_IO_REFERENCE_RETIRED;backend.reason=SYNC_IO_REFERENCE_TIMEOUT;
    backend.valid=0;backend.deadline_raw=UINT64_C(625000000);
    reference_release_core0();const vdc_reference_status_t s=status();
    assert(!s.enabled&&!s.resource_held&&s.monitor.reason==SYNC_IO_REFERENCE_TIMEOUT);
    assert(!s.monitor.valid&&s.monitor.deadline_raw==UINT64_C(625000000));
    assert(!cancel_calls&&release_calls==1&&!write_calls);
}
static void gates(void){
    assert(!vdc_dpll_manager_get_reference_config(NULL));assert(!vdc_dpll_manager_get_reference_status(NULL));
    assert(!vdc_dpll_manager_set_reference_config(NULL));
    sync_io_reference_config_t bad=candidate;bad.edge=2;
    assert(!vdc_dpll_manager_set_reference_config(&bad));assert(metadata_calls==0);
    for(unsigned mode=0;mode<4;++mode){
        if(mode==0)core=1;
        if(mode==1)s_reference_control_gate=1;
        if(mode==2)stopped=false;
        if(mode==3)s_vdc_tdma_service=NULL;
        assert(!vdc_dpll_manager_set_reference_config(&candidate));
        assert(!vdc_dpll_manager_set_reference_enabled(true));
        assert(!vdc_dpll_manager_recall_reference());assert(!vdc_dpll_manager_default_reference());
        assert(!vdc_dpll_manager_store_reference());
        if(mode<2){assert(!vdc_dpll_manager_set_reference_enabled(false));reference_release_core0();}
        core=0;s_reference_control_gate=0;stopped=true;s_vdc_tdma_service=&service;
        expect_config(&defaults);assert(!prepare_calls&&!write_calls&&!release_calls);
    }
    gate_ok=false;assert(!vdc_dpll_manager_set_reference_config(&candidate));
    assert(!vdc_dpll_manager_set_reference_enabled(true));gate_ok=true;
    backend_ok=false;assert(!vdc_dpll_manager_set_reference_enabled(true));
    assert(!status().resource_held&&!status().enabled);backend_ok=true;
    s_reference_config_generation=UINT32_MAX;
    assert(!vdc_dpll_manager_set_reference_config(&candidate));expect_config(&defaults);
    assert(status().config_generation==UINT32_MAX);
}
static void persistence(void){
    assert(vdc_dpll_manager_set_reference_config(&candidate));
    const unsigned generation=status().config_generation;
    maintenance_ok=false;assert(!vdc_dpll_manager_store_reference());assert(!write_calls);
    maintenance_ok=true;write_ok=false;assert(!vdc_dpll_manager_store_reference());
    expect_config(&candidate);assert(stored.input_port==2&&status().config_generation==generation);
    write_ok=true;assert(vdc_dpll_manager_store_reference());assert(stored.input_port==candidate.input_port);
    assert(!status().enabled&&!status().resource_held);
    assert(vdc_dpll_manager_default_reference());expect_config(&defaults);
    read_ok=false;assert(!vdc_dpll_manager_recall_reference());expect_config(&defaults);
    read_ok=true;assert(vdc_dpll_manager_recall_reference());expect_config(&candidate);
    stored.timeout_ms=stored.window_ms;assert(!vdc_dpll_manager_recall_reference());expect_config(&candidate);
    assert(write_calls==2); /* default/recall never persist */
}
static void boot(void){
    assert(reference_init_from_product_config());
    const sync_io_reference_config_t expected={2,1,1234000,500,1500};expect_config(&expected);
    expect_discipline_config(&discipline_candidate);
    assert(!(s_reference_discipline_request&1u) && !discipline_write_calls && discipline_read_calls==1);
    assert(!status().enabled&&!status().resource_held&&!prepare_calls&&!write_calls);
    session=1;const unsigned calls=read_calls;assert(!reference_init_from_product_config());assert(read_calls==calls);
    session=0;core=1;assert(!reference_init_from_product_config());core=0;
    assert(vdc_dpll_manager_set_reference_enabled(true));assert(!reference_init_from_product_config());
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strcmp(argv[1],"discipline"))discipline();
    else if(!strcmp(argv[1],"discipline_config"))discipline_config();
    else if(!strcmp(argv[1],"discipline_gates"))discipline_gates();
    else if(!strcmp(argv[1],"discipline_persistence"))discipline_persistence();
    else if(!strcmp(argv[1],"lifecycle"))lifecycle();
    else if(!strcmp(argv[1],"snapshots"))snapshots();
    else if(!strcmp(argv[1],"timeout"))timeout_result();
    else if(!strcmp(argv[1],"gates"))gates();
    else if(!strcmp(argv[1],"persistence"))persistence();
    else if(!strcmp(argv[1],"boot"))boot();
    else assert(false);
    return 0;
}
'''
def test_reference_lifecycle_is_not_gated_by_optional_capture_load():
    from pathlib import Path
    root = Path(__file__).resolve().parents[2]
    app = (root / "application/src/app.c").read_text(encoding="utf-8")
    phase = app.split("static void app_realtime_tdma_phase(void)", 1)[1].split(
        "static void app_realtime_vdc_phase", 1)[0]
    assert phase.count("sync_io_reference_service_core1();") == 1
    manager = (root / "components/vdc_dpll_manager/src/vdc_dpll_manager.c").read_text(encoding="utf-8")
    assert "sync_io_reference_service_core1();" not in manager
