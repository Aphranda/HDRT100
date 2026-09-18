"""Production config/controller execute with real Domain and external guard stubs."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT/"tests/python"))
import test_vdc_priority_follow as existing


@pytest.fixture(scope="module")
def executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("baseline-config")
    manager = ROOT/"components/vdc_dpll_manager/src"
    physical = (ROOT/"components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    begin = physical.index("enum {\n    TDMA_EVENT_LIVE_RETAINED")
    end = physical.index("} tdma_pio_spi_event_exact_t;", begin)+len("} tdma_pio_spi_event_exact_t;")
    event_types = '#include "tdma_event_history.h"\n'+physical[begin:end]
    matcher = (manager/"vdc_dpll_feedback_match.inc").read_text(encoding="utf-8")
    source_type = re.search(r'typedef struct \{\s*uint64_t next_ordinal.*?\} vdc_feedback_match_source_t;', matcher, re.S)
    assert source_type
    helpers = matcher[matcher.index("static uint32_t match_inc"):matcher.index("/* Keep authorization")]
    prelude = existing.OWNER_PRELUDE.replace("EVENT_TYPES", event_types).replace(
        '#include <assert.h>', '#include <assert.h>\n#include <inttypes.h>\n#include <pthread.h>\n'
        '#include "vdc_priority_rx.h"\n#include "vdc_priority_match.h"\n'
        'static unsigned core;\nstatic unsigned get_core_num(void) { return core; }')
    prelude = prelude.replace('{ (void)hz;(void)out;return false; }',
        '{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=(vdc_timestamp_clock_bridge_t){'
        '.tick_hz=hz,.raw_before=raw_now,.raw_after=raw_now+1,.local_ns=(now_ns/1000u)*1000u};return true; }')
    prelude = prelude.replace('bool boundary_test_stopped_metadata(',
        'static bool metadata_busy,maintenance_busy,metadata_locked,maintenance_locked,flash_accept=true,persisted_available=true;\n'
        'static unsigned metadata_calls,maintenance_calls,flash_calls,persisted_reads;\n'
        'bool boundary_test_stopped_metadata(')
    prelude = prelude.replace('{ assert(p==&owner);return stopped && publish(ctx); }',
        '{ assert(p==&owner);++metadata_calls;if(!stopped||metadata_busy)return false;'
        'assert(!metadata_locked&&!maintenance_locked);metadata_locked=true;bool ok=publish(ctx);metadata_locked=false;return ok; }')
    harness = '#include "vdc_priority_follow.h"\n#include "vdc_priority_phase.h"\n'
    harness += '#include "'+str(ROOT/"components/product_config/inc/product_config.h").replace('\\','/')+'"\n'
    harness += '#define VDC_PRIORITY_CONFIG_REAL 1\n'
    harness += '#define tdma_service_run_stopped_maintenance baseline_test_stopped_maintenance\n'
    harness += prelude + existing.EXTERNAL_INPUTS + source_type.group(0) + existing.MATCH_STORAGE + helpers + STUBS
    harness += '\nstatic void phase_model_committed_core1(const vdc_dpll_manager_committed_model_t *model);\n#define VDC_PRIORITY_PHASE_MODEL_COMMITTED_HOOK(model) phase_model_committed_core1(model)\n'
    for name in ("vdc_model_feedback.inc", "vdc_boundary_control.inc", "vdc_priority_match.inc"):
        harness += "\n"+(manager/name).read_text(encoding="utf-8")
    harness += "\n"+(manager/"vdc_priority_follow_config.inc").read_text(encoding="utf-8")
    harness += "\n"+(manager/"vdc_priority_follow.inc").read_text(encoding="utf-8").replace(
        '#include "vdc_priority_phase.inc"', (manager/"vdc_priority_phase.inc").read_text(encoding="utf-8"))
    scenarios = existing.SCENARIOS.replace("OWNER_REMOTE_INPUT",
        existing.ingress_definition(existing.OWNER_TESTS, "follower_input"))
    scenarios = scenarios.replace('int main(int argc,char **argv)', CONTROL_CASES+'\nint main(int argc,char **argv)')
    scenarios = scenarios.replace('if(!strcmp(argv[1],"sizes"))',
        'if(!strncmp(argv[1],"config_",7))config_test(argv[1]+7);\n    else if(!strcmp(argv[1],"sizes"))')
    harness += "\n"+existing.ingress_definition(existing.DOMAIN_HARNESS,"fixture")+scenarios
    source = directory/"baseline_config.c"
    source.write_text(harness, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    includes = [ROOT/f"components/{c}/inc" for c in ("tdma", "vdc_domain", "vdc_dpll_manager",
        "distributed_refmem", "calibration_manager", "ota_manager")]
    sources = existing.domain_sources()+[manager/"vdc_feedback_match.c",
        ROOT/"components/distributed_refmem/src/refmem_sync_vdc_feedback.c"]
    exe = directory/"baseline_config.exe"
    command = [compiler,"-std=c11","-O2","-Wall","-Wextra","-Werror","-pthread",
        *[f"-I{p}" for p in includes],str(source),*map(str,sources),"-o",str(exe)]
    result = subprocess.run(command,capture_output=True,text=True,timeout=60)
    (directory/"build.json").write_text(json.dumps(dict(command=command,returncode=result.returncode,
        stdout=result.stdout,stderr=result.stderr),indent=2),encoding="utf-8")
    assert result.returncode == 0, result.stdout+result.stderr
    return exe


@pytest.mark.parametrize("case", ["range", "boot", "persistence", "guards", "count0", "count1", "count2",
    "window_inside", "window_outside", "next_binding", "model_latch", "interval_latch", "pending_unchanged", "STOP_cancels",
    "corrupt_word", "corrupt_first_bind", "concurrent_pair"])
def test_config_candidate(executable, case):
    existing.run_case(executable,"config_"+case)


@pytest.mark.parametrize("case", ["fast", "slow", "uncertain", "quality_two_replacements", "quality_window_exact",
    "quality_window_after", "quality_total_delay_bound", "quality_after_first", "quality_busy_no_refund",
    "quality_model_reset", "quality_stop_cleanup", "quality_apply_cleanup", "quality_final_baseline_reset",
    "quality_owner_stop", "quality_owner_session", "quality_owner_arm", "quality_owner_observer",
    "quality_owner_rx_epoch", "quality_owner_path", "quality_owner_role", "quality_owner_config", "quality_owner_clock_run"])
def test_unchanged_default_control_lifecycle(executable, case):
    existing.run_case(executable,case)


def test_public_ABI_and_private_sizes(executable):
    sizes = tuple(map(int,existing.run_case(executable,"sizes").split()))
    assert sizes == (784,216,408)


STUBS = r'''
static product_config_dpll_baseline_profile_t persisted={1u,100000000u};
bool product_config_get_dpll_baseline_profile(product_config_dpll_baseline_profile_t *out)
{ ++persisted_reads;if(!persisted_available)return false;*out=persisted;return true; }
bool product_config_set_dpll_baseline_profile(const product_config_dpll_baseline_profile_t *in)
{ assert(core==0u&&maintenance_locked&&!metadata_locked);++flash_calls;
  if(!flash_accept)return false;
  persisted=*in;return true; }
bool tdma_service_run_stopped_maintenance(tdma_service_service_t *service,bool(*callback)(void*),void *ctx)
{ assert(service==&owner);++maintenance_calls;if(!stopped||maintenance_busy)return false;
  assert(!metadata_locked&&!maintenance_locked);maintenance_locked=true;
  bool ok=callback(ctx);maintenance_locked=false;return ok; }
'''


CONTROL_CASES = r'''
static vdc_priority_follow_baseline_config_t requested(void)
{ vdc_priority_follow_baseline_config_t result={99u,99u};
  assert(vdc_dpll_manager_get_priority_follow_baseline(&result));return result; }
static void configure_for_next(uint32_t count,uint32_t window)
{ core=0;stopped=true;vdc_priority_follow_baseline_config_t config={count,window};
  uint32_t enabled=ring.enabled,started=ring.adapter_started;
  ring.enabled=ring.adapter_started=0;
  assert(vdc_dpll_manager_set_priority_follow_baseline(&config));
  ring.enabled=enabled;ring.adapter_started=started;core=1;stopped=false; }
static void new_follow_binding(void)
{ core=0;stopped=true;assert(vdc_dpll_manager_set_priority_follow(false));
  assert(vdc_dpll_manager_set_priority_follow(true));core=1;stopped=false; }
static void *config_writer(void *unused)
{ (void)unused;for(unsigned i=0;i<200000u;++i) {
    vdc_priority_follow_baseline_config_t c=(i&1u)?
        (vdc_priority_follow_baseline_config_t){0u,1u}:(vdc_priority_follow_baseline_config_t){2u,250000000u};
    assert(vdc_dpll_manager_set_priority_follow_baseline(&c)); } return NULL; }
static void *config_reader(void *unused)
{ (void)unused;for(unsigned i=0;i<400000u;++i) {vdc_priority_follow_baseline_config_t c=requested();
    assert((c.max_replacements==0u&&c.window_ns==1u)||(c.max_replacements==2u&&c.window_ns==250000000u));}
  return NULL; }
static void config_test(const char *kind)
{
    if(!strcmp(kind,"range")) {
        vdc_priority_follow_baseline_config_t c;
        for(uint32_t count=0;count<=2u;++count)for(uint32_t j=0;j<3u;++j) {
            c=(vdc_priority_follow_baseline_config_t){count,(uint32_t[]){1u,100000000u,250000000u}[j]};
            assert(vdc_dpll_manager_set_priority_follow_baseline(&c));
            vdc_priority_follow_baseline_config_t r=requested();assert(!memcmp(&c,&r,sizeof(c)));
        }
        vdc_priority_follow_baseline_config_t old=requested();unsigned calls=metadata_calls;
        const vdc_priority_follow_baseline_config_t invalid[]={{3u,1u},{UINT32_MAX,1u},{1u,0u},{1u,250000001u},{1u,UINT32_MAX}};
        for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
            assert(!vdc_dpll_manager_set_priority_follow_baseline(&invalid[i]));
        assert(!vdc_dpll_manager_set_priority_follow_baseline(NULL));
        assert(!vdc_dpll_manager_get_priority_follow_baseline(NULL));
        c=requested();assert(!memcmp(&old,&c,sizeof(c))&&metadata_calls==calls&&!flash_calls);return;
    }
    if(!strcmp(kind,"boot")) {
        assert(priority_follow_baseline_init_from_product_config());
        assert(requested().max_replacements==1u&&requested().window_ns==100000000u&&!metadata_calls&&!flash_calls);
        persisted_available=false;assert(!priority_follow_baseline_init_from_product_config());
        assert(requested().max_replacements==1u);persisted_available=true;
        persisted.window_ns=0;assert(!priority_follow_baseline_init_from_product_config());
        persisted.window_ns=100000000u;core=1;assert(!priority_follow_baseline_init_from_product_config());return;
    }
    if(!strcmp(kind,"persistence")) {
        assert(vdc_dpll_manager_default_priority_follow_baseline());
        assert(requested().max_replacements==2u&&requested().window_ns==250000000u);
        assert(vdc_dpll_manager_recall_priority_follow_baseline());
        assert(requested().max_replacements==1u&&requested().window_ns==100000000u&&!flash_calls);
        vdc_priority_follow_baseline_config_t c={0u,1u};assert(vdc_dpll_manager_set_priority_follow_baseline(&c));
        flash_accept=false;assert(!vdc_dpll_manager_store_priority_follow_baseline());
        assert(requested().max_replacements==0u&&persisted.max_replacements==1u&&flash_calls==1u);
        flash_accept=true;assert(vdc_dpll_manager_store_priority_follow_baseline());
        assert(persisted.max_replacements==0u&&persisted.window_ns==1u&&flash_calls==2u);
        assert(vdc_dpll_manager_default_priority_follow_baseline());
        assert(vdc_dpll_manager_recall_priority_follow_baseline()&&requested().max_replacements==0u);
        persisted_available=false;assert(!vdc_dpll_manager_recall_priority_follow_baseline());
        assert(requested().max_replacements==0u&&!metadata_locked&&!maintenance_locked);return;
    }
    if(!strcmp(kind,"guards")) {
        vdc_priority_follow_baseline_config_t c={1u,2u};
        for(unsigned block=0;block<4u;++block) {
            stopped=block!=0;core=block==1;metadata_busy=maintenance_busy=block==2;
            s_vdc_tdma_service=block==3?NULL:&owner;
            unsigned writes=flash_calls,reads=persisted_reads;
            assert(!vdc_dpll_manager_set_priority_follow_baseline(&c));
            assert(!vdc_dpll_manager_default_priority_follow_baseline());
            assert(!vdc_dpll_manager_recall_priority_follow_baseline());
            assert(!vdc_dpll_manager_store_priority_follow_baseline());
            assert(flash_calls==writes&&persisted_reads==reads);
            assert(requested().max_replacements==2u&&requested().window_ns==250000000u);
        }return;
    }
    if(!strcmp(kind,"concurrent_pair")) {
        pthread_t writer,reader1,reader2;core=0;stopped=true;
        assert(!pthread_create(&writer,NULL,config_writer,NULL));
        assert(!pthread_create(&reader1,NULL,config_reader,NULL));
        assert(!pthread_create(&reader2,NULL,config_reader,NULL));
        assert(!pthread_join(writer,NULL)&&!pthread_join(reader1,NULL)&&!pthread_join(reader2,NULL));return;
    }
    if(!strcmp(kind,"corrupt_word")) {
        vdc_priority_follow_baseline_config_t c={99u,88u};
        const uint32_t bad[]={0u,UINT32_C(0x90000001),UINT32_C(0xc0000001),UINT32_C(0x8fffffff)};
        for(unsigned i=0;i<4u;++i) {
            __atomic_store_n(&s_priority_follow_config_word,bad[i],__ATOMIC_RELEASE);
            assert(!vdc_dpll_manager_get_priority_follow_baseline(&c)&&c.max_replacements==99u&&c.window_ns==88u);
        }return;
    }
    setup((!strcmp(kind,"pending_unchanged")||!strcmp(kind,"STOP_cancels"))?6000:0);
    if(!strcmp(kind,"corrupt_first_bind")) {
        __atomic_store_n(&s_priority_follow_config_word,0u,__ATOMIC_RELEASE);
        tick();assert(status().state==VDC_PRIORITY_FOLLOW_RETIRED&&!status().active&&
            status().last_reason==VDC_PRIORITY_FOLLOW_BINDING&&!s_priority_follow_work.bound&&
            !s_priority_follow_work.pending&&!s_priority_follow_work.have_baseline&&!status().prepared);return;
    }
    uint32_t count=2u,window=250000000u;
    if(!strncmp(kind,"count",5))count=(uint32_t)(kind[5]-'0');
    if(!strncmp(kind,"window_",7))window=100000000u;
    configure_for_next(count,window);
    priority_rx.typed_record.uncertainty_width=1600u;tick();
    assert(s_priority_follow_work.baseline_config.max_replacements==count&&
           s_priority_follow_work.baseline_config.window_ns==window);
    if(!strncmp(kind,"count",5)) {
        for(unsigned i=1;i<=3u;++i){quality_event(100u+i,100000000u*i,1600u>>i);tick();}
        assert(s_priority_follow_work.baseline_replacements==count&&status().baselines==1u+count);
        quality_event(500u,1400000000u,200u);tick();assert(status().prepared==1u);return;
    }
    if(!strncmp(kind,"window_",7)) {
        const bool inside=!strcmp(kind,"window_inside");
        quality_event(200u,inside?99999999u-800u:100000001u-800u,800u);tick();
        assert(s_priority_follow_work.baseline_replacements==(inside?1u:0u));return;
    }
    if(!strcmp(kind,"interval_latch")) {
        configure_for_next(0u,1u);
        quality_event(200u,10000000001ull,1600u);tick();
        assert(status().last_reason==VDC_PRIORITY_FOLLOW_INTERVAL&&status().baselines==2u);
        assert(s_priority_follow_work.baseline_config.max_replacements==2u);
        quality_event(300u,10100000001ull,800u);tick();
        assert(s_priority_follow_work.baseline_replacements==1u);return;
    }
    if(!strcmp(kind,"next_binding")||!strcmp(kind,"model_latch")) {
        configure_for_next(0u,1u);
        assert(requested().max_replacements==0u&&s_priority_follow_work.baseline_config.max_replacements==2u);
        if(!strcmp(kind,"model_latch")) {++s_vdc_domain.dco.period_adjust_ppb;publish();
            quality_event(150u,20000000u,1600u);tick();assert(status().last_reason==VDC_PRIORITY_FOLLOW_MODEL);}
        quality_event(200u,100000000u,800u);tick();assert(s_priority_follow_work.baseline_replacements==1u);
        new_follow_binding();quality_event(300u,200000000u,1600u);tick();
        assert(s_priority_follow_work.baseline_config.max_replacements==0u&&status().baselines==1u);
        quality_event(400u,300000000u,800u);tick();assert(!s_priority_follow_work.baseline_replacements);return;
    }
    if(!strcmp(kind,"pending_unchanged")) {
        quality_event(200u,1050000000u,800u);prepare();assert(s_priority_follow_work.pending);
        vdc_priority_follow_work_t before=s_priority_follow_work;
        core=0;stopped=false;vdc_priority_follow_baseline_config_t c={0u,1u};
        assert(!vdc_dpll_manager_set_priority_follow_baseline(&c));
        assert(!memcmp(&before,&s_priority_follow_work,sizeof(before)));
        core=1;apply();assert(status().applied==1u&&s_priority_follow_work.baseline_config.max_replacements==2u);return;
    }
    if(!strcmp(kind,"STOP_cancels")) {
        quality_event(200u,1050000000u,800u);prepare();assert(s_priority_follow_work.pending);
        vdc_priority_follow_work_t before=s_priority_follow_work;
        ring.enabled=ring.adapter_started=ring.data_enabled=0;priority_rx_available=false;
        configure_for_next(0u,1u);assert(!memcmp(&before,&s_priority_follow_work,sizeof(before)));
        apply();assert(!s_priority_follow_work.pending&&!s_priority_follow_work.have_baseline);
        assert(!status().applied&&status().last_reason==VDC_PRIORITY_FOLLOW_STOP);
        new_follow_binding();
        ring.enabled=ring.adapter_started=ring.data_enabled=1;priority_rx_available=true;
        quality_event(300u,1150000000u,1600u);tick();
        assert(s_priority_follow_work.baseline_config.max_replacements==0u);return;
    }
    assert(0);
}
'''
