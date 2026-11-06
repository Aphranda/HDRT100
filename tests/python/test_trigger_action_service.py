"""Execute the real SRAM-only installation owner, without any IO backend."""
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid
from test_trigger_deployment_service import PARSER as DEPLOYMENT_PARSER


@pytest.fixture(scope='module')
def owner(tmp_path_factory):
    d = tmp_path_factory.mktemp('action-owner')
    (d/'pico.h').write_text('unsigned get_core_num(void);\n', encoding='utf-8')
    (d/'board_config.h').write_text('#define BOARD_SYS_CLOCK_HZ 250000000u\n', encoding='utf-8')
    (d/'board_identity.h').write_text('const char *board_identity_serial(void);\n', encoding='utf-8')
    (d/'tdma_runtime_owner.h').write_text(
        '#include "tdma_service.h"\n'
        'tdma_service_service_t *tdma_runtime_owner_get(void);\n'
        'bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out);\n'
        'bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out);\n', encoding='utf-8')
    return compile_grid(d, 'owner', HARNESS,
                        [ROOT/'components/sync_trigger/src/trigger_action_service.c',
                         ROOT/'tests/unit/host_stubs/tdma_seat_mount_store_stub.c'])


@pytest.mark.parametrize('case', [
    'lifecycle', 'empty', 'null_arguments', 'core', 'running', 'adapter', 'pending',
    'metadata_busy', 'missing_dep', 'inactive_dep', 'uid', 'slot', 'schedule',
    'ring_config', 'corrupt_dep', 'missing_staged', 'missing_ring', 'disabled_staged',
    'bad_digest', 'bad_code', 'bad_dep_generation', 'bad_dep_crc', 'reserved',
    'overflow', 'duplicate_id', 'overlap', 'order', 'count', 'pulse_tick',
    'reinterpret', 'old_generation', 'clear_highwater', 'row_range', 'run_snapshot',
    'run_row', 'run_clear', 'reentrant', 'unused_rows_ignored',
    *[f'local_{slot}' for slot in range(4)],
    *[f'changed_{key}' for key in ('generation', 'crc', 'uid', 'slot', 'role', 'schedule', 'ring', 'remote_row')],
])
def test_actual_installation_owner(owner, case):
    r = subprocess.run([str(owner), case], capture_output=True, text=True, timeout=5)
    assert r.returncode == 0, r.stdout+r.stderr


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trigger_action_service.h"
#include "tdma_runtime_owner.h"
#include "tdma_seat_mount_store.h"
/* Execution slice is tested separately; these stubs keep this harness focused
 * on SRAM/deployment and prove install has no IO call. */
uint32_t vdc_dpll_manager_feedback_session(void){return 0u;}
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out){(void)out;return false;}
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out){(void)hz;(void)out;return false;}
bool vdc_domain_dco_local_to_output_ns(const vdc_dco_control_t *d,uint64_t n,uint64_t *o){(void)d;(void)n;(void)o;return false;}
bool vdc_domain_dco_output_to_local_ns(const vdc_dco_control_t *d,uint64_t n,uint64_t *o){(void)d;(void)n;(void)o;return false;}
bool sync_io_action_load(const sync_io_action_config_t *c,uint32_t *g){(void)c;(void)g;assert(!"install must not load action IO");return false;}
bool sync_io_action_arm_checked(uint32_t g,uint64_t d,bool(*f)(void*),void*c){(void)g;(void)d;(void)f;(void)c;assert(!"install must not arm action IO");return false;}
bool sync_io_action_stop(uint32_t g){(void)g;assert(!"install must not stop action IO");return false;}
bool sync_io_action_release(uint32_t g){(void)g;assert(!"install must not release action IO");return false;}
bool sync_io_action_snapshot(sync_io_action_snapshot_t *o){(void)o;assert(!"install must not read action IO");return false;}
static unsigned core,depth,metadata_calls;
static bool metadata_ok=true,dep_ok=true,ring_ok=true,staged_ok=true,reentrant;
static const char *serial="0000000000000001";
static tdma_ring_clock_snapshot_t ring={.config_seq=7,.applied_config_seq=7};
static tdma_service_ring_runtime_config_t staged={.enabled=1,.local_slot_id=0,.schedule_crc32=123};
static trigger_deployment_snapshot_t dep;
unsigned get_core_num(void){return core;}
const char *board_identity_serial(void){return serial;}
tdma_service_service_t *tdma_runtime_owner_get(void){return NULL;}
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{if(!ring_ok)return false;*out=ring;return true;}
bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out)
{if(!staged_ok)return false;*out=staged;return true;}
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *out)
{assert(depth==1);if(!dep_ok)return TRIGGER_DEPLOYMENT_SERVICE_BUSY;*out=dep;return 0;}
bool tdma_service_update_stopped_metadata(tdma_service_service_t *service,bool(*fn)(void*),void *ctx)
{
    (void)service;++metadata_calls;
    if(!metadata_ok || ring.enabled || ring.adapter_started || ring.config_seq!=ring.applied_config_seq)return false;
    assert(!depth);++depth;
    if(reentrant){trigger_action_snapshot_t out;assert(trigger_action_snapshot(&out)==TRIGGER_ACTION_SERVICE_BUSY);assert(trigger_action_clear()==TRIGGER_ACTION_SERVICE_BUSY);}
    bool ok=fn(ctx);--depth;return ok;
}
bool tdma_service_run_stopped_maintenance(tdma_service_service_t *service,bool(*fn)(void*),void *ctx)
{(void)service;(void)fn;(void)ctx;assert(!"installation owner must use metadata only");return false;}
/* No SYNC_IO, model, APPLY/FIRE, counter, gateway, switch or live-clock stub
 * is linked. Any new implementation dependency on one fails at link time. */
static void setup(void)
{
    dep.active=(trigger_deployment_binding_t){.valid=1,.local_uid=1,.local_role=0,.local_slot=0,
        .ring_config=7,.schedule_crc=123,.table={.generation=10,.rows={
        {.board_uid=1,.tdma_slot=0,.input_mask=1},
        {.board_uid=2,.tdma_slot=1,.output_mask=7},
        {.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},
        {.board_uid=4,.tdma_slot=3,.output_mask=1}}}};
    dep.active.crc=trigger_deployment_crc(&dep.active.table);
}
static trigger_action_table_t table(void)
{
    return (trigger_action_table_t){.generation=21,.dep_generation=10,.dep_crc=dep.active.crc,
        .grid={.epoch=5,.generation=6,.anchor_vdc_ns=UINT64_C(9007199254740993),.period_ns=1000000,.subdivisions=2000},
        .count=3,.rows={
            {.action_id=1,.role=1,.kind=TRIGGER_ACTION_SWITCH_CODE,.argument=3,.ordinal=2010,.span_slices=2},
            {.action_id=2,.role=3,.kind=TRIGGER_ACTION_SWITCH_CODE,.argument=1,.ordinal=2012,.span_slices=2},
            {.action_id=3,.role=2,.kind=TRIGGER_ACTION_GATEWAY_PULSE,.argument=1000,.ordinal=2014,.span_slices=2}}};
}
static trigger_action_snapshot_t snapshot(void)
{trigger_action_snapshot_t out;assert(!trigger_action_snapshot(&out));return out;}
static void install(trigger_action_table_t *t)
{assert(!trigger_action_install(t,trigger_action_table_crc(t)));}
static void unqualified(const trigger_action_snapshot_t *s)
{
    assert(!s->execution_authorized && !s->armed && !s->deadline_valid && !s->anchor_authenticated);
    assert(s->state==(s->installed?TRIGGER_ACTION_SERVICE_INSTALLED_CONFIG:TRIGGER_ACTION_SERVICE_EMPTY));
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *n=argv[1];setup();trigger_action_table_t t=table();
    if(!strcmp(n,"null_arguments")) {
        trigger_action_row_t row;uint64_t target;
        assert(trigger_action_install(NULL,0)==TRIGGER_ACTION_TABLE_ARGUMENT);
        assert(trigger_action_snapshot(NULL)==TRIGGER_ACTION_TABLE_ARGUMENT);
        assert(trigger_action_row(0,NULL,&target)==TRIGGER_ACTION_TABLE_ARGUMENT);
        assert(trigger_action_row(0,&row,NULL)==TRIGGER_ACTION_TABLE_ARGUMENT);assert(!metadata_calls);return 0;
    }
    if(!strcmp(n,"empty")) {
        trigger_action_snapshot_t s=snapshot();assert(!s.installed && !s.binding_current);unqualified(&s);
        trigger_action_row_t row={.action_id=99};uint64_t target=77;
        assert(trigger_action_row(0,&row,&target)==TRIGGER_ACTION_SERVICE_UNCONFIGURED && row.action_id==99 && target==77);return 0;
    }
    if(!strncmp(n,"local_",6)) {
        unsigned slot=(unsigned)atoi(n+6);static const char *ids[]={"0000000000000001","0000000000000002","0000000000000003","0000000000000004"};
        serial=ids[slot];dep.active.local_uid=slot+1;dep.active.local_role=slot;dep.active.local_slot=slot;staged.local_slot_id=slot;
        install(&t);trigger_action_snapshot_t s=snapshot();unsigned masks[]={0,1,4,2};
        assert(s.local_row_mask==masks[slot] && s.crc==trigger_action_table_crc(&t));unqualified(&s);return 0;
    }
    unsigned expected=TRIGGER_ACTION_SERVICE_CHANGED;bool reject=true;
    if(!strcmp(n,"core")){core=1;expected=TRIGGER_ACTION_SERVICE_BUSY;}
    else if(!strcmp(n,"running")){ring.enabled=1;expected=TRIGGER_ACTION_SERVICE_NOT_IDLE;}
    else if(!strcmp(n,"adapter")){ring.adapter_started=1;expected=TRIGGER_ACTION_SERVICE_NOT_IDLE;}
    else if(!strcmp(n,"pending")){++ring.config_seq;expected=TRIGGER_ACTION_SERVICE_NOT_IDLE;}
    else if(!strcmp(n,"metadata_busy")){metadata_ok=false;expected=TRIGGER_ACTION_SERVICE_NOT_IDLE;}
    else if(!strcmp(n,"missing_dep"))dep_ok=false;
    else if(!strcmp(n,"inactive_dep"))dep.active.valid=0;
    else if(!strcmp(n,"uid"))serial="000000000000000F";
    else if(!strcmp(n,"slot"))++staged.local_slot_id;
    else if(!strcmp(n,"schedule"))++staged.schedule_crc32;
    else if(!strcmp(n,"ring_config"))++dep.active.ring_config;
    else if(!strcmp(n,"corrupt_dep"))++dep.active.table.rows[3].output_mask;
    else if(!strcmp(n,"missing_staged"))staged_ok=false;
    else if(!strcmp(n,"missing_ring"))ring_ok=false;
    else if(!strcmp(n,"disabled_staged"))staged.enabled=0;
    else reject=false;
    if(reject){
        uint32_t code=0u,want=0u;
        if(!strcmp(n,"missing_dep"))want=61u;
        else if(!strcmp(n,"inactive_dep"))want=62u;
        else if(!strcmp(n,"uid"))want=45u;
        else if(!strcmp(n,"slot"))want=46u;
        else if(!strcmp(n,"schedule"))want=47u;
        else if(!strcmp(n,"ring_config"))want=44u;
        else if(!strcmp(n,"corrupt_dep"))want=48u;
        else if(!strcmp(n,"disabled_staged"))want=43u;
        else if(!strcmp(n,"missing_staged"))want=65u;
        else if(!strcmp(n,"missing_ring"))want=64u;
        assert(trigger_action_install(&t,trigger_action_table_crc(&t))==expected);
        /* Freeze the bench reject codes: current_binding records one of
         * 41..50/61..65 through the seat-mount diagnostic sink. */
        tdma_seat_mount_store_status(&code,NULL);
        assert(code==want);
        return 0;}
    reentrant=!strcmp(n,"reentrant");install(&t);const trigger_action_snapshot_t original=snapshot();
    assert(original.installed && original.binding_current && original.last_generation==21);unqualified(&original);
    if(reentrant)return 0;
    if(!strcmp(n,"run_snapshot") || !strcmp(n,"run_row") || !strcmp(n,"run_clear")) {
        ring.enabled=1;trigger_action_snapshot_t s=original;
        trigger_action_row_t row={.action_id=99};uint64_t target=77;
        if(!strcmp(n,"run_snapshot")){assert(trigger_action_snapshot(&s)==TRIGGER_ACTION_SERVICE_NOT_IDLE);assert(!memcmp(&s,&original,sizeof(s)));}
        if(!strcmp(n,"run_row")){assert(trigger_action_row(0,&row,&target)==TRIGGER_ACTION_SERVICE_NOT_IDLE && row.action_id==99 && target==77);}
        if(!strcmp(n,"run_clear")){assert(trigger_action_clear()==TRIGGER_ACTION_SERVICE_NOT_IDLE);}
        ring.enabled=0;s=snapshot();assert(s.installed && s.crc==original.crc);return 0;
    }
    if(!strncmp(n,"changed_",8)) {
        const char *f=n+8;
        if(!strcmp(f,"generation"))++dep.active.table.generation;
        else if(!strcmp(f,"crc"))++dep.active.crc;
        else if(!strcmp(f,"uid"))serial="000000000000000F";
        else if(!strcmp(f,"slot"))++staged.local_slot_id;
        else if(!strcmp(f,"role"))dep.active.local_role=1;
        else if(!strcmp(f,"schedule"))++staged.schedule_crc32;
        else if(!strcmp(f,"ring")){++ring.config_seq;++ring.applied_config_seq;}
        else if(!strcmp(f,"remote_row"))dep.active.table.rows[1].output_mask=3;
        else assert(false);
        trigger_action_snapshot_t stale=snapshot();assert(stale.installed && !stale.binding_current && stale.crc==original.crc);
        assert(trigger_action_table_equal(&stale.table,&t));unqualified(&stale);
        trigger_action_row_t row={.action_id=99};uint64_t target=77;
        assert(trigger_action_row(0,&row,&target)==TRIGGER_ACTION_SERVICE_CHANGED && row.action_id==99 && target==77);
        /* A new valid deployment/table generation can replace the stale
         * configuration; a query never silently rebinds it. */
        setup();++dep.active.table.generation;dep.active.crc=trigger_deployment_crc(&dep.active.table);
        serial="0000000000000001";ring=(tdma_ring_clock_snapshot_t){.config_seq=7,.applied_config_seq=7};
        staged=(tdma_service_ring_runtime_config_t){.enabled=1,.local_slot_id=0,.schedule_crc32=123};
        ++t.generation;t.dep_generation=dep.active.table.generation;t.dep_crc=dep.active.crc;install(&t);
        assert(snapshot().binding_current);return 0;
    }
    if(!strcmp(n,"clear_highwater")) {
        assert(!trigger_action_clear());trigger_action_snapshot_t s=snapshot();
        assert(!s.installed && !s.binding_current && s.last_generation==21);unqualified(&s);
        assert(trigger_action_install(&t,trigger_action_table_crc(&t))==TRIGGER_ACTION_SERVICE_GENERATION);
        ++t.generation;install(&t);return 0;
    }
    if(!strcmp(n,"row_range")){trigger_action_row_t row={.action_id=99};uint64_t target=77;assert(trigger_action_row(3,&row,&target)==TRIGGER_ACTION_TABLE_ARGUMENT && row.action_id==99 && target==77);return 0;}
    if(!strcmp(n,"unused_rows_ignored")){t.rows[7].action_id=999;install(&t);assert(snapshot().crc==original.crc);return 0;}
    if(!strcmp(n,"lifecycle")) {
        install(&t);assert(snapshot().crc==original.crc);
        for(unsigned i=0;i<3;i++){trigger_action_row_t row;uint64_t target;assert(!trigger_action_row(i,&row,&target));assert(row.action_id==i+1 && target==t.grid.anchor_vdc_ns+t.rows[i].ordinal*500u);}
        ++t.generation;t.rows[0].argument=2;install(&t);assert(snapshot().last_generation==22);return 0;
    }
    ++t.generation;expected=TRIGGER_ACTION_TABLE_ARGUMENT;
    if(!strcmp(n,"bad_digest")){assert(trigger_action_install(&t,trigger_action_table_crc(&t)^1u)==TRIGGER_ACTION_SERVICE_DIGEST);return 0;}
    else if(!strcmp(n,"bad_code")){t.rows[0].argument=8;expected=TRIGGER_ACTION_TABLE_CODE;}
    else if(!strcmp(n,"bad_dep_generation")){++t.dep_generation;expected=TRIGGER_ACTION_TABLE_DEPLOYMENT;}
    else if(!strcmp(n,"bad_dep_crc")){++t.dep_crc;expected=TRIGGER_ACTION_TABLE_CRC;}
    else if(!strcmp(n,"reserved")){t.rows[0].ordinal=1999;expected=TRIGGER_ACTION_TABLE_RESERVED;}
    else if(!strcmp(n,"overflow")){t.grid.anchor_vdc_ns=UINT64_MAX;expected=TRIGGER_ACTION_TABLE_OVERFLOW;}
    else if(!strcmp(n,"duplicate_id")){t.rows[1].action_id=1;expected=TRIGGER_ACTION_TABLE_ID;}
    else if(!strcmp(n,"overlap")){t.rows[0].span_slices=3;expected=TRIGGER_ACTION_TABLE_CONFLICT;}
    else if(!strcmp(n,"order")){t.rows[1].ordinal=t.rows[0].ordinal;expected=TRIGGER_ACTION_TABLE_ORDER;}
    else if(!strcmp(n,"count"))t.count=9;
    else if(!strcmp(n,"pulse_tick")){t.rows[2].argument=999;expected=TRIGGER_ACTION_TABLE_CODE;}
    else if(!strcmp(n,"reinterpret")){t.generation=21;t.rows[0].argument=2;expected=TRIGGER_ACTION_SERVICE_GENERATION;}
    else if(!strcmp(n,"old_generation")){t.generation=20;expected=TRIGGER_ACTION_SERVICE_GENERATION;}
    else assert(false);
    uint32_t r=trigger_action_install(&t,trigger_action_table_crc(&t));if(r!=expected)fprintf(stderr,"%s %u != %u\n",n,r,expected);assert(r==expected);
    trigger_action_snapshot_t after=snapshot();assert(!memcmp(&after,&original,sizeof(after)));return 0;
}
'''


INSTALL = 'TRIG:RES:ACT:INST ' + ','.join(map(str, [21, 1234, 10, 5678,
    5, 6, 9007199254740993, 1000000, 2000, 1,
    1, 1, 1, 3, 9007199254740995, 2]))


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    units = [ROOT / 'middleware/scpi_port/src/scpi_trigger_grid_commands.c',
             ROOT / 'middleware/scpi_port/src/scpi_trigger_deployment_commands.c',
             ROOT / 'middleware/scpi_port/src/scpi_trigger_action_commands.c']
    return compile_grid(tmp_path_factory.mktemp('action-parser'), 'parser', PARSER,
        [*units, *sorted((ROOT/'third_party/scpi-parser/libscpi/src').glob('*.c')),
         ROOT/'tests/unit/host_stubs/tdma_seat_mount_store_stub.c'], parser=True)


def parse_action(parser, command, mode='accept'):
    r = subprocess.run([str(parser), command, mode], capture_output=True, text=True, timeout=5)
    assert r.returncode == 0, r.stdout+r.stderr
    return list(map(int, r.stdout.strip().split(',')))


def test_scpi_install_keeps_exact_u64_arguments(parser):
    assert parse_action(parser, INSTALL) == [1, 0]
    assert parse_action(parser, INSTALL.replace('9007199254740995', '18446744073709551615')) == [1, 0]
    assert parse_action(parser, 'TRIG:RES:ACT:CLE') == [1, 0]


def test_scpi_summary_and_row_arithmetic_are_not_execution_authority(parser):
    s = parse_action(parser, 'TRIG:RES:ACT?')
    assert s == [1, 0, 1, 1, 1, 21, 21, 1234, 10, 5678, 9007199254740993,
                 1, 3, 7, 123, 5, 6, 9007199254740993, 1000000, 2000, 1, 1, 0, 0, 0, 0]
    assert len(s) == 26
    r = parse_action(parser, 'TRIG:RES:ACT:ROW? 0')
    assert r == [1, 0, 0, 1, 1, 1, 3, 2010, 2, 9007199255745993]
    assert len(r) == 10


@pytest.mark.parametrize('command', [
    'TRIG:RES:ACT:INST', INSTALL.rsplit(',', 1)[0], INSTALL+',1', INSTALL+',',
    INSTALL.replace(',2000,1,', ',2000,9,'),
    INSTALL.replace(',2000,1,', ',2000,4294967296,'),
    INSTALL.replace('9007199254740993', '18446744073709551616'),
    INSTALL.replace('9007199254740995', '18446744073709551616'),
    INSTALL.replace('9007199254740995', '-1'),
    INSTALL.replace('9007199254740993', '9e15'),
    INSTALL.replace('9007199254740993', '9007199254740993.0'),
    INSTALL.replace('INST 21,', 'INST 4294967296,'),
    INSTALL.replace('INST 21,', 'INST -1,'),
    'TRIG:RES:ACT:CLE 1', 'TRIG:RES:ACT? 1', 'TRIG:RES:ACT:ROW?',
    'TRIG:RES:ACT:ROW? -1', 'TRIG:RES:ACT:ROW? 4294967296',
    'TRIG:RES:ACT:ROW? 0,1',
])
def test_scpi_malformed_input_never_calls_owner(parser, command):
    assert parse_action(parser, command, 'reject') == [1, 1]


@pytest.mark.parametrize('command', [INSTALL, 'TRIG:RES:ACT:CLE',
                                    'TRIG:RES:ACT?', 'TRIG:RES:ACT:ROW? 0'])
def test_scpi_busy_is_two_fields(parser, command):
    assert parse_action(parser, command, 'owner_reject') == [1, 32]


@pytest.fixture(scope='module')
def execute_parser(tmp_path_factory):
    units = [ROOT / 'middleware/scpi_port/src/scpi_trigger_grid_commands.c',
             ROOT / 'middleware/scpi_port/src/scpi_trigger_deployment_commands.c',
             ROOT / 'middleware/scpi_port/src/scpi_trigger_action_execute.c']
    return compile_grid(tmp_path_factory.mktemp('action-execute-parser'), 'parser', EXEC_PARSER,
        [*units, *sorted((ROOT/'third_party/scpi-parser/libscpi/src').glob('*.c')),
         ROOT/'tests/unit/host_stubs/tdma_seat_mount_store_stub.c'], parser=True)


def parse_execute(parser, command, mode='accept'):
    r = subprocess.run([str(parser), command, mode], capture_output=True, text=True, timeout=5)
    assert r.returncode == 0, r.stdout+r.stderr
    return list(map(int, r.stdout.strip().split(',')))


def test_execute_scpi_now_and_snapshot_schema(execute_parser):
    now = parse_execute(execute_parser, 'TRIG:RES:ACT:EXEC:NOW?')
    assert len(now) == 23 and now[:5] == [1, 0, 250000000, 1, 0]
    assert now[5] == 9000000000000001 and now[-1] == 1
    snap = parse_execute(execute_parser, 'TRIG:RES:ACT:EXEC?')
    assert len(snap) == 60 and snap[:6] == [1, 0, 1, 0, 1, 1]
    assert all(value in (0, 1) for value in snap[6:9]) and snap[9:11] == [21, 1234]


@pytest.mark.parametrize('command', [
    'TRIG:RES:ACTion:EXECute:PREPare', 'TRIG:RES:ACTion:EXECute:PREPare 1,2', 'TRIG:RES:ACTion:EXECute:PREPare -1',
    'TRIG:RES:ACTion:EXECute:ARM 1', 'TRIG:RES:ACTion:EXECute:STOP 1', 'TRIG:RES:ACTion:EXECute:RELease 1',
    'TRIG:RES:ACTion:EXECute:NOW? 1', 'TRIG:RES:ACTion:EXECute? 1',
])
def test_execute_scpi_strict_parameters(execute_parser, command):
    assert parse_execute(execute_parser, command, 'reject') == [1, 1]


def test_execute_scpi_owner_busy_is_two_fields(execute_parser):
    for command in ('TRIG:RES:ACTion:EXECute:PREPare 0', 'TRIG:RES:ACTion:EXECute:ARM', 'TRIG:RES:ACTion:EXECute:STOP',
                    'TRIG:RES:ACTion:EXECute:RELease', 'TRIG:RES:ACTion:EXECute:NOW?', 'TRIG:RES:ACTion:EXECute?'):
        assert parse_execute(execute_parser, command, 'owner_reject') == [1, 32]


PARSER = DEPLOYMENT_PARSER.split('static const scpi_command_t commands[]')[0] + r'''
#include "trigger_action_service.h"
uint32_t trigger_action_install(const trigger_action_table_t *t,uint32_t crc)
{
    ++calls;assert(t->generation==21 && crc==1234 && t->dep_generation==10 && t->dep_crc==5678);
    assert(t->grid.epoch==5 && t->grid.generation==6 && t->grid.anchor_vdc_ns==UINT64_C(9007199254740993));
    assert(t->grid.period_ns==1000000 && t->grid.subdivisions==2000 && t->count==1);
    const trigger_action_row_t *r=&t->rows[0];
    assert(r->action_id==1 && r->role==1 && r->kind==1 && r->argument==3 && r->span_slices==2);
    assert(r->ordinal==UINT64_C(9007199254740995) || r->ordinal==UINT64_MAX);return result;
}
uint32_t trigger_action_clear(void){++calls;return result;}
uint32_t trigger_action_snapshot(trigger_action_snapshot_t *out)
{
    ++calls;if(result)return result;
    *out=(trigger_action_snapshot_t){.installed=1,.state=TRIGGER_ACTION_SERVICE_INSTALLED_CONFIG,
        .binding_current=1,.last_generation=21,.crc=1234,.local_row_mask=1,
        .binding={.local_uid=UINT64_C(9007199254740993),.local_role=1,.local_slot=3,.ring_config=7,.schedule_crc=123},
        .table={.generation=21,.dep_generation=10,.dep_crc=5678,.count=1,
            .grid={.epoch=5,.generation=6,.anchor_vdc_ns=UINT64_C(9007199254740993),.period_ns=1000000,.subdivisions=2000}}};
    return 0;
}
uint32_t trigger_action_row(uint32_t index,trigger_action_row_t *row,uint64_t *target)
{
    ++calls;assert(index==0);if(result)return result;
    trigger_action_table_t table={.count=1,.grid={.epoch=5,.generation=6,
        .anchor_vdc_ns=UINT64_C(9007199254740993),.period_ns=1000000,.subdivisions=2000},
        .rows={{.action_id=1,.role=1,.kind=1,.argument=3,.ordinal=2010,.span_slices=2}}};
    *row=table.rows[0];assert(trigger_action_table_target(&table,0,target)==0);return 0;
}
#include "scpi_trigger_action_commands.h"
static const scpi_command_t commands[]={
    {.pattern="TRIGger:REServation:ACTion:INSTall",.callback=scpi_cmd_trigger_action_install},
    {.pattern="TRIGger:REServation:ACTion:CLEar",.callback=scpi_cmd_trigger_action_clear},
    {.pattern="TRIGger:REServation:ACTion?",.callback=scpi_cmd_trigger_action_q},
    {.pattern="TRIGger:REServation:ACTion:ROW?",.callback=scpi_cmd_trigger_action_row_q},
    SCPI_CMD_LIST_END};
int main(int argc,char **argv)
{
    assert(argc==3);char input[2048],line[2048];scpi_t c;scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&c,commands,&interface,scpi_units_def,"a","b","c","d",input,sizeof(input),errors,16);
    if(!strcmp(argv[2],"owner_reject"))result=TRIGGER_ACTION_SERVICE_BUSY;
    int n=snprintf(line,sizeof(line),"%s\n",argv[1]);assert(n>0 && (size_t)n<sizeof(line));
    (void)SCPI_Input(&c,line,n);
    if(!strcmp(argv[2],"reject"))assert(!calls);else assert(calls==1 && !SCPI_ErrorCount(&c));
    assert(response_size);printf("%s",response);return 0;
}
'''

EXEC_PARSER = DEPLOYMENT_PARSER.split('static const scpi_command_t commands[]')[0] + r'''
#include "trigger_action_service.h"
uint32_t trigger_action_execution_prepare(uint32_t i){++calls;assert(i==0);return result;}
uint32_t trigger_action_execution_arm(void){++calls;return result;}
uint32_t trigger_action_execution_stop(void){++calls;return result;}
uint32_t trigger_action_execution_release(void){++calls;return result;}
uint32_t trigger_action_execution_now(trigger_action_now_t *o)
{++calls;if(result)return result;memset(o,0,sizeof(*o));o->tick_hz=250000000;o->diagnostic=1;o->anchor_authenticated=0;o->raw_before_tick=9000000000000001ull;o->raw_after_tick=9000000000000010ull;o->vdc_before_ns=9000000000000000ull;o->vdc_after_ns=9000000000000004ull;o->model.token=3;o->model.session=4;o->model.role_generation=5;o->model.clock_epoch_id=6;o->model.clock_run_id=7;o->model.local_slot=0;o->model.valid_from_raw=9000000000000000ull;o->model.dco.dco_update_seq=8;o->model.dco.tdma_schedule_crc32=9;o->model.dco.base_local_tick64=10;o->model.dco.base_vdc_time64_ns=11;o->model.dco.phase_offset_ns=-12;o->model.dco.period_adjust_ppb=-13;o->model.dco.lock_state=1;return 0;}
uint32_t trigger_action_execution_snapshot(trigger_action_execution_snapshot_t *o)
{++calls;if(result)return result;memset(o,0,sizeof(*o));o->loaded=1;o->row_index=0;o->binding_current=1;o->model_current=1;o->diagnostic=1;o->installation.table.generation=21;o->installation.crc=1234;o->installation.binding.local_uid=9007199254740993ull;o->installation.binding.local_role=1;o->installation.binding.local_slot=3;o->plan.ordinal=2010;o->plan.target_vdc_ns=9007199254740993ull;o->plan.deadline_ticks=9007199254740994ull;o->plan.projected_vdc_ns=9007199254740995ull;o->prepared_at_tick=9007199254740996ull;o->expires_at_tick=9007199254740997ull;o->arm_checked_tick=9007199254740998ull;o->model.token=3;o->io.schema=1;o->io.generation=2;o->io.state=1;o->io.loaded=1;o->io.config.kind=1;o->io.config.output_mask=7;o->io.config.code=3;o->io.config.input_port=0;o->io.config.pulse_ns=0;o->io.tick_hz=250000000;o->io.group_count=2;o->io.sm_claimed_mask=10;o->io.instruction_words=8;o->io.expected_receipts=2;o->io.deadline_tick=9007199254740994ull;o->io.enable_before_tick=9007199254740990ull;o->io.enable_after_tick=9007199254741000ull;return 0;}
#include "scpi_trigger_action_execute.h"
static const scpi_command_t commands[]={
 {.pattern="TRIGger:REServation:ACTion:EXECute:PREPare",.callback=scpi_cmd_trigger_action_prepare},
 {.pattern="TRIGger:REServation:ACTion:EXECute:ARM",.callback=scpi_cmd_trigger_action_arm},
 {.pattern="TRIGger:REServation:ACTion:EXECute:STOP",.callback=scpi_cmd_trigger_action_stop},
 {.pattern="TRIGger:REServation:ACTion:EXECute:RELease",.callback=scpi_cmd_trigger_action_release},
 {.pattern="TRIGger:REServation:ACTion:EXECute:NOW?",.callback=scpi_cmd_trigger_action_now_q},
 {.pattern="TRIGger:REServation:ACTion:EXECute?",.callback=scpi_cmd_trigger_action_execute_q},SCPI_CMD_LIST_END};
int main(int argc,char **argv){assert(argc==3);char input[2048],line[2048];scpi_t c;scpi_error_t errors[16];scpi_interface_t interface={.write=output,.flush=flush,.error=error};SCPI_Init(&c,commands,&interface,scpi_units_def,"a","b","c","d",input,sizeof(input),errors,16);if(!strcmp(argv[2],"owner_reject"))result=TRIGGER_ACTION_SERVICE_BUSY;int n=snprintf(line,sizeof(line),"%s\n",argv[1]);assert(n>0);(void)SCPI_Input(&c,line,n);if(!strcmp(argv[2],"reject")){assert(!calls);printf("1,1\n");return 0;}else assert(calls==1&&!SCPI_ErrorCount(&c));assert(response_size);printf("%s",response);return 0;}
'''
