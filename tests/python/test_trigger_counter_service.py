"""Compile the production COUNTER owner with hardware/identity boundary stubs.

The reference branch's role resolver motivates local identity/claim lifetime
checks only. This harness uses this branch's active deployment and real C owner,
not its legacy board-number role model or a Python reimplementation.
"""
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid
from test_trigger_deployment_service import PARSER as DEPLOYMENT_PARSER


@pytest.fixture(scope='module')
def owner(tmp_path_factory):
    directory = tmp_path_factory.mktemp('trigger-counter-owner')
    (directory/'pico').mkdir()
    for name in ('pico.h', 'pico/platform.h'):
        (directory/name).write_text('unsigned get_core_num(void);\n', encoding='utf-8')
    (directory/'board_identity.h').write_text('const char *board_identity_serial(void);\n', encoding='utf-8')
    (directory/'tdma_runtime_owner.h').write_text(
        '#include "tdma_service.h"\n'
        'tdma_service_service_t *tdma_runtime_owner_get(void);\n'
        'bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out);\n'
        'bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out);\n',
        encoding='utf-8')
    return compile_grid(directory, 'owner', HARNESS,
                        [ROOT/'components/sync_trigger/src/trigger_counter_service.c'])


@pytest.mark.parametrize('case', [
    'lifecycle', 'unconfigured', 'null_snapshot', 'core', 'running_ring',
    'pending_ring', 'started_adapter', 'maintenance_busy', 'trigger_busy',
    'missing_staged', 'disabled_staged', 'missing_ring', 'missing_deployment',
    'invalid_active', 'wrong_generation', 'wrong_crc', 'wrong_uid', 'null_uid',
    'malformed_uid', 'wrong_role_1', 'wrong_role_2', 'wrong_role_3',
    'wrong_staged_slot', 'wrong_staged_schedule', 'wrong_ring_config',
    'load_hardware_failure', 'start_hardware_failure', 'stop_hardware_failure',
    'clear_hardware_failure', 'release_hardware_failure', 'snapshot_failure',
    'start_changed_generation', 'start_changed_crc', 'start_changed_uid',
    'start_changed_role', 'start_changed_staged_slot', 'start_changed_schedule',
    'start_changed_ring', 'start_changed_input', 'clear_running',
    'counter_on_other_board', 'snapshot_preserves_timestamp_validity',
    'input_port_1', 'input_port_2', 'input_port_3', 'input_port_4',
    'read_core1', 'read_core1_busy', 'read_core1_arguments',
])
def test_actual_counter_owner(owner, case):
    result = subprocess.run([str(owner), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trigger_counter_service.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"

/* Public result numbers are append-only protocol semantics, not a copy of
 * the owner's branching logic. Rejections must never mutate hardware. */
enum { OK=0, ARGUMENT=1, BUSY=2, NOT_IDLE=3, DEPLOYMENT=4,
       WRONG_ROLE=5, CHANGED=6, HARDWARE=7, NOT_LOADED=8 };
static unsigned core, maintenance_depth, mutation_calls, load_calls, start_calls;
static unsigned stop_calls, clear_calls, release_calls;
static bool maintenance_ok=true, staged_ok=true, ring_ok=true, deployment_ok=true;
static bool load_ok=true, start_ok=true, stop_ok=true, clear_ok=true, release_ok=true, snapshot_ok=true;
static uint32_t trigger_state, lease_generation;
static const char *serial="0000000000000001";
static tdma_ring_clock_snapshot_t ring={.config_seq=7,.applied_config_seq=7};
static tdma_service_ring_runtime_config_t staged={.enabled=1,.local_slot_id=0,.schedule_crc32=123};
static trigger_deployment_snapshot_t deployment;
static sync_io_pulse_counter_snapshot_t hw;

unsigned get_core_num(void) {return core;}
const char *board_identity_serial(void) {return serial;}
void sync_trigger_get_status(sync_trigger_status_t *out)
{memset(out,0,sizeof(*out));out->state=trigger_state;}
tdma_service_service_t *tdma_runtime_owner_get(void) {return NULL;}
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{if(!ring_ok)return false;*out=ring;return true;}
bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out)
{if(!staged_ok)return false;*out=staged;return true;}
bool tdma_service_run_stopped_maintenance(tdma_service_service_t *service,
    bool (*callback)(void *),void *context)
{
    (void)service;
    if(!maintenance_ok || ring.enabled || ring.adapter_started || ring.config_seq!=ring.applied_config_seq)
        return false;
    assert(!maintenance_depth);++maintenance_depth;
    bool result=callback(context);
    --maintenance_depth;return result;
}
/* Hardware mutations in a metadata callback would violate its SRAM-only
 * contract even when a mock happened to accept them. */
bool tdma_service_update_stopped_metadata(tdma_service_service_t *service,
    bool (*callback)(void *),void *context)
{(void)service;(void)callback;(void)context;assert(!"metadata gate used for hardware owner");return false;}
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *out)
{
    if(!deployment_ok)return TRIGGER_DEPLOYMENT_SERVICE_BUSY;
    if(trigger_state!=TRIG_STATE_IDLE)return TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;
    *out=deployment;return 0;
}

static void mutation(void) {assert(core==0 && maintenance_depth==1);++mutation_calls;}
bool sync_io_pulse_counter_load(uint32_t port,uint32_t *generation)
{
    mutation();++load_calls;assert(port>=1 && port<=4 && generation);
    if(!load_ok)return false;
    assert(hw.state==SYNC_IO_PULSE_COUNTER_IDLE);
    hw=(sync_io_pulse_counter_snapshot_t){.schema=1,.generation=++lease_generation,
        .state=SYNC_IO_PULSE_COUNTER_LOADED,.input_port=port,.input_pin=24u-port,
        .valid=1,.tick_hz=250000000,.timestamp_valid=1,
        .sample_before_tick=100,.sample_after_tick=120};
    *generation=hw.generation;return true;
}
bool sync_io_pulse_counter_start(uint32_t generation)
{
    mutation();++start_calls;assert(generation==hw.generation);
    if(!start_ok || (hw.state!=SYNC_IO_PULSE_COUNTER_LOADED && hw.state!=SYNC_IO_PULSE_COUNTER_STOPPED))return false;
    hw.state=SYNC_IO_PULSE_COUNTER_RUNNING;return true;
}
bool sync_io_pulse_counter_stop(uint32_t generation)
{
    mutation();++stop_calls;assert(generation==hw.generation);
    if(!stop_ok)return false;
    hw.state=SYNC_IO_PULSE_COUNTER_STOPPED;return true;
}
bool sync_io_pulse_counter_clear(uint32_t generation)
{
    mutation();++clear_calls;assert(generation==hw.generation);
    if(!clear_ok || hw.state==SYNC_IO_PULSE_COUNTER_RUNNING)return false;
    hw.count32=0;++hw.clear_generation;return true;
}
bool sync_io_pulse_counter_release(uint32_t generation)
{
    mutation();++release_calls;assert(generation==hw.generation);
    if(!release_ok || hw.state==SYNC_IO_PULSE_COUNTER_RUNNING)return false;
    hw.state=SYNC_IO_PULSE_COUNTER_IDLE;return true;
}
bool sync_io_pulse_counter_snapshot(sync_io_pulse_counter_snapshot_t *out)
{if(!snapshot_ok)return false;*out=hw;return true;}
bool sync_io_pulse_counter_snapshot_core1(sync_io_pulse_counter_snapshot_t *out)
{return sync_io_pulse_counter_snapshot(out);}
bool sync_io_pulse_counter_read_core1(uint32_t *out)
{if(!snapshot_ok)return false;*out=hw.count32;return true;}

static void setup(void)
{
    deployment.active=(trigger_deployment_binding_t){.valid=1,.local_uid=1,
        .local_role=0,.local_slot=0,.ring_config=7,.schedule_crc=123,
        .table={.generation=10,.rows={
            {.board_uid=1,.tdma_slot=0,.input_mask=1},
            {.board_uid=2,.tdma_slot=1,.output_mask=7},
            {.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},
            {.board_uid=4,.tdma_slot=3,.output_mask=1}}}};
    deployment.active.crc=trigger_deployment_crc(&deployment.active.table);
}
static trigger_counter_snapshot_t snapshot(void)
{trigger_counter_snapshot_t s;assert(trigger_counter_snapshot(&s)==OK);return s;}
static void load(void)
{assert(trigger_counter_load(10,deployment.active.crc)==OK);assert(trigger_counter_is_loaded());}
static void check_result(const char *name,uint32_t actual,uint32_t expected)
{if(actual!=expected)fprintf(stderr,"%s: got %u expected %u\n",name,actual,expected);assert(actual==expected);}

int main(int argc,char **argv)
{
    assert(argc==2);const char *name=argv[1];setup();
    if(!strncmp(name,"read_core1",10)) {
        uint32_t count=0xa5a5u;
        core=1;assert(trigger_counter_read_core1(&count)==ARGUMENT&&count==0xa5a5u);
        core=0;load();assert(trigger_counter_start()==OK);hw.count32=12345;
        assert(trigger_counter_read_core1(&count)==ARGUMENT&&count==0xa5a5u);
        core=1;assert(trigger_counter_read_core1(NULL)==ARGUMENT);
        if(!strcmp(name,"read_core1_busy")) {
            snapshot_ok=false;assert(trigger_counter_read_core1(&count)==BUSY&&count==0xa5a5u);
        } else {assert(trigger_counter_read_core1(&count)==OK&&count==12345);}
        return 0;
    }
    if(!strcmp(name,"null_snapshot")) {assert(trigger_counter_snapshot(NULL)==ARGUMENT);return 0;}
    if(!strcmp(name,"unconfigured")) {
        assert(!trigger_counter_is_loaded());
        assert(trigger_counter_start()==NOT_LOADED);
        assert(trigger_counter_clear()==NOT_LOADED);
        assert(!mutation_calls);return 0;
    }
    if(!strcmp(name,"counter_on_other_board")) {
        /* Same role and route, another UID AND physical ring slot: no NO1
         * special case may authorize or reject this legitimate deployment. */
        deployment.active.table.rows[0].board_uid=4;deployment.active.table.rows[0].tdma_slot=3;
        deployment.active.table.rows[3].board_uid=1;deployment.active.table.rows[3].tdma_slot=0;
        deployment.active.local_uid=4;deployment.active.local_slot=3;staged.local_slot_id=3;
        deployment.active.crc=trigger_deployment_crc(&deployment.active.table);serial="0000000000000004";
        load();assert(trigger_counter_start()==OK);assert(snapshot().binding.local_uid==4);
        assert(trigger_counter_stop()==OK);assert(trigger_counter_unload()==OK);return 0;
    }
    if(!strncmp(name,"input_port_",11)) {
        const uint32_t port=(uint32_t)atoi(name+11);
        assert(port>=1 && port<=4);
        deployment.active.table.rows[0].input_mask=1u<<(port-1u);
        deployment.active.crc=trigger_deployment_crc(&deployment.active.table);
        load();trigger_counter_snapshot_t s=snapshot();
        /* This assertion observes the actual port argument crossing the
         * production owner/backend boundary, not a Python mask resolver. */
        assert(s.io.input_port==port && s.io.input_pin==24u-port);
        assert(s.binding.table.rows[0].input_mask==(1u<<(port-1u)));
        assert(trigger_counter_start()==OK);hw.count32=123;
        assert(trigger_counter_stop()==OK && snapshot().io.count32==123);
        assert(trigger_counter_unload()==OK && !trigger_counter_is_loaded());
        return 0;
    }
    uint32_t requested_generation=10,requested_crc=deployment.active.crc,expected=DEPLOYMENT;
    bool load_reject=true;
    if(!strcmp(name,"core")){core=1;expected=BUSY;}
    else if(!strcmp(name,"running_ring")){ring.enabled=1;expected=NOT_IDLE;}
    else if(!strcmp(name,"pending_ring")){++ring.config_seq;expected=NOT_IDLE;}
    else if(!strcmp(name,"started_adapter")){ring.adapter_started=1;expected=NOT_IDLE;}
    else if(!strcmp(name,"maintenance_busy")){maintenance_ok=false;expected=NOT_IDLE;}
    else if(!strcmp(name,"trigger_busy")){trigger_state=1;expected=DEPLOYMENT;}
    else if(!strcmp(name,"missing_staged"))staged_ok=false;
    else if(!strcmp(name,"disabled_staged"))staged.enabled=0;
    else if(!strcmp(name,"missing_ring"))ring_ok=false;
    else if(!strcmp(name,"missing_deployment"))deployment_ok=false;
    else if(!strcmp(name,"invalid_active"))deployment.active.valid=0;
    else if(!strcmp(name,"wrong_generation"))++requested_generation;
    else if(!strcmp(name,"wrong_crc"))++requested_crc;
    else if(!strcmp(name,"wrong_uid"))serial="000000000000000F";
    else if(!strcmp(name,"null_uid"))serial=NULL;
    else if(!strcmp(name,"malformed_uid"))serial="000000000000000X";
    else if(!strncmp(name,"wrong_role_",11)) {
        uint32_t role=(uint32_t)atoi(name+11);
        static const char *ids[]={"0000000000000001","0000000000000002","0000000000000003","0000000000000004"};
        deployment.active.local_role=role;deployment.active.local_uid=role+1;
        deployment.active.local_slot=role;staged.local_slot_id=role;serial=ids[role];expected=WRONG_ROLE;
    }
    else if(!strcmp(name,"wrong_staged_slot"))++staged.local_slot_id;
    else if(!strcmp(name,"wrong_staged_schedule"))++staged.schedule_crc32;
    else if(!strcmp(name,"wrong_ring_config"))++deployment.active.ring_config;
    else if(!strcmp(name,"load_hardware_failure")){load_ok=false;expected=HARDWARE;}
    else load_reject=false;
    if(load_reject) {
        uint32_t result=trigger_counter_load(requested_generation,requested_crc);
        /* Some unavailable runtime facts may be classified NOT_IDLE rather
         * than DEPLOYMENT; authority and no hardware mutation are the point. */
        if(expected==DEPLOYMENT)assert(result==DEPLOYMENT || result==CHANGED || result==NOT_IDLE || result==WRONG_ROLE);
        else check_result(name,result,expected);
        assert(hw.state==SYNC_IO_PULSE_COUNTER_IDLE);
        assert(load_calls==(!strcmp(name,"load_hardware_failure")?1u:0u));
        if(!strcmp(name,"load_hardware_failure")) {
            assert(!trigger_counter_is_loaded());load_ok=true;load();
        }
        return 0;
    }
    load();const trigger_counter_snapshot_t loaded=snapshot();
    if(!strncmp(name,"start_changed_",14)) {
        const char *what=name+14;
        if(!strcmp(what,"generation"))++deployment.active.table.generation;
        else if(!strcmp(what,"crc"))++deployment.active.crc;
        else if(!strcmp(what,"uid"))serial="000000000000000F";
        else if(!strcmp(what,"role"))deployment.active.local_role=1;
        else if(!strcmp(what,"staged_slot"))++staged.local_slot_id;
        else if(!strcmp(what,"schedule"))++staged.schedule_crc32;
        else if(!strcmp(what,"ring")){++ring.config_seq;++ring.applied_config_seq;}
        else if(!strcmp(what,"input")){deployment.active.table.rows[0].input_mask=2;deployment.active.crc=trigger_deployment_crc(&deployment.active.table);}
        else assert(false);
        assert(trigger_counter_start()!=OK && !start_calls && trigger_counter_is_loaded());
        assert(hw.state==SYNC_IO_PULSE_COUNTER_LOADED);return 0;
    }
    if(!strcmp(name,"start_hardware_failure")) {
        start_ok=false;assert(trigger_counter_start()==HARDWARE);
        assert(trigger_counter_is_loaded() && hw.state==SYNC_IO_PULSE_COUNTER_LOADED);return 0;
    }
    assert(trigger_counter_start()==OK);hw.count32=1001;hw.sample_before_tick=250000100;hw.sample_after_tick=250000120;
    if(!strcmp(name,"clear_running")) {
        assert(trigger_counter_clear()!=OK && hw.count32==1001 && hw.state==SYNC_IO_PULSE_COUNTER_RUNNING);return 0;
    }
    if(!strcmp(name,"snapshot_failure")) {
        trigger_counter_snapshot_t out;memset(&out,0xA5,sizeof(out));trigger_counter_snapshot_t before=out;
        snapshot_ok=false;assert(trigger_counter_snapshot(&out)==BUSY);
        assert(!memcmp(&out,&before,sizeof(out)));return 0;
    }
    if(!strcmp(name,"snapshot_preserves_timestamp_validity")) {
        hw.timestamp_valid=0;hw.valid=1;
        trigger_counter_snapshot_t s=snapshot();assert(s.io.count32==1001 && !s.io.timestamp_valid && s.io.valid);
        assert(!memcmp(&s.binding,&loaded.binding,sizeof(s.binding)));return 0;
    }
    if(!strcmp(name,"stop_hardware_failure")) {
        stop_ok=false;assert(trigger_counter_stop()==HARDWARE);
        assert(trigger_counter_is_loaded() && hw.state==SYNC_IO_PULSE_COUNTER_RUNNING);return 0;
    }
    assert(trigger_counter_stop()==OK);
    trigger_counter_snapshot_t frozen=snapshot();assert(frozen.io.count32==1001 && frozen.io.state==SYNC_IO_PULSE_COUNTER_STOPPED);
    assert(snapshot().io.count32==frozen.io.count32);
    if(!strcmp(name,"release_hardware_failure")) {
        release_ok=false;assert(trigger_counter_unload()==HARDWARE);assert(trigger_counter_is_loaded());return 0;
    }
    if(!strcmp(name,"clear_hardware_failure")) {
        clear_ok=false;assert(trigger_counter_clear()==HARDWARE);assert(snapshot().io.count32==1001);return 0;
    }
    assert(!strcmp(name,"lifecycle"));
    assert(trigger_counter_clear()==OK);assert(snapshot().io.count32==0 && snapshot().io.clear_generation==frozen.io.clear_generation+1);
    assert(trigger_counter_start()==OK);hw.count32=17;assert(trigger_counter_stop()==OK);
    assert(trigger_counter_unload()==OK && !trigger_counter_is_loaded());
    trigger_counter_snapshot_t retired=snapshot();assert(!retired.loaded && retired.io.count32==17);
    assert(!memcmp(&retired.binding,&loaded.binding,sizeof(retired.binding)));
    load();assert(snapshot().io.generation>loaded.io.generation && !snapshot().io.count32);
    assert(load_calls==2 && start_calls==2 && stop_calls==2 && clear_calls==1 && release_calls==1);
    return 0;
}
'''


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    library = ROOT/'third_party/scpi-parser/libscpi/src'
    units = [ROOT / 'middleware/scpi_port/src/scpi_trigger_grid_commands.c',
             ROOT / 'middleware/scpi_port/src/scpi_trigger_deployment_commands.c',
             ROOT / 'middleware/scpi_port/src/scpi_trigger_counter_commands.c']
    return compile_grid(tmp_path_factory.mktemp('counter-parser'), 'parser', COUNTER_PARSER,
                        [*units, *sorted(library.glob('*.c'))], parser=True)


def parse_counter(parser, command, mode='accept'):
    result = subprocess.run([str(parser), command, mode], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    return list(map(int, result.stdout.strip().split(',')))


@pytest.mark.parametrize('command', [
    'TRIG:RES:COUN:LOAD 10,4294967295',
    'TRIGGER:RESERVATION:COUNTER:LOAD +00010,+4294967295',
    'TRIG:RES:COUN:LOAD 4294967295,4294967295',
    'TRIG:RES:COUN:LOAD 10,4294967295 ',
    'TRIG:RES:COUN:STAR', 'TRIG:RES:COUN:STOP',
    'TRIG:RES:COUN:CLE', 'TRIG:RES:COUN:UNL',
])
def test_counter_scpi_legal_ack(parser, command):
    assert parse_counter(parser, command) == [1, 0]


def test_counter_scpi_snapshot_preserves_uint64(parser):
    values = parse_counter(parser, 'TRIG:RES:COUN?')
    assert values == [1, 0, 1, 10, 4294967295, 9007199254740993,
                      0, 3, 7, 123, 1, 17, 19, 2, 1, 23, 4294967295,
                      1, 250000000, 1, 9007199254740995, 18446744073709551615]
    assert len(values) == 22


@pytest.mark.parametrize('command', [
    'TRIG:RES:COUN:LOAD', 'TRIG:RES:COUN:LOAD 10',
    'TRIG:RES:COUN:LOAD 4294967296,1', 'TRIG:RES:COUN:LOAD 10,4294967296',
    'TRIG:RES:COUN:LOAD -1,1', 'TRIG:RES:COUN:LOAD 10,-1',
    'TRIG:RES:COUN:LOAD 1e1,1', 'TRIG:RES:COUN:LOAD 10,1.0',
    'TRIG:RES:COUN:LOAD #H0A,1', 'TRIG:RES:COUN:LOAD "10",1',
    'TRIG:RES:COUN:LOAD 10 ,1',
    'TRIG:RES:COUN:LOAD 10,1,', 'TRIG:RES:COUN:LOAD 10,1,2',
    *[f'TRIG:RES:COUN:{action} {extra}'
      for action in ('STAR', 'STOP', 'CLE', 'UNL') for extra in ('1', '1,2', '"x"')],
    'TRIG:RES:COUN? 1',
])
def test_counter_scpi_rejects_without_owner_call(parser, command):
    assert parse_counter(parser, command, 'reject') == [1, 1]


@pytest.mark.parametrize('command', ['TRIG:RES:COUN:LOAD 10,4294967295',
    'TRIG:RES:COUN:STAR', 'TRIG:RES:COUN:STOP', 'TRIG:RES:COUN:CLE',
    'TRIG:RES:COUN:UNL', 'TRIG:RES:COUN?'])
def test_counter_scpi_busy_is_only_two_fields(parser, command):
    assert parse_counter(parser, command, 'owner_reject') == [1, 2]


# Reuse the existing no-hardware stub container, including the REAL strict
# grid/deployment helpers. The only exposed command table below is COUNTER;
# no copied parser logic or permissive replacement integer function is used.
COUNTER_PARSER = DEPLOYMENT_PARSER.split('static const scpi_command_t commands[]')[0] + r'''
#include "trigger_counter_service.h"
uint32_t trigger_counter_load(uint32_t generation,uint32_t crc)
{++calls;assert((generation==10u || generation==UINT32_MAX) && crc==UINT32_MAX);return result;}
uint32_t trigger_counter_start(void) {++calls;return result;}
uint32_t trigger_counter_stop(void) {++calls;return result;}
uint32_t trigger_counter_clear(void) {++calls;return result;}
uint32_t trigger_counter_unload(void) {++calls;return result;}
bool trigger_counter_is_loaded(void) {assert(!"parser must not query an unrelated owner");return false;}
uint32_t trigger_counter_snapshot(trigger_counter_snapshot_t *out)
{
    ++calls;
    if(result)return result;
    *out=(trigger_counter_snapshot_t){.loaded=1,.binding={.table={.generation=10},
        .crc=UINT32_MAX,.local_uid=UINT64_C(9007199254740993),.local_role=0,
        .local_slot=3,.ring_config=7,.schedule_crc=123},
        .io={.schema=1,.generation=17,.clear_generation=19,.state=2,
            .input_port=1,.input_pin=23,.count32=UINT32_MAX,.valid=1,.tick_hz=250000000,
            .timestamp_valid=1,.sample_before_tick=UINT64_C(9007199254740995),
            .sample_after_tick=UINT64_MAX}};
    return 0;
}
#include "scpi_trigger_counter_commands.h"
static const scpi_command_t commands[]={
    {.pattern="TRIGger:REServation:COUNter:LOAD",.callback=scpi_cmd_trigger_counter_load},
    {.pattern="TRIGger:REServation:COUNter:STARt",.callback=scpi_cmd_trigger_counter_start},
    {.pattern="TRIGger:REServation:COUNter:STOP",.callback=scpi_cmd_trigger_counter_stop},
    {.pattern="TRIGger:REServation:COUNter:CLEar",.callback=scpi_cmd_trigger_counter_clear},
    {.pattern="TRIGger:REServation:COUNter:UNLoad",.callback=scpi_cmd_trigger_counter_unload},
    {.pattern="TRIGger:REServation:COUNter?",.callback=scpi_cmd_trigger_counter_q},
    SCPI_CMD_LIST_END};
int main(int argc,char **argv)
{
    assert(argc==3);char input[2048],line[2048];scpi_t c;scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&c,commands,&interface,scpi_units_def,"a","b","c","d",input,sizeof(input),errors,16);
    if(!strcmp(argv[2],"owner_reject"))result=COUNTER_BUSY;
    int n=snprintf(line,sizeof(line),"%s\n",argv[1]);assert(n>0 && (size_t)n<sizeof(line));
    (void)SCPI_Input(&c,line,n);
    if(!strcmp(argv[2],"reject"))assert(!calls);
    else assert(calls==1 && !SCPI_ErrorCount(&c));
    assert(response_size);printf("%s",response);return 0;
}
'''
