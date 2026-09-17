"""Execute the actual realtime dispatch with the production sequence mailbox."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def _function(source, signature):
    start = source.index(signature)
    body_start = source.index("{", start)
    depth = 1
    end = body_start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def test_sequence_mailbox_survives_disabled_loads_and_offline_returns(tmp_path):
    app = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    dispatch = _function(app, "void app_realtime_run_once(void)")
    tdma_phase = _function(app, "static void app_realtime_tdma_phase(void)")
    legacy = (ROOT / "components/sync_trigger/src/sync_trigger.c").read_text(encoding="utf-8")
    legacy_service = legacy.split("void sync_trigger_service(void)", 1)[1].split(
        "void sync_trigger_get_summary", 1)[0]
    assert "trigger_sequence_service_service();" not in legacy_service
    assert "trigger_sequence_service_is_active()" in legacy_service
    transport = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    assert "trigger_sequence_link_service();" not in transport
    (tmp_path / "sync_trigger.h").write_text(
        "#include <stdbool.h>\nbool sync_trigger_sequence_can_start(void);\n", encoding="utf-8")
    unit = (ROOT / "tests/unit/test_trigger_sequence_service.c").as_posix()
    harness = ('#define trigger_sequence_service_service production_sequence_service\n'
               '#define main sequence_lifecycle_test_main\n#include "' + unit +
               '"\n#undef main\n#undef trigger_sequence_service_service\n')
    harness += r'''
static bool offline_p3, offline_training, ring_capture, loads_enabled, link_step;
static unsigned optional_calls, mandatory_calls;
static bool phase_open;
static unsigned phase_order, sequence_calls;
static struct { uint32_t cycle_count; } s_realtime_schedule;
enum { APP_REALTIME_PHASE_TDMA, APP_REALTIME_PHASE_VDC, APP_REALTIME_PHASE_DPLL,
 APP_REALTIME_PHASE_CALIBRATION, APP_REALTIME_PHASE_SYNC_CAPTURE,
 APP_REALTIME_PHASE_REFMEM, APP_REALTIME_PHASE_MODEL, APP_REALTIME_PHASE_SYNC_TRIGGER,
 APP_REALTIME_PHASE_TRIGGER_MEASURE, APP_REALTIME_LOAD_VDC, APP_REALTIME_LOAD_DPLL,
 APP_REALTIME_LOAD_CALIBRATION, APP_REALTIME_LOAD_SYNC_CAPTURE, APP_REALTIME_LOAD_REFMEM,
 APP_REALTIME_LOAD_MODEL, APP_REALTIME_LOAD_SYNC_TRIGGER, APP_REALTIME_LOAD_TRIGGER_MEASURE,
 DIAGNOSTICS_WATCHDOG_TASK_CORE1, TDMA_TIMING_ANALYZER, TDMA_TIMING_ACCOUNTING };
static bool calibration_manager_p3_offline_active_core1(void) { return offline_p3; }
static bool calibration_manager_ring_capture_offline_active_core1(void) { return ring_capture; }
static bool calibration_manager_training_offline_active_core1(void) { return offline_training; }
static void calibration_manager_p3_service_core1(void) { ++mandatory_calls; }
static void calibration_manager_service_core1(void) { ++mandatory_calls; }
static void drv_watchdog_mark_progress(unsigned a, unsigned b) { (void)a; (void)b; }
static void diagnostics_record_core1_loop(void) {}
static void diagnostics_watchdog_task_heartbeat(unsigned task) { (void)task; }
static uint32_t app_realtime_cycle_now(void) { return 0; }
static void app_realtime_schedule_write_begin(void) {}
static void app_realtime_schedule_write_end(void) {}
static void tdma_service_timing_phase_begin(void)
{ assert(!phase_open); phase_open=true; phase_order=0; ++mandatory_calls; }
static void tdma_service_timing_phase_end(void)
{ assert(phase_open && (phase_order==4 || phase_order==5)); phase_open=false; }
static void tdma_service_timing_context(unsigned context, bool active)
{ (void)context; (void)active; }
static unsigned tdma_runtime_owner_timing_context(void) { return 0; }
static unsigned board_identity_get_no(void) { return 1; }
static void tdma_component_core1_service(void)
{ assert(phase_open && phase_order==0); phase_order=1; }
static void sync_io_logic_analyzer_service_core1(unsigned budget)
{ assert(budget==8 && phase_open && phase_order==1); phase_order=2; }
static uint64_t tdma_service_timing_now(void) { assert(phase_open); return 0; }
static void tdma_service_timing_record(unsigned kind, uint64_t start)
{ (void)kind; (void)start; assert(phase_open); }
static void trigger_sequence_service_service(void)
{
    if (offline_p3 || offline_training) assert(!phase_open);
    else { assert(phase_open && (phase_order==2 || phase_order==4)); ++phase_order; }
    ++sequence_calls;
    production_sequence_service();
    if (hw.pending) physical_write();
}
static bool trigger_sequence_link_service(void)
{
    assert(phase_open && phase_order==3 && !offline_p3 && !offline_training);
    phase_order=4;
    if (!link_step) return false;
    link_step=false;
    assert(trigger_sequence_service_step()==TRIGGER_SEQUENCE_SERVICE_OK);
    return true;
}
static void app_realtime_vdc_phase(void) { ++optional_calls; }
static void app_realtime_dpll_phase(void) { ++optional_calls; }
static void app_realtime_calibration_phase(void) { ++optional_calls; }
static void app_realtime_sync_capture_phase(void) { ++optional_calls; }
static void app_realtime_refmem_phase(void) { ++optional_calls; }
static void app_realtime_model_phase(void) { ++optional_calls; }
static void app_realtime_sync_trigger_phase(void) { ++optional_calls; }
static void app_realtime_trigger_measure_phase(void) { ++optional_calls; }
static bool app_realtime_run_phase(uint32_t epoch, int phase, int load, void (*fn)(void))
{ (void)epoch; (void)phase; if (load < 0 || loads_enabled) fn(); return true; }
'''
    harness += tdma_phase + "\n" + dispatch
    harness += r'''
int main(void)
{
    assert(sequence_lifecycle_test_main() == 0);
    for (unsigned mode = 0; mode < 5; ++mode) {
        setup();
        offline_p3 = mode == 1;
        offline_training = mode == 2;
        ring_capture = mode == 3;
        loads_enabled = mode == 4;
        mandatory_calls = optional_calls = 0;
        sequence_calls = 0;
        assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_OK);
        assert(status().state == TRIGGER_SEQUENCE_SERVICE_STARTING);
        app_realtime_run_once();
        assert(status().state == TRIGGER_SEQUENCE_SERVICE_READY && writes == 0);
        assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
        app_realtime_run_once();
        assert(status().state == TRIGGER_SEQUENCE_SERVICE_RUNNING && writes == 1);
        assert(trigger_sequence_service_stop() == TRIGGER_SEQUENCE_SERVICE_OK);
        app_realtime_run_once();
        assert(status().state == TRIGGER_SEQUENCE_SERVICE_IDLE && output == 0);
        assert(status().cancelled == 1 && !reserved);
        assert(mandatory_calls != 0);
        assert(sequence_calls == 3 && !phase_open);
        assert(loads_enabled ? optional_calls != 0 : optional_calls == 0);
    }
    /* An event admitted by the coordinator reaches the IO owner in the same
     * mandatory phase, even when every optional service is disabled. */
    setup();
    offline_p3=offline_training=ring_capture=loads_enabled=false;
    start();
    sequence_calls=0;
    link_step=true;
    app_realtime_run_once();
    assert(!link_step && sequence_calls==2 && writes==1);
    assert(status().state==TRIGGER_SEQUENCE_SERVICE_RUNNING);
    stop();
    puts("mandatory sequence dispatch passed");
    return 0;
}
'''
    source = tmp_path / "dispatch.c"
    source.write_text(harness, encoding="utf-8")
    executable = tmp_path / "dispatch.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(tmp_path),
               "-I" + str(ROOT / "components/sync_trigger/inc"),
               "-I" + str(ROOT / "components/sync_io/inc"),
               "-I" + str(ROOT / "osal/inc"),
               "-I" + str(ROOT / "third_party/portable_ota/include"),
               str(source),
               str(ROOT / "components/sync_trigger/src/trigger_sequence_config.c"),
               str(ROOT / "third_party/portable_ota/src/pota_crc32.c"),
               "-o", str(executable)]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "mandatory sequence dispatch passed" in result.stdout
