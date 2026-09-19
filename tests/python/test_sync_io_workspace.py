"""Exercise production ownership paths with simulated hardware side effects."""
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/sync_io/src"


def function(source: str, name: str) -> str:
    start = re.search(rf"(?m)^(?:static )?(?:bool|void|size_t) {name}\([^;]*?\n\{{", source)
    assert start, name
    end = source.index("\n}\n", start.start()) + 3
    return source[start.start():end]


def compile_run(tmp_path: Path, source: str, *, analyzer: bool = False) -> None:
    cc = shutil.which("gcc")
    assert cc, "host gcc is required for workspace ownership checks"
    if "vdc_timestamp_clock_try_read_ns" in source and not re.search(
            r"vdc_timestamp_clock_try_read_ns\s*\([^)]*\)\s*\{", source):
        source = ("#include <stdbool.h>\n#include <stdint.h>\n"
                  "#define BOARD_SYS_CLOCK_HZ 250000000u\n"
                  "static __attribute__((unused)) bool vdc_timestamp_clock_try_read_ns(uint32_t hz, uint64_t *out) {\n"
                      "if (!out || hz != BOARD_SYS_CLOCK_HZ) return false;\n"
                      "*out=1000000000ull; return true; }\n" + source)
    harness = tmp_path / "workspace.c"
    harness.write_text(source, encoding="utf-8")
    includes = ["tests/unit/host_stubs", "boards/rp2350_trig/inc",
                "components/tdma/inc", "components/sync_io/inc",
                "components/sync_io/src", "components/resource_arbiter/inc", "osal/inc"]
    command = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
               *("-I" + str(ROOT / path) for path in includes), str(harness),
               str(SRC / "sync_io_persona_resources.c")]
    if analyzer:
        command += [str(SRC / "sync_io_persona_manager.c"),
                    str(SRC / "sync_io_analyzer_burst.c"),
                    str(ROOT / "components/resource_arbiter/src/resource_arbiter.c")]
    executable = tmp_path / "workspace.exe"
    built = subprocess.run(command + ["-o", str(executable)], capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True)
    assert ran.returncode == 0, ran.stdout + ran.stderr


def test_capture_rejection_idle_stop_and_failed_restart(tmp_path: Path) -> None:
    source = (SRC / "sync_io.c").read_text(encoding="utf-8")
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "sync_io_persona_resources.h"
static __attribute__((unused)) bool vdc_timestamp_clock_try_read_ns(uint32_t hz, uint64_t *out) {
    if (!out || hz != 250000000u) return false;
    *out = 1000000000ull;
    return true;
}
#define BOARD_SYS_CLOCK_HZ 250000000u
static struct {
    bool initialized, capture_running, capture_timebase_valid;
    bool capture_dma_write_index_valid;
    uint32_t capture_sample_hz, dropped_capture_words;
    uint32_t capture_dma_read_seq, capture_dma_produced_seq;
    uint32_t capture_dma_last_write_index;
    uint64_t capture_timebase_start_ns;
} s_sync_io;
static unsigned hardware_writes, aborts;
static bool dma_config_ok = true;
#define BOARD_SYNC_PIO_FAST 0u
#define BOARD_SYNC_CAPTURE_SM 0u
#define SYNC_IO_TRACE_CAPTURE_FAIL 0u
#define SYNC_IO_TRACE_CAPTURE_START 0u
#define SYNC_IO_TRACE_CAPTURE_STOP 0u
#define SYNC_IO_TRACE_ERROR 0u
#define SYNC_IO_TRACE_INFO 0u
#define sync_io_trace(...) ((void)0)
#define pio_sm_set_enabled(...) (++hardware_writes)
#define pio_sm_clear_fifos(...) (++hardware_writes)
#define pio_sm_set_clkdiv(...) (++hardware_writes)
#define pio_sm_restart(...) (++hardware_writes)
#define dma_start_channel_mask(...) (++hardware_writes)
#define dma_channel_abort(...) (++aborts)
#define sync_io_core_model_output_active() false
#define sync_io_core_wave_output_persona_active_owned() false
#define sync_io_core_legacy_try_enter() true
#define sync_io_core_legacy_leave() ((void)0)
#define sync_io_common_time_now_ns() 1234u
#define osal_critical_enter() ((void)0)
#define osal_critical_exit() ((void)0)
#define sync_io_capture_latch_reset_locked() ((void)0)
static bool sync_io_capture_dma_configure(void) {
    ++hardware_writes;
    return dma_config_ok;
}
'''
    harness += function(source, "sync_io_stop_capture_owned")
    harness += function(source, "sync_io_stop_capture")
    harness += function(source, "sync_io_start_capture_owned")
    harness += function(source, "sync_io_start_capture")
    harness += r'''
int main(void) {
    static int frozen_burst;
    s_sync_io.initialized = true;
    assert(sync_io_workspace_claim(&frozen_burst));
    assert(!sync_io_start_capture(10000000u));
    sync_io_stop_capture();
    assert(hardware_writes == 0u && aborts == 0u);
    assert(sync_io_workspace_held_by(&frozen_burst));
    assert(sync_io_workspace_release(&frozen_burst));
    dma_config_ok = false;
    assert(!sync_io_start_capture(10000000u));
    assert(!sync_io_workspace_held_by(&s_sync_io));
    assert(sync_io_workspace_claim(&frozen_burst));
    assert(sync_io_workspace_release(&frozen_burst));
    dma_config_ok = true;
    assert(sync_io_start_capture(10000000u));
    assert(s_sync_io.capture_running);
    assert(sync_io_workspace_held_by(&s_sync_io));
    assert(!sync_io_workspace_claim(&frozen_burst));
    assert(sync_io_start_capture(1000000u));
    assert(s_sync_io.capture_sample_hz == 1000000u);
    sync_io_stop_capture();
    assert(!s_sync_io.capture_running && !s_sync_io.capture_timebase_valid);
    assert(sync_io_workspace_claim(&frozen_burst));
    unsigned before = aborts;
    sync_io_stop_capture();
    assert(aborts == before && sync_io_workspace_held_by(&frozen_burst));
    assert(sync_io_workspace_release(&frozen_burst));
    return 0;
}
'''
    compile_run(tmp_path, harness)


def test_analyzer_cleanup_reentry_and_export_lifetime(tmp_path: Path) -> None:
    source = (SRC / "sync_io_logic_analyzer.c").read_text(encoding="utf-8")
    # Include the actual host control/export implementation, then replace only
    # host-only persona stubs with the actual target callbacks and begin/end.
    harness = r'''
#include <assert.h>
#include "resource_arbiter.h"
#define sync_io_logic_analyzer_persona_begin unused_host_persona_begin
#define sync_io_logic_analyzer_persona_end unused_host_persona_end
#define sync_io_logic_analyzer_hw_arm unused_host_hw_arm
#define sync_io_logic_analyzer_hw_start unused_host_hw_start
#define sync_io_logic_analyzer_hw_stop unused_host_hw_stop
#include "sync_io_logic_analyzer.c"
#undef sync_io_logic_analyzer_persona_begin
#undef sync_io_logic_analyzer_persona_end
#undef sync_io_logic_analyzer_hw_arm
#undef sync_io_logic_analyzer_hw_start
#undef sync_io_logic_analyzer_hw_stop
void osal_critical_enter(void) {}
void osal_critical_exit(void) {}
static struct {
    sync_io_logic_analyzer_raw_capture_t *capture;
    bool program_loaded;
    unsigned offset;
} s_hw;
static unsigned stops;
static bool fail_arm;
#define pio_sm_set_enabled(...) (++stops)
#define dma_channel_abort(...) ((void)0)
#define pio_remove_program(...) ((void)0)
bool sync_io_logic_analyzer_hw_arm(sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *records, uint32_t capacity,
    const sync_io_logic_analyzer_config_t *config) {
    (void)records; (void)capacity; (void)config;
    if (fail_arm || !sync_io_workspace_claim(capture)) return false;
    s_hw.capture = capture;
    return true;
}
bool sync_io_logic_analyzer_hw_start(void) { return s_hw.capture != NULL; }
'''
    harness += function(source, "sync_io_logic_analyzer_hw_stop")
    for name in ("sync_io_logic_analyzer_persona_load", "sync_io_logic_analyzer_persona_arm",
                 "sync_io_logic_analyzer_persona_start", "sync_io_logic_analyzer_persona_stop",
                 "sync_io_logic_analyzer_persona_cleanup", "sync_io_logic_analyzer_persona_begin",
                 "sync_io_logic_analyzer_persona_end"):
        harness += function(source, name)
    harness += r'''
int main(void) {
    static int next_owner;
    sync_io_logic_analyzer_persona_t persona = {0};
    sync_io_logic_analyzer_raw_capture_t a = {0}, b = {0};
    sync_io_logic_analyzer_record_t records[2] = {0}, drained[2];
    sync_io_logic_analyzer_config_t config = {
        .contract_version = SYNC_IO_LOGIC_ANALYZER_CONTRACT_VERSION,
        .mode = SYNC_IO_LOGIC_ANALYZER_MODE_RAW_SAMPLE, .max_records = 2u,
        .sample_period_ns = 1000u,
        .timeout_us = 1000u,
        .expected_profile_generation = 1u,
        .expected_persona_generation = 1u,
        .source_mask = 1u,
    };
    const sync_io_persona_descriptor_t *descriptor =
        sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_LOGIC_ANALYZER);
    config.source_mask = descriptor->gpio_read_mask & (0u - descriptor->gpio_read_mask);
    assert(sync_io_logic_analyzer_config_valid(&config));
    assert(resource_arbiter_init());
    /* Direct hardware owner A survives a rejected persona B and its cleanup. */
    assert(sync_io_logic_analyzer_hw_arm(&a, records, 2u, &config));
    assert(!sync_io_logic_analyzer_persona_begin(&persona, &b, records, 2u, &config));
    assert(s_hw.capture == &a && stops == 0u && sync_io_workspace_held_by(&a));
    sync_io_logic_analyzer_hw_stop();
    assert(sync_io_logic_analyzer_persona_begin(&persona, &a, records, 2u, &config));
    sync_io_logic_analyzer_persona_t saved = persona;
    assert(!sync_io_logic_analyzer_persona_begin(&persona, &b, records, 2u, &config));
    assert(memcmp(&saved, &persona, sizeof(saved)) == 0);
    assert(s_hw.capture == &a && sync_io_workspace_held_by(&a));
    sync_io_logic_analyzer_persona_end(&persona);
    fail_arm = true;
    assert(!sync_io_logic_analyzer_persona_begin(&persona, &a, records, 2u, &config));
    fail_arm = false;
    assert(sync_io_logic_analyzer_persona_begin(&persona, &a, records, 2u, &config));
    sync_io_logic_analyzer_persona_end(&persona);
    /* Both an empty capture and a fully consumed capture retire their lease. */
    for (uint32_t count = 0; count != 3; ++count) {
        memset(&s_control, 0, sizeof(s_control));
        s_control.capture.initialized = true;
        s_control.capture.produced_records = count;
        s_control.capture.consumed_records = count;
        assert(sync_io_workspace_claim(&s_control.capture));
        sync_io_logic_analyzer_publish_shadow();
        assert(sync_io_workspace_claim(&next_owner));
        assert(sync_io_workspace_release(&next_owner));
    }
    memset(&s_control, 0, sizeof(s_control));
    s_control.capture.initialized = true;
    s_control.capture.capacity = 2u;
    s_control.capture.records = records;
    s_control.capture.produced_records = 2u;
    assert(sync_io_workspace_claim(&s_control.capture));
    s_hw.capture = &s_control.capture;
    sync_io_logic_analyzer_hw_stop();
    assert(sync_io_workspace_held_by(&s_control.capture));
    sync_io_logic_analyzer_publish_shadow();
    assert(!sync_io_workspace_claim(&next_owner));
    assert(sync_io_logic_analyzer_drain_core0(drained, 1u) == 1u);
    assert(!sync_io_workspace_claim(&next_owner));
    assert(sync_io_logic_analyzer_drain_core0(drained, 1u) == 1u);
    assert(sync_io_workspace_claim(&next_owner));
    assert(sync_io_workspace_release(&next_owner));
    return 0;
}
'''
    compile_run(tmp_path, harness, analyzer=True)


def test_schedule_bounds_rejection_and_private_observer(tmp_path: Path) -> None:
    source = (SRC / "sync_io_model_sched.c").read_text(encoding="utf-8")
    model_type = source[source.index("typedef struct {"):
                        source.index("} sync_io_model_pulse_t;") + len("} sync_io_model_pulse_t;")]
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "sync_io_persona_resources.h"
typedef unsigned uint;
typedef struct { uint32_t txf[4]; } fake_pio_t;
typedef fake_pio_t *PIO;
typedef struct { unsigned value; } pio_program_t;
typedef struct { unsigned value; } dma_channel_config;
typedef struct { uint32_t delay_us, high_us; } sync_io_model_pulse_entry_t;
typedef struct { uint32_t delay_ns, high_ns; } sync_io_model_pulse_entry_ns_t;
static fake_pio_t fake_pio;
'''
    harness += model_type + r'''
static sync_io_model_pulse_t s_model_pulse;
static uint32_t sync_io_shared_workspace[SYNC_IO_SHARED_WORKSPACE_WORDS];
static uint32_t s_phase_observer_words[SYNC_IO_MODEL_PULSE_WORDS_PER_ENTRY];
static bool capture_running, manager_ok = true, s_wave_output_manager_active;
static unsigned manager_starts;
static pio_program_t sync_model_sched_pulse_high_program, sync_model_sched_pulse_low_program;
#define BOARD_SYNC_PIO_FAST (&fake_pio)
#define BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM 1u
#define BOARD_SYNC_PIO0_WAVE_OUTPUT_SM 2u
#define BOARD_SYNC_MODEL_SCHED_SM 3u
#define SYNC_IO_MODEL_PULSE_DEFAULT_TICK_PERIOD_NS 100u
#define SYNC_IO_TRACE_MODEL_FAIL 0u
#define SYNC_IO_TRACE_MODEL_ARM 0u
#define SYNC_IO_TRACE_ERROR 0u
#define SYNC_IO_TRACE_INFO 0u
#define DMA_SIZE_32 0u
#define LOG_INFO(...) ((void)0)
#define sync_io_core_trace(a,b,c,d) ((void)(a),(void)(b),(void)(c),(void)(d))
#define sync_io_core_initialized() true
#define sync_io_core_capture_is_running() capture_running
#define time_us_64() 0u
#define sync_io_model_delay_ticks_for_duration(ns,tick) ((ns)/(tick))
#define sync_io_model_high_ticks_for_duration(ns,tick) ((ns)/(tick))
#define sync_io_model_delay_word(ticks) (ticks)
#define sync_io_model_high_word(ticks) (ticks)
#define sync_io_model_saturate_u64_to_u32(value) ((uint32_t)(value))
#define sync_io_model_clkdiv_for_tick_rate(value) (value)
#define sync_io_model_tick_hz_from_period_ns(value) (value)
#define pio_can_add_program(pio,program) ((void)(pio),(void)(program),true)
#define pio_add_program(pio,program) ((void)(pio),(void)(program),1u)
#define pio_sm_set_enabled(pio,sm,on) ((void)(pio),(void)(sm),(void)(on))
#define pio_sm_clear_fifos(pio,sm) ((void)(pio),(void)(sm))
#define pio_sm_restart(pio,sm) ((void)(pio),(void)(sm))
#define sync_model_sched_pulse_program_init(a,b,c,d,e,f) \
    ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e),(void)(f))
#define dma_channel_abort(ch) ((void)(ch))
#define dma_channel_set_irq0_enabled(ch,on) ((void)(ch),(void)(on))
#define dma_channel_get_default_config(ch) ((void)(ch),(dma_channel_config){0})
#define channel_config_set_transfer_data_size(a,b) ((void)(a),(void)(b))
#define channel_config_set_read_increment(a,b) ((void)(a),(void)(b))
#define channel_config_set_write_increment(a,b) ((void)(a),(void)(b))
#define channel_config_set_dreq(a,b) ((void)(a),(void)(b))
#define dma_channel_configure(a,b,c,d,e,f) \
    ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e),(void)(f))
static void sync_io_model_pulse_schedule_disarm(void) {
    (void)sync_io_workspace_release(&s_model_pulse);
    memset(&s_model_pulse, 0, sizeof(s_model_pulse));
    s_wave_output_manager_active = false;
}
#define sync_io_model_pulse_schedule_disarm_owned sync_io_model_pulse_schedule_disarm
static bool sync_io_wave_output_manager_start(sync_io_persona_id_t id) {
    (void)id;
    ++manager_starts;
    s_wave_output_manager_active = manager_ok;
    return manager_ok;
}
'''
    phase_type = source[source.index("typedef enum {"):
                        source.index("} sync_io_schedule_phase_t;") +
                        len("} sync_io_schedule_phase_t;")]
    harness += phase_type + "\nstatic uint32_t s_schedule_phase;\nstatic uintptr_t s_reference_token;\n"
    harness += function(source, "sync_io_schedule_reserve")
    harness += function(source, "sync_io_schedule_publish_phase")
    harness += function(source, "sync_io_pulse_schedule_arm_on_pin_common_owned")
    harness += function(source, "sync_io_pulse_schedule_arm_on_pin_common")
    harness += r'''
static bool arm(unsigned count, unsigned sm, unsigned high_ns) {
    return sync_io_pulse_schedule_arm_on_pin_common(BOARD_SYNC_PIO_FAST, sm,
        0u, 0u, 0u, NULL, NULL, 0u, 0u, 1000u, high_ns, count, true, 100u);
}
int main(void) {
    static int capture, burst;
    memset(sync_io_shared_workspace, 0xA5, sizeof(sync_io_shared_workspace));
    assert(sync_io_workspace_claim(&burst));
    assert(!arm(2u, BOARD_SYNC_PIO0_WAVE_OUTPUT_SM, 500u));
    assert(manager_starts == 0u);
    for (unsigned i=0; i<SYNC_IO_SHARED_WORKSPACE_WORDS; ++i)
        assert(sync_io_shared_workspace[i] == 0xA5A5A5A5u);
    assert(sync_io_workspace_held_by(&burst));
    assert(sync_io_workspace_release(&burst));
    assert(!arm(SYNC_IO_MODEL_PULSE_MAX_ENTRIES + 1u,
                BOARD_SYNC_PIO0_WAVE_OUTPUT_SM, 500u));
    assert(!arm(2u, BOARD_SYNC_PIO0_WAVE_OUTPUT_SM, 1u));
    assert(sync_io_workspace_claim(&burst));
    assert(sync_io_workspace_release(&burst));
    manager_ok = false;
    assert(!arm(2u, BOARD_SYNC_PIO0_WAVE_OUTPUT_SM, 500u));
    assert(sync_io_workspace_claim(&burst));
    assert(sync_io_workspace_release(&burst));
    manager_ok = true;
    assert(arm(SYNC_IO_MODEL_PULSE_MAX_ENTRIES,
               BOARD_SYNC_PIO0_WAVE_OUTPUT_SM, 500u));
    assert(s_model_pulse.words == sync_io_shared_workspace);
    assert(sync_io_shared_workspace[SYNC_IO_SHARED_WORKSPACE_WORDS-1u] == 5u);
    assert(sync_io_workspace_held_by(&s_model_pulse));
    sync_io_model_pulse_schedule_disarm();
    assert(sync_io_workspace_claim(&capture));
    capture_running = true;
    assert(arm(1u, BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM, 500u));
    assert(s_model_pulse.words == s_phase_observer_words);
    assert(sync_io_workspace_held_by(&capture));
    sync_io_model_pulse_schedule_disarm();
    assert(sync_io_workspace_held_by(&capture));
    assert(!arm(2u, BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM, 500u));
    assert(sync_io_workspace_release(&capture));
    return 0;
}
'''
    compile_run(tmp_path, harness)
