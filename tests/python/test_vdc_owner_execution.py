"""Execute production owner and RefMem routines with fault injection.

External services and hardware/profile application boundaries are stubbed.
This is a serial interleaving test, not a claim of measured cross-core timing
or race freedom.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


def test_production_owner_ack_and_failed_topology_rollback(tmp_path):
    manager = (ROOT / "components/vdc_dpll_manager/src/vdc_dpll_manager.c").read_text(encoding="utf-8")
    refmem = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    declarations = manager[manager.index("typedef enum {\n    VDC_DPLL_MANAGER_OWNER_INTENT_NONE"):
                           manager.index("static volatile uint32_t s_vdc_ready_applied_generation;")]
    pending_type = re.search(r"typedef struct \{[^}]+\} distributed_refmem_tdma_topology_pending_t;", refmem).group()
    signatures = [
        "static uint32_t vdc_dpll_manager_increment_nonzero(",
        "static bool vdc_dpll_manager_read_ready_request(",
        "static bool vdc_dpll_manager_owner_intent_begin_locked(",
        "static bool vdc_dpll_manager_is_control_intent(",
        "static void vdc_dpll_manager_publish_control_request(",
        "static void vdc_dpll_manager_publish_control_result(",
        "static void vdc_dpll_manager_owner_intent_end_locked(",
        "void vdc_dpll_manager_get_owner_status(",
        "bool vdc_dpll_manager_get_control_owner_status(",
        "bool vdc_dpll_manager_reject_control_owner_status(",
        "bool vdc_dpll_manager_complete_control_owner_status(",
        "bool vdc_dpll_manager_ack_owner_status(",
        "bool vdc_dpll_manager_owner_change_pending(",
        "static bool vdc_dpll_manager_control_transaction_open(",
        "static bool vdc_dpll_manager_queue_tdma_ring_topology(",
        "bool vdc_dpll_manager_set_tdma_ring_topology(",
        "bool vdc_dpll_manager_restore_tdma_ring_topology(",
    ]
    code = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "vdc_dpll_manager.h"
#include "tdma_profile.h"
static void osal_critical_enter(void) {}
static void osal_critical_exit(void) {}
'''
    code += declarations + pending_type + "\n"
    code += "\n".join(function(manager, signature) for signature in signatures)
    code += r'''
static distributed_refmem_tdma_topology_pending_t s_tdma_topology_pending;
static bool apply_ok;
static unsigned model_changes;
static tdma_foundation_profile_t profile;
static bool refmem_application_model_set_tdma_ring_topology(uint32_t local,
    uint32_t reference, uint32_t count)
{ (void)local; (void)reference; (void)count; ++model_changes; return true; }
static const tdma_foundation_profile_t *refmem_application_model_get_tdma_foundation_profile(void)
{ return &profile; }
static bool distributed_refmem_tdma_profile_activation_ready(
    const tdma_foundation_profile_t *p, uint32_t *crc)
{ (void)p; *crc=123; return true; }
static bool distributed_refmem_apply_tdma_foundation_profile(
    const tdma_foundation_profile_t *p, uint32_t crc)
{ (void)p; (void)crc; return apply_ok; }
'''
    code += function(refmem, "static void distributed_refmem_tdma_topology_service(void)")
    code += r'''
/* Drive the completed Core1 transition; the production status and ACK
 * functions below must not infer completion from request submission. */
static void complete(uint32_t result) {
    s_owner_intent_result_sequence=s_owner_intent.sequence;
    s_owner_intent_result_opcode=s_owner_intent.opcode;
    s_owner_intent_result=result;
    if (s_owner_intent.report_control)
        vdc_dpll_manager_publish_control_result(s_owner_intent.sequence,
            s_owner_intent.opcode,result);
    s_owner_intent_consumed_sequence=s_owner_intent.sequence;
}
static void check(uint32_t sequence, uint32_t result, bool pending) {
    vdc_dpll_manager_owner_status_t status; bool busy=false;
    assert(vdc_dpll_manager_get_control_owner_status(&status,&busy));
    assert(status.requested_sequence==sequence && status.result==result);
    assert(busy==pending);
}
int main(void) {
    uint32_t first=0, next=0;
    s_vdc_ready_request.generation=1;
    assert(vdc_dpll_manager_set_tdma_ring_topology(0,0,2,&first));
    check(first,VDC_DPLL_MANAGER_OWNER_RESULT_PENDING,true);
    assert(!vdc_dpll_manager_ack_owner_status(first));
    assert(!vdc_dpll_manager_set_tdma_ring_topology(0,0,2,&next));
    complete(VDC_DPLL_MANAGER_OWNER_RESULT_APPLIED);
    check(first,VDC_DPLL_MANAGER_OWNER_RESULT_APPLIED,true);
    assert(!vdc_dpll_manager_ack_owner_status(first+1));
    assert(vdc_dpll_manager_ack_owner_status(first));
    check(first,VDC_DPLL_MANAGER_OWNER_RESULT_APPLIED,true);
    assert(!vdc_dpll_manager_set_tdma_ring_topology(0,0,2,&next));
    assert(vdc_dpll_manager_complete_control_owner_status(first));
    check(first,VDC_DPLL_MANAGER_OWNER_RESULT_APPLIED,false);

    /* Subsequent observation traffic must not erase management completion. */
    assert(vdc_dpll_manager_owner_intent_begin_locked(
        VDC_DPLL_MANAGER_OWNER_INTENT_TDMA_EVIDENCE));
    vdc_dpll_manager_owner_intent_end_locked();
    complete(VDC_DPLL_MANAGER_OWNER_RESULT_REJECTED);
    check(first,VDC_DPLL_MANAGER_OWNER_RESULT_APPLIED,false);

    assert(vdc_dpll_manager_set_tdma_ring_topology(1,0,2,&next));
    complete(VDC_DPLL_MANAGER_OWNER_RESULT_APPLIED);
    s_tdma_topology_pending=(distributed_refmem_tdma_topology_pending_t){
        .pending=true,.owner_sequence=next,.control_sequence=next,
        .local_slot_id=1,.node_count=2,
        .previous_node_count=2};
    apply_ok=false;
    distributed_refmem_tdma_topology_service();
    check(next,VDC_DPLL_MANAGER_OWNER_RESULT_REJECTED,true);
    assert(model_changes==2 && s_tdma_topology_pending.rollback_needed);
    assert(s_tdma_topology_pending.owner_sequence==0);
    uint32_t forbidden=0;
    assert(!vdc_dpll_manager_set_tdma_ring_topology(0,0,2,&forbidden));
    /* The internal restore must leave the failed user's token visible. */
    distributed_refmem_tdma_topology_service();
    assert(s_tdma_topology_pending.owner_sequence!=0);
    check(next,VDC_DPLL_MANAGER_OWNER_RESULT_REJECTED,true);
    complete(VDC_DPLL_MANAGER_OWNER_RESULT_APPLIED);
    distributed_refmem_tdma_topology_service();
    assert(!s_tdma_topology_pending.pending);
    check(next,VDC_DPLL_MANAGER_OWNER_RESULT_REJECTED,false);

    /* An interrupted publication is unreadable and remains pending. */
    s_control_result.guard|=1;
    vdc_dpll_manager_owner_status_t status; bool pending=false;
    assert(!vdc_dpll_manager_get_control_owner_status(&status,&pending));
    assert(pending);
    puts("production owner execution passed");
    return 0;
}
'''
    source = tmp_path / "owner.c"
    source.write_text(code, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    output = tmp_path / "owner.exe"
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
               *("-I" + str(ROOT / "components" / component / "inc")
                 for component in ("vdc_dpll_manager", "vdc_domain", "tdma", "calibration_manager")),
               str(source), "-o", str(output)]
    build = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "production owner execution passed" in result.stdout


def test_follower_rx_submits_only_while_tdma_admission_is_open(tmp_path):
    refmem = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    follower_type = re.search(
        r"typedef struct \{\n(?:    .+\n)+\} distributed_refmem_vdc_follower_rx_t;",
        refmem).group()
    signatures = [
        "static uint32_t distributed_refmem_next_nonzero_sequence(",
        "static bool distributed_refmem_tdma_busy(",
        "static bool distributed_refmem_tdma_admission_open(",
        "static void distributed_refmem_vdc_follower_rx_process_completed(",
    ]
    code = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "refmem_realtime_tdma.h"
#include "refmem_sync.h"
#include "tdma_traffic_scheduler.h"
#include "vdc_dpll_manager.h"
#define BOARD_REFMEM_SPI_BAUD_HZ 25000000u
#define BOARD_REFMEM_SPI_RX_PIN 0u
#define BOARD_REFMEM_SPI_SCK_PIN 1u
#define BOARD_REFMEM_SPI_TX_PIN 2u
'''
    code += follower_type + "\n"
    code += r'''
static refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
static refmem_sync_vdc_context_t s_vdc_command_context;
static distributed_refmem_vdc_follower_rx_t s_vdc_follower_rx;
static vdc_dpll_manager_refmem_snapshot_t test_vdc;
static refmem_realtime_tdma_snapshot_t test_tdma;
static tdma_traffic_scheduler_t test_scheduler;
static bool scheduler_available = true;
static bool scheduler_snapshot_available = true;
static uint32_t scheduler_snapshot_count;
static bool submit_ok = true;
static uint32_t submit_count;

tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void);
static uint32_t board_identity_get_no(void) { return 1u; }
bool vdc_dpll_manager_get_refmem_snapshot(
    vdc_dpll_manager_refmem_snapshot_t *snapshot)
{ *snapshot = test_vdc; return true; }
bool refmem_realtime_tdma_get_snapshot_for_payload_class(
    const refmem_realtime_tdma_service_t *service, uint32_t payload_class,
    refmem_realtime_tdma_snapshot_t *snapshot)
{
    (void)service; (void)payload_class; *snapshot = test_tdma; return true;
}
static bool distributed_refmem_refresh_vdc_command_context_from_snapshot(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot)
{ (void)snapshot; return true; }
tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void)
{ return scheduler_available ? &test_scheduler : NULL; }
bool tdma_traffic_scheduler_get_snapshot(
    tdma_traffic_scheduler_t *scheduler,
    tdma_traffic_scheduler_snapshot_t *snapshot)
{
    (void)scheduler;
    ++scheduler_snapshot_count;
    if (!scheduler_snapshot_available) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->configured = test_scheduler.configured;
    snapshot->admission_open = test_scheduler.admission_open;
    return true;
}
bool refmem_realtime_tdma_get_result_frame_for_payload_class(
    const refmem_realtime_tdma_service_t *service, uint32_t payload_class,
    uint8_t *frame, size_t frame_capacity, size_t *frame_size)
{
    (void)service; (void)payload_class; (void)frame; (void)frame_capacity;
    *frame_size = 0u; return false;
}
refmem_sync_rx_result_t refmem_sync_vdc_receive_frame(
    refmem_sync_vdc_context_t *context, const uint8_t *frame,
    size_t frame_size, refmem_sync_rx_snapshot_t *snapshot)
{
    (void)context; (void)frame; (void)frame_size; (void)snapshot;
    return REFMEM_SYNC_RX_FRAME_INVALID;
}
const refmem_sync_vdc_command_snapshot_t *refmem_sync_vdc_get_command(
    const refmem_sync_vdc_context_t *context, uint8_t source_slot)
{ (void)context; (void)source_slot; return NULL; }
bool vdc_dpll_manager_plan_published_tdma_window(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot, uint32_t window_class,
    uint64_t now_ns, vdc_tdma_window_plan_t *plan, vdc_gate_result_t *gate)
{
    (void)window_class; (void)now_ns; (void)gate;
    memset(plan, 0, sizeof(*plan));
    plan->valid = 1u; plan->schedule_epoch = 7u;
    plan->schedule_crc32 = snapshot->schedule.schedule_crc32;
    plan->window_start_ns = 1000u; plan->window_end_ns = 2000u;
    plan->guard_start_ns = 900u; plan->guard_end_ns = 2100u;
    return true;
}
bool vdc_dpll_manager_translate_tdma_transport_plan(
    const vdc_tdma_window_plan_t *source, vdc_tdma_window_plan_t *transport)
{ *transport = *source; return true; }
bool refmem_realtime_tdma_submit_rx(
    refmem_realtime_tdma_service_t *service,
    const refmem_realtime_tdma_intent_config_t *config)
{
    (void)service; (void)config; ++submit_count;
    if (submit_ok) test_tdma.intent_seq++;
    return submit_ok;
}
'''
    code += "\n".join(function(refmem, signature) for signature in signatures)
    service_signature = "static void distributed_refmem_vdc_follower_rx_service("
    service_source = refmem[refmem.rindex(service_signature):]
    code += "\n" + function(service_source, service_signature)
    code += r'''
static void reset_fixture(void)
{
    memset(&s_vdc_follower_rx, 0, sizeof(s_vdc_follower_rx));
    memset(&test_vdc, 0, sizeof(test_vdc));
    memset(&test_tdma, 0, sizeof(test_tdma));
    memset(&test_scheduler, 0, sizeof(test_scheduler));
    scheduler_available = true;
    scheduler_snapshot_available = true;
    scheduler_snapshot_count = 0u;
    submit_ok = true;
    submit_count = 0u;
    test_vdc.control_profile.valid = 1u;
    test_vdc.control_profile.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    test_vdc.control_profile.follow_master_slot_id = 0u;
    test_vdc.schedule.schedule_crc32 = 123u;
}
int main(void)
{
    reset_fixture();
    scheduler_available = false;
    for (uint32_t i = 0u; i < 1000u; ++i)
        distributed_refmem_vdc_follower_rx_service();
    assert(submit_count == 0u && s_vdc_follower_rx.submit_reject_count == 0u);
    assert(scheduler_snapshot_count == 0u);

    scheduler_available = true;
    test_scheduler.admission_open = 1u;
    distributed_refmem_vdc_follower_rx_service();
    assert(submit_count == 0u && s_vdc_follower_rx.submit_reject_count == 0u);

    test_scheduler.configured = 1u;
    scheduler_snapshot_available = false;
    distributed_refmem_vdc_follower_rx_service();
    assert(submit_count == 0u && s_vdc_follower_rx.submit_reject_count == 0u);

    scheduler_snapshot_available = true;
    test_scheduler.admission_open = 0u;
    scheduler_snapshot_count = 0u;
    for (uint32_t i = 0u; i < 1000u; ++i)
        distributed_refmem_vdc_follower_rx_service();
    assert(submit_count == 0u && s_vdc_follower_rx.submit_reject_count == 0u);
    assert(scheduler_snapshot_count == 0u);

    s_vdc_follower_rx.active = 1u;
    s_vdc_follower_rx.active_intent_seq = 4u;
    test_tdma.completed_seq = 4u;
    test_tdma.last_result = REFMEM_REALTIME_TDMA_RESULT_TIMEOUT;
    distributed_refmem_vdc_follower_rx_service();
    assert(s_vdc_follower_rx.active == 0u);
    assert(s_vdc_follower_rx.timeout_count == 1u && submit_count == 0u);

    test_scheduler.admission_open = 1u;
    test_tdma.intent_seq = test_tdma.completed_seq;
    distributed_refmem_vdc_follower_rx_service();
    assert(submit_count == 1u && s_vdc_follower_rx.active == 1u);
    assert(s_vdc_follower_rx.submitted_count == 1u);

    s_vdc_follower_rx.active = 0u;
    test_tdma.intent_seq = test_tdma.completed_seq;
    submit_ok = false;
    distributed_refmem_vdc_follower_rx_service();
    assert(submit_count == 2u && s_vdc_follower_rx.submit_reject_count == 1u);
    assert(s_vdc_follower_rx.last_error == 4u);
    puts("follower admission execution passed");
    return 0;
}
'''
    source = tmp_path / "follower.c"
    source.write_text(code, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    output = tmp_path / "follower.exe"
    include_components = (
        "distributed_refmem", "vdc_dpll_manager", "vdc_domain", "tdma",
        "calibration_manager", "refmem_table_registry", "sync_trigger")
    command = [
        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I" + str(ROOT / "config"),
        *("-I" + str(ROOT / "components" / component / "inc")
          for component in include_components),
        str(source), "-o", str(output),
    ]
    build = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "follower admission execution passed" in result.stdout


def test_master_publish_discards_closed_batches_and_defers_unknown_state(tmp_path):
    refmem = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    batch_type = re.search(
        r"typedef struct \{\n(?:    .+\n)+\} distributed_refmem_vdc_command_batch_t;",
        refmem).group()
    signatures = [
        "static uint32_t distributed_refmem_next_nonzero_sequence(",
        "static bool distributed_refmem_tdma_admission_open(",
        "static void DISTRIBUTED_REFMEM_TIME_CRITICAL(\n    distributed_refmem_reset_vdc_command_batch)(void)",
        "static bool DISTRIBUTED_REFMEM_TIME_CRITICAL(\n    distributed_refmem_begin_vdc_command_batch)(",
        "static bool DISTRIBUTED_REFMEM_TIME_CRITICAL(\n    distributed_refmem_capture_vdc_command_batch)(",
        "static bool distributed_refmem_publish_vdc_command_once(",
        "void DISTRIBUTED_REFMEM_TIME_CRITICAL(\n    distributed_refmem_realtime_run_once)(void)",
        "void distributed_refmem_tdma_publish_service(void)",
    ]
    code = r'''
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "distributed_refmem.h"
#include "refmem_realtime_tdma.h"
#include "refmem_sync.h"
#include "tdma_traffic_scheduler.h"
#include "vdc_dpll_manager.h"
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) name
#define BOARD_REFMEM_SPI_BAUD_HZ 25000000u
#define BOARD_REFMEM_SPI_RX_PIN 0u
#define BOARD_REFMEM_SPI_SCK_PIN 1u
#define BOARD_REFMEM_SPI_TX_PIN 2u
'''
    code += batch_type + "\n"
    code += r'''
static refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
static distributed_refmem_vdc_command_batch_t s_vdc_command_batch;
static uint32_t s_vdc_command_sequence;
static uint32_t s_vdc_command_frame_sequence;
static uint32_t s_vdc_command_published_update_seq = UINT32_MAX;
static uint32_t s_vdc_command_published_generation = UINT32_MAX;
static uint32_t s_vdc_command_published_source_slot = UINT32_MAX;
static uint32_t s_vdc_command_published_schedule_crc32 = UINT32_MAX;
static bool s_initialized;
static vdc_dpll_manager_refmem_snapshot_t test_vdc;
static tdma_traffic_scheduler_t test_scheduler;
static bool vdc_snapshot_available = true;
static bool scheduler_available = true;
static bool scheduler_snapshot_available = true;
static uint32_t scheduler_snapshot_count;
static bool submit_ok = true;
static uint32_t submit_count;
static uint32_t submit_reject_count;
static uint32_t plan_count;
static uint32_t built_target;
static uint32_t submitted_target[16];

tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void)
{ return scheduler_available ? &test_scheduler : NULL; }
bool tdma_traffic_scheduler_get_snapshot(
    tdma_traffic_scheduler_t *scheduler,
    tdma_traffic_scheduler_snapshot_t *snapshot)
{
    (void)scheduler;
    ++scheduler_snapshot_count;
    if (!scheduler_snapshot_available) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->configured = test_scheduler.configured;
    snapshot->admission_open = test_scheduler.admission_open;
    return true;
}
bool vdc_dpll_manager_get_refmem_snapshot(
    vdc_dpll_manager_refmem_snapshot_t *snapshot)
{
    if (!vdc_snapshot_available) return false;
    *snapshot = test_vdc;
    return true;
}
static bool distributed_refmem_refresh_vdc_command_context_from_snapshot(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot)
{ (void)snapshot; return true; }
static void distributed_refmem_publish_runtime_vector_if_pending(
    uint32_t source_update_seq)
{ (void)source_update_seq; }
bool vdc_dpll_manager_plan_published_tdma_window(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot, uint32_t window_class,
    uint64_t now_ns, vdc_tdma_window_plan_t *plan, vdc_gate_result_t *gate)
{
    (void)window_class; (void)now_ns; (void)gate;
    memset(plan, 0, sizeof(*plan));
    ++plan_count;
    plan->valid = 1u;
    plan->schedule_epoch = snapshot->schedule.schedule_epoch;
    plan->schedule_crc32 = snapshot->schedule.schedule_crc32;
    plan->window_start_ns = 100000u + plan_count * 1000u;
    plan->window_end_ns = plan->window_start_ns + 500u;
    plan->guard_start_ns = plan->window_start_ns - 100u;
    plan->guard_end_ns = plan->window_end_ns + 100u;
    return true;
}
bool vdc_dpll_manager_local_to_vdc_time(uint64_t local_time_ns,
                                        uint64_t *vdc_time_ns)
{ *vdc_time_ns = local_time_ns + 1000000u; return true; }
bool vdc_dpll_manager_translate_tdma_transport_plan(
    const vdc_tdma_window_plan_t *source, vdc_tdma_window_plan_t *transport)
{ *transport = *source; return true; }
uint32_t osal_tick_ms(void) { return 42u; }
bool refmem_sync_vdc_command_frame_build(
    uint8_t source_slot, uint8_t target_slot, uint32_t epoch_id,
    uint32_t run_id, uint32_t frame_seq32, uint32_t compact_time,
    uint32_t control_generation, uint32_t command_seq,
    uint32_t schedule_crc32, uint64_t effective_vdc_time_ns,
    int32_t period_adjust_ppb, int32_t phase_offset_ns, uint32_t lock_state,
    uint32_t quality, uint8_t *frame, size_t frame_capacity,
    size_t *frame_size)
{
    (void)source_slot; (void)epoch_id; (void)run_id; (void)frame_seq32;
    (void)compact_time; (void)control_generation; (void)command_seq;
    (void)schedule_crc32; (void)effective_vdc_time_ns;
    (void)period_adjust_ppb; (void)phase_offset_ns; (void)lock_state;
    (void)quality;
    assert(frame_capacity != 0u);
    built_target = target_slot;
    frame[0] = target_slot;
    *frame_size = 1u;
    return true;
}
bool refmem_realtime_tdma_submit_tx(
    refmem_realtime_tdma_service_t *service,
    const refmem_realtime_tdma_intent_config_t *config)
{
    (void)service;
    assert(config->payload_class == REFMEM_REALTIME_TDMA_PAYLOAD_VDC_COMMAND);
    assert(config->frame_size == 1u && config->frame[0] == built_target);
    submitted_target[submit_count++] = built_target;
    if (!submit_ok) {
        ++submit_reject_count;
        return false;
    }
    return true;
}
'''
    code += "\n".join(
        function(refmem[refmem.rindex(signature):], signature)
        for signature in signatures)
    code += r'''
static void reset_fixture(uint32_t node_count)
{
    memset(&s_vdc_command_batch, 0, sizeof(s_vdc_command_batch));
    memset(&test_vdc, 0, sizeof(test_vdc));
    memset(&test_scheduler, 0, sizeof(test_scheduler));
    memset(submitted_target, 0, sizeof(submitted_target));
    s_vdc_command_sequence = 0u;
    s_vdc_command_frame_sequence = 0u;
    s_vdc_command_published_update_seq = UINT32_MAX;
    s_vdc_command_published_generation = UINT32_MAX;
    s_vdc_command_published_source_slot = UINT32_MAX;
    s_vdc_command_published_schedule_crc32 = UINT32_MAX;
    s_initialized = true;
    vdc_snapshot_available = true;
    scheduler_available = true;
    scheduler_snapshot_available = true;
    scheduler_snapshot_count = 0u;
    submit_ok = true;
    submit_count = 0u;
    submit_reject_count = 0u;
    plan_count = 0u;
    test_vdc.control_profile.valid = 1u;
    test_vdc.control_profile.mode = VDC_DPLL_CONTROL_MODE_MASTER;
    test_vdc.control_profile.generation = 11u;
    test_vdc.schedule.local_slot_id = 0u;
    test_vdc.schedule.ring_binding.node_count = node_count;
    test_vdc.schedule.schedule_crc32 = 123u;
    test_vdc.schedule.schedule_epoch = 7u;
    test_vdc.clock_epoch_id = 3u;
    test_vdc.clock_run_id = 5u;
    test_vdc.dpll_update_seq = 17u;
    test_scheduler.configured = 1u;
}

static void cycle(void)
{
    distributed_refmem_realtime_run_once();
    distributed_refmem_tdma_publish_service();
}

int main(void)
{
    /* A readable STOPPED scheduler must neither capture nor submit. */
    reset_fixture(2u);
    test_scheduler.admission_open = 0u;
    for (uint32_t i = 0u; i < 1000u; ++i) cycle();
    assert(submit_count == 0u && submit_reject_count == 0u);
    assert(scheduler_snapshot_count == 0u);
    assert(s_vdc_command_batch.active == 0u);
    assert(s_vdc_command_published_update_seq == UINT32_MAX);

    /* Unknown scheduler state defers an existing batch without mutating it. */
    test_scheduler.admission_open = 1u;
    distributed_refmem_realtime_run_once();
    assert(s_vdc_command_batch.active != 0u);
    const uint32_t deferred_command = s_vdc_command_batch.command_seq;
    scheduler_snapshot_available = false;
    for (uint32_t i = 0u; i < 1000u; ++i) cycle();
    assert(submit_count == 0u && s_vdc_command_batch.active != 0u);
    assert(s_vdc_command_batch.command_seq == deferred_command);
    assert(s_vdc_command_batch.window_planned == 0u);
    assert(s_vdc_command_published_update_seq == UINT32_MAX);

    /* Once STOPPED is readable, that deferred batch is discarded. */
    scheduler_snapshot_available = true;
    test_scheduler.admission_open = 0u;
    distributed_refmem_tdma_publish_service();
    assert(s_vdc_command_batch.active == 0u);
    assert(s_vdc_command_published_update_seq == UINT32_MAX);
    test_scheduler.admission_open = 1u;
    distributed_refmem_realtime_run_once();
    assert(s_vdc_command_batch.active != 0u);
    assert(s_vdc_command_batch.command_seq != deferred_command);
    distributed_refmem_tdma_publish_service();
    assert(submit_count == 1u && submitted_target[0] == 1u);

    /* Closing after one target invalidates the partial batch.  Reopen starts
     * again at the first target with a new command and future window. */
    reset_fixture(3u);
    test_scheduler.admission_open = 1u;
    cycle();
    assert(submit_count == 1u && submitted_target[0] == 1u);
    assert(s_vdc_command_batch.active != 0u);
    const uint32_t partial_command = s_vdc_command_batch.command_seq;
    const uint64_t partial_window =
        s_vdc_command_batch.transport_window_start_ns;
    test_scheduler.admission_open = 0u;
    distributed_refmem_tdma_publish_service();
    assert(s_vdc_command_batch.active == 0u);
    assert(s_vdc_command_published_update_seq == UINT32_MAX);
    test_scheduler.admission_open = 1u;
    cycle();
    assert(submit_count == 2u && submitted_target[1] == 1u);
    assert(s_vdc_command_batch.command_seq != partial_command);
    assert(s_vdc_command_batch.transport_window_start_ns != partial_window);

    /* Even when the final target was admitted, STOP before the finalize pass
     * must not mark that batch published. */
    reset_fixture(2u);
    test_scheduler.admission_open = 1u;
    cycle();
    assert(s_vdc_command_batch.next_target_index == 2u);
    test_scheduler.admission_open = 0u;
    distributed_refmem_tdma_publish_service();
    assert(s_vdc_command_batch.active == 0u);
    assert(s_vdc_command_published_update_seq == UINT32_MAX);

    /* Identity changes while admission is unreadable are not guessed.  Once
     * readable/open, the old batch is cleared, then rebuilt from the new
     * production snapshot on the following capture phase. */
    reset_fixture(2u);
    test_scheduler.admission_open = 1u;
    distributed_refmem_realtime_run_once();
    const uint32_t old_command = s_vdc_command_batch.command_seq;
    scheduler_snapshot_available = false;
    test_vdc.control_profile.generation++;
    test_vdc.dpll_update_seq++;
    cycle();
    assert(s_vdc_command_batch.active != 0u);
    assert(s_vdc_command_batch.command_seq == old_command);
    scheduler_snapshot_available = true;
    distributed_refmem_realtime_run_once();
    assert(s_vdc_command_batch.active == 0u);
    distributed_refmem_realtime_run_once();
    assert(s_vdc_command_batch.active != 0u);
    assert(s_vdc_command_batch.command_seq != old_command);
    assert(s_vdc_command_batch.control_generation ==
           test_vdc.control_profile.generation);

    /* A real submit rejection while OPEN remains observable and retryable. */
    reset_fixture(2u);
    test_scheduler.admission_open = 1u;
    distributed_refmem_realtime_run_once();
    submit_ok = false;
    distributed_refmem_tdma_publish_service();
    assert(submit_count == 1u && submit_reject_count == 1u);
    assert(s_vdc_command_batch.active != 0u);
    assert(s_vdc_command_batch.next_target_index == 1u);
    submit_ok = true;
    distributed_refmem_tdma_publish_service();
    assert(submit_count == 2u && submitted_target[1] == 1u);
    puts("master admission execution passed");
    return 0;
}
'''
    source = tmp_path / "master.c"
    source.write_text(code, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    output = tmp_path / "master.exe"
    include_components = (
        "distributed_refmem", "vdc_dpll_manager", "vdc_domain", "tdma",
        "calibration_manager", "refmem_table_registry", "sync_trigger")
    command = [
        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I" + str(ROOT / "config"),
        *("-I" + str(ROOT / "components" / component / "inc")
          for component in include_components),
        str(source), "-o", str(output),
    ]
    build = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "master admission execution passed" in result.stdout
