"""Run the production STOP grid owner and real libscpi handlers without IO."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def compile_grid(directory, name, harness, sources=(), parser=False):
    source = directory / f'{name}.c'
    source.write_text(harness, encoding='utf-8')
    compiler = os.environ.get('HOST_CC') or shutil.which('gcc') or shutil.which('clang')
    assert compiler
    includes = [directory, *(ROOT / f'components/{domain}/inc' for domain in (
        'sync_trigger', 'tdma', 'vdc_domain', 'vdc_dpll_manager', 'distributed_refmem',
        'calibration_manager', 'ota_manager', 'sync_io'))]
    flags = []
    if parser:
        includes += [ROOT / 'third_party/scpi-parser/libscpi/inc',
                     ROOT / 'middleware/scpi_port/inc', ROOT / 'middleware/scpi_port/src']
        flags = ['-DSCPI_USER_CONFIG=1', '-Wno-error=attributes']
    executable = directory / (name + ('.exe' if os.name == 'nt' else ''))
    result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffunction-sections', '-fdata-sections', *flags,
        *[f'-I{p}' for p in includes], str(source), *map(str, sources),
        '-Wl,--gc-sections', '-o', str(executable)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.fixture(scope='module')
def owner(tmp_path_factory):
    directory = tmp_path_factory.mktemp('trigger-grid-owner')
    (directory / 'pico').mkdir()
    (directory / 'pico/platform.h').write_text('unsigned get_core_num(void);\n', encoding='utf-8')
    (directory / 'board_config.h').write_text('#define BOARD_SYS_CLOCK_HZ 250000000u\n', encoding='utf-8')
    (directory / 'tdma_runtime_owner.h').write_text(
        '#include "tdma_service.h"\n'
        'tdma_service_service_t *tdma_runtime_owner_get(void);\n'
        'bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out);\n'
        'bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out);\n',
        encoding='utf-8')
    return compile_grid(directory, 'owner', OWNER, [
        ROOT / 'components/sync_trigger/src/trigger_grid_preview.c',
        ROOT / 'components/vdc_domain/src/vdc_domain.c',
            ROOT / 'components/vdc_domain/src/vdc_timestamp.c',
            ROOT / 'components/tdma/src/tdma_profile.c',
            ROOT / 'components/sync_trigger/src/trigger_time_cursor.c'])


@pytest.mark.parametrize('scenario', [
    'success', 'unconfigured', 'core', 'stop', 'adapter', 'pending', 'missing_ring',
    'missing_model', 'invalid_model', 'no_session', 'old_generation', 'reinterpret',
    'clear', 'clear_running', 'wrong_epoch', 'wrong_generation', 'reserved', 'horizon',
    'late', 'session', 'role', 'epoch', 'run', 'slot', 'schedule', 'ring_config',
    'token_between_calls', 'model_during_plan', 'session_during_plan',
    'ring_during_plan', 'clock', 'clock_second', 'clock_reverse', 'lead_expired',
    'model_not_yet_valid', 'configure_race', 'invalid_grid', 'invalid_lead',
    'configure_schedule', 'wrong_period', 'ring_schedule', 'base_not_yet_valid',
    'runtime_retained', 'missing_staged', 'disabled_staged', 'configure_slot',
    'stage_slot_changed', 'stage_schedule_changed', 'stage_during_plan',
    'runtime_slot_conflict', 'runtime_schedule_conflict', 'stop_sequence_race',
    'anchor_none', 'anchor_success', 'anchor_wrong_event', 'anchor_bad_quality',
    'anchor_grid_mismatch', 'anchor_event_changed', 'anchor_new_generation',
])
def test_owner_boundaries(owner, scenario):
    result = subprocess.run([str(owner), scenario], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


OWNER = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "trigger_grid_preview.h"
#include "tdma_runtime_owner.h"
static unsigned core, model_reads, ring_reads, clock_reads, staged_reads, observer_reads;
static bool model_ok=true, ring_ok=true, staged_ok=true;
static uint32_t session=11;
static uint64_t tick=250000;
static const char *race="";
static tdma_ring_clock_snapshot_t ring;
static tdma_service_ring_runtime_config_t staged;
static vdc_dpll_manager_committed_model_t model;
static vdc_dpll_manager_sync_io_observer_status_t observer;
unsigned get_core_num(void) { return core; }
tdma_service_service_t *tdma_runtime_owner_get(void) { return NULL; }
bool tdma_service_update_stopped_metadata(tdma_service_service_t *service,
    bool (*update)(void *), void *context)
{
    (void)service;
    return ring_ok && !ring.enabled && !ring.adapter_started &&
        ring.config_seq==ring.applied_config_seq && update(context);
}
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{
    ++ring_reads;
    if (ring_reads==3 && !strcmp(race,"ring_during_plan")) ring.enabled=true;
    if (ring_reads==2 && !strcmp(race,"stop_sequence_race")) {
        ++ring.config_seq; ++ring.applied_config_seq;
    }
    if (!ring_ok) return false;
    *out=ring; return true;
}
bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out)
{
    ++staged_reads;
    if (staged_reads==2 && !strcmp(race,"stage_during_plan")) ++staged.schedule_crc32;
    if (!staged_ok) return false;
    *out=staged; return out->enabled != 0u;
}
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{
    ++model_reads;
    if(model_reads==2 && (!strcmp(race,"model_during_plan") ||
                         !strcmp(race,"configure_race"))) ++model.token;
    if(model_reads==2 && !strcmp(race,"session_during_plan")) ++session;
    if(!model_ok) return false;
    *out=model; return true;
}
void vdc_dpll_manager_get_sync_io_observer_status(
    vdc_dpll_manager_sync_io_observer_status_t *out)
{
    ++observer_reads;
    if (observer_reads == 2 && !strcmp(race,"anchor_event_changed")) ++observer.last_sample_seq;
    *out=observer;
}
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{
    assert(hz==250000000u); ++clock_reads;
    if(!strcmp(race,"clock") || (clock_reads==2 && !strcmp(race,"clock_second"))) return false;
    *out=tick;
    if(clock_reads==2 && !strcmp(race,"clock_reverse")) --*out;
    if(clock_reads==2 && !strcmp(race,"lead_expired")) *out=500240;
    return true;
}
int main(int argc,char **argv)
{
    assert(argc==2); const char *scenario=argv[1];
    trigger_time_grid_t grid={.epoch=7,.generation=10,.anchor_vdc_ns=2000000,
        .period_ns=1000000,.subdivisions=1000};
    /* STOP retirement clears live identity; the staged Core0 configuration
     * remains enabled and retains the owner-selected identity. */
    ring=(tdma_ring_clock_snapshot_t){.config_seq=3,.applied_config_seq=3};
    staged=(tdma_service_ring_runtime_config_t){.enabled=1,.local_slot_id=1,
        .schedule_crc32=123};
    model=(vdc_dpll_manager_committed_model_t){.token=17,.session=11,.role_generation=4,
        .clock_epoch_id=5,.clock_run_id=6,.local_slot=1,.applied_command_seq=13,
        .dco={.valid=1,.dco_update_seq=19,.nominal_period_ns=1000000,
              .tdma_schedule_crc32=123}};
    observer=(vdc_dpll_manager_sync_io_observer_status_t){.enabled=1,.last_sample_seq=9,
        .last_event_id=7,.expected_window_start_lo=2000000,.expected_window_start_hi=0,
        .sample_period_ns=4,
        .schedule_crc32=123,.last_timestamp_source=VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK,
        .last_timestamp_resolution_ns=4,
        .last_timestamp_flags=VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE};
    trigger_grid_preview_plan_t result,before;
    memset(&result,0xA5,sizeof(result));before=result;
    if(!strcmp(scenario,"unconfigured")) {
        assert(trigger_grid_preview_plan(7,10,1,100,2000000,&result)==TRIGGER_GRID_PREVIEW_UNCONFIGURED);
        assert(!memcmp(&result,&before,sizeof(result)));return 0;
    }
    if(!strcmp(scenario,"invalid_grid")) {
        grid.subdivisions=1;
        assert(trigger_grid_preview_configure(&grid)==TRIGGER_TIME_GRID_ARGUMENT);return 0;
    }
    if(!strcmp(scenario,"wrong_period")) {
        grid.period_ns=2000000;
        assert(trigger_grid_preview_configure(&grid)==TRIGGER_GRID_PREVIEW_MODEL);return 0;
    }
    if(!strcmp(scenario,"configure_race")) {
        race=scenario;
        assert(trigger_grid_preview_configure(&grid)==TRIGGER_GRID_PREVIEW_CHANGED);
        trigger_grid_preview_snapshot_t s;assert(!trigger_grid_preview_snapshot(&s));
        assert(!s.configured && !s.last_generation);return 0;
    }
    if(!strcmp(scenario,"configure_schedule")) {
        ++staged.schedule_crc32;
        assert(trigger_grid_preview_configure(&grid)==TRIGGER_GRID_PREVIEW_MODEL);return 0;
    }
    if(!strcmp(scenario,"configure_slot")) {
        ++staged.local_slot_id;
        assert(trigger_grid_preview_configure(&grid)==TRIGGER_GRID_PREVIEW_MODEL);return 0;
    }
    if(!strcmp(scenario,"missing_staged") || !strcmp(scenario,"disabled_staged") ||
       !strcmp(scenario,"runtime_slot_conflict") || !strcmp(scenario,"runtime_schedule_conflict")) {
        if(!strcmp(scenario,"missing_staged")) staged_ok=false;
        if(!strcmp(scenario,"disabled_staged")) staged.enabled=0;
        if(!strcmp(scenario,"runtime_slot_conflict")) {ring.local_slot_id=2;ring.schedule_crc32=123;}
        if(!strcmp(scenario,"runtime_schedule_conflict")) ring.schedule_crc32=124;
        const tdma_service_ring_runtime_config_t initial=staged;
        assert(trigger_grid_preview_configure(&grid)==TRIGGER_GRID_PREVIEW_NOT_STOPPED);
        assert(!memcmp(&initial,&staged,sizeof(staged)));return 0;
    }
    if(!strcmp(scenario,"runtime_retained")) {
        ring.local_slot_id=staged.local_slot_id;ring.schedule_crc32=staged.schedule_crc32;
    }
    const tdma_ring_clock_snapshot_t initial_ring=ring;
    const tdma_service_ring_runtime_config_t initial_staged=staged;
    assert(trigger_grid_preview_configure(&grid)==TRIGGER_TIME_GRID_OK);
    assert(trigger_grid_preview_configure(&grid)==TRIGGER_TIME_GRID_OK);
    assert(!memcmp(&initial_ring,&ring,sizeof(ring)));
    assert(!memcmp(&initial_staged,&staged,sizeof(staged)));
    if(!strcmp(scenario,"anchor_none")) {
        assert(trigger_grid_preview_plan_anchored(7,10,1,100,2000000,&result)==TRIGGER_GRID_PREVIEW_ANCHOR);
        assert(!memcmp(&result,&before,sizeof(result)));return 0;
    }
    if(!strcmp(scenario,"anchor_wrong_event") || !strcmp(scenario,"anchor_bad_quality") ||
       !strcmp(scenario,"anchor_grid_mismatch") || !strcmp(scenario,"anchor_success") ||
       !strcmp(scenario,"anchor_event_changed") || !strcmp(scenario,"anchor_new_generation")) {
        if(!strcmp(scenario,"anchor_wrong_event")) observer.last_event_id=8;
        if(!strcmp(scenario,"anchor_bad_quality")) observer.last_timestamp_flags=VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
        const uint64_t anchor = !strcmp(scenario,"anchor_grid_mismatch") ? 2000001u : 2000000u;
        const uint32_t bind = trigger_grid_preview_bind_anchor(7,anchor);
        if(!strcmp(scenario,"anchor_wrong_event") || !strcmp(scenario,"anchor_bad_quality") ||
           !strcmp(scenario,"anchor_grid_mismatch")) {
            assert(bind==TRIGGER_GRID_PREVIEW_ANCHOR);return 0;
        }
        assert(bind==TRIGGER_TIME_GRID_OK);
        if(!strcmp(scenario,"anchor_new_generation")) {
            ++grid.generation; assert(trigger_grid_preview_configure(&grid)==TRIGGER_TIME_GRID_OK);
            assert(trigger_grid_preview_plan_anchored(7,11,1,100,2000000,&result)==TRIGGER_GRID_PREVIEW_ANCHOR);
            return 0;
        }
        race=scenario; observer_reads=0;
        const uint32_t anchored=trigger_grid_preview_plan_anchored(7,10,1,100,2000000,&result);
        assert(anchored==(!strcmp(scenario,"anchor_event_changed") ? TRIGGER_GRID_PREVIEW_ANCHOR : TRIGGER_TIME_GRID_OK));
        if(!strcmp(scenario,"anchor_event_changed"))
            assert(!memcmp(&result,&before,sizeof(result)));
        return 0;
    }
    if(!strcmp(scenario,"old_generation") || !strcmp(scenario,"reinterpret")) {
        if(!strcmp(scenario,"old_generation")) --grid.generation;
        else ++grid.anchor_vdc_ns;
        assert(trigger_grid_preview_configure(&grid)==TRIGGER_GRID_PREVIEW_GENERATION);return 0;
    }
    if(!strcmp(scenario,"clear")) {
        assert(!trigger_grid_preview_clear());
        trigger_grid_preview_snapshot_t s;assert(!trigger_grid_preview_snapshot(&s));
        assert(!s.configured && s.last_generation==10);
        assert(trigger_grid_preview_configure(&grid)==TRIGGER_GRID_PREVIEW_GENERATION);
        ++grid.generation;assert(!trigger_grid_preview_configure(&grid));return 0;
    }
    if(!strcmp(scenario,"clear_running")) {
        ring.enabled=true;assert(trigger_grid_preview_clear()==TRIGGER_GRID_PREVIEW_NOT_STOPPED);
        ring.enabled=false;trigger_grid_preview_snapshot_t s;
        assert(!trigger_grid_preview_snapshot(&s) && s.configured);return 0;
    }
    uint32_t expected=TRIGGER_GRID_PREVIEW_CHANGED, epoch=7, generation=10;
    uint64_t q=1,lead=100,ahead=2000000;
    if(!strcmp(scenario,"success") || !strcmp(scenario,"runtime_retained")) expected=TRIGGER_TIME_GRID_OK;
    else if(!strcmp(scenario,"core")) {core=1;expected=TRIGGER_GRID_PREVIEW_BUSY;}
    else if(!strcmp(scenario,"stop")) {ring.enabled=true;expected=TRIGGER_GRID_PREVIEW_NOT_STOPPED;}
    else if(!strcmp(scenario,"adapter")) {ring.adapter_started=true;expected=TRIGGER_GRID_PREVIEW_NOT_STOPPED;}
    else if(!strcmp(scenario,"pending")) {++ring.config_seq;expected=TRIGGER_GRID_PREVIEW_NOT_STOPPED;}
    else if(!strcmp(scenario,"missing_ring")) {ring_ok=false;expected=TRIGGER_GRID_PREVIEW_NOT_STOPPED;}
    else if(!strcmp(scenario,"missing_model")) {model_ok=false;expected=TRIGGER_GRID_PREVIEW_MODEL;}
    else if(!strcmp(scenario,"invalid_model")) {model.dco.valid=0;expected=TRIGGER_GRID_PREVIEW_MODEL;}
    else if(!strcmp(scenario,"no_session")) {session=0;expected=TRIGGER_GRID_PREVIEW_MODEL;}
    else if(!strcmp(scenario,"wrong_epoch")) {++epoch;expected=TRIGGER_GRID_PREVIEW_GENERATION;}
    else if(!strcmp(scenario,"wrong_generation")) {++generation;expected=TRIGGER_GRID_PREVIEW_GENERATION;}
    else if(!strcmp(scenario,"reserved")) {q=999;expected=TRIGGER_TIME_GRID_RESERVED;}
    else if(!strcmp(scenario,"horizon")) {ahead=1000;expected=TRIGGER_TIME_GRID_HORIZON;}
    else if(!strcmp(scenario,"late")) {tick=600000;expected=TRIGGER_TIME_GRID_LATE;}
    else if(!strcmp(scenario,"invalid_lead")) {lead=0;expected=TRIGGER_TIME_GRID_ARGUMENT;}
    else if(!strcmp(scenario,"session")) {++session;++model.session;}
    else if(!strcmp(scenario,"role")) ++model.role_generation;
    else if(!strcmp(scenario,"epoch")) ++model.clock_epoch_id;
    else if(!strcmp(scenario,"run")) ++model.clock_run_id;
    else if(!strcmp(scenario,"slot")) {++model.local_slot;++staged.local_slot_id;}
    else if(!strcmp(scenario,"schedule")) ++model.dco.tdma_schedule_crc32;
    else if(!strcmp(scenario,"ring_schedule")) {ring.schedule_crc32=124;expected=TRIGGER_GRID_PREVIEW_NOT_STOPPED;}
    else if(!strcmp(scenario,"stage_slot_changed")) ++staged.local_slot_id;
    else if(!strcmp(scenario,"stage_schedule_changed")) ++staged.schedule_crc32;
    else if(!strcmp(scenario,"ring_config")) {++ring.config_seq;++ring.applied_config_seq;}
    else if(!strcmp(scenario,"token_between_calls")) {
        ++model.token;++model.dco.dco_update_seq;expected=TRIGGER_TIME_GRID_OK;
    }
    else if(!strcmp(scenario,"model_not_yet_valid")) {model.valid_from_raw=tick+1;expected=TRIGGER_GRID_PREVIEW_MODEL;}
    else if(!strcmp(scenario,"base_not_yet_valid")) {model.dco.base_local_tick64=tick*4+1;expected=TRIGGER_GRID_PREVIEW_MODEL;}
    else {
        race=scenario;
        if(!strncmp(scenario,"clock",5)) expected=TRIGGER_GRID_PREVIEW_CLOCK;
        if(!strcmp(scenario,"lead_expired")) expected=TRIGGER_TIME_GRID_LATE;
        if(!strcmp(scenario,"stop_sequence_race")) expected=TRIGGER_GRID_PREVIEW_NOT_STOPPED;
    }
    model_reads=ring_reads=clock_reads=staged_reads=0;
    const vdc_dpll_manager_committed_model_t model_before=model;
    const tdma_ring_clock_snapshot_t ring_before=ring;
    const tdma_service_ring_runtime_config_t staged_before=staged;
    const uint32_t status=trigger_grid_preview_plan(epoch,generation,q,lead,ahead,&result);
    if(status!=expected) fprintf(stderr,"%s: got %u expected %u\n",scenario,status,expected);
    assert(status==expected);
    if(status) assert(!memcmp(&result,&before,sizeof(result)));
    else {
        assert(result.plan.target_vdc_ns==2001000 && result.plan.deadline_ticks==500250);
        assert(result.model.token==model.token && result.configuration.grid.generation==10);
        assert(result.now_ticks==250000 && result.checked_at_ticks==250000);
        assert(result.minimum_lead_ticks==25 && result.maximum_ahead_ticks==500000);
        assert(!memcmp(&model,&model_before,sizeof(model)));
        assert(!memcmp(&ring,&ring_before,sizeof(ring)));
        assert(!memcmp(&staged,&staged_before,sizeof(staged)));
    }
    /* No output/prepare/ARM functions are linked into this harness at all. */
    return 0;
}
'''


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    library = ROOT / 'third_party/scpi-parser/libscpi/src'
    return compile_grid(tmp_path_factory.mktemp('trigger-grid-parser'), 'parser', PARSER,
                        sorted(library.glob('*.c')), parser=True)


def parser_run(parser, command, mode='accept'):
    result = subprocess.run([str(parser), command, mode], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    return [int(value) for value in result.stdout.strip().split(',')]


@pytest.mark.parametrize('anchor', ['9007199254740993', '18446744073709551615', '+0009007199254740993'])
def test_parser_preserves_u64(parser, anchor):
    assert parser_run(parser, f'TRIG:RES:GRID:CONF 7,10,{anchor},1000000,1000') == [1, 0]


@pytest.mark.parametrize('command', [
    'TRIG:RES:GRID:CONF', 'TRIG:RES:GRID:CONF 7,10,2000000,1000000',
    'TRIG:RES:GRID:CONF 7,10,2000000,1000000,1000,9',
    'TRIG:RES:GRID:CONF 7,10,2000000,1000000,1000,',
    'TRIG:RES:GRID:CONF 7 ,10,2000000,1000000,1000',
    'TRIG:RES:GRID:CONF 7,10,2000000 ,1000000,1000',
    'TRIG:RES:GRID:CONF 7,10,18446744073709551616,1000000,1000',
    'TRIG:RES:GRID:CONF 4294967296,10,2000000,1000000,1000',
    'TRIG:RES:GRID:CONF 7,10,-1,1000000,1000',
    'TRIG:RES:GRID:CONF 7,10,2e6,1000000,1000',
    'TRIG:RES:GRID:CONF 7,10,2000000.0,1000000,1000',
    'TRIG:RES:GRID:CONF 7,10,"2000000",1000000,1000',
    'TRIG:RES:GRID:CONF 7,10,#H100,1000000,1000',
    'TRIG:RES:GRID:CONF 7,10,2000000ns,1000000,1000',
    'TRIG:RES:GRID:PLAN? 7,10,1,100,2000000,1',
    'TRIG:RES:GRID:PLAN? 7,10,1,100,',
    'TRIG:RES:GRID:PLAN? 7,10,1,100,18446744073709551616',
    'TRIG:RES:GRID:CLE 1', 'TRIG:RES:GRID? 1',
])
def test_parser_rejects_without_owner_call(parser, command):
    assert parser_run(parser, command, 'reject') == [1, 1]


def test_parser_plan_and_snapshot_schema(parser):
    snapshot = parser_run(parser, 'TRIG:RES:GRID?')
    assert snapshot == [1, 0, 1, 10, 7, 10, 9007199254740993, 1000000, 1000,
                        11, 3, 4, 5, 6, 1, 123]
    plan = parser_run(parser, 'TRIG:RES:GRID:PLAN? 7,10,18446744073709551615,100,2000000')
    assert plan == snapshot + [17, 19, 13, -29, 9007199254740995, 9007199254740997,
                               -31, 9007199254740999, 18446744073709551615,
                               2001000, 2001020, 500255, 2001002,
                               250000, 250001, 25, 500000, 250000000]
    assert len(plan) == 34
    assert parser_run(parser, 'TRIG:RES:GRID:CLE') == [1, 0]


@pytest.mark.parametrize('command', ['TRIG:RES:GRID?', 'TRIG:RES:GRID:CLE',
    'TRIG:RES:GRID:CONF 7,10,9007199254740993,1000000,1000',
    'TRIG:RES:GRID:PLAN? 7,10,18446744073709551615,100,2000000'])
def test_parser_owner_rejection_is_two_fields(parser, command):
    assert parser_run(parser, command, 'owner_reject') == [1, 33]


def test_parser_anchor_commands(parser):
    assert parser_run(parser, 'TRIG:RES:GRID:ANCH 7,9007199254740993') == [1, 0]
    assert parser_run(parser, 'TRIG:RES:GRID:ANCH?') == [1, 0, 1, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0]
    plan = parser_run(parser, 'TRIG:RES:GRID:ANCH:PLAN? 7,10,18446744073709551615,100,2000000')
    assert plan[0:2] == [1, 0] and len(plan) == 34


PARSER = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scpi/scpi.h"
#include "trigger_grid_preview.h"
static unsigned calls;
static uint32_t owner_result;
static char response[4096];
static size_t response_size;
static size_t output(scpi_t *context,const char *data,size_t length)
{
    (void)context;assert(response_size+length<sizeof(response));
    memcpy(response+response_size,data,length);response_size+=length;
    response[response_size]=0;return length;
}
static scpi_result_t flush(scpi_t *context) {(void)context;return SCPI_RES_OK;}
static int error(scpi_t *context,int_fast16_t value) {(void)context;(void)value;return 0;}
static trigger_grid_preview_snapshot_t configuration(void)
{
    return (trigger_grid_preview_snapshot_t){.configured=1,.last_generation=10,
        .grid={.epoch=7,.generation=10,.anchor_vdc_ns=UINT64_C(9007199254740993),
               .period_ns=1000000,.subdivisions=1000},
        .session=11,.ring_config=3,.role_generation=4,.clock_epoch=5,.clock_run=6,
        .local_slot=1,.schedule_crc=123};
}
uint32_t trigger_grid_preview_configure(const trigger_time_grid_t *grid)
{
    ++calls;assert(grid->epoch==7 && grid->generation==10 && grid->period_ns==1000000 &&
        grid->subdivisions==1000 && (grid->anchor_vdc_ns==UINT64_C(9007199254740993) ||
        grid->anchor_vdc_ns==UINT64_MAX));return owner_result;
}
uint32_t trigger_grid_preview_clear(void) {++calls;return owner_result;}
uint32_t trigger_grid_preview_snapshot(trigger_grid_preview_snapshot_t *out)
{++calls;if(!owner_result)*out=configuration();return owner_result;}
uint32_t trigger_grid_preview_plan(uint32_t epoch,uint32_t generation,uint64_t ordinal,
    uint64_t minimum,uint64_t maximum,trigger_grid_preview_plan_t *out)
{
    ++calls;assert(epoch==7 && generation==10 && ordinal==UINT64_MAX && minimum==100 && maximum==2000000);
    if(owner_result)return owner_result;
    *out=(trigger_grid_preview_plan_t){.configuration=configuration(),
        .model={.token=17,.applied_command_seq=13,.valid_from_raw=UINT64_C(9007199254740999),
            .dco={.dco_update_seq=19,.period_adjust_ppb=-29,.phase_offset_ns=-31,
                .base_local_tick64=UINT64_C(9007199254740995),
                .base_vdc_time64_ns=UINT64_C(9007199254740997)}},
        .plan={.ordinal=ordinal,.target_vdc_ns=2001000,.model_local_ns=2001020,
               .deadline_ticks=500255,.projected_vdc_ns=2001002},
        .now_ticks=250000,.checked_at_ticks=250001,.minimum_lead_ticks=25,
        .maximum_ahead_ticks=500000,.tick_hz=250000000};return 0;
}
uint32_t trigger_grid_preview_bind_anchor(uint32_t event_id,uint64_t anchor)
{++calls;(void)event_id;(void)anchor;return owner_result;}
uint32_t trigger_grid_preview_anchor_snapshot(trigger_grid_anchor_snapshot_t *out)
{++calls;if(!owner_result)*out=(trigger_grid_anchor_snapshot_t){.valid=1,.generation=10};return owner_result;}
uint32_t trigger_grid_preview_plan_anchored(uint32_t epoch,uint32_t generation,uint64_t ordinal,
    uint64_t minimum,uint64_t maximum,trigger_grid_preview_plan_t *out)
{return trigger_grid_preview_plan(epoch,generation,ordinal,minimum,maximum,out);}
#include "scpi_trigger_grid_commands.inc"
static const scpi_command_t commands[]={
    {.pattern="TRIGger:REServation:GRID:CONFigure",.callback=scpi_cmd_trigger_grid_configure},
    {.pattern="TRIGger:REServation:GRID:CLEar",.callback=scpi_cmd_trigger_grid_clear},
    {.pattern="TRIGger:REServation:GRID?",.callback=scpi_cmd_trigger_grid_q},
    {.pattern="TRIGger:REServation:GRID:PLAN?",.callback=scpi_cmd_trigger_grid_plan_q},
    {.pattern="TRIGger:REServation:GRID:ANCHor",.callback=scpi_cmd_trigger_grid_anchor},
    {.pattern="TRIGger:REServation:GRID:ANCHor?",.callback=scpi_cmd_trigger_grid_anchor_q},
    {.pattern="TRIGger:REServation:GRID:ANCHor:PLAN?",.callback=scpi_cmd_trigger_grid_anchor_plan_q},
    SCPI_CMD_LIST_END
};
int main(int argc,char **argv)
{
    assert(argc==3);char input[1024],line[1024];scpi_t context;scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&context,commands,&interface,scpi_units_def,"a","b","c","d",input,sizeof(input),errors,16);
    if(!strcmp(argv[2],"owner_reject"))owner_result=33;
    const int length=snprintf(line,sizeof(line),"%s\n",argv[1]);
    assert(length>0 && (size_t)length<sizeof(line));
    (void)SCPI_Input(&context,line,length);
    if(!strcmp(argv[2],"reject"))assert(!calls);
    else assert(calls==1 && !SCPI_ErrorCount(&context));
    assert(response_size);printf("%s",response);return 0;
}
'''
