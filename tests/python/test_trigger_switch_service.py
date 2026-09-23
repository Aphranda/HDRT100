"""Actual SWITCH owner C: deployment authority and finite maintenance actions."""
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid
from test_trigger_deployment_service import PARSER as DEPLOYMENT_PARSER


@pytest.fixture(scope='module')
def owner(tmp_path_factory):
    d = tmp_path_factory.mktemp('switch-owner')
    (d/'pico.h').write_text('unsigned get_core_num(void);\n', encoding='utf-8')
    (d/'board_identity.h').write_text('const char *board_identity_serial(void);\n', encoding='utf-8')
    (d/'tdma_runtime_owner.h').write_text(
        '#include "tdma_service.h"\n'
        'tdma_service_service_t *tdma_runtime_owner_get(void);\n'
        'bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out);\n'
        'bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out);\n', encoding='utf-8')
    return compile_grid(d, 'owner', HARNESS, [ROOT/'components/sync_trigger/src/trigger_switch_service.c'])


def run_case(owner, case):
    r = subprocess.run([str(owner), case], capture_output=True, text=True, timeout=5)
    assert r.returncode == 0, r.stdout+r.stderr


@pytest.mark.parametrize('case', [
    'lifecycle', 'empty', 'null_snapshot', 'zero_generation', 'core', 'maintenance_busy',
    'running', 'adapter', 'pending_config', 'no_deployment', 'inactive_deployment',
    'bad_uid', 'null_uid', 'short_uid', 'wrong_generation', 'wrong_crc', 'corrupt_table',
    'wrong_role_counter', 'wrong_role_gateway', 'wrong_slot', 'wrong_schedule', 'wrong_ring',
    'missing_staged', 'disabled_staged', 'missing_ring', 'mask_zero', 'mask_range',
    'counter_loaded', 'gateway_loaded', 'duplicate_load', 'load_failure',
    'stage_failure', 'apply_failure', 'stop_failure', 'release_failure',
    'snapshot_busy', 'stop_staged', 'stop_pending', 'cleanup_changed', 'reentrant',
    *[f'{operation}_changed_{field}' for operation in ('stage', 'apply')
      for field in ('generation', 'crc', 'uid', 'role', 'slot', 'schedule', 'ring', 'mask')],
])
def test_actual_switch_owner(owner, case):
    run_case(owner, case)


@pytest.mark.parametrize('role', [1, 3])
@pytest.mark.parametrize('slot', range(4))
def test_switch_roles_authorize_any_deployed_board(owner, role, slot):
    run_case(owner, f'identity_{role}_{slot}')


LEGAL_CODES = [(mask, code) for mask in range(1, 16) for code in range(16) if not code & ~mask]
assert len(LEGAL_CODES) == 80


@pytest.mark.parametrize('mask,code', LEGAL_CODES)
def test_all_80_subset_codes_reach_backend_without_stage_output(owner, mask, code):
    run_case(owner, f'code_{mask}_{code}')


@pytest.mark.parametrize('mask', range(1, 16))
def test_outside_mask_never_reaches_backend(owner, mask):
    run_case(owner, f'outside_{mask}')


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trigger_switch_service.h"
#include "trigger_counter_service.h"
#include "trigger_gateway_service.h"
#include "tdma_runtime_owner.h"
static unsigned core,depth,load_calls,stage_calls,apply_calls,stop_calls,release_calls;
static bool maintenance=true,dep_ok=true,staged_ok=true,ring_ok=true,counter_loaded,gateway_loaded;
static bool load_ok=true,stage_ok=true,apply_ok=true,stop_ok=true,release_ok=true,snapshot_ok=true;
static bool complete,reentrant;
static uint32_t generation;
static const char *serial="0000000000000002";
static tdma_ring_clock_snapshot_t ring={.config_seq=7,.applied_config_seq=7};
static tdma_service_ring_runtime_config_t staged={.enabled=1,.local_slot_id=1,.schedule_crc32=123};
static trigger_deployment_snapshot_t dep;
static sync_io_switch_snapshot_t hw;
unsigned get_core_num(void){return core;}
const char *board_identity_serial(void){return serial;}
bool trigger_counter_is_loaded(void){return counter_loaded;}
bool trigger_gateway_is_loaded(void){return gateway_loaded;}
tdma_service_service_t *tdma_runtime_owner_get(void){return NULL;}
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{if(!ring_ok)return false;*out=ring;return true;}
bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out)
{if(!staged_ok)return false;*out=staged;return true;}
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *out)
{if(!dep_ok)return 32;*out=dep;return 0;}
bool tdma_service_run_stopped_maintenance(tdma_service_service_t *service,bool(*fn)(void*),void *ctx)
{
    (void)service;if(!maintenance || ring.enabled || ring.adapter_started || ring.config_seq!=ring.applied_config_seq)return false;
    assert(!depth);++depth;bool ok=fn(ctx);--depth;return ok;
}
static void mutate(uint32_t g){assert(core==0 && depth==1 && g==hw.generation && hw.loaded);}
bool sync_io_switch_load(uint32_t mask,uint32_t *out)
{
    assert(core==0 && depth==1 && out && mask && mask<=15);++load_calls;
    if(reentrant){assert(trigger_switch_load(10,dep.active.crc)==SWITCH_BUSY);assert(trigger_switch_stage(0)==SWITCH_BUSY);}
    if(!load_ok)return false;
    assert(!hw.loaded);hw=(sync_io_switch_snapshot_t){.schema=1,.generation=++generation,
        .state=SYNC_IO_SWITCH_LOADED,.loaded=1,.output_mask=mask};*out=hw.generation;return true;
}
bool sync_io_switch_stage(uint32_t g,uint32_t code)
{
    mutate(g);++stage_calls;assert(!(code&~hw.output_mask));
    if(!stage_ok || hw.state==SYNC_IO_SWITCH_PENDING)return false;
    hw.desired_code=code;++hw.stage_sequence;hw.state=SYNC_IO_SWITCH_STAGED;return true;
}
bool sync_io_switch_apply(uint32_t g)
{
    mutate(g);++apply_calls;
    if(!apply_ok || hw.state!=SYNC_IO_SWITCH_STAGED)return false;
    hw.state=SYNC_IO_SWITCH_PENDING;return true;
}
bool sync_io_switch_stop(uint32_t g)
{
    mutate(g);++stop_calls;if(!stop_ok)return false;
    if(hw.state==SYNC_IO_SWITCH_PENDING || hw.state==SYNC_IO_SWITCH_STAGED)hw.cancelled_sequence=hw.stage_sequence;
    hw.actual_output_mask=0;hw.applied_valid=0;hw.state=SYNC_IO_SWITCH_STOPPED;return true;
}
bool sync_io_switch_release(uint32_t g)
{
    mutate(g);++release_calls;if(!release_ok)return false;
    hw.loaded=0;hw.state=SYNC_IO_SWITCH_IDLE;return true;
}
bool sync_io_switch_snapshot(sync_io_switch_snapshot_t *out)
{
    if(!snapshot_ok)return false;
    if(complete && hw.state==SYNC_IO_SWITCH_PENDING) {
        hw.state=SYNC_IO_SWITCH_COMPLETE;hw.applied_code=hw.desired_code;hw.applied_valid=1;
        hw.actual_output_mask=hw.applied_code;hw.completed_sequence=hw.stage_sequence;
    }
    *out=hw;return true;
}
static void setup(void)
{
    dep.active=(trigger_deployment_binding_t){.valid=1,.local_uid=2,.local_role=1,.local_slot=1,
        .ring_config=7,.schedule_crc=123,.table={.generation=10,.rows={
        {.board_uid=1,.tdma_slot=0,.input_mask=1},
        {.board_uid=2,.tdma_slot=1,.output_mask=7},
        {.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},
        {.board_uid=4,.tdma_slot=3,.output_mask=1}}}};
    dep.active.crc=trigger_deployment_crc(&dep.active.table);
}
static void load(void){assert(trigger_switch_load(10,dep.active.crc)==SWITCH_OK && trigger_switch_is_loaded());}
static trigger_switch_snapshot_t snapshot(void)
{trigger_switch_snapshot_t out;assert(trigger_switch_snapshot(&out)==SWITCH_OK);return out;}
int main(int argc,char **argv)
{
    assert(argc==2);const char *n=argv[1];setup();
    if(!strcmp(n,"null_snapshot")){assert(trigger_switch_snapshot(NULL)==SWITCH_ARGUMENT);return 0;}
    if(!strcmp(n,"empty")) {
        assert(trigger_switch_stage(0)==SWITCH_NOT_LOADED && trigger_switch_apply()==SWITCH_NOT_LOADED);
        assert(trigger_switch_stop()==SWITCH_NOT_LOADED && trigger_switch_unload()==SWITCH_NOT_LOADED);
        assert(!load_calls && !stage_calls && !apply_calls);return 0;
    }
    if(!strncmp(n,"identity_",9)) {
        unsigned role,slot;assert(sscanf(n,"identity_%u_%u",&role,&slot)==2);
        static const char *ids[]={"0000000000000001","0000000000000002","0000000000000003","0000000000000004"};
        dep.active.table.rows[slot].board_uid=role+1;dep.active.table.rows[slot].tdma_slot=role;
        dep.active.table.rows[role].board_uid=slot+1;dep.active.table.rows[role].tdma_slot=slot;
        dep.active.local_uid=slot+1;dep.active.local_role=role;dep.active.local_slot=slot;
        staged.local_slot_id=slot;serial=ids[slot];dep.active.crc=trigger_deployment_crc(&dep.active.table);
        load();assert(trigger_switch_stage(0)==SWITCH_OK && trigger_switch_apply()==SWITCH_OK);
        assert(snapshot().binding.local_uid==slot+1 && snapshot().binding.local_role==role);
        assert(trigger_switch_stop()==SWITCH_OK && trigger_switch_unload()==SWITCH_OK);return 0;
    }
    if(!strncmp(n,"code_",5) || !strncmp(n,"outside_",8)) {
        unsigned mask,code=0;const bool outside=!strncmp(n,"outside_",8);
        if(outside){mask=(unsigned)atoi(n+8);code=16u;for(unsigned b=1;b<16;b<<=1)if(!(mask&b)){code=b;break;}}
        else assert(sscanf(n,"code_%u_%u",&mask,&code)==2);
        dep.active.table.rows[1].output_mask=mask;dep.active.crc=trigger_deployment_crc(&dep.active.table);load();
        if(outside){assert(trigger_switch_stage(code)==SWITCH_ARGUMENT && !stage_calls && !hw.actual_output_mask);return 0;}
        assert(hw.output_mask==mask && trigger_switch_stage(code)==SWITCH_OK);
        assert(hw.desired_code==code && !hw.actual_output_mask && !hw.applied_valid && !apply_calls);
        assert(trigger_switch_apply()==SWITCH_OK);assert(!hw.completed_sequence);
        complete=true;assert(snapshot().io.applied_code==code && snapshot().io.completed_sequence==1);
        assert(trigger_switch_stop()==SWITCH_OK && trigger_switch_unload()==SWITCH_OK);return 0;
    }
    uint32_t gen=10,crc=dep.active.crc,expected=SWITCH_CHANGED;bool reject=true;
    if(!strcmp(n,"zero_generation")){gen=0;expected=SWITCH_ARGUMENT;}
    else if(!strcmp(n,"core")){core=1;expected=SWITCH_BUSY;}
    else if(!strcmp(n,"maintenance_busy")){maintenance=false;expected=SWITCH_NOT_IDLE;}
    else if(!strcmp(n,"running")){ring.enabled=1;expected=SWITCH_NOT_IDLE;}
    else if(!strcmp(n,"adapter")){ring.adapter_started=1;expected=SWITCH_NOT_IDLE;}
    else if(!strcmp(n,"pending_config")){++ring.config_seq;expected=SWITCH_NOT_IDLE;}
    else if(!strcmp(n,"no_deployment")){dep_ok=false;expected=SWITCH_DEPLOYMENT;}
    else if(!strcmp(n,"inactive_deployment")){dep.active.valid=0;expected=SWITCH_DEPLOYMENT;}
    else if(!strcmp(n,"bad_uid"))serial="000000000000000F";
    else if(!strcmp(n,"null_uid"))serial=NULL;
    else if(!strcmp(n,"short_uid"))serial="2";
    else if(!strcmp(n,"wrong_generation"))++gen;
    else if(!strcmp(n,"wrong_crc"))++crc;
    else if(!strcmp(n,"corrupt_table"))++dep.active.table.rows[1].output_mask;
    else if(!strcmp(n,"wrong_role_counter")){dep.active.local_role=0;expected=SWITCH_WRONG_ROLE;}
    else if(!strcmp(n,"wrong_role_gateway")){dep.active.local_role=2;expected=SWITCH_WRONG_ROLE;}
    else if(!strcmp(n,"wrong_slot"))++staged.local_slot_id;
    else if(!strcmp(n,"wrong_schedule"))++staged.schedule_crc32;
    else if(!strcmp(n,"wrong_ring"))++dep.active.ring_config;
    else if(!strcmp(n,"missing_staged"))staged_ok=false;
    else if(!strcmp(n,"disabled_staged"))staged.enabled=0;
    else if(!strcmp(n,"missing_ring"))ring_ok=false;
    else if(!strcmp(n,"mask_zero") || !strcmp(n,"mask_range")) {
        dep.active.table.rows[1].output_mask=!strcmp(n,"mask_zero")?0:16;
        crc=dep.active.crc=trigger_deployment_crc(&dep.active.table);
    }
    else if(!strcmp(n,"counter_loaded")){counter_loaded=true;expected=SWITCH_BUSY;}
    else if(!strcmp(n,"gateway_loaded")){gateway_loaded=true;expected=SWITCH_BUSY;}
    else reject=false;
    if(reject){uint32_t r=trigger_switch_load(gen,crc);if(r!=expected)fprintf(stderr,"%s %u != %u\n",n,r,expected);assert(r==expected && !load_calls && !hw.loaded);return 0;}
    if(!strcmp(n,"load_failure")){load_ok=false;assert(trigger_switch_load(gen,crc)==SWITCH_HARDWARE && !trigger_switch_is_loaded());load_ok=true;load();return 0;}
    reentrant=!strcmp(n,"reentrant");load();const trigger_switch_snapshot_t loaded=snapshot();
    if(reentrant)return 0;
    if(!strcmp(n,"duplicate_load")){assert(trigger_switch_load(gen,crc)==SWITCH_BUSY && load_calls==1);return 0;}
    if(!strncmp(n,"stage_changed_",14) || !strncmp(n,"apply_changed_",14)) {
        bool apply=n[0]=='a';if(apply)assert(trigger_switch_stage(3)==SWITCH_OK);
        const char *f=n+14;
        if(!strcmp(f,"generation"))++dep.active.table.generation;
        else if(!strcmp(f,"crc"))++dep.active.crc;
        else if(!strcmp(f,"uid"))serial="000000000000000F";
        else if(!strcmp(f,"role"))dep.active.local_role=3;
        else if(!strcmp(f,"slot"))++staged.local_slot_id;
        else if(!strcmp(f,"schedule"))++staged.schedule_crc32;
        else if(!strcmp(f,"ring")){++ring.config_seq;++ring.applied_config_seq;}
        else if(!strcmp(f,"mask")){dep.active.table.rows[1].output_mask=1;dep.active.crc=trigger_deployment_crc(&dep.active.table);}
        else assert(false);
        unsigned stages=stage_calls;
        assert((apply?trigger_switch_apply():trigger_switch_stage(1))!=SWITCH_OK);
        assert(!apply_calls && stage_calls==stages && !hw.actual_output_mask);return 0;
    }
    if(!strcmp(n,"snapshot_busy")){trigger_switch_snapshot_t s;memset(&s,0xA5,sizeof(s));trigger_switch_snapshot_t old=s;snapshot_ok=false;assert(trigger_switch_snapshot(&s)==SWITCH_BUSY && !memcmp(&s,&old,sizeof(s)));return 0;}
    if(!strcmp(n,"stage_failure")){stage_ok=false;assert(trigger_switch_stage(1)==SWITCH_HARDWARE && !hw.desired_code);return 0;}
    assert(trigger_switch_stage(3)==SWITCH_OK && !hw.actual_output_mask);
    if(!strcmp(n,"apply_failure")){apply_ok=false;assert(trigger_switch_apply()==SWITCH_HARDWARE && hw.state==SYNC_IO_SWITCH_STAGED);return 0;}
    if(!strcmp(n,"stop_staged")){assert(trigger_switch_stop()==SWITCH_OK && hw.cancelled_sequence==1 && !hw.completed_sequence);return 0;}
    assert(trigger_switch_apply()==SWITCH_OK && hw.state==SYNC_IO_SWITCH_PENDING);
    assert(trigger_switch_apply()!=SWITCH_OK && trigger_switch_stage(1)!=SWITCH_OK);
    assert(hw.stage_sequence==1 && hw.desired_code==3 && !hw.completed_sequence);
    if(!strcmp(n,"stop_failure")){stop_ok=false;assert(trigger_switch_stop()==SWITCH_HARDWARE && trigger_switch_is_loaded());return 0;}
    if(!strcmp(n,"stop_pending")){assert(trigger_switch_stop()==SWITCH_OK && hw.cancelled_sequence==1 && !hw.completed_sequence && !hw.applied_valid);return 0;}
    if(!strcmp(n,"cleanup_changed")){++dep.active.table.generation;serial=NULL;assert(trigger_switch_stop()==SWITCH_OK && trigger_switch_unload()==SWITCH_OK);assert(snapshot().binding.local_uid==2);return 0;}
    complete=true;trigger_switch_snapshot_t done=snapshot();assert(done.io.completed_sequence==1 && done.io.applied_code==3);
    unsigned applies=apply_calls;assert(snapshot().io.completed_sequence==1 && apply_calls==applies);
    assert(trigger_switch_apply()!=SWITCH_OK);assert(trigger_switch_stage(3)==SWITCH_OK && trigger_switch_apply()==SWITCH_OK);
    assert(snapshot().io.completed_sequence==2);assert(trigger_switch_stop()==SWITCH_OK);
    if(!strcmp(n,"release_failure")){release_ok=false;assert(trigger_switch_unload()==SWITCH_HARDWARE && trigger_switch_is_loaded());return 0;}
    assert(!strcmp(n,"lifecycle"));assert(trigger_switch_unload()==SWITCH_OK && !trigger_switch_is_loaded());
    trigger_switch_snapshot_t retired=snapshot();assert(!memcmp(&loaded.binding,&retired.binding,sizeof(loaded.binding)));
    assert(retired.io.completed_sequence==2 && !retired.io.loaded);load();assert(snapshot().io.generation>loaded.io.generation);return 0;
}
'''


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    libscpi = ROOT/'third_party/scpi-parser/libscpi/src'
    return compile_grid(tmp_path_factory.mktemp('switch-parser'), 'parser', PARSER,
                        sorted(libscpi.glob('*.c')), parser=True)


def parse_switch(parser, command, mode='accept'):
    result = subprocess.run([str(parser), command, mode], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout+result.stderr
    return list(map(int, result.stdout.strip().split(',')))


@pytest.mark.parametrize('command', [
    'TRIG:RES:SWIT:LOAD 10,4294967295',
    'TRIGGER:RESERVATION:SWITCH:LOAD +00010,+4294967295',
    'TRIG:RES:SWIT:LOAD 4294967295,4294967295',
    'TRIG:RES:SWIT:STAG 0', 'TRIG:RES:SWIT:STAG 4294967295',
    'TRIG:RES:SWIT:APPL', 'TRIG:RES:SWIT:STOP', 'TRIG:RES:SWIT:UNL',
])
def test_switch_scpi_legal_ack(parser, command):
    assert parse_switch(parser, command) == [1, 0]


def test_switch_scpi_41_fields_preserve_uid_and_two_groups(parser):
    fields = parse_switch(parser, 'TRIG:RES:SWIT?')
    assert fields == [1, 0, 1, 10, 4294967295, 9007199254740993, 3, 0, 7, 123,
                      1, 19, 4, 1, 5, 2, 5, 5, 1, 23, 23, 22, 10, 0, 4,
                      9, 5, 3, 0, 1, 16, 1, 1, 2, 3, 3, 18, 1, 1, 4, 5]
    assert len(fields) == 41


@pytest.mark.parametrize('command', [
    'TRIG:RES:SWIT:LOAD', 'TRIG:RES:SWIT:LOAD 10',
    'TRIG:RES:SWIT:LOAD 4294967296,1', 'TRIG:RES:SWIT:LOAD 10,4294967296',
    'TRIG:RES:SWIT:LOAD -1,1', 'TRIG:RES:SWIT:LOAD 10,-1',
    'TRIG:RES:SWIT:LOAD 1e1,1', 'TRIG:RES:SWIT:LOAD 10,1.0',
    'TRIG:RES:SWIT:LOAD "10",1', 'TRIG:RES:SWIT:LOAD 10 ,1',
    'TRIG:RES:SWIT:LOAD 10,1,', 'TRIG:RES:SWIT:LOAD 10,1,2',
    'TRIG:RES:SWIT:STAG', 'TRIG:RES:SWIT:STAG 4294967296',
    'TRIG:RES:SWIT:STAG -1', 'TRIG:RES:SWIT:STAG 1.0',
    'TRIG:RES:SWIT:STAG 1e1', 'TRIG:RES:SWIT:STAG #H0f',
    'TRIG:RES:SWIT:STAG 1,2', 'TRIG:RES:SWIT:STAG 1,',
    *[f'TRIG:RES:SWIT:{op} {arg}' for op in ('APPL', 'STOP', 'UNL') for arg in ('1', '1,2', '"x"')],
    'TRIG:RES:SWIT? 1',
])
def test_switch_scpi_invalid_parameters_do_not_call_owner(parser, command):
    assert parse_switch(parser, command, 'reject') == [1, 1]


@pytest.mark.parametrize('command', ['TRIG:RES:SWIT:LOAD 10,4294967295',
    'TRIG:RES:SWIT:STAG 0', 'TRIG:RES:SWIT:APPL', 'TRIG:RES:SWIT:STOP',
    'TRIG:RES:SWIT:UNL', 'TRIG:RES:SWIT?'])
def test_switch_scpi_owner_busy_returns_two_fields(parser, command):
    assert parse_switch(parser, command, 'owner_reject') == [1, 2]


PARSER = DEPLOYMENT_PARSER.split('static const scpi_command_t commands[]')[0] + r'''
#include "trigger_switch_service.h"
uint32_t trigger_switch_load(uint32_t generation,uint32_t crc)
{++calls;assert((generation==10 || generation==UINT32_MAX) && crc==UINT32_MAX);return result;}
uint32_t trigger_switch_stage(uint32_t code)
{++calls;assert(code==0 || code==UINT32_MAX);return result;}
uint32_t trigger_switch_apply(void){++calls;return result;}
uint32_t trigger_switch_stop(void){++calls;return result;}
uint32_t trigger_switch_unload(void){++calls;return result;}
uint32_t trigger_switch_snapshot(trigger_switch_snapshot_t *out)
{
    ++calls;if(result)return result;
    *out=(trigger_switch_snapshot_t){.loaded=1,.binding={.table={.generation=10},
        .crc=UINT32_MAX,.local_uid=UINT64_C(9007199254740993),.local_role=3,
        .local_slot=0,.ring_config=7,.schedule_crc=123},
        .io={.schema=1,.generation=19,.state=4,.loaded=1,.output_mask=5,.group_count=2,
            .desired_code=5,.applied_code=5,.applied_valid=1,.stage_sequence=23,
            .completed_sequence=23,.cancelled_sequence=22,.sm_claimed_mask=10,
            .sm_enabled_mask=0,.instruction_words=4,.program_offset=9,
            .actual_output_mask=5,.receipt_mask=3,.fault=0,
            .groups={{.sm=1,.output_base_pin=16,.output_count=1,.code=1,.tx_level=2,.rx_level=3},
                     {.sm=3,.output_base_pin=18,.output_count=1,.code=1,.tx_level=4,.rx_level=5}}}};
    return 0;
}
#include "scpi_trigger_switch_commands.inc"
static const scpi_command_t commands[]={
    {.pattern="TRIGger:REServation:SWITch:LOAD",.callback=scpi_cmd_trigger_switch_load},
    {.pattern="TRIGger:REServation:SWITch:STAGe",.callback=scpi_cmd_trigger_switch_stage},
    {.pattern="TRIGger:REServation:SWITch:APPLy",.callback=scpi_cmd_trigger_switch_apply},
    {.pattern="TRIGger:REServation:SWITch:STOP",.callback=scpi_cmd_trigger_switch_stop},
    {.pattern="TRIGger:REServation:SWITch:UNLoad",.callback=scpi_cmd_trigger_switch_unload},
    {.pattern="TRIGger:REServation:SWITch?",.callback=scpi_cmd_trigger_switch_q},
    SCPI_CMD_LIST_END};
int main(int argc,char **argv)
{
    assert(argc==3);char input[2048],line[2048];scpi_t c;scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&c,commands,&interface,scpi_units_def,"a","b","c","d",input,sizeof(input),errors,16);
    if(!strcmp(argv[2],"owner_reject"))result=SWITCH_BUSY;
    const int n=snprintf(line,sizeof(line),"%s\n",argv[1]);assert(n>0 && (size_t)n<sizeof(line));
    (void)SCPI_Input(&c,line,n);
    if(!strcmp(argv[2],"reject"))assert(!calls);
    else assert(calls==1 && !SCPI_ErrorCount(&c));
    assert(response_size);printf("%s",response);return 0;
}
'''
