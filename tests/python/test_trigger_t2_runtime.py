"""Connect the VDC cursor, turntable predictor, position transport and local queue."""
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "trigger_t2_runtime.h"
#include "trigger_counter_service.h"

static trigger_time_cursor_snapshot_t cursor;
static trigger_counter_snapshot_t counter;
static trigger_position_descriptor_t remote_descriptor;
static bool remote_valid;
static bool tx_pending;
static trigger_position_descriptor_t transmitted[3];
static unsigned transmitted_count;

bool trigger_time_cursor_configure(uint32_t e, uint32_t g, uint32_t sub)
{ cursor=(trigger_time_cursor_snapshot_t){.configured=1,.grid_epoch=e,
    .grid_generation=g,.subdivisions=sub,.session=1}; return true; }
void trigger_time_cursor_clear(void) { memset(&cursor,0,sizeof(cursor)); }
void trigger_time_cursor_service(void)
{ if (!cursor.configured) return; ++cursor.sample_seq; ++cursor.pulse_index;
  cursor.action_cursor=cursor.pulse_index*cursor.subdivisions; cursor.valid=1;
  cursor.last_error=TRIGGER_TIME_CURSOR_OK; }
bool trigger_time_cursor_snapshot(trigger_time_cursor_snapshot_t *out)
{ if (!out) return false; *out=cursor; return true; }

uint32_t trigger_counter_snapshot(trigger_counter_snapshot_t *out)
{ if (!out) return COUNTER_ARGUMENT; *out=counter; return COUNTER_OK; }
bool distributed_refmem_position_descriptor_pending(void) { return tx_pending; }
bool distributed_refmem_stage_position_descriptor(
    const trigger_position_descriptor_t *d, uint32_t mask)
{ (void)mask; assert(d); transmitted[transmitted_count++]=*d; tx_pending=true; return true; }
bool distributed_refmem_get_position_descriptor(uint32_t slot,
    trigger_position_descriptor_t *out)
{ if (!out || slot != 0u || !remote_valid) return false; *out=remote_descriptor; return true; }
bool sync_io_switch_realtime_fire(uint32_t code) { (void)code; return true; }
void sync_io_switch_realtime_service_core1(void) {}
bool sync_io_gateway_realtime_fire(uint32_t pulse_ns) { (void)pulse_ns; return true; }
void sync_io_gateway_realtime_service_core1(void) {}

static trigger_t2_runtime_config_t config(bool origin)
{
    trigger_t2_runtime_config_t c={0};
    c.grid=(trigger_time_grid_t){.epoch=1,.generation=2,.anchor_vdc_ns=0,
        .period_ns=1000000,.subdivisions=4};
    c.predictor=(trigger_turntable_predictor_config_t){1000,0,0,10000,5000,5000,950};
    c.deployment=(trigger_deployment_t){.generation=3,.rows={
        {.board_uid=1,.tdma_slot=0,.input_mask=1},
        {.board_uid=2,.tdma_slot=1,.output_mask=1},
        {.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=1,.feedback_mode=1},
        {.board_uid=4,.tdma_slot=3,.output_mask=1}}};
    c.templates=(trigger_action_template_table_t){.generation=4,.count=1,.row={{
        .template_id=7,.generation=4,.role=1,.kind=TRIGGER_ACTION_TEMPLATE_SWITCH,
        .argument=1,.span_slices=1,.delta_q=0}}};
    c.template_crc=trigger_action_template_table_crc(&c.templates);
    c.run_id=9; c.local_role=origin?0u:1u; c.source_slot=0; c.target_mask=0x0fu; c.origin=origin?1u:0u;
    return c;
}

int main(int argc,char **argv)
{
    assert(argc==2); memset(&counter,0,sizeof(counter)); counter.loaded=1;
    counter.io.valid=1; counter.io.count32=4500;
    if(!strcmp(argv[1],"origin")){
        trigger_t2_runtime_config_t c=config(true);
        assert(trigger_t2_runtime_configure(&c));
        assert(trigger_t2_runtime_start(11,0));
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(transmitted_count==0);
        counter.io.count32=4750; tx_pending=false;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(transmitted_count==1 && transmitted[0].position_id==1 &&
            transmitted[0].target_count==5000 && transmitted[0].prediction_version);
        tx_pending=false; counter.io.count32=5000;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(transmitted_count==2 && transmitted[1].position_id==2);
        return 0;
    }
    if(!strcmp(argv[1],"receiver")){
        trigger_t2_runtime_config_t c=config(false);
        assert(trigger_t2_runtime_configure(&c));
        assert(trigger_t2_runtime_start(11,0));
        remote_descriptor=(trigger_position_descriptor_t){.schema=1,.run_id=9,
            .sync_epoch=1,.position_id=1,.count_epoch=11,.target_count=5000,
            .prediction_version=1,.target_ms_index=1,.target_fine_slice_index=0,
            .grid_generation=2,.deployment_generation=3,
            .deployment_crc=trigger_deployment_crc(&c.deployment),
            .valid_from_sync=1,.expiry_sync=10};
        remote_descriptor.descriptor_crc32=trigger_position_descriptor_crc(&remote_descriptor);
        remote_valid=true;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_action_runtime_event_t event;
        assert(trigger_t2_runtime_take_event(&event));
        assert(event.position_id==1 && event.template_id==7 && event.action_q==4);
        return 0;
    }
    return 2;
}
'''


@pytest.fixture(scope="module")
def runtime(tmp_path_factory):
    return compile_grid(
        tmp_path_factory.mktemp("t2-runtime"), "runtime", HARNESS,
        [ROOT / "components/sync_trigger/src/trigger_t2_runtime.c"],
    )


@pytest.mark.parametrize("case", ["origin", "receiver"])
def test_t2_runtime(runtime, case):
    result = subprocess.run([str(runtime), case], capture_output=True,
                            text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
