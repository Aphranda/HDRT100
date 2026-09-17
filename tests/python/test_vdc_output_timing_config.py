"""Execute timing migration, real requested-profile code and libscpi parser."""
from pathlib import Path
import os
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def compile_host(directory, name, sources, extra=()):
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler
    executable = directory / (name + (".exe" if os.name == "nt" else ""))
    includes = [ROOT, ROOT / "components/product_config/inc",
                ROOT / "components/vdc_dpll_manager/inc",
                ROOT / "components/flash_transaction/inc", ROOT / "components/ota_manager/inc",
                ROOT / "drivers/mcu/flash/inc", ROOT / "config"]
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               *["-I" + str(p) for p in includes], *map(str, sources), *extra,
               "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    (directory / (name + "-compile.log")).write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


def test_actual_product_timing_persistence(tmp_path):
    executable = compile_host(tmp_path, "persistence", [
        ROOT / "tests/unit/test_output_timing_persistence.c",
        ROOT / "components/product_config/src/product_config.c"])
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.fixture(scope="module")
def parser_host(tmp_path_factory):
    directory = tmp_path_factory.mktemp("timing-config-parser")
    source = (ROOT / "middleware/scpi_port/src/scpi_sync_commands.c").read_text(encoding="utf-8")
    callbacks = source[source.index("static bool scpi_output_timing_read_us"):
                       source.index("scpi_result_t scpi_cmd_vdc_priority_follow_baseline(")]
    header = (ROOT / "middleware/scpi_port/inc/scpi_system_snapshot_commands.h").read_text(encoding="utf-8")
    # Compile registrations from the real command table, not hand-copied names.
    registrations = re.findall(r'\{\.pattern = "SYSTem:VDC:OUTPut:TIMing[^\n]+?\},', header)
    assert len(registrations) == 5
    command_table = "\nstatic const scpi_command_t commands[]={\n" + "\n".join(registrations) + "\nSCPI_CMD_LIST_END};\n"
    unit = directory / "timing.c"
    unit.write_text(HARNESS + callbacks + command_table + MAIN, encoding="utf-8")
    library = ROOT / "third_party/scpi-parser/libscpi"
    flags = ["-DSCPI_USER_CONFIG=1", "-I" + str(library / "inc"),
             "-I" + str(ROOT / "middleware/scpi_port/inc")]
    flags += ["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]
    return compile_host(directory, "timing", [unit, *sorted((library / "src").glob("*.c"))], flags)


def test_actual_config_lifecycle_and_atomic_read(parser_host):
    result = subprocess.run([str(parser_host), "selftest"], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("arguments,expected", [
    ("12000,16000,6000", "12000,16000,6000"),
    ("+12000,+16000,+6000", "12000,16000,6000"),
    (" 24000, 32000, 8000", "24000,32000,8000"),
    ("24000,32000,8000 ", "24000,32000,8000"),
    ("00001001,00001001,00001000", "1001,1001,1000"),
    ("1000000,1000000,999999", "1000000,1000000,999999"),
])
def test_real_parser_accepts_tuple(parser_host, arguments, expected):
    run_parser(parser_host, "SYST:VDC:OUTP:TIM " + arguments, expected)


@pytest.mark.parametrize("arguments", [
    "", "12000", "12000,16000", "12000,16000,6000,1", "12000,16000,6000,",
    "12000,,6000", "12000 16000 6000", "12000,16000,6000 1", "12000,16000,-1",
    "12000,16000,-0", "12000,16000,+", "12000,16000,6e3", "12000,16000,6000.0",
    '12000,16000,"6000"', "12000,16000,#H1770", "12000,16000,6000us",
    "4294967296,16000,6000", "12000,4294967296,6000", "12000,16000,4294967296",
    "18446744073709551616,16000,6000", "12000,16000,999999999999999999999999",
    "12000,16000,0", "12000,16000,999", "6000,16000,6000",
    "17000,16000,6000", "12000,1000001,6000", "0,0,0",
    "24000 ,32000,8000", "24000,32000 ,8000",
])
def test_real_parser_rejects_without_mutation(parser_host, arguments):
    run_parser(parser_host, "SYST:VDC:OUTP:TIM " + arguments, "ERROR")


@pytest.mark.parametrize("suffix,expected", [
    ("?", "12000,16000,6000"), (":DEFA", '"OK"'),
    (":REC", '"OK"'), (":STOR", '"OK"'),
])
def test_registered_profile_commands(parser_host, suffix, expected):
    run_parser(parser_host, "SYST:VDC:OUTP:TIM" + suffix, expected)


@pytest.mark.parametrize("suffix", ["?", ":DEFA", ":REC", ":STOR"])
def test_no_parameter_commands_reject_extra_token(parser_host, suffix):
    run_parser(parser_host, "SYST:VDC:OUTP:TIM" + suffix + " 1", "ERROR")


@pytest.mark.parametrize("blocker", ["stop", "request", "core", "gate"])
@pytest.mark.parametrize("suffix", [" 24000,32000,8000", ":DEFA", ":REC", ":STOR"])
def test_parser_owner_rejection_preserves_profile(parser_host, blocker, suffix):
    run_parser(parser_host, "SYST:VDC:OUTP:TIM" + suffix, "ERROR", blocker)


@pytest.mark.parametrize("suffix,blocker", [(":REC", "read"), (":STOR", "flash")])
def test_parser_persistence_failure_preserves_profiles(parser_host, suffix, blocker):
    run_parser(parser_host, "SYST:VDC:OUTP:TIM" + suffix, "ERROR", blocker)


def run_parser(executable, command, expected, blocker="none"):
    result = subprocess.run([str(executable), command, expected, blocker],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scpi/scpi.h"
#include "product_config.h"
#include "vdc_output_timing.h"
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"CHECK line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned core,session,metadata_calls,maintenance_calls,flash_calls;
static bool stopped=true,control_guard,output_idle=true,readable=true,flash_ok=true;
static int service;
static void *s_vdc_tdma_service=&service;
static product_config_vdc_output_timing_profile_t saved={24000u,32000u,8000u};
static unsigned get_core_num(void){return core;}
static unsigned vdc_dpll_manager_feedback_session(void){return session;}
static bool vdc_run_output_configuration_idle(void){return output_idle;}
static bool tdma_service_update_stopped_metadata(void *owner,bool(*cb)(void *),void *ctx){
    assert(owner==&service);++metadata_calls;
    if(!stopped||control_guard)return false;
    control_guard=true;const bool ok=cb(ctx);control_guard=false;return ok;
}
static bool tdma_service_run_stopped_maintenance(void *owner,bool(*cb)(void *),void *ctx){
    assert(owner==&service);++maintenance_calls;
    if(!stopped||control_guard)return false;
    control_guard=true;const bool ok=cb(ctx);control_guard=false;return ok;
}
bool product_config_get_vdc_output_timing_profile(product_config_vdc_output_timing_profile_t *p){
    if(!readable)return false;
    *p=saved;return true;
}
bool product_config_set_vdc_output_timing_profile(const product_config_vdc_output_timing_profile_t *p){
    assert(core==0u&&stopped&&control_guard&&output_idle);++flash_calls;
    if(!flash_ok)return false;
    saved=*p;return true;
}
static void (*load_hook)(const uint32_t *);
static uint32_t hooked_load(const uint32_t *p,int order){
    const uint32_t value=__atomic_load_n(p,order);
    if(load_hook)load_hook(p);
    return value;
}
#define __atomic_load_n(p,order) hooked_load((const uint32_t *)(p),(order))
#include "components/vdc_dpll_manager/src/vdc_output_timing_config.inc"
#undef __atomic_load_n
static char response[2048];static size_t response_size;
static size_t write_response(scpi_t *ctx,const char *data,size_t length){
    (void)ctx;assert(response_size+length<sizeof(response));
    memcpy(response+response_size,data,length);response_size+=length;response[response_size]=0;return length;
}
static scpi_result_t flush_response(scpi_t *ctx){(void)ctx;return SCPI_RES_OK;}
static int record_error(scpi_t *ctx,int_fast16_t error){(void)ctx;(void)error;return 0;}
static void scpi_port_push_exec_error(scpi_t *ctx,const char *text){(void)text;SCPI_ErrorPush(ctx,SCPI_ERROR_EXECUTION_ERROR);}
static bool same(const vdc_output_timing_profile_t *a,const vdc_output_timing_profile_t *b){
    return a->plan_ahead_us==b->plan_ahead_us&&a->commit_ahead_us==b->commit_ahead_us&&a->refill_low_us==b->refill_low_us;
}
static vdc_output_timing_profile_t requested(void){
    vdc_output_timing_profile_t p;assert(vdc_dpll_manager_get_output_timing_profile(&p));return p;
}
static void update_during_read(const uint32_t *p){
    if(p==&s_output_timing_profile.plan_ahead_us){
        load_hook=NULL;const vdc_output_timing_profile_t next={36000u,48000u,12000u};
        assert(vdc_dpll_manager_set_output_timing_profile(&next));
    }
}
static void selftest(void){
    assert(!vdc_dpll_manager_get_output_timing_profile(NULL));
    assert(!vdc_dpll_manager_set_output_timing_profile(NULL));
    assert(output_timing_init_from_product_config()&&flash_calls==0u);
    const vdc_output_timing_profile_t boot=requested();assert(boot.plan_ahead_us==24000u);
    readable=false;assert(!output_timing_init_from_product_config());readable=true;
    session=1u;assert(!output_timing_init_from_product_config());session=0u;
    core=1u;assert(!output_timing_init_from_product_config());core=0u;
    const vdc_output_timing_profile_t next={36000u,48000u,12000u};
    for(unsigned mode=0;mode<5u;++mode){
        core=mode==0u?1u:0u;stopped=mode!=1u;control_guard=mode==2u;
        output_idle=mode!=3u;s_vdc_tdma_service=mode==4u?NULL:&service;
        assert(!vdc_dpll_manager_set_output_timing_profile(&next));
        assert(!vdc_dpll_manager_default_output_timing());
        assert(!vdc_dpll_manager_recall_output_timing());
        assert(!vdc_dpll_manager_store_output_timing());
        const vdc_output_timing_profile_t p=requested();assert(same(&p,&boot));
    }
    core=0u;stopped=true;control_guard=false;output_idle=true;s_vdc_tdma_service=&service;
    assert(flash_calls==0u);
    vdc_output_timing_profile_t sentinel={7u,8u,9u},out=sentinel;
    s_output_timing_guard|=1u;
    assert(!vdc_dpll_manager_get_output_timing_profile(&out)&&same(&out,&sentinel));
    ++s_output_timing_guard;load_hook=update_during_read;
    assert(!vdc_dpll_manager_get_output_timing_profile(&out)&&same(&out,&sentinel));
    const vdc_output_timing_profile_t after=requested();assert(same(&after,&next));
    flash_ok=false;assert(!vdc_dpll_manager_store_output_timing());
    assert(saved.plan_ahead_us==24000u);flash_ok=true;
    assert(vdc_dpll_manager_store_output_timing()&&saved.plan_ahead_us==36000u);
    assert(vdc_dpll_manager_default_output_timing());
    assert(output_timing_init_from_product_config());
    const vdc_output_timing_profile_t restored=requested();assert(same(&restored,&next));
    assert(flash_calls==2u);
    /* Semantic-invalid persisted mirror never partially replaces request. */
    saved.refill_low_us=saved.plan_ahead_us;
    assert(!vdc_dpll_manager_recall_output_timing());
    const vdc_output_timing_profile_t retained=requested();assert(same(&retained,&next));
}
'''

MAIN = r'''
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"selftest")){selftest();return 0;}
    assert(argc==4);
    const vdc_output_timing_profile_t before=requested();
    const product_config_vdc_output_timing_profile_t old_saved=saved;
    if(!strcmp(argv[3],"stop"))stopped=false;
    if(!strcmp(argv[3],"request"))output_idle=false;
    if(!strcmp(argv[3],"core"))core=1u;
    if(!strcmp(argv[3],"gate"))control_guard=true;
    if(!strcmp(argv[3],"read"))readable=false;
    if(!strcmp(argv[3],"flash"))flash_ok=false;
    char input[1024],command[1024];scpi_t ctx;scpi_error_t errors[16];
    scpi_interface_t interface={.write=write_response,.flush=flush_response,.error=record_error};
    SCPI_Init(&ctx,commands,&interface,scpi_units_def,"vendor","model","serial","version",
        input,sizeof(input),errors,16);
    const int length=snprintf(command,sizeof(command),"%s\n",argv[1]);
    assert(length>0&&(size_t)length<sizeof(command));
    const scpi_bool_t parsed=SCPI_Input(&ctx,command,length);
    const int error_count=SCPI_ErrorCount(&ctx);
    fprintf(stderr,"parsed=%u errors=%d response=%s\n",(unsigned)parsed,error_count,response);
    if(!strcmp(argv[2],"ERROR")){
        const vdc_output_timing_profile_t after=requested();
        assert(error_count>0&&response_size==0u&&same(&before,&after));
        assert(!memcmp(&saved,&old_saved,sizeof(saved)));
        if(!strcmp(argv[3],"none"))assert(metadata_calls==0u&&maintenance_calls==0u&&flash_calls==0u);
    }else{
        assert(parsed&&error_count==0);
        response[strcspn(response,"\r\n")]=0;assert(!strcmp(response,argv[2]));
        if(strstr(argv[1],":REC")){const vdc_output_timing_profile_t p=requested();assert(p.plan_ahead_us==saved.plan_ahead_us);}
        if(strstr(argv[1],":STOR"))assert(flash_calls==1u&&saved.plan_ahead_us==before.plan_ahead_us);
    }
    return 0;
}
'''
