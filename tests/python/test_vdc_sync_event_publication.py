"""VDC publishes one exact hardware event alongside each local sync_seq."""
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid
from test_vdc_command_owner import function_body
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body


def test_vdc_sync_event_publication(tmp_path):
    source = (ROOT / "components/vdc_dpll_manager/src/vdc_dpll_manager.c").read_text(
        encoding="utf-8")
    service = function_body(source, "vdc_sync_ao_service")
    getter = c_definition_body(source, "vdc_dpll_manager_get_sync_event")
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "vdc_dpll_manager.h"
static vdc_dpll_manager_vdc_status_t s_vdc_status,s_published_vdc_status;
static vdc_dpll_manager_sync_event_t s_vdc_sync_event_candidate;
static vdc_dpll_manager_sync_event_t s_published_vdc_sync_event;
static volatile uint32_t s_published_vdc_sync_event_guard;
static bool s_vdc_ready=true, observation_ready;
static uint32_t board_uptime_ms(void){return 100u;}
static bool vdc_dpll_manager_ring_observer_service(uint32_t *lock_state){
    if(!observation_ready)return false;
    *lock_state=VDC_DOMAIN_LOCK_LOCKED;
    return true;
}
void vdc_sync_ao_service(void){
''' + service + r'''
}
bool vdc_dpll_manager_get_sync_event(vdc_dpll_manager_sync_event_t *event){
''' + getter + r'''
}
int main(void){
    vdc_dpll_manager_sync_event_t event;
    vdc_sync_ao_service();
    assert(vdc_dpll_manager_get_sync_event(&event)&&!event.valid);
    observation_ready=true;
    s_vdc_sync_event_candidate=(vdc_dpll_manager_sync_event_t){
        .valid=1,.ring_config_seq=7,.carrier_sequence=41,
        .identity_crc32=0x1234,.schedule_crc32=0x5678,
        .timestamp_flags=TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED,
        .clock_epoch_id=2,.clock_run_id=3,.local_slot=1,
        .local_rx_timestamp_ns=UINT64_C(10000000)};
    vdc_sync_ao_service();
    assert(s_published_vdc_status.sync_seq==1);
    assert(vdc_dpll_manager_get_sync_event(&event)&&event.valid&&
        event.sync_seq==1&&event.carrier_sequence==41&&
        event.local_rx_timestamp_ns==UINT64_C(10000000));
    s_vdc_sync_event_candidate.carrier_sequence=42;
    vdc_sync_ao_service();
    assert(s_published_vdc_status.sync_seq==2);
    assert(vdc_dpll_manager_get_sync_event(&event)&&event.sync_seq==2&&
        event.carrier_sequence==42);
    observation_ready=false;
    vdc_sync_ao_service();
    assert(s_published_vdc_status.sync_seq==2);
    assert(vdc_dpll_manager_get_sync_event(&event)&&!event.valid);
    assert(!(s_published_vdc_sync_event_guard&1u));
    return 0;
}
'''
    executable = compile_grid(tmp_path, "vdc-sync-event", harness)
    result = subprocess.run([str(executable)], capture_output=True, text=True,
                            timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
