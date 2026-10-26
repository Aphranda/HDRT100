"""Production gateway load owner and real SCPI parser with no hardware access."""
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid
from test_trigger_deployment_service import PARSER as DEPLOYMENT_PARSER


@pytest.fixture(scope='module')
def owner(tmp_path_factory):
    directory = tmp_path_factory.mktemp('trigger-gateway-owner')
    (directory/'pico.h').write_text('unsigned get_core_num(void);\n', encoding='utf-8')
    (directory/'board_identity.h').write_text('const char *board_identity_serial(void);\n', encoding='utf-8')
    (directory/'tdma_runtime_owner.h').write_text(
        '#include "tdma_service.h"\n'
        'tdma_service_service_t *tdma_runtime_owner_get(void);\n'
        'bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out);\n'
        'bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out);\n',
        encoding='utf-8')
    return compile_grid(directory, 'owner', OWNER,
                        [ROOT/'components/sync_trigger/src/trigger_gateway_service.c'])


@pytest.mark.parametrize('case', [
    'lifecycle', 'unconfigured', 'null_snapshot', 'zero_generation', 'zero_pulse',
    'core', 'maintenance_busy', 'running_ring', 'adapter_started', 'pending_ring',
    'missing_deployment', 'invalid_active', 'trigger_busy', 'missing_ring',
    'missing_staged', 'disabled_staged', 'wrong_uid', 'null_uid', 'short_uid',
    'malformed_uid', 'long_uid', 'wrong_generation', 'wrong_crc', 'corrupt_table',
    'wrong_role_0', 'wrong_role_1', 'wrong_role_3', 'wrong_staged_slot',
    'wrong_schedule', 'wrong_ring_config', 'retained_slot_conflict', 'retained_crc_conflict',
    'wrong_row_uid', 'wrong_row_slot', 'invalid_input_mask', 'invalid_output_mask',
    'counter_lease', 'duplicate_load', 'load_rollback', 'stop_failure',
    'release_failure', 'snapshot_busy', 'ready_high_is_not_receipt',
    'gateway_on_other_board', 'cleanup_after_binding_change', 'reentrant_load',
    *[f'ports_{i}_{o}' for i in range(1, 5) for o in range(1, 5)],
])
def test_actual_gateway_owner(owner, case):
    result = subprocess.run([str(owner), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


OWNER = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trigger_gateway_service.h"
#include "trigger_counter_service.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"
/* Pilot model default: off. The gateway service is exercised on the shipped
 * default path; enabling a pilot is a separate module's responsibility. */
static bool pilot_position_enabled;
bool trigger_sequence_pilot_position_enabled(void) { return pilot_position_enabled; }

static unsigned core, maintenance_depth, mutations, load_calls, stop_calls, release_calls;
static uint32_t generation;
static bool maintenance_ok=true, ring_ok=true, staged_ok=true, deployment_ok=true;
static bool load_ok=true,stop_ok=true,release_ok=true,snapshot_ok=true,counter_loaded,reenter;
static uint32_t trigger_state;
static const char *serial="0000000000000003";
static tdma_ring_clock_snapshot_t ring={.config_seq=7,.applied_config_seq=7};
static tdma_service_ring_runtime_config_t staged={.enabled=1,.local_slot_id=2,.schedule_crc32=123};
static trigger_deployment_snapshot_t deployment;
static sync_io_gateway_snapshot_t hw;
unsigned get_core_num(void) {return core;}
const char *board_identity_serial(void) {return serial;}
bool trigger_counter_is_loaded(void) {return counter_loaded;}
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
    if(!maintenance_ok || ring.enabled || ring.adapter_started || ring.config_seq!=ring.applied_config_seq)return false;
    assert(maintenance_depth==0);++maintenance_depth;
    const bool result=callback(context);--maintenance_depth;return result;
}
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *out)
{
    if(!deployment_ok)return TRIGGER_DEPLOYMENT_SERVICE_BUSY;
    if(trigger_state!=TRIG_STATE_IDLE)return TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;
    *out=deployment;return 0;
}
static void mutation(void) {assert(core==0 && maintenance_depth==1);++mutations;}
bool sync_io_gateway_load(uint32_t input,uint32_t output,uint32_t pulse,uint32_t *lease)
{
    mutation();++load_calls;assert(input>=1 && input<=4 && output>=1 && output<=4 && pulse==200 && lease);
    if(reenter) {
        assert(trigger_gateway_load(10,deployment.active.crc,200)==GATEWAY_BUSY);
        assert(trigger_gateway_stop()==GATEWAY_BUSY);
        assert(trigger_gateway_unload()==GATEWAY_BUSY);
    }
    if(!load_ok)return false;
    assert(!hw.loaded);
    hw=(sync_io_gateway_snapshot_t){.schema=1,.generation=++generation,
        .state=SYNC_IO_GATEWAY_LOADED,.loaded=1,.input_port=input,.input_pin=24u-input,
        .output_port=output,.output_pin=15u+output,.pulse_ns=pulse,.pulse_ticks=50,
        .tick_hz=250000000,.sm_claimed_mask=10,.instruction_words=13};
    *lease=hw.generation;return true;
}
bool sync_io_gateway_stop(uint32_t lease)
{
    mutation();++stop_calls;assert(lease==hw.generation && hw.loaded);
    if(!stop_ok)return false;
    hw.state=SYNC_IO_GATEWAY_STOPPED;return true;
}
bool sync_io_gateway_release(uint32_t lease)
{
    mutation();++release_calls;assert(lease==hw.generation && hw.loaded);
    if(!release_ok)return false;
    hw.state=SYNC_IO_GATEWAY_IDLE;hw.loaded=0;hw.sm_claimed_mask=0;return true;
}
bool sync_io_gateway_snapshot(sync_io_gateway_snapshot_t *out)
{if(!snapshot_ok)return false;*out=hw;return true;}
static void setup(void)
{
    deployment.active=(trigger_deployment_binding_t){.valid=1,.local_uid=3,
        .local_role=2,.local_slot=2,.ring_config=7,.schedule_crc=123,
        .table={.generation=10,.rows={
            {.board_uid=1,.tdma_slot=0,.input_mask=1},
            {.board_uid=2,.tdma_slot=1,.output_mask=7},
            {.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},
            {.board_uid=4,.tdma_slot=3,.output_mask=1}}}};
    deployment.active.crc=trigger_deployment_crc(&deployment.active.table);
}
static trigger_gateway_snapshot_t snapshot(void)
{trigger_gateway_snapshot_t s;assert(trigger_gateway_snapshot(&s)==GATEWAY_OK);return s;}
static void load(void)
{assert(trigger_gateway_load(10,deployment.active.crc,200)==GATEWAY_OK);assert(trigger_gateway_is_loaded());}
int main(int argc,char **argv)
{
    assert(argc==2);const char *name=argv[1];setup();
    if(!strcmp(name,"null_snapshot")) {assert(trigger_gateway_snapshot(NULL)==GATEWAY_ARGUMENT);return 0;}
    if(!strcmp(name,"unconfigured")) {
        assert(!trigger_gateway_is_loaded());assert(trigger_gateway_stop()==GATEWAY_NOT_LOADED);
        assert(trigger_gateway_unload()==GATEWAY_NOT_LOADED);assert(!mutations);return 0;
    }
    if(!strcmp(name,"gateway_on_other_board")) {
        /* Role routing, not physical board number: gateway travels to UID4/slot3. */
        deployment.active.table.rows[2].board_uid=4;deployment.active.table.rows[2].tdma_slot=3;
        deployment.active.table.rows[3].board_uid=3;deployment.active.table.rows[3].tdma_slot=2;
        deployment.active.local_uid=4;deployment.active.local_slot=3;staged.local_slot_id=3;
        deployment.active.crc=trigger_deployment_crc(&deployment.active.table);serial="0000000000000004";
        load();assert(snapshot().binding.local_uid==4 && snapshot().binding.local_role==2);
        assert(trigger_gateway_stop()==GATEWAY_OK);assert(trigger_gateway_unload()==GATEWAY_OK);return 0;
    }
    if(!strncmp(name,"ports_",6)) {
        unsigned input=0,output=0;assert(sscanf(name,"ports_%u_%u",&input,&output)==2);
        deployment.active.table.rows[2].input_mask=1u<<(input-1u);
        deployment.active.table.rows[2].output_mask=1u<<(output-1u);
        deployment.active.crc=trigger_deployment_crc(&deployment.active.table);
        load();trigger_gateway_snapshot_t s=snapshot();
        assert(s.io.input_port==input && s.io.output_port==output);
        assert(s.io.input_pin==24u-input && s.io.output_pin==15u+output);
        assert(s.binding.table.rows[2].input_mask==(1u<<(input-1u)));
        assert(s.binding.table.rows[2].output_mask==(1u<<(output-1u)));
        assert(trigger_gateway_stop()==GATEWAY_OK && trigger_gateway_unload()==GATEWAY_OK);return 0;
    }
    uint32_t depgen=10,crc=deployment.active.crc,pulse=200,expected=GATEWAY_CHANGED;
    bool reject=true;
    if(!strcmp(name,"zero_generation")){depgen=0;expected=GATEWAY_ARGUMENT;}
    else if(!strcmp(name,"zero_pulse")){pulse=0;expected=GATEWAY_ARGUMENT;}
    else if(!strcmp(name,"core")){core=1;expected=GATEWAY_BUSY;}
    else if(!strcmp(name,"maintenance_busy")){maintenance_ok=false;expected=GATEWAY_NOT_IDLE;}
    else if(!strcmp(name,"running_ring")){ring.enabled=1;expected=GATEWAY_NOT_IDLE;}
    else if(!strcmp(name,"adapter_started")){ring.adapter_started=1;expected=GATEWAY_NOT_IDLE;}
    else if(!strcmp(name,"pending_ring")){++ring.config_seq;expected=GATEWAY_NOT_IDLE;}
    else if(!strcmp(name,"missing_deployment")){deployment_ok=false;expected=GATEWAY_DEPLOYMENT;}
    else if(!strcmp(name,"invalid_active")){deployment.active.valid=0;expected=GATEWAY_DEPLOYMENT;}
    else if(!strcmp(name,"trigger_busy")){trigger_state=1;expected=GATEWAY_DEPLOYMENT;}
    else if(!strcmp(name,"missing_ring"))ring_ok=false;
    else if(!strcmp(name,"missing_staged"))staged_ok=false;
    else if(!strcmp(name,"disabled_staged"))staged.enabled=0;
    else if(!strcmp(name,"wrong_uid"))serial="000000000000000F";
    else if(!strcmp(name,"null_uid"))serial=NULL;
    else if(!strcmp(name,"short_uid"))serial="3";
    else if(!strcmp(name,"malformed_uid"))serial="000000000000000Z";
    else if(!strcmp(name,"long_uid"))serial="00000000000000030";
    else if(!strcmp(name,"wrong_generation"))++depgen;
    else if(!strcmp(name,"wrong_crc"))++crc;
    else if(!strcmp(name,"corrupt_table"))++deployment.active.table.rows[3].output_mask;
    else if(!strncmp(name,"wrong_role_",11)){deployment.active.local_role=(uint32_t)atoi(name+11);expected=GATEWAY_WRONG_ROLE;}
    else if(!strcmp(name,"wrong_staged_slot"))++staged.local_slot_id;
    else if(!strcmp(name,"wrong_schedule"))++staged.schedule_crc32;
    else if(!strcmp(name,"wrong_ring_config"))++deployment.active.ring_config;
    else if(!strcmp(name,"retained_slot_conflict")){ring.schedule_crc32=123;ring.local_slot_id=1;}
    else if(!strcmp(name,"retained_crc_conflict")){ring.schedule_crc32=124;ring.local_slot_id=2;}
    else if(!strcmp(name,"counter_lease")){counter_loaded=true;expected=GATEWAY_BUSY;}
    else if(!strcmp(name,"wrong_row_uid") || !strcmp(name,"wrong_row_slot") ||
            !strcmp(name,"invalid_input_mask") || !strcmp(name,"invalid_output_mask")) {
        if(!strcmp(name,"wrong_row_uid"))deployment.active.table.rows[2].board_uid=15;
        if(!strcmp(name,"wrong_row_slot"))deployment.active.table.rows[2].tdma_slot=4;
        if(!strcmp(name,"invalid_input_mask"))deployment.active.table.rows[2].input_mask=3;
        if(!strcmp(name,"invalid_output_mask"))deployment.active.table.rows[2].output_mask=16;
        crc=deployment.active.crc=trigger_deployment_crc(&deployment.active.table);
    }
    else reject=false;
    if(reject) {
        const uint32_t r=trigger_gateway_load(depgen,crc,pulse);
        if(r!=expected)fprintf(stderr,"%s: got %u expected %u\n",name,r,expected);
        assert(r==expected && !load_calls && !mutations && !hw.loaded);
        assert(!trigger_gateway_is_loaded());return 0;
    }
    if(!strcmp(name,"load_rollback")) {
        load_ok=false;assert(trigger_gateway_load(10,crc,200)==GATEWAY_HARDWARE);
        assert(!trigger_gateway_is_loaded() && !hw.loaded);
        assert(!snapshot().binding.valid);load_ok=true;load();return 0;
    }
    if(!strcmp(name,"reentrant_load"))reenter=true;
    load();const trigger_gateway_snapshot_t loaded=snapshot();
    assert(loaded.loaded && loaded.binding.valid && loaded.binding.local_uid==3 && loaded.binding.local_role==2);
    assert(!loaded.io.sm_enabled_mask && !loaded.io.trigger_count && !loaded.io.ready_count && !loaded.io.grant_pending);
    if(!strcmp(name,"reentrant_load"))return 0;
    if(!strcmp(name,"duplicate_load")) {
        assert(trigger_gateway_load(10,crc,200)==GATEWAY_BUSY && load_calls==1);
        assert(snapshot().io.generation==loaded.io.generation);return 0;
    }
    if(!strcmp(name,"snapshot_busy")) {
        trigger_gateway_snapshot_t out;memset(&out,0xA5,sizeof(out));trigger_gateway_snapshot_t before=out;
        snapshot_ok=false;assert(trigger_gateway_snapshot(&out)==GATEWAY_BUSY);
        assert(!memcmp(&out,&before,sizeof(out)));return 0;
    }
    if(!strcmp(name,"ready_high_is_not_receipt")) {
        hw.ready_level=1;trigger_gateway_snapshot_t s=snapshot();
        assert(s.io.ready_level && !s.io.ready_count && !s.io.trigger_count && !s.io.sm_enabled_mask);return 0;
    }
    if(!strcmp(name,"stop_failure")) {
        stop_ok=false;assert(trigger_gateway_stop()==GATEWAY_HARDWARE && trigger_gateway_is_loaded());
        assert(snapshot().io.state==SYNC_IO_GATEWAY_LOADED);return 0;
    }
    if(!strcmp(name,"cleanup_after_binding_change")) {
        ++deployment.active.table.generation;serial="000000000000000F";
        assert(trigger_gateway_stop()==GATEWAY_OK && trigger_gateway_unload()==GATEWAY_OK);
        assert(!trigger_gateway_is_loaded() && snapshot().binding.local_uid==3);return 0;
    }
    assert(trigger_gateway_stop()==GATEWAY_OK);
    assert(snapshot().io.state==SYNC_IO_GATEWAY_STOPPED && trigger_gateway_is_loaded());
    if(!strcmp(name,"release_failure")) {
        release_ok=false;assert(trigger_gateway_unload()==GATEWAY_HARDWARE && trigger_gateway_is_loaded());return 0;
    }
    assert(!strcmp(name,"lifecycle"));
    assert(trigger_gateway_unload()==GATEWAY_OK && !trigger_gateway_is_loaded());
    trigger_gateway_snapshot_t retired=snapshot();
    assert(!retired.loaded && !retired.io.loaded && retired.io.state==SYNC_IO_GATEWAY_IDLE);
    assert(!memcmp(&retired.binding,&loaded.binding,sizeof(retired.binding)));
    load();assert(snapshot().io.generation>loaded.io.generation);
    assert(load_calls==2 && stop_calls==1 && release_calls==1);
    return 0;
}
'''


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    library = ROOT/'third_party/scpi-parser/libscpi/src'
    units = [ROOT / 'middleware/scpi_port/src/scpi_trigger_grid_commands.c',
             ROOT / 'middleware/scpi_port/src/scpi_trigger_deployment_commands.c',
             ROOT / 'middleware/scpi_port/src/scpi_trigger_gateway_commands.c']
    return compile_grid(tmp_path_factory.mktemp('gateway-parser'), 'parser', PARSER,
                        [*units, *sorted(library.glob('*.c'))], parser=True)


def parse_gateway(parser, command, mode='accept'):
    result = subprocess.run([str(parser), command, mode], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    return list(map(int, result.stdout.strip().split(',')))


@pytest.mark.parametrize('command', [
    'TRIG:RES:GATE:LOAD 10,4294967295,200',
    'TRIGGER:RESERVATION:GATEWAY:LOAD +00010,+4294967295,+200',
    'TRIG:RES:GATE:LOAD 4294967295,4294967295,4294967295',
    'TRIG:RES:GATE:STOP', 'TRIG:RES:GATE:UNL',
])
def test_gateway_scpi_ack(parser, command):
    assert parse_gateway(parser, command) == [1, 0]


def test_gateway_scpi_complete_snapshot_and_uint64_uid(parser):
    values = parse_gateway(parser, 'TRIG:RES:GATE?')
    # Page layout after the sequence instrumentation: 37 fields (the tail reports late/arm refusals).
    assert values == [1, 0, 1, 10, 4294967295, 9007199254740993, 2, 3, 7, 123,
                      1, 17, 1, 1, 1, 23, 4, 19, 200, 50, 250000000, 1,
                      10, 0, 13, 0, 7, 0, 0, 0, 0, 2, 3, 4, 1, 0, 0]
    assert len(values) == 37


@pytest.mark.parametrize('command', [
    'TRIG:RES:GATE:LOAD', 'TRIG:RES:GATE:LOAD 10,1',
    'TRIG:RES:GATE:LOAD 4294967296,1,200',
    'TRIG:RES:GATE:LOAD 10,4294967296,200',
    'TRIG:RES:GATE:LOAD 10,1,4294967296',
    'TRIG:RES:GATE:LOAD -1,1,200', 'TRIG:RES:GATE:LOAD 10,-1,200',
    'TRIG:RES:GATE:LOAD 10,1,-200', 'TRIG:RES:GATE:LOAD 10,1,2e2',
    'TRIG:RES:GATE:LOAD 10,1,200.0', 'TRIG:RES:GATE:LOAD 10,1,#Hc8',
    'TRIG:RES:GATE:LOAD "10",1,200', 'TRIG:RES:GATE:LOAD 10 ,1,200',
    'TRIG:RES:GATE:LOAD 10,1,200,', 'TRIG:RES:GATE:LOAD 10,1,200,1',
    'TRIG:RES:GATE:STOP 1', 'TRIG:RES:GATE:STOP "x"',
    'TRIG:RES:GATE:UNL 1', 'TRIG:RES:GATE:UNL 1,2', 'TRIG:RES:GATE? 1',
])
def test_gateway_scpi_rejects_before_owner(parser, command):
    assert parse_gateway(parser, command, 'reject') == [1, 1]


@pytest.mark.parametrize('command', ['TRIG:RES:GATE:LOAD 10,4294967295,200',
    'TRIG:RES:GATE:STOP', 'TRIG:RES:GATE:UNL', 'TRIG:RES:GATE?'])
def test_gateway_scpi_busy_has_two_fields(parser, command):
    assert parse_gateway(parser, command, 'owner_reject') == [1, 2]


@pytest.mark.parametrize('command', ['TRIG:RES:GATE:FIRE', 'TRIG:RES:GATE:ARM',
                                    'TRIG:RES:GATE:START'])
def test_gateway_has_no_immediate_or_arm_command(parser, command):
    result = subprocess.run([str(parser), command, 'unknown'], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == 'UNDEFINED'


PARSER = DEPLOYMENT_PARSER.split('static const scpi_command_t commands[]')[0] + r'''
#include "trigger_gateway_service.h"
uint32_t trigger_gateway_load(uint32_t generation,uint32_t crc,uint32_t pulse)
{
    ++calls;assert((generation==10u || generation==UINT32_MAX) && crc==UINT32_MAX &&
        (pulse==200u || pulse==UINT32_MAX));return result;
}
uint32_t trigger_gateway_stop(void) {++calls;return result;}
uint32_t trigger_gateway_unload(void) {++calls;return result;}
bool trigger_gateway_is_loaded(void) {assert(!"SCPI must call only the selected owner API");return false;}
uint32_t trigger_gateway_snapshot(trigger_gateway_snapshot_t *out)
{
    ++calls;if(result)return result;
    *out=(trigger_gateway_snapshot_t){.loaded=1,.binding={.table={.generation=10},
        .crc=UINT32_MAX,.local_uid=UINT64_C(9007199254740993),.local_role=2,
        .local_slot=3,.ring_config=7,.schedule_crc=123},
        .io={.schema=1,.generation=17,.state=1,.loaded=1,.input_port=1,.input_pin=23,
            .output_port=4,.output_pin=19,.pulse_ns=200,.pulse_ticks=50,.tick_hz=250000000,
            .ready_level=1,.sm_claimed_mask=10,.sm_enabled_mask=0,.instruction_words=13,
            .output_program_offset=0,.ready_program_offset=7,.grant_pending=0,
            .output_level=0,.trigger_count=0,.ready_count=0,
            .output_tx_level=2,.output_rx_level=3,.ready_tx_level=4,.ready_rx_level=1}};
    return 0;
}
#include "scpi_trigger_gateway_commands.h"
static const scpi_command_t commands[]={
    {.pattern="TRIGger:REServation:GATEway:LOAD",.callback=scpi_cmd_trigger_gateway_load},
    {.pattern="TRIGger:REServation:GATEway:STOP",.callback=scpi_cmd_trigger_gateway_stop},
    {.pattern="TRIGger:REServation:GATEway:UNLoad",.callback=scpi_cmd_trigger_gateway_unload},
    {.pattern="TRIGger:REServation:GATEway?",.callback=scpi_cmd_trigger_gateway_q},
    SCPI_CMD_LIST_END};
int main(int argc,char **argv)
{
    assert(argc==3);char input[2048],line[2048];scpi_t c;scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&c,commands,&interface,scpi_units_def,"a","b","c","d",input,sizeof(input),errors,16);
    if(!strcmp(argv[2],"owner_reject"))result=GATEWAY_BUSY;
    int n=snprintf(line,sizeof(line),"%s\n",argv[1]);assert(n>0 && (size_t)n<sizeof(line));
    (void)SCPI_Input(&c,line,n);
    if(!strcmp(argv[2],"unknown")) {
        assert(!calls && SCPI_ErrorCount(&c));printf("UNDEFINED\n");return 0;
    }
    if(!strcmp(argv[2],"reject"))assert(!calls);
    else assert(calls==1 && !SCPI_ErrorCount(&c));
    assert(response_size);printf("%s",response);return 0;
}
'''
