"""Exercise the shared admission CAS and real public mutator wrappers."""
from pathlib import Path
import re

from test_sync_io_workspace import compile_run, function

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/sync_io/src"


def test_run_reservation_excludes_every_legacy_wrapper_and_existing_owner(tmp_path):
    model = (SRC / "sync_io_model_sched.c").read_text(encoding="utf-8")
    enum = re.search(r"typedef enum \{\n    SYNC_IO_SCHEDULE_IDLE.*?} sync_io_schedule_phase_t;", model, re.S)[0]
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sync_io.h"
#include "sync_io_mode_seq_step.h"
#include "sync_io_mode_enc_count.h"
static unsigned core, calls;
static bool initialized=true, capture, sequence, encoder, pwm;
static struct { bool running; } s_model_pulse;
static bool s_wave_output_manager_active, s_wave_output_sm_claimed;
static bool s_wave_output_dma_claimed, s_wave_output_program_loaded;
static uint32_t s_schedule_phase;
static uintptr_t s_run_output_token;
static uintptr_t s_reference_token;
#define get_core_num() core
#define sync_io_core_initialized() initialized
#define sync_io_core_capture_is_running() capture
#define sync_io_seq_step_is_running() sequence
#define sync_io_enc_count_is_running() encoder
#define sync_io_core_sma_frequency_output_active() pwm
'''
    harness += enum + "\n"
    for name in ["sync_io_schedule_reserve", "sync_io_schedule_publish_phase",
                 "sync_io_core_legacy_try_enter", "sync_io_core_legacy_leave",
                 "sync_io_core_wave_output_persona_active",
                 "sync_io_core_wave_output_persona_active_owned",
                 "sync_io_core_run_output_reserve", "sync_io_core_run_output_held",
                 "sync_io_core_run_output_release"]:
        harness += function(model, name)
    wrappers = {
        "sync_io.c": ["sync_io_start_capture", "sync_io_stop_capture",
                      "sync_io_debug_set_output_mask", "sync_io_debug_release_output_mask",
                      "sync_io_sma_frequency_tx_start", "sync_io_sma_frequency_tx_stop",
                      "sync_io_sma_frequency_rx_measure"],
        "sync_io_mode_seq_step.c": ["sync_io_seq_step_arm", "sync_io_seq_step_disarm"],
        "sync_io_mode_enc_count.c": ["sync_io_enc_count_arm", "sync_io_enc_count_disarm"],
    }
    for filename, names in wrappers.items():
        source = (SRC / filename).read_text(encoding="utf-8")
        for name in names:
            body = function(source, name)
            signature = body.split("\n{", 1)[0]
            owned_sig = "static " + signature.replace(name + "(", name + "_owned(")
            params = owned_sig.split("(", 1)[1].rsplit(")", 1)[0]
            ignore = "".join("(void)" + re.findall(r"\w+", p)[-1] + ";"
                             for p in params.split(",") if p.strip() != "void")
            stub = owned_sig + "\n{ " + ignore + "++calls;"
            if signature.startswith("bool "):
                stub += "return true;"
            harness += stub + " }\n" + body
    harness += r'''
static void all_mutators(bool expected) {
    assert(sync_io_start_capture(1000) == expected);
    sync_io_stop_capture();
    assert(sync_io_debug_set_output_mask(15) == expected);
    sync_io_debug_release_output_mask();
    assert(sync_io_sma_frequency_tx_start(1, 1000000, NULL) == expected);
    sync_io_sma_frequency_tx_stop();
    assert(sync_io_sma_frequency_rx_measure(1, 100, NULL) == expected);
    assert(sync_io_seq_step_arm(NULL, 1, 1, 0, 0, false) == expected);
    sync_io_seq_step_disarm();
    assert(sync_io_enc_count_arm(1, 20, 16) == expected);
    sync_io_enc_count_disarm();
}
int main(void) {
    static int token, wrong;
    assert(!sync_io_core_run_output_reserve(NULL));
    core=1; assert(!sync_io_core_run_output_reserve(&token)); core=0;
    bool *owners[] = { &s_model_pulse.running, &s_wave_output_manager_active,
        &s_wave_output_sm_claimed, &s_wave_output_dma_claimed,
        &s_wave_output_program_loaded, &capture, &sequence, &encoder, &pwm };
    for (unsigned i=0; i<sizeof owners/sizeof owners[0]; ++i) {
        *owners[i]=true;
        assert(!sync_io_core_run_output_reserve(&token));
        assert(s_schedule_phase==SYNC_IO_SCHEDULE_IDLE);
        *owners[i]=false;
    }
    initialized=false; assert(!sync_io_core_run_output_reserve(&token)); initialized=true;
    assert(sync_io_core_legacy_try_enter());
    /* Same core reentry and other core contention both fail: no depth bypass. */
    assert(!sync_io_core_legacy_try_enter());
    assert(sync_io_core_wave_output_persona_active());
    assert(!sync_io_core_wave_output_persona_active_owned());
    assert(!sync_io_core_run_output_reserve(&token));
    core=1; assert(!sync_io_core_legacy_try_enter()); core=0;
    sync_io_core_legacy_leave();
    for (uint32_t phase=SYNC_IO_SCHEDULE_FIXED_PREPARING;
         phase<=SYNC_IO_SCHEDULE_FIXED_SERVICE; ++phase) {
        s_schedule_phase=phase;
        assert(!sync_io_core_run_output_reserve(&token));
        all_mutators(false); assert(calls==0);
    }
    s_schedule_phase=SYNC_IO_SCHEDULE_IDLE;
    assert(sync_io_core_run_output_reserve(&token));
    assert(sync_io_core_run_output_held(&token));
    assert(!sync_io_core_run_output_held(&wrong));
    assert(!sync_io_core_run_output_reserve(&token));
    assert(!sync_io_core_run_output_release(&wrong));
    all_mutators(false); assert(calls==0);
    core=1;
    assert(sync_io_core_run_output_held(&token));
    assert(!sync_io_core_run_output_release(&token));
    all_mutators(false); assert(calls==0);
    core=0;
    sync_io_core_legacy_leave(); /* cannot clear RUN */
    assert(sync_io_core_run_output_held(&token));
    assert(sync_io_core_run_output_release(&token));
    assert(!sync_io_core_run_output_held(&token));
    assert(!sync_io_core_run_output_release(&token));
    all_mutators(true); assert(calls==11);
    assert(s_schedule_phase==SYNC_IO_SCHEDULE_IDLE);
    assert(sync_io_core_run_output_reserve(&wrong));
    assert(!sync_io_core_run_output_release(&token));
    assert(sync_io_core_run_output_release(&wrong));
    return 0;
}
'''
    compile_run(tmp_path, harness)


def test_nested_mutators_use_owned_helpers_without_reentering_gate():
    for filename, parent, children in [
        ("sync_io.c", "sync_io_start_capture_owned", ["sync_io_stop_capture"]),
        ("sync_io.c", "sync_io_sma_frequency_tx_start_owned",
         ["sync_io_sma_frequency_tx_stop", "sync_io_debug_set_output_mask"]),
        ("sync_io.c", "sync_io_sma_frequency_tx_stop_owned", ["sync_io_debug_release_output_mask"]),
        ("sync_io_mode_seq_step.c", "sync_io_seq_step_arm_owned", ["sync_io_seq_step_disarm"]),
        ("sync_io_mode_enc_count.c", "sync_io_enc_count_arm_owned", ["sync_io_enc_count_disarm"]),
    ]:
        body = function((SRC / filename).read_text(encoding="utf-8"), parent)
        assert "sync_io_core_legacy_try_enter" not in body
        for child in children:
            assert child + "_owned(" in body
            assert not re.search(r"\b" + child + r"\(", body)
