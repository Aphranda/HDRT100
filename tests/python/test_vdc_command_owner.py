"""Execute the production follower boundary; retain Core0 command ownership.

The follower harness uses real data types and time mapping. RefMem reads,
the clock source and the downstream Domain actuator are observable stubs;
this tests the manager boundary, not the complete Domain or hardware DCO.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
MANAGER = ROOT / "components/vdc_dpll_manager/src/vdc_dpll_manager.c"
REFMEM = ROOT / "components/distributed_refmem/src/distributed_refmem.c"


def function_body(source, name):
    # Accept both ordinary and TIME_CRITICAL(name)(void) definitions while
    # requiring the opening brace, so a forward declaration cannot match.
    definition = re.search(rf"\b{re.escape(name)}\s*\)?\s*\([^;{{}}]*\)\s*\{{", source)
    assert definition, f"Missing production definition: {name}"
    opening = definition.end()
    depth = 1
    cursor = opening
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[opening:cursor - 1]


def compile_executable(directory, name, text, sources=()):
    source = directory / f"{name}.c"
    source.write_text(text, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "A host C compiler is required for the real owner tests"
    exe = directory / (name + (".exe" if os.name == "nt" else ""))
    includes = [ROOT / f"components/{component}/inc" for component in (
        "tdma", "vdc_domain", "vdc_dpll_manager", "distributed_refmem")]
    result = subprocess.run([
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        *[f"-I{path}" for path in includes], str(source),
        *map(str, sources), "-o", str(exe),
    ], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.fixture(scope="module")
def follower_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("vdc-command-owner")
    manager = MANAGER.read_text(encoding="utf-8")
    body = function_body(manager, "vdc_dpll_manager_consume_follower_command")
    capture_defines = "\n".join(re.findall(
        r"^#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_\w+\s+\d+u", manager, re.M))
    assert "FOLLOWER_COMMAND" in capture_defines and "FOLLOWER_STATE" in capture_defines
    harness = r'''
#include <assert.h>
#include <string.h>
#include "vdc_domain.h"
#include "vdc_time_mapping.h"
#include "refmem_sync.h"
static vdc_domain_context_t s_vdc_domain;
static uint32_t s_vdc_follower_last_applied_seq, s_vdc_follower_last_generation;
static uint32_t s_vdc_follower_last_epoch_id, s_vdc_follower_last_run_id;
static uint32_t s_vdc_follower_capture_kind_hint;
static refmem_sync_vdc_command_snapshot_t retained_source;
static tdma_ring_clock_snapshot_t ring_source;
static vdc_dpll_follower_command_t last_command;
static uint32_t reads, requested_source, ring_reads, missing, late, apply_calls, dco_writes;
static bool retained_available, ring_available, domain_accept;
static uint64_t now_ns;

static bool distributed_refmem_get_vdc_follower_command(uint32_t source,
    refmem_sync_vdc_command_snapshot_t *out)
{
    ++reads; requested_source = source; *out = retained_source;
    return retained_available;
}
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{
    ++ring_reads; *out = ring_source; return ring_available;
}
static uint64_t vdc_dpll_manager_now_ns(void) { return now_ns; }
void vdc_domain_note_follower_command_missing(vdc_domain_context_t *context)
{
    assert(context == &s_vdc_domain); ++missing;
}
void vdc_domain_note_follower_command_late(vdc_domain_context_t *context)
{
    assert(context == &s_vdc_domain); ++late;
}
bool vdc_domain_apply_follower_command(vdc_domain_context_t *context,
    const vdc_dpll_follower_command_t *command)
{
    assert(context == &s_vdc_domain); ++apply_calls; last_command = *command;
    if (!command->valid || !domain_accept) return false;
    ++dco_writes;
    context->dco.period_adjust_ppb = command->period_adjust_ppb;
    context->dco.phase_offset_ns = command->phase_offset_ns;
    return true;
}
''' + capture_defines + "\nstatic void vdc_dpll_manager_consume_follower_command(void) {\n" + body + r'''
}
static void reset_fixture(void)
{
    memset(&s_vdc_domain, 0, sizeof(s_vdc_domain));
    memset(&retained_source, 0, sizeof(retained_source));
    memset(&ring_source, 0, sizeof(ring_source));
    memset(&last_command, 0, sizeof(last_command));
    s_vdc_follower_last_applied_seq = s_vdc_follower_last_generation = 0;
    s_vdc_follower_last_epoch_id = s_vdc_follower_last_run_id = 0;
    s_vdc_follower_capture_kind_hint = 0;
    reads = requested_source = ring_reads = missing = late = apply_calls = dco_writes = 0;
    retained_available = ring_available = domain_accept = true;
    s_vdc_domain.control.profile = (vdc_dpll_control_profile_t){
        .valid = 1, .mode = VDC_DPLL_CONTROL_MODE_FOLLOWER,
        .follow_master_slot_id = 0, .generation = 9};
    s_vdc_domain.schedule.local_slot_id = 1;
    s_vdc_domain.schedule.schedule_crc32 = 0x1234;
    s_vdc_domain.clock.epoch_id = 41; s_vdc_domain.clock.run_id = 51;
    s_vdc_domain.dco.period_adjust_ppb = -700;
    s_vdc_domain.dco.phase_offset_ns = -800;
    retained_source = (refmem_sync_vdc_command_snapshot_t){
        .valid = 1, .source_slot = 0, .target_slot = 1,
        .control_generation = 77, .command_seq = 1, .schedule_crc32 = 0x1234,
        .epoch_id = 41, .run_id = 51, .effective_vdc_time_ns = 51000,
        .period_adjust_ppb = 123, .phase_offset_ns = -45,
        .lock_state = VDC_DOMAIN_LOCK_FREQ_LOCK, .quality = 1};
    now_ns = 11000;
    ring_source.enabled = ring_source.adapter_started = 1;
    ring_source.cycle_period_ns = 2000; ring_source.feedback_timeout_ns = 8000;
    ring_source.schedule_crc32 = s_vdc_domain.schedule.schedule_crc32;
    ring_source.clock_observation.valid = 1;
    ring_source.clock_observation.correlated_frame_evidence = 1;
    ring_source.clock_observation.correlation_flags = TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
        TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME;
    ring_source.clock_observation.timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    ring_source.clock_observation.local_rx_timestamp_ns = 10000;
    ring_source.clock_observation.common_effective_time_ns = 50000;
}
static void assert_applied(uint32_t count)
{
    assert(apply_calls == count && dco_writes == count && !missing && !late);
    assert(requested_source == s_vdc_domain.control.profile.follow_master_slot_id);
    assert(last_command.valid == retained_source.valid);
    assert(last_command.source_slot_id == retained_source.source_slot);
    assert(last_command.control_generation == retained_source.control_generation);
    assert(last_command.command_seq == retained_source.command_seq);
    assert(last_command.schedule_crc32 == retained_source.schedule_crc32);
    assert(last_command.effective_vdc_time_ns == retained_source.effective_vdc_time_ns);
    assert(last_command.period_adjust_ppb == retained_source.period_adjust_ppb);
    assert(last_command.phase_offset_ns == retained_source.phase_offset_ns);
    assert(last_command.lock_state == retained_source.lock_state);
    assert(last_command.quality == retained_source.quality);
    assert(s_vdc_follower_last_applied_seq == retained_source.command_seq);
    assert(s_vdc_follower_capture_kind_hint == VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_COMMAND);
}
int main(int argc, char **argv)
{
    assert(argc == 2); reset_fixture();
    const char *scenario = argv[1];
    if (!strcmp(scenario, "unicast") || !strcmp(scenario, "broadcast")) {
        if (!strcmp(scenario, "broadcast")) retained_source.target_slot = REFMEM_SYNC_VDC_TARGET_BROADCAST;
        vdc_dpll_manager_consume_follower_command(); assert_applied(1);
        /* MASTER's generation is intentionally different from local FOLLOWER generation. */
        assert(last_command.control_generation != s_vdc_domain.control.profile.generation);
        vdc_dpll_manager_consume_follower_command(); assert_applied(1); assert(ring_reads == 1);
    } else if (!strcmp(scenario, "epoch_mismatch") || !strcmp(scenario, "run_mismatch")) {
        if (!strcmp(scenario, "epoch_mismatch")) ++retained_source.epoch_id;
        else ++retained_source.run_id;
        vdc_dpll_manager_consume_follower_command();
        assert(missing == 1 && apply_calls == 0 && dco_writes == 0 && ring_reads == 0);
        assert(s_vdc_follower_last_applied_seq == 0 && s_vdc_follower_capture_kind_hint == 0);
        assert(s_vdc_domain.dco.period_adjust_ppb == -700 && s_vdc_domain.dco.phase_offset_ns == -800);
        /* Repair context, retaining the very same command sequence: it must remain eligible. */
        retained_source.epoch_id = s_vdc_domain.clock.epoch_id;
        retained_source.run_id = s_vdc_domain.clock.run_id; missing = 0;
        vdc_dpll_manager_consume_follower_command(); assert_applied(1);
    } else if (!strcmp(scenario, "epoch_restart") || !strcmp(scenario, "run_restart") ||
               !strcmp(scenario, "both_restart") || !strcmp(scenario, "generation_restart")) {
        retained_source.command_seq = 99;
        vdc_dpll_manager_consume_follower_command(); assert_applied(1);
        if (!strcmp(scenario, "epoch_restart") || !strcmp(scenario, "both_restart"))
            ++s_vdc_domain.clock.epoch_id;
        if (!strcmp(scenario, "run_restart") || !strcmp(scenario, "both_restart"))
            ++s_vdc_domain.clock.run_id;
        if (!strcmp(scenario, "generation_restart")) ++s_vdc_domain.control.profile.generation;
        retained_source.command_seq = 1;
        if (strcmp(scenario, "generation_restart")) {
            /* Core0 has not refreshed yet: old retained command cannot set the new-session watermark. */
            vdc_dpll_manager_consume_follower_command();
            assert(missing == 1 && apply_calls == 1 && dco_writes == 1);
            assert(s_vdc_follower_last_applied_seq == 0); missing = 0;
            assert(s_vdc_domain.control.profile.generation == 9);
        }
        retained_source.epoch_id = s_vdc_domain.clock.epoch_id;
        retained_source.run_id = s_vdc_domain.clock.run_id;
        retained_source.effective_vdc_time_ns += 1000; now_ns += 1000;
        vdc_dpll_manager_consume_follower_command(); assert_applied(2);
    } else if (!strcmp(scenario, "wrong_source") || !strcmp(scenario, "wrong_target")) {
        if (!strcmp(scenario, "wrong_source")) retained_source.source_slot = 2;
        else retained_source.target_slot = 2;
        vdc_dpll_manager_consume_follower_command();
        assert(reads == 1 && requested_source == 0 && apply_calls == 1 && !last_command.valid);
        assert(dco_writes == 0 && ring_reads == 0 && s_vdc_follower_last_applied_seq == 1);
        assert(s_vdc_follower_capture_kind_hint == VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_STATE);
        vdc_dpll_manager_consume_follower_command(); assert(apply_calls == 1);
    } else if (!strcmp(scenario, "missing") || !strcmp(scenario, "invalid")) {
        if (!strcmp(scenario, "missing")) retained_available = false;
        else retained_source.valid = 0;
        vdc_dpll_manager_consume_follower_command();
        assert(missing == 1 && !apply_calls && !ring_reads && !dco_writes);
    } else if (!strcmp(scenario, "non_follower")) {
        vdc_dpll_manager_consume_follower_command(); assert_applied(1);
        s_vdc_domain.control.profile.mode = VDC_DPLL_CONTROL_MODE_MASTER;
        vdc_dpll_manager_consume_follower_command();
        assert(reads == 1 && apply_calls == 1 && s_vdc_follower_last_applied_seq == 0);
        s_vdc_domain.control.profile.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
        vdc_dpll_manager_consume_follower_command(); assert_applied(2);
        s_vdc_domain.control.profile.valid = 0;
        vdc_dpll_manager_consume_follower_command(); assert(reads == 2 && apply_calls == 2);
    } else if (!strcmp(scenario, "clock_unavailable")) {
        ring_available = false; vdc_dpll_manager_consume_follower_command();
        assert(!apply_calls && !missing && !s_vdc_follower_last_applied_seq);
        ring_available = true; ring_source.clock_observation.valid = 0;
        vdc_dpll_manager_consume_follower_command(); assert(!apply_calls && !s_vdc_follower_last_applied_seq);
        ring_source.clock_observation.valid = 1;
        vdc_dpll_manager_consume_follower_command(); assert_applied(1);
    } else if (!strcmp(scenario, "not_due") || !strcmp(scenario, "too_late")) {
        retained_source.effective_vdc_time_ns = !strcmp(scenario, "not_due") ? 52000 : 48000;
        vdc_dpll_manager_consume_follower_command();
        if (!strcmp(scenario, "not_due")) {
            assert(!apply_calls && !late && !s_vdc_follower_last_applied_seq);
            now_ns += 1000; vdc_dpll_manager_consume_follower_command(); assert_applied(1);
        } else {
            assert(late == 1 && apply_calls == 1 && !last_command.valid && !dco_writes);
            vdc_dpll_manager_consume_follower_command(); assert(apply_calls == 1);
        }
    } else if (!strcmp(scenario, "domain_rejected")) {
        domain_accept = false; vdc_dpll_manager_consume_follower_command();
        assert(apply_calls == 1 && !dco_writes && s_vdc_follower_last_applied_seq == 1);
        assert(s_vdc_follower_capture_kind_hint == VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_STATE);
        vdc_dpll_manager_consume_follower_command(); assert(apply_calls == 1);
    } else { assert(!"Unknown scenario"); }
    return 0;
}
'''
    return compile_executable(directory, "follower", harness, [
        ROOT / "components/vdc_dpll_manager/src/vdc_time_mapping.c"])


@pytest.mark.parametrize("scenario", [
    "unicast", "broadcast", "epoch_mismatch", "run_mismatch",
    "epoch_restart", "run_restart", "both_restart", "generation_restart",
    "wrong_source", "wrong_target", "missing", "invalid", "non_follower",
    "clock_unavailable", "not_due", "too_late", "domain_rejected",
])
def test_actual_follower_command_boundary(follower_executable, scenario):
    result = subprocess.run([str(follower_executable), scenario],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, f"{scenario}: {result.stdout}{result.stderr}"


@pytest.mark.parametrize("entry", [
    "distributed_refmem_realtime_run_once",
    "distributed_refmem_configure_node_load_auto_sync",
])
def test_other_tasks_do_not_retire_receiver_command_context(entry):
    body = function_body(REFMEM.read_text(encoding="utf-8"), entry)
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
    assert "refresh_vdc_command_context" not in body
    assert "refmem_sync_vdc_init" not in body
    assert "refmem_sync_vdc_reset" not in body
    assert "s_vdc_command_context" not in body


def test_actual_core0_service_refreshes_before_receiving(tmp_path):
    body = function_body(REFMEM.read_text(encoding="utf-8"), "distributed_refmem_service")
    # Observe order from the actual entry, without hardware or RTOS machinery.
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint32_t table_seq; } refmem_vector_header_region_t;
typedef struct { uint32_t heartbeat, slot_version, last_update_ms, state; } refmem_vector_node_region_t;
#define DISTRIBUTED_REFMEM_LOCAL_NODE_ID 0u
#define DISTRIBUTED_REFMEM_NODE_OK 1u
static bool s_initialized, ota, s_vdc_command_context_ready, refresh_ok = true;
static uint32_t s_service_count, order, refreshed, received, node_load, flight_services;
static refmem_vector_header_region_t header_storage;
static refmem_vector_node_region_t node_storage;
static void osal_critical_enter(void) { }
static void osal_critical_exit(void) { }
static uint32_t osal_tick_ms(void) { return 100; }
static refmem_vector_header_region_t *distributed_refmem_header(void) { return &header_storage; }
static refmem_vector_node_region_t *distributed_refmem_node_region(uint32_t node)
{ assert(node == 0); return &node_storage; }
static void distributed_refmem_publish_runtime_locked(void) { }
static void distributed_refmem_publish_status_locked(void) { }
static bool ota_ao_is_active(void) { return ota; }
static bool distributed_refmem_refresh_vdc_command_context(void)
{ assert(order == 0); order = 1; ++refreshed; return refresh_ok; }
static void distributed_refmem_vdc_follower_rx_service(void)
{ assert(order == 1 && s_vdc_command_context_ready); order = 2; ++received; }
static void distributed_refmem_node_load_auto_service(void)
{ assert(s_vdc_command_context_ready); ++node_load; }
static void distributed_refmem_tdma_flight_sync_service(void) { ++flight_services; }
static void distributed_refmem_log_tdma_ring_service(void) { }
static void distributed_refmem_service(void) {
''' + body + r'''
}
int main(void)
{
    distributed_refmem_service(); assert(!refreshed && !received && !s_service_count);
    s_initialized = true; ota = true;
    distributed_refmem_service(); assert(!refreshed && !received);
    ota = false;
    distributed_refmem_service(); assert(refreshed == 1 && received == 1 && order == 2);
    order = 0; distributed_refmem_service(); assert(refreshed == 2 && received == 2);
    /* A transient snapshot failure gates command reception, while ordinary
     * resident mailbox service remains live and the following beat retries. */
    order = 0; refresh_ok = false; distributed_refmem_service();
    assert(refreshed == 3 && received == 2 && node_load == 2 && flight_services == 3);
    assert(!s_vdc_command_context_ready);
    order = 0; refresh_ok = true; distributed_refmem_service();
    assert(refreshed == 4 && received == 3 && node_load == 3 && flight_services == 4);
    return 0;
}
'''
    executable = compile_executable(tmp_path, "service", harness)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
