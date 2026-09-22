"""Production deployment owner and real SCPI parser, with no hardware IO."""
import struct
import subprocess
import zlib

import pytest

from test_trigger_grid_preview import ROOT, compile_grid


@pytest.fixture(scope='module')
def owner(tmp_path_factory):
    directory = tmp_path_factory.mktemp('deployment-owner')
    (directory / 'pico.h').write_text('unsigned get_core_num(void);\n', encoding='utf-8')
    (directory / 'board_identity.h').write_text('const char *board_identity_serial(void);\n', encoding='utf-8')
    (directory / 'tdma_runtime_owner.h').write_text(
        '#include "tdma_service.h"\n'
        'tdma_service_service_t *tdma_runtime_owner_get(void);\n'
        'bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out);\n'
        'bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out);\n',
        encoding='utf-8')
    return compile_grid(directory, 'owner', OWNER,
        [ROOT / 'components/sync_trigger/src/trigger_deployment_service.c'])


@pytest.mark.parametrize('case', [
    'success', 'stage_only', 'stage_preserves_active', 'bad_crc', 'activate_crc',
    'activate_generation', 'no_stage', 'stage_old', 'reinterpret', 'clear',
    'core', 'trigger_busy', 'running', 'pending', 'adapter', 'missing_staged',
    'bad_serial', 'null_serial', 'not_installed', 'wrong_local_slot',
    'not_local', 'wrong_generation', 'grid_error', 'grid_slot', 'grid_schedule',
    'grid_ring', 'changed_ring', 'changed_schedule', 'changed_slot',
    'during_ring', 'during_slot', 'during_schedule', 'during_uid', 'during_trigger',
    'ring_read_race', 'stage_then_new_runtime', 'unconfigured', 'invalid_table',
    'swap_0', 'swap_1', 'swap_2', 'swap_3', 'switch_configs',
])
def test_owner(owner, case):
    result = subprocess.run([str(owner), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


OWNER = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trigger_deployment_service.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"
static unsigned core, ring_reads, grid_calls;
static bool staged_ok=true;
static uint32_t trigger_state;
static const char *serial="0000000000000001", *race="";
static tdma_ring_clock_snapshot_t ring={.config_seq=3,.applied_config_seq=3};
static tdma_service_ring_runtime_config_t staged={.enabled=1,.local_slot_id=0,.schedule_crc32=123};
unsigned get_core_num(void) {return core;}
const char *board_identity_serial(void) {return serial;}
void sync_trigger_get_status(sync_trigger_status_t *out)
{memset(out,0,sizeof(*out));out->state=trigger_state;}
tdma_service_service_t *tdma_runtime_owner_get(void) {return NULL;}
bool tdma_service_update_stopped_metadata(tdma_service_service_t *service,
    bool (*update)(void *),void *context)
{
    (void)service;return !ring.enabled && !ring.adapter_started &&
        ring.config_seq==ring.applied_config_seq && update(context);
}
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{
    ++ring_reads;
    if(ring_reads==2 && !strcmp(race,"ring_read_race")) {
        ++ring.config_seq;++ring.applied_config_seq;
    }
    *out=ring;return true;
}
bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out)
{*out=staged;return staged_ok && staged.enabled;}
uint32_t trigger_grid_preview_plan(uint32_t epoch,uint32_t generation,uint64_t ordinal,
    uint64_t minimum,uint64_t maximum,trigger_grid_preview_plan_t *out)
{
    ++grid_calls;assert(epoch==7 && generation==10 && ordinal==123 && minimum==100 && maximum==2000000);
    if(!strcmp(race,"grid_error"))return TRIGGER_TIME_GRID_RESERVED;
    *out=(trigger_grid_preview_plan_t){
        .configuration={.local_slot=staged.local_slot_id,.schedule_crc=staged.schedule_crc32,
                        .ring_config=ring.config_seq},
        .plan={.ordinal=ordinal,.target_vdc_ns=2001000,.deadline_ticks=500250}};
    if(!strcmp(race,"grid_slot"))++out->configuration.local_slot;
    if(!strcmp(race,"grid_schedule"))++out->configuration.schedule_crc;
    if(!strcmp(race,"grid_ring"))++out->configuration.ring_config;
    if(!strcmp(race,"during_ring")){++ring.config_seq;++ring.applied_config_seq;}
    if(!strcmp(race,"during_slot"))++staged.local_slot_id;
    if(!strcmp(race,"during_schedule"))++staged.schedule_crc32;
    if(!strcmp(race,"during_uid"))serial="0000000000000002";
    if(!strcmp(race,"during_trigger"))trigger_state=1;
    return 0;
}
static trigger_deployment_t table(void)
{
    return (trigger_deployment_t){.generation=10,.rows={
        {.board_uid=1,.tdma_slot=0,.input_mask=1},
        {.board_uid=2,.tdma_slot=1,.output_mask=7},
        {.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},
        {.board_uid=4,.tdma_slot=3,.output_mask=1}}};
}
static trigger_deployment_snapshot_t snapshot(void)
{
    trigger_deployment_snapshot_t s;assert(!trigger_deployment_snapshot(&s));return s;
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *kind=argv[1];trigger_deployment_t t=table();
    uint32_t crc=trigger_deployment_crc(&t), role=0, generation=10;
    trigger_deployment_plan_t out,before;memset(&out,0xA5,sizeof(out));before=out;
    if(!strcmp(kind,"unconfigured")){
        assert(trigger_deployment_plan(10,0,7,10,123,100,2000000,&out)==TRIGGER_DEPLOYMENT_SERVICE_UNCONFIGURED);
        assert(!memcmp(&out,&before,sizeof(out)));return 0;
    }
    if(!strcmp(kind,"no_stage")){
        assert(trigger_deployment_activate(10,crc)==TRIGGER_DEPLOYMENT_SERVICE_NO_STAGE);return 0;
    }
    if(!strcmp(kind,"bad_crc")){
        assert(trigger_deployment_stage(&t,crc+1)==TRIGGER_DEPLOYMENT_SERVICE_DIGEST);
        assert(!snapshot().staged_valid);return 0;
    }
    if(!strcmp(kind,"invalid_table")){
        t.rows[1].output_mask=16;
        assert(trigger_deployment_stage(&t,trigger_deployment_crc(&t))==TRIGGER_DEPLOYMENT_IO);return 0;
    }
    if(!strcmp(kind,"bad_serial") || !strcmp(kind,"null_serial") ||
       !strcmp(kind,"not_installed") || !strcmp(kind,"wrong_local_slot")){
        uint32_t expected=TRIGGER_DEPLOYMENT_IDENTITY;
        if(!strcmp(kind,"bad_serial"))serial="000000000000000X";
        if(!strcmp(kind,"null_serial"))serial=NULL;
        if(!strcmp(kind,"not_installed")){serial="000000000000000F";expected=TRIGGER_DEPLOYMENT_NOT_LOCAL;}
        if(!strcmp(kind,"wrong_local_slot")){staged.local_slot_id=2;expected=TRIGGER_DEPLOYMENT_NOT_LOCAL;}
        assert(trigger_deployment_stage(&t,crc)==expected);return 0;
    }
    assert(!trigger_deployment_stage(&t,crc));assert(!trigger_deployment_stage(&t,crc));
    trigger_deployment_snapshot_t s=snapshot();
    assert(s.staged_valid && !s.active.valid && s.last_generation==10 && s.staged_crc==crc);
    if(!strcmp(kind,"stage_only"))return 0;
    if(!strcmp(kind,"activate_crc") || !strcmp(kind,"activate_generation")){
        const uint32_t expected=!strcmp(kind,"activate_crc")?TRIGGER_DEPLOYMENT_SERVICE_DIGEST:TRIGGER_DEPLOYMENT_GENERATION;
        assert(trigger_deployment_activate(!strcmp(kind,"activate_generation")?9:10,
            !strcmp(kind,"activate_crc")?crc+1:crc)==expected);
        assert(!snapshot().active.valid && snapshot().staged_valid);return 0;
    }
    if(!strcmp(kind,"stage_then_new_runtime")){++ring.config_seq;++ring.applied_config_seq;}
    assert(!trigger_deployment_activate(10,crc));s=snapshot();
    assert(s.active.valid && !s.staged_valid && s.active.crc==crc && s.active.local_role==0);
    if(!strcmp(kind,"stage_then_new_runtime")){
        assert(s.active.ring_config==4);return 0;
    }
    if(!strcmp(kind,"clear")){
        assert(!trigger_deployment_clear());s=snapshot();assert(!s.active.valid && s.last_generation==10);
        assert(trigger_deployment_stage(&t,crc)==TRIGGER_DEPLOYMENT_GENERATION);
        ++t.generation;assert(!trigger_deployment_stage(&t,trigger_deployment_crc(&t)));return 0;
    }
    if(!strcmp(kind,"stage_old") || !strcmp(kind,"reinterpret")){
        if(!strcmp(kind,"stage_old"))--t.generation;else t.rows[0].input_mask=2;
        assert(trigger_deployment_stage(&t,trigger_deployment_crc(&t))==TRIGGER_DEPLOYMENT_GENERATION);return 0;
    }
    if(!strcmp(kind,"stage_preserves_active")){
        ++t.generation;t.rows[0].input_mask=2;
        assert(!trigger_deployment_stage(&t,trigger_deployment_crc(&t)));
        trigger_deployment_snapshot_t after=snapshot();
        assert(!memcmp(&s.active,&after.active,sizeof(s.active)) && after.staged_valid);return 0;
    }
    if(!strcmp(kind,"switch_configs")){
        ++t.generation;t.rows[1].output_mask=1;t.rows[3].output_mask=7;
        assert(!trigger_deployment_stage(&t,trigger_deployment_crc(&t)));
        assert(!trigger_deployment_activate(t.generation,trigger_deployment_crc(&t)));
        assert(snapshot().active.table.rows[1].output_mask==1);
        assert(snapshot().active.table.rows[3].output_mask==7);
        assert(trigger_deployment_role_kind(1)==trigger_deployment_role_kind(3));return 0;
    }
    if(!strncmp(kind,"swap_",5)){
        role=(uint32_t)atoi(kind+5);
        /* Roles keep their per-board IO semantics; only UID and slot move. */
        t.rows[0].board_uid=role+1;t.rows[0].tdma_slot=role;
        t.rows[role].board_uid=1;t.rows[role].tdma_slot=0;
        ++t.generation;generation=t.generation;crc=trigger_deployment_crc(&t);
        assert(!trigger_deployment_stage(&t,crc));assert(!trigger_deployment_activate(generation,crc));
        assert(snapshot().active.local_role==role);
    }
    uint32_t expected=TRIGGER_DEPLOYMENT_SERVICE_CHANGED;
    if(!strcmp(kind,"success") || !strncmp(kind,"swap_",5))expected=0;
    else if(!strcmp(kind,"core")){core=1;expected=TRIGGER_DEPLOYMENT_SERVICE_BUSY;}
    else if(!strcmp(kind,"trigger_busy")){trigger_state=1;expected=TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;}
    else if(!strcmp(kind,"running")){ring.enabled=1;expected=TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;}
    else if(!strcmp(kind,"adapter")){ring.adapter_started=1;expected=TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;}
    else if(!strcmp(kind,"pending")){++ring.config_seq;expected=TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;}
    else if(!strcmp(kind,"missing_staged")){staged_ok=false;expected=TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;}
    else if(!strcmp(kind,"not_local")){role=1;expected=TRIGGER_DEPLOYMENT_NOT_LOCAL;}
    else if(!strcmp(kind,"wrong_generation")){--generation;expected=TRIGGER_DEPLOYMENT_GENERATION;}
    else if(!strcmp(kind,"changed_ring")){++ring.config_seq;++ring.applied_config_seq;}
    else if(!strcmp(kind,"changed_schedule"))++staged.schedule_crc32;
    else if(!strcmp(kind,"changed_slot")){++staged.local_slot_id;expected=TRIGGER_DEPLOYMENT_NOT_LOCAL;}
    else {
        race=kind;
        if(!strcmp(kind,"grid_error"))expected=TRIGGER_DEPLOYMENT_SERVICE_GRID_BASE+TRIGGER_TIME_GRID_RESERVED;
        if(!strcmp(kind,"ring_read_race"))expected=TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;
    }
    ring_reads=0;
    const tdma_ring_clock_snapshot_t ring_before=ring;
    const tdma_service_ring_runtime_config_t staged_before=staged;
    uint32_t result=trigger_deployment_plan(generation,role,7,10,123,100,2000000,&out);
    if(result!=expected)fprintf(stderr,"%s: got %u expected %u\n",kind,result,expected);
    assert(result==expected);
    if(result)assert(!memcmp(&out,&before,sizeof(out)));
    else {
        assert(out.role==role && out.route.board_uid==1 && out.route.tdma_slot==0);
        assert(out.clock.plan.ordinal==123 && out.clock.plan.target_vdc_ns==2001000);
        assert(!memcmp(&ring,&ring_before,sizeof(ring)) && !memcmp(&staged,&staged_before,sizeof(staged)));
    }
    return 0;
}
'''


ROWS = [(9007199254740993, 0, 1, 0, 0), (2, 1, 0, 7, 0),
        (3, 2, 1, 8, 1), (4, 3, 0, 1, 0)]
CRC = zlib.crc32(struct.pack('<I', 10) + b''.join(struct.pack('<QIIII', *r) for r in ROWS))
STAGE = 'TRIG:RES:DEP:STAG ' + ','.join(map(str, [10, CRC, *(v for r in ROWS for v in r)]))
PLAN = 'TRIG:RES:DEP:PLAN? 10,0,7,11,9007199254740993,100,2000000'


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    library = ROOT / 'third_party/scpi-parser/libscpi/src'
    return compile_grid(tmp_path_factory.mktemp('deployment-parser'), 'parser', PARSER,
                        sorted(library.glob('*.c')), parser=True)


def run_parser(parser, command, mode='accept'):
    result = subprocess.run([str(parser), command, mode], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    return list(map(int, result.stdout.strip().split(',')))


def test_parser_schema(parser):
    assert run_parser(parser, STAGE) == [1, 0]
    assert run_parser(parser, f'TRIG:RES:DEP:ACT 10,{CRC}') == [1, 0]
    assert run_parser(parser, 'TRIG:RES:DEP:CLE') == [1, 0]
    s = run_parser(parser, 'TRIG:RES:DEP?')
    assert s == [1, 0, 1, 11, 1, 321, CRC, 9007199254740993, 0, 3, 123, 0, 10, 11] + [v for r in ROWS for v in r] * 2
    assert len(s) == 54
    p = run_parser(parser, PLAN)
    assert p == [1, 0, 10, CRC, 0, *ROWS[0], 7, 11, 9007199254740993,
                 2001000, 500250, 2001001, 17, 19, -29, 250000, 250001, 25, 250000000, 0]
    assert len(p) == 24


@pytest.mark.parametrize('command', [STAGE, f'TRIG:RES:DEP:ACT 10,{CRC}',
    'TRIG:RES:DEP:CLE', 'TRIG:RES:DEP?', PLAN])
def test_owner_failure_is_two_fields(parser, command):
    assert run_parser(parser, command, 'owner_reject') == [1, 35]


@pytest.mark.parametrize('command', [
    STAGE + ',1', STAGE + ',', STAGE.rsplit(',', 1)[0],
    STAGE.replace('9007199254740993', '18446744073709551616'),
    STAGE.replace('9007199254740993', '9e15'), STAGE.replace('9007199254740993', '-1'),
    STAGE.replace('9007199254740993', '9007199254740993 '),
    STAGE.replace('9007199254740993', '9007199254740993.0'),
    'TRIG:RES:DEP:STAG', 'TRIG:RES:DEP:ACT 10', 'TRIG:RES:DEP:ACT 10,1,2',
    'TRIG:RES:DEP:ACT 4294967296,1', 'TRIG:RES:DEP:ACT 10,4294967296',
    'TRIG:RES:DEP:CLE 1', 'TRIG:RES:DEP? 1', PLAN + ',1',
    PLAN.replace('9007199254740993', '18446744073709551616'),
    PLAN.rsplit(',', 1)[0], 'TRIG:RES:DEP:PLAN?',
])
def test_parser_rejection_preserves_owner(parser, command):
    assert run_parser(parser, command, 'reject') == [1, 1]


PARSER = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "scpi/scpi.h"
#include "trigger_deployment_service.h"
static unsigned calls;
static uint32_t result;
static char response[4096];static size_t response_size;
static size_t output(scpi_t *c,const char *data,size_t n)
{(void)c;assert(response_size+n<sizeof(response));memcpy(response+response_size,data,n);response_size+=n;response[response_size]=0;return n;}
static scpi_result_t flush(scpi_t *c){(void)c;return SCPI_RES_OK;}
static int error(scpi_t *c,int_fast16_t e){(void)c;(void)e;return 0;}
static trigger_deployment_t table(void)
{return (trigger_deployment_t){.generation=10,.rows={
    {.board_uid=UINT64_C(9007199254740993),.tdma_slot=0,.input_mask=1},
    {.board_uid=2,.tdma_slot=1,.output_mask=7},
    {.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},
    {.board_uid=4,.tdma_slot=3,.output_mask=1}}};}
uint32_t trigger_deployment_stage(const trigger_deployment_t *t,uint32_t crc)
{
    ++calls;trigger_deployment_t expected=table();
    assert(crc==trigger_deployment_crc(&expected) && t->generation==10);
    for(unsigned i=0;i<4;++i){
        assert(t->rows[i].board_uid==expected.rows[i].board_uid && t->rows[i].tdma_slot==expected.rows[i].tdma_slot);
        assert(t->rows[i].input_mask==expected.rows[i].input_mask && t->rows[i].output_mask==expected.rows[i].output_mask);
        assert(t->rows[i].feedback_mode==expected.rows[i].feedback_mode);
    }
    return result;
}
uint32_t trigger_deployment_activate(uint32_t generation,uint32_t crc)
{++calls;trigger_deployment_t t=table();assert(generation==10 && crc==trigger_deployment_crc(&t));return result;}
uint32_t trigger_deployment_clear(void){++calls;return result;}
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *out)
{
    ++calls;
    if(result)return result;
    trigger_deployment_t t=table();
    *out=(trigger_deployment_snapshot_t){.active={.table=t,.valid=1,.crc=trigger_deployment_crc(&t),
        .local_uid=UINT64_C(9007199254740993),.local_role=0,.ring_config=3,.schedule_crc=123,.local_slot=0},
        .staged=t,.staged_valid=1,.staged_crc=321,.last_generation=11};
    out->staged.generation=11;return 0;
}
uint32_t trigger_deployment_plan(uint32_t generation,uint32_t role,uint32_t epoch,uint32_t gridgen,
    uint64_t ordinal,uint64_t minimum,uint64_t maximum,trigger_deployment_plan_t *out)
{
    ++calls;assert(generation==10 && !role && epoch==7 && gridgen==11 &&
        ordinal==UINT64_C(9007199254740993) && minimum==100 && maximum==2000000);
    if(result)return result;
    trigger_deployment_t t=table();
    *out=(trigger_deployment_plan_t){.deployment={.table=t,.crc=trigger_deployment_crc(&t)},
        .route=t.rows[0],.role=role,.clock={
            .configuration={.grid={.epoch=epoch,.generation=gridgen}},
            .plan={.ordinal=ordinal,.target_vdc_ns=2001000,.deadline_ticks=500250,.projected_vdc_ns=2001001},
            .model={.token=17,.dco={.dco_update_seq=19,.period_adjust_ppb=-29}},
            .now_ticks=250000,.checked_at_ticks=250001,.minimum_lead_ticks=25,.tick_hz=250000000}};
    return 0;
}
/* Compile the real grid helper container; deployment must not call its owner. */
uint32_t trigger_grid_preview_configure(const trigger_time_grid_t *grid)
{(void)grid;assert(0);return 1;}
uint32_t trigger_grid_preview_clear(void){assert(0);return 1;}
uint32_t trigger_grid_preview_snapshot(trigger_grid_preview_snapshot_t *out)
{(void)out;assert(0);return 1;}
uint32_t trigger_grid_preview_plan(uint32_t epoch,uint32_t generation,uint64_t ordinal,
    uint64_t minimum,uint64_t maximum,trigger_grid_preview_plan_t *out)
{(void)epoch;(void)generation;(void)ordinal;(void)minimum;(void)maximum;(void)out;assert(0);return 1;}
#include "scpi_trigger_grid_commands.inc"
#include "scpi_trigger_deployment_commands.inc"
static const scpi_command_t commands[]={
    {.pattern="TRIGger:REServation:DEPloyment:STAGe",.callback=scpi_cmd_trigger_deployment_stage},
    {.pattern="TRIGger:REServation:DEPloyment:ACTivate",.callback=scpi_cmd_trigger_deployment_activate},
    {.pattern="TRIGger:REServation:DEPloyment:CLEar",.callback=scpi_cmd_trigger_deployment_clear},
    {.pattern="TRIGger:REServation:DEPloyment?",.callback=scpi_cmd_trigger_deployment_q},
    {.pattern="TRIGger:REServation:DEPloyment:PLAN?",.callback=scpi_cmd_trigger_deployment_plan_q},SCPI_CMD_LIST_END};
int main(int argc,char **argv)
{
    assert(argc==3);char input[2048],line[2048];scpi_t c;scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&c,commands,&interface,scpi_units_def,"a","b","c","d",input,sizeof(input),errors,16);
    if(!strcmp(argv[2],"owner_reject"))result=35;
    int n=snprintf(line,sizeof(line),"%s\n",argv[1]);assert(n>0 && (size_t)n<sizeof(line));
    (void)SCPI_Input(&c,line,n);
    if(!strcmp(argv[2],"reject"))assert(!calls);else assert(calls==1 && !SCPI_ErrorCount(&c));
    assert(response_size);printf("%s",response);return 0;
}
'''
