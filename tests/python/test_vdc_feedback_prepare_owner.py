"""Execute the real RefMem service boundary around Core0 preparation.

Scheduling dependencies are observable stubs; the production service function
and public RefMem types are unchanged. Cache arithmetic is covered separately.
"""
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import MANAGER, REFMEM, compile_executable, function_body


@pytest.fixture(scope="module")
def prepare_owner_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("feedback-prepare-owner")
    service = ingress_definition(REFMEM.read_text(encoding="utf-8"), "distributed_refmem_service")
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "distributed_refmem.h"
#include "refmem_vector_table.h"
static bool s_initialized, s_vdc_command_context_ready, ota_active, context_ready;
static bool in_critical;
static unsigned prepare_calls, refresh_calls, rx_calls, load_calls, drain_calls, log_calls;
static uint32_t s_service_count;
static refmem_vector_header_region_t header;
static refmem_vector_node_region_t node;
static char events[128];
static unsigned event_count;
static void event(char value) { assert(event_count+1<sizeof(events));events[event_count++]=value; }
static void osal_critical_enter(void) { assert(!in_critical);in_critical=true;event('E'); }
static void osal_critical_exit(void) { assert(in_critical);in_critical=false;event('X'); }
static refmem_vector_header_region_t *distributed_refmem_header(void)
{ assert(in_critical);return &header; }
static refmem_vector_node_region_t *distributed_refmem_node_region(uint32_t id)
{ assert(in_critical && id==DISTRIBUTED_REFMEM_LOCAL_NODE_ID);return &node; }
static void distributed_refmem_publish_runtime_locked(void) { assert(in_critical);event('R'); }
static void distributed_refmem_publish_status_locked(void) { assert(in_critical);event('S'); }
static uint32_t osal_tick_ms(void) { assert(in_critical);return 1234; }
static bool ota_ao_is_active(void) { assert(!in_critical);event('O');return ota_active; }
/* No new sample is available: preparation returns without a result. The
 * enclosing service must still progress and must never borrow RX first. */
void vdc_dpll_manager_feedback_prepare_core0(void)
{ assert(!in_critical && !ota_active);++prepare_calls;event('P'); }
static bool distributed_refmem_refresh_vdc_command_context(void)
{ assert(!in_critical && prepare_calls==refresh_calls+1);++refresh_calls;event('C');return context_ready; }
static void distributed_refmem_vdc_follower_rx_service(void)
{ assert(!in_critical && s_vdc_command_context_ready);++rx_calls;event('F'); }
static void distributed_refmem_node_load_auto_service(void)
{ assert(!in_critical && s_vdc_command_context_ready);++load_calls;event('A'); }
static void distributed_refmem_tdma_flight_sync_service(void)
{ assert(!in_critical && prepare_calls==drain_calls+1);++drain_calls;event('D'); }
static void distributed_refmem_log_tdma_ring_service(void)
{ assert(!in_critical && drain_calls==log_calls+1);++log_calls;event('L'); }
''' + service + r'''
int main(int argc,char **argv)
{
    assert(argc==2);
    s_initialized=strcmp(argv[1],"uninitialized")!=0;
    ota_active=!strcmp(argv[1],"ota");
    context_ready=strcmp(argv[1],"context_unready")!=0;
    distributed_refmem_service();
    if(!s_initialized) {
        assert(!event_count && !s_service_count && !header.table_seq && !node.heartbeat);
        assert(!prepare_calls && !refresh_calls && !drain_calls);
    } else {
        assert(s_service_count==1 && header.table_seq==1 && node.heartbeat==1);
        assert(node.slot_version==1 && node.last_update_ms==1234 && node.state==DISTRIBUTED_REFMEM_NODE_OK);
        if(ota_active) {
            assert(!strcmp(events,"ERSXO") && !prepare_calls && !refresh_calls && !drain_calls && !log_calls);
        } else {
            assert(!strcmp(events,context_ready?"ERSXOPCFADL":"ERSXOPCDL"));
            assert(prepare_calls==1 && refresh_calls==1 && drain_calls==1 && log_calls==1);
            assert(rx_calls==(unsigned)context_ready && load_calls==(unsigned)context_ready);
            distributed_refmem_service();
            assert(prepare_calls==2 && refresh_calls==2 && drain_calls==2 && log_calls==2);
        }
    }
    assert(!in_critical);puts("Core0 RefMem preparation ownership passed");return 0;
}
'''
    return compile_executable(directory, "feedback_prepare_owner", harness)


@pytest.mark.parametrize("case", ["uninitialized", "ota", "context_ready", "context_unready"])
def test_feedback_prepare_owner(prepare_owner_executable, case):
    result = subprocess.run([str(prepare_owner_executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.fixture(scope="module")
def authorization_owner_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("feedback-authorization-owner")
    body = function_body(MANAGER.read_text(encoding="utf-8"), "sync_dpll_fb_service")
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
static vdc_domain_context_t s_vdc_domain;
static bool s_vdc_domain_service_pending;
static bool debug_continue, role_apply, role_pending, tune, finalize, prepare;
static unsigned token=17, retire_calls, authorize_calls;
static char events[64];
static unsigned event_count;
static void event(char value) { assert(event_count+1<sizeof(events));events[event_count++]=value; }
static bool vdc_dpll_manager_apply_pending_debug_continue(void) { event('D');return debug_continue; }
static bool vdc_dpll_manager_apply_pending_dpll_role(void) { event('R');return role_apply; }
void vdc_dpll_manager_get_dpll_role_status(vdc_dpll_manager_dpll_role_status_t *out)
{ memset(out,0,sizeof(*out));out->pending=role_pending;event('G'); }
static void vdc_dpll_manager_feedback_match_retire(void) { token=0;++retire_calls;event('X'); }
static void vdc_dpll_manager_feedback_match_service(void) { token=18;++authorize_calls;event('A'); }
static void vdc_dpll_manager_publish_runtime_snapshot_locked(void) { event('P'); }
static void vdc_dpll_manager_consume_follower_command(void) { event('C'); }
static bool vdc_dpll_manager_apply_pending_debug_servo_tune(void) { event('T');return tune; }
static uint64_t vdc_dpll_manager_now_ns(void) { return 1234; }
void vdc_domain_service(vdc_domain_context_t *ctx,uint64_t now)
{ assert(ctx==&s_vdc_domain && now==1234);assert(!s_vdc_domain_service_pending);event('S'); }
static bool vdc_dpll_manager_finalize_ring_evidence(void) { event('F');return finalize; }
static bool vdc_dpll_manager_prepare_ring_evidence(void) { event('B');return prepare; }
static bool vdc_dpll_manager_apply_ring_evidence(void) { event('I');return true; }
void sync_dpll_fb_service(void) {
''' + body + r'''
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *mode=argv[1];
    debug_continue=!strcmp(mode,"debug_pending");
    role_apply=!strcmp(mode,"role_apply");
    role_pending=debug_continue || !strcmp(mode,"role_pending");
    tune=!strcmp(mode,"tune");
    s_vdc_domain_service_pending=!strcmp(mode,"domain");
    finalize=!strcmp(mode,"finalize");
    prepare=!strcmp(mode,"prepare");
    sync_dpll_fb_service();
    if(debug_continue || role_apply || role_pending) {
        assert(token==0 && retire_calls==1 && !authorize_calls);
        assert(!strcmp(events,debug_continue?"DXP":role_apply?"DRXP":"DRGXP"));
    } else {
        assert(token==18 && authorize_calls==1 && !retire_calls);
        const char *expected=tune?"DRGACTP":!strcmp(mode,"domain")?"DRGACTSP":
            finalize?"DRGACTFP":prepare?"DRGACTFB":"DRGACTFBI";
        assert(!strcmp(events,expected));
    }
    puts("Core1 feedback authorization service boundary passed");return 0;
}
'''
    return compile_executable(directory, "feedback_authorization_owner", harness)


@pytest.mark.parametrize("case", [
    "debug_pending", "role_apply", "role_pending", "tune", "domain", "finalize", "prepare", "apply",
])
def test_feedback_authorization_owner(authorization_owner_executable, case):
    result = subprocess.run([str(authorization_owner_executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
