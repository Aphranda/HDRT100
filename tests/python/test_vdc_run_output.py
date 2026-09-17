"""Execute the real VDC client/planner and libscpi callbacks at owner boundaries.

Hardware, clock samples and transport snapshots are observable stubs. The
client source, model inverse, edge planner, bridge arithmetic and parser are
production code. No board or serial connection is used.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def compile_host(directory, name, text, sources=(), parser=False):
    source = directory / (name + '.c')
    source.write_text(text, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler
    include = [ROOT / f'components/{domain}/inc' for domain in (
        'tdma', 'vdc_domain', 'vdc_dpll_manager', 'distributed_refmem',
        'calibration_manager', 'ota_manager', 'sync_io')]
    flags = []
    if parser:
        include += [ROOT / 'third_party/scpi-parser/libscpi/inc',
                    ROOT / 'middleware/scpi_port/inc']
        flags += ['-DSCPI_USER_CONFIG=1', '-Wno-error=attributes']
    executable = directory / (name + ('.exe' if os.name == 'nt' else ''))
    result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffunction-sections', '-fdata-sections', *flags,
        *['-I' + str(p) for p in include], str(source), *map(str, sources),
        '-Wl,--gc-sections', '-o', str(executable)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.fixture(scope='module')
def client(tmp_path_factory):
    source = (ROOT / 'components/vdc_dpll_manager/src/vdc_run_output.inc').read_text(encoding='utf-8')
    return compile_host(tmp_path_factory.mktemp('vdc-run-client'), 'client',
                        CLIENT_PREFIX + source + CLIENT_MAIN,
                        [ROOT / 'components/vdc_domain/src/vdc_domain.c',
                         ROOT / 'components/vdc_domain/src/vdc_timestamp.c',
                         ROOT / 'components/tdma/src/tdma_profile.c'])


@pytest.mark.parametrize('scenario', [
    'arm_wait', 'pre_arm_wait', 'pre_first_stop', 'unseen_arm_stop',
    'retire_interleave', 'prepare_interleave', 'status_interleave', 'wrong_generation',
    'busy_session', 'busy_role', 'busy_epoch', 'busy_run', 'busy_invalid',
    'busy_slot', 'busy_schedule', 'busy_owner_without_model',
    'first_session', 'first_role', 'first_epoch', 'first_run', 'first_slot',
    'first_schedule', 'first_invalid', 'model_tail', 'delay_latched',
    'stop_during_plan', 'session_during_plan', 'gate_busy', 'core_guard', 'no_request',
])
def test_production_client(client, scenario):
    result = subprocess.run([str(client), scenario], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


CLIENT_PREFIX = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "vdc_run_output.h"
#include "vdc_output_edge_plan.h"
#include "vdc_future_raw.h"
#define BOARD_SYS_CLOCK_HZ 250000000u
static vdc_domain_context_t s_vdc_domain;
static tdma_service_service_t *s_vdc_tdma_service;
static unsigned core, prepare_calls, release_calls, service_calls, submit_calls, cancels;
static unsigned hook, hook_seen, generation, snapshot_calls;
static bool ready=true, model_available=true, cancelled;
static int32_t delay=100;
static uint32_t session=8u;
static tdma_ring_clock_snapshot_t ring;
static vdc_dpll_manager_committed_model_t model;
static sync_io_run_output_snapshot_t hardware;
static sync_io_run_output_edge_t admitted[4][SYNC_IO_RUN_OUTPUT_BLOCK_EDGES];
static vdc_timestamp_clock_bridge_t bridge={.raw_before=1000000u,.raw_after=1000002u,
    .local_ns=4000000u,.tick_hz=BOARD_SYS_CLOCK_HZ};
static void interleave(void);
static unsigned get_core_num(void) { return core; }
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
bool vdc_dpll_manager_get_output_delay_ns(int32_t *out) { *out=delay; return true; }
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{ *out=ring; return true; }
bool tdma_service_run_stopped_maintenance(tdma_service_service_t *service,
    bool(*callback)(void*),void *context)
{ (void)service; return !ring.enabled && !ring.adapter_started && callback(context); }
bool vdc_timestamp_clock_configuration_supported(uint32_t hz)
{ assert(hz==BOARD_SYS_CLOCK_HZ); return true; }
bool vdc_timestamp_clock_try_read_bridge(uint32_t hz,vdc_timestamp_clock_bridge_t *out)
{
    assert(hz==BOARD_SYS_CLOCK_HZ); *out=bridge;
    if(hook==4u) { ring.enabled=0u; ring.data_enabled=0u; ++ring.config_seq; }
    if(hook==5u) ++session;
    return true;
}
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{ if(!model_available)return false; *out=model; return true; }
bool sync_io_run_output_prepare(uint32_t hz,uint32_t duration,uint32_t *request)
{
    assert(hz==BOARD_SYS_CLOCK_HZ && duration==1000u); ++prepare_calls;
    if(hook==2u)interleave();
    memset(&hardware,0,sizeof(hardware)); hardware.state=SYNC_IO_RUN_OUTPUT_PREPARED;
    hardware.generation=++generation; *request=generation; cancelled=false; return true;
}
void sync_io_run_output_cancel(void) { ++cancels; cancelled=true; }
void sync_io_run_output_service_core1(void)
{
    ++service_calls;
    if(cancelled)hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED;
    if(hook==1u) { hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED; interleave(); }
}
bool sync_io_run_output_snapshot(sync_io_run_output_snapshot_t *out)
{ ++snapshot_calls; if(hook==3u && snapshot_calls==2u)interleave(); *out=hardware; return true; }
bool sync_io_run_output_release(uint32_t request)
{
    assert(request==hardware.generation && hardware.state==SYNC_IO_RUN_OUTPUT_RETIRED);
    ++release_calls; hardware.state=SYNC_IO_RUN_OUTPUT_IDLE; return true;
}
bool sync_io_run_output_can_submit_core1(uint32_t request)
{ return request==hardware.generation && ready && !cancelled; }
bool sync_io_run_output_submit_core1(uint32_t request,
    const sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES])
{
    assert(request==hardware.generation && ready && !cancelled && submit_calls<4u);
    memcpy(admitted[submit_calls++],edges,sizeof(admitted[0]));
    hardware.last_ordinal=edges[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES-1u].ordinal;
    hardware.state=SYNC_IO_RUN_OUTPUT_RUNNING; return true;
}
'''


CLIENT_MAIN = r'''
static void interleave(void)
{
    const unsigned kind=hook; hook=0u; ++hook_seen;
    if(kind==1u) {
        core=0u; run_output_release_core0();
        assert(!release_calls && s_run_output_request==generation);
        uint32_t request=999u; vdc_run_output_status_t output;
        assert(!vdc_run_output_prepare(1000000u,1000u,1000u,&request) && request==999u);
        assert(!vdc_run_output_status(&output)); core=1u;
    } else {
        const unsigned before=service_calls; core=1u;
        vdc_run_output_service_core1(); assert(service_calls==before); core=0u;
    }
}
static void initialize(void)
{
    ring.config_seq=ring.applied_config_seq=2u;
    ring.local_slot_id=1u; ring.schedule_crc32=0x123u;
    model.token=11u; model.session=session; model.role_generation=3u;
    model.clock_epoch_id=4u; model.clock_run_id=5u; model.local_slot=1u;
    model.dco.valid=1u; model.dco.nominal_period_ns=1000000u;
    model.dco.tdma_schedule_crc32=ring.schedule_crc32;
    s_vdc_domain.control.profile.generation=model.role_generation;
    s_vdc_domain.clock.epoch_id=model.clock_epoch_id;
    s_vdc_domain.clock.run_id=model.clock_run_id;
    s_vdc_domain.schedule.local_slot_id=model.local_slot;
    s_vdc_domain.schedule.schedule_crc32=model.dco.tdma_schedule_crc32;
}
static void prepare(void)
{
    uint32_t request=0u; core=0u;
    assert(vdc_run_output_prepare(1000000u,1000u,1000u,&request));
    assert(request==generation && s_run_output_request==generation);
}
static void arm(bool start)
{
    ring.enabled=ring.adapter_started=1u; ring.config_seq=ring.applied_config_seq=3u;
    ring.data_enabled=start; core=1u;
}
static void finish_cancel(void)
{
    assert(cancels); core=1u; vdc_run_output_service_core1(); core=0u;
    run_output_release_core0(); assert(release_calls==1u && !s_run_output_request);
}
int main(int argc,char **argv)
{
    assert(argc==2); const char *test=argv[1]; initialize();
    if(!strcmp(test,"no_request")) {
        core=1u; vdc_run_output_service_core1();
        assert(service_calls==1u && !s_run_output_request && !s_run_output_busy);
        assert(!snapshot_calls && !submit_calls && !prepare_calls && !cancels); return 0;
    }
    if(!strcmp(test,"prepare_interleave"))hook=2u;
    prepare();
    if(!strcmp(test,"prepare_interleave")) { assert(hook_seen==1u); return 0; }
    if(!strcmp(test,"pre_arm_wait")) {
        core=1u; vdc_run_output_service_core1(); assert(!submit_calls && !cancels); return 0;
    }
    if(!strcmp(test,"unseen_arm_stop")) {
        ring.config_seq=ring.applied_config_seq=4u; core=1u;
        vdc_run_output_service_core1(); finish_cancel(); return 0;
    }
    if(!strcmp(test,"gate_busy")) {
        s_run_output_busy=1u; core=1u; vdc_run_output_service_core1();
        assert(!service_calls); return 0;
    }
    if(!strcmp(test,"core_guard")) {
        vdc_run_output_service_core1(); assert(!service_calls); return 0;
    }
    arm(false); vdc_run_output_service_core1(); assert(!submit_calls && !cancels);
    if(!strcmp(test,"arm_wait"))return 0;
    if(!strcmp(test,"pre_first_stop")) {
        ring.enabled=ring.adapter_started=0u; ring.config_seq=ring.applied_config_seq=4u;
        vdc_run_output_service_core1(); finish_cancel(); return 0;
    }
    if(!strncmp(test,"first_",6u)) {
        vdc_dpll_manager_committed_model_t saved=model;
        if(!strcmp(test,"first_session"))++model.session;
        if(!strcmp(test,"first_role"))++model.role_generation;
        if(!strcmp(test,"first_epoch"))++model.clock_epoch_id;
        if(!strcmp(test,"first_run"))++model.clock_run_id;
        if(!strcmp(test,"first_slot"))++model.local_slot;
        if(!strcmp(test,"first_schedule"))++model.dco.tdma_schedule_crc32;
        if(!strcmp(test,"first_invalid"))model.dco.valid=0u;
        ring.data_enabled=1u; vdc_run_output_service_core1(); assert(!submit_calls && !cancels);
        model=saved; vdc_run_output_service_core1(); assert(submit_calls==1u); return 0;
    }
    if(!strcmp(test,"stop_during_plan"))hook=4u;
    if(!strcmp(test,"session_during_plan"))hook=5u;
    ring.data_enabled=1u; vdc_run_output_service_core1();
    if(hook==4u || hook==5u) { assert(!submit_calls && cancels); return 0; }
    assert(submit_calls==1u && s_run_output.blocks_planned==1u);
    if(!strncmp(test,"busy_",5u)) {
        ready=false;
        if(!strcmp(test,"busy_session"))++session;
        if(!strcmp(test,"busy_role"))++model.role_generation;
        if(!strcmp(test,"busy_epoch"))++model.clock_epoch_id;
        if(!strcmp(test,"busy_run"))++model.clock_run_id;
        if(!strcmp(test,"busy_slot"))++model.local_slot;
        if(!strcmp(test,"busy_schedule"))++model.dco.tdma_schedule_crc32;
        if(!strcmp(test,"busy_invalid"))model.dco.valid=0u;
        if(!strcmp(test,"busy_owner_without_model")) {
            model_available=false; ++s_vdc_domain.clock.epoch_id;
        }
        vdc_run_output_service_core1(); assert(submit_calls==1u); finish_cancel(); return 0;
    }
    if(!strcmp(test,"retire_interleave")) {
        hook=1u; vdc_run_output_service_core1(); assert(hook_seen==1u && !release_calls);
        core=0u; run_output_release_core0(); assert(release_calls==1u && !s_run_output_request);
        ring.enabled=ring.adapter_started=ring.data_enabled=0u;
        prepare(); assert(generation==2u && !cancelled); return 0;
    }
    if(!strcmp(test,"wrong_generation")) {
        ++hardware.generation; hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED; core=0u;
        run_output_release_core0(); assert(!release_calls && s_run_output_request); return 0;
    }
    if(!strcmp(test,"status_interleave")) {
        hardware.state=SYNC_IO_RUN_OUTPUT_RETIRED; core=0u;
        snapshot_calls=0u; hook=3u;
        vdc_run_output_status_t out; assert(vdc_run_output_status(&out));
        assert(hook_seen==1u && out.blocks_planned==1u); return 0;
    }
    assert(!strcmp(test,"model_tail") || !strcmp(test,"delay_latched"));
    sync_io_run_output_edge_t prefix[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES];
    memcpy(prefix,admitted[0],sizeof(prefix));
    ready=false; ++model.token; model.dco.phase_offset_ns=2000;
    delay=50000; vdc_run_output_service_core1(); assert(submit_calls==1u && !cancels);
    assert(!memcmp(prefix,admitted[0],sizeof(prefix)));
    ready=true; vdc_run_output_service_core1(); assert(submit_calls==2u && !cancels);
    assert(s_run_output.delay_ns==100);
    assert(!memcmp(prefix,admitted[0],sizeof(prefix)));
    for(unsigned i=0;i<SYNC_IO_RUN_OUTPUT_BLOCK_EDGES;++i) {
        assert(admitted[1][i].model_token==model.token);
        assert(admitted[1][i].ordinal>prefix[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES-1u].ordinal);
        assert(admitted[1][i].rising_tick>prefix[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES-1u].falling_tick);
        /* Independent zero-rate oracle: F(local)=local+2000; physical
         * delay remains +100, not the newly requested +50000. Bridge raw
         * upper sample is local/4+2, and the enclosure adds one tick. */
        const uint64_t local=admitted[1][i].ordinal*UINT64_C(1000000)-2000u+100u;
        assert(admitted[1][i].rising_tick==local/4u+3u);
        assert(admitted[1][i].falling_tick-admitted[1][i].rising_tick==250u);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def parser_host(tmp_path_factory):
    source = (ROOT / 'middleware/scpi_port/src/scpi_system_snapshot_commands.c').read_text(encoding='utf-8')
    callbacks = source[source.index('static bool scpi_vdc_run_u32('):
                       source.index('scpi_result_t scpi_cmd_vdc_fixed_output(')]
    library = ROOT / 'third_party/scpi-parser/libscpi/src'
    return compile_host(tmp_path_factory.mktemp('vdc-run-parser'), 'parser',
                        PARSER_PREFIX + callbacks + PARSER_MAIN,
                        sorted(library.glob('*.c')), parser=True)


@pytest.mark.parametrize('command', [
    'RUN 1000000,1000,1000', 'RUN +1000000,+1000,+1000',
    'RUN 0001000000,001000,1000', 'RUN 1000000, 1000, 1000', 'STOP',
])
def test_real_parser_accepts_complete_commands(parser_host, command):
    result = subprocess.run([str(parser_host), command, 'accept'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize('command', [
    'RUN', 'RUN 1000000', 'RUN 1000000,1000', 'RUN 1000000,1000,1000,1',
    'RUN 1000000,1000,1000,', 'RUN 1000000,1000,1000 1',
    'RUN -1000000,1000,1000', 'RUN 1000000,-1000,1000', 'RUN 1000000,1000,-1',
    'RUN 4294967296,1000,1000', 'RUN 1000000,4294967296,1000',
    'RUN 1000000,1000,4294967296', 'RUN 999999999999999999999999999999,1000,1000',
    'RUN 1e6,1000,1000', 'RUN 1000000.0,1000,1000', 'RUN 1000000ns,1000,1000',
    'RUN #Hf4240,1000,1000', 'RUN "1000000",1000,1000',
    'RUN 1000000,1000,+', 'RUN 1000000,1000,-', 'RUN 1000000,1000,abc',
    'RUN 1000000 , 1000 , 1000', 'RUN 1000000 ,1000,1000',
    'RUN 1000000,1000 ,1000',
    'STOP 1', 'STOP ,', 'STOP abc',
])
def test_real_parser_rejects_without_side_effects(parser_host, command):
    result = subprocess.run([str(parser_host), command, 'reject'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


def test_real_parser_exports_start_observation_receipt(parser_host):
    result = subprocess.run([str(parser_host), 'RUN?', 'query'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    fields = [int(value) for value in result.stdout.strip().split(',')]
    assert len(fields) == 43
    # Preserve every old position: 26 small fields, ten uint64, two config.
    assert fields[:36] == [0] * 36
    assert fields[36:] == [20, 21, 13, 12, 3, 4294967303, 4294967311]


PARSER_PREFIX = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "scpi/scpi.h"
#include "vdc_run_output.h"
static unsigned prepares, cancels, writes;
static char response[4096];
static size_t response_size;
static size_t output(scpi_t *context,const char *data,size_t length)
{
    (void)context; ++writes; assert(response_size+length<sizeof(response));
    memcpy(response+response_size,data,length);response_size+=length;response[response_size]=0;
    return length;
}
static scpi_result_t flush(scpi_t *context) { (void)context; return SCPI_RES_OK; }
static int error(scpi_t *context,int_fast16_t value) { (void)context; (void)value; return 0; }
static void scpi_port_push_exec_error(scpi_t *context,const char *message)
{ (void)message; SCPI_ErrorPush(context,SCPI_ERROR_EXECUTION_ERROR); }
static scpi_result_t scpi_port_result_ok(scpi_t *context)
{ SCPI_ResultText(context,"OK"); return SCPI_RES_OK; }
bool vdc_run_output_prepare(uint32_t period,uint32_t high,uint32_t duration,uint32_t *request)
{
    ++prepares;
    fprintf(stderr,"prepare %lu,%lu,%lu\n",(unsigned long)period,(unsigned long)high,(unsigned long)duration);
    assert(period==1000000u && high==1000u && duration==1000u);
    *request=7u; return true;
}
void vdc_run_output_cancel(void) { ++cancels; }
bool vdc_run_output_status(vdc_run_output_status_t *out)
{
    memset(out,0,sizeof(*out));out->prepared_config=20u;out->arm_config=21u;
    out->hardware.start_pc=13u;out->hardware.program_offset=12u;out->hardware.start_raw_flags=3u;
    out->hardware.start_raw_observed=UINT64_C(4294967303);
    out->hardware.start_raw_after=UINT64_C(4294967311);return true;
}
'''


PARSER_MAIN = r'''
static const scpi_command_t commands[]={
    {.pattern="RUN",.callback=scpi_cmd_vdc_run_output},
    {.pattern="RUN?",.callback=scpi_cmd_vdc_run_output_q},
    {.pattern="STOP",.callback=scpi_cmd_vdc_run_output_stop}, SCPI_CMD_LIST_END
};
int main(int argc,char **argv)
{
    assert(argc==3); char input[1024],line[1024]; scpi_t context; scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&context,commands,&interface,scpi_units_def,"a","b","c","d",
        input,sizeof(input),errors,16);
    const int length=snprintf(line,sizeof(line),"%s\n",argv[1]);
    assert(length>0 && (size_t)length<sizeof(line));
    const bool parsed=SCPI_Input(&context,line,length);
    if(!strcmp(argv[2],"query")) {
        assert(parsed && !SCPI_ErrorCount(&context) && writes && !prepares && !cancels);
        printf("%s",response);
    } else if(!strcmp(argv[2],"accept")) {
        assert(parsed && !SCPI_ErrorCount(&context) && writes);
        if(!strncmp(argv[1],"RUN",3u))assert(prepares==1u && !cancels);
        else assert(!prepares && cancels==1u);
    } else {
        assert(!prepares && !cancels && !writes && SCPI_ErrorCount(&context)>0);
    }
    return 0;
}
'''
